// Musecorder applet runtime for Meta's Muse Gadget firmware. See musecorder.h.
#include "musecorder.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string>
#include <map>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern "C" {   // Meta's C headers without C++ guards
#include "led_status.h"
#include "pixel_font.h"
}
#include "noise_control.h"
extern "C" {
#include "button.h"
#include "wifi_known.h"
#include "link_pairing.h"
void app_touch_short_press(void);   // app.c: the BOOT tap (confirms pairing) and the 5 s hold (erases setup), from the touchscreen
void app_touch_long_press(void);
}
#include "esp_system.h"

static const char *TAG = "musecorder";
static bool pressCb(bool pressed);

#define MC_MAX_APPLET_BYTES 4096
#define MC_MAX_APPLETS 5          // stored applets (the launcher lists them)
#define MC_MAX_ID 12              // NVS keys are 15 chars: "a." + id
#define MC_MAX_LOG 12
#define MC_TICK_MS 50
#define MC_HTTP_REPLY_MAX 384
#define MC_PART "applets"         // its own NVS partition (gadget/port/partitions_cyd24_4mb.csv): Meta's pairing data is never touched
#define MC_NS "mc"

#if CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796
#define MC_TOUCH_XPT2046 1        // resistive panel on the LCD's SPI bus: CS 33, pen IRQ 36
#define MC_PIN_AMP_EN 4           // speaker amplifier enable (high = on)
#define MC_TAP_SAMPLES 1
#else
#define MC_TOUCH_CST820 1         // capacitive panel on I2C0: SDA 33, SCL 32, address 0x15
#define MC_PIN_AMP_EN -1
#define MC_TAP_SAMPLES 2
#endif
#define MC_PIN_SPEAKER 26

// ---------------------------------------------------------------- values + expressions
struct Value {
    bool isNum = true;
    double num = 0;
    std::string str;
    static Value N(double d) { Value v; v.isNum = true; v.num = d; return v; }
    static Value S(const std::string &s) { Value v; v.isNum = false; v.str = s; return v; }
    double asNum() const { return isNum ? num : atof(str.c_str()); }
    bool truthy() const { return isNum ? num != 0 : !str.empty() && str != "false" && str != "0"; }
    std::string asStr() const {
        if (!isNum) return str;
        char b[32];
        if (fabs(num - round(num)) < 1e-9 && fabs(num) < 1e15) snprintf(b, sizeof b, "%lld", (long long)llround(num));
        else snprintf(b, sizeof b, "%g", num);
        return b;
    }
};

static std::map<std::string, Value> s_state;

struct Parser {
    const char *s; size_t i = 0; int depth = 0; bool err = false;
    explicit Parser(const char *src) : s(src) {}
    void ws() { while (s[i] == ' ' || s[i] == '\t') i++; }
    bool take(const char *t) { ws(); size_t n = strlen(t); if (!strncmp(s + i, t, n)) { i += n; return true; } return false; }
    Value expr() { if (++depth > 24) { err = true; depth--; return Value::N(0); } Value v = orx(); depth--; return v; }
    Value orx() { Value l = andx(); while (take("||")) { Value r = andx(); l = Value::N(l.truthy() || r.truthy()); } return l; }
    Value andx() { Value l = eq(); while (take("&&")) { Value r = eq(); l = Value::N(l.truthy() && r.truthy()); } return l; }
    static bool same(const Value &a, const Value &b) { return (a.isNum && b.isNum) ? fabs(a.num - b.num) < 1e-9 : a.asStr() == b.asStr(); }
    Value eq() { Value l = cmp(); for (;;) { if (take("==")) { Value r = cmp(); l = Value::N(same(l, r)); } else if (take("!=")) { Value r = cmp(); l = Value::N(!same(l, r)); } else return l; } }
    Value cmp() { Value l = add(); for (;;) {
        if (take("<=")) { Value r = add(); l = Value::N(l.asNum() <= r.asNum()); }
        else if (take(">=")) { Value r = add(); l = Value::N(l.asNum() >= r.asNum()); }
        else if (take("<")) { Value r = add(); l = Value::N(l.asNum() < r.asNum()); }
        else if (take(">")) { Value r = add(); l = Value::N(l.asNum() > r.asNum()); }
        else return l; } }
    Value add() { Value l = mul(); for (;;) {
        if (take("+")) { Value r = mul(); if (l.isNum && r.isNum) l = Value::N(l.num + r.num); else l = Value::S(l.asStr() + r.asStr()); }
        else if (take("-")) { Value r = mul(); l = Value::N(l.asNum() - r.asNum()); }
        else return l; } }
    Value mul() { Value l = unary(); for (;;) {
        if (take("*")) { Value r = unary(); l = Value::N(l.asNum() * r.asNum()); }
        else if (take("/")) { Value r = unary(); double d = r.asNum(); l = Value::N(d == 0 ? 0 : l.asNum() / d); }
        else if (take("%")) { Value r = unary(); double d = r.asNum(); l = Value::N(d == 0 ? 0 : fmod(l.asNum(), d)); }
        else return l; } }
    Value unary() { if (take("!")) return Value::N(!unary().truthy()); if (take("-")) return Value::N(-unary().asNum()); return primary(); }
    std::vector<Value> args() { std::vector<Value> a; if (take(")")) return a; do { a.push_back(expr()); if (a.size() > 6) break; } while (take(",")); take(")"); return a; }
    Value call(const std::string &f) {
        std::vector<Value> a = args();
        auto A = [&](size_t k) { return k < a.size() ? a[k] : Value::N(0); };
        if (f == "len") return Value::N((double)A(0).asStr().size());
        if (f == "int") return Value::N(trunc(A(0).asNum()));
        if (f == "round") return Value::N(round(A(0).asNum()));
        if (f == "abs") return Value::N(fabs(A(0).asNum()));
        if (f == "min") return Value::N(fmin(A(0).asNum(), A(1).asNum()));
        if (f == "max") return Value::N(fmax(A(0).asNum(), A(1).asNum()));
        if (f == "str") return Value::S(A(0).asStr());
        if (f == "num") return Value::N(A(0).asNum());
        if (f == "upper" || f == "lower") { std::string s = A(0).asStr(); for (auto &c : s) c = f == "upper" ? toupper(c) : tolower(c); return Value::S(s); }
        if (f == "pad") { std::string s = A(0).asStr(); int w = (int)A(1).asNum(); while ((int)s.size() < w) s = " " + s; return Value::S(s); }
        if (f == "random") { double lo = A(0).asNum(), hi = a.size() > 1 ? A(1).asNum() : 100; if (hi < lo) hi = lo; return Value::N(lo + (double)(esp_random() % (uint32_t)(hi - lo + 1))); }
        if (f == "hhmmss") { long t = (long)A(0).asNum(); char b[16]; snprintf(b, sizeof b, "%02ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60); return Value::S(b); }
        if (f == "time") return Value::N((double)time(nullptr));
        if (f == "uptime") return Value::N((double)(esp_timer_get_time() / 1000000));
        if (f == "clock") { time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm); char b[16]; snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min); return Value::S(b); }
        err = true; return Value::N(0);
    }
    Value primary() {
        ws();
        char c = s[i];
        if (c == '(') { i++; Value v = expr(); take(")"); return v; }
        if (c == '\'' || c == '"') { char q = c; i++; std::string out; while (s[i] && s[i] != q) out += s[i++]; if (s[i]) i++; return Value::S(out); }
        if ((c >= '0' && c <= '9') || c == '.') { char *end; double d = strtod(s + i, &end); i = end - s; return Value::N(d); }
        if (isalpha((unsigned char)c) || c == '_') {
            std::string id; while (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.') id += s[i++];
            if (take("(")) return call(id);
            if (id == "true") return Value::N(1);
            if (id == "false") return Value::N(0);
            if (!id.compare(0, 6, "state.")) id = id.substr(6);
            auto it = s_state.find(id);
            if (it != s_state.end()) return it->second;
            return Value::S("");
        }
        err = true; if (s[i]) i++; return Value::N(0);
    }
};

static Value eval(const std::string &src) { Parser p(src.c_str()); Value v = p.expr(); return v; }

// "${expr}" inside text; a string that is exactly one ${expr} keeps the value's type.
static Value render(const char *tpl) {
    if (!tpl) return Value::S("");
    size_t n = strlen(tpl);
    if (n > 3 && tpl[0] == '$' && tpl[1] == '{' && tpl[n - 1] == '}' && !strstr(tpl + 2, "${")) return eval(std::string(tpl + 2, n - 3));
    std::string out; const char *p = tpl;
    while (*p) {
        if (p[0] == '$' && p[1] == '{') {
            const char *e = strchr(p + 2, '}'); if (!e) { out += p; break; }
            out += eval(std::string(p + 2, e - p - 2)).asStr(); p = e + 1;
        } else out += *p++;
    }
    return Value::S(out);
}

static Value fromJson(cJSON *j) {
    if (cJSON_IsNumber(j)) return Value::N(j->valuedouble);
    if (cJSON_IsBool(j)) return Value::N(cJSON_IsTrue(j) ? 1 : 0);
    if (cJSON_IsString(j)) return render(j->valuestring);
    return Value::S("");
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static double numOf(cJSON *o, const char *key, double dflt) { cJSON *j = cJSON_GetObjectItem(o, key); return j ? fromJson(j).asNum() : dflt; }
static std::string strOf(cJSON *o, const char *key) { cJSON *j = cJSON_GetObjectItem(o, key); return j ? fromJson(j).asStr() : std::string(); }
static void setState(const std::string &k, cJSON *j) { s_state[k] = cJSON_IsNumber(j) ? Value::N(j->valuedouble) : Value::S(cJSON_GetStringValue(j) ? cJSON_GetStringValue(j) : ""); }

// ---------------------------------------------------------------- runtime state
static SemaphoreHandle_t s_lock;
static cJSON *s_app = nullptr;          // the applet document (owned)
static std::string s_app_id;
static std::vector<std::string> s_log;
static bool s_dirty_screen = false, s_dirty_state = false, s_cleared = false, s_running = false, s_exit = false;
static int64_t s_last_save = 0;
static int s_screen_w = 0, s_screen_h = 0;
static bool s_vm_up = false;
struct Timer { int period_ms; int64_t next; const char *key; };
static std::vector<Timer> s_timers;
struct Watch { int pin; int mode; int last; const char *key; };   // mode 0 rise 1 fall 2 change
static std::vector<Watch> s_watches;
static std::vector<std::string> s_pending;   // event names queued from other tasks
static bool s_tap_pending = false;
struct Hit { int x, y, w, h; std::string ev; };   // touch targets of the current screen (or the launcher)
static std::vector<Hit> s_hits;
static bool s_launcher = false;
static bool s_canvas = false;

static void logf(const char *fmt, ...) {
    char b[120]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ESP_LOGI(TAG, "%s", b);
    if (s_log.size() >= MC_MAX_LOG) s_log.erase(s_log.begin());
    s_log.push_back(b);
}

static bool pinAllowed(int pin) {
    static const int ok[] = {4, 16, 17, 21, 22, 25, 26, 32, 34, 35, 36, 39};
    for (int p : ok) if (p == pin) return true;
    return false;
}

// ---------------------------------------------------------------- applet store (own NVS partition)
static bool s_store_ok = false;
static std::string slug(const char *id) { std::string s; for (const char *p = id; *p && s.size() < MC_MAX_ID; p++) if (isalnum((unsigned char)*p) || *p == '_' || *p == '-') s += *p; return s.empty() ? "applet" : s; }
static bool storeOpen(nvs_open_mode_t mode, nvs_handle_t *h) { return s_store_ok && nvs_open_from_partition(MC_PART, MC_NS, mode, h) == ESP_OK; }
static char *storeGet(const char *key, size_t maxLen) {   // malloc'd NUL-terminated blob or NULL
    nvs_handle_t h; size_t n = 0; if (!storeOpen(NVS_READONLY, &h)) return nullptr;
    char *txt = nullptr;
    if (nvs_get_blob(h, key, nullptr, &n) == ESP_OK && n > 1 && n <= maxLen + 1) { txt = (char *)malloc(n); if (txt && nvs_get_blob(h, key, txt, &n) != ESP_OK) { free(txt); txt = nullptr; } }
    nvs_close(h); if (txt) txt[n - 1] = 0; return txt;
}
static void storeSet(const char *key, const char *val) {   // NULL erases
    nvs_handle_t h; if (!storeOpen(NVS_READWRITE, &h)) return;
    if (val) nvs_set_blob(h, key, val, strlen(val) + 1); else nvs_erase_key(h, key);
    nvs_commit(h); nvs_close(h);
}
static std::vector<std::string> storeIds() {
    std::vector<std::string> ids; char *txt = storeGet("ids", 128); if (!txt) return ids;
    for (char *p = strtok(txt, ","); p; p = strtok(nullptr, ",")) if (*p) ids.push_back(p);
    free(txt); return ids;
}
static void storeSetIds(const std::vector<std::string> &ids) { std::string s; for (auto &i : ids) { if (!s.empty()) s += ","; s += i; } storeSet("ids", s.empty() ? nullptr : s.c_str()); }
static std::string key(const char *prefix, const std::string &id) { return std::string(prefix) + id; }
static void storeDelete(const std::string &id) {
    auto ids = storeIds(); for (size_t i = 0; i < ids.size(); i++) if (ids[i] == id) { ids.erase(ids.begin() + i); break; }
    storeSetIds(ids); storeSet(key("a.", id).c_str(), nullptr); storeSet(key("s.", id).c_str(), nullptr); storeSet(key("n.", id).c_str(), nullptr);
}
static void storeInit() {
    esp_err_t e = nvs_flash_init_partition(MC_PART);
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) { nvs_flash_erase_partition(MC_PART); e = nvs_flash_init_partition(MC_PART); }
    s_store_ok = e == ESP_OK;
    if (!s_store_ok) { ESP_LOGW(TAG, "applet store unavailable (%s): add the 'applets' partition", esp_err_to_name(e)); return; }
    // Applets from the first firmware lived in Meta's NVS: move them over once.
    nvs_handle_t h; if (nvs_open("musecorder", NVS_READWRITE, &h) != ESP_OK) return;
    size_t n = 0;
    if (nvs_get_blob(h, "applet", nullptr, &n) == ESP_OK && n > 1 && n <= MC_MAX_APPLET_BYTES + 1) {
        char *txt = (char *)malloc(n);
        if (txt && nvs_get_blob(h, "applet", txt, &n) == ESP_OK) {
            cJSON *doc = cJSON_Parse(txt);
            const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(doc, "id"));
            if (id) {
                std::string sid = slug(id); auto ids = storeIds(); ids.push_back(sid); storeSetIds(ids);
                storeSet(key("a.", sid).c_str(), txt); const char *nm = cJSON_GetStringValue(cJSON_GetObjectItem(doc, "name")); storeSet(key("n.", sid).c_str(), nm ? nm : sid.c_str());
                size_t sn = 0; if (nvs_get_blob(h, "state", nullptr, &sn) == ESP_OK && sn > 1 && sn < 4096) { char *st = (char *)malloc(sn); if (st && nvs_get_blob(h, "state", st, &sn) == ESP_OK) storeSet(key("s.", sid).c_str(), st); free(st); }
                storeSet("run", sid.c_str()); ESP_LOGI(TAG, "moved applet %s into the applet store", sid.c_str());
            }
            cJSON_Delete(doc);
        }
        free(txt); nvs_erase_key(h, "applet"); nvs_erase_key(h, "state"); nvs_commit(h);
    }
    nvs_close(h);
}

// ---------------------------------------------------------------- screen primitives
#define STRIP_ROWS 4
static uint16_t *s_strip = nullptr;   // s_screen_w * STRIP_ROWS
#define TOP_CLEAR 28                  // Meta's status bars live in the first and last rows
#define BTN_H 44

static inline uint16_t be565(uint8_t r, uint8_t g, uint8_t b) { uint16_t v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return (uint16_t)((v >> 8) | (v << 8)); }
static const uint16_t C_BG = 0, C_TEXT = be565(0xf3, 0xf4, 0xf8), C_BIG = be565(0x2e, 0xe6, 0xd6), C_MUTED = be565(0x9a, 0xa3, 0xb5), C_BAR = be565(0xe8, 0x79, 0xf9), C_BAR_BG = be565(0x2a, 0x2f, 0x3d), C_BTN = be565(0x24, 0x3a, 0x5e), C_BTN_HOT = be565(0x7a, 0x2f, 0x8e);
static uint16_t hexColor(const std::string &in, uint16_t dflt) { std::string h = in; if (!h.empty() && h[0] == '#') h.erase(0, 1); if (h.size() != 6) return dflt; unsigned rgb = (unsigned)strtoul(h.c_str(), nullptr, 16); return be565(rgb >> 16, rgb >> 8, rgb); }

static bool ensureStrip() { if (!s_strip && s_screen_w > 0) s_strip = (uint16_t *)malloc(s_screen_w * STRIP_ROWS * sizeof(uint16_t)); return s_strip != nullptr; }
static void fillRect(int x, int y, int w, int h, uint16_t color) {
    if (!ensureStrip()) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > s_screen_w) w = s_screen_w - x;
    if (y < 0) { h += y; y = 0; }
    if (y + h > s_screen_h) h = s_screen_h - y;
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w * STRIP_ROWS; i++) s_strip[i] = color;
    for (int sy = 0; sy < h; sy += STRIP_ROWS) { int rows = h - sy < STRIP_ROWS ? h - sy : STRIP_ROWS; led_status_draw_rect(x, y + sy, w, rows, s_strip); }
}
// Text of height 8*scale, centred in [x, x+w), with its own background.
static void drawTextIn(int x, int y, int w, const std::string &text, int scale, uint16_t color, uint16_t bg) {
    if (!ensureStrip() || w <= 0) return;
    int cw = PIXEL_FONT_WIDTH * scale + scale, h = PIXEL_FONT_HEIGHT * scale;
    int maxc = w / cw; std::string t = text.size() > (size_t)maxc ? text.substr(0, maxc) : text;
    int x0 = (w - (int)t.size() * cw) / 2;
    for (int sy = 0; sy < h; sy += STRIP_ROWS) {
        int rows = h - sy < STRIP_ROWS ? h - sy : STRIP_ROWS;
        for (int r = 0; r < rows; r++) {
            uint16_t *line = s_strip + r * w; for (int i = 0; i < w; i++) line[i] = bg;
            int fy = (sy + r) / scale;
            for (size_t ci = 0; ci < t.size(); ci++) {
                unsigned char ch = t[ci]; if (ch < PIXEL_FONT_FIRST || ch > PIXEL_FONT_LAST) ch = '?';
                for (int gx = 0; gx < PIXEL_FONT_WIDTH; gx++) {
                    if (pixel_font[ch - PIXEL_FONT_FIRST][gx] >> fy & 1) { int px = x0 + ci * cw + gx * scale; for (int k = 0; k < scale; k++) if (px + k >= 0 && px + k < w) line[px + k] = color; }
                }
            }
        }
        led_status_draw_rect(x, y + sy, w, rows, s_strip);
    }
}
static void drawText(int y, const std::string &text, int scale, uint16_t color) { drawTextIn(0, y, s_screen_w, text, scale, color, C_BG); }
static void drawBar(int y, double frac, int h) {
    int x0 = 16, w = s_screen_w - 32, fill = (int)(w * (frac < 0 ? 0 : frac > 1 ? 1 : frac));
    fillRect(0, y, s_screen_w, h, C_BG); fillRect(x0, y, w, h, C_BAR_BG); if (fill > 0) fillRect(x0, y, fill, h, C_BAR);
}
static void clearRows(int y0, int y1) { if (y1 > y0) fillRect(0, y0, s_screen_w, y1 - y0, C_BG); }
static void drawButton(int x, int y, int w, const std::string &label, uint16_t bg) {
    fillRect(x, y, w, BTN_H, bg);
    int scale = (int)label.size() * (PIXEL_FONT_WIDTH * 3 + 3) <= w - 8 ? 3 : 2;
    drawTextIn(x + 2, y + (BTN_H - 8 * scale) / 2, w - 4, label, scale, C_TEXT, bg);
}

// ---------------------------------------------------------------- touch
struct Cal { bool valid; int swap, x0, x1, y0, y1; };   // raw -> screen: sx = (rx - x0) * (W-1) / (x1 - x0)
static Cal s_cal = {false, 0, 0, 0, 0, 0};
static bool s_touch_ok = false;
static int s_raw_x = 0, s_raw_y = 0;
#if MC_TOUCH_XPT2046
static spi_device_handle_t s_tdev = nullptr;
static int xptRead(uint8_t cmd) {
    spi_transaction_t t = {}; t.length = 24; t.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA; t.tx_data[0] = cmd;
    if (spi_device_polling_transmit(s_tdev, &t) != ESP_OK) return -1;
    return ((t.rx_data[1] << 8) | t.rx_data[2]) >> 3;
}
static bool touchInit() {
    spi_device_interface_config_t dc = {}; dc.clock_speed_hz = 1000000; dc.mode = 0; dc.spics_io_num = 33; dc.queue_size = 1;
    if (spi_bus_add_device(SPI2_HOST, &dc, &s_tdev) != ESP_OK) { ESP_LOGW(TAG, "touch: XPT2046 not on the LCD bus"); return false; }
    gpio_reset_pin(GPIO_NUM_36); gpio_set_direction(GPIO_NUM_36, GPIO_MODE_INPUT);
    xptRead(0x90);   // power down between reads so the pen IRQ works
    return true;
}
static bool touchRaw() {
    if (gpio_get_level(GPIO_NUM_36)) return false;   // pen IRQ is active low
    int xs[3], ys[3];
    for (int i = 0; i < 3; i++) { xs[i] = xptRead(0xD1); ys[i] = xptRead(0x91); }
    xptRead(0x90);
    auto med = [](int *v) { if (v[0] > v[1]) std::swap(v[0], v[1]); if (v[1] > v[2]) std::swap(v[1], v[2]); if (v[0] > v[1]) std::swap(v[0], v[1]); return v[1]; };
    int x = med(xs), y = med(ys);
    if (x < 50 || x > 4050 || y < 50 || y > 4050) return false;
    s_raw_x = x; s_raw_y = y; return true;
}
static void calDefault() { s_cal = {false, 0, 200, 3900, 200, 3900}; }
#else
static i2c_master_bus_handle_t s_tbus = nullptr; static i2c_master_dev_handle_t s_tdev = nullptr;
static bool touchInit() {
    i2c_master_bus_config_t bc = {}; bc.i2c_port = I2C_NUM_0; bc.sda_io_num = GPIO_NUM_33; bc.scl_io_num = GPIO_NUM_32; bc.clk_source = I2C_CLK_SRC_DEFAULT; bc.glitch_ignore_cnt = 7; bc.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bc, &s_tbus) != ESP_OK) return false;
    i2c_device_config_t dc = {}; dc.dev_addr_length = I2C_ADDR_BIT_LEN_7; dc.device_address = 0x15; dc.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(s_tbus, &dc, &s_tdev) != ESP_OK) return false;
    uint8_t reg = 0xA7, id = 0; if (i2c_master_transmit_receive(s_tdev, &reg, 1, &id, 1, 50) != ESP_OK) { ESP_LOGW(TAG, "touch: no CST8xx"); return false; }
    return true;
}
static bool touchRaw() {
    uint8_t reg = 0x02, b[5];
    if (i2c_master_transmit_receive(s_tdev, &reg, 1, b, 5, 20) != ESP_OK) return false;
    if ((b[0] & 0x0F) != 1) return false;
    int x = ((b[1] & 0x0F) << 8) | b[2], y = ((b[3] & 0x0F) << 8) | b[4];
    if (x >= s_screen_w || y >= s_screen_h) return false;
    s_raw_x = x; s_raw_y = y; return true;
}
static void calDefault() { s_cal = {true, 0, 0, s_screen_w - 1, 0, s_screen_h - 1}; }
#endif
static void calLoad() {
    calDefault(); char *txt = storeGet("cal", 64); if (!txt) return;
    int sw, x0, x1, y0, y1; if (sscanf(txt, "%d %d %d %d %d", &sw, &x0, &x1, &y0, &y1) == 5 && x1 != x0 && y1 != y0) s_cal = {true, sw, x0, x1, y0, y1};
    free(txt);
}
static void calSave() { char b[64]; snprintf(b, sizeof b, "%d %d %d %d %d", s_cal.swap, s_cal.x0, s_cal.x1, s_cal.y0, s_cal.y1); storeSet("cal", b); }
static bool touchRead(int *sx, int *sy) {
    if (!s_touch_ok || !touchRaw()) return false;
    int rx = s_cal.swap ? s_raw_y : s_raw_x, ry = s_cal.swap ? s_raw_x : s_raw_y;
    int x = (int)((long)(rx - s_cal.x0) * (s_screen_w - 1) / (s_cal.x1 - s_cal.x0));
    int y = (int)((long)(ry - s_cal.y0) * (s_screen_h - 1) / (s_cal.y1 - s_cal.y0));
    *sx = x < 0 ? 0 : x >= s_screen_w ? s_screen_w - 1 : x; *sy = y < 0 ? 0 : y >= s_screen_h ? s_screen_h - 1 : y;
    return true;
}

// Three-target calibration wizard: top-left, top-right, bottom-left.
static int s_wiz = -1; static int s_wiz_raw[3][2];
#define WIZ_M 30
static void wizTarget(int i, int *x, int *y) { *x = i == 1 ? s_screen_w - WIZ_M : WIZ_M; *y = i == 2 ? s_screen_h - WIZ_M : WIZ_M; }
static void wizDraw() {
    clearRows(0, s_screen_h);
    drawText(s_screen_h / 2 - 40, "Touch setup", 3, C_MUTED);
    char b[32]; snprintf(b, sizeof b, "tap the box %d/3", s_wiz + 1); drawText(s_screen_h / 2, b, 2, C_TEXT);
    int x, y; wizTarget(s_wiz, &x, &y); fillRect(x - 12, y - 12, 24, 24, C_BAR); fillRect(x - 4, y - 4, 8, 8, C_BG);
}
static void wizStart() { s_wiz = 0; s_launcher = false; s_hits.clear(); wizDraw(); }
static void wizSample() {   // called on pen release with the last raw sample
    s_wiz_raw[s_wiz][0] = s_raw_x; s_wiz_raw[s_wiz][1] = s_raw_y;
    if (++s_wiz < 3) { wizDraw(); return; }
    int dxX = abs(s_wiz_raw[1][0] - s_wiz_raw[0][0]), dxY = abs(s_wiz_raw[1][1] - s_wiz_raw[0][1]);
    int sw = dxY > dxX;   // the raw axis that moved most between TL and TR is the screen's X
    auto R = [&](int i, int axis) { return s_wiz_raw[i][(axis ^ sw) & 1]; };
    int spanX = s_screen_w - 2 * WIZ_M, spanY = s_screen_h - 2 * WIZ_M;
    int ax = R(1, 0) - R(0, 0), ay = R(2, 1) - R(0, 1);
    if (abs(ax) < 100 || abs(ay) < 100) { logf("touch setup: bad samples, retry"); s_wiz = 0; wizDraw(); return; }
    s_cal.swap = sw; s_cal.valid = true;
    s_cal.x0 = R(0, 0) - ax * WIZ_M / spanX; s_cal.x1 = R(1, 0) + ax * (WIZ_M - 1) / spanX;
    s_cal.y0 = R(0, 1) - ay * WIZ_M / spanY; s_cal.y1 = R(2, 1) + ay * (WIZ_M - 1) / spanY;
    calSave(); logf("touch calibrated (swap %d)", sw);
    s_wiz = -1; s_cleared = false; s_dirty_screen = true;
    if (!s_running) led_status_show_animation();
}

// ---------------------------------------------------------------- sound, PWM, ADC, I2C
static std::vector<int> s_tone_q; static int s_tone_ms = 0; static int64_t s_tone_until = 0; static bool s_tone_on = false, s_tone_timer = false;
static void toneSet(int freq) {
    if (freq <= 0) { if (s_tone_on) { ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, 0); gpio_reset_pin((gpio_num_t)MC_PIN_SPEAKER); if (MC_PIN_AMP_EN >= 0) gpio_set_level((gpio_num_t)MC_PIN_AMP_EN, 0); } s_tone_on = false; return; }
    freq = clampi(freq, 40, 8000);
    if (!s_tone_timer) { ledc_timer_config_t tc = {}; tc.speed_mode = LEDC_LOW_SPEED_MODE; tc.timer_num = LEDC_TIMER_3; tc.duty_resolution = LEDC_TIMER_10_BIT; tc.freq_hz = freq; tc.clk_cfg = LEDC_AUTO_CLK; if (ledc_timer_config(&tc) != ESP_OK) return; s_tone_timer = true; }
    else ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_3, freq);
    if (!s_tone_on) {
        ledc_channel_config_t cc = {}; cc.gpio_num = MC_PIN_SPEAKER; cc.speed_mode = LEDC_LOW_SPEED_MODE; cc.channel = LEDC_CHANNEL_5; cc.timer_sel = LEDC_TIMER_3; cc.duty = 512; cc.hpoint = 0;
        if (ledc_channel_config(&cc) != ESP_OK) return;
        if (MC_PIN_AMP_EN >= 0) { gpio_reset_pin((gpio_num_t)MC_PIN_AMP_EN); gpio_set_direction((gpio_num_t)MC_PIN_AMP_EN, GPIO_MODE_OUTPUT); gpio_set_level((gpio_num_t)MC_PIN_AMP_EN, 1); }
        s_tone_on = true;
    } else { ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, 512); ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5); }
}
static void toneNext(int64_t now) {
    if (s_tone_q.empty()) { toneSet(0); return; }
    int f = s_tone_q.front(); s_tone_q.erase(s_tone_q.begin());
    if (f <= 0) { if (s_tone_on) { ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5, 0); ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5); } } else toneSet(f);
    s_tone_until = now + (int64_t)s_tone_ms * 1000;
}
static void doTone(cJSON *t) {   // tone{freq | freq:[...], ms}
    s_tone_q.clear(); cJSON *f = cJSON_GetObjectItem(t, "freq");
    if (cJSON_IsArray(f)) { cJSON *it = nullptr; cJSON_ArrayForEach(it, f) { if (s_tone_q.size() < 32) s_tone_q.push_back((int)fromJson(it).asNum()); } }
    else s_tone_q.push_back((int)(f ? fromJson(f).asNum() : 880));
    s_tone_ms = clampi((int)numOf(t, "ms", 120), 10, 2000);
    toneNext(esp_timer_get_time());
}

static int s_pwm_pin[2] = {-1, -1}; static bool s_pwm_timer = false;
static void pwmStopAll() { for (int k = 0; k < 2; k++) if (s_pwm_pin[k] >= 0) { ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(LEDC_CHANNEL_6 + k), 0); gpio_reset_pin((gpio_num_t)s_pwm_pin[k]); s_pwm_pin[k] = -1; } }
static void doPwm(cJSON *p) {   // pwm{pin, duty 0-100, freq}
    int pin = (int)numOf(p, "pin", -1); double duty = numOf(p, "duty", 50); int freq = (int)numOf(p, "freq", 1000);
    if (!pinAllowed(pin) || pin >= 34) { logf("pwm: pin %d not allowed", pin); return; }
    int slot = -1;
    for (int k = 0; k < 2; k++) if (s_pwm_pin[k] == pin) slot = k;
    if (slot < 0) for (int k = 0; k < 2; k++) if (s_pwm_pin[k] < 0) { slot = k; break; }
    if (slot < 0) { logf("pwm: two pins max"); return; }
    if (duty <= 0) { if (s_pwm_pin[slot] == pin) { ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(LEDC_CHANNEL_6 + slot), 0); gpio_reset_pin((gpio_num_t)pin); s_pwm_pin[slot] = -1; } return; }
    freq = clampi(freq, 10, 40000);
    if (duty > 100) duty = 100;
    if (!s_pwm_timer) { ledc_timer_config_t tc = {}; tc.speed_mode = LEDC_LOW_SPEED_MODE; tc.timer_num = LEDC_TIMER_2; tc.duty_resolution = LEDC_TIMER_10_BIT; tc.freq_hz = freq; tc.clk_cfg = LEDC_AUTO_CLK; if (ledc_timer_config(&tc) != ESP_OK) { logf("pwm: timer failed"); return; } s_pwm_timer = true; }
    else ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_2, freq);
    ledc_channel_config_t cc = {}; cc.gpio_num = pin; cc.speed_mode = LEDC_LOW_SPEED_MODE; cc.channel = (ledc_channel_t)(LEDC_CHANNEL_6 + slot); cc.timer_sel = LEDC_TIMER_2; cc.duty = (uint32_t)(duty * 1023 / 100); cc.hpoint = 0;
    if (ledc_channel_config(&cc) == ESP_OK) s_pwm_pin[slot] = pin; else logf("pwm: channel failed");
}

static adc_oneshot_unit_handle_t s_adc = nullptr;
static void doAdcRead(cJSON *a) {   // adc.read{pin,to}: raw 0-4095 in to, millivolts (approx.) in to.mv
    int pin = (int)numOf(a, "pin", -1); std::string to = strOf(a, "to"); if (to.empty()) to = "adc";
    static const int map[][2] = {{36, 0}, {39, 3}, {32, 4}, {34, 6}, {35, 7}}; int ch = -1;
    for (auto &m : map) if (m[0] == pin) ch = m[1];
    if (ch < 0 || !pinAllowed(pin)) { logf("adc.read: pin %d is not an ADC pin (36 39 32 34 35)", pin); return; }
    if (!s_adc) { adc_oneshot_unit_init_cfg_t uc = {}; uc.unit_id = ADC_UNIT_1; uc.ulp_mode = ADC_ULP_MODE_DISABLE; if (adc_oneshot_new_unit(&uc, &s_adc) != ESP_OK) { logf("adc: init failed"); return; } }
    adc_oneshot_chan_cfg_t cc = {}; cc.atten = ADC_ATTEN_DB_12; cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(s_adc, (adc_channel_t)ch, &cc);
    int raw = 0; if (adc_oneshot_read(s_adc, (adc_channel_t)ch, &raw) != ESP_OK) { logf("adc: read failed"); return; }
    s_state[to] = Value::N(raw); s_state[to + ".mv"] = Value::N(raw * 3300 / 4095);
}

static i2c_master_bus_handle_t s_i2c = nullptr;
static bool i2cEnsure() {
    if (s_i2c) return true;
    cJSON *cfg = s_app ? cJSON_GetObjectItem(s_app, "i2c") : nullptr; int sda = (int)numOf(cfg, "sda", 21), scl = (int)numOf(cfg, "scl", 22);
    if (!pinAllowed(sda) || !pinAllowed(scl) || sda >= 34 || scl >= 34) { logf("i2c: pins %d/%d not allowed", sda, scl); return false; }
    i2c_master_bus_config_t bc = {}; bc.i2c_port = I2C_NUM_1; bc.sda_io_num = (gpio_num_t)sda; bc.scl_io_num = (gpio_num_t)scl; bc.clk_source = I2C_CLK_SRC_DEFAULT; bc.glitch_ignore_cnt = 7; bc.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bc, &s_i2c) != ESP_OK) { logf("i2c: bus failed"); s_i2c = nullptr; return false; }
    return true;
}
static void i2cRelease() { if (s_i2c) { i2c_del_master_bus(s_i2c); s_i2c = nullptr; } }
static i2c_master_dev_handle_t i2cDev(int addr) { i2c_device_config_t dc = {}; dc.dev_addr_length = I2C_ADDR_BIT_LEN_7; dc.device_address = addr; dc.scl_speed_hz = 100000; i2c_master_dev_handle_t d = nullptr; i2c_master_bus_add_device(s_i2c, &dc, &d); return d; }
static void doI2c(const char *op, cJSON *a) {   // i2c.scan{to} i2c.read{addr,reg,len,to} i2c.write{addr,reg,data:[..]}
    if (!i2cEnsure()) return;
    std::string to = strOf(a, "to"); if (to.empty()) to = "i2c";
    if (!strcmp(op, "scan")) {
        std::string found; for (int ad = 8; ad < 120; ad++) if (i2c_master_probe(s_i2c, ad, 20) == ESP_OK) { char b[8]; snprintf(b, sizeof b, "%s0x%02x", found.empty() ? "" : " ", ad); found += b; }
        s_state[to] = Value::S(found); logf("i2c: %s", found.empty() ? "nothing found" : found.c_str()); return;
    }
    int addr = (int)numOf(a, "addr", -1); if (addr < 0 || addr > 127) { logf("i2c: bad addr"); return; }
    i2c_master_dev_handle_t d = i2cDev(addr); if (!d) { logf("i2c: device failed"); return; }
    cJSON *regj = cJSON_GetObjectItem(a, "reg"); uint8_t reg = (uint8_t)numOf(a, "reg", 0);
    if (!strcmp(op, "read")) {
        int len = clampi((int)numOf(a, "len", 1), 1, 16); uint8_t buf[16];
        esp_err_t e = regj ? i2c_master_transmit_receive(d, &reg, 1, buf, len, 100) : i2c_master_receive(d, buf, len, 100);
        if (e != ESP_OK) logf("i2c.read 0x%02x: %s", addr, esp_err_to_name(e));
        else { std::string hex; for (int i = 0; i < len; i++) { char b[4]; snprintf(b, sizeof b, "%02x", buf[i]); hex += b; s_state[to + "." + std::to_string(i)] = Value::N(buf[i]); } s_state[to] = Value::S(hex); }
    } else {
        uint8_t out[17]; int n = 0; if (regj) out[n++] = reg;
        cJSON *it = nullptr; cJSON_ArrayForEach(it, cJSON_GetObjectItem(a, "data")) { if (n < 17) out[n++] = (uint8_t)fromJson(it).asNum(); }
        esp_err_t e = i2c_master_transmit(d, out, n, 100); if (e != ESP_OK) logf("i2c.write 0x%02x: %s", addr, esp_err_to_name(e));
    }
    i2c_master_bus_rm_device(d);
}

// ---------------------------------------------------------------- actions
static void runActions(cJSON *list, int depth);

struct HttpJob { cJSON *h; SemaphoreHandle_t done; };
static void doHttpSync(cJSON *h) {
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(h, "url")); if (!url) { logf("http: no url"); return; }
    cJSON *m = cJSON_GetObjectItem(h, "method"); const char *method = cJSON_IsString(m) ? m->valuestring : "GET";
    std::string body; cJSON *json = cJSON_GetObjectItem(h, "json");
    if (json) {   // render ${} templates in string values (one level)
        cJSON *copy = cJSON_Duplicate(json, true); cJSON *it = nullptr;
        cJSON_ArrayForEach(it, copy) { if (cJSON_IsString(it)) { Value v = render(it->valuestring); cJSON *nv = v.isNum ? cJSON_CreateNumber(v.num) : cJSON_CreateString(v.str.c_str()); cJSON_ReplaceItemInObject(copy, it->string, nv); it = nv; } }
        char *txt = cJSON_PrintUnformatted(copy); if (txt) { body = txt; free(txt); } cJSON_Delete(copy);
    } else if (cJSON *b = cJSON_GetObjectItem(h, "body")) body = render(cJSON_GetStringValue(b)).asStr();
    std::string u = render(url).asStr();
    esp_http_client_config_t cfg = {};
    cfg.url = u.c_str(); cfg.method = !strcasecmp(method, "POST") ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    cfg.timeout_ms = 12000; cfg.crt_bundle_attach = esp_crt_bundle_attach; cfg.disable_auto_redirect = false; cfg.max_redirection_count = 5;
    esp_http_client_handle_t c = esp_http_client_init(&cfg); if (!c) { logf("http: init failed"); return; }
    if (!body.empty()) { esp_http_client_set_header(c, "Content-Type", json ? "application/json" : "text/plain"); esp_http_client_set_post_field(c, body.c_str(), body.size()); }
    esp_err_t err = esp_http_client_open(c, body.size());
    std::string reply;
    if (err == ESP_OK) {
        if (!body.empty()) esp_http_client_write(c, body.c_str(), body.size());
        esp_http_client_fetch_headers(c);
        char buf[128]; int n;
        while ((n = esp_http_client_read(c, buf, sizeof buf)) > 0 && reply.size() < MC_HTTP_REPLY_MAX) reply.append(buf, n);
        int status = esp_http_client_get_status_code(c);
        logf("http %s -> %d (%d bytes)", method, status, (int)reply.size());
        s_state["http.status"] = Value::N(status);
    } else { logf("http: %s", esp_err_to_name(err)); s_state["http.status"] = Value::N(-1); }
    esp_http_client_cleanup(c);
    if (const char *to = cJSON_GetStringValue(cJSON_GetObjectItem(h, "to"))) { while (!reply.empty() && (reply.back() == '\n' || reply.back() == '\r')) reply.pop_back(); s_state[to] = Value::S(reply); }
}

static void httpWorker(void *arg) { HttpJob *j = (HttpJob *)arg; doHttpSync(j->h); xSemaphoreGive(j->done); vTaskDelete(nullptr); }
static void doHttp(cJSON *h) {
    HttpJob job = {h, xSemaphoreCreateBinary()}; if (!job.done) return;
    if (xTaskCreate(httpWorker, "mc_http", 7168, &job, 3, nullptr) != pdPASS) { logf("http: no memory for worker"); vSemaphoreDelete(job.done); return; }
    xSemaphoreTake(job.done, pdMS_TO_TICKS(20000)); vSemaphoreDelete(job.done);
}
static int s_vm_status = 0; static char s_vm_body[64];
static void vmWriteCb(void *ctx, int status, const uint8_t *data, size_t len, bool end) {
    (void)ctx;
    if (status > 0) s_vm_status = status;
    if (data && len && !s_vm_body[0]) { size_t n = len < sizeof(s_vm_body) - 1 ? len : sizeof(s_vm_body) - 1; memcpy(s_vm_body, data, n); s_vm_body[n] = 0; }
    if (status < 0) { logf("vm.write failed (session)"); s_vm_status = 0; s_vm_body[0] = 0; }
    else if (end) { logf("vm.write -> %d %s", s_vm_status, s_vm_body); s_vm_status = 0; s_vm_body[0] = 0; }
}
static void doVmWrite(cJSON *w) {
    const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(w, "path")); if (!path) { logf("vm.write: no path"); return; }
    if (!s_vm_up) { logf("vm.write: session down"); return; }
    cJSON *b = cJSON_CreateObject(); cJSON_AddStringToObject(b, "path", path);
    cJSON_AddStringToObject(b, "text", render(cJSON_GetStringValue(cJSON_GetObjectItem(w, "text"))).asStr().c_str());
    bool append = cJSON_IsTrue(cJSON_GetObjectItem(w, "append"));
    cJSON_AddBoolToObject(b, "overwrite", !append); cJSON_AddBoolToObject(b, "append", append); cJSON_AddBoolToObject(b, "create_parent", true);
    char *txt = cJSON_PrintUnformatted(b); cJSON_Delete(b); if (!txt) return;
    static const char *const headers[] = {"Content-Type", "application/json", nullptr};
    int64_t id = noise_ctrl_req_open("POST", "/fs/write", headers, false, vmWriteCb, nullptr);
    if (id <= 0) logf("vm.write: busy"); else if (!noise_ctrl_req_send(id, txt, strlen(txt), true, 2000)) logf("vm.write: send failed");
    free(txt);
}

// ---------------------------------------------------------------- talking to your Muse (the chat routes Muse boards use)
// muse.say posts a user turn into your Muse's chat (POST /chat/stream). muse.ask does the same and keeps a
// POST /chat/subscribe stream open to collect the reply text (delta.text_append ... delta.message_done).
static int64_t s_sub_id = 0; static std::string s_sub_line, s_reply; static std::string s_ask_to; static int s_say_status = 0; static char s_say_body[64];
static void sayCb(void *ctx, int status, const uint8_t *data, size_t len, bool end) {
    (void)ctx;
    if (status > 0) s_say_status = status;
    if (data && len && !s_say_body[0]) { size_t n = len < sizeof(s_say_body) - 1 ? len : sizeof(s_say_body) - 1; memcpy(s_say_body, data, n); s_say_body[n] = 0; }
    if (status < 0) { logf("muse.say failed (session)"); s_say_status = 0; s_say_body[0] = 0; }
    else if (end) { logf("muse.say -> %d %s", s_say_status, s_say_status == 200 ? "" : s_say_body); s_say_status = 0; s_say_body[0] = 0; }
}
static void subLine(const std::string &line) {
    cJSON *j = cJSON_Parse(line.c_str()); if (!j) return;
    const char *type = cJSON_GetStringValue(cJSON_GetObjectItem(j, "type"));
    const char *ev = cJSON_GetStringValue(cJSON_GetObjectItem(j, "event"));
    cJSON *payload = cJSON_GetObjectItem(j, "payload");
    if (type && !strcmp(type, "event") && ev) {
        if (!strcmp(ev, "delta.text_append")) { const char *tx = cJSON_GetStringValue(cJSON_GetObjectItem(payload, "text")); if (tx && s_reply.size() < 400) s_reply += tx; }
        else if (!strcmp(ev, "delta.message_done") || !strcmp(ev, "message.assistant")) {
            const char *tx = cJSON_GetStringValue(cJSON_GetObjectItem(payload, "display_text")); if (!tx) tx = cJSON_GetStringValue(cJSON_GetObjectItem(payload, "content"));
            std::string final = tx ? std::string(tx) : s_reply; if (final.size() > 400) final.resize(400);
            if (!s_ask_to.empty()) { s_state[s_ask_to] = Value::S(final); s_dirty_screen = s_dirty_state = true; }
            logf("muse: %.60s", final.c_str()); s_reply.clear();
        }
    }
    cJSON_Delete(j);
}
static void subCb(void *ctx, int status, const uint8_t *data, size_t len, bool end) {
    (void)ctx;
    if (status < 0 || end) { s_sub_id = 0; s_sub_line.clear(); if (status < 0) logf("muse subscribe closed"); else logf("muse subscribe ended %d", status); return; }
    if (status > 0 && status != 200) { logf("muse subscribe -> %d", status); }
    for (size_t i = 0; i < len; i++) {
        char ch = (char)data[i];
        if (ch == '\n') { if (!s_sub_line.empty()) subLine(s_sub_line); s_sub_line.clear(); }
        else if (s_sub_line.size() < 1500) s_sub_line += ch;
    }
}
static bool ensureSubscribed() {
    if (s_sub_id > 0) return true;
    static const char *const headers[] = {"Content-Type", "application/json", "Accept", "application/x-ndjson", nullptr};
    s_sub_id = noise_ctrl_req_open("POST", "/chat/subscribe", headers, false, subCb, nullptr);
    if (s_sub_id <= 0) { logf("muse subscribe: busy"); return false; }
    if (!noise_ctrl_req_send(s_sub_id, "{}", 2, true, 2000)) { logf("muse subscribe: send failed"); s_sub_id = 0; return false; }
    return true;
}
static void doMuseSay(const char *text, const char *to) {
    if (!s_vm_up) { logf("muse.say: session down"); return; }
#if CONFIG_SPIRAM
    if (to) { s_ask_to = to; s_reply.clear(); ensureSubscribed(); }
#else
    // Chat subscriptions arrive as 16 KB frames, larger than this board's trimmed session buffers (the frame
    // resets the session). Post only; the reply shows in the Muse app. Boards with PSRAM read it back here.
    if (to) { s_ask_to.clear(); s_state[to] = Value::S("see your Muse chat"); s_dirty_screen = true; }
#endif
    cJSON *b = cJSON_CreateObject(); cJSON_AddStringToObject(b, "message", render(text).asStr().c_str()); cJSON_AddStringToObject(b, "output_modality", "text");
    char *txt = cJSON_PrintUnformatted(b); cJSON_Delete(b); if (!txt) return;
    static const char *const headers[] = {"Content-Type", "application/json", nullptr};
    int64_t id = noise_ctrl_req_open("POST", "/chat/stream", headers, false, sayCb, nullptr);
    if (id <= 0) logf("muse.say: busy"); else if (!noise_ctrl_req_send(id, txt, strlen(txt), true, 2000)) logf("muse.say: send failed");
    free(txt);
}

static void doGpioWrite(cJSON *g) {
    int pin = (int)fromJson(cJSON_GetObjectItem(g, "pin")).asNum(); int v = fromJson(cJSON_GetObjectItem(g, "value")).truthy();
    if (!pinAllowed(pin) || pin >= 34) { logf("gpio.write: pin %d not allowed", pin); return; }
    gpio_reset_pin((gpio_num_t)pin); gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT); gpio_set_level((gpio_num_t)pin, v);
}
static void doGpioRead(cJSON *g) {
    int pin = (int)fromJson(cJSON_GetObjectItem(g, "pin")).asNum(); const char *to = cJSON_GetStringValue(cJSON_GetObjectItem(g, "to"));
    if (!pinAllowed(pin)) { logf("gpio.read: pin %d not allowed", pin); return; }
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT); if (pin < 34) gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY);
    s_state[to ? to : "gpio"] = Value::N(gpio_get_level((gpio_num_t)pin));
}
static void doLed(const char *hex) {
#if CONFIG_HOMEHUB_LED_BACKEND_CYD_ST7796
    const int R = 22, G = 16, B = 17;
#else
    const int R = 4, G = 16, B = 17;
#endif
    std::string h = render(hex).asStr(); if (!h.empty() && h[0] == '#') h.erase(0, 1);
    unsigned rgb = (unsigned)strtoul(h.c_str(), nullptr, 16);
    int pins[3] = {R, G, B}; int on[3] = {(int)((rgb >> 16) & 0xff) > 127, (int)((rgb >> 8) & 0xff) > 127, (int)(rgb & 0xff) > 127};
    for (int k = 0; k < 3; k++) { gpio_reset_pin((gpio_num_t)pins[k]); gpio_set_direction((gpio_num_t)pins[k], GPIO_MODE_OUTPUT); gpio_set_level((gpio_num_t)pins[k], !on[k]); }   // active low
}
static void doDrawRect(cJSON *d) {   // draw.rect{x,y,w,h,color}: screen coordinates, kept off Meta's status bars
    int x = (int)numOf(d, "x", 0), y = (int)numOf(d, "y", TOP_CLEAR), w = (int)numOf(d, "w", 10), h = (int)numOf(d, "h", 10);
    int y0 = y < TOP_CLEAR ? TOP_CLEAR : y, y1 = y + h > s_screen_h - TOP_CLEAR ? s_screen_h - TOP_CLEAR : y + h;
    if (y1 > y0 && w > 0) fillRect(x, y0, w, y1 - y0, hexColor(strOf(d, "color"), C_TEXT));
}
static void doDrawText(cJSON *d) {   // draw.text{x?,y,text,scale,color}: x omitted = centred
    int y = (int)numOf(d, "y", TOP_CLEAR), scale = clampi((int)numOf(d, "scale", 2), 1, 8);
    if (y < TOP_CLEAR || y + 8 * scale > s_screen_h - TOP_CLEAR) return;
    std::string t = strOf(d, "text"); uint16_t c = hexColor(strOf(d, "color"), C_TEXT);
    cJSON *xj = cJSON_GetObjectItem(d, "x");
    if (!xj) drawText(y, t, scale, c);
    else { int x = (int)fromJson(xj).asNum(); int w = (int)t.size() * (PIXEL_FONT_WIDTH * scale + scale); drawTextIn(x, y, w, t, scale, c, C_BG); }
}

static void runOne(cJSON *a, int depth) {
    if (!cJSON_IsObject(a) || depth > 6) return;
    if (cJSON *x = cJSON_GetObjectItem(a, "set")) { cJSON *it = nullptr; cJSON_ArrayForEach(it, x) s_state[it->string] = fromJson(it); s_dirty_screen = s_dirty_state = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "inc")) { double by = cJSON_GetObjectItem(a, "by") ? fromJson(cJSON_GetObjectItem(a, "by")).asNum() : 1; s_state[x->valuestring] = Value::N(s_state[x->valuestring].asNum() + by); s_dirty_screen = s_dirty_state = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "dec")) { double by = cJSON_GetObjectItem(a, "by") ? fromJson(cJSON_GetObjectItem(a, "by")).asNum() : 1; s_state[x->valuestring] = Value::N(s_state[x->valuestring].asNum() - by); s_dirty_screen = s_dirty_state = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "toggle")) { s_state[x->valuestring] = Value::N(!s_state[x->valuestring].truthy()); s_dirty_screen = s_dirty_state = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "log")) logf("%s", render(cJSON_GetStringValue(x)).asStr().c_str());
    if (cJSON *x = cJSON_GetObjectItem(a, "gpio.write")) doGpioWrite(x);
    if (cJSON *x = cJSON_GetObjectItem(a, "gpio.read")) { doGpioRead(x); s_dirty_screen = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "led")) doLed(cJSON_GetStringValue(x));
    if (cJSON *x = cJSON_GetObjectItem(a, "tone")) doTone(x);
    if (cJSON *x = cJSON_GetObjectItem(a, "pwm")) doPwm(x);
    if (cJSON *x = cJSON_GetObjectItem(a, "adc.read")) { doAdcRead(x); s_dirty_screen = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "i2c.scan")) { doI2c("scan", x); s_dirty_screen = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "i2c.read")) { doI2c("read", x); s_dirty_screen = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "i2c.write")) doI2c("write", x);
    if (cJSON *x = cJSON_GetObjectItem(a, "draw.rect")) doDrawRect(x);
    if (cJSON *x = cJSON_GetObjectItem(a, "draw.text")) doDrawText(x);
    if (cJSON_GetObjectItem(a, "clear")) { clearRows(TOP_CLEAR, s_screen_h - TOP_CLEAR); if (!s_canvas) s_dirty_screen = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "http")) { doHttp(x); s_dirty_screen = true; }
    if (cJSON *x = cJSON_GetObjectItem(a, "vm.write")) doVmWrite(x);
    if (cJSON *x = cJSON_GetObjectItem(a, "muse.say")) doMuseSay(cJSON_GetStringValue(x), nullptr);
    if (cJSON *x = cJSON_GetObjectItem(a, "muse.ask")) { const char *to = cJSON_GetStringValue(cJSON_GetObjectItem(x, "to")); doMuseSay(cJSON_GetStringValue(cJSON_GetObjectItem(x, "text")), to ? to : "reply"); }
    if (cJSON *x = cJSON_GetObjectItem(a, "if")) {
        bool ok = fromJson(x).truthy();
        runActions(cJSON_GetObjectItem(a, ok ? "then" : "else"), depth + 1);
    }
    if (cJSON_GetObjectItem(a, "exit")) s_exit = true;
}
static void runActions(cJSON *list, int depth) { cJSON *a = nullptr; cJSON_ArrayForEach(a, list) { runOne(a, depth); if (s_exit) break; } }
static bool hasHandler(const char *name) { if (!s_app) return false; cJSON *on = cJSON_GetObjectItem(s_app, "on"); return on && cJSON_GetObjectItem(on, name); }
static void fire(const char *name) { if (!s_app) return; cJSON *on = cJSON_GetObjectItem(s_app, "on"); if (!on) return; cJSON *h = cJSON_GetObjectItem(on, name); if (h) runActions(h, 0); }

// ---------------------------------------------------------------- the applet screen
static void renderScreen() {
    if (!s_app || !ensureStrip()) return;
    const int top = TOP_CLEAR, bottom = s_screen_h - TOP_CLEAR;
    if (!s_cleared) { clearRows(0, s_screen_h); s_cleared = true; }
    s_hits.clear(); s_canvas = false;
    int y = top + 8;
    cJSON *name = cJSON_GetObjectItem(s_app, "name");
    drawText(y, cJSON_IsString(name) ? name->valuestring : s_app_id, 3, C_MUTED); y += 8 * 3 + 12;
    cJSON *screen = cJSON_GetObjectItem(s_app, "screen"); cJSON *it = nullptr;
    cJSON_ArrayForEach(it, screen) {
        if (y >= bottom - 16) break;
        if (cJSON *t = cJSON_GetObjectItem(it, "text")) { drawText(y, render(cJSON_GetStringValue(t)).asStr(), 3, C_TEXT); y += 8 * 3 + 10; }
        else if (cJSON *b = cJSON_GetObjectItem(it, "big")) { drawText(y, render(cJSON_GetStringValue(b)).asStr(), 8, C_BIG); y += 8 * 8 + 14; }
        else if (cJSON *bar = cJSON_GetObjectItem(it, "bar")) { double mx = cJSON_GetObjectItem(it, "max") ? fromJson(cJSON_GetObjectItem(it, "max")).asNum() : 100; drawBar(y, mx > 0 ? fromJson(bar).asNum() / mx : 0, 16); y += 16 + 12; }
        else if (cJSON *g = cJSON_GetObjectItem(it, "gap")) { y += (int)fromJson(g).asNum(); }
        else if (cJSON *bt = cJSON_GetObjectItem(it, "button")) {
            if (y + BTN_H > bottom) break;
            int x = 12, w = s_screen_w - 24; drawButton(x, y, w, render(cJSON_GetStringValue(bt)).asStr(), hexColor(strOf(it, "color"), C_BTN));
            s_hits.push_back({x, y, w, BTN_H, strOf(it, "event")}); y += BTN_H + 8;
        }
        else if (cJSON *bs = cJSON_GetObjectItem(it, "buttons")) {
            if (y + BTN_H > bottom) break;
            int n = cJSON_GetArraySize(bs);
            if (n < 1) continue;
            if (n > 4) n = 4;
            int gap = 8, w = (s_screen_w - 24 - gap * (n - 1)) / n, x = 12; cJSON *b = nullptr; int k = 0;
            cJSON_ArrayForEach(b, bs) { if (k++ >= n) break; drawButton(x, y, w, strOf(b, "label"), hexColor(strOf(b, "color"), C_BTN)); s_hits.push_back({x, y, w, BTN_H, strOf(b, "event")}); x += w + gap; }
            y += BTN_H + 8;
        }
        else if (cJSON_GetObjectItem(it, "canvas")) { s_canvas = true; break; }   // the rest of the screen belongs to draw.* actions
    }
    if (!s_canvas && y < bottom) clearRows(y, bottom);
    s_dirty_screen = false;
}

// ---------------------------------------------------------------- persistence
static void saveState() {
    cJSON *persist = s_app ? cJSON_GetObjectItem(s_app, "persist") : nullptr;
    cJSON *o = cJSON_CreateObject(); cJSON *k = nullptr;
    cJSON_ArrayForEach(k, persist) { if (!cJSON_IsString(k)) continue; auto it = s_state.find(k->valuestring); if (it == s_state.end()) continue; if (it->second.isNum) cJSON_AddNumberToObject(o, k->valuestring, it->second.num); else cJSON_AddStringToObject(o, k->valuestring, it->second.str.c_str()); }
    char *txt = cJSON_PrintUnformatted(o); cJSON_Delete(o); if (!txt) return;
    storeSet(key("s.", s_app_id).c_str(), txt);
    free(txt); s_dirty_state = false; s_last_save = esp_timer_get_time();
}
static void loadState() {
    char *txt = storeGet(key("s.", s_app_id).c_str(), 4096); if (!txt) return;
    cJSON *o = cJSON_Parse(txt); cJSON *it = nullptr; cJSON_ArrayForEach(it, o) setState(it->string, it); cJSON_Delete(o); free(txt);
}

// ---------------------------------------------------------------- lifecycle (tick task context)
static void stopApplet(bool restoreScreen) {
    if (s_app) { fire("stop"); cJSON_Delete(s_app); s_app = nullptr; }
    s_timers.clear(); s_watches.clear(); s_state.clear(); s_hits.clear(); s_running = false; s_exit = false; s_cleared = false; s_canvas = false;
    toneSet(0); s_tone_q.clear(); pwmStopAll(); i2cRelease();
    if (s_sub_id > 0) { noise_ctrl_req_cancel(s_sub_id); s_sub_id = 0; }
    if (restoreScreen) led_status_show_animation();
}
static bool startApplet(cJSON *doc, bool fresh) {
    stopApplet(false);
    s_app = doc; cJSON *id = cJSON_GetObjectItem(doc, "id"); s_app_id = slug(cJSON_IsString(id) ? id->valuestring : "applet");
    cJSON *st = cJSON_GetObjectItem(doc, "state"); cJSON *it = nullptr;
    cJSON_ArrayForEach(it, st) setState(it->string, it);
    if (!fresh) loadState();
    cJSON *on = cJSON_GetObjectItem(doc, "on"); int64_t now = esp_timer_get_time();
    cJSON_ArrayForEach(it, on) {
        const char *k = it->string;
        if (!strncmp(k, "every:", 6)) { int ms = (int)(atof(k + 6) * 1000); if (ms >= 100 && s_timers.size() < 8) s_timers.push_back({ms, now + (int64_t)ms * 1000, k}); }
        else if (!strncmp(k, "gpio:", 5)) {
            int pin = atoi(k + 5); const char *m = strchr(k + 5, ':'); int mode = m && !strcmp(m + 1, "fall") ? 1 : m && !strcmp(m + 1, "change") ? 2 : 0;
            if (pinAllowed(pin) && s_watches.size() < 4) { gpio_reset_pin((gpio_num_t)pin); gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT); if (pin < 34) gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY); s_watches.push_back({pin, mode, gpio_get_level((gpio_num_t)pin), k}); }
        }
    }
    ensureStrip();
    s_running = true; s_dirty_screen = true; s_launcher = false;
    storeSet("run", s_app_id.c_str());
    logf("applet %s started", s_app_id.c_str());
    fire("start");
    return true;
}
static bool startStored(const std::string &id, bool fresh) {
    char *txt = storeGet(key("a.", id).c_str(), MC_MAX_APPLET_BYTES); if (!txt) return false;
    cJSON *doc = cJSON_Parse(txt); free(txt); if (!doc) return false;
    return startApplet(doc, fresh);
}

// ---------------------------------------------------------------- launcher (tap the title, or the idle screen)
static int64_t s_repair_until = 0;
static void launcherDraw() {
    s_hits.clear(); clearRows(0, s_screen_h);
    int y = TOP_CLEAR + 8; drawText(y, "Musecorder", 3, C_MUTED); y += 8 * 3 + 12;
    auto ids = storeIds();
    for (auto &id : ids) {
        if (y + BTN_H > s_screen_h - TOP_CLEAR - 2 * (BTN_H + 8)) break;
        char *nm = storeGet(key("n.", id).c_str(), 40); std::string label = nm ? nm : id; free(nm);
        if (s_running && id == s_app_id) label = "* " + label;
        drawButton(12, y, s_screen_w - 24, label, C_BTN); s_hits.push_back({12, y, s_screen_w - 24, BTN_H, "run:" + id}); y += BTN_H + 8;
    }
    if (ids.empty()) { drawText(y + 8, "no applets yet", 2, C_MUTED); drawText(y + 32, "ask your Muse to install one", 2, C_MUTED); y += 64; }
    y = s_screen_h - TOP_CLEAR - 2 * (BTN_H + 8);
    drawButton(12, y, s_screen_w - 24, s_running ? "Stop: status screen" : "Status screen", C_BTN_HOT); s_hits.push_back({12, y, s_screen_w - 24, BTN_H, "status"}); y += BTN_H + 8;
    int third = (s_screen_w - 24 - 16) / 3;
    drawButton(12, y, third, "Back", C_BTN); s_hits.push_back({12, y, third, BTN_H, "back"});
    drawButton(12 + third + 8, y, third, "Touch", C_BTN); s_hits.push_back({12 + third + 8, y, third, BTN_H, "calibrate"});
    bool arming = s_repair_until > esp_timer_get_time();
    drawButton(12 + 2 * (third + 8), y, third, arming ? "SURE?" : "Re-pair", arming ? C_BTN_HOT : C_BTN); s_hits.push_back({12 + 2 * (third + 8), y, third, BTN_H, "repair"});
    if (arming) drawText(y - 24, "tap SURE? to erase pairing", 2, C_BAR);
}
static void launcherOpen() { s_launcher = true; s_repair_until = 0; launcherDraw(); ESP_LOGI(TAG, "launcher open"); }
static void launcherClose() { s_launcher = false; s_hits.clear(); s_cleared = false; if (s_running) s_dirty_screen = true; else led_status_show_animation(); }
static void launcherTap(const std::string &ev) {
    ESP_LOGI(TAG, "launcher: %s", ev.c_str());
    if (ev == "back") launcherClose();
    else if (ev == "calibrate") wizStart();
    else if (ev == "repair") {   // two taps within 8 s: the same as holding BOOT for 5 s (erase pairing, reboot into setup)
        if (s_repair_until > esp_timer_get_time()) { s_repair_until = 0; drawText(s_screen_h / 2, "erasing pairing...", 2, C_TEXT); app_touch_long_press(); }
        else { s_repair_until = esp_timer_get_time() + 8000000; launcherDraw(); }
    }
    else if (ev == "status") { if (s_running) { storeSet("run", nullptr); stopApplet(false); } launcherClose(); }
    else if (!ev.compare(0, 4, "run:")) { std::string id = ev.substr(4); s_launcher = false; s_hits.clear(); if (!(s_running && id == s_app_id)) { if (!startStored(id, false)) { logf("launch %s failed", id.c_str()); launcherClose(); } } else launcherClose(); }
}

// ---------------------------------------------------------------- touch dispatch
static bool s_down = false, s_wiz_wait_up = false, s_held = false; static int s_dx = 0, s_dy = 0, s_down_n = 0, s_up_n = 0;
static void onTap(int x, int y) {
    if (s_launcher) { for (auto &h : s_hits) if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) { launcherTap(h.ev); return; } return; }
    if (!s_running) { if (link_pairing_confirmation_required()) { ESP_LOGI(TAG, "touch confirms pairing"); app_touch_short_press(); } else launcherOpen(); return; }
    for (auto &h : s_hits) if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) { if (!h.ev.empty()) fire(h.ev.c_str()); return; }
    if (y < TOP_CLEAR + 60) { launcherOpen(); return; }   // the title band
    s_state["touch.x"] = Value::N(x); s_state["touch.y"] = Value::N(y); fire("touch");
}
static void touchTick() {
    if (!s_touch_ok || esp_timer_get_time() < 2000000) return;   // the pen IRQ reads low briefly after power-up: ignore the first 2 s
    int x = 0, y = 0; bool p = touchRead(&x, &y);
    if (s_wiz >= 0) { if (s_wiz_wait_up) { if (!p) s_wiz_wait_up = false; return; } if (p) { s_down = true; s_up_n = 0; } else if (s_down) { if (++s_up_n < 3) return; s_down = false; ESP_LOGI(TAG, "touch setup sample %d: raw %d,%d", s_wiz + 1, s_raw_x, s_raw_y); wizSample(); } return; }
    if (p) {
        s_up_n = 0;
        if (!s_down) {
            s_down = true; s_down_n = 0; s_held = false;
            ESP_LOGI(TAG, "touch down raw %d,%d -> %d,%d%s", s_raw_x, s_raw_y, x, y, s_cal.valid ? "" : " (uncalibrated)");
            if (!s_cal.valid) { wizStart(); s_down = false; s_wiz_wait_up = true; return; }   // the finger that opened it is not a sample   // first touch ever: calibrate before anything else
        }
        else s_down_n++;
        s_dx = x; s_dy = y;
        if (!s_held && s_down_n == 1000 / MC_TICK_MS && !s_launcher) {   // held for a second: the launcher, from any screen
            s_held = true; bool onButton = false;
            for (auto &h : s_hits) if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) onButton = true;
            if (!onButton) launcherOpen();
        }
        if (s_running && !s_launcher && hasHandler("touch:move")) { s_state["touch.x"] = Value::N(x); s_state["touch.y"] = Value::N(y); fire("touch:move"); }
    } else if (s_down) {
        if (++s_up_n < 3) return;   // resistive contact flickers: the pen is up only after 3 quiet ticks (150 ms)
        s_down = false;
        if (s_held) { s_held = false; return; }
        if (s_down_n + 1 >= MC_TAP_SAMPLES) onTap(s_dx, s_dy);
    }
}

static void tickTask(void *) {
    char *run = storeGet("run", MC_MAX_ID + 1);
    if (run) { xSemaphoreTake(s_lock, portMAX_DELAY); if (!startStored(run, false)) logf("stored applet %s missing", run); xSemaphoreGive(s_lock); free(run); }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(MC_TICK_MS));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int64_t now = esp_timer_get_time();
        touchTick();
        if (s_tone_on && now >= s_tone_until) toneNext(now);
        if (s_running) {
            for (auto &t : s_timers) if (now >= t.next) { t.next = now + (int64_t)t.period_ms * 1000; fire(t.key); }
            for (auto &w : s_watches) { int v = gpio_get_level((gpio_num_t)w.pin); if (v != w.last) { bool hit = w.mode == 2 || (w.mode == 0 && v) || (w.mode == 1 && !v); w.last = v; if (hit) fire(w.key); } }
            if (s_tap_pending) { s_tap_pending = false; fire("boot:press"); }
            while (!s_pending.empty()) { std::string e = s_pending.front(); s_pending.erase(s_pending.begin()); fire(e.c_str()); }
            if (s_exit) { logf("applet exited"); storeSet("run", nullptr); stopApplet(true); }
            else {
                if (s_dirty_screen && !s_launcher && s_wiz < 0) renderScreen();
                if (s_dirty_state && now - s_last_save > 2000000) saveState();
            }
        } else s_tap_pending = false;
        xSemaphoreGive(s_lock);
    }
}

// ---------------------------------------------------------------- public API
extern "C" void musecorder_init(void) {
    s_lock = xSemaphoreCreateMutex();
    if (!led_status_display_info(&s_screen_w, &s_screen_h)) { s_screen_w = 0; }
    storeInit();
    s_touch_ok = s_screen_w > 0 && touchInit();
    if (s_touch_ok) calLoad();
    button_set_press_cb(pressCb);
    xTaskCreate(tickTask, "musecorder", 5120, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "ready (%dx%d, touch %s, store %s)", s_screen_w, s_screen_h, s_touch_ok ? "yes" : "no", s_store_ok ? "ok" : "none");
}
extern "C" void musecorder_vm_connected(void) { s_vm_up = true; }
extern "C" void musecorder_screen_reset(void) { s_cleared = false; s_dirty_screen = true; if (s_launcher) s_launcher = false; }
static bool pressCb(bool pressed) {
    if (s_running && pressed) s_tap_pending = true;   // instant boot:press for the applet
    return false;   // not claimed: a 5 s hold still resets setup (re-pair, new Wi-Fi) while an applet runs
}
extern "C" bool musecorder_on_tap(void) {
    return s_running;   // the press callback already queued it; skip Meta's (idle) short-press handling
}

static cJSON *errorResult(const char *code, const char *message) {
    cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", false);
    cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e, "code", code); cJSON_AddStringToObject(e, "message", message); cJSON_AddItemToObject(r, "error", e);
    return r;
}
static cJSON *statePayload() {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "applet", s_running ? s_app_id.c_str() : "");
    cJSON_AddBoolToObject(p, "running", s_running);
    cJSON *st = cJSON_CreateObject(); for (auto &kv : s_state) { if (kv.second.isNum) cJSON_AddNumberToObject(st, kv.first.c_str(), kv.second.num); else cJSON_AddStringToObject(st, kv.first.c_str(), kv.second.str.c_str()); }
    cJSON_AddItemToObject(p, "state", st);
    cJSON *lg = cJSON_CreateArray(); for (auto &l : s_log) cJSON_AddItemToArray(lg, cJSON_CreateString(l.c_str())); cJSON_AddItemToObject(p, "log", lg);
    cJSON *ids = cJSON_CreateArray(); for (auto &id : storeIds()) cJSON_AddItemToArray(ids, cJSON_CreateString(id.c_str())); cJSON_AddItemToObject(p, "stored", ids);
    cJSON_AddBoolToObject(p, "touch", s_touch_ok); cJSON_AddBoolToObject(p, "touch_calibrated", s_cal.valid);
    cJSON_AddNumberToObject(p, "free", (double)esp_get_free_heap_size());
    cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true); cJSON_AddItemToObject(r, "payload", p);
    return r;
}

// ---------------------------------------------------------------- known Wi-Fi networks (Meta keeps up to 8; the app only adds one per setup)
static cJSON *wifiList() {
    wifi_known_list_t *l = (wifi_known_list_t *)calloc(1, sizeof(wifi_known_list_t)); if (!l) return cJSON_CreateArray();
    int n = wifi_known_load(l); cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n && i < WIFI_KNOWN_MAX; i++) { cJSON *e = cJSON_CreateObject(); cJSON_AddStringToObject(e, "ssid", l->nets[i].ssid); if (l->nets[i].hidden) cJSON_AddTrueToObject(e, "hidden"); cJSON_AddItemToArray(arr, e); }
    wifi_known_wipe(l); free(l); return arr;   // names only: passwords never leave the device
}
static cJSON *wifiCommand(const char *command, cJSON *params) {
    if (!strcmp(command, "wifi.list")) { cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true); cJSON *p = cJSON_CreateObject(); cJSON_AddItemToObject(p, "networks", wifiList()); cJSON_AddItemToObject(r, "payload", p); return r; }
    const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(params, "ssid"));
    if (!ssid || !ssid[0] || strlen(ssid) > WIFI_KNOWN_SSID_MAX) return errorResult("invalid_params", "ssid (1-32 chars) is required");
    if (!strcmp(command, "wifi.forget")) { bool ok = wifi_known_forget(ssid); cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", ok); cJSON *p = cJSON_CreateObject(); cJSON_AddItemToObject(p, "networks", wifiList()); cJSON_AddItemToObject(r, "payload", p); return r; }
    if (!strcmp(command, "wifi.add")) {
        const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(params, "password")); if (!pass) pass = "";
        if (strlen(pass) > WIFI_KNOWN_PASS_MAX) return errorResult("invalid_params", "password too long");
        cJSON *h = cJSON_GetObjectItem(params, "hidden"); int hidden = cJSON_IsTrue(h) || (cJSON_IsString(h) && !strcmp(h->valuestring, "true")) ? 1 : 0;
        if (!wifi_known_remember(ssid, pass, hidden)) return errorResult("storage", "could not save the network");
        ESP_LOGI(TAG, "wifi.add stored a network");
        cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true); cJSON *p = cJSON_CreateObject(); cJSON_AddItemToObject(p, "networks", wifiList()); cJSON_AddStringToObject(p, "note", "joins it when the current network is out of range (or after a reboot)"); cJSON_AddItemToObject(r, "payload", p); return r;
    }
    return errorResult("unknown_command", "no such wifi command");
}
// Serial console (sdk_token_console.c hands over lines it does not own): >wifi={"ssid":"..","password":"..","hidden":false}
// stores a network and restarts so the boot join picks it up; >wifi={"forget":".."}; >wifi=list. Nothing echoes the password.
extern "C" bool musecorder_console_command(const char *line) {
    if (strncmp(line, "wifi=", 5)) return false;
    const char *arg = line + 5;
    if (!strcmp(arg, "list")) { cJSON *l = wifiList(); char *t = cJSON_PrintUnformatted(l); printf("@wifi list %s\n", t ? t : "[]"); fflush(stdout); free(t); cJSON_Delete(l); return true; }
    cJSON *j = cJSON_Parse(arg); cJSON *r = nullptr;
    if (!j) { printf("@wifi error json\n"); fflush(stdout); return true; }
    if (cJSON_GetObjectItem(j, "forget")) { cJSON *q = cJSON_CreateObject(); cJSON_AddStringToObject(q, "ssid", cJSON_GetStringValue(cJSON_GetObjectItem(j, "forget"))); r = wifiCommand("wifi.forget", q); cJSON_Delete(q); }
    else r = wifiCommand("wifi.add", j);
    bool ok = cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")); const char *msg = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(r, "error"), "message"));
    printf("@wifi %s%s%s\n", ok ? "ok" : "error ", ok ? "" : (msg ? msg : ""), ok && !cJSON_GetObjectItem(j, "forget") ? " restarting" : ""); fflush(stdout);
    bool restart = ok && !cJSON_GetObjectItem(j, "forget");
    cJSON_Delete(r); cJSON_Delete(j);
    if (restart) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
    return true;
}

extern "C" cJSON *musecorder_command(const char *command, cJSON *params) {
    if (!strncmp(command, "wifi.", 5)) return wifiCommand(command, params);
    if (strncmp(command, "applet.", 7)) return nullptr;
    if (!s_lock) return errorResult("unavailable", "runtime not ready");
    if (!strcmp(command, "applet.install")) {
        cJSON *j = cJSON_GetObjectItem(params, "json");
        if (!j) {   // start an applet already stored on the gadget
            const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(params, "id"));
            if (!id) return errorResult("missing_param", "json (a new applet) or id (a stored one) is required");
            xSemaphoreTake(s_lock, portMAX_DELAY); bool ok = startStored(slug(id), false); cJSON *r = ok ? statePayload() : errorResult("not_found", "no stored applet with that id"); xSemaphoreGive(s_lock);
            return r;
        }
        char *txt = cJSON_IsString(j) ? strdup(j->valuestring) : cJSON_PrintUnformatted(j);
        if (!txt) return errorResult("out_of_memory", "failed to copy applet");
        if (strlen(txt) > MC_MAX_APPLET_BYTES) { free(txt); return errorResult("too_large", "applet must be under 4 KB"); }
        cJSON *doc = cJSON_Parse(txt);
        if (!doc || !cJSON_IsObject(doc) || !cJSON_IsString(cJSON_GetObjectItem(doc, "id"))) { cJSON_Delete(doc); free(txt); return errorResult("invalid_params", "json must be an applet object with an id"); }
        std::string id = slug(cJSON_GetObjectItem(doc, "id")->valuestring);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        auto ids = storeIds(); bool known = false; for (auto &i : ids) if (i == id) known = true;
        if (!known && ids.size() >= MC_MAX_APPLETS) {
            xSemaphoreGive(s_lock); cJSON_Delete(doc); free(txt);
            std::string msg = "5 applets stored; applet.remove one first:"; for (auto &i : ids) msg += " " + i;
            return errorResult("too_many", msg.c_str());
        }
        if (!known) { ids.push_back(id); storeSetIds(ids); }
        storeSet(key("a.", id).c_str(), txt); free(txt);
        const char *nm = cJSON_GetStringValue(cJSON_GetObjectItem(doc, "name")); storeSet(key("n.", id).c_str(), nm ? nm : id.c_str());
        startApplet(doc, true);
        cJSON *r = statePayload();
        xSemaphoreGive(s_lock);
        return r;
    }
    if (!strcmp(command, "applet.remove")) {
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(params, "id")); bool all = cJSON_IsTrue(cJSON_GetObjectItem(params, "all"));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool was = s_running; int removed = 0;
        if (all) { auto ids = storeIds(); removed = (int)ids.size(); for (auto &i : ids) storeDelete(i); storeSet("run", nullptr); stopApplet(true); }
        else if (id) { std::string sid = slug(id); if (s_running && s_app_id == sid) { storeSet("run", nullptr); stopApplet(true); } storeDelete(sid); removed = 1; }
        else { storeSet("run", nullptr); stopApplet(true); }
        xSemaphoreGive(s_lock);
        cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true); cJSON *p = cJSON_CreateObject(); cJSON_AddBoolToObject(p, "stopped", was); cJSON_AddNumberToObject(p, "deleted", removed); cJSON_AddItemToObject(r, "payload", p); return r;
    }
    if (!strcmp(command, "applet.state")) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (cJSON *set = cJSON_GetObjectItem(params, "set")) { cJSON *it = nullptr; cJSON_ArrayForEach(it, set) setState(it->string, it); s_dirty_screen = s_dirty_state = true; }
        if (cJSON_IsTrue(cJSON_GetObjectItem(params, "calibrate")) && s_touch_ok) wizStart();
        cJSON *r = statePayload(); xSemaphoreGive(s_lock); return r;
    }
    if (!strcmp(command, "applet.event")) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(params, "name"));
        if (!name) return errorResult("missing_param", "name is required");
        xSemaphoreTake(s_lock, portMAX_DELAY); if (s_running && s_pending.size() < 8) s_pending.push_back(name); xSemaphoreGive(s_lock);
        cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true); cJSON *p = cJSON_CreateObject(); cJSON_AddBoolToObject(p, "queued", s_running); cJSON_AddItemToObject(r, "payload", p); return r;
    }
    return errorResult("unknown_command", "no such applet command");
}
