// Musecorder applet runtime for Meta's Muse Gadget firmware (CYD boards).
// Your Muse installs a small JSON "applet" over its control session (applet.install); the gadget runs it:
// state, timers, the BOOT button, GPIO pins, HTTP posts, writes into your Muse's VM files, and a text
// screen drawn on the status display. Nothing here touches the firmware image, so a bad applet cannot
// brick the device: applet.remove (or a long BOOT hold) brings the status screen back.
#pragma once
#include <stdbool.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

void musecorder_init(void);                                  // after led_status_init() and button_init()
bool musecorder_on_tap(void);                                // BOOT tap: true when a running applet consumed it
cJSON *musecorder_command(const char *command, cJSON *params);   // applet.* commands; NULL if not ours
void musecorder_vm_connected(void);                          // control session is up (vm.write may be used)

// Command descriptions advertised to Muse in link.register (keep the total under 8 KB).
#define MUSECORDER_INSTALL_DESC \
    "Install and run a small JSON tool (applet) on this gadget; json = the applet object. Example: " \
    "{\"id\":\"tally\",\"name\":\"Door Tally\",\"state\":{\"n\":0},\"persist\":[\"n\"]," \
    "\"screen\":[{\"text\":\"Door A\"},{\"big\":\"${n}\"},{\"bar\":\"${n}\",\"max\":100}]," \
    "\"on\":{\"boot:press\":[{\"inc\":\"n\"}],\"every:300\":[{\"vm.write\":{\"path\":\"musecorder/tally.log\",\"text\":\"${n}\\n\",\"append\":true}}]}}. " \
    "Events: start, boot:press, every:SECONDS, gpio:PIN:rise|fall|change (pins 4 16 17 21 22 25 26 32 34-39), custom names via applet.event. " \
    "Actions: set{k:v}, inc/dec/toggle k, log, led #rrggbb, gpio.write{pin,value}, gpio.read{pin,to}, vm.write{path,text,append} (file on this VM), " \
    "http{method,url,json,to} (may fail: little RAM), if{if,then,else}, exit. ${expr}: state keys, math, comparisons, len int round pad clock hhmmss random. Under 4 KB."
#define MUSECORDER_REMOVE_DESC "Stop and delete the running applet and bring back the status screen."
#define MUSECORDER_STATE_DESC \
    "Read the running applet: its id, state values and the last log lines. Optional set: an object of state keys to assign first."
#define MUSECORDER_EVENT_DESC "Fire a custom event by name on the running applet (its on.<name> actions run)."

#ifdef __cplusplus
}
#endif
