#include "ble_hid.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <HIDTypes.h>
#include "../core/settings.h"
#include "../link/link.h"

namespace blehid {
static NimBLEServer* s_server = nullptr;
static NimBLEHIDDevice* s_hid = nullptr;
static NimBLECharacteristic* s_kbd = nullptr;
static NimBLECharacteristic* s_media = nullptr;
static bool s_started = false, s_connected = false;
static String s_queue; static uint32_t s_nextCharAt = 0; static bool s_keyDown = false;
// NUS
static NimBLECharacteristic* s_nusTx = nullptr; static NimBLECharacteristic* s_nusRx = nullptr;
static bool s_nusSub = false; static LineCb s_lineCb = nullptr; static String s_nusBuf;
// Inbound bytes arrive on the NimBLE host task; they are queued here and parsed from the main loop (tick()),
// because the protocol handler touches LVGL and app state that are not thread-safe.
static portMUX_TYPE s_rxMux = portMUX_INITIALIZER_UNLOCKED;
static std::string s_rxQueue;
static const char* NUS_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* NUS_RX  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* NUS_TX  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
class NusRxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    std::string v = c->getValue();
    portENTER_CRITICAL(&s_rxMux);
    if (s_rxQueue.size() < 16384) s_rxQueue += v;
    portEXIT_CRITICAL(&s_rxMux);
  }
};
static void drainRx() {
  std::string chunk;
  portENTER_CRITICAL(&s_rxMux);
  chunk.swap(s_rxQueue);
  portEXIT_CRITICAL(&s_rxMux);
  for (char ch : chunk) {
    if (ch == '\n') { if (s_nusBuf.length() && s_lineCb) s_lineCb(s_nusBuf.c_str(), s_nusBuf.length()); s_nusBuf = ""; }
    else if (s_nusBuf.length() < 6000) s_nusBuf += ch;
  }
}
class NusTxCb : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t subValue) override { s_nusSub = subValue != 0; }   // logged from tick()
};

static const uint8_t REPORT_MAP[] = {
  USAGE_PAGE(1), 0x01, USAGE(1), 0x06, COLLECTION(1), 0x01, REPORT_ID(1), 0x01,
  USAGE_PAGE(1), 0x07, USAGE_MINIMUM(1), 0xE0, USAGE_MAXIMUM(1), 0xE7, LOGICAL_MINIMUM(1), 0x00, LOGICAL_MAXIMUM(1), 0x01,
  REPORT_SIZE(1), 0x01, REPORT_COUNT(1), 0x08, HIDINPUT(1), 0x02,
  REPORT_COUNT(1), 0x01, REPORT_SIZE(1), 0x08, HIDINPUT(1), 0x01,
  REPORT_COUNT(1), 0x05, REPORT_SIZE(1), 0x01, USAGE_PAGE(1), 0x08, USAGE_MINIMUM(1), 0x01, USAGE_MAXIMUM(1), 0x05, HIDOUTPUT(1), 0x02,
  REPORT_COUNT(1), 0x01, REPORT_SIZE(1), 0x03, HIDOUTPUT(1), 0x01,
  REPORT_COUNT(1), 0x06, REPORT_SIZE(1), 0x08, LOGICAL_MINIMUM(1), 0x00, LOGICAL_MAXIMUM(1), 0x65,
  USAGE_PAGE(1), 0x07, USAGE_MINIMUM(1), 0x00, USAGE_MAXIMUM(1), 0x65, HIDINPUT(1), 0x00,
  END_COLLECTION(0),
  USAGE_PAGE(1), 0x0C, USAGE(1), 0x01, COLLECTION(1), 0x01, REPORT_ID(1), 0x02,
  LOGICAL_MINIMUM(1), 0x00, LOGICAL_MAXIMUM(2), 0xFF, 0x03, USAGE_MINIMUM(1), 0x00, USAGE_MAXIMUM(2), 0xFF, 0x03,
  REPORT_SIZE(1), 0x10, REPORT_COUNT(1), 0x01, HIDINPUT(1), 0x00,
  END_COLLECTION(0)
};

class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override { s_connected = true; bridge::log("ble hid connected"); }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override { s_connected = false; s_nusSub = false; bridge::log("ble disconnected (%d)", reason); NimBLEDevice::startAdvertising(); }
};

void begin() {
  if (s_started) return;
  NimBLEDevice::init("AITL Keyboard");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, no MITM (no display pin), secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new SrvCb());
  s_hid = new NimBLEHIDDevice(s_server);
  s_hid->setManufacturer("Caeruli Obscuri");
  s_hid->setPnp(0x02, 0x05ac, 0x820a, 0x0210);
  s_hid->setHidInfo(0x00, 0x01);
  s_hid->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  s_kbd = s_hid->getInputReport(1);
  s_media = s_hid->getInputReport(2);
  s_hid->setBatteryLevel(100);
  if (g_settings.bleLink) {
    NimBLEService* nus = s_server->createService(NUS_SVC);
    s_nusRx = nus->createCharacteristic(NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    s_nusTx = nus->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);
    s_nusRx->setCallbacks(new NusRxCb()); s_nusTx->setCallbacks(new NusTxCb());
    nus->start();
    NimBLEDevice::setMTU(247);
  }
  s_server->start();
  // Advertising packet: flags + appearance + HID UUID (fits in 31 bytes). Name + 128-bit NUS UUID go in the scan response.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData; advData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advData.setAppearance(HID_KEYBOARD); advData.addServiceUUID(s_hid->getHidService()->getUUID());
  NimBLEAdvertisementData scanResp; scanResp.setName("AITL Keyboard");
  if (g_settings.bleLink) scanResp.addServiceUUID(NimBLEUUID(NUS_SVC));
  adv->setAdvertisementData(advData); adv->setScanResponseData(scanResp);
  bool ok = adv->start();
  s_started = true;
  bridge::log("ble advertising 'AITL Keyboard'%s: %s", g_settings.bleLink ? " + uart link" : "", ok ? "ok" : "FAILED (payload?)");
}
void end() {
  if (!s_started) return;
  NimBLEDevice::deinit(true); s_started = false; s_connected = false; s_nusSub = false; s_nusTx = s_nusRx = nullptr; s_queue = "";
}
void setLineHandler(LineCb cb) { s_lineCb = cb; }
bool linkSubscribed() { return s_started && s_connected && s_nusSub && s_nusTx; }
bool linkSend(const char* data, size_t len) {
  if (!linkSubscribed()) return false;
  const size_t mtu = NimBLEDevice::getMTU() > 23 ? NimBLEDevice::getMTU() - 3 : 20;
  for (size_t off = 0; off < len; off += mtu) {
    size_t n = (len - off) < mtu ? (len - off) : mtu;
    s_nusTx->setValue((const uint8_t*)data + off, n);
    if (!s_nusTx->notify()) return false;
    delay(3);
  }
  return true;
}
bool started() { return s_started; }
bool connected() { return s_connected; }
String status() { if (!s_started) return "off"; return s_connected ? "connected" : "advertising"; }
size_t pendingChars() { return s_queue.length(); }

static void sendKbd(uint8_t mods, uint8_t key) {
  if (!s_connected || !s_kbd) return;
  uint8_t r[8] = {mods, 0, key, 0, 0, 0, 0, 0};
  s_kbd->setValue(r, sizeof(r)); s_kbd->notify();
}
void tap(uint8_t mods, uint8_t keycode) { sendKbd(mods, keycode); delay(6); sendKbd(0, 0); delay(6); }
void consumer(uint16_t usage) {
  if (!s_connected || !s_media) return;
  uint8_t r[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  s_media->setValue(r, 2); s_media->notify(); delay(6);
  r[0] = 0; r[1] = 0; s_media->setValue(r, 2); s_media->notify();
}

// ASCII -> (shift, keycode)
static bool asciiToKey(char c, uint8_t& mods, uint8_t& key) {
  mods = 0;
  if (c >= 'a' && c <= 'z') { key = 4 + (c - 'a'); return true; }
  if (c >= 'A' && c <= 'Z') { key = 4 + (c - 'A'); mods = L_SHIFT; return true; }
  if (c >= '1' && c <= '9') { key = 30 + (c - '1'); return true; }
  if (c == '0') { key = 39; return true; }
  switch (c) {
    case '\n': key = 40; return true;  case '\t': key = 43; return true;  case ' ': key = 44; return true;
    case '-': key = 45; return true;   case '=': key = 46; return true;   case '[': key = 47; return true;
    case ']': key = 48; return true;   case '\\': key = 49; return true;  case ';': key = 51; return true;
    case '\'': key = 52; return true;  case '`': key = 53; return true;   case ',': key = 54; return true;
    case '.': key = 55; return true;   case '/': key = 56; return true;
    case '!': key = 30; mods = L_SHIFT; return true; case '@': key = 31; mods = L_SHIFT; return true;
    case '#': key = 32; mods = L_SHIFT; return true; case '$': key = 33; mods = L_SHIFT; return true;
    case '%': key = 34; mods = L_SHIFT; return true; case '^': key = 35; mods = L_SHIFT; return true;
    case '&': key = 36; mods = L_SHIFT; return true; case '*': key = 37; mods = L_SHIFT; return true;
    case '(': key = 38; mods = L_SHIFT; return true; case ')': key = 39; mods = L_SHIFT; return true;
    case '_': key = 45; mods = L_SHIFT; return true; case '+': key = 46; mods = L_SHIFT; return true;
    case '{': key = 47; mods = L_SHIFT; return true; case '}': key = 48; mods = L_SHIFT; return true;
    case '|': key = 49; mods = L_SHIFT; return true; case ':': key = 51; mods = L_SHIFT; return true;
    case '"': key = 52; mods = L_SHIFT; return true; case '~': key = 53; mods = L_SHIFT; return true;
    case '<': key = 54; mods = L_SHIFT; return true; case '>': key = 55; mods = L_SHIFT; return true;
    case '?': key = 56; mods = L_SHIFT; return true;
  }
  return false;
}

void typeText(const char* utf8) { if (utf8) s_queue += utf8; }

static uint8_t namedKey(const String& n) {
  if (n == "enter" || n == "return") return 40; if (n == "esc" || n == "escape") return 41; if (n == "backspace") return 42;
  if (n == "tab") return 43; if (n == "space") return 44; if (n == "right") return 79; if (n == "left") return 80;
  if (n == "down") return 81; if (n == "up") return 82; if (n == "delete") return 76; if (n == "home") return 74; if (n == "end") return 77;
  if (n.length() >= 2 && n[0] == 'f') { int f = n.substring(1).toInt(); if (f >= 1 && f <= 12) return 58 + (f - 1); }
  if (n.length() == 1) { uint8_t m, k; if (asciiToKey(n[0], m, k)) return k; }
  return 0;
}
void combo(const char* spec) {
  String s(spec); s.toLowerCase(); uint8_t mods = 0; uint8_t key = 0;
  int start = 0;
  while (start <= (int)s.length()) {
    int plus = s.indexOf('+', start); String part = plus < 0 ? s.substring(start) : s.substring(start, plus); part.trim();
    if (part == "cmd" || part == "gui" || part == "win" || part == "meta") mods |= L_GUI;
    else if (part == "rcmd") mods |= R_GUI;
    else if (part == "ctrl" || part == "control") mods |= L_CTRL;
    else if (part == "alt" || part == "opt" || part == "option") mods |= L_ALT;
    else if (part == "shift") mods |= L_SHIFT;
    else if (part.length()) key = namedKey(part);
    if (plus < 0) break; start = plus + 1;
  }
  if (key || mods) tap(mods, key);
}
void dictationShortcut() {
  switch (g_settings.dictShortcut) {
    case 0: tap(R_GUI, 0); delay(60); tap(R_GUI, 0); break;        // "Press Right Command key twice"
    case 1: tap(L_CTRL, 0); delay(60); tap(L_CTRL, 0); break;      // "Press Control key twice"
    case 2: tap(0, 62); break;                                      // F5
    default: tap(L_GUI | L_ALT, 7); break;                          // Cmd+Opt+D (custom shortcut)
  }
}
void tick() {
  if (!s_started) return;
  static bool lastSub = false;
  if (s_nusSub != lastSub) { lastSub = s_nusSub; bridge::log("ble link %s", s_nusSub ? "subscribed" : "unsubscribed"); }
  drainRx();
  if (!s_connected || !s_queue.length()) return;
  uint32_t now = millis(); if (now < s_nextCharAt) return;
  if (s_keyDown) { sendKbd(0, 0); s_keyDown = false; s_nextCharAt = now + 4; return; }
  char c = s_queue[0]; s_queue.remove(0, 1);
  uint8_t m, k;
  if ((uint8_t)c >= 0x80) { s_nextCharAt = now; return; }   // skip non-ASCII bytes (HID keyboards have no Unicode)
  if (asciiToKey(c, m, k)) { sendKbd(m, k); s_keyDown = true; s_nextCharAt = now + 6; }
}
}  // namespace blehid
