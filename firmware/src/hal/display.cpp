#define LGFX_USE_V1
#include "display.h"
#include <LovyanGFX.hpp>
#include "../config.h"
#include "../core/settings.h"
#include "led.h"
#if defined(AITL_PANEL_RGB)
// The RGB-parallel bus/panel classes aren't in the default umbrella; pull them in explicitly (ESP32-S3 only).
// Must come AFTER config.h so AITL_PANEL_RGB is defined.
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#endif

#if defined(AITL_TOUCH_XPT2046)
// Resistive XPT2046 on its own pins, bit-banged (the ESP32's two SPI hosts are taken by the panel and the microSD).
// Returns raw 12-bit ADC values; LovyanGFX's Touch base class maps them with x_min/x_max calibration.
class Touch_XPT2046_BitBang : public lgfx::ITouch {
 public:
  bool init() override {
    pinMode(PIN_TOUCH_CS, OUTPUT); digitalWrite(PIN_TOUCH_CS, HIGH);
    pinMode(PIN_TOUCH_CLK, OUTPUT); digitalWrite(PIN_TOUCH_CLK, LOW);
    pinMode(PIN_TOUCH_MOSI, OUTPUT); pinMode(PIN_TOUCH_MISO, INPUT); pinMode(PIN_TOUCH_IRQ, INPUT);
    _inited = true; return true;
  }
  void wakeup() override {}
  void sleep() override {}
  bool isEnabled() const { return _inited; }
  uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
    if (!count) return 0;
    if (digitalRead(PIN_TOUCH_IRQ) == HIGH) return 0;         // pen-up
    int32_t z = read(0xB1) + (4095 - read(0xC1));
    if (z < 300) return 0;
    int32_t x = 0, y = 0;
    for (int i = 0; i < 4; i++) { x += read(0xD1); y += read(0x91); }
    read(0xD0);                                                  // power down between reads
    tp[0].x = x / 4; tp[0].y = y / 4; tp[0].size = 1; tp[0].id = 0;
    return 1;
  }
 private:
  bool _inited = false;
  uint16_t read(uint8_t cmd) {
    digitalWrite(PIN_TOUCH_CS, LOW);
    shift(cmd);
    uint16_t v = 0;
    for (int i = 0; i < 16; i++) { digitalWrite(PIN_TOUCH_CLK, HIGH); delayMicroseconds(1); v = (v << 1) | digitalRead(PIN_TOUCH_MISO); digitalWrite(PIN_TOUCH_CLK, LOW); delayMicroseconds(1); }
    digitalWrite(PIN_TOUCH_CS, HIGH);
    return (v >> 4) & 0x0FFF;
  }
  void shift(uint8_t b) {
    for (int i = 7; i >= 0; i--) { digitalWrite(PIN_TOUCH_MOSI, (b >> i) & 1); digitalWrite(PIN_TOUCH_CLK, HIGH); delayMicroseconds(1); digitalWrite(PIN_TOUCH_CLK, LOW); delayMicroseconds(1); }
  }
};
#endif

#if defined(AITL_PANEL_RGB)
// --- CYD43 (ESP32-4827S043): 480x272 RGB-parallel panel (ST7262) + GT911 touch on ESP32-S3. ---
// WIP port. The framebuffer lives in OCTAL PSRAM, so this env MUST build with PSRAM enabled.
// Pins come from boards/cyd43.h (same values the proven gadget port uses on this hardware).
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB     _bus;
  lgfx::Panel_RGB   _panel;
  lgfx::Light_PWM   _light;
  lgfx::Touch_GT911 _touch;
 public:
  LGFX() {
    { auto c = _panel.config();
      c.memory_width = TFT_NATIVE_W; c.memory_height = TFT_NATIVE_H;
      c.panel_width  = TFT_NATIVE_W; c.panel_height  = TFT_NATIVE_H;
      c.offset_x = 0; c.offset_y = 0;
      _panel.config(c); }
    { auto c = _panel.config_detail();
      c.use_psram = 1;   // framebuffer streamed from octal PSRAM (ST7262 is a dumb panel)
      _panel.config_detail(c); }
    { auto c = _bus.config();
      c.panel = &_panel;
      c.pin_d0  = PIN_RGB_D0;  c.pin_d1  = PIN_RGB_D1;  c.pin_d2  = PIN_RGB_D2;  c.pin_d3  = PIN_RGB_D3;
      c.pin_d4  = PIN_RGB_D4;  c.pin_d5  = PIN_RGB_D5;  c.pin_d6  = PIN_RGB_D6;  c.pin_d7  = PIN_RGB_D7;
      c.pin_d8  = PIN_RGB_D8;  c.pin_d9  = PIN_RGB_D9;  c.pin_d10 = PIN_RGB_D10; c.pin_d11 = PIN_RGB_D11;
      c.pin_d12 = PIN_RGB_D12; c.pin_d13 = PIN_RGB_D13; c.pin_d14 = PIN_RGB_D14; c.pin_d15 = PIN_RGB_D15;
      c.pin_henable = PIN_RGB_DE; c.pin_vsync = PIN_RGB_VSYNC; c.pin_hsync = PIN_RGB_HSYNC; c.pin_pclk = PIN_RGB_PCLK;
      c.freq_write = RGB_PCLK_HZ;
      c.hsync_polarity = 0; c.hsync_front_porch = RGB_HSYNC_FP; c.hsync_pulse_width = RGB_HSYNC_PW; c.hsync_back_porch = RGB_HSYNC_BP;
      c.vsync_polarity = 0; c.vsync_front_porch = RGB_VSYNC_FP; c.vsync_pulse_width = RGB_VSYNC_PW; c.vsync_back_porch = RGB_VSYNC_BP;
      c.pclk_active_neg = 1; c.de_idle_high = 0; c.pclk_idle_high = 0;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _light.config(); c.pin_bl = PIN_TFT_BL; c.invert = false; c.freq = 12000; c.pwm_channel = 7; _light.config(c); _panel.setLight(&_light); }
    { auto c = _touch.config();
      c.x_min = 0; c.x_max = TFT_NATIVE_W - 1; c.y_min = 0; c.y_max = TFT_NATIVE_H - 1;
      c.pin_int = -1; c.pin_rst = PIN_TOUCH_RST; c.bus_shared = false; c.offset_rotation = 0;
      c.i2c_port = 0; c.i2c_addr = TOUCH_I2C_ADDR; c.pin_sda = PIN_TOUCH_SDA; c.pin_scl = PIN_TOUCH_SCL; c.freq = 400000;
      _touch.config(c); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
  void applyPanelFlags(bool invert, bool bgr) {   // RGB panel: only rgb_order is meaningful
    auto c = _panel.config(); c.rgb_order = bgr; _panel.config(c); (void)invert;
  }
};
#else
class LGFX : public lgfx::LGFX_Device {
#if defined(AITL_PANEL_ST7796)
  lgfx::Panel_ST7796 _panel;
#elif defined(AITL_PANEL_ST7789)
  lgfx::Panel_ST7789 _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
#if defined(AITL_TOUCH_XPT2046)
  Touch_XPT2046_BitBang _touch;
#elif defined(AITL_TOUCH_XPT2046_SHARED)
  lgfx::Touch_XPT2046 _touch;
#else
  lgfx::Touch_CST816S _touch;   // CST816/CST820 share a register map
#endif
 public:
  LGFX() {
    { auto c = _bus.config();
      c.spi_host = HSPI_HOST; c.spi_mode = 0;
      c.freq_write = TFT_SPI_HZ; c.freq_read = 6000000;   // GRAM read-back (screenshots) is unreliable above ~8 MHz on CYD wiring
      c.spi_3wire = false; c.use_lock = true; c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = PIN_TFT_SCLK; c.pin_mosi = PIN_TFT_MOSI; c.pin_miso = PIN_TFT_MISO; c.pin_dc = PIN_TFT_DC;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = PIN_TFT_CS; c.pin_rst = PIN_TFT_RST; c.pin_busy = -1;
      c.memory_width = TFT_NATIVE_W; c.memory_height = TFT_NATIVE_H;
      c.panel_width = TFT_NATIVE_W;  c.panel_height = TFT_NATIVE_H;
      c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 0;
      c.dummy_read_pixel = 8; c.dummy_read_bits = 1;
      c.readable = AITL_PANEL_READABLE; c.invert = false; c.rgb_order = false;
#if defined(AITL_TOUCH_XPT2046_SHARED)
      c.dlen_16bit = false; c.bus_shared = true;
#else
      c.dlen_16bit = false; c.bus_shared = false;
#endif
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = PIN_TFT_BL; c.invert = false; c.freq = 12000; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    { auto c = _touch.config();
#if defined(AITL_TOUCH_XPT2046)
      c.x_min = 300; c.x_max = 3800; c.y_min = 300; c.y_max = 3800;   // typical ADC span; calibration refines scale/offset
      c.pin_int = -1; c.bus_shared = false; c.offset_rotation = 0;
#elif defined(AITL_TOUCH_XPT2046_SHARED)
      c.x_min = 300; c.x_max = 3900; c.y_min = 300; c.y_max = 3900;
      c.pin_int = PIN_TOUCH_IRQ; c.bus_shared = true; c.offset_rotation = 0;
      c.spi_host = HSPI_HOST; c.freq = TOUCH_SPI_HZ; c.pin_sclk = PIN_TFT_SCLK; c.pin_mosi = PIN_TFT_MOSI; c.pin_miso = PIN_TFT_MISO; c.pin_cs = PIN_TOUCH_CS;
#else
      c.x_min = 0; c.x_max = TFT_NATIVE_W - 1; c.y_min = 0; c.y_max = TFT_NATIVE_H - 1;
      c.pin_int = -1; c.pin_rst = PIN_TOUCH_RST; c.bus_shared = false; c.offset_rotation = 0;
      c.i2c_port = 0; c.i2c_addr = TOUCH_I2C_ADDR; c.pin_sda = PIN_TOUCH_SDA; c.pin_scl = PIN_TOUCH_SCL; c.freq = 400000;
#endif
      _touch.config(c);
#if !defined(AITL_TOUCH_CST8XX)
      _panel.setTouch(&_touch);     // CST8xx boards use the filtered raw driver below instead (ghost-touch gating)
#endif
    }
    setPanel(&_panel);
  }
  void applyPanelFlags(bool invert, bool bgr) {
    auto c = _panel.config(); c.invert = invert; c.rgb_order = bgr; _panel.config(c);
  }
};
#endif  // AITL_PANEL_RGB

static LGFX tft;
static constexpr uint16_t kBufLinesMax = (TFT_NATIVE_H > 320) ? 12 : 32;   // 480-wide boards: 2 x 480x12 RGB565 = 23 KB (keeps ~80 KB free with an applet up); 320-wide: 2 x 320x32 = 41 KB. DMA overlaps render with transfer
static uint16_t kBufLines = kBufLinesMax;
static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;
static lv_indev_drv_t s_indevDrv;
static lv_color_t* s_buf1 = nullptr;
static lv_color_t* s_buf2 = nullptr;
static TouchRaw s_lastRaw{false, 0, 0};
static bool s_calibrating = false;

enum class Power { On, Dim, Off };
static Power s_power = Power::On;
static bool s_swallow = false;

bool displayReadRow(int y, uint16_t* out, int w, int step) {
  if (!AITL_PANEL_READABLE || y < 0 || y >= tft.height()) return false;
  static uint16_t row[480];
  int full = tft.width(); if (full > 480) full = 480;
  tft.readRect(0, y, full, 1, row);
  for (int i = 0, x = 0; i < w && x < full; i++, x += step) out[i] = row[x];
  return true;
}
lv_coord_t displayW() { return (g_settings.rotation & 1) ? TFT_NATIVE_H : TFT_NATIVE_W; }
lv_coord_t displayH() { return (g_settings.rotation & 1) ? TFT_NATIVE_W : TFT_NATIVE_H; }

// perf counters (reported by displayPerfReport)
static uint32_t s_flushUs = 0, s_flushes = 0, s_frames = 0, s_refrMs = 0, s_refrMax = 0, s_refrN = 0, s_refrPx = 0, s_touchUs = 0, s_touchN = 0;
static void flushCb(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* px) {
  uint32_t t0 = micros();
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  if (tft.getStartCount() == 0) tft.startWrite();
  // DMA: returns as soon as the transfer is queued; LVGL renders the next chunk into the other buffer meanwhile.
  tft.pushImageDMA(a->x1, a->y1, w, h, (lgfx::swap565_t*)&px->full);   // LV_COLOR_16_SWAP=1: already big-endian, no conversion
  if (!s_buf2) tft.waitDMA();   // single-buffer mode: LVGL reuses this buffer for the next strip, so the transfer must finish first
  if (lv_disp_flush_is_last(d)) { tft.endWrite(); s_frames++; }   // endWrite waits for the DMA to finish and frees the bus for touch
  s_flushUs += micros() - t0; s_flushes++;
  lv_disp_flush_ready(d);
}
static void monitorCb(lv_disp_drv_t*, uint32_t time, uint32_t px) { s_refrMs += time; if (time > s_refrMax) s_refrMax = time; s_refrN++; s_refrPx += px; }
void displayPerfReport(uint32_t handlerUs, uint32_t handlerMaxUs, uint32_t loops) {
  Serial.printf("# perf: refr n=%lu avg=%lums max=%lums avgpx=%lu | flush %lu chunks %lums | touch %lu reads %lums | handler max=%lums loops=%lu\n",
                (unsigned long)s_refrN, (unsigned long)(s_refrN ? s_refrMs / s_refrN : 0), (unsigned long)s_refrMax, (unsigned long)(s_refrN ? s_refrPx / s_refrN : 0),
                (unsigned long)s_flushes, (unsigned long)(s_flushUs / 1000), (unsigned long)s_touchN, (unsigned long)(s_touchUs / 1000),
                (unsigned long)(handlerMaxUs / 1000), (unsigned long)loops);
  s_flushUs = 0; s_flushes = 0; s_frames = 0; s_refrMs = 0; s_refrMax = 0; s_refrN = 0; s_refrPx = 0; s_touchUs = 0; s_touchN = 0;
}

// LovyanGFX rotates touch with the display. On top we apply user calibration in UI space:
// optional axis swap, X/Y inversion, scale (resistive panels) and offset, solved by the calibration wizard.
static void mapTouch(uint16_t rx, uint16_t ry, lv_coord_t& ux, lv_coord_t& uy) {
  const int32_t W = displayW(), H = displayH();
  int32_t x = rx, y = ry;
  if (!s_calibrating) {
    if (g_settings.touchSwapXY) { int32_t t = x; x = (y * (W - 1)) / (H - 1); y = (t * (H - 1)) / (W - 1); }
    if (g_settings.touchInvX) x = (W - 1) - x;
    if (g_settings.touchInvY) y = (H - 1) - y;
    if (g_settings.touchScaleX && g_settings.touchScaleX != 1000) x = (x * (int32_t)g_settings.touchScaleX) / 1000;
    if (g_settings.touchScaleY && g_settings.touchScaleY != 1000) y = (y * (int32_t)g_settings.touchScaleY) / 1000;
    x += g_settings.touchOffX; y += g_settings.touchOffY;
  }
  ux = constrain(x, 0, W - 1); uy = constrain(y, 0, H - 1);
}

#if defined(AITL_TOUCH_CST8XX)
// ---------------- CST816/CST820 raw driver with ghost-touch filtering ----------------
// The CYD's CST820 emits garbage frames (RF noise from the radios, dirty USB power) and sometimes keeps reporting the
// same frozen coordinate after the finger lifts. LovyanGFX's driver passes all of that through, which shows up as
// taps nobody made. This driver (ported from TouchBoard, verified on this panel) gates reports on the chip's INT
// pulse, drops impossible frames, requires consecutive agreeing samples, and force-releases a latched coordinate.
#include <Wire.h>
static const uint8_t CST_ADDR = TOUCH_I2C_ADDR;
static const uint32_t TOUCH_INT_GATE_MS = 150, TOUCH_STUCK_MS = 1500;
static const uint8_t PRESS_FRAMES = 2, RELEASE_FRAMES = 2; static const int JITTER_PX = 40;
static volatile uint32_t s_lastIntMs = 0, s_intCount = 0;
static void IRAM_ATTR touchIsr() { s_lastIntMs = millis(); s_intCount++; }
static bool cstRead(uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(CST_ADDR); Wire.write(reg); if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)CST_ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read(); return true;
}
static void cstWrite(uint8_t reg, uint8_t v) { Wire.beginTransmission(CST_ADDR); Wire.write(reg); Wire.write(v); Wire.endTransmission(); }
static void cstBegin() {
  pinMode(PIN_TOUCH_RST, OUTPUT); digitalWrite(PIN_TOUCH_RST, LOW); delay(20); digitalWrite(PIN_TOUCH_RST, HIGH); delay(150);
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
  cstWrite(0xFE, 0xFF);   // DisAutoSleep: the chip otherwise powers down after a few idle seconds and stops answering
  cstWrite(0xFA, 0x60);   // IrqCtl: pulse INT while touched and on change
  pinMode(PIN_TOUCH_INT, INPUT); attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_INT), touchIsr, FALLING);
  uint8_t id = 0; bool ok = cstRead(0xA7, &id, 1) && id != 0;
  Serial.printf("# touch: CST8xx %s (id 0x%02X), INT gate on IO%d\n", ok ? "found" : "NOT FOUND", id, PIN_TOUCH_INT);
}
// 1 finger down (rx, ry native portrait), 0 none, -1 garbage
static int cstRaw(int& rx, int& ry) {
  uint8_t d[6]; if (!cstRead(0x01, d, 6)) return -1;
  if (d[1] == 0) return 0; if (d[1] > 2) return -1;
  rx = ((d[2] & 0x0F) << 8) | d[3]; ry = ((d[4] & 0x0F) << 8) | d[5];
  if (rx > TFT_NATIVE_W - 1 || ry > TFT_NATIVE_H - 1) return -1;
  return 1;
}
// Filtered read in native portrait coordinates. Returns true while a stable finger is down.
static bool cstFiltered(uint16_t& ox, uint16_t& oy) {
  static bool stableDown = false, gateArmed = false, stuck = false; static uint8_t downN = 0, upN = 0;
  static int lastX = 0, lastY = 0, prevRX = -1, prevRY = -1; static uint32_t sameSince = 0;
  int rx = 0, ry = 0; int st = cstRaw(rx, ry);
  if (!gateArmed && s_intCount > 0) { gateArmed = true; Serial.println("# touch: INT line alive, stale-report gating armed"); }
  if (gateArmed && st == 1 && millis() - s_lastIntMs > TOUCH_INT_GATE_MS) st = 0;   // report without a recent pulse = stale
  if (st == 1) {
    if (rx == prevRX && ry == prevRY) { if (!sameSince) sameSince = millis(); if (millis() - sameSince > TOUCH_STUCK_MS) stuck = true; }
    else { prevRX = rx; prevRY = ry; sameSince = 0; stuck = false; }
    if (stuck) st = 0;                                                             // latched coordinate, not a finger
  } else if (st == 0) { sameSince = 0; stuck = false; prevRX = prevRY = -1; }
  if (st == 1) {
    upN = 0;
    if (!stableDown && downN > 0 && (abs(rx - lastX) > JITTER_PX || abs(ry - lastY) > JITTER_PX)) downN = 1; else if (downN < 255) downN++;
    lastX = rx; lastY = ry;
    if (downN >= PRESS_FRAMES) stableDown = true;
  } else if (st == 0) { downN = 0; if (upN < 255) upN++; if (upN >= RELEASE_FRAMES) stableDown = false; }
  if (stableDown) { ox = lastX; oy = lastY; }
  return stableDown;
}
static bool readTouchRotated(uint16_t* x, uint16_t* y) {   // same convention as LovyanGFX's rotated touch
  uint16_t rx, ry; if (!cstFiltered(rx, ry)) return false;
  const int W0 = TFT_NATIVE_W, H0 = TFT_NATIVE_H;
  switch (g_settings.rotation & 3) {
    case 0: *x = rx; *y = ry; break;
    case 1: *x = ry; *y = W0 - 1 - rx; break;
    case 2: *x = W0 - 1 - rx; *y = H0 - 1 - ry; break;
    default: *x = H0 - 1 - ry; *y = rx; break;
  }
  return true;
}
#endif

static void touchCb(lv_indev_drv_t*, lv_indev_data_t* data) {
  static uint8_t stable = 0;
  static lv_coord_t lx = 0, ly = 0;
  uint16_t x, y;
  uint32_t t0 = micros();
#if defined(AITL_TOUCH_CST8XX)
  bool touched = readTouchRotated(&x, &y);
#else
  bool touched = tft.getTouch(&x, &y);
#endif
  s_touchUs += micros() - t0; s_touchN++;
  s_lastRaw = {touched, x, y};
  if (touched) {
    if (s_power != Power::On) { s_swallow = (s_power == Power::Off); displayWake(); }
    if (stable < 250) stable++;
  } else { stable = 0; s_swallow = false; }
  bool pressed = touched && !s_swallow && stable >= g_settings.touchDebounce;
  if (pressed) {
    mapTouch(x, y, lx, ly);
    if (stable == g_settings.touchDebounce) {
      if (g_settings.hapticLed) ledTapFlash();
      Serial.printf("# touch raw=(%u,%u) ui=(%d,%d) rot=%u\n", x, y, (int)lx, (int)ly, g_settings.rotation);
    }
  }
  data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  data->point.x = lx; data->point.y = ly;
}

TouchRaw displayReadTouchRaw() { return s_lastRaw; }
void displaySetCalibrating(bool on) { s_calibrating = on; }
bool displayIsAwake() { return s_power == Power::On; }
void displayApplyBrightness() { if (s_power == Power::On) tft.setBrightness(g_settings.brightness); }
void displayWake() { s_power = Power::On; tft.setBrightness(g_settings.brightness); lv_disp_trig_activity(nullptr); }

void displayPowerTick() {
  uint32_t tmo = g_settings.screenTimeout * 1000UL;
  if (!tmo) { if (s_power != Power::On) displayWake(); return; }
  uint32_t idle = lv_disp_get_inactive_time(nullptr);
  if (idle > tmo + 60000) { if (s_power != Power::Off) { s_power = Power::Off; tft.setBrightness(0); } }
  else if (idle > tmo)    { if (s_power == Power::On) { s_power = Power::Dim; tft.setBrightness(10); } }
}

void displayApplyPanel() {
  tft.applyPanelFlags(g_settings.invertColors, g_settings.bgrOrder);
  tft.init();
  tft.setRotation(g_settings.rotation & 3);
  s_dispDrv.hor_res = displayW(); s_dispDrv.ver_res = displayH();
  lv_disp_drv_update(lv_disp_get_default(), &s_dispDrv);
  lv_obj_invalidate(lv_scr_act());
  displayApplyBrightness();
}

void displayBegin() {
  tft.applyPanelFlags(g_settings.invertColors, g_settings.bgrOrder);
  tft.init();
  tft.setRotation(g_settings.rotation & 3);
  tft.setBrightness(0);
  tft.fillScreen(0x0000);
  lv_init();
  // Wi-Fi mode: the Wi-Fi stack (~40 KB) and a TLS handshake (~30 KB) must fit next to LVGL, so draw with one small buffer.
  bool lean = g_settings.wifiEnabled;                                  // single buffer while Wi-Fi is on (saves ~25 KB)
  if (lean) kBufLines = g_settings.wifiMode == 1 ? ((TFT_NATIVE_H > 320) ? 10 : 12) : ((TFT_NATIVE_H > 320) ? 20 : 24);
  size_t n = TFT_NATIVE_H * kBufLines;  // sized for the widest orientation
  s_buf1 = (lv_color_t*)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_DMA);
  s_buf2 = lean ? nullptr : (lv_color_t*)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_DMA);
  lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, n);
  lv_disp_drv_init(&s_dispDrv);
  s_dispDrv.hor_res = displayW(); s_dispDrv.ver_res = displayH();
  s_dispDrv.flush_cb = flushCb; s_dispDrv.draw_buf = &s_drawBuf; s_dispDrv.monitor_cb = monitorCb;
  lv_disp_drv_register(&s_dispDrv);
#if defined(AITL_TOUCH_CST8XX)
  cstBegin();
#endif
  lv_indev_drv_init(&s_indevDrv);
  s_indevDrv.type = LV_INDEV_TYPE_POINTER; s_indevDrv.read_cb = touchCb;
  s_indevDrv.long_press_time = g_settings.longPressMs;
  lv_indev_drv_register(&s_indevDrv);
}
