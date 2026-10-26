#include "settings.h"
#include <Preferences.h>

Settings g_settings;
static Preferences prefs;
static const char* NS = "aitl";

void settingsLoad() {
  prefs.begin(NS, true);
  Settings d;  // defaults
  g_settings.brightness    = prefs.getUChar("bright", d.brightness);
  g_settings.screenTimeout = prefs.getUShort("tmo", d.screenTimeout);
  g_settings.rotation      = prefs.getUChar("rot", d.rotation) & 3;
  g_settings.invertColors  = prefs.getBool("inv", d.invertColors);
  g_settings.bgrOrder      = prefs.getBool("bgr", d.bgrOrder);
  g_settings.touchSwapXY   = prefs.getBool("tswap", d.touchSwapXY);
  g_settings.touchInvX     = prefs.getBool("tinvx", d.touchInvX);
  g_settings.touchInvY     = prefs.getBool("tinvy", d.touchInvY);
  g_settings.touchOffX     = prefs.getShort("toffx", d.touchOffX);
  g_settings.touchOffY     = prefs.getShort("toffy", d.touchOffY);
  g_settings.touchScaleX   = prefs.getUShort("tscx", d.touchScaleX);
  g_settings.touchScaleY   = prefs.getUShort("tscy", d.touchScaleY);
  g_settings.longPressMs   = prefs.getUShort("lpms", d.longPressMs);
  g_settings.touchDebounce = prefs.getUChar("tdeb", d.touchDebounce);
  g_settings.hapticLed     = prefs.getBool("hled", d.hapticLed);
  g_settings.sdLog         = prefs.getBool("sdlog", d.sdLog);
  g_settings.wifiEnabled   = prefs.getBool("wifi", d.wifiEnabled);
  g_settings.bleEnabled    = prefs.getBool("ble", d.bleEnabled);
  g_settings.hidTarget     = prefs.getUChar("hid", d.hidTarget);
  g_settings.dictShortcut  = prefs.getUChar("dict", d.dictShortcut);
  g_settings.dictSource    = prefs.getUChar("dsrc", d.dictSource);
  g_settings.dictTarget    = prefs.getUChar("dtgt", d.dictTarget);
  g_settings.micGain       = prefs.getUChar("mgain", d.micGain);
  g_settings.bleLink       = prefs.getBool("blel", d.bleLink);
  g_settings.wifiMode      = prefs.getUChar("wmode", d.wifiMode);
  prefs.getString("ssid", g_settings.wifiSsid, sizeof(g_settings.wifiSsid));
  prefs.getString("pass", g_settings.wifiPass, sizeof(g_settings.wifiPass));
  prefs.getString("bhost", g_settings.bridgeHost, sizeof(g_settings.bridgeHost));
  g_settings.bridgePort    = prefs.getUShort("bport", d.bridgePort);
  prefs.getString("btok", g_settings.bridgeToken, sizeof(g_settings.bridgeToken));
  prefs.getString("bname", g_settings.bridgeName, sizeof(g_settings.bridgeName));
  prefs.getString("model", g_settings.modelId, sizeof(g_settings.modelId));
  prefs.getString("lchat", g_settings.lastChat, sizeof(g_settings.lastChat));
  g_settings.setupDone     = prefs.getBool("setup", d.setupDone);
  g_settings.bootCount     = prefs.getUShort("boots", 0);
  prefs.end();
  if (g_settings.touchDebounce < 1) g_settings.touchDebounce = 1;
  if (g_settings.touchDebounce > 4) g_settings.touchDebounce = 4;
  g_settings.bootCount++;
  prefs.begin(NS, false); prefs.putUShort("boots", g_settings.bootCount); prefs.end();
}

void settingsSave() {
  prefs.begin(NS, false);
  prefs.putUChar("bright", g_settings.brightness);
  prefs.putUShort("tmo", g_settings.screenTimeout);
  prefs.putUChar("rot", g_settings.rotation & 3);
  prefs.putBool("inv", g_settings.invertColors);
  prefs.putBool("bgr", g_settings.bgrOrder);
  prefs.putBool("tswap", g_settings.touchSwapXY);
  prefs.putBool("tinvx", g_settings.touchInvX);
  prefs.putBool("tinvy", g_settings.touchInvY);
  prefs.putShort("toffx", g_settings.touchOffX);
  prefs.putShort("toffy", g_settings.touchOffY);
  prefs.putUShort("tscx", g_settings.touchScaleX);
  prefs.putUShort("tscy", g_settings.touchScaleY);
  prefs.putUShort("lpms", g_settings.longPressMs);
  prefs.putUChar("tdeb", g_settings.touchDebounce);
  prefs.putBool("hled", g_settings.hapticLed);
  prefs.putBool("sdlog", g_settings.sdLog);
  prefs.putBool("wifi", g_settings.wifiEnabled);
  prefs.putBool("ble", g_settings.bleEnabled);
  prefs.putUChar("hid", g_settings.hidTarget);
  prefs.putUChar("dict", g_settings.dictShortcut);
  prefs.putUChar("dsrc", g_settings.dictSource);
  prefs.putUChar("dtgt", g_settings.dictTarget);
  prefs.putUChar("mgain", g_settings.micGain);
  prefs.putBool("blel", g_settings.bleLink);
  prefs.putUChar("wmode", g_settings.wifiMode);
  prefs.putString("ssid", g_settings.wifiSsid);
  prefs.putString("pass", g_settings.wifiPass);
  prefs.putString("bhost", g_settings.bridgeHost);
  prefs.putUShort("bport", g_settings.bridgePort);
  prefs.putString("btok", g_settings.bridgeToken);
  prefs.putString("bname", g_settings.bridgeName);
  prefs.putString("model", g_settings.modelId);
  prefs.putString("lchat", g_settings.lastChat);
  prefs.putBool("setup", g_settings.setupDone);
  prefs.end();
}

void settingsReset() {
  prefs.begin(NS, false); prefs.clear(); prefs.end();
  g_settings = Settings();
}

void settingsRotate() {
  g_settings.rotation = (g_settings.rotation + 1) & 3;
  bool t = g_settings.touchInvX; g_settings.touchInvX = g_settings.touchInvY; g_settings.touchInvY = t;  // 90-degree step
  g_settings.touchOffX = g_settings.touchOffY = 0;
  { uint16_t t = g_settings.touchScaleX; g_settings.touchScaleX = g_settings.touchScaleY; g_settings.touchScaleY = t; }
  settingsSave();
}

void settingsSetRotation(uint8_t r) {
  r &= 3;
  if (((r - g_settings.rotation) & 1) != 0) {   // 90-degree change: the mirror axes trade places
    bool t = g_settings.touchInvX; g_settings.touchInvX = g_settings.touchInvY; g_settings.touchInvY = t;
    uint16_t sc = g_settings.touchScaleX; g_settings.touchScaleX = g_settings.touchScaleY; g_settings.touchScaleY = sc;
    g_settings.touchOffX = g_settings.touchOffY = 0;
  }
  g_settings.rotation = r;
  settingsSave();
}
