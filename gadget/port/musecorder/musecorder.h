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
void musecorder_session_starved(void);                       // the control session could not allocate its buffers: a running applet is paused and relaunched once linked
void musecorder_screen_reset(void);                          // the status screen was taken back (display.show_animation): repaint the applet
bool musecorder_console_command(const char *line);           // serial ">wifi=..." lines (sdk_token_console.c hands over what it does not own)

// Command descriptions advertised to Muse in link.register (keep the total under 8 KB).
#if CONFIG_HOMEHUB_LED_BACKEND_STICKS3_ST7789
#define MC_SCREEN_NOTE "Screen is a small colour LCD with NO touch, worn on a watch strap: 135 wide x 240 tall in portrait, or 240 wide x 135 tall if the user switched to landscape, so keep layouts to a few short centered lines rather than fixed pixel positions. Input is two buttons: the big front button fires boot:press, the side button fires btn:down; the user holds the side button to open the launcher. Design for buttons - put actions under on:{boot:press} and on:{btn:down}; on-screen touch buttons do nothing here. Keep text scale 1-2 (big up to 3). Up to 5 applets are stored and survive reboot; list state keys under 'persist' to keep their values across restarts. IR remote: ir.nec/ir.raw default to pin 46. IMU: imu.read{to} fills <to>.ax/.ay/.az (g), <to>.gx/.gy/.gz (deg/s) and <to>.pitch/.roll (deg) from the 6-axis sensor - good for tilt, taps and gestures (default to 'imu')."
#else
#define MC_SCREEN_NOTE "Screen 320x480 (y 28-452 usable); a 1 s finger hold opens the launcher."
#endif
#define MUSECORDER_INSTALL_DESC \
    "Install and run a small JSON tool (applet) on this gadget's touchscreen; json = the applet object (under 4 KB; up to 5 stay stored, id alone restarts a stored one). Example: " \
    "{\"id\":\"tally\",\"name\":\"Door Tally\",\"state\":{\"n\":0},\"persist\":[\"n\"]," \
    "\"screen\":[{\"text\":\"Door A\"},{\"big\":\"${n}\"},{\"bar\":\"${n}\",\"max\":100},{\"buttons\":[{\"label\":\"+1\",\"event\":\"up\"},{\"label\":\"-1\",\"event\":\"down\"}]},{\"button\":\"Post\",\"event\":\"post\"}]," \
    "\"on\":{\"up\":[{\"inc\":\"n\"}],\"down\":[{\"dec\":\"n\"}],\"post\":[{\"muse.say\":\"tally ${n}\"}],\"every:300\":[{\"muse.say\":\"tally ${n}\"}]}}. " \
    "Screen items: text, big, bar{max}, gap, button{event,color}, buttons[{label,event}], canvas (rest of the screen is yours via draw.*). " \
    "Events: start, stop, boot:press, touch (sets touch.x touch.y; also touch:move), every:SECONDS, gpio:PIN:rise|fall|change (pins 4 16 17 21 22 25 26 32 34-39), custom names via applet.event. " \
    "Actions: set{k:v}, inc/dec/toggle k, log, if{if,then,else}, exit, led #rrggbb, tone{freq|freq:[..],ms}, pwm{pin,duty,freq}, gpio.write{pin,value}, gpio.read{pin,to}, adc.read{pin,to} (36 39 32 34 35), " \
    "i2c.scan{to}, i2c.read{addr,reg|cmd:[..],wait,len,to} (bytes land in to.0..to.n), i2c.write{addr,reg|cmd,data,wait} (wait = ms pause; pins from applet i2c{sda,scl}, the 4-pin header is sda 32 scl 25), " \
    "ir.nec{addr,cmd,pin}, ir.raw{us:[mark,space,...],pin} (38 kHz IR remote; on the StickS3 the IR LED is pin 46 and its 5 V rail powers on automatically), " \
    "draw.rect{x,y,w,h,color}, draw.text{x,y,text,scale,color}, clear, sd.append{file,text}, sd.read{file,to,tail}, sd.draw{file}, sd.fetch{path,file} (microSD), " \
    "muse.say \"text\" (posts into your chat with this Muse), muse.ask{text,to}, http{method,url,json,to} (little RAM), vm.write{path,text,append} (needs scope). " \
    "${expr}: state keys, math, comparisons, functions len int floor ceil round abs min max pad(x,w,fill) mmss(seconds)=m:ss hhmmss clock random(lo,hi) uptime time. The name is the title. " MC_SCREEN_NOTE
#define MUSECORDER_REMOVE_DESC "Stop the running applet and bring back the status screen (it stays stored). id: delete that stored applet. all:true deletes every stored applet."
#define MUSECORDER_STATE_DESC \
    "Read the running applet: id, state, last log lines, stored ids, free RAM. set: an object of state keys to assign first; doc:true also returns the applet JSON (edit and reinstall); calibrate:true runs the touch setup."
#define MUSECORDER_EVENT_DESC "Fire a custom event by name on the running applet (its on.<name> actions run)."
#define MUSECORDER_WIFI_ADD_DESC "Remember another Wi-Fi network (up to 8); the gadget joins whichever saved network is nearby. Ask the user for the password; it stays on the device."
#define MUSECORDER_WIFI_FORGET_DESC "Forget a saved Wi-Fi network by ssid."
#define MUSECORDER_WIFI_LIST_DESC "List the Wi-Fi networks this gadget remembers (names only)."
#define MUSECORDER_SD_DESC "microSD card. op=list (files in folder mc) | read{file,tail} (last bytes, default 384) | append{file,text} | delete{file} | fetch{path,file} (copy a file from this VM, e.g. workspace/cat.jpg, to the card in the background; the applet.state log reports sd.fetch <file>: N bytes) | draw{file,row} (show a baseline JPEG from the card, offline)."

#ifdef __cplusplus
}
#endif
