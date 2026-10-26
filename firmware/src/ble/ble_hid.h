#pragma once
#include <Arduino.h>
// BLE HID keyboard + consumer control ("AITL Keyboard"). Pairs with macOS/iOS/Windows/Linux as a real keyboard.
namespace blehid {
  enum Mod : uint8_t { L_CTRL = 1, L_SHIFT = 2, L_ALT = 4, L_GUI = 8, R_CTRL = 16, R_SHIFT = 32, R_ALT = 64, R_GUI = 128 };
  void begin();
  void end();
  bool started();
  bool connected();
  String status();                         // "advertising" / "connected" / "off"
  void typeText(const char* utf8);         // queued, non-blocking; ASCII + newline/tab
  void tap(uint8_t mods, uint8_t keycode); // single key press/release
  void combo(const char* spec);            // "cmd+v", "ctrl+alt+d", "enter", "f5"
  void dictationShortcut();                // per g_settings.dictShortcut
  void consumer(uint16_t usage);           // 0xE9 vol+, 0xEA vol-, 0xCD play/pause, 0xE2 mute
  void tick();
  size_t pendingChars();
  // Nordic UART Service link (bridge protocol over BLE). Enabled with g_settings.bleLink; same NDJSON frames.
  typedef void (*LineCb)(const char* line, size_t len);
  void setLineHandler(LineCb cb);
  bool linkSubscribed();           // a central is listening on TX
  bool linkSend(const char* data, size_t len);   // chunks to MTU, appends nothing (caller sends the newline)
}
