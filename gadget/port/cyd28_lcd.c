// ILI9341 LCD backend for the LCD Wiki 2.8" ESP32-S3 Display (E32C28P). See cyd28_lcd.h.
// Pins from the vendor wiki (lcdwiki.com/2.8inch_ESP32-S3_Display_E32C28P):
//   LCD  MOSI 11, SCLK 12, CS 10, DC 46, RST shared with EN (none), backlight 45.
//   Panel is 240x320, BGR element order, colour inversion ON (vendor init sends 0x21).
// Structure mirrors sticks3_lcd.c (SPI panel + LEDC backlight + a DMA bounce buffer
// with a trans-done semaphore so draws never overwrite the buffer mid-transfer), but
// there is no PMIC on this board — the panel rail is always powered — so bring-up is
// just the SPI bus, the ILI9341 panel and the backlight.
#include "cyd28_lcd.h"
#include <string.h>
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "cyd28_lcd";

#define LCD_HOST  SPI2_HOST
#define LCD_MOSI  11
#define LCD_SCLK  12
#define LCD_CS    10
#define LCD_DC    46         // strapping pin; driven as output after boot, no external pull
#define LCD_RST   (-1)       // reset is tied to the ESP32-S3 EN line, no dedicated GPIO
#define LCD_BL    45         // backlight, high = on (also a strapping pin)

static esp_lcd_panel_handle_t s_panel;
static bool s_ready;
static uint16_t *s_dma;            // DMA bounce buffer for draws
static size_t s_dma_px;
static SemaphoreHandle_t s_draw_done;   // signalled when a draw's DMA finishes

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

// Fires in ISR when a draw_bitmap DMA transfer completes; lets cyd28_lcd_draw
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
        .max_transfer_sz = CYD28_LCD_W * CYD28_LCD_H * 2 + 16,
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
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,   // CYD panels take BGR; flip if orange shows as blue
        .bits_per_pixel = 16,
    };
    if (esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel) != ESP_OK) { ESP_LOGW(TAG, "ili9341"); return false; }

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, true);        // vendor init turns inversion on (0x21)
    esp_lcd_panel_swap_xy(s_panel, true);             // 240x320 portrait -> 320x240 landscape (case is landscape-only)
    esp_lcd_panel_mirror(s_panel, false, false);      // landscape, un-mirrored, upright (tuned on hardware)
    esp_lcd_panel_disp_on_off(s_panel, true);
    return true;
}

// Paint the whole panel black so the power-on RAM noise never shows.
static void panel_clear_black(void) {
    const int rows = 16;
    uint16_t *zero = calloc((size_t)CYD28_LCD_W * rows, sizeof(uint16_t));
    if (!zero) return;
    for (int y = 0; y < CYD28_LCD_H; y += rows) {
        int h = (y + rows <= CYD28_LCD_H) ? rows : (CYD28_LCD_H - y);
        cyd28_lcd_draw(0, y, CYD28_LCD_W, h, zero);
    }
    free(zero);
}

bool cyd28_lcd_init(void) {
    if (s_ready) return true;
    if (!backlight_init()) return false;
    if (!panel_init()) return false;
    s_ready = true;
    panel_clear_black();
    cyd28_lcd_brightness(80);
    ESP_LOGI(TAG, "CYD28 ILI9341 %dx%d up", CYD28_LCD_W, CYD28_LCD_H);
    return true;
}

bool cyd28_lcd_ready(void) { return s_ready; }

bool cyd28_lcd_draw(int x, int y, int w, int h, const uint16_t *px) {
    if (!s_ready || !px || w <= 0 || h <= 0 || x < 0 || y < 0
        || x + w > CYD28_LCD_W || y + h > CYD28_LCD_H) {
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

void cyd28_lcd_brightness(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, pct * 1023 / 100);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

int cyd28_lcd_width(void) { return CYD28_LCD_W; }
int cyd28_lcd_height(void) { return CYD28_LCD_H; }

// Shared I2C bus on IO15 (SCL) / IO16 (SDA): FT6336 touch (0x38) + ES8311 codec (0x18).
// Lazily created so it does not matter whether touch or the voice board asks first.
#define CYD28_I2C_SCL 15
#define CYD28_I2C_SDA 16
static i2c_master_bus_handle_t s_i2c_bus;
void *cyd28_i2c_bus(void) {
    if (s_i2c_bus) return (void *)s_i2c_bus;
    const i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CYD28_I2C_SDA,
        .scl_io_num = CYD28_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    if (i2c_new_master_bus(&bc, &s_i2c_bus) != ESP_OK) { ESP_LOGW(TAG, "shared I2C bus create failed"); s_i2c_bus = NULL; }
    return (void *)s_i2c_bus;
}
