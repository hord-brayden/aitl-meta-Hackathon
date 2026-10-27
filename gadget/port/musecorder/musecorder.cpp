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
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "driver/gpio.h"
#include "nvs.h"
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
}

static const char *TAG = "musecorder";
static bool pressCb(bool pressed);

#define MC_MAX_APPLET_BYTES 4096
#define MC_MAX_LOG 12
#define MC_TICK_MS 100
#define MC_HTTP_REPLY_MAX 384

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
    if (to) { s_ask_to = to; s_reply.clear(); ensureSubscribed(); }
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
static void fire(const char *name) { if (!s_app) return; cJSON *on = cJSON_GetObjectItem(s_app, "on"); if (!on) return; cJSON *h = cJSON_GetObjectItem(on, name); if (h) runActions(h, 0); }

// ---------------------------------------------------------------- screen
#define STRIP_ROWS 4
static uint16_t *s_strip = nullptr;   // s_screen_w * STRIP_ROWS

static inline uint16_t be565(uint8_t r, uint8_t g, uint8_t b) { uint16_t v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); return (uint16_t)((v >> 8) | (v << 8)); }
static const uint16_t C_BG = 0, C_TEXT = be565(0xf3, 0xf4, 0xf8), C_BIG = be565(0x2e, 0xe6, 0xd6), C_MUTED = be565(0x9a, 0xa3, 0xb5), C_BAR = be565(0xe8, 0x79, 0xf9), C_BAR_BG = be565(0x2a, 0x2f, 0x3d);

// Draw one text line of height 8*scale at y (top), centred, into strips and out to the panel.
static void drawText(int y, const std::string &text, int scale, uint16_t color) {
    int cw = PIXEL_FONT_WIDTH * scale + scale, h = PIXEL_FONT_HEIGHT * scale;
    int maxc = s_screen_w / cw; std::string t = text.size() > (size_t)maxc ? text.substr(0, maxc) : text;
    int x0 = (s_screen_w - (int)t.size() * cw) / 2;
    for (int sy = 0; sy < h; sy += STRIP_ROWS) {
        int rows = h - sy < STRIP_ROWS ? h - sy : STRIP_ROWS;
        for (int r = 0; r < rows; r++) {
            uint16_t *line = s_strip + r * s_screen_w; for (int x = 0; x < s_screen_w; x++) line[x] = C_BG;
            int fy = (sy + r) / scale;
            for (size_t ci = 0; ci < t.size(); ci++) {
                unsigned char ch = t[ci]; if (ch < PIXEL_FONT_FIRST || ch > PIXEL_FONT_LAST) ch = '?';
                for (int gx = 0; gx < PIXEL_FONT_WIDTH; gx++) {
                    if (pixel_font[ch - PIXEL_FONT_FIRST][gx] >> fy & 1) { int x = x0 + ci * cw + gx * scale; for (int k = 0; k < scale; k++) if (x + k >= 0 && x + k < s_screen_w) line[x + k] = color; }
                }
            }
        }
        led_status_draw_rect(0, y + sy, s_screen_w, rows, s_strip);
    }
}
static void drawBar(int y, double frac, int h) {
    int x0 = 16, w = s_screen_w - 32, fill = (int)(w * (frac < 0 ? 0 : frac > 1 ? 1 : frac));
    for (int sy = 0; sy < h; sy += STRIP_ROWS) {
        int rows = h - sy < STRIP_ROWS ? h - sy : STRIP_ROWS;
        for (int r = 0; r < rows; r++) { uint16_t *line = s_strip + r * s_screen_w; for (int x = 0; x < s_screen_w; x++) line[x] = (x >= x0 && x < x0 + w) ? (x < x0 + fill ? C_BAR : C_BAR_BG) : C_BG; }
        led_status_draw_rect(0, y + sy, s_screen_w, rows, s_strip);
    }
}
static void clearRows(int y0, int y1) {
    for (int x = 0; x < s_screen_w * STRIP_ROWS; x++) s_strip[x] = C_BG;
    for (int y = y0; y < y1; y += STRIP_ROWS) { int rows = y1 - y < STRIP_ROWS ? y1 - y : STRIP_ROWS; led_status_draw_rect(0, y, s_screen_w, rows, s_strip); }
}

static void renderScreen() {
    if (!s_app || !s_strip) return;
    const int top = 28, bottom = s_screen_h - 28;   // keep clear of Meta's status bars
    if (!s_cleared) { clearRows(0, s_screen_h); s_cleared = true; }
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
    }
    if (y < bottom) clearRows(y, bottom);
    s_dirty_screen = false;
}

// ---------------------------------------------------------------- persistence
static void saveState() {
    cJSON *persist = s_app ? cJSON_GetObjectItem(s_app, "persist") : nullptr;
    cJSON *o = cJSON_CreateObject(); cJSON *k = nullptr;
    cJSON_ArrayForEach(k, persist) { if (!cJSON_IsString(k)) continue; auto it = s_state.find(k->valuestring); if (it == s_state.end()) continue; if (it->second.isNum) cJSON_AddNumberToObject(o, k->valuestring, it->second.num); else cJSON_AddStringToObject(o, k->valuestring, it->second.str.c_str()); }
    char *txt = cJSON_PrintUnformatted(o); cJSON_Delete(o); if (!txt) return;
    nvs_handle_t h; if (nvs_open("musecorder", NVS_READWRITE, &h) == ESP_OK) { nvs_set_blob(h, "state", txt, strlen(txt) + 1); nvs_commit(h); nvs_close(h); }
    free(txt); s_dirty_state = false; s_last_save = esp_timer_get_time();
}
static void loadState() {
    nvs_handle_t h; size_t n = 0; if (nvs_open("musecorder", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_blob(h, "state", nullptr, &n) == ESP_OK && n > 0 && n < 4096) { char *txt = (char *)malloc(n); if (txt && nvs_get_blob(h, "state", txt, &n) == ESP_OK) { cJSON *o = cJSON_Parse(txt); cJSON *it = nullptr; cJSON_ArrayForEach(it, o) s_state[it->string] = cJSON_IsNumber(it) ? Value::N(it->valuedouble) : Value::S(cJSON_GetStringValue(it) ? cJSON_GetStringValue(it) : ""); cJSON_Delete(o); } free(txt); }
    nvs_close(h);
}
static void saveApplet(const char *json) {
    nvs_handle_t h; if (nvs_open("musecorder", NVS_READWRITE, &h) != ESP_OK) return;
    if (json) nvs_set_blob(h, "applet", json, strlen(json) + 1); else { nvs_erase_key(h, "applet"); nvs_erase_key(h, "state"); }
    nvs_commit(h); nvs_close(h);
}
static char *loadApplet() {
    nvs_handle_t h; size_t n = 0; if (nvs_open("musecorder", NVS_READONLY, &h) != ESP_OK) return nullptr;
    char *txt = nullptr; if (nvs_get_blob(h, "applet", nullptr, &n) == ESP_OK && n > 1 && n <= MC_MAX_APPLET_BYTES + 1) { txt = (char *)malloc(n); if (txt && nvs_get_blob(h, "applet", txt, &n) != ESP_OK) { free(txt); txt = nullptr; } }
    nvs_close(h); return txt;
}

// ---------------------------------------------------------------- lifecycle (tick task context)
static void stopApplet(bool restoreScreen) {
    if (s_app) { fire("stop"); cJSON_Delete(s_app); s_app = nullptr; }
    s_timers.clear(); s_watches.clear(); s_state.clear(); s_running = false; s_exit = false; s_cleared = false;
    free(s_strip); s_strip = nullptr;
    if (s_sub_id > 0) { noise_ctrl_req_cancel(s_sub_id); s_sub_id = 0; }
    if (restoreScreen) led_status_show_animation();
}
static bool startApplet(cJSON *doc, bool fresh) {
    stopApplet(false);
    s_app = doc; cJSON *id = cJSON_GetObjectItem(doc, "id"); s_app_id = cJSON_IsString(id) ? id->valuestring : "applet";
    cJSON *st = cJSON_GetObjectItem(doc, "state"); cJSON *it = nullptr;
    cJSON_ArrayForEach(it, st) s_state[it->string] = cJSON_IsNumber(it) ? Value::N(it->valuedouble) : Value::S(cJSON_GetStringValue(it) ? cJSON_GetStringValue(it) : "");
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
    if (s_screen_w > 0 && !s_strip) s_strip = (uint16_t *)malloc(s_screen_w * STRIP_ROWS * sizeof(uint16_t));
    s_running = true; s_dirty_screen = true;
    logf("applet %s started", s_app_id.c_str());
    fire("start");
    return true;
}

static void tickTask(void *) {
    char *saved = loadApplet();
    if (saved) { cJSON *doc = cJSON_Parse(saved); free(saved); if (doc) { xSemaphoreTake(s_lock, portMAX_DELAY); startApplet(doc, false); xSemaphoreGive(s_lock); } }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(MC_TICK_MS));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (s_running) {
            int64_t now = esp_timer_get_time();
            for (auto &t : s_timers) if (now >= t.next) { t.next = now + (int64_t)t.period_ms * 1000; fire(t.key); }
            for (auto &w : s_watches) { int v = gpio_get_level((gpio_num_t)w.pin); if (v != w.last) { bool hit = w.mode == 2 || (w.mode == 0 && v) || (w.mode == 1 && !v); w.last = v; if (hit) fire(w.key); } }
            if (s_tap_pending) { s_tap_pending = false; fire("boot:press"); }
            while (!s_pending.empty()) { std::string e = s_pending.front(); s_pending.erase(s_pending.begin()); fire(e.c_str()); }
            if (s_exit) { logf("applet exited"); saveApplet(nullptr); stopApplet(true); }
            else {
                if (s_dirty_screen) renderScreen();
                if (s_dirty_state && now - s_last_save > 2000000) saveState();
            }
        }
        xSemaphoreGive(s_lock);
    }
}

// ---------------------------------------------------------------- public API
extern "C" void musecorder_init(void) {
    s_lock = xSemaphoreCreateMutex();
    if (!led_status_display_info(&s_screen_w, &s_screen_h)) { s_screen_w = 0; }
    button_set_press_cb(pressCb);
    xTaskCreate(tickTask, "musecorder", 4096, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "ready (%dx%d)", s_screen_w, s_screen_h);
}
extern "C" void musecorder_vm_connected(void) { s_vm_up = true; }
extern "C" void musecorder_screen_reset(void) { s_cleared = false; s_dirty_screen = true; }
static bool pressCb(bool pressed) {
    if (!s_running) return false;
    if (pressed) s_tap_pending = true;
    return true;   // claimed: not a Wi-Fi retry, rescan or setup reset while an applet runs
}
extern "C" bool musecorder_on_tap(void) {
    if (!s_running) return false;
    s_tap_pending = true;   // consumed by the tick task
    return true;
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
    cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true); cJSON_AddItemToObject(r, "payload", p);
    return r;
}

extern "C" cJSON *musecorder_command(const char *command, cJSON *params) {
    if (strncmp(command, "applet.", 7)) return nullptr;
    if (!s_lock) return errorResult("unavailable", "runtime not ready");
    if (!strcmp(command, "applet.install")) {
        cJSON *j = cJSON_GetObjectItem(params, "json");
        if (!j) return errorResult("missing_param", "json is required");
        char *txt = cJSON_IsString(j) ? strdup(j->valuestring) : cJSON_PrintUnformatted(j);
        if (!txt) return errorResult("out_of_memory", "failed to copy applet");
        if (strlen(txt) > MC_MAX_APPLET_BYTES) { free(txt); return errorResult("too_large", "applet must be under 6 KB"); }
        cJSON *doc = cJSON_Parse(txt);
        if (!doc || !cJSON_IsObject(doc) || !cJSON_IsString(cJSON_GetObjectItem(doc, "id"))) { cJSON_Delete(doc); free(txt); return errorResult("invalid_params", "json must be an applet object with an id"); }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        saveApplet(txt); free(txt);
        startApplet(doc, true);
        cJSON *r = statePayload();
        xSemaphoreGive(s_lock);
        return r;
    }
    if (!strcmp(command, "applet.remove")) {
        xSemaphoreTake(s_lock, portMAX_DELAY); bool was = s_running; saveApplet(nullptr); stopApplet(true); xSemaphoreGive(s_lock);
        cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true); cJSON *p = cJSON_CreateObject(); cJSON_AddBoolToObject(p, "removed", was); cJSON_AddItemToObject(r, "payload", p); return r;
    }
    if (!strcmp(command, "applet.state")) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (cJSON *set = cJSON_GetObjectItem(params, "set")) { cJSON *it = nullptr; cJSON_ArrayForEach(it, set) s_state[it->string] = cJSON_IsNumber(it) ? Value::N(it->valuedouble) : Value::S(cJSON_GetStringValue(it) ? cJSON_GetStringValue(it) : ""); s_dirty_screen = s_dirty_state = true; }
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
