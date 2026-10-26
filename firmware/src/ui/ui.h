#pragma once
#include <lvgl.h>
#include "../core/app.h"
namespace ui {
  void begin();
  void tick();
  void showBoot();
  void bootLog(const char* line);
  void showHome();
  void showModels();
  void showChats();
  void showActions();
  void showCompose(int target);       // 0 = AI, 1 = Mac keyboard
  void showSettings();
  void showSettingsRoot();
  void showSettingsSection(int i);     // 0 Connection … 7 About (ui.show "settings:3")            // the section menu (showSettings reopens the last section after a change)
  void showCalibrate(bool fromWizard);
  void showTouchTest();
  void showSetupWizard();
  void showPairing();
  void showInbox();
  void showTextEntry(const char* title, const char* initial, bool password, void (*onDone)(const char* text));
  void showTextEntryEx(const char* title, const char* initial, bool password, void (*onDone)(const char* text), void (*onCancel)());
  void showApps();
  void showApplet();
  void toast(const char* msg, bool error = false);
  void onAppEvent(app::Ev ev, const char* arg);
  void listsUpdated();
  void showAlert();
  void hideAlert();
}
// shared helpers (ui_internal)
namespace ui_int {
  lv_obj_t* newScreen();
  lv_obj_t* statusBar(lv_obj_t* scr, const char* section, const char* title, bool backBtn);
  void load(lv_obj_t* scr);
  lv_obj_t* row(lv_obj_t* parent, const char* label);             // settings row container: label left, control right
  lv_obj_t* addSwitch(lv_obj_t* parent, const char* label, bool on, lv_event_cb_t cb, void* ud);
  lv_obj_t* addDropdown(lv_obj_t* parent, const char* label, const char* opts, uint16_t sel, lv_event_cb_t cb, void* ud);
  lv_obj_t* addSlider(lv_obj_t* parent, const char* label, int min, int max, int val, lv_event_cb_t cb, void* ud);
  lv_obj_t* addAction(lv_obj_t* parent, const char* label, const char* value, lv_event_cb_t cb, void* ud);
  lv_obj_t* sectionLabel(lv_obj_t* parent, const char* text);
  lv_obj_t* sectionCard(lv_obj_t* parent, const char* title);   // card with a title; rows go inside it
  lv_obj_t* scrollList(lv_obj_t* scr);                           // vertical scrolling column filling the screen
  void confirm(const char* title, const char* text, void (*onYes)());
}
