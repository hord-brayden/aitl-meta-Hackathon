#include "applet.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <map>
#include <time.h>
#include "../config.h"
#include "../core/app.h"
#include "../core/settings.h"
#include "../hal/led.h"
#include "../ui/ui.h"

#ifndef PIN_SPEAKER
#define PIN_SPEAKER 26
#endif

namespace applets {
static const char* DIR = "/applets";
static const size_t MAX_JSON = 12 * 1024;

// ---------------- running applet ----------------
static JsonDocument* s_doc = nullptr;
static String s_id, s_name, s_icon; static bool s_background = false;
static std::map<String, expr::Value> s_state;
static std::vector<String> s_persist; static bool s_persistDirty = false; static uint32_t s_persistDue = 0;
struct Timer { uint32_t period, next; String key; };
static std::vector<Timer> s_timers;
struct Watch { uint8_t pin, mode; int last; uint32_t lastChange; String key; };
static std::vector<Watch> s_watches;
static std::vector<JsonArray> s_actions;
struct Pending { uint8_t kind; JsonArray then, els; String into; uint32_t at; };
static std::map<int, Pending> s_pending; static int s_reqSeq = 1;
static int s_respStatus = 0; static String s_respBody, s_err;
static std::vector<String> s_log;
static bool s_dirty = false, s_onScreen = false, s_exitReq = false, s_wantsBoot = false, s_keepAlive = false;
static lv_obj_t* s_scr = nullptr;
static int s_depth = 0;
static bool s_ntpStarted = false;
// chunked install buffer
static String s_rxId, s_rxBuf; static int s_rxN = 0, s_rxGot = 0;

static const String EMPTY;
bool running() { return s_doc != nullptr; }
const String& runningId() { return s_id; }
const String& runningName() { return s_name; }
bool onScreen() { return s_onScreen; }
bool wantsBoot() { return s_wantsBoot; }
void markDirty() { s_dirty = true; }
bool takeDirty() { bool d = s_dirty; s_dirty = false; return d; }
JsonArray uiArray() { return s_doc ? (*s_doc)["ui"].as<JsonArray>() : JsonArray(); }
JsonObject onObject() { return s_doc ? (*s_doc)["on"].as<JsonObject>() : JsonObject(); }
size_t registerActions(JsonArray a) { s_actions.push_back(a); return s_actions.size() - 1; }
void resetActions() { s_actions.clear(); }
void runActionsAt(size_t idx) { if (idx < s_actions.size()) runActions(s_actions[idx]); }
void onScreenShown(lv_obj_t* scr) { s_scr = scr; s_onScreen = true; }
void keepAlive(bool on) { s_keepAlive = on; }
void onScreenGone() {
  s_onScreen = false; s_scr = nullptr;
  if (s_keepAlive) return;                          // a text entry screen is borrowing the display
  if (s_doc && !s_background) s_exitReq = true;     // tick() runs `stop` and unloads
}

static String path(const String& id) { return String(DIR) + "/" + id + ".json"; }
static String statePath(const String& id) { return String(DIR) + "/" + id + ".state"; }
static bool validId(const String& id) {
  if (!id.length() || id.length() > 24) return false;
  for (size_t i = 0; i < id.length(); i++) { char c = id[i]; if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '-')) return false; }
  return true;
}

void logLine(const String& line) {
  s_log.push_back(line.substring(0, 160)); if (s_log.size() > 40) s_log.erase(s_log.begin());
  JsonDocument d; d["t"] = "applet.log"; d["id"] = s_id; d["line"] = line.substring(0, 300); bridge::send(d);
}

// ---------------- state + resolver ----------------
expr::Value getState(const String& k) { auto it = s_state.find(k); return it == s_state.end() ? expr::Value() : it->second; }
void setState(const String& k, const expr::Value& v) {
  s_state[k] = v; s_dirty = true;
  for (auto& p : s_persist) if (p == k) { s_persistDirty = true; s_persistDue = millis() + 2000; break; }
}
static bool resolver(const String& name, expr::Value& out, void*) { return resolve(name, out); }
bool resolve(const String& name, expr::Value& out) {
  auto it = s_state.find(name); if (it != s_state.end()) { out = it->second; return true; }
  if (name == "device.id") { out = expr::Value::str(app::deviceId()); return true; }
  if (name == "device.name") { out = expr::Value::str(AITL_PRODUCT); return true; }
  if (name == "device.heap") { out = expr::Value::num(ESP.getFreeHeap()); return true; }
  if (name == "device.board") { out = expr::Value::str(AITL_BOARD_NAME); return true; }
  if (name == "link.up") { out = expr::Value::boolean(bridge::connected()); return true; }
  if (name == "wifi.up") { out = expr::Value::boolean(bridge::wifiUp()); return true; }
  if (name == "wifi.ip") { out = expr::Value::str(bridge::ipAddress()); return true; }
  if (name == "wifi.rssi") { out = expr::Value::num(bridge::rssi()); return true; }
  if (name == "uptime") { out = expr::Value::num(millis() / 1000); return true; }
  if (name == "millis") { out = expr::Value::num(millis()); return true; }
  if (name == "epoch" || name == "time" || name == "date") {
    time_t now = time(nullptr); bool ok = now > 1600000000;
    if (name == "epoch") { out = expr::Value::num(ok ? (double)now : 0); return true; }
    if (!ok) { out = expr::Value::str(name == "time" ? "--:--" : "----"); return true; }
    struct tm tmv; localtime_r(&now, &tmv); char b[24];
    strftime(b, sizeof(b), name == "time" ? "%H:%M" : "%Y-%m-%d", &tmv); out = expr::Value::str(b); return true;
  }
  if (name == "resp.status") { out = expr::Value::num(s_respStatus); return true; }
  if (name == "resp.body") { out = expr::Value::str(s_respBody); return true; }
  if (name == "err") { out = expr::Value::str(s_err); return true; }
  if (name.startsWith("resp.json.")) {
    JsonDocument d; if (deserializeJson(d, s_respBody) != DeserializationError::Ok) { out = expr::Value(); return true; }
    JsonVariant v = d.as<JsonVariant>(); String p = name.substring(10); int st = 0;
    while (st <= (int)p.length() && !v.isNull()) { int dot = p.indexOf('.', st); String k = dot < 0 ? p.substring(st) : p.substring(st, dot); if (k.length()) { if (v.is<JsonArray>()) v = v[k.toInt()]; else v = v[k]; } if (dot < 0) break; st = dot + 1; }
    if (v.isNull()) out = expr::Value(); else if (v.is<bool>()) out = expr::Value::boolean(v.as<bool>()); else if (v.is<double>()) out = expr::Value::num(v.as<double>()); else if (v.is<const char*>()) out = expr::Value::str(v.as<const char*>()); else { String s; serializeJson(v, s); out = expr::Value::str(s); }
    return true;
  }
  return false;
}
static expr::Value typed(JsonVariant v) {
  if (v.is<bool>()) return expr::Value::boolean(v.as<bool>());
  if (v.is<double>() || v.is<long>()) return expr::Value::num(v.as<double>());
  if (v.is<const char*>()) { String e; expr::Value r = expr::renderTyped(v.as<const char*>(), resolver, nullptr, &e); if (e.length()) logLine("expr: " + e); return r; }
  return expr::Value();
}
static String rendered(JsonVariant v) { if (v.is<const char*>()) { String e; String r = expr::render(v.as<const char*>(), resolver, nullptr, &e); if (e.length()) logLine("expr: " + e); return r; } return typed(v).toStr(); }

// ---------------- persistence ----------------
static void persistNow() {
  s_persistDirty = false; if (!s_doc || s_persist.empty()) return;
  JsonDocument d; for (auto& k : s_persist) { expr::Value v = getState(k); if (v.t == expr::Value::Num) d[k] = v.n; else if (v.t == expr::Value::Bool) d[k] = v.n != 0; else if (v.t == expr::Value::Str) d[k] = v.s; }
  File f = LittleFS.open(statePath(s_id), "w"); if (!f) return; serializeJson(d, f); f.close();
}
static void restoreState() {
  if (s_persist.empty() || !LittleFS.exists(statePath(s_id))) return;
  File f = LittleFS.open(statePath(s_id), "r"); if (!f) return; JsonDocument d; bool ok = deserializeJson(d, f) == DeserializationError::Ok; f.close(); if (!ok) return;
  for (auto& k : s_persist) { JsonVariant v = d[k]; if (v.isNull()) continue; if (v.is<bool>()) s_state[k] = expr::Value::boolean(v.as<bool>()); else if (v.is<double>()) s_state[k] = expr::Value::num(v.as<double>()); else if (v.is<const char*>()) s_state[k] = expr::Value::str(v.as<const char*>()); }
}

// ---------------- pins ----------------
static bool pinAllowed(int pin, bool output) {
  static const int inputsOnly[] = {34, 35, 36, 39}; static const int any[] = {0, 21, 22, 26};
  for (int p : any) if (p == pin) return true;
  if (!output) for (int p : inputsOnly) if (p == pin) return true;
  return false;
}

// ---------------- actions ----------------
static LedMode ledFor(const String& n) {
  if (n == "green" || n == "ok" || n == "done") return LedMode::Done; if (n == "red" || n == "error") return LedMode::Error;
  if (n == "amber" || n == "listening") return LedMode::Listening; if (n == "blue" || n == "thinking") return LedMode::Thinking;
  if (n == "alert") return LedMode::Alert; if (n == "off") return LedMode::Off; if (n == "typing") return LedMode::Typing; return LedMode::Idle;
}
static void doHttp(JsonObject h) {
  String method = rendered(h["method"]); if (!method.length()) method = h["json"].isNull() && h["body"].isNull() ? "GET" : "POST"; method.toUpperCase();
  String url = rendered(h["url"]); String via = rendered(h["via"]); int timeout = h["timeout"] | 10;
  if (!url.startsWith("http://") && !url.startsWith("https://")) { s_err = "bad url"; runActions(h["else"].as<JsonArray>()); return; }
  String body; if (!h["json"].isNull()) { JsonDocument b; JsonObject o = b.to<JsonObject>(); for (JsonPair kv : h["json"].as<JsonObject>()) { expr::Value v = typed(kv.value()); if (v.t == expr::Value::Num) o[kv.key()] = v.n; else if (v.t == expr::Value::Bool) o[kv.key()] = v.n != 0; else o[kv.key()] = v.toStr(); } serializeJson(b, body); }
  else if (!h["body"].isNull()) body = rendered(h["body"]);
  bool useBridge = via == "bridge" || (via != "direct" && bridge::connected());
  if (useBridge) {
    if (!bridge::connected()) { s_err = "no bridge"; runActions(h["else"].as<JsonArray>()); return; }
    int req = s_reqSeq++;
    JsonDocument d; d["t"] = "net.http"; d["req"] = req; d["method"] = method; d["url"] = url; d["timeout"] = timeout;
    if (!h["json"].isNull()) d["json"] = true;
    if (body.length()) d["body"] = body;
    if (!h["headers"].isNull()) { JsonObject hh = d["headers"].to<JsonObject>(); for (JsonPair kv : h["headers"].as<JsonObject>()) hh[kv.key()] = rendered(kv.value()); }
    bridge::send(d);
    s_pending[req] = {0, h["then"].as<JsonArray>(), h["else"].as<JsonArray>(), String((const char*)(h["into"] | "")), millis()};
    return;
  }
  if (!bridge::wifiUp()) { s_err = "no wifi, no bridge"; runActions(h["else"].as<JsonArray>()); return; }
  bool https = url.startsWith("https://");
  if (https && ESP.getFreeHeap() < 60000) { s_err = "low memory for TLS"; runActions(h["else"].as<JsonArray>()); return; }
  HTTPClient http; WiFiClientSecure sec; WiFiClient plain;
  if (https) { sec.setInsecure(); http.begin(sec, url); } else http.begin(plain, url);
  http.setTimeout(timeout * 1000); http.setConnectTimeout(timeout * 1000);
  if (!h["json"].isNull()) http.addHeader("Content-Type", "application/json");
  if (!h["headers"].isNull()) for (JsonPair kv : h["headers"].as<JsonObject>()) http.addHeader(kv.key().c_str(), rendered(kv.value()));
  int code = method == "GET" ? http.GET() : method == "PUT" ? http.PUT(body) : http.POST(body);
  s_respStatus = code; s_respBody = code > 0 ? http.getString().substring(0, 2000) : ""; s_err = code > 0 ? "" : HTTPClient::errorToString(code);
  http.end();
  String into = h["into"] | ""; if (into.length()) setState(into, expr::Value::str(s_respBody));
  runActions(code >= 200 && code < 400 ? h["then"].as<JsonArray>() : h["else"].as<JsonArray>());
}
static void doAsk(JsonObject a) {
  if (!bridge::connected()) { s_err = "no bridge"; runActions(a["else"].as<JsonArray>()); return; }
  int req = s_reqSeq++;
  JsonDocument d; d["t"] = "ai.ask"; d["req"] = req; d["prompt"] = rendered(a["prompt"]); if (!a["model"].isNull()) d["model"] = rendered(a["model"]); bridge::send(d);
  s_pending[req] = {1, a["then"].as<JsonArray>(), a["else"].as<JsonArray>(), String((const char*)(a["into"] | "answer")), millis()};
}
static void runOne(JsonObject a) {
  if (!a["if"].isNull()) { bool c = typed(a["if"]).truthy(); runActions(a[c ? "then" : "else"].as<JsonArray>()); return; }
  for (JsonPair kv : a) {
    String verb = kv.key().c_str(); JsonVariant v = kv.value();
    if (verb == "set") { for (JsonPair s : v.as<JsonObject>()) setState(s.key().c_str(), typed(s.value())); }
    else if (verb == "inc" || verb == "dec") {
      double sign = verb == "inc" ? 1 : -1;
      if (v.is<const char*>()) { String k = v.as<const char*>(); setState(k, expr::Value::num(getState(k).toNum() + sign)); }
      else for (JsonPair s : v.as<JsonObject>()) { String k = s.key().c_str(); setState(k, expr::Value::num(getState(k).toNum() + sign * typed(s.value()).toNum())); }
    }
    else if (verb == "toggle") { String k = rendered(v); setState(k, expr::Value::boolean(!getState(k).truthy())); }
    else if (verb == "append") {
      int mx = v["max"] | 20;
      for (JsonPair s : v.as<JsonObject>()) {
        if (!strcmp(s.key().c_str(), "max")) continue;
        String k = s.key().c_str(); String cur = getState(k).toStr(); if (cur.length()) cur += '\n'; cur += rendered(s.value());
        int lines = 0; for (int i = cur.length() - 1; i >= 0; i--) if (cur[i] == '\n' && ++lines >= mx) { cur = cur.substring(i + 1); break; }
        setState(k, expr::Value::str(cur));
      }
    }
    else if (verb == "toast") { if (v.is<const char*>()) ui::toast(rendered(v).c_str()); else ui::toast(rendered(v["text"]).c_str(), v["error"] | false); }
    else if (verb == "led") ledSetMode(ledFor(rendered(v)));
    else if (verb == "beep") { int f = (int)typed(v["freq"]).toNum(); int ms = (int)typed(v["ms"]).toNum(); if (f <= 0) f = 880; if (ms <= 0) ms = 60; if (ms > 2000) ms = 2000; tone(PIN_SPEAKER, f, ms); }
    else if (verb == "gpio.mode") { int pin = (int)typed(v["pin"]).toNum(); String m = rendered(v["mode"]); bool out = m == "out"; if (!pinAllowed(pin, out)) { logLine("gpio: pin " + String(pin) + " not allowed"); continue; } pinMode(pin, out ? OUTPUT : m == "in" ? INPUT : INPUT_PULLUP); }
    else if (verb == "gpio.write") { int pin = (int)typed(v["pin"]).toNum(); if (!pinAllowed(pin, true)) { logLine("gpio: pin " + String(pin) + " not allowed"); continue; } pinMode(pin, OUTPUT); digitalWrite(pin, typed(v["value"]).truthy() ? HIGH : LOW); }
    else if (verb == "gpio.read") { int pin = (int)typed(v["pin"]).toNum(); if (!pinAllowed(pin, false)) continue; setState(rendered(v["into"]), expr::Value::num(digitalRead(pin))); }
    else if (verb == "adc.read") { int pin = (int)typed(v["pin"]).toNum(); if (!pinAllowed(pin, false)) continue; setState(rendered(v["into"]), expr::Value::num(analogRead(pin))); }
    else if (verb == "http") doHttp(v.as<JsonObject>());
    else if (verb == "ask") doAsk(v.as<JsonObject>());
    else if (verb == "hid.type") app::typeToMac(rendered(v));
    else if (verb == "hid.key") app::keyToMac(rendered(v).c_str());
    else if (verb == "notify") { JsonDocument d; d["t"] = "notify"; d["title"] = v.is<const char*>() ? String(s_name) : rendered(v["title"]); d["text"] = v.is<const char*>() ? rendered(v) : rendered(v["text"]); bridge::send(d); }
    else if (verb == "log") logLine(rendered(v));
    else if (verb == "event") raise(rendered(v).c_str());
    else if (verb == "emit") { JsonDocument d; d["t"] = "applet.event.out"; d["id"] = s_id; d["name"] = rendered(v); JsonObject st = d["state"].to<JsonObject>(); for (auto& kv2 : s_state) { if (kv2.second.t == expr::Value::Num) st[kv2.first] = kv2.second.n; else if (kv2.second.t == expr::Value::Bool) st[kv2.first] = kv2.second.n != 0; else st[kv2.first] = kv2.second.toStr(); } bridge::send(d); }
    else if (verb == "save") persistNow();
    else if (verb == "exit") s_exitReq = true;
    else logLine("unknown action: " + verb);
  }
}
void runActions(JsonArray a) {
  if (a.isNull() || s_depth > 8) return;
  s_depth++;
  for (JsonObject o : a) runOne(o);
  s_depth--;
  s_dirty = true;
}
void raise(const char* event) {
  if (!s_doc) return;
  JsonArray h = (*s_doc)["on"][event].as<JsonArray>();
  if (!h.isNull()) runActions(h);
}

// ---------------- lifecycle ----------------
static void unload() {
  if (!s_doc) return;
  if (s_persistDirty) persistNow();
  for (auto& w : s_watches) (void)w;
  s_timers.clear(); s_watches.clear(); s_actions.clear(); s_pending.clear(); s_state.clear(); s_persist.clear(); s_log.clear();
  delete s_doc; s_doc = nullptr; s_id = ""; s_name = ""; s_icon = ""; s_background = false; s_wantsBoot = false; s_onScreen = false; s_scr = nullptr;
}
void requestClose() { s_exitReq = true; }
void handleBootHold() { if (s_doc) { s_exitReq = true; ui::toast("Leaving applet"); } }

bool open(const String& id, String& err) {
  if (!validId(id) || !LittleFS.exists(path(id))) { err = "no such applet"; return false; }
  if (s_doc) { raise("stop"); unload(); }
  File f = LittleFS.open(path(id), "r"); if (!f) { err = "open failed"; return false; }
  s_doc = new JsonDocument();
  DeserializationError de = deserializeJson(*s_doc, f); f.close();
  if (de != DeserializationError::Ok) { err = String("json: ") + de.c_str(); unload(); return false; }
  s_id = id; s_name = (const char*)((*s_doc)["name"] | id.c_str()); s_icon = (const char*)((*s_doc)["icon"] | ""); s_background = (*s_doc)["background"] | false;
  for (JsonPair kv : (*s_doc)["state"].as<JsonObject>()) { JsonVariant v = kv.value(); if (v.is<bool>()) s_state[kv.key().c_str()] = expr::Value::boolean(v.as<bool>()); else if (v.is<double>()) s_state[kv.key().c_str()] = expr::Value::num(v.as<double>()); else s_state[kv.key().c_str()] = expr::Value::str((const char*)(v | "")); }
  for (JsonVariant v : (*s_doc)["persist"].as<JsonArray>()) if (v.is<const char*>()) s_persist.push_back(v.as<const char*>());
  restoreState();
  uint32_t now = millis();
  for (JsonPair kv : (*s_doc)["on"].as<JsonObject>()) {
    String k = kv.key().c_str();
    if (k.startsWith("every:")) { long n = k.substring(6).toInt(); if (n >= 1 && s_timers.size() < 8) s_timers.push_back({(uint32_t)n * 1000, now + (uint32_t)n * 1000, k}); }
    else if (k.startsWith("gpio:")) {
      int p1 = k.indexOf(':', 5); if (p1 < 0) continue; int pin = k.substring(5, p1).toInt(); String rest = k.substring(p1 + 1);
      bool nopull = rest.endsWith(":nopull"); if (nopull) rest = rest.substring(0, rest.length() - 7);
      uint8_t mode = rest == "rise" ? 0 : rest == "fall" ? 1 : 2;
      if (!pinAllowed(pin, false) || s_watches.size() >= 4) { logLine("gpio watch refused: " + k); continue; }
      pinMode(pin, nopull ? INPUT : INPUT_PULLUP);
      s_watches.push_back({(uint8_t)pin, mode, digitalRead(pin), now, k});
    }
    else if (k == "boot:press") s_wantsBoot = true;
  }
  s_exitReq = false;
  buildScreen();
  raise("start");
  s_dirty = true;
  bridge::log("applet open: %s heap=%u", id.c_str(), ESP.getFreeHeap());
  return true;
}

// ---------------- storage ----------------
static bool readInfo(const String& id, Info& out) {
  File f = LittleFS.open(path(id), "r"); if (!f) return false;
  JsonDocument filter; filter["name"] = true; filter["icon"] = true; filter["version"] = true; filter["autostart"] = true;
  JsonDocument d; size_t size = f.size();
  bool ok = deserializeJson(d, f, DeserializationOption::Filter(filter)) == DeserializationError::Ok; f.close();
  if (!ok) return false;
  out.id = id; out.name = (const char*)(d["name"] | id.c_str()); out.icon = (const char*)(d["icon"] | ""); out.version = d["version"] | 1; out.size = size; out.autostart = d["autostart"] | false;
  return true;
}
std::vector<Info> list() {
  std::vector<Info> out;
  File dir = LittleFS.open(DIR); if (!dir || !dir.isDirectory()) return out;
  File f = dir.openNextFile();
  while (f) {
    String n = f.name(); int slash = n.lastIndexOf('/'); if (slash >= 0) n = n.substring(slash + 1);
    f.close();
    if (n.endsWith(".json")) { Info i; if (readInfo(n.substring(0, n.length() - 5), i)) out.push_back(i); }
    f = dir.openNextFile();
  }
  return out;
}
static int countWidgets(JsonArray a, int depth) {
  int n = 0; if (depth > 4) return 99;
  for (JsonObject w : a) { n++; if (!w["items"].isNull()) n += countWidgets(w["items"].as<JsonArray>(), depth + 1); }
  return n;
}
bool install(const String& json, bool openIt, String& err, String* idOut) {
  if (json.length() > MAX_JSON) { err = "document larger than 12 KB"; return false; }
  JsonDocument d;
  DeserializationError de = deserializeJson(d, json);
  if (de != DeserializationError::Ok) { err = String("json: ") + de.c_str(); return false; }
  String id = (const char*)(d["id"] | "");
  if (!validId(id)) { err = "id must match [a-z0-9-]{1,24}"; return false; }
  if (!d["ui"].is<JsonArray>()) { err = "ui must be an array"; return false; }
  if (countWidgets(d["ui"].as<JsonArray>(), 0) > 48) { err = "more than 48 widgets"; return false; }
  if (d["state"].as<JsonObject>().size() > 32) { err = "more than 32 state keys"; return false; }
  if (d["on"].as<JsonObject>().size() > 16) { err = "more than 16 handlers"; return false; }
  if (idOut) *idOut = id;
  if (!LittleFS.exists(DIR)) LittleFS.mkdir(DIR);
  File f = LittleFS.open(path(id), "w"); if (!f) { err = "flash write failed"; return false; }
  f.print(json); f.close();
  bridge::log("applet installed: %s (%u bytes)", id.c_str(), (unsigned)json.length());
  if (openIt || s_id == id) { String e; if (!open(id, e)) { err = e; return false; } }
  return true;
}
bool remove(const String& id) {
  if (!validId(id)) return false;
  if (s_id == id) { raise("stop"); unload(); ui::showHome(); }
  bool ok = LittleFS.remove(path(id)); LittleFS.remove(statePath(id));
  return ok;
}

bool setAutostart(const String& id, bool on) {
  if (!validId(id) || !LittleFS.exists(path(id))) return false;
  File f = LittleFS.open(path(id), "r"); if (!f) return false; JsonDocument d; bool ok = deserializeJson(d, f) == DeserializationError::Ok; f.close(); if (!ok) return false;
  if (on) { for (auto& i : list()) if (i.autostart && i.id != id) setAutostart(i.id, false); }   // one kiosk applet at a time
  d["autostart"] = on;
  File w = LittleFS.open(path(id), "w"); if (!w) return false; serializeJson(d, w); w.close();
  return on;
}

// ---------------- link ----------------
static void ack(const String& id, bool ok, const String& err, bridge::Transport from, size_t size = 0) {
  JsonDocument r; r["t"] = "applet.ack"; r["id"] = id; r["ok"] = ok; if (err.length()) r["err"] = err; if (size) r["size"] = size; r["heap"] = ESP.getFreeHeap(); bridge::sendVia(from, r);
}
bool onMessage(JsonDocument& d, bridge::Transport from) {
  const char* t = d["t"] | "";
  if (!strcmp(t, "net.http.reply") || !strcmp(t, "ai.reply")) {
    int req = d["req"] | 0; auto it = s_pending.find(req); if (it == s_pending.end()) return true;
    Pending p = it->second; s_pending.erase(it);
    s_err = (const char*)(d["err"] | "");
    if (p.kind == 0) { s_respStatus = d["status"] | 0; s_respBody = (const char*)(d["body"] | ""); if (p.into.length()) setState(p.into, expr::Value::str(s_respBody)); runActions(!s_err.length() && s_respStatus >= 200 && s_respStatus < 400 ? p.then : p.els); }
    else { String text = (const char*)(d["text"] | ""); setState(p.into, expr::Value::str(text)); runActions(s_err.length() ? p.els : p.then); }
    return true;
  }
  if (strncmp(t, "applet.", 7)) return false;
  String id = (const char*)(d["id"] | "");
  if (!strcmp(t, "applet.put")) {
    int i = d["i"] | 0, n = d["n"] | 1;
    if (i == 0 || s_rxId != id) { s_rxId = id; s_rxBuf = ""; s_rxN = n; s_rxGot = 0; }
    s_rxBuf += (const char*)(d["d"] | ""); s_rxGot++;
    if (s_rxBuf.length() > MAX_JSON + 512) { s_rxBuf = ""; s_rxId = ""; ack(id, false, "document larger than 12 KB", from); }
    return true;
  }
  if (!strcmp(t, "applet.install")) {
    String json; if (!d["json"].isNull()) { if (d["json"].is<const char*>()) json = d["json"].as<const char*>(); else serializeJson(d["json"], json); }
    else if (s_rxId == id && s_rxBuf.length()) { json = s_rxBuf; s_rxBuf = ""; s_rxId = ""; }
    if (!json.length()) { ack(id, false, "no document (send applet.put chunks first)", from); return true; }
    String err, gotId; bool ok = install(json, d["open"] | true, err, &gotId);
    ack(gotId.length() ? gotId : id, ok, err, from, json.length());
    return true;
  }
  if (!strcmp(t, "applet.list")) {
    JsonDocument r; r["t"] = "applet.list"; JsonArray arr = r["applets"].to<JsonArray>();
    for (auto& i : list()) { JsonObject o = arr.add<JsonObject>(); o["id"] = i.id; o["name"] = i.name; o["icon"] = i.icon; o["version"] = i.version; o["size"] = i.size; o["autostart"] = i.autostart; }
    r["running"] = s_id; r["onScreen"] = s_onScreen; r["free"] = ESP.getFreeHeap(); size_t tot = LittleFS.totalBytes(), used = LittleFS.usedBytes(); r["flashFree"] = tot > used ? tot - used : 0;
    bridge::sendVia(from, r); return true;
  }
  if (!strcmp(t, "applet.remove")) { bool ok = remove(id); ack(id, ok, ok ? "" : "not found", from); return true; }
  if (!strcmp(t, "applet.open")) { String err; bool ok = open(id, err); ack(id, ok, err, from); return true; }
  if (!strcmp(t, "applet.close")) { if (s_doc) { s_exitReq = true; ack(s_id, true, "", from); } else ack("", false, "nothing running", from); return true; }
  if (!strcmp(t, "applet.state.get")) {
    JsonDocument r; r["t"] = "applet.state"; r["id"] = s_id; JsonObject st = r["state"].to<JsonObject>();
    for (auto& kv : s_state) { if (kv.second.t == expr::Value::Num) st[kv.first] = kv.second.n; else if (kv.second.t == expr::Value::Bool) st[kv.first] = kv.second.n != 0; else st[kv.first] = kv.second.toStr(); }
    if (!s_doc) r["err"] = "nothing running";
    bridge::sendVia(from, r); return true;
  }
  if (!strcmp(t, "applet.state.set")) {
    if (!s_doc || (id.length() && id != s_id)) { ack(id, false, "applet not running", from); return true; }
    for (JsonPair kv : d["state"].as<JsonObject>()) { JsonVariant v = kv.value(); if (v.is<bool>()) setState(kv.key().c_str(), expr::Value::boolean(v.as<bool>())); else if (v.is<double>()) setState(kv.key().c_str(), expr::Value::num(v.as<double>())); else setState(kv.key().c_str(), expr::Value::str((const char*)(v | ""))); }
    ack(s_id, true, "", from); return true;
  }
  if (!strcmp(t, "applet.event")) { if (!s_doc) { ack(id, false, "applet not running", from); return true; } raise((const char*)(d["name"] | "")); ack(s_id, true, "", from); return true; }
  if (!strcmp(t, "applet.ui.get")) { JsonDocument r; r["t"] = "applet.ui"; r["id"] = s_id; r["text"] = s_doc ? uiText() : String("(home screen, no applet running)"); bridge::sendVia(from, r); return true; }
  if (!strcmp(t, "applet.log.get")) { JsonDocument r; r["t"] = "applet.log.list"; r["id"] = s_id; JsonArray a = r["lines"].to<JsonArray>(); for (auto& l : s_log) a.add(l); bridge::sendVia(from, r); return true; }
  return false;
}

// ---------------- loop ----------------
void begin() {
  if (!LittleFS.exists(DIR)) LittleFS.mkdir(DIR);
  for (auto& i : list()) if (i.autostart) { String e; if (!open(i.id, e)) bridge::log("autostart %s failed: %s", i.id.c_str(), e.c_str()); break; }
}
void tick() {
  uint32_t now = millis();
  if (!s_ntpStarted && bridge::wifiUp()) { s_ntpStarted = true; configTime(0, 0, "pool.ntp.org", "time.google.com"); }
  if (s_exitReq) {
    s_exitReq = false;
    if (s_doc) { bool wasOn = s_onScreen; raise("stop"); unload(); if (wasOn) ui::showHome(); }
    return;
  }
  if (!s_doc) return;
  bool active = s_onScreen || s_background;
  if (active) {
    for (auto& tm : s_timers) if ((int32_t)(now - tm.next) >= 0) { tm.next = now + tm.period; raise(tm.key.c_str()); }
    for (auto& w : s_watches) {
      int lvl = digitalRead(w.pin);
      if (lvl != w.last && now - w.lastChange > 25) {
        bool rise = lvl == HIGH; w.last = lvl; w.lastChange = now;
        if (w.mode == 2 || (w.mode == 0 && rise) || (w.mode == 1 && !rise)) raise(w.key.c_str());
      }
    }
  }
  if (s_persistDirty && (int32_t)(now - s_persistDue) >= 0) persistNow();
  for (auto it = s_pending.begin(); it != s_pending.end();) {
    if (now - it->second.at > 20000) { s_err = "timeout"; JsonArray els = it->second.els; it = s_pending.erase(it); runActions(els); } else ++it;
  }
  if (s_dirty && s_onScreen) { s_dirty = false; refreshBindings(); }
}
}  // namespace applets
