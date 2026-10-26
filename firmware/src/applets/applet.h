#pragma once
// Applet engine: JSON-described tools (screen + state + handlers) that a host agent installs over the link.
// See docs/APPLETS.md. One applet runs at a time; `background` applets keep their timers while off screen.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <vector>
#include "expr.h"
#include "../link/link.h"

namespace applets {
struct Info { String id, name, icon; int version; size_t size; bool autostart; };

void begin();                                   // storage + autostart
void tick();                                    // timers, pin watches, persistence, deferred exit
bool onMessage(JsonDocument& d, bridge::Transport from);   // applet.* / net.http.reply / ai.reply; true when consumed

std::vector<Info> list();
bool install(const String& json, bool open, String& err, String* idOut = nullptr);
bool remove(const String& id);
bool open(const String& id, String& err);       // loads, shows the screen, runs `start`
void requestClose();                            // leave the running applet (deferred to tick)
bool running();
const String& runningId();
const String& runningName();
bool onScreen();

// Events + state
void raise(const char* event);                  // run a handler by name (no-op when missing)
bool wantsBoot();                               // the applet defines boot:press
void handleBootHold();                          // BOOT held 3 s: leave the applet
void setState(const String& key, const expr::Value& v);
expr::Value getState(const String& key);
bool resolve(const String& name, expr::Value& out);   // state + built-ins (expr::Resolver shape)
void logLine(const String& line);
String uiText();                                // plain-text rendering of the current screen

// Used by the renderer (applet_ui.cpp)
JsonArray uiArray();
JsonObject onObject();
size_t registerActions(JsonArray a);            // returns an index usable from LVGL user data
void runActionsAt(size_t idx);
void runActions(JsonArray a);
void markDirty();
bool takeDirty();
void onScreenShown(lv_obj_t* scr);
void onScreenGone();
void keepAlive(bool on);                        // screen is being swapped for a text entry: do not exit

// Renderer / launcher (applet_ui.cpp)
void buildScreen();                             // creates + loads the LVGL screen for the running applet
void refreshBindings();                         // re-evaluate templates after state changes
const char* symbolFor(const String& name);     // LVGL symbol for an icon name, or nullptr
}

namespace ui {
void showApps();                                // launcher
void showApplet();                              // the running applet's screen
}
