// Settings (grouped cards), first-boot setup, touch calibration + touch test, network pairing, mic test.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/settings.h"
#include "../hal/display.h"
#include "../hal/sdcard.h"
#include "../link/link.h"
#include "../ble/ble_hid.h"
#include "../hal/mic.h"
#include "../applets/applet.h"
#include <LittleFS.h>

using namespace theme; using namespace ui_int;
namespace ui {

// ---------- callbacks ----------
static void cbBright(lv_event_t* e) { g_settings.brightness = lv_slider_get_value(lv_event_get_target(e)); displayApplyBrightness(); }
static void cbTimeout(lv_event_t* e) { static const uint16_t v[] = {10, 30, 60, 120, 300, 600, 0}; g_settings.screenTimeout = v[lv_dropdown_get_selected(lv_event_get_target(e))]; settingsSave(); displayWake(); }
static void cbRotate(lv_event_t* e) { settingsSetRotation(lv_dropdown_get_selected(lv_event_get_target(e))); displayApplyPanel(); showSettings(); }
static void cbInvert(lv_event_t* e) { g_settings.invertColors = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); displayApplyPanel(); }
static void cbBgr(lv_event_t* e) { g_settings.bgrOrder = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); displayApplyPanel(); }
static void cbCal(lv_event_t*) { showCalibrate(false); }
static void cbTouchTest(lv_event_t*) { showTouchTest(); }
static void cbLongPress(lv_event_t* e) { static const uint16_t v[] = {250, 400, 600, 900}; g_settings.longPressMs = v[lv_dropdown_get_selected(lv_event_get_target(e))]; settingsSave(); }
static void cbDebounce(lv_event_t* e) { g_settings.touchDebounce = 1 + lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void cbTapLed(lv_event_t* e) { g_settings.hapticLed = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); }
static void cbSdLog(lv_event_t* e) { g_settings.sdLog = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); }
static void cbWifi(lv_event_t* e) { g_settings.wifiEnabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); toast(g_settings.wifiEnabled ? "Wi-Fi on, restarting (Bluetooth pauses while Wi-Fi is on)" : "Wi-Fi off, restarting"); lv_timer_handler(); delay(900); ESP.restart(); }
static void cbBle(lv_event_t* e) { g_settings.bleEnabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); if (g_settings.bleEnabled && !g_settings.wifiEnabled) blehid::begin(); else blehid::end(); toast(!g_settings.bleEnabled ? "Bluetooth off" : g_settings.wifiEnabled ? "Saved. Bluetooth starts when Wi-Fi is off." : "Advertising as Musecorder Keyboard"); }
static void cbHidTarget(lv_event_t* e) { g_settings.hidTarget = lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void cbDictSrc(lv_event_t* e) { g_settings.dictSource = lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void cbDictTgt(lv_event_t* e) { g_settings.dictTarget = lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void cbMicGain(lv_event_t* e) { g_settings.micGain = lv_slider_get_value(lv_event_get_target(e)); settingsSave(); }
static void cbBleLink(lv_event_t* e) { g_settings.bleLink = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); if (g_settings.bleEnabled) { blehid::end(); blehid::begin(); } toast(g_settings.bleLink ? "BLE link on: run musecorder serve --ble" : "BLE link off"); }
static void cbMicTest(lv_event_t*);
static void cbDictSc(lv_event_t* e) { g_settings.dictShortcut = lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void ssidDone(const char* v) { strlcpy(g_settings.wifiSsid, v, sizeof(g_settings.wifiSsid)); settingsSave(); showTextEntry("Wi-Fi password", g_settings.wifiPass, true, [](const char* p) { strlcpy(g_settings.wifiPass, p, sizeof(g_settings.wifiPass)); g_settings.wifiEnabled = true; settingsSave(); bridge::wifiApply(); showSettings(); }); }
static void cbSsid(lv_event_t*) { showTextEntry("Wi-Fi network name", g_settings.wifiSsid, false, ssidDone); }
static void hostDone(const char* v) { strlcpy(g_settings.bridgeHost, v, sizeof(g_settings.bridgeHost)); settingsSave(); showTextEntry("Bridge port", String(g_settings.bridgePort).c_str(), false, [](const char* p) { int port = atoi(p); if (port > 0 && port < 65536) g_settings.bridgePort = port; settingsSave(); showSettings(); }); }
static void cbHost(lv_event_t*) { showTextEntry("Bridge host (Mac name or IP)", g_settings.bridgeHost, false, hostDone); }
static void cbPair(lv_event_t*) { app::startNetworkPairing(); }
static void doForget() { app::forgetBridge(); showSettings(); }
static void cbForget(lv_event_t*) { confirm("Forget the bridge?", "The pinned certificate and token are removed.", doForget); }
static void doReset() { settingsReset(); bridge::storeBridgeCert(""); delay(200); ESP.restart(); }
static void cbReset(lv_event_t*) { confirm("Factory reset?", "All settings, Wi-Fi and pairing are erased.", doReset); }
static void cbReboot(lv_event_t*) { ESP.restart(); }
static void cbWizard(lv_event_t*) { showSetupWizard(); }
static void cbApps(lv_event_t*) { showApps(); }
static void doRemoveApplets() { for (auto& a : applets::list()) applets::remove(a.id); toast("All applets removed"); showSettings(); }
static void cbRemoveApplets(lv_event_t*) { confirm("Remove all applets?", "Every installed applet and its saved state is erased.", doRemoveApplets); }
static void cbClearAutostart(lv_event_t*) { for (auto& a : applets::list()) if (a.autostart) applets::setAutostart(a.id, false); toast("Boots to the home screen"); showSettings(); }

// Settings is a menu of sections; each section is its own light screen (a few rows), so even with a radio
// stack resident there is heap to spare, and 480-wide boards get one calm column instead of a wall of rows.
static int s_section = -1;
static const char* kSectionNames[] = {"Connection", "Bluetooth", "Voice", "Display", "Touch", "Apps", "Storage", "About"};
static const char* kSectionIcons[] = {LV_SYMBOL_USB, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_AUDIO, LV_SYMBOL_IMAGE, LV_SYMBOL_EDIT, LV_SYMBOL_LIST, LV_SYMBOL_SD_CARD, LV_SYMBOL_SETTINGS};
static String sectionSummary(int i) {
  switch (i) {
    case 0: return app::bridgeLinked() ? "Linked over " + app::linkSummary() : (g_settings.wifiEnabled ? bridge::wifiStatus() : String("USB, Wi-Fi off"));
    case 1: return g_settings.bleEnabled ? blehid::status() : String("off");
    case 2: return g_settings.dictSource == 1 ? "Device mic" : "Host Mac dictation";
    case 3: { char b[24]; snprintf(b, sizeof(b), "Brightness %u%%", (unsigned)(g_settings.brightness * 100 / 255)); return b; }
    case 4: return "Calibrate, test";
    case 5: { char b[24]; snprintf(b, sizeof(b), "%u installed", (unsigned)applets::list().size()); return b; }
    case 6: return sdInfo();
    default: return "Musecorder " AITL_FW_VERSION;
  }
}
static void buildSection(lv_obj_t* c, int i) {
  switch (i) {
    case 0: {
  addAction(c, "USB link", app::bridgeLinked() && app::linkSummary() == "USB" ? app::bridgeName.c_str() : "plug in, run musecorder serve", nullptr, nullptr);
  addSwitch(c, "Wi-Fi", g_settings.wifiEnabled, cbWifi, nullptr);
  addAction(c, "Network", bridge::wifiStatus().c_str(), cbSsid, nullptr);
  { String h = g_settings.bridgeHost[0] ? String(g_settings.bridgeHost) + ":" + g_settings.bridgePort : String("not set"); addAction(c, "Bridge host", h.c_str(), cbHost, nullptr); }
  addAction(c, "Bridge", bridge::bridgeStatus().c_str(), nullptr, nullptr);
  addAction(c, "Pair over network", bridge::hasBridgeCert() ? "paired (re-pair)" : "start", cbPair, nullptr);
  if (bridge::hasBridgeCert()) addAction(c, "Forget bridge", "", cbForget, nullptr);
      break; }
    case 1: {
  addSwitch(c, big() ? "Keyboard" : "Bluetooth keyboard", g_settings.bleEnabled, cbBle, nullptr);
  addAction(c, "Status", blehid::status().c_str(), nullptr, nullptr);
  addDropdown(c, "Keystrokes via", "Bridge (USB/Wi-Fi)\nBluetooth HID", g_settings.hidTarget, cbHidTarget, nullptr);
  addDropdown(c, "BLE dictation key", "Right Cmd x2\nCtrl x2\nF5\nCmd+Opt+D", g_settings.dictShortcut, cbDictSc, nullptr);
  addSwitch(c, big() ? "Bridge link" : "Bridge link over BLE", g_settings.bleLink, cbBleLink, nullptr);
      break; }
    case 2: {
  addDropdown(c, "Microphone", "Host Mac\nDevice mic (P3)", g_settings.dictSource, cbDictSrc, nullptr);
  addDropdown(c, "Device mic sends to", "Ask Muse\nType into Mac", g_settings.dictTarget, cbDictTgt, nullptr);
  addSlider(c, "Mic gain", 1, 8, g_settings.micGain, cbMicGain, nullptr);
  addAction(c, "Mic test", "", cbMicTest, nullptr);
      break; }
    case 3: {
  addSlider(c, "Brightness", 10, 255, g_settings.brightness, cbBright, nullptr);
  { static const uint16_t v[] = {10, 30, 60, 120, 300, 600, 0}; uint16_t sel = 3; for (int i = 0; i < 7; i++) if (v[i] == g_settings.screenTimeout) sel = i;
    addDropdown(c, "Dim after", "10 s\n30 s\n1 min\n2 min\n5 min\n10 min\nNever", sel, cbTimeout, nullptr); }
  addDropdown(c, "Rotation", "Portrait\nLandscape A\nPortrait flipped\nLandscape B", g_settings.rotation & 3, cbRotate, nullptr);
  addSwitch(c, "Invert colors", g_settings.invertColors, cbInvert, nullptr);
  addSwitch(c, "Swap red and blue", g_settings.bgrOrder, cbBgr, nullptr);
      break; }
    case 4: {
  addAction(c, "Calibrate touch", "", cbCal, nullptr);
  addAction(c, "Touch test", "draw", cbTouchTest, nullptr);
  { static const uint16_t v[] = {250, 400, 600, 900}; uint16_t sel = 1; for (int i = 0; i < 4; i++) if (v[i] == g_settings.longPressMs) sel = i;
    addDropdown(c, "Long-press time", "250 ms\n400 ms\n600 ms\n900 ms", sel, cbLongPress, nullptr); }
  addDropdown(c, "Sensitivity", "Instant\nNormal\nFirm\nDeliberate", g_settings.touchDebounce - 1, cbDebounce, nullptr);
  addSwitch(c, "LED flash on tap", g_settings.hapticLed, cbTapLed, nullptr);
      break; }
    case 5: {
  { auto apps = applets::list(); String auto_; for (auto& a : apps) if (a.autostart) auto_ = a.name;
    char n[40]; snprintf(n, sizeof(n), "%u installed", (unsigned)apps.size()); addAction(c, "Applets", n, cbApps, nullptr);
    addAction(c, "Running", applets::running() ? applets::runningName().c_str() : "none", nullptr, nullptr);
    addAction(c, "Open on boot", auto_.length() ? auto_.c_str() : "off", auto_.length() ? cbClearAutostart : nullptr, nullptr);
    { size_t tot = LittleFS.totalBytes(), used = LittleFS.usedBytes(); char f[40]; snprintf(f, sizeof(f), "%u KB free of %u", (unsigned)((tot - used) / 1024), (unsigned)(tot / 1024)); addAction(c, "Storage", f, nullptr, nullptr); }
    if (!apps.empty()) addAction(c, "Remove all applets", "", cbRemoveApplets, nullptr); }
      break; }
    case 6: {
  addAction(c, "microSD", sdInfo().c_str(), nullptr, nullptr);
  addSwitch(c, "Log chats to microSD", g_settings.sdLog, cbSdLog, nullptr);
      break; }
    case 7: {
  addAction(c, "Firmware", "Musecorder " AITL_FW_VERSION " / " AITL_BOARD_NAME, nullptr, nullptr);
  addAction(c, "Device ID", app::deviceId().c_str(), nullptr, nullptr);
  { String b = app::bridgeName.length() ? app::bridgeName + " " + app::bridgeVersion : String("none"); addAction(c, "Bridge", b.c_str(), nullptr, nullptr); }
  { String fp = bridge::bridgeCertFingerprint(); if (fp.length()) addAction(c, "Pinned cert", fp.substring(0, 23).c_str(), nullptr, nullptr); }
  { char h[32]; snprintf(h, sizeof(h), "%u KB free", (unsigned)(ESP.getFreeHeap() / 1024)); addAction(c, "Memory", h, nullptr, nullptr); }
  { uint32_t up = millis() / 1000; char u[32]; snprintf(u, sizeof(u), "%luh %02lum", (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60)); addAction(c, "Uptime", u, nullptr, nullptr); }
  if (bridge::wifiUp()) { char w[48]; snprintf(w, sizeof(w), "%s  %d dBm", bridge::ipAddress().c_str(), bridge::rssi()); addAction(c, "Wi-Fi", w, nullptr, nullptr); }
  addAction(c, "Run setup again", "", cbWizard, nullptr);
  addAction(c, "Restart", "", cbReboot, nullptr);
  addAction(c, "Factory reset", "", cbReset, nullptr);
      break; }
    default: break;
  }
}
static void evSection(lv_event_t* e) { s_section = (int)(uintptr_t)lv_event_get_user_data(e); showSettings(); }
static void evSectionsBack(lv_event_t*) { s_section = -1; showSettings(); }
void showSettingsRoot() { s_section = -1; showSettings(); }
void showSettingsSection(int i) { s_section = (i >= 0 && i < 8) ? i : -1; showSettings(); }
void showSettings() {
  lv_obj_t* scr = newScreen();
  if (s_section < 0) {
    statusBar(scr, "", "Settings", true);
    lv_obj_t* list = scrollList(scr);
    lv_obj_t* grid = lv_obj_create(list); lv_obj_remove_style_all(grid); lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP); lv_obj_set_style_pad_gap(grid, big() ? 10 : 6, 0);
    for (int i = 0; i < 8; i++) {
      lv_obj_t* b = lv_btn_create(grid); lv_obj_remove_style_all(b); lv_obj_add_style(b, &card, 0); lv_obj_add_style(b, &pressed, LV_STATE_PRESSED);
      lv_obj_set_width(b, big() ? LV_PCT(49) : LV_PCT(100)); lv_obj_set_height(b, LV_SIZE_CONTENT); lv_obj_set_style_pad_all(b, big() ? 12 : 10, 0);
      lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(b, big() ? 12 : 10, 0);
      lv_obj_t* ic = makeLabel(b, kSectionIcons[i], &body); lv_obj_set_style_text_color(ic, lv_color_hex(MU_TEAL), 0); lv_obj_set_style_text_font(ic, big() ? &lv_font_montserrat_20 : &lv_font_montserrat_16, 0);
      lv_obj_t* col = lv_obj_create(b); lv_obj_remove_style_all(col); lv_obj_set_flex_grow(col, 1); lv_obj_set_height(col, LV_SIZE_CONTENT); lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(col, 1, 0);
      lv_obj_t* t = makeLabel(col, kSectionNames[i], &body); lv_obj_set_width(t, LV_PCT(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
      lv_obj_t* sm = makeLabel(col, sectionSummary(i).c_str(), &caption); lv_obj_set_width(sm, LV_PCT(100)); lv_label_set_long_mode(sm, LV_LABEL_LONG_DOT);
      lv_obj_t* ch = makeLabel(b, LV_SYMBOL_RIGHT, &caption); lv_obj_set_style_text_color(ch, lv_color_hex(MU_DIM), 0);
      lv_obj_add_event_cb(b, evSection, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    }
    load(scr); return;
  }
  // one section: custom bar (back returns to the menu, not home)
  lv_obj_t* bar = lv_obj_create(scr); lv_obj_remove_style_all(bar); lv_obj_set_size(bar, LV_PCT(100), chipH() + 8);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(bar, 8, 0);
  lv_obj_t* bk = makeIconButton(bar, LV_SYMBOL_LEFT, chipH() + 8, evSectionsBack); lv_obj_set_ext_click_area(bk, 10);
  lv_obj_t* t = makeLabel(bar, kSectionNames[s_section], &title); lv_obj_set_style_text_font(t, big() ? &lv_font_montserrat_24 : &lv_font_montserrat_18, 0); lv_obj_set_flex_grow(t, 1);
  lv_obj_t* list = scrollList(scr);
  lv_obj_t* c = makeCard(list); lv_obj_set_style_pad_row(c, 0, 0); lv_obj_set_width(c, LV_PCT(100));
  buildSection(c, s_section);
  load(scr);
}

// ---------- touch calibration ----------
// Three targets in UI space (TL, TR, BL). Raw samples use identity mapping; swap / invert / scale / offset are solved
// so raw -> expected coordinates, then a verification screen shows a dot under the finger.
static void wizardContinue();
static bool s_calFromWizard = false; static int s_calStep = 0; static lv_obj_t* s_calScr = nullptr; static lv_obj_t* s_calTarget = nullptr; static lv_obj_t* s_calLabel = nullptr;
static int32_t s_rawX[3], s_rawY[3]; static lv_coord_t s_tgtX[3], s_tgtY[3]; static lv_timer_t* s_calTimer = nullptr; static bool s_wasTouched = false; static uint32_t s_touchStart = 0; static int32_t s_accX, s_accY, s_accN;
static lv_obj_t* s_verifyDot = nullptr;

static void calPlaceTarget() { lv_obj_set_pos(s_calTarget, s_tgtX[s_calStep] - 14, s_tgtY[s_calStep] - 14); lv_label_set_text_fmt(s_calLabel, "Touch calibration  %d of 3\nPress and hold the ring", s_calStep + 1); }
static void calFinish() {
  const int32_t W = displayW(), H = displayH();
  int32_t ex[3], ey[3]; for (int i = 0; i < 3; i++) { ex[i] = s_tgtX[i]; ey[i] = s_tgtY[i]; }
  int32_t rdx01 = s_rawX[1] - s_rawX[0], rdy01 = s_rawY[1] - s_rawY[0];
  int32_t edx01 = ex[1] - ex[0], edy01 = ey[1] - ey[0];
  bool expectXDominant = abs(edx01) > abs(edy01); bool rawXDominant = abs(rdx01) > abs(rdy01); bool swap = expectXDominant != rawXDominant;
  auto mapped = [&](int i, int32_t& mx, int32_t& my) { if (swap) { mx = (s_rawY[i] * (W - 1)) / (H - 1); my = (s_rawX[i] * (H - 1)) / (W - 1); } else { mx = s_rawX[i]; my = s_rawY[i]; } };
  int32_t m0x, m0y, m1x, m1y, m2x, m2y; mapped(0, m0x, m0y); mapped(1, m1x, m1y); mapped(2, m2x, m2y);
  bool invX, invY;
  if (expectXDominant) { invX = ((m1x - m0x) > 0) != ((ex[1] - ex[0]) > 0); invY = ((m2y - m0y) > 0) != ((ey[2] - ey[0]) > 0); }
  else { invX = ((m2x - m0x) > 0) != ((ex[2] - ex[0]) > 0); invY = ((m1y - m0y) > 0) != ((ey[1] - ey[0]) > 0); }
  auto full = [&](int i, int32_t& fx, int32_t& fy) { int32_t mx, my; mapped(i, mx, my); fx = invX ? (W - 1) - mx : mx; fy = invY ? (H - 1) - my : my; };
  int32_t f0x, f0y, f1x, f1y, f2x, f2y; full(0, f0x, f0y); full(1, f1x, f1y); full(2, f2x, f2y);
  int32_t mdx = expectXDominant ? (f1x - f0x) : (f2x - f0x), edx = expectXDominant ? (ex[1] - ex[0]) : (ex[2] - ex[0]);
  int32_t mdy = expectXDominant ? (f2y - f0y) : (f1y - f0y), edy = expectXDominant ? (ey[2] - ey[0]) : (ey[1] - ey[0]);
  int32_t scx = (abs(mdx) > 20) ? (edx * 1000) / mdx : 1000, scy = (abs(mdy) > 20) ? (edy * 1000) / mdy : 1000;
  scx = constrain(scx, 700, 1500); scy = constrain(scy, 700, 1500);
#if !defined(AITL_TOUCH_XPT2046)
  if (abs(scx - 1000) < 60) scx = 1000; if (abs(scy - 1000) < 60) scy = 1000;   // capacitive: ignore noise
#endif
  int32_t offx = 0, offy = 0; for (int i = 0; i < 3; i++) { int32_t fx, fy; full(i, fx, fy); offx += ex[i] - (fx * scx) / 1000; offy += ey[i] - (fy * scy) / 1000; }
  offx /= 3; offy /= 3; offx = constrain(offx, -60, 60); offy = constrain(offy, -60, 60);
  g_settings.touchSwapXY = swap; g_settings.touchInvX = invX; g_settings.touchInvY = invY; g_settings.touchOffX = offx; g_settings.touchOffY = offy; g_settings.touchScaleX = scx; g_settings.touchScaleY = scy;
  settingsSave(); displaySetCalibrating(false);
  bridge::log("touch cal: raw TL=(%d,%d) TR=(%d,%d) BL=(%d,%d) -> swap=%d invX=%d invY=%d scale=(%d,%d) off=(%d,%d)", (int)s_rawX[0], (int)s_rawY[0], (int)s_rawX[1], (int)s_rawY[1], (int)s_rawX[2], (int)s_rawY[2], swap, invX, invY, (int)scx, (int)scy, (int)offx, (int)offy);
  lv_obj_clean(s_calScr);
  lv_obj_t* l = makeLabel(s_calScr, "Check it\nDrag anywhere: the dot should sit under your finger.", &body); lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(l, fontSmall(), 0);
  lv_obj_t* r = lv_obj_create(s_calScr); lv_obj_remove_style_all(r); lv_obj_set_size(r, LV_PCT(100), btnH() + 4); lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(r, 6, 0);
  lv_obj_add_flag(r, LV_OBJ_FLAG_IGNORE_LAYOUT); lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_t* ok = makeButton(r, LV_SYMBOL_OK "  Looks right", true); lv_obj_set_flex_grow(ok, 1);
  lv_obj_add_event_cb(ok, [](lv_event_t*) { if (s_calTimer) { lv_timer_del(s_calTimer); s_calTimer = nullptr; } if (s_calFromWizard) wizardContinue(); else showSettings(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* redo = makeButton(r, LV_SYMBOL_REFRESH "  Redo"); lv_obj_set_flex_grow(redo, 1);
  lv_obj_add_event_cb(redo, [](lv_event_t*) { if (s_calTimer) { lv_timer_del(s_calTimer); s_calTimer = nullptr; } showCalibrate(s_calFromWizard); }, LV_EVENT_CLICKED, nullptr);
  s_verifyDot = lv_obj_create(s_calScr); lv_obj_remove_style_all(s_verifyDot); lv_obj_set_size(s_verifyDot, 16, 16); lv_obj_set_style_radius(s_verifyDot, 8, 0);
  gradient(s_verifyDot, false); lv_obj_add_flag(s_verifyDot, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
  s_calStep = 99;
}
static void calTick(lv_timer_t*) {
  TouchRaw r = displayReadTouchRaw();
  if (s_calStep == 99) { if (r.touched) { lv_indev_t* in = lv_indev_get_act(); lv_point_t p; if (in) { lv_indev_get_point(in, &p); lv_obj_clear_flag(s_verifyDot, LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(s_verifyDot, p.x - 8, p.y - 8); } } return; }
  if (r.touched) {
    if (!s_wasTouched) { s_touchStart = millis(); s_accX = s_accY = s_accN = 0; }
    s_wasTouched = true; s_accX += r.x; s_accY += r.y; s_accN++;
    uint32_t held = millis() - s_touchStart;
    lv_obj_set_style_bg_opa(s_calTarget, (lv_opa_t)(LV_OPA_20 + (held > 600 ? 200 : held / 3)), 0);
    if (held > 650 && s_accN > 5) { s_rawX[s_calStep] = s_accX / s_accN; s_rawY[s_calStep] = s_accY / s_accN; s_calStep++; s_wasTouched = false; s_accN = 0; if (s_calStep >= 3) calFinish(); else { calPlaceTarget(); lv_obj_set_style_bg_opa(s_calTarget, LV_OPA_20, 0); } }
  } else { s_wasTouched = false; lv_obj_set_style_bg_opa(s_calTarget, LV_OPA_20, 0); }
}
void showCalibrate(bool fromWizard) {
  s_calFromWizard = fromWizard; s_calStep = 0; s_wasTouched = false; displaySetCalibrating(true);
  s_calScr = lv_obj_create(nullptr); lv_obj_add_style(s_calScr, &screen, 0); lv_obj_clear_flag(s_calScr, LV_OBJ_FLAG_SCROLLABLE);
  lv_coord_t W = displayW(), H = displayH(); const lv_coord_t inset = big() ? 30 : 22;
  s_tgtX[0] = inset; s_tgtY[0] = inset; s_tgtX[1] = W - inset; s_tgtY[1] = inset; s_tgtX[2] = inset; s_tgtY[2] = H - inset;
  s_calLabel = makeLabel(s_calScr, "", &body); lv_obj_set_style_text_align(s_calLabel, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(s_calLabel);
  s_calTarget = lv_obj_create(s_calScr); lv_obj_remove_style_all(s_calTarget); lv_obj_set_size(s_calTarget, 28, 28); lv_obj_set_style_radius(s_calTarget, 14, 0);
  lv_obj_set_style_border_width(s_calTarget, 3, 0); lv_obj_set_style_border_color(s_calTarget, lv_color_hex(MU_TEAL), 0); gradient(s_calTarget, false); lv_obj_set_style_bg_opa(s_calTarget, LV_OPA_20, 0);
  calPlaceTarget();
  if (s_calTimer) lv_timer_del(s_calTimer);
  s_calTimer = lv_timer_create(calTick, 30, nullptr);
  load(s_calScr);
}

// ---------- first-boot setup: hello (tap) -> calibration -> orientation -> colors ----------
static int s_wiz = 0; static lv_obj_t* s_wizScr = nullptr;
static void wizRender();
static void wizNext(lv_event_t*) { s_wiz++; wizRender(); }
static void wizardContinue() { s_wiz = 2; wizRender(); }
static void wizRotate(lv_event_t*) { settingsRotate(); displayApplyPanel(); wizRender(); }
static void wizInvert(lv_event_t*) { g_settings.invertColors = !g_settings.invertColors; settingsSave(); displayApplyPanel(); wizRender(); }
static void wizBgr(lv_event_t*) { g_settings.bgrOrder = !g_settings.bgrOrder; settingsSave(); displayApplyPanel(); wizRender(); }
static lv_obj_t* swatch(lv_obj_t* parent, uint32_t color, const char* name) {
  lv_obj_t* c = lv_obj_create(parent); lv_obj_remove_style_all(c); lv_obj_set_size(c, 60, 42);
  lv_obj_set_style_bg_color(c, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0); lv_obj_set_style_radius(c, 10, 0);
  lv_obj_t* l = makeLabel(c, name, &caption); lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0); lv_obj_center(l); return c;
}
static lv_obj_t* wizStep(const char* step) {
  s_wizScr = newScreen(); lv_obj_set_flex_align(s_wizScr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_row(s_wizScr, big() ? 8 : 5, 0);
  lv_obj_t* h = makeLabel(s_wizScr, step, &caption); lv_obj_set_style_pad_top(h, 4, 0); return s_wizScr;
}
static void wizRender() {
  if (s_wiz == 1) { showCalibrate(true); return; }
  if (s_wiz >= 4) { g_settings.setupDone = true; settingsSave(); showHome(); toast("All set. Say hi."); return; }
  if (s_wiz == 0) {
    wizStep("Welcome");
    orb(s_wizScr, big() ? 64 : 44);
    lv_obj_t* t = makeLabel(s_wizScr, "Hi, I'm your Muse device.", &title); lv_obj_set_style_text_font(t, big() ? &lv_font_montserrat_20 : &lv_font_montserrat_18, 0);
    lv_obj_t* b = makeLabel(s_wizScr, "Keys stay on your Mac; I only hold a pairing token. Let's calibrate touch, then check orientation and colors.", &caption);
    lv_obj_set_width(b, LV_PCT(90)); lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* n = makeButton(s_wizScr, "Let's go  " LV_SYMBOL_RIGHT, true); lv_obj_set_size(n, big() ? 220 : 160, btnH()); lv_obj_add_event_cb(n, wizNext, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_wizScr, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(s_wizScr, wizNext, LV_EVENT_CLICKED, nullptr);
  } else if (s_wiz == 2) {
    wizStep("Step 2 of 3  " MU_BULLET "  Orientation");
    lv_obj_t* up = makeLabel(s_wizScr, LV_SYMBOL_UP "  This edge is up  " LV_SYMBOL_UP, &body); lv_obj_set_style_text_color(up, lv_color_hex(MU_TEAL), 0);
    lv_obj_t* b = makeLabel(s_wizScr, "Rotate until the text reads correctly and the arrows point to the top of the case.", &caption);
    lv_obj_set_width(b, LV_PCT(90)); lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* r = makeButton(s_wizScr, LV_SYMBOL_REFRESH "  Rotate"); lv_obj_set_size(r, big() ? 260 : 190, btnH()); lv_obj_add_event_cb(r, wizRotate, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* n = makeButton(s_wizScr, "Looks right  " LV_SYMBOL_RIGHT, true); lv_obj_set_size(n, big() ? 260 : 190, btnH()); lv_obj_add_event_cb(n, wizNext, LV_EVENT_CLICKED, nullptr);
  } else {
    wizStep("Step 3 of 3  " MU_BULLET "  Colors");
    makeLabel(s_wizScr, "Do these swatches match their names?", &body);
    lv_obj_t* row = lv_obj_create(s_wizScr); lv_obj_remove_style_all(row); lv_obj_set_size(row, LV_SIZE_CONTENT, 46); lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 6, 0);
    swatch(row, 0xff0000, "Red"); swatch(row, 0x00ff00, "Green"); swatch(row, 0x0000ff, "Blue"); swatch(row, 0xffffff, "White");
    lv_obj_t* r2 = lv_obj_create(s_wizScr); lv_obj_remove_style_all(r2); lv_obj_set_size(r2, LV_PCT(94), btnH() + 2); lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(r2, 6, 0);
    lv_obj_t* i = makeButton(r2, g_settings.invertColors ? "Invert: on" : "Invert: off"); lv_obj_set_flex_grow(i, 1); lv_obj_add_event_cb(i, wizInvert, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* g = makeButton(r2, g_settings.bgrOrder ? "Swap R/B: on" : "Swap R/B: off"); lv_obj_set_flex_grow(g, 1); lv_obj_add_event_cb(g, wizBgr, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* n = makeButton(s_wizScr, "Colors are right  " LV_SYMBOL_RIGHT, true); lv_obj_set_size(n, big() ? 260 : 200, btnH()); lv_obj_add_event_cb(n, wizNext, LV_EVENT_CLICKED, nullptr);
  }
  load(s_wizScr);
}
void showSetupWizard() { s_wiz = 0; wizRender(); }

// ---------- network pairing ----------
static void pairYes(lv_event_t*) { app::confirmPairing(true); showSettings(); }
static void pairNo(lv_event_t*) { app::confirmPairing(false); showSettings(); }
void showPairing() {
  lv_obj_t* scr = newScreen(); statusBar(scr, "", "Pair with the bridge", false);
  lv_obj_t* b = makeLabel(scr, "A bridge wants to pair. Confirm that both values match what musecorder serve printed on your Mac.", &caption);
  lv_obj_set_width(b, LV_PCT(100)); lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
  lv_obj_t* box = makeCard(scr);
  makeLabel(box, app::pairName.c_str(), &caption);
  lv_obj_t* code = makeLabel(box, app::pairCode.c_str(), &title); lv_obj_set_style_text_font(code, &lv_font_montserrat_28, 0); lv_obj_set_style_text_color(code, lv_color_hex(MU_TEAL), 0);
  lv_obj_t* fp = makeLabel(box, app::pairFingerprint.c_str(), &caption); lv_obj_set_width(fp, LV_PCT(100)); lv_label_set_long_mode(fp, LV_LABEL_LONG_WRAP);
  lv_obj_t* r = lv_obj_create(scr); lv_obj_remove_style_all(r); lv_obj_set_size(r, LV_PCT(100), btnH() + 2); lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(r, 6, 0);
  lv_obj_t* y = makeButton(r, LV_SYMBOL_OK "  They match", true); lv_obj_set_flex_grow(y, 1); lv_obj_add_event_cb(y, pairYes, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* no = makeButton(r, LV_SYMBOL_CLOSE "  Reject"); lv_obj_set_flex_grow(no, 1); lv_obj_add_event_cb(no, pairNo, LV_EVENT_CLICKED, nullptr);
  load(scr);
}

// ---------- mic test ----------
static lv_obj_t* s_micBar = nullptr; static lv_obj_t* s_micLbl = nullptr; static lv_timer_t* s_micTimer = nullptr;
static void micTick(lv_timer_t*) { uint8_t lv = mic::level(); if (s_micBar) lv_bar_set_value(s_micBar, lv, LV_ANIM_OFF); if (s_micLbl) lv_label_set_text_fmt(s_micLbl, "Level %u%%  %s", lv, lv > 10 ? "signal" : lv > 2 ? "noise floor" : "silent (no mic? SCK 22, WS 21, SD 35)"); }
static void micLeave(lv_event_t*) { if (s_micTimer) { lv_timer_del(s_micTimer); s_micTimer = nullptr; } mic::stopMonitor(); s_micBar = nullptr; s_micLbl = nullptr; showSettings(); }
static void cbMicTest(lv_event_t*) {
  lv_obj_t* scr = newScreen(); statusBar(scr, "", "Mic test", false);
  lv_obj_t* b = makeLabel(scr, mic::begin() ? "Speak. The bar should move with your voice." : "The I2S driver failed to start.", &body); lv_obj_set_width(b, LV_PCT(100)); lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
  s_micBar = lv_bar_create(scr); lv_obj_set_size(s_micBar, LV_PCT(100), 18); lv_bar_set_range(s_micBar, 0, 100); lv_obj_set_style_radius(s_micBar, 9, 0); lv_obj_set_style_radius(s_micBar, 9, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_micBar, lv_color_hex(MU_SURFACE2), 0); lv_obj_set_style_bg_color(s_micBar, lv_color_hex(MU_TEAL), LV_PART_INDICATOR); lv_obj_set_style_bg_grad_color(s_micBar, lv_color_hex(MU_MAGENTA), LV_PART_INDICATOR); lv_obj_set_style_bg_grad_dir(s_micBar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  s_micLbl = makeLabel(scr, "", &caption);
  lv_obj_t* hint = makeLabel(scr, "INMP441: VDD 3V3, GND, SCK IO22, WS IO21, SD IO35, L/R GND", &caption); lv_obj_set_style_text_color(hint, lv_color_hex(MU_DIM), 0); lv_obj_set_width(hint, LV_PCT(100)); lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_t* back = makeButton(scr, LV_SYMBOL_LEFT "  Done", true); lv_obj_set_size(back, big() ? 220 : 150, btnH()); lv_obj_add_event_cb(back, micLeave, LV_EVENT_CLICKED, nullptr);
  mic::startMonitor(); if (s_micTimer) lv_timer_del(s_micTimer); s_micTimer = lv_timer_create(micTick, 60, nullptr);
  load(scr);
}

// ---------- touch test: draw with a finger, see raw and mapped coordinates ----------
static lv_obj_t* s_ttScr = nullptr; static lv_obj_t* s_ttLbl = nullptr; static lv_obj_t* s_ttCross = nullptr; static lv_timer_t* s_ttTimer = nullptr; static int s_ttDots = 0;
static void ttTick(lv_timer_t*) {
  lv_indev_t* in = lv_indev_get_act(); if (!in || !s_ttScr) return;
  TouchRaw r = displayReadTouchRaw(); if (!r.touched) return;
  lv_point_t p; lv_indev_get_point(in, &p);
  lv_label_set_text_fmt(s_ttLbl, "mapped %d,%d   raw %u,%u   %d dots", (int)p.x, (int)p.y, (unsigned)r.x, (unsigned)r.y, s_ttDots);
  lv_obj_clear_flag(s_ttCross, LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(s_ttCross, p.x - 10, p.y - 10);
  if (s_ttDots < 400) {
    lv_obj_t* d = lv_obj_create(s_ttScr); lv_obj_remove_style_all(d); lv_obj_set_size(d, 4, 4); lv_obj_set_style_radius(d, 2, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(MU_TEAL), 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0); lv_obj_add_flag(d, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(d, p.x - 2, p.y - 2); lv_obj_move_background(d); s_ttDots++;
  }
}
static void ttLeave(lv_event_t*) { if (s_ttTimer) { lv_timer_del(s_ttTimer); s_ttTimer = nullptr; } s_ttScr = nullptr; showSettings(); }
static void ttClear(lv_event_t*) { showTouchTest(); }
void showTouchTest() {
  if (s_ttTimer) { lv_timer_del(s_ttTimer); s_ttTimer = nullptr; }
  s_ttDots = 0;
  s_ttScr = lv_obj_create(nullptr); lv_obj_add_style(s_ttScr, &screen, 0); lv_obj_clear_flag(s_ttScr, LV_OBJ_FLAG_SCROLLABLE);
  lv_coord_t W = displayW(), H = displayH();
  for (int i = 1; i < 4; i++) {
    lv_obj_t* v = lv_obj_create(s_ttScr); lv_obj_remove_style_all(v); lv_obj_set_size(v, 1, H); lv_obj_set_pos(v, W * i / 4, 0); lv_obj_set_style_bg_color(v, lv_color_hex(MU_LINE), 0); lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_t* h = lv_obj_create(s_ttScr); lv_obj_remove_style_all(h); lv_obj_set_size(h, W, 1); lv_obj_set_pos(h, 0, H * i / 4); lv_obj_set_style_bg_color(h, lv_color_hex(MU_LINE), 0); lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
  }
  s_ttLbl = makeLabel(s_ttScr, "Draw anywhere. The line should follow your finger exactly.", &caption); lv_obj_set_pos(s_ttLbl, 6, 6); lv_obj_set_width(s_ttLbl, W - 12); lv_label_set_long_mode(s_ttLbl, LV_LABEL_LONG_WRAP);
  s_ttCross = lv_obj_create(s_ttScr); lv_obj_remove_style_all(s_ttCross); lv_obj_set_size(s_ttCross, 20, 20); lv_obj_set_style_radius(s_ttCross, 10, 0);
  lv_obj_set_style_border_width(s_ttCross, 2, 0); lv_obj_set_style_border_color(s_ttCross, lv_color_hex(MU_MAGENTA), 0); lv_obj_add_flag(s_ttCross, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_t* r = lv_obj_create(s_ttScr); lv_obj_remove_style_all(r); lv_obj_set_size(r, W - 12, btnH()); lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(r, 6, 0); lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_t* cal = makeButton(r, "Recalibrate"); lv_obj_set_flex_grow(cal, 1); lv_obj_add_event_cb(cal, [](lv_event_t*) { if (s_ttTimer) { lv_timer_del(s_ttTimer); s_ttTimer = nullptr; } s_ttScr = nullptr; showCalibrate(false); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* clr = makeButton(r, "Clear"); lv_obj_set_flex_grow(clr, 1); lv_obj_add_event_cb(clr, ttClear, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* back = makeButton(r, LV_SYMBOL_OK "  Done", true); lv_obj_set_flex_grow(back, 1); lv_obj_add_event_cb(back, ttLeave, LV_EVENT_CLICKED, nullptr);
  s_ttTimer = lv_timer_create(ttTick, 25, nullptr);
  load(s_ttScr);
}
}  // namespace ui
