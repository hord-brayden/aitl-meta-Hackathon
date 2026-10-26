// Settings, first-boot wizard, touch calibration, network pairing.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/settings.h"
#include "../hal/display.h"
#include "../hal/sdcard.h"
#include "../link/link.h"
#include "../ble/ble_hid.h"
#include "../hal/mic.h"

using namespace theme; using namespace ui_int;
namespace ui {

// ---------- settings callbacks ----------
static void cbBright(lv_event_t* e) { g_settings.brightness = lv_slider_get_value(lv_event_get_target(e)); displayApplyBrightness(); }
static void cbTimeout(lv_event_t* e) { static const uint16_t v[] = {10, 30, 60, 120, 300, 600, 0}; g_settings.screenTimeout = v[lv_dropdown_get_selected(lv_event_get_target(e))]; settingsSave(); displayWake(); }
static void cbRotate(lv_event_t* e) { settingsSetRotation(lv_dropdown_get_selected(lv_event_get_target(e))); displayApplyPanel(); showSettings(); }
static void cbInvert(lv_event_t* e) { g_settings.invertColors = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); displayApplyPanel(); }
static void cbBgr(lv_event_t* e) { g_settings.bgrOrder = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); displayApplyPanel(); }
static void cbCal(lv_event_t*) { showCalibrate(false); }
static void cbLongPress(lv_event_t* e) { static const uint16_t v[] = {250, 400, 600, 900}; g_settings.longPressMs = v[lv_dropdown_get_selected(lv_event_get_target(e))]; settingsSave(); }
static void cbDebounce(lv_event_t* e) { g_settings.touchDebounce = 1 + lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void cbTapLed(lv_event_t* e) { g_settings.hapticLed = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); }
static void cbSdLog(lv_event_t* e) { g_settings.sdLog = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); }
static void cbWifi(lv_event_t* e) { g_settings.wifiEnabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); toast(g_settings.wifiEnabled ? "Wi-Fi on: restarting (Bluetooth parks while Wi-Fi is on)" : "Wi-Fi off: restarting"); lv_timer_handler(); delay(900); ESP.restart(); }
static void cbBle(lv_event_t* e) { g_settings.bleEnabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); if (g_settings.bleEnabled && !g_settings.wifiEnabled) blehid::begin(); else blehid::end(); toast(!g_settings.bleEnabled ? "Bluetooth off" : g_settings.wifiEnabled ? "Saved. Bluetooth starts when Wi-Fi is off (not enough RAM for both)." : "Advertising as 'AITL Keyboard'"); }
static void cbHidTarget(lv_event_t* e) { g_settings.hidTarget = lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void cbDictSrc(lv_event_t* e) { g_settings.dictSource = lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void cbDictTgt(lv_event_t* e) { g_settings.dictTarget = lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void cbMicGain(lv_event_t* e) { g_settings.micGain = lv_slider_get_value(lv_event_get_target(e)); settingsSave(); }
static void cbBleLink(lv_event_t* e) { g_settings.bleLink = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED); settingsSave(); if (g_settings.bleEnabled) { blehid::end(); blehid::begin(); } toast(g_settings.bleLink ? "BLE link on: run aitl serve --ble" : "BLE link off"); }
static void cbMicTest(lv_event_t*);
static void cbDictSc(lv_event_t* e) { g_settings.dictShortcut = lv_dropdown_get_selected(lv_event_get_target(e)); settingsSave(); }
static void ssidDone(const char* v) { strlcpy(g_settings.wifiSsid, v, sizeof(g_settings.wifiSsid)); settingsSave(); showTextEntry("WI-FI PASSWORD", g_settings.wifiPass, true, [](const char* p) { strlcpy(g_settings.wifiPass, p, sizeof(g_settings.wifiPass)); g_settings.wifiEnabled = true; settingsSave(); bridge::wifiApply(); showSettings(); }); }
static void cbSsid(lv_event_t*) { showTextEntry("WI-FI NETWORK NAME", g_settings.wifiSsid, false, ssidDone); }
static void hostDone(const char* v) { strlcpy(g_settings.bridgeHost, v, sizeof(g_settings.bridgeHost)); settingsSave(); showTextEntry("BRIDGE PORT", String(g_settings.bridgePort).c_str(), false, [](const char* p) { int port = atoi(p); if (port > 0 && port < 65536) g_settings.bridgePort = port; settingsSave(); showSettings(); }); }
static void cbHost(lv_event_t*) { showTextEntry("BRIDGE HOST (mac ip or name)", g_settings.bridgeHost, false, hostDone); }
static void cbPair(lv_event_t*) { app::startNetworkPairing(); }
static void doForget() { app::forgetBridge(); showSettings(); }
static void cbForget(lv_event_t*) { confirm("Forget bridge?", "Removes the pinned certificate and token.", doForget); }
static void doReset() { settingsReset(); bridge::storeBridgeCert(""); delay(200); ESP.restart(); }
static void cbReset(lv_event_t*) { confirm("Factory reset?", "All settings, Wi-Fi and pairing are erased.", doReset); }
static void cbReboot(lv_event_t*) { ESP.restart(); }
static void cbWizard(lv_event_t*) { showSetupWizard(); }

void showSettings() {
  lv_obj_t* scr = newScreen(); statusBar(scr, "06", "SETUP", true);
  lv_obj_t* list = lv_obj_create(scr); lv_obj_remove_style_all(list); lv_obj_set_width(list, LV_PCT(100)); lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(list, 3, 0); lv_obj_set_scroll_dir(list, LV_DIR_VER); lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

  sectionLabel(list, "CONNECTION");
  addAction(list, "USB link", app::bridgeLinked() && app::linkSummary() == "USB" ? app::bridgeName.c_str() : "plug in + run aitl serve", nullptr, nullptr);
  addSwitch(list, "Wi-Fi", g_settings.wifiEnabled, cbWifi, nullptr);
  addAction(list, "Network", bridge::wifiStatus().c_str(), cbSsid, nullptr);
  { String h = g_settings.bridgeHost[0] ? String(g_settings.bridgeHost) + ":" + g_settings.bridgePort : String("not set"); addAction(list, "Bridge host", h.c_str(), cbHost, nullptr); }
  addAction(list, "Bridge", bridge::bridgeStatus().c_str(), nullptr, nullptr);
  addAction(list, "Pair over network", bridge::hasBridgeCert() ? "paired (re-pair)" : "start", cbPair, nullptr);
  if (bridge::hasBridgeCert()) addAction(list, "Forget bridge", "", cbForget, nullptr);
  addSwitch(list, "Bluetooth keyboard", g_settings.bleEnabled, cbBle, nullptr);
  addAction(list, "BLE status", blehid::status().c_str(), nullptr, nullptr);
  addDropdown(list, "Keystrokes via", "Bridge (USB/Wi-Fi)\nBluetooth HID", g_settings.hidTarget, cbHidTarget, nullptr);
  addDropdown(list, "BLE dictation key", "Right Cmd x2\nCtrl x2\nF5\nCmd+Opt+D", g_settings.dictShortcut, cbDictSc, nullptr);
  addSwitch(list, "Bluetooth link (bridge over BLE)", g_settings.bleLink, cbBleLink, nullptr);

  sectionLabel(list, "DICTATION");
  addDropdown(list, "Microphone", "Host Mac\nDevice mic (P3)", g_settings.dictSource, cbDictSrc, nullptr);
  addDropdown(list, "Device mic sends to", "Ask the AI\nType into Mac", g_settings.dictTarget, cbDictTgt, nullptr);
  addSlider(list, "Mic gain", 1, 8, g_settings.micGain, cbMicGain, nullptr);
  addAction(list, "Mic test (level meter)", "", cbMicTest, nullptr);

  sectionLabel(list, "DISPLAY");
  addSlider(list, "Brightness", 10, 255, g_settings.brightness, cbBright, nullptr);
  { static const uint16_t v[] = {10, 30, 60, 120, 300, 600, 0}; uint16_t sel = 3; for (int i = 0; i < 7; i++) if (v[i] == g_settings.screenTimeout) sel = i;
    addDropdown(list, "Screen dims after", "10 s\n30 s\n1 min\n2 min\n5 min\n10 min\nNever (always on)", sel, cbTimeout, nullptr); }
  addDropdown(list, "Rotation", "Portrait\nLandscape A\nPortrait flipped\nLandscape B (SD slot up)", g_settings.rotation & 3, cbRotate, nullptr);
  addSwitch(list, "Invert colors", g_settings.invertColors, cbInvert, nullptr);
  addSwitch(list, "Swap red/blue", g_settings.bgrOrder, cbBgr, nullptr);

  sectionLabel(list, "TOUCH");
  addAction(list, "Calibrate touch", "", cbCal, nullptr);
  { static const uint16_t v[] = {250, 400, 600, 900}; uint16_t sel = 1; for (int i = 0; i < 4; i++) if (v[i] == g_settings.longPressMs) sel = i;
    addDropdown(list, "Long-press time", "250 ms\n400 ms\n600 ms\n900 ms", sel, cbLongPress, nullptr); }
  addDropdown(list, "Touch sensitivity", "Instant\nNormal\nFirm\nDeliberate", g_settings.touchDebounce - 1, cbDebounce, nullptr);
  addSwitch(list, "LED flash on tap", g_settings.hapticLed, cbTapLed, nullptr);

  sectionLabel(list, "STORAGE");
  addAction(list, "microSD", sdInfo().c_str(), nullptr, nullptr);
  addSwitch(list, "Log chats to microSD", g_settings.sdLog, cbSdLog, nullptr);

  sectionLabel(list, "ABOUT");
  addAction(list, "Firmware", "AITL " AITL_FW_VERSION " / " TFT_PANEL_NAME, nullptr, nullptr);
  addAction(list, "Device ID", app::deviceId().c_str(), nullptr, nullptr);
  { String b = app::bridgeName.length() ? app::bridgeName + " " + app::bridgeVersion : String("none"); addAction(list, "Bridge", b.c_str(), nullptr, nullptr); }
  { String fp = bridge::bridgeCertFingerprint(); if (fp.length()) addAction(list, "Pinned cert", fp.substring(0, 23).c_str(), nullptr, nullptr); }
  { char h[32]; snprintf(h, sizeof(h), "%u KB free", (unsigned)(ESP.getFreeHeap() / 1024)); addAction(list, "Memory", h, nullptr, nullptr); }
  addAction(list, "Run setup wizard", "", cbWizard, nullptr);
  addAction(list, "Reboot", "", cbReboot, nullptr);
  addAction(list, "Factory reset", "", cbReset, nullptr);
  lv_obj_t* foot = makeLabel(list, AITL_ORG "  //  " AITL_MOTTO, &mono); lv_obj_set_style_text_color(foot, lv_color_hex(CO_MUTE), 0); lv_obj_set_style_pad_ver(foot, 8, 0);
  load(scr);
}

// ---------- touch calibration ----------
// Three targets in UI space (TL, TR, BL). Raw samples are taken with identity mapping, then
// swap / invert / offset are solved so raw -> expected native coordinates.
static void wizardContinue();
static bool s_calFromWizard = false; static int s_calStep = 0; static lv_obj_t* s_calScr = nullptr; static lv_obj_t* s_calTarget = nullptr; static lv_obj_t* s_calLabel = nullptr;
static int32_t s_rawX[3], s_rawY[3]; static lv_coord_t s_tgtX[3], s_tgtY[3]; static lv_timer_t* s_calTimer = nullptr; static bool s_wasTouched = false; static uint32_t s_touchStart = 0; static int32_t s_accX, s_accY, s_accN;
static lv_obj_t* s_verifyDot = nullptr;

static void calPlaceTarget() {
  lv_obj_set_pos(s_calTarget, s_tgtX[s_calStep] - 12, s_tgtY[s_calStep] - 12);
  lv_label_set_text_fmt(s_calLabel, "TOUCH CALIBRATION  %d/3\nPress and hold the target", s_calStep + 1);
}
static void calFinish() {
  const int32_t W = displayW(), H = displayH();
  int32_t ex[3], ey[3]; for (int i = 0; i < 3; i++) { ex[i] = s_tgtX[i]; ey[i] = s_tgtY[i]; }
  // Pair 0->1 is horizontal in UI; 0->2 vertical. Determine which raw axis moved for each.
  int32_t rdx01 = s_rawX[1] - s_rawX[0], rdy01 = s_rawY[1] - s_rawY[0];
  int32_t edx01 = ex[1] - ex[0], edy01 = ey[1] - ey[0];
  bool expectXDominant = abs(edx01) > abs(edy01);
  bool rawXDominant = abs(rdx01) > abs(rdy01);
  bool swap = expectXDominant != rawXDominant;
  auto mapped = [&](int i, int32_t& mx, int32_t& my) {
    if (swap) { mx = (s_rawY[i] * (W - 1)) / (H - 1); my = (s_rawX[i] * (H - 1)) / (W - 1); } else { mx = s_rawX[i]; my = s_rawY[i]; } };
  int32_t m0x, m0y, m1x, m1y, m2x, m2y; mapped(0, m0x, m0y); mapped(1, m1x, m1y); mapped(2, m2x, m2y);
  // sign check on the axis each pair is expected to move along
  bool invX, invY;
  if (expectXDominant) { invX = ((m1x - m0x) > 0) != ((ex[1] - ex[0]) > 0); invY = ((m2y - m0y) > 0) != ((ey[2] - ey[0]) > 0); }
  else { invX = ((m2x - m0x) > 0) != ((ex[2] - ex[0]) > 0); invY = ((m1y - m0y) > 0) != ((ey[1] - ey[0]) > 0); }
  auto full = [&](int i, int32_t& fx, int32_t& fy) { int32_t mx, my; mapped(i, mx, my); fx = invX ? (W - 1) - mx : mx; fy = invY ? (H - 1) - my : my; };
  // scale from the two spans (TL->TR along x, TL->BL along y), then offset from the mean residual
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
  g_settings.touchSwapXY = swap; g_settings.touchInvX = invX; g_settings.touchInvY = invY; g_settings.touchOffX = offx; g_settings.touchOffY = offy;
  g_settings.touchScaleX = scx; g_settings.touchScaleY = scy;
  settingsSave(); displaySetCalibrating(false);
  bridge::log("touch cal: raw TL=(%d,%d) TR=(%d,%d) BL=(%d,%d) -> swap=%d invX=%d invY=%d scale=(%d,%d) off=(%d,%d)", (int)s_rawX[0], (int)s_rawY[0], (int)s_rawX[1], (int)s_rawY[1], (int)s_rawX[2], (int)s_rawY[2], swap, invX, invY, (int)scx, (int)scy, (int)offx, (int)offy);
  // verification screen
  lv_obj_clean(s_calScr);
  lv_obj_t* l = makeLabel(s_calScr, "VERIFY\nDrag anywhere: the dot should sit under your finger.", &body); lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
  lv_obj_t* row = lv_obj_create(s_calScr); lv_obj_remove_style_all(row); lv_obj_set_size(row, LV_PCT(100), btnH() + 4); lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_add_flag(row, LV_OBJ_FLAG_IGNORE_LAYOUT); lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_t* ok = makeButton(row, LV_SYMBOL_OK " Looks right", true); lv_obj_set_flex_grow(ok, 1);
  lv_obj_add_event_cb(ok, [](lv_event_t*) { if (s_calTimer) { lv_timer_del(s_calTimer); s_calTimer = nullptr; } if (s_calFromWizard) wizardContinue(); else showSettings(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* redo = makeButton(row, LV_SYMBOL_REFRESH " Redo"); lv_obj_set_flex_grow(redo, 1);
  lv_obj_add_event_cb(redo, [](lv_event_t*) { if (s_calTimer) { lv_timer_del(s_calTimer); s_calTimer = nullptr; } showCalibrate(s_calFromWizard); }, LV_EVENT_CLICKED, nullptr);
  s_verifyDot = lv_obj_create(s_calScr); lv_obj_remove_style_all(s_verifyDot); lv_obj_set_size(s_verifyDot, 14, 14); lv_obj_set_style_radius(s_verifyDot, 7, 0);
  lv_obj_set_style_bg_color(s_verifyDot, lv_color_hex(CO_AMBER), 0); lv_obj_set_style_bg_opa(s_verifyDot, LV_OPA_COVER, 0); lv_obj_add_flag(s_verifyDot, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
  s_calStep = 99;
}
static void calTick(lv_timer_t*) {
  TouchRaw r = displayReadTouchRaw();
  if (s_calStep == 99) {  // verify mode: show mapped point
    if (r.touched) { lv_indev_t* in = lv_indev_get_act(); lv_point_t p; if (in) { lv_indev_get_point(in, &p); lv_obj_clear_flag(s_verifyDot, LV_OBJ_FLAG_HIDDEN); lv_obj_set_pos(s_verifyDot, p.x - 7, p.y - 7); } }
    return;
  }
  if (r.touched) {
    if (!s_wasTouched) { s_touchStart = millis(); s_accX = s_accY = s_accN = 0; }
    s_wasTouched = true; s_accX += r.x; s_accY += r.y; s_accN++;
    uint32_t held = millis() - s_touchStart;
    lv_obj_set_style_bg_opa(s_calTarget, (lv_opa_t)(LV_OPA_30 + (held > 600 ? 200 : held / 3)), 0);
    if (held > 650 && s_accN > 5) {
      s_rawX[s_calStep] = s_accX / s_accN; s_rawY[s_calStep] = s_accY / s_accN;
      s_calStep++; s_wasTouched = false; s_accN = 0;
      if (s_calStep >= 3) calFinish(); else { calPlaceTarget(); lv_obj_set_style_bg_opa(s_calTarget, LV_OPA_30, 0); }
    }
  } else { s_wasTouched = false; lv_obj_set_style_bg_opa(s_calTarget, LV_OPA_30, 0); }
}
void showCalibrate(bool fromWizard) {
  s_calFromWizard = fromWizard; s_calStep = 0; s_wasTouched = false;
  displaySetCalibrating(true);
  s_calScr = lv_obj_create(nullptr); lv_obj_add_style(s_calScr, &screen, 0); lv_obj_clear_flag(s_calScr, LV_OBJ_FLAG_SCROLLABLE);
  lv_coord_t W = displayW(), H = displayH(); const lv_coord_t inset = big() ? 30 : 22;
  s_tgtX[0] = inset; s_tgtY[0] = inset; s_tgtX[1] = W - inset; s_tgtY[1] = inset; s_tgtX[2] = inset; s_tgtY[2] = H - inset;
  s_calLabel = makeLabel(s_calScr, "", &mono); lv_obj_set_style_text_align(s_calLabel, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(s_calLabel);
  s_calTarget = lv_obj_create(s_calScr); lv_obj_remove_style_all(s_calTarget); lv_obj_set_size(s_calTarget, 24, 24); lv_obj_set_style_radius(s_calTarget, 12, 0);
  lv_obj_set_style_border_width(s_calTarget, 2, 0); lv_obj_set_style_border_color(s_calTarget, lv_color_hex(CO_CAERULEI), 0);
  lv_obj_set_style_bg_color(s_calTarget, lv_color_hex(CO_CAERULEI), 0); lv_obj_set_style_bg_opa(s_calTarget, LV_OPA_30, 0);
  calPlaceTarget();
  if (s_calTimer) lv_timer_del(s_calTimer);
  s_calTimer = lv_timer_create(calTick, 30, nullptr);
  load(s_calScr);
}

// ---------- first-boot wizard: welcome (tap anywhere) -> touch calibration -> orientation -> colors ----------
static int s_wiz = 0; static lv_obj_t* s_wizScr = nullptr;
static void wizRender();
static void wizNext(lv_event_t*) { s_wiz++; wizRender(); }
static void wizardContinue() { s_wiz = 2; wizRender(); }
static void wizRotate(lv_event_t*) { settingsRotate(); displayApplyPanel(); wizRender(); }
static void wizInvert(lv_event_t*) { g_settings.invertColors = !g_settings.invertColors; settingsSave(); displayApplyPanel(); wizRender(); }
static void wizBgr(lv_event_t*) { g_settings.bgrOrder = !g_settings.bgrOrder; settingsSave(); displayApplyPanel(); wizRender(); }
static lv_obj_t* swatch(lv_obj_t* parent, uint32_t color, const char* name) {
  lv_obj_t* c = lv_obj_create(parent); lv_obj_remove_style_all(c); lv_obj_set_size(c, 60, 44);
  lv_obj_set_style_bg_color(c, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0); lv_obj_set_style_radius(c, 4, 0);
  lv_obj_t* l = makeLabel(c, name, &mono); lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0); lv_obj_center(l); return c;
}
static void wizRender() {
  if (s_wiz == 1) { showCalibrate(true); return; }
  if (s_wiz >= 4) { g_settings.setupDone = true; settingsSave(); showHome(); toast("Setup complete. Ex aperto, claritas."); return; }
  s_wizScr = newScreen(); lv_obj_set_flex_align(s_wizScr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  char hdr[16]; snprintf(hdr, sizeof(hdr), "SETUP %d/3", s_wiz == 0 ? 1 : s_wiz);
  lv_obj_t* h = makeLabel(s_wizScr, hdr, &mono); lv_obj_set_style_pad_top(h, 6, 0);
  if (s_wiz == 0) {
    lv_obj_t* t = makeLabel(s_wizScr, "Welcome to AITL", &title);
    (void)t;
    lv_obj_t* b = makeLabel(s_wizScr, "Your AI companion. Keys stay on your Mac; this device only ever holds a pairing token.\n\nFirst we calibrate touch, then orientation and colors.", &body);
    lv_obj_set_width(b, LV_PCT(92)); lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(b, &lv_font_montserrat_12, 0);
    lv_obj_t* n = makeLabel(s_wizScr, LV_SYMBOL_RIGHT "  TAP ANYWHERE TO BEGIN  " LV_SYMBOL_LEFT, &monoBig); lv_obj_set_style_text_color(n, lv_color_hex(CO_AMBER), 0); lv_obj_set_style_pad_top(n, 14, 0);
    lv_obj_add_flag(s_wizScr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wizScr, wizNext, LV_EVENT_CLICKED, nullptr);
  } else if (s_wiz == 2) {
    lv_obj_t* up = makeLabel(s_wizScr, LV_SYMBOL_UP "  THIS EDGE IS UP  " LV_SYMBOL_UP, &monoBig); lv_obj_set_style_text_color(up, lv_color_hex(CO_AMBER), 0);
    lv_obj_t* b = makeLabel(s_wizScr, "Rotate until the text reads correctly and the arrows point to the top of the case.", &body);
    lv_obj_set_width(b, LV_PCT(92)); lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_style_text_font(b, &lv_font_montserrat_12, 0);
    lv_obj_t* r = makeButton(s_wizScr, LV_SYMBOL_REFRESH " Rotate"); lv_obj_set_size(r, big() ? 300 : 200, btnH() + 4); lv_obj_add_event_cb(r, wizRotate, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* n = makeButton(s_wizScr, "Looks right " LV_SYMBOL_RIGHT, true); lv_obj_set_size(n, big() ? 300 : 200, btnH() + 4); lv_obj_add_event_cb(n, wizNext, LV_EVENT_CLICKED, nullptr);
  } else {
    lv_obj_t* b = makeLabel(s_wizScr, "Do these swatches match their names?", &body); lv_obj_set_style_text_font(b, &lv_font_montserrat_12, 0);
    lv_obj_t* row = lv_obj_create(s_wizScr); lv_obj_remove_style_all(row); lv_obj_set_size(row, LV_SIZE_CONTENT, 48); lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 6, 0);
    swatch(row, 0xff0000, "RED"); swatch(row, 0x00ff00, "GREEN"); swatch(row, 0x0000ff, "BLUE"); swatch(row, 0xffffff, "WHITE");
    lv_obj_t* r2 = lv_obj_create(s_wizScr); lv_obj_remove_style_all(r2); lv_obj_set_size(r2, LV_PCT(96), 40); lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(r2, 4, 0);
    lv_obj_t* i = makeButton(r2, g_settings.invertColors ? "Invert: ON" : "Invert: off"); lv_obj_set_flex_grow(i, 1); lv_obj_set_height(i, btnH() + 4); lv_obj_add_event_cb(i, wizInvert, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* g = makeButton(r2, g_settings.bgrOrder ? "R/B swap: ON" : "R/B swap: off"); lv_obj_set_flex_grow(g, 1); lv_obj_set_height(g, btnH() + 4); lv_obj_add_event_cb(g, wizBgr, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* n = makeButton(s_wizScr, "Colors correct " LV_SYMBOL_RIGHT, true); lv_obj_set_size(n, big() ? 300 : 200, btnH() + 4); lv_obj_add_event_cb(n, wizNext, LV_EVENT_CLICKED, nullptr);
  }
  load(s_wizScr);
}
void showSetupWizard() { s_wiz = 0; wizRender(); }

// ---------- network pairing ----------
static void pairYes(lv_event_t*) { app::confirmPairing(true); showSettings(); }
static void pairNo(lv_event_t*) { app::confirmPairing(false); showSettings(); }
void showPairing() {
  lv_obj_t* scr = newScreen(); statusBar(scr, "07", "PAIR BRIDGE", false);
  lv_obj_t* b = makeLabel(scr, "A bridge wants to pair. Confirm that BOTH values match what `aitl serve` printed on your Mac:", &body);
  lv_obj_set_width(b, LV_PCT(100)); lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_font(b, &lv_font_montserrat_12, 0);
  lv_obj_t* box = lv_obj_create(scr); lv_obj_remove_style_all(box); lv_obj_add_style(box, &panel, 0); lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_t* n = makeLabel(box, app::pairName.c_str(), &mono); lv_obj_set_style_text_color(n, lv_color_hex(CO_ASH), 0);
  lv_obj_t* code = makeLabel(box, app::pairCode.c_str(), &monoBig); lv_obj_set_style_text_color(code, lv_color_hex(CO_AMBER), 0);
  lv_obj_t* fp = makeLabel(box, app::pairFingerprint.c_str(), &mono); lv_obj_set_width(fp, LV_PCT(100)); lv_label_set_long_mode(fp, LV_LABEL_LONG_WRAP);
  lv_obj_t* row = lv_obj_create(scr); lv_obj_remove_style_all(row); lv_obj_set_size(row, LV_PCT(100), btnH() + 4); lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_t* y = makeButton(row, LV_SYMBOL_OK " Match, pair", true); lv_obj_set_flex_grow(y, 1); lv_obj_add_event_cb(y, pairYes, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* no = makeButton(row, LV_SYMBOL_CLOSE " Reject"); lv_obj_set_flex_grow(no, 1); lv_obj_add_event_cb(no, pairNo, LV_EVENT_CLICKED, nullptr);
  load(scr);
}
}  // namespace ui

// ---------- mic test: live level meter to verify the soldered I2S mic ----------
namespace ui {
static lv_obj_t* s_micBar = nullptr; static lv_obj_t* s_micLbl = nullptr; static lv_timer_t* s_micTimer = nullptr;
static void micTick(lv_timer_t*) {
  uint8_t lv = mic::level();
  if (s_micBar) lv_bar_set_value(s_micBar, lv, LV_ANIM_OFF);
  if (s_micLbl) lv_label_set_text_fmt(s_micLbl, "level %3u%%   %s", lv, lv > 10 ? "SIGNAL" : lv > 2 ? "noise floor" : "silent (no mic? check SCK22 WS21 SD35)");
}
static void micLeave(lv_event_t*) { if (s_micTimer) { lv_timer_del(s_micTimer); s_micTimer = nullptr; } mic::stopMonitor(); s_micBar = nullptr; s_micLbl = nullptr; showSettings(); }
static void cbMicTest(lv_event_t*) {
  lv_obj_t* scr = newScreen(); statusBar(scr, "08", "MIC TEST", false);
  lv_obj_t* b = makeLabel(scr, mic::begin() ? "Speak. The bar should jump with your voice." : "I2S driver failed to start.", &body);
  lv_obj_set_width(b, LV_PCT(100)); lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP); lv_obj_set_style_text_font(b, &lv_font_montserrat_12, 0);
  s_micBar = lv_bar_create(scr); lv_obj_set_size(s_micBar, LV_PCT(100), 22); lv_bar_set_range(s_micBar, 0, 100);
  lv_obj_set_style_bg_color(s_micBar, lv_color_hex(CO_DEEP), 0); lv_obj_set_style_bg_color(s_micBar, lv_color_hex(CO_PHOSPHOR), LV_PART_INDICATOR);
  s_micLbl = makeLabel(scr, "", &mono);
  lv_obj_t* hint = makeLabel(scr, "INMP441: VDD 3V3  GND  SCK IO22  WS IO21  SD IO35  L/R GND", &mono); lv_obj_set_style_text_color(hint, lv_color_hex(CO_MUTE), 0); lv_obj_set_width(hint, LV_PCT(100)); lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
  lv_obj_t* back = makeButton(scr, LV_SYMBOL_LEFT " Back", true); lv_obj_set_size(back, big() ? 240 : 160, btnH() + 4); lv_obj_add_event_cb(back, micLeave, LV_EVENT_CLICKED, nullptr);
  mic::startMonitor();
  if (s_micTimer) lv_timer_del(s_micTimer);
  s_micTimer = lv_timer_create(micTick, 60, nullptr);
  load(scr);
}
}
