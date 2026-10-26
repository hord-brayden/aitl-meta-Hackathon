#include "app.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include "../config.h"
#include "settings.h"
#include "../link/link.h"
#include "../ble/ble_hid.h"
#include "../hal/led.h"
#include "../hal/sdcard.h"
#include "../hal/display.h"
#include "../hal/mic.h"
#include <mbedtls/base64.h>
#include <sys/time.h>
#include "../applets/applet.h"

namespace app {
std::vector<Model> models; std::vector<Chat> chats; std::vector<Action> actions; std::vector<CcSession> ccSessions; Alert alert; std::vector<Alert> inbox; uint8_t inboxUnread = 0; NowPlaying np;
String currentModel, currentChatId, currentChatTitle, bridgeName, bridgeVersion, frontApp;
bool dictating = false, streaming = false, hasReply = false, capturing = false;
static uint32_t s_captureStart = 0;
String pairFingerprint, pairCode, pairName;
static String s_pendingCa, s_pendingToken;
static UiCb s_ui = nullptr;
static std::vector<Msg> s_msgs; static String s_lastUser, s_lastReply;
static uint32_t s_lastHelloTx = 0, s_lastRxSerial = 0, s_lastRxWifi = 0, s_lastRxBle = 0, s_lastPing = 0;
static bool s_wsWasUp = false;

static void emit(Ev e, const char* a = "") { if (s_ui) s_ui(e, a); }
void setUiCb(UiCb cb) { s_ui = cb; }
const std::vector<Msg>& messages() { return s_msgs; }
bool transcriptEmpty() { return s_msgs.empty(); }
void clearTranscript() { s_msgs.clear(); s_lastReply = ""; hasReply = false; emit(Ev::Transcript, "reset"); }
bool bridgeLinked() { return bridge::connected(); }
String linkSummary() { switch (bridge::active()) { case bridge::Transport::Serial: return "USB"; case bridge::Transport::WiFi: return "WIFI"; case bridge::Transport::Ble: return "BLE"; default: return "--"; } }
String deviceId() { uint64_t mac = ESP.getEfuseMac(); char b[16]; snprintf(b, sizeof(b), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac); return String(b); }

static void pushMsg(uint8_t role, const String& text) {
  s_msgs.push_back({role, text});
  size_t total = 0; for (auto& m : s_msgs) total += m.text.length();
  while (s_msgs.size() > 2 && total > CHAT_VIEW_MAX) { total -= s_msgs.front().text.length(); s_msgs.erase(s_msgs.begin()); }
}
static void appendAssistant(const char* piece) {
  if (s_msgs.empty() || s_msgs.back().role != 1) s_msgs.push_back({1, String()});
  s_msgs.back().text += piece;
  if (s_msgs.back().text.length() > CHAT_VIEW_MAX) s_msgs.back().text.remove(0, s_msgs.back().text.length() - CHAT_VIEW_MAX);
}
// bridge transcripts mark user turns with "> " at line start
static void messagesFromPlain(const String& plain) {
  s_msgs.clear();
  int start = 0;
  while (start <= (int)plain.length()) {
    int nl = plain.indexOf('\n', start); String line = nl < 0 ? plain.substring(start) : plain.substring(start, nl);
    if (line.startsWith("> ")) s_msgs.push_back({0, line.substring(2)});
    else if (line.length() || (!s_msgs.empty() && s_msgs.back().role == 1)) {
      if (s_msgs.empty() || s_msgs.back().role != 1) s_msgs.push_back({1, String()});
      if (s_msgs.back().text.length()) s_msgs.back().text += '\n';
      s_msgs.back().text += line;
    }
    if (nl < 0) break; start = nl + 1;
  }
  for (auto& m : s_msgs) m.text.trim();
}
static void sendHello(bridge::Transport via = bridge::Transport::None) {
  DynamicJsonDocument d(512);
  d["t"] = "hello"; d["id"] = deviceId(); d["fw"] = AITL_FW_VERSION; d["name"] = AITL_PRODUCT;
  JsonArray caps = d.createNestedArray("caps"); caps.add("hid"); caps.add("ble"); caps.add("sd"); caps.add("wifi"); caps.add("mic"); caps.add("applets"); d["board"] = AITL_BOARD_NAME;
  if (g_settings.modelId[0]) d["model"] = g_settings.modelId;
  d["chat"] = currentChatId.length() ? currentChatId : String(g_settings.lastChat);
  d["heap"] = ESP.getFreeHeap();
  if (via == bridge::Transport::None) bridge::send(d); else bridge::sendVia(via, d);
  s_lastHelloTx = millis();
}

static void simple(const char* t) { DynamicJsonDocument d(128); d["t"] = t; bridge::send(d); }

void sendPrompt(const String& text) {
  if (!text.length()) return;
  if (!bridge::connected()) { emit(Ev::Error, "No bridge linked. Plug in USB or pair over Wi-Fi."); return; }
  s_lastUser = text;
  pushMsg(0, text);
  s_lastReply = ""; hasReply = false; streaming = true;
  ledSetMode(LedMode::Thinking);
  emit(Ev::Transcript, "reset");
  DynamicJsonDocument d(LINK_MAX_MSG);
  d["t"] = "chat.send"; d["text"] = text; d["chat"] = currentChatId; d["model"] = currentModel;
  bridge::send(d);
}
void stopGeneration() { if (streaming) simple("chat.stop"); }
void newChat() { currentChatId = ""; currentChatTitle = ""; g_settings.lastChat[0] = 0; settingsSave(); clearTranscript(); simple("chat.new"); emit(Ev::Lists); }
void openChat(const String& id) { DynamicJsonDocument d(256); d["t"] = "chat.open"; d["chat"] = id; bridge::send(d); }
void deleteChat(const String& id) { DynamicJsonDocument d(256); d["t"] = "chat.delete"; d["chat"] = id; bridge::send(d); if (id == currentChatId) { currentChatId = ""; currentChatTitle = ""; clearTranscript(); } }
void setModel(const String& id) {
  currentModel = id; strlcpy(g_settings.modelId, id.c_str(), sizeof(g_settings.modelId)); settingsSave();
  DynamicJsonDocument d(256); d["t"] = "model.set"; d["model"] = id; bridge::send(d); emit(Ev::Lists);
}
void runAction(const String& id) {
  if (!bridge::connected()) { emit(Ev::Error, "No bridge linked."); return; }
  String label = id; for (auto& a : actions) if (a.id == id) label = a.label;
  pushMsg(0, "[" + label + "]");
  s_lastReply = ""; hasReply = false; streaming = true; ledSetMode(LedMode::Thinking); emit(Ev::Transcript, "reset");
  DynamicJsonDocument d(512); d["t"] = "action.run"; d["id"] = id; d["chat"] = currentChatId; d["model"] = currentModel; bridge::send(d);
}
void refreshLists() { simple("model.list"); simple("chat.list"); simple("action.list"); }
void listSessions() { simple("cc.list"); }
void openSession(const String& id) { DynamicJsonDocument d(256); d["t"] = "cc.open"; d["id"] = id; bridge::send(d); }
void inboxMarkRead() { inboxUnread = 0; emit(Ev::Inbox); }
void inboxClear() { inbox.clear(); inboxUnread = 0; emit(Ev::Inbox); }
void replyAlert(const char* action) {
  if (!alert.id.length()) return;
  DynamicJsonDocument d(256); d["t"] = "alert.reply"; d["id"] = alert.id; d["action"] = action; bridge::send(d);
  alert = Alert(); if (!streaming) ledSetMode(LedMode::Idle); emit(Ev::AlertClear);
}

void typeToMac(const String& text) {
  if (g_settings.hidTarget == 1) {
    if (!blehid::connected()) { emit(Ev::Error, "BLE keyboard not connected."); return; }
    blehid::typeText(text.c_str()); ledSetMode(LedMode::Typing); return;
  }
  if (!bridge::connected()) { emit(Ev::Error, "No bridge linked."); return; }
  DynamicJsonDocument d(LINK_MAX_MSG); d["t"] = "hid.type"; d["text"] = text; bridge::send(d); ledSetMode(LedMode::Typing);
}
void keyToMac(const char* combo) {
  if (g_settings.hidTarget == 1) { if (blehid::connected()) blehid::combo(combo); else emit(Ev::Error, "BLE keyboard not connected."); return; }
  if (!bridge::connected()) { emit(Ev::Error, "No bridge linked."); return; }
  DynamicJsonDocument d(256); d["t"] = "hid.key"; d["combo"] = combo; bridge::send(d);
}
static void audioFrame(const uint8_t* adpcm, size_t len, uint16_t seq) {
  unsigned char b64[200]; size_t olen = 0;
  if (mbedtls_base64_encode(b64, sizeof(b64), &olen, adpcm, len) != 0) return;
  StaticJsonDocument<320> d; d["t"] = "audio.chunk"; d["seq"] = seq; d["d"] = (const char*)b64;
  bridge::send(d);
}
void captureStart() {
  if (capturing) return;
  if (!bridge::connected()) { emit(Ev::Error, "No bridge linked (needed to transcribe)."); return; }
  if (!mic::begin()) { emit(Ev::Error, "Mic driver failed to start."); return; }
  DynamicJsonDocument d(128); d["t"] = "audio.start"; d["rate"] = MIC_SAMPLE_RATE; d["codec"] = "ima-adpcm"; d["target"] = g_settings.dictTarget == 1 ? "type" : "ask"; bridge::send(d);
  mic::startCapture(audioFrame); capturing = true; s_captureStart = millis(); ledSetMode(LedMode::Listening); emit(Ev::Status);
}
void captureStop(bool discard) {
  if (!capturing) return;
  mic::stopCapture(); capturing = false;
  DynamicJsonDocument d(96); d["t"] = discard ? "audio.cancel" : "audio.stop"; bridge::send(d);
  ledSetMode(discard ? LedMode::Idle : LedMode::Thinking); emit(Ev::Status);
  if (!discard) emit(Ev::Toast, "Transcribing...");
}
void dictation(const char* op) {
  if (g_settings.dictSource == 1) {                      // device microphone
    bool start = !strcmp(op, "start") ? true : !strcmp(op, "stop") ? false : !capturing;
    if (start) captureStart(); else captureStop(false);
    return;
  }
  if (g_settings.hidTarget == 1) {
    if (!blehid::connected()) { emit(Ev::Error, "BLE keyboard not connected."); return; }
    blehid::dictationShortcut(); dictating = !dictating; ledSetMode(dictating ? LedMode::Listening : LedMode::Idle); emit(Ev::Status); return;
  }
  if (!bridge::connected()) { emit(Ev::Error, "No bridge linked."); return; }
  DynamicJsonDocument d(128); d["t"] = "dictation"; d["op"] = op; bridge::send(d);
}
void pasteLastReply() { if (!s_lastReply.length()) return; if (g_settings.hidTarget == 1) typeToMac(s_lastReply); else simple("hid.paste_last"); }
void copyLastReply() { if (!bridge::connected()) { emit(Ev::Error, "No bridge linked."); return; } simple("clip.set_last"); }
void mediaKey(uint16_t usage) {
  if (g_settings.hidTarget == 1 && blehid::connected()) { blehid::consumer(usage); return; }
  const char* k = usage == 0xE9 ? "vol_up" : usage == 0xEA ? "vol_down" : usage == 0xE2 ? "mute" : usage == 0xB5 ? "next" : usage == 0xB6 ? "prev" : "play_pause";
  DynamicJsonDocument d(128); d["t"] = "media"; d["key"] = k; bridge::send(d);
}

void startNetworkPairing() {
  if (!g_settings.bridgeHost[0]) { emit(Ev::Error, "Set bridge host first."); return; }
  bridge::setPairingMode(true); ledSetMode(LedMode::Pairing); emit(Ev::Toast, "Pairing: connecting to bridge...");
}
void confirmPairing(bool accept) {
  if (accept && s_pendingCa.length()) {
    bridge::storeBridgeCert(s_pendingCa);
    strlcpy(g_settings.bridgeToken, s_pendingToken.c_str(), sizeof(g_settings.bridgeToken));
    strlcpy(g_settings.bridgeName, pairName.c_str(), sizeof(g_settings.bridgeName));
    settingsSave();
    DynamicJsonDocument d(256); d["t"] = "pair.confirm"; d["code"] = pairCode; bridge::sendVia(bridge::Transport::WiFi, d);
    emit(Ev::Toast, "Paired. Reconnecting pinned...");
  } else { DynamicJsonDocument d(128); d["t"] = "pair.reject"; bridge::sendVia(bridge::Transport::WiFi, d); }
  s_pendingCa = ""; s_pendingToken = ""; pairFingerprint = ""; pairCode = "";
  delay(200); bridge::setPairingMode(false); ledSetMode(LedMode::Idle);
}
void forgetBridge() {
  g_settings.bridgeToken[0] = 0; g_settings.bridgeName[0] = 0; settingsSave(); bridge::storeBridgeCert(""); bridge::setPairingMode(false); emit(Ev::Link);
}

static void onMessage(JsonDocument& d, bridge::Transport from) {
  const char* t = d["t"] | "";
  if (from == bridge::Transport::Serial) s_lastRxSerial = millis(); else if (from == bridge::Transport::WiFi) s_lastRxWifi = millis(); else s_lastRxBle = millis();
  if (applets::onMessage(d, from)) return;
  if (!strcmp(t, "hello.ack")) {
    if (d["now"].is<double>() && d["now"].as<double>() > 1600000000) { struct timeval tv = {(time_t)d["now"].as<double>(), 0}; settimeofday(&tv, nullptr); const char* tz = d["tz"] | ""; if (*tz) { setenv("TZ", tz, 1); tzset(); } }
    bridgeName = (const char*)(d["bridge"] | "bridge"); bridgeVersion = (const char*)(d["version"] | "");
    models.clear(); for (JsonObject m : d["models"].as<JsonArray>()) models.push_back({(const char*)(m["id"] | ""), (const char*)(m["label"] | ""), (const char*)(m["provider"] | ""), (bool)(m["ok"] | false)});
    chats.clear();  for (JsonObject c : d["chats"].as<JsonArray>())  chats.push_back({(const char*)(c["id"] | ""), (const char*)(c["title"] | "")});
    actions.clear(); for (JsonObject a : d["actions"].as<JsonArray>()) actions.push_back({(const char*)(a["id"] | ""), (const char*)(a["label"] | "")});
    currentModel = (const char*)(d["model"] | currentModel.c_str());
    if (d.containsKey("chat")) { currentChatId = (const char*)(d["chat"] | ""); currentChatTitle = (const char*)(d["title"] | ""); if (s_msgs.empty() && currentChatId.length()) openChat(currentChatId); }
    bridge::markHello(from, true); ledSetMode(LedMode::Idle);
    emit(Ev::Lists); emit(Ev::Link, "linked");
    bridge::log("linked via %s: %s %s, %u models", from == bridge::Transport::Serial ? "usb" : from == bridge::Transport::WiFi ? "wifi" : "ble", bridgeName.c_str(), bridgeVersion.c_str(), (unsigned)models.size());
  } else if (!strcmp(t, "chat.delta")) {
    const char* piece = d["d"] | ""; appendAssistant(piece); s_lastReply += piece; streaming = true; emit(Ev::Transcript, "delta");
  } else if (!strcmp(t, "chat.done")) {
    streaming = false; hasReply = s_lastReply.length() > 0;
    currentChatId = (const char*)(d["chat"] | currentChatId.c_str()); currentChatTitle = (const char*)(d["title"] | currentChatTitle.c_str());
    ledSetMode(LedMode::Done); emit(Ev::Transcript, "delta"); emit(Ev::Lists);
    if (currentChatId != String(g_settings.lastChat)) { strlcpy(g_settings.lastChat, currentChatId.c_str(), sizeof(g_settings.lastChat)); settingsSave(); }
    if (g_settings.sdLog && sdReady() && currentChatId.length()) {
      String p = "/aitl/chats/" + currentChatId + ".txt"; sdAppendLine(p.c_str(), "> " + s_lastUser); sdAppendLine(p.c_str(), s_lastReply + "\n");
    }
  } else if (!strcmp(t, "chat.error")) {
    streaming = false; ledSetMode(LedMode::Error); pushMsg(2, (const char*)(d["msg"] | "error")); emit(Ev::Transcript, "reset"); emit(Ev::Error, d["msg"] | "error");
  } else if (!strcmp(t, "chat.view")) {
    currentChatId = (const char*)(d["chat"] | ""); currentChatTitle = (const char*)(d["title"] | "");
    messagesFromPlain((const char*)(d["text"] | "")); s_lastReply = (const char*)(d["last"] | ""); hasReply = s_lastReply.length() > 0; streaming = false; emit(Ev::Transcript, "reset"); emit(Ev::Lists);
    if (currentChatId != String(g_settings.lastChat)) { strlcpy(g_settings.lastChat, currentChatId.c_str(), sizeof(g_settings.lastChat)); settingsSave(); }
  } else if (!strcmp(t, "chat.list")) {
    chats.clear(); for (JsonObject c : d["chats"].as<JsonArray>()) chats.push_back({(const char*)(c["id"] | ""), (const char*)(c["title"] | "")}); emit(Ev::Lists);
  } else if (!strcmp(t, "model.list")) {
    models.clear(); for (JsonObject m : d["models"].as<JsonArray>()) models.push_back({(const char*)(m["id"] | ""), (const char*)(m["label"] | ""), (const char*)(m["provider"] | ""), (bool)(m["ok"] | false)});
    if (d.containsKey("model")) currentModel = (const char*)d["model"]; emit(Ev::Lists);
  } else if (!strcmp(t, "action.list")) {
    actions.clear(); for (JsonObject a : d["actions"].as<JsonArray>()) actions.push_back({(const char*)(a["id"] | ""), (const char*)(a["label"] | "")}); emit(Ev::Lists);
  } else if (!strcmp(t, "status")) {
    if (d.containsKey("dictating")) { dictating = d["dictating"]; if (!streaming) ledSetMode(dictating ? LedMode::Listening : LedMode::Idle); }
    if (d.containsKey("app")) frontApp = (const char*)d["app"];
    emit(Ev::Status);
  } else if (!strcmp(t, "ui.toast")) { emit(Ev::Toast, d["msg"] | "");
  } else if (!strcmp(t, "prov.wifi")) {
    strlcpy(g_settings.wifiSsid, d["ssid"] | "", sizeof(g_settings.wifiSsid)); strlcpy(g_settings.wifiPass, d["pass"] | "", sizeof(g_settings.wifiPass));
    g_settings.wifiEnabled = true; settingsSave(); emit(Ev::Provisioned, "wifi"); DynamicJsonDocument r(128); r["t"] = "prov.ok"; r["what"] = "wifi"; bridge::sendVia(from, r); delay(400); ESP.restart();
  } else if (!strcmp(t, "prov.bridge")) {
    strlcpy(g_settings.bridgeHost, d["host"] | "", sizeof(g_settings.bridgeHost)); g_settings.bridgePort = d["port"] | BRIDGE_WS_PORT_DEFAULT;
    strlcpy(g_settings.bridgeToken, d["token"] | "", sizeof(g_settings.bridgeToken)); strlcpy(g_settings.bridgeName, d["name"] | "", sizeof(g_settings.bridgeName));
    g_settings.wifiMode = (!strcmp(d["mode"] | "signed", "tls")) ? 1 : 0;
    if (d.containsKey("ca") && strlen(d["ca"] | "")) bridge::storeBridgeCert((const char*)d["ca"]);
    settingsSave(); emit(Ev::Provisioned, "bridge"); DynamicJsonDocument r(128); r["t"] = "prov.ok"; r["what"] = "bridge"; bridge::sendVia(from, r);
    if (bridge::wifiUp()) { bridge::setPairingMode(false); }
  } else if (!strcmp(t, "pair.offer")) {
    s_pendingCa = (const char*)(d["ca"] | ""); s_pendingToken = (const char*)(d["token"] | ""); pairFingerprint = (const char*)(d["fp"] | ""); pairCode = (const char*)(d["code"] | ""); pairName = (const char*)(d["name"] | "");
    emit(Ev::Pairing);
  } else if (!strcmp(t, "dictation.text")) {
    const char* txt = d["text"] | ""; const char* note = d["note"] | "";
    if (!*txt) { ledSetMode(LedMode::Error); emit(Ev::Error, *note ? note : "Nothing transcribed"); }
    else if (!strcmp(d["target"] | "ask", "type")) { ledSetMode(LedMode::Done); emit(Ev::Toast, "Typed into Mac"); if (g_settings.hidTarget == 1 && blehid::connected()) blehid::typeText(txt); }
    else { s_lastUser = txt; pushMsg(0, txt); s_lastReply = ""; hasReply = false; streaming = true; ledSetMode(LedMode::Thinking); emit(Ev::Transcript, "reset"); }
  } else if (!strcmp(t, "settings.get")) {       // full settings dump for the Mac app (never includes the Wi-Fi password or the token)
    DynamicJsonDocument r(1024);
    r["t"] = "settings.dump"; r["board"] = AITL_BOARD_NAME; r["fw"] = AITL_FW_VERSION; r["id"] = deviceId(); r["heap"] = ESP.getFreeHeap();
    r["brightness"] = g_settings.brightness; r["screenTimeout"] = g_settings.screenTimeout; r["rotation"] = g_settings.rotation;
    r["invertColors"] = g_settings.invertColors; r["bgrOrder"] = g_settings.bgrOrder; r["longPressMs"] = g_settings.longPressMs; r["touchDebounce"] = g_settings.touchDebounce;
    r["hapticLed"] = g_settings.hapticLed; r["sdLog"] = g_settings.sdLog; r["wifiEnabled"] = g_settings.wifiEnabled; r["wifiSsid"] = g_settings.wifiSsid; r["wifiMode"] = g_settings.wifiMode;
    r["bridgeHost"] = g_settings.bridgeHost; r["bridgePort"] = g_settings.bridgePort; r["bridgeName"] = g_settings.bridgeName; r["paired"] = g_settings.bridgeToken[0] != 0;
    r["bleEnabled"] = g_settings.bleEnabled; r["bleLink"] = g_settings.bleLink; r["bleConnected"] = blehid::connected(); r["hidTarget"] = g_settings.hidTarget; r["dictShortcut"] = g_settings.dictShortcut;
    r["dictSource"] = g_settings.dictSource; r["dictTarget"] = g_settings.dictTarget; r["micGain"] = g_settings.micGain; r["model"] = currentModel; r["setupDone"] = g_settings.setupDone;
    r["wifiIp"] = bridge::ipAddress(); r["wifiStatus"] = bridge::wifiStatus(); r["bridgeStatus"] = bridge::bridgeStatus(); r["sd"] = sdInfo();
    bridge::sendVia(from, r);
  } else if (!strcmp(t, "settings.set")) {               // bridge-side `musecorder set key value` (USB/BLE/Wi-Fi, authenticated link)
    const char* k = d["key"] | ""; JsonVariant v = d["value"]; bool ok = true;
    if (!strcmp(k, "bleEnabled")) { g_settings.bleEnabled = v.as<bool>(); if (g_settings.bleEnabled && !g_settings.wifiEnabled) blehid::begin(); else blehid::end(); }
    else if (!strcmp(k, "bleLink")) { g_settings.bleLink = v.as<bool>(); if (g_settings.bleEnabled) { blehid::end(); blehid::begin(); } }
    else if (!strcmp(k, "wifiEnabled")) { g_settings.wifiEnabled = v.as<bool>(); settingsSave(); DynamicJsonDocument r(160); r["t"] = "settings.ack"; r["key"] = k; r["ok"] = true; bridge::sendVia(from, r); emit(Ev::Toast, g_settings.wifiEnabled ? "Wi-Fi on: restarting..." : "Wi-Fi off: restarting..."); delay(400); ESP.restart(); }   // radio stacks don't fully release RAM; a restart gives Wi-Fi a clean heap
    else if (!strcmp(k, "dictSource")) g_settings.dictSource = v.as<int>() ? 1 : 0;
    else if (!strcmp(k, "dictTarget")) g_settings.dictTarget = v.as<int>() ? 1 : 0;
    else if (!strcmp(k, "micGain")) g_settings.micGain = constrain(v.as<int>(), 1, 8);
    else if (!strcmp(k, "hidTarget")) g_settings.hidTarget = v.as<int>() ? 1 : 0;
    else if (!strcmp(k, "brightness")) { g_settings.brightness = constrain(v.as<int>(), 10, 255); displayApplyBrightness(); }
    else if (!strcmp(k, "rotation")) { settingsSetRotation(v.as<int>()); displayApplyPanel(); }
    else if (!strcmp(k, "model")) { setModel(String((const char*)(v | ""))); }
    else if (!strcmp(k, "screenTimeout")) { g_settings.screenTimeout = v.as<int>(); displayWake(); }
    else if (!strcmp(k, "invertColors")) { g_settings.invertColors = v.as<bool>(); displayApplyPanel(); }
    else if (!strcmp(k, "bgrOrder")) { g_settings.bgrOrder = v.as<bool>(); displayApplyPanel(); }
    else if (!strcmp(k, "longPressMs")) { g_settings.longPressMs = constrain(v.as<int>(), 150, 1500); }
    else if (!strcmp(k, "touchDebounce")) { g_settings.touchDebounce = constrain(v.as<int>(), 1, 4); }
    else if (!strcmp(k, "hapticLed")) { g_settings.hapticLed = v.as<bool>(); }
    else if (!strcmp(k, "sdLog")) { g_settings.sdLog = v.as<bool>(); }
    else if (!strcmp(k, "dictShortcut")) { g_settings.dictShortcut = constrain(v.as<int>(), 0, 3); }
    else if (!strcmp(k, "calibrate")) { emit(Ev::Toast, "Touch calibration requested from the Mac"); /* UI opens it via Ev */ }
    else if (!strcmp(k, "reboot")) { DynamicJsonDocument r(96); r["t"] = "settings.ack"; r["key"] = k; r["ok"] = true; bridge::sendVia(from, r); delay(300); ESP.restart(); }
    else if (!strcmp(k, "factoryReset")) { settingsReset(); bridge::storeBridgeCert(""); delay(300); ESP.restart(); }
    else if (!strcmp(k, "bench")) {   // text-rendering microbenchmark, results in the log
      int lines = constrain(v.as<int>(), 1, 14);
      String txt; for (int i = 0; i < lines; i++) txt += "The quick brown fox jumps over the lazy dog while the bridge streams tokens to the device.\n";
      struct F { const char* name; const lv_font_t* f; } fonts[] = {{"montserrat16", &lv_font_montserrat_16}};
      lv_obj_t* scr = lv_obj_create(nullptr); lv_obj_set_style_bg_color(scr, lv_color_hex(0), 0); lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
      lv_obj_t* prev = lv_scr_act(); lv_scr_load(scr); lv_timer_handler();
      for (auto& fx : fonts) {
        lv_point_t sz; uint32_t t0 = micros(); for (int i = 0; i < 5; i++) lv_txt_get_size(&sz, txt.c_str(), fx.f, 0, 0, displayW(), LV_TEXT_FLAG_NONE); uint32_t layoutUs = (micros() - t0) / 5;
        lv_obj_clean(scr);
        lv_obj_t* l = lv_label_create(scr); lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_font(l, fx.f, 0); lv_obj_set_style_text_color(l, lv_color_hex(0xe8eef5), 0); lv_label_set_text(l, txt.c_str());
        lv_timer_handler();
        t0 = millis(); for (int i = 0; i < 3; i++) { lv_obj_invalidate(scr); lv_refr_now(nullptr); } uint32_t frameMs = (millis() - t0) / 3;
        uint32_t g0 = micros(); lv_font_glyph_dsc_t g; for (int i = 0; i < 2000; i++) lv_font_get_glyph_dsc(fx.f, &g, 'a' + (i % 26), 'b'); uint32_t dscUs = (micros() - g0) / 2000;
        g0 = micros(); for (int i = 0; i < 2000; i++) lv_font_get_glyph_bitmap(fx.f, 'a' + (i % 26)); uint32_t bmpUs = (micros() - g0) / 2000;
        bridge::log("bench %s lines=%d: layout=%luus frame=%lums glyph_dsc=%luus bitmap=%luus", fx.name, lines, (unsigned long)layoutUs, (unsigned long)frameMs, (unsigned long)dscUs, (unsigned long)bmpUs);
      }
      lv_scr_load(prev); lv_obj_del(scr);
    }
    else ok = false;
    if (ok) { settingsSave(); String msg = String("Setting applied: ") + k; emit(Ev::Toast, msg.c_str()); }
    DynamicJsonDocument r(160); r["t"] = "settings.ack"; r["key"] = k; r["ok"] = ok; bridge::sendVia(from, r);
  } else if (!strcmp(t, "np")) {
    np.app = (const char*)(d["app"] | ""); np.state = (const char*)(d["state"] | ""); np.title = (const char*)(d["title"] | ""); np.artist = (const char*)(d["artist"] | "");
    emit(Ev::NowPlaying);
  } else if (!strcmp(t, "cc.list")) {
    ccSessions.clear(); for (JsonObject c : d["sessions"].as<JsonArray>()) ccSessions.push_back({(const char*)(c["id"] | ""), (const char*)(c["title"] | ""), (const char*)(c["project"] | ""), (const char*)(c["age"] | "")}); emit(Ev::Lists);
  } else if (!strcmp(t, "alert")) {
    alert.id = (const char*)(d["id"] | ""); alert.kind = (const char*)(d["kind"] | "notify"); alert.title = (const char*)(d["title"] | "");
    alert.text = (const char*)(d["text"] | ""); alert.project = (const char*)(d["project"] | ""); alert.session = (const char*)(d["session"] | ""); alert.twoWay = alert.kind == "permission";
    inbox.insert(inbox.begin(), alert); if (inbox.size() > 20) inbox.pop_back(); if (inboxUnread < 99) inboxUnread++;
    ledSetMode(alert.twoWay ? LedMode::Alert : LedMode::Done); if (!displayIsAwake()) displayWake(); emit(Ev::Alert); emit(Ev::Inbox);
  } else if (!strcmp(t, "alert.clear")) {
    if (alert.id == String((const char*)(d["id"] | ""))) { alert = Alert(); if (!streaming) ledSetMode(LedMode::Idle); emit(Ev::AlertClear); }
  } else if (!strcmp(t, "screen.get")) {   // stream the framebuffer as RGB565 rows (base64), read back from the panel
    int step = constrain((int)(d["scale"] | 1), 1, 4); int W = displayW(), H = displayH(); int w = (W + step - 1) / step;
    static uint16_t px[480]; unsigned char b64[1400];
    lv_timer_handler();   // flush pending draws first
    for (int y = 0; y < H; y += step) {
      if (!displayReadRow(y, px, w, step)) { DynamicJsonDocument r(96); r["t"] = "screen.done"; r["err"] = "panel not readable"; bridge::sendVia(from, r); return; }
      // LovyanGFX hands uint16_t rows back byte-swapped already (swap565): the wire is big-endian RGB565 as-is
      size_t olen = 0; mbedtls_base64_encode(b64, sizeof(b64), &olen, (const uint8_t*)px, w * 2);
      DynamicJsonDocument r(1600); r["t"] = "screen.row"; r["y"] = y / step; r["d"] = (const char*)b64; bridge::sendVia(from, r);
      if (from != bridge::Transport::Serial) delay(4);
    }
    DynamicJsonDocument r(128); r["t"] = "screen.done"; r["w"] = w; r["h"] = (H + step - 1) / step; r["fmt"] = "rgb565be"; bridge::sendVia(from, r);
  } else if (!strcmp(t, "pong")) { /* alive */
  } else if (!strcmp(t, "reboot")) { delay(100); ESP.restart();
  } else if (!strcmp(t, "ping")) { DynamicJsonDocument r(64); r["t"] = "pong"; bridge::sendVia(from, r); }
}

void begin() {
  currentModel = g_settings.modelId;
  bridge::setHandler(onMessage);
  bridge::setDeviceId(deviceId());
  bridge::begin();
  // Classic ESP32 without PSRAM: LVGL + Wi-Fi + TLS + BLE do not fit together (~45 KB for a TLS handshake alone).
  // Wi-Fi wins when both are enabled; BLE stays off until Wi-Fi is turned off again.
  if (g_settings.bleEnabled && !g_settings.wifiEnabled) blehid::begin();
  pinMode(PIN_BTN_BOOT, INPUT_PULLUP);
}

void tick() {
  bridge::tick(); blehid::tick(); sdTick();
  uint32_t now = millis();
  // hello: whenever a transport is up but not acked
  bool wsUp = bridge::wsUp();
  if (wsUp && !s_wsWasUp) sendHello(bridge::Transport::WiFi);
  s_wsWasUp = wsUp;
  static bool bleWasUp = false; static uint32_t bleUpAt = 0;
  bool bleUp = bridge::bleUp();
  if (bleUp && !bleWasUp) bleUpAt = now;
  if (bleUp && bleWasUp && bleUpAt && now - bleUpAt > 400) { sendHello(bridge::Transport::Ble); bleUpAt = 0; }   // after the subscription settles
  if (!bleUp) { bleUpAt = 0; if (bleWasUp) { bridge::markHello(bridge::Transport::Ble, false); emit(Ev::Link, bridge::connected() ? "linked" : "lost"); } }
  bleWasUp = bleUp;
  if (!bridge::connected() && now - s_lastHelloTx > 3000) sendHello();
  // liveness: ping every 5s, drop after 15s silence
  if (bridge::connected()) {
    if (now - s_lastPing > 5000) { simple("ping"); s_lastPing = now; }
    if (bridge::active() == bridge::Transport::Serial && now - s_lastRxSerial > 15000) { bridge::markHello(bridge::Transport::Serial, false); emit(Ev::Link, "lost"); }
    if (bridge::active() == bridge::Transport::WiFi && now - s_lastRxWifi > 20000) { bridge::markHello(bridge::Transport::WiFi, false); emit(Ev::Link, "lost"); }
    if (bridge::active() == bridge::Transport::Ble && now - s_lastRxBle > 25000) { bridge::markHello(bridge::Transport::Ble, false); emit(Ev::Link, "lost"); }
  }
  mic::tick();
  if (capturing) {
    uint32_t dur = now - s_captureStart;
    if ((mic::heardSpeech() && mic::silenceMs() > 1600 && dur > 1500) || dur > 90000) captureStop(false);   // VAD auto-stop
    else if (!mic::heardSpeech() && dur > 8000) captureStop(true);                                           // nothing said
  }
  // BOOT button = physical push-to-talk. Host dictation: tap toggles. Device mic: tap toggles, hold = talk while held.
  static bool wasDown = false; static uint32_t downAt = 0; static bool holdCapture = false;
  bool down = digitalRead(PIN_BTN_BOOT) == LOW;
  if (down && !wasDown) { downAt = now; holdCapture = false; }
  if (down && wasDown && !holdCapture && g_settings.dictSource == 1 && !capturing && now - downAt > 350 && !(applets::running() && applets::wantsBoot())) { holdCapture = true; captureStart(); }
  static bool holdExit = false;
  if (applets::running()) {   // inside an applet: tap = the applet's boot:press (or push-to-talk when it has none), hold 3 s = leave
    if (down && !wasDown) holdExit = false;
    if (down && wasDown && !holdExit && now - downAt > 3000) { holdExit = true; holdCapture = false; if (capturing) captureStop(true); applets::handleBootHold(); }
    if (!down && wasDown && !holdExit) {
      if (holdCapture) captureStop(false);
      else if (now - downAt > 40 && now - downAt < 1500) { if (applets::wantsBoot()) applets::raise("boot:press"); else dictation("toggle"); }
    }
    wasDown = down; return;
  }
  if (!down && wasDown) {
    if (holdCapture) captureStop(false);
    else if (now - downAt > 40 && now - downAt < 1500) dictation("toggle");
  }
  wasDown = down;
  if (blehid::pendingChars() == 0 && !streaming && !dictating) { /* LED settles via Done/Error timeouts */ }
}
}  // namespace app
