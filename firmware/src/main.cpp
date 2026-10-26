// Musecorder — AITL (AI In The Loop) for Meta Muse · Caeruli Obscuri Laboratorium
// A reconfigurable Muse companion on an ESP32 CYD. Keys live on your Mac; the device never sees them.
#include <Arduino.h>
#include <lvgl.h>
#include "config.h"
#include "core/settings.h"
#include "core/app.h"
#include "hal/display.h"
#include "hal/led.h"
#include "hal/sdcard.h"
#include "ui/ui.h"
#include "link/link.h"
#include "applets/applet.h"

void setup() {
  settingsLoad();
  Serial.setRxBufferSize(g_settings.wifiEnabled ? 4096 : 16384);   // list replies arrive in bursts while LVGL is busy redrawing (must precede begin)
  Serial.begin(SERIAL_BAUD);
  ledBegin();
  displayBegin();
  ui::begin();
  ui::showBoot();
  lv_timer_handler(); displayWake();
  ui::bootLog("board: " AITL_BOARD_NAME "  " TFT_PANEL_NAME " + " TOUCH_NAME);
  ui::bootLog(sdBegin() ? "microsd: mounted" : "microsd: no card");
  app::begin();
  ui::bootLog(g_settings.wifiEnabled ? "wifi: joining" : "wifi: off");
  ui::bootLog(g_settings.bleEnabled ? "ble keyboard: advertising" : "ble keyboard: off");
  ui::bootLog("usb link: waiting for bridge");
  bridge::log("AITL %s %s boot #%u heap=%u id=%s", AITL_FW_VERSION, AITL_BOARD_NAME, g_settings.bootCount, ESP.getFreeHeap(), app::deviceId().c_str());
  uint32_t t0 = millis(); while (millis() - t0 < 500) { lv_timer_handler(); ledTick(); delay(5); }
  if (g_settings.wifiEnabled && g_settings.wifiSsid[0] && g_settings.bridgeHost[0] && g_settings.wifiMode == 1) {
    // TLS needs its peak memory during the handshake: do it before the UI exists, while the heap is still roomy.
    ui::bootLog("wifi: linking to bridge...");
    t0 = millis(); while (millis() - t0 < 12000 && !bridge::wsUp()) { app::tick(); lv_timer_handler(); ledTick(); delay(5); }
    ui::bootLog(bridge::wsUp() ? "wifi: bridge linked" : "wifi: no link yet (keeps trying)");
    bridge::log("after tls attempt: heap=%u largest=%u wsUp=%d", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), bridge::wsUp());
  }
  if (!g_settings.setupDone) ui::showSetupWizard(); else ui::showHome();
  lv_timer_handler();
  if (g_settings.setupDone) applets::begin();   // autostart applet (kiosk) replaces the home screen
  bridge::log("after ui: heap=%u largest=%u", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void loop() {
  static uint32_t perfAt = 0, handlerUs = 0, handlerMax = 0, loops = 0;
  app::tick();
  applets::tick();
  uint32_t t0 = micros();
  ui::tick();
  uint32_t dt = micros() - t0; handlerUs += dt; if (dt > handlerMax) handlerMax = dt; loops++;
  ledTick();
  displayPowerTick();
  if (millis() - perfAt > 3000) { perfAt = millis(); if (handlerMax > 120000) displayPerfReport(handlerUs, handlerMax, loops); handlerUs = handlerMax = loops = 0; }
  delay(2);
}
