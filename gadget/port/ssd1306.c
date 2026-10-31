// Minimal SSD1306 128x64 OLED driver over the ESP-IDF new I2C master API.
#include "ssd1306.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ssd1306";
static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_fb[SSD1306_W * SSD1306_H / 8];   // 8 pages of 128 bytes; bit y&7 of page y/8
static bool s_ready = false;

static bool cmd(uint8_t c) {
    uint8_t b[2] = {0x00, c};   // 0x00 = command stream
    return i2c_master_transmit(s_dev, b, 2, 100) == ESP_OK;
}

bool ssd1306_ready(void) { return s_ready; }

bool ssd1306_init(int sda_gpio, int scl_gpio, uint8_t addr) {
    if (s_ready) return true;
    i2c_master_bus_config_t bc = {
        .i2c_port = -1,                       // pick a free port
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    bc.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bc, &s_bus) != ESP_OK) { ESP_LOGW(TAG, "i2c bus init failed"); return false; }
    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &dc, &s_dev) != ESP_OK) { ESP_LOGW(TAG, "i2c add dev failed"); return false; }
    if (i2c_master_probe(s_bus, addr, 100) != ESP_OK) { ESP_LOGW(TAG, "no SSD1306 at 0x%02x", addr); return false; }

    static const uint8_t seq[] = {
        0xAE,             // display off
        0x20, 0x00,       // horizontal addressing mode
        0xB0,             // page start
        0xC8,             // COM scan direction remapped
        0x00, 0x10,       // low/high column
        0x40,             // start line 0
        0x81, 0x7F,       // contrast
        0xA1,             // segment remap
        0xA6,             // normal (not inverted)
        0xA8, 0x3F,       // multiplex 1/64
        0xA4,             // output follows RAM
        0xD3, 0x00,       // no display offset
        0xD5, 0x80,       // clock divide
        0xD9, 0xF1,       // pre-charge
        0xDA, 0x12,       // COM pins (128x64)
        0xDB, 0x40,       // vcom detect
        0x8D, 0x14,       // charge pump on
        0xAF,             // display on
    };
    for (size_t i = 0; i < sizeof seq; i++) if (!cmd(seq[i])) { ESP_LOGW(TAG, "init write failed"); return false; }
    memset(s_fb, 0, sizeof s_fb);
    s_ready = true;
    ssd1306_flush();
    ESP_LOGI(TAG, "SSD1306 128x64 up on SDA %d SCL %d addr 0x%02x", sda_gpio, scl_gpio, addr);
    return true;
}

void ssd1306_set_flip(bool flip) {
    if (!s_ready) return;
    cmd(flip ? 0xA0 : 0xA1);   // segment remap (horizontal); 0xA1 is the default from init
    cmd(flip ? 0xC0 : 0xC8);   // COM scan direction (vertical); 0xC8 is the default from init
}

void ssd1306_clear(void) { memset(s_fb, 0, sizeof s_fb); }

void ssd1306_px(int x, int y, bool on) {
    if (x < 0 || x >= SSD1306_W || y < 0 || y >= SSD1306_H) return;
    uint8_t *cell = &s_fb[(y >> 3) * SSD1306_W + x];
    uint8_t mask = 1 << (y & 7);
    if (on) *cell |= mask; else *cell &= ~mask;
}

void ssd1306_fill_rect(int x, int y, int w, int h, bool on) {
    for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) ssd1306_px(x + c, y + r, on);
}

void ssd1306_blit_rgb565(int x, int y, int w, int h, const uint16_t *px) {
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            uint16_t v = px[r * w + c];
            v = (uint16_t)((v >> 8) | (v << 8));   // stored high byte first; make it native
            int R = ((v >> 11) & 0x1F) << 3;
            int G = ((v >> 5) & 0x3F) << 2;
            int B = (v & 0x1F) << 3;
            int lum = (R * 77 + G * 150 + B * 29) >> 8;
            ssd1306_px(x + c, y + r, lum >= 128);
        }
    }
}

static void flush_pages(int p0, int p1) {
    if (!s_ready) return;
    if (p0 < 0) p0 = 0;
    if (p1 > 8) p1 = 8;
    for (int p = p0; p < p1; p++) {
        cmd(0xB0 | p);   // page
        cmd(0x00);       // low column 0
        cmd(0x10);       // high column 0
        uint8_t buf[1 + SSD1306_W];
        buf[0] = 0x40;   // data stream
        memcpy(buf + 1, &s_fb[p * SSD1306_W], SSD1306_W);
        i2c_master_transmit(s_dev, buf, sizeof buf, 100);
    }
}

void ssd1306_flush(void) { flush_pages(0, 8); }
void ssd1306_flush_rows(int y0, int y1) { flush_pages(y0 >> 3, ((y1 - 1) >> 3) + 1); }

void ssd1306_contrast(uint8_t c) { if (s_ready) { cmd(0x81); cmd(c); } }
