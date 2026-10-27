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
    "Install and run a tool ('applet') on this gadget. Pass the applet as the json string parameter. The applet " \
    "is a JSON object: {\"id\":\"door-tally\",\"name\":\"Door Tally\",\"state\":{\"count\":0},\"persist\":[\"count\"]," \
    "\"screen\":[{\"text\":\"Door A\"},{\"big\":\"${count}\"},{\"bar\":\"${count}\",\"max\":100},{\"text\":\"BOOT = +1\"}]," \
    "\"on\":{\"boot:press\":[{\"inc\":\"count\"}],\"every:300\":[{\"http\":{\"method\":\"POST\",\"url\":\"https://...\"," \
    "\"json\":{\"count\":\"${count}\"}}}],\"start\":[{\"log\":\"ready\"}]}}. " \
    "screen items: text (small line), big (large line), bar (value, max), gap (pixels). Strings may contain ${expr}: " \
    "state keys, numbers, 'strings', + - * / %, comparisons, && || !, and len(), int(), round(), abs(), min(), max(), " \
    "pad(v,width), upper(), lower(), clock() (HH:MM), hhmmss(seconds), random(a,b). " \
    "on events: start, boot:press, every:SECONDS, gpio:PIN:rise|fall|change (pins 4,16,17,21,22,25,26,32,34,35,36,39; 34-39 input only), " \
    "and custom names fired with applet.event. actions: {\"set\":{\"k\":\"${expr}\"}}, {\"inc\":\"k\",\"by\":1}, {\"dec\":\"k\"}, " \
    "{\"toggle\":\"k\"}, {\"log\":\"text\"}, {\"gpio.write\":{\"pin\":22,\"value\":1}}, {\"gpio.read\":{\"pin\":34,\"to\":\"k\"}}, " \
    "{\"led\":\"#00ff00\"}, {\"http\":{\"method\":\"POST\",\"url\":\"...\",\"json\":{...},\"to\":\"k\"}} (response text into state k), " \
    "{\"vm.write\":{\"path\":\"musecorder/tally.log\",\"text\":\"${count}\\n\",\"append\":true}} (writes a file on this VM you can read), " \
    "{\"if\":\"${expr}\",\"then\":[...],\"else\":[...]}, {\"exit\":true}. Keep the applet under 6 KB. Replaces any running applet; " \
    "applet.remove restores the status screen. Returns the applet's state."
#define MUSECORDER_REMOVE_DESC "Stop and delete the running applet and bring back the status screen."
#define MUSECORDER_STATE_DESC \
    "Read the running applet: its id, state values and the last log lines. Optional set: an object of state keys to assign first."
#define MUSECORDER_EVENT_DESC "Fire a custom event by name on the running applet (its on.<name> actions run)."

#ifdef __cplusplus
}
#endif
