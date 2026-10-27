// Musecorder applet runtime for Meta's Muse Gadget firmware (CYD boards).
// Your Muse installs a small JSON "applet" over its control session (applet.install); the gadget runs it:
// state, timers, the BOOT button, touch buttons, GPIO / ADC / PWM / I2C pins, the speaker, drawing, HTTP
// posts, messages into your Muse chat, and a text screen on the status display. Up to five applets stay in
// their own flash partition and a touch launcher switches between them. Nothing here touches the firmware
// image, so a bad applet cannot brick the device: applet.remove (or the launcher) brings the status screen back.
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
void musecorder_screen_reset(void);                          // the status screen was taken back (display.show_animation): repaint the applet
bool musecorder_console_command(const char *line);           // serial ">wifi=..." lines (sdk_token_console.c hands over what it does not own)

// Command descriptions advertised to Muse in link.register (keep the total under 8 KB).
#define MUSECORDER_INSTALL_DESC \
    "Install and run a small JSON tool (applet) on this gadget's touchscreen; json = the applet object (under 4 KB; up to 5 stay stored, id alone restarts a stored one). Example: " \
    "{\"id\":\"tally\",\"name\":\"Door Tally\",\"state\":{\"n\":0},\"persist\":[\"n\"]," \
    "\"screen\":[{\"text\":\"Door A\"},{\"big\":\"${n}\"},{\"bar\":\"${n}\",\"max\":100},{\"buttons\":[{\"label\":\"+1\",\"event\":\"up\"},{\"label\":\"-1\",\"event\":\"down\"}]},{\"button\":\"Post\",\"event\":\"post\"}]," \
    "\"on\":{\"up\":[{\"inc\":\"n\"},{\"tone\":{\"freq\":880,\"ms\":60}}],\"down\":[{\"dec\":\"n\"}],\"boot:press\":[{\"inc\":\"n\"}],\"every:300\":[{\"muse.say\":\"tally ${n}\"}]}}. " \
    "Screen items: text, big, bar{max}, gap, button{event,color}, buttons[{label,event}], canvas (rest of the screen is yours via draw.*). " \
    "Events: start, stop, boot:press, touch (sets touch.x touch.y; also touch:move), every:SECONDS, gpio:PIN:rise|fall|change (pins 4 16 17 21 22 25 26 32 34-39), custom names via applet.event. " \
    "Actions: set{k:v}, inc/dec/toggle k, log, if{if,then,else}, exit, led #rrggbb, tone{freq|freq:[..],ms} (speaker), pwm{pin,duty,freq}, gpio.write{pin,value}, gpio.read{pin,to}, adc.read{pin,to} (36 39 32 34 35), " \
    "i2c.scan{to}, i2c.read{addr,reg,len,to}, i2c.write{addr,reg,data} (pins from applet i2c{sda,scl}, default 21/22), draw.rect{x,y,w,h,color}, draw.text{x,y,text,scale,color}, clear, " \
    "muse.say \"text\" (posts into your chat with this Muse), muse.ask{text,to}, http{method,url,json,to} (may fail: little RAM), vm.write{path,text,append} (refused unless the credential has the scope). " \
    "${expr}: state keys, math, comparisons, functions len int floor ceil round abs min max pad(x,w,fill) mmss(seconds)=m:ss hhmmss clock random(lo,hi) uptime time. The name is drawn as the title automatically. Screen is 320x480 (y 28-452 usable). Holding a finger on the screen for 1 s opens the launcher (stored applets)."
#define MUSECORDER_REMOVE_DESC "Stop the running applet and bring back the status screen (it stays stored). id: delete that stored applet. all:true deletes every stored applet."
#define MUSECORDER_STATE_DESC \
    "Read the running applet: id, state values, last log lines, stored applet ids, free RAM. Optional set: an object of state keys to assign first; doc:true also returns the applet JSON (to edit and reinstall); calibrate:true runs the touch setup on the screen."
#define MUSECORDER_EVENT_DESC "Fire a custom event by name on the running applet (its on.<name> actions run)."
#define MUSECORDER_WIFI_ADD_DESC "Remember another Wi-Fi network on this gadget (up to 8); it joins whichever saved network is nearby after a reboot or when the current one drops. Ask the user for the password; it is stored on the device only."
#define MUSECORDER_WIFI_FORGET_DESC "Forget a saved Wi-Fi network by ssid."
#define MUSECORDER_WIFI_LIST_DESC "List the Wi-Fi networks this gadget remembers (names only)."

#ifdef __cplusplus
}
#endif
