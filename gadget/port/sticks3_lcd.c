// ST7789 LCD + M5PM1 power bring-up for the M5Stack StickS3. See sticks3_lcd.h.
// Pins and the PMIC power sequence are from M5's K150 schematic, matching the
// values the Muse SDK's own board profile uses for this board.
#include "sticks3_lcd.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "sticks3_lcd";

#define LCD_GAP_X 52               // the 135x240 window in the controller's 240x320
#define LCD_GAP_Y 40
#define LCD_HOST  SPI3_HOST
#define LCD_SCLK  40
#define LCD_MOSI  39
#define LCD_CS    41
#define LCD_DC    45
#define LCD_RST   21
#define LCD_BL    38

#define I2C_SDA   47
#define I2C_SCL   48

#define PMIC_ADDR     0x6E
#define PMIC_PWR_CFG  0x06         // bit 0x08: 5 V out; bit 0x10: green status LED
#define PMIC_PWR_5V   0x08
#define PMIC_PWR_LED  0x10
#define PMIC_GPIO_DIR 0x10
#define PMIC_GPIO_OUT 0x11
#define PMIC_GPIO_OD  0x13         // 1 = open drain (the PMIC's default)
#define PMIC_L3B      (1 << 2)     // LCD and audio power
#define PMIC_SPK      (1 << 3)     // amp enable (kept off here: no audio)
// Battery/VIN ADC and power-source registers (M5PM1, per M5Unified's driver).
#define PMIC_PWR_SRC  0x04         // bit0: 5V-in (USB) valid; bit2: VBAT node valid
#define PMIC_SRC_VIN  0x01
#define PMIC_VBAT_L   0x22         // battery voltage, 2 bytes little-endian, direct mV

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_pmic;
static esp_lcd_panel_handle_t s_panel;
static bool s_ready;
static uint16_t *s_dma;            // DMA bounce buffer for draws
static size_t s_dma_px;
static SemaphoreHandle_t s_draw_done;   // signalled when a draw's DMA finishes

// The PMIC is a microcontroller and now and then misses a transfer; retry.
static esp_err_t pmic_read(uint8_t reg, uint8_t *buf, size_t n) {
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK; i++) {
        err = i2c_master_transmit_receive(s_pmic, &reg, 1, buf, n, 50);
    }
    return err;
}

static esp_err_t pmic_write(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = {reg, val};
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3 && err != ESP_OK; i++) {
        err = i2c_master_transmit(s_pmic, buf, sizeof(buf), 50);
    }
    return err;
}

static esp_err_t pmic_update(uint8_t reg, uint8_t mask, bool on) {
    uint8_t v;
    esp_err_t err = pmic_read(reg, &v, 1);
    if (err != ESP_OK) return err;
    uint8_t want = on ? (v | mask) : (v & ~mask);
    return want == v ? ESP_OK : pmic_write(reg, want);
}

static bool pmic_power_on(void) {
    const i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) { ESP_LOGW(TAG, "i2c bus"); return false; }
    const i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PMIC_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(s_bus, &dc, &s_pmic) != ESP_OK) { ESP_LOGW(TAG, "pmic add"); return false; }

    // Drive the LCD/audio rail and amp-enable push-pull outputs; power the LCD
    // rail on, leave the amp off and the 5 V output and green LED off.
    esp_err_t err = ESP_OK;
    err |= pmic_update(PMIC_GPIO_OD, PMIC_L3B | PMIC_SPK, false);
    err |= pmic_update(PMIC_GPIO_DIR, PMIC_L3B | PMIC_SPK, true);
    err |= pmic_update(PMIC_GPIO_OUT, PMIC_SPK, false);
    err |= pmic_update(PMIC_GPIO_OUT, PMIC_L3B, true);
    err |= pmic_update(PMIC_PWR_CFG, PMIC_PWR_5V, false);
    err |= pmic_update(PMIC_PWR_CFG, PMIC_PWR_LED, false);
    if (err != ESP_OK) { ESP_LOGW(TAG, "pmic power sequence"); return false; }
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

static bool backlight_init(void) {
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t c = {
        .gpio_num = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    return ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&c) == ESP_OK;
}

// Fires in ISR when a draw_bitmap DMA transfer completes; lets sticks3_lcd_draw
// wait so the shared bounce buffer is never overwritten mid-transfer.
static bool lcd_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_draw_done, &woken);
    return woken == pdTRUE;
}

static bool panel_init(void) {
    s_draw_done = xSemaphoreCreateBinary();
    if (!s_draw_done) { ESP_LOGW(TAG, "draw sem"); return false; }
    const spi_bus_config_t bus = {
        .sclk_io_num = LCD_SCLK,
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = STICKS3_LCD_W * STICKS3_LCD_H * 2 + 16,
    };
    if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) { ESP_LOGW(TAG, "spi bus"); return false; }

    esp_lcd_panel_io_handle_t io;
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = lcd_trans_done,
    };
    if (esp_lcd_new_panel_io_spi(LCD_HOST, &io_cfg, &io) != ESP_OK) { ESP_LOGW(TAG, "panel io"); return false; }

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel) != ESP_OK) { ESP_LOGW(TAG, "st7789"); return false; }

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, LCD_GAP_X, LCD_GAP_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);
    return true;
}

// Paint the whole panel black so the power-on RAM noise never shows. Uses the
// persistent DMA path in sticks3_lcd_draw; the zero source stripe is copy-only.
static void panel_clear_black(void) {
    const int rows = 16;
    uint16_t *zero = calloc((size_t)STICKS3_LCD_W * rows, sizeof(uint16_t));
    if (!zero) return;
    for (int y = 0; y < STICKS3_LCD_H; y += rows) {
        int h = (y + rows <= STICKS3_LCD_H) ? rows : (STICKS3_LCD_H - y);
        sticks3_lcd_draw(0, y, STICKS3_LCD_W, h, zero);
    }
    free(zero);
}

bool sticks3_lcd_init(void) {
    if (s_ready) return true;
    if (!pmic_power_on()) return false;
    if (!backlight_init()) return false;
    if (!panel_init()) return false;
    s_ready = true;
    panel_clear_black();
    sticks3_lcd_brightness(80);
    ESP_LOGI(TAG, "StickS3 ST7789 %dx%d up", STICKS3_LCD_W, STICKS3_LCD_H);
    return true;
}

bool sticks3_lcd_ready(void) { return s_ready; }

bool sticks3_pmic_5v(bool on) {
    if (!s_pmic) return false;   // PMIC not up yet
    return pmic_update(PMIC_PWR_CFG, PMIC_PWR_5V, on) == ESP_OK;
}

bool sticks3_battery(uint16_t *millivolt, bool *usb) {
    if (!s_pmic) return false;   // PMIC not up yet
    uint8_t v[2];
    if (pmic_read(PMIC_VBAT_L, v, 2) != ESP_OK) return false;
    if (millivolt) *millivolt = (uint16_t)((v[1] << 8) | v[0]);   // direct mV
    if (usb) { uint8_t s = 0; *usb = pmic_read(PMIC_PWR_SRC, &s, 1) == ESP_OK && (s & PMIC_SRC_VIN); }
    return true;
}

bool sticks3_lcd_draw(int x, int y, int w, int h, const uint16_t *px) {
    if (!s_ready || !px || w <= 0 || h <= 0 || x < 0 || y < 0
        || x + w > STICKS3_LCD_W || y + h > STICKS3_LCD_H) {
        return false;
    }
    size_t need = (size_t)w * h;
    if (need > s_dma_px) {
        free(s_dma);
        s_dma = heap_caps_malloc(need * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        s_dma_px = s_dma ? need : 0;
    }
    if (!s_dma) return false;
    memcpy(s_dma, px, need * sizeof(uint16_t));
    if (esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, s_dma) != ESP_OK) return false;
    xSemaphoreTake(s_draw_done, portMAX_DELAY);   // wait for DMA before the buffer can be reused
    return true;
}

void sticks3_lcd_brightness(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

int sticks3_lcd_width(void) { return STICKS3_LCD_W; }
int sticks3_lcd_height(void) { return STICKS3_LCD_H; }
