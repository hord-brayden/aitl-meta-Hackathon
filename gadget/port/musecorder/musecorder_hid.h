// BLE HID (keyboard + mouse + consumer) for the Musecorder port.
//
// A persistent *mode*, not a burst. The gadget's live link is over Wi-Fi, and a
// BLE connection starves that radio (it dropped the Muse Noise session ~every
// 2s). So the device is EITHER a Muse gadget OR a BLE HID device, never both at
// once. "HID mode" is a flag in NVS: toggling it reboots. On a HID-mode boot,
// app_run skips Wi-Fi/Muse entirely and calls musecorder_hid_begin(), which
// brings up BLE and advertises as a keyboard/mouse that stays discoverable.
// This is the foundation for Muse-programmed macropads (an applet calls the
// send API below while in HID mode).
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct ble_gatt_svc_def;   // from NimBLE; forward-declared so C callers need no NimBLE include

#ifdef __cplusplus
extern "C" {
#endif

// The HID + Battery + Device-Info service array to register at boot
// (ble_server.c hands this to ble_gatts_add_svcs). NULL if unavailable.
const struct ble_gatt_svc_def *musecorder_hid_svcs(void);

// Is HID mode the active boot mode? Read from NVS (cached). app_run uses this to
// skip the Wi-Fi/Muse session, and the UI to label the toggle.
bool musecorder_hid_mode_active(void);

// Flip HID mode on/off: persists to NVS and reboots into the chosen mode.
void musecorder_hid_set_mode(bool on);

// Called once from app_run on a HID-mode boot, AFTER the BLE server is started.
// Stops the setup advertising and advertises as a HID keyboard/mouse, staying
// discoverable and re-advertising on disconnect.
void musecorder_hid_begin(void);

// True once a host is connected, bonded and subscribed (ready for input).
bool musecorder_hid_ready(void);

// Type an ASCII string on the HID keyboard (key down/up per char). Must run on
// a normal task (it delays between keys), never the BLE host callback. No-op if
// not ready. Returns the number of characters sent.
int musecorder_hid_type(const char *s);

#ifdef __cplusplus
}
#endif
