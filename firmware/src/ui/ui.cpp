#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/settings.h"
#include "../hal/display.h"
#include "../hal/led.h"
#include "../link/link.h"
#include "../ble/ble_hid.h"
#include "../hal/mic.h"
#include "../applets/applet.h"

using namespace theme;
namespace ui {
static lv_obj_t* s_boot = nullptr; static lv_obj_t* s_bootLog = nullptr;
static lv_obj_t* s_home = nullptr; static lv_obj_t* s_msgList = nullptr; static lv_obj_t* s_transcriptPanel = nullptr; static lv_obj_t* s_empty = nullptr; static size_t s_msgCount = 0; static int s_lastMsgLabels = 0;
static lv_obj_t* s_modelChip = nullptr; static lv_obj_t* s_chatChip = nullptr; static lv_obj_t* s_linkChip = nullptr; static lv_obj_t* s_linkDot = nullptr;
static lv_obj_t* s_dictBtn = nullptr; static lv_obj_t* s_stopBtn = nullptr; static lv_obj_t* s_pasteBtn = nullptr; static lv_obj_t* s_copyBtn = nullptr;
static lv_obj_t* s_toast = nullptr; static lv_timer_t* s_toastTimer = nullptr;
static lv_obj_t* s_bell = nullptr; static lv_obj_t* s_np = nullptr; static lv_obj_t* s_npLabel = nullptr; static lv_obj_t* s_npPlay = nullptr;
static String s_bootText;
static void (*s_confirmCb)() = nullptr;
static lv_obj_t* s_alertBox = nullptr;
}

namespace ui_int {
lv_obj_t* newScreen() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_add_style(s, &screen, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(s, big() ? 8 : 5, 0);
  lv_obj_set_style_pad_row(s, big() ? 6 : 4, 0);
  return s;
}
void load(lv_obj_t* scr) {
  lv_obj_t* old = lv_scr_act();
  if (old == scr) return;
  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 90, 0, old != nullptr);   // auto_del: safe from the old screen's events
}
static void backCb(lv_event_t*) { ui::showHome(); }
lv_obj_t* statusBar(lv_obj_t* scr, const char* section, const char* ttl, bool backBtn) {
  lv_obj_t* bar = lv_obj_create(scr);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, LV_PCT(100), chipH() + 6);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(bar, 8, 0);
  if (backBtn) {
    lv_obj_t* b = lv_btn_create(bar); lv_obj_remove_style_all(b); lv_obj_add_style(b, &btn, 0); lv_obj_add_style(b, &btnPressed, LV_STATE_PRESSED);
    lv_obj_set_size(b, big() ? 64 : 42, chipH() + 6); lv_obj_set_style_pad_all(b, 0, 0); lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, LV_SYMBOL_LEFT); lv_obj_center(l);
    lv_obj_add_event_cb(b, backCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_ext_click_area(b, 8);
  }
  makeLabel(bar, section, &mono);
  lv_obj_t* rule = lv_obj_create(bar); lv_obj_remove_style_all(rule); lv_obj_set_size(rule, 14, 1);
  lv_obj_set_style_bg_color(rule, lv_color_hex(CO_CAERULEI), 0); lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
  lv_obj_t* t = makeLabel(bar, ttl, &monoBig);
  (void)t;
  return bar;
}
lv_obj_t* sectionLabel(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = makeLabel(parent, text, &mono);
  lv_obj_set_style_pad_top(l, big() ? 10 : 7, 0); lv_obj_set_style_pad_left(l, 4, 0);
  return l;
}
lv_obj_t* row(lv_obj_t* parent, const char* label) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r); lv_obj_add_style(r, &panel, 0);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(r, rowH(), 0);
  lv_obj_set_style_pad_ver(r, big() ? 6 : 4, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = makeLabel(r, label, &body);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_flex_grow(l, 1);
  return r;
}
lv_obj_t* addSwitch(lv_obj_t* parent, const char* label, bool on, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = row(parent, label);
  lv_obj_t* sw = lv_switch_create(r);
  lv_obj_set_size(sw, big() ? 62 : 46, big() ? 32 : 24);
  lv_obj_set_style_bg_color(sw, lv_color_hex(CO_DEEP), 0);
  lv_obj_set_style_bg_color(sw, lv_color_hex(CO_CAERULEI), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, lv_color_hex(CO_BONE), LV_PART_KNOB);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  if (cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, ud);
  return sw;
}
lv_obj_t* addDropdown(lv_obj_t* parent, const char* label, const char* opts, uint16_t sel, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = row(parent, label);
  lv_obj_t* dd = lv_dropdown_create(r);
  lv_dropdown_set_options(dd, opts); lv_dropdown_set_selected(dd, sel);
  lv_obj_set_width(dd, big() ? 230 : 140);
  lv_obj_set_style_bg_color(dd, lv_color_hex(CO_DEEP), 0); lv_obj_set_style_text_color(dd, lv_color_hex(CO_BONE), 0);
  lv_obj_set_style_border_color(dd, lv_color_hex(CO_CAERULEI_D), 0); lv_obj_set_style_text_font(dd, fontSmall(), 0);
  lv_obj_set_style_radius(dd, 6, 0);
  lv_obj_t* list = lv_dropdown_get_list(dd);
  lv_obj_set_style_bg_color(list, lv_color_hex(CO_ABYSS), 0); lv_obj_set_style_text_color(list, lv_color_hex(CO_BONE), 0);
  lv_obj_set_style_text_font(list, fontSmall(), 0); lv_obj_set_style_border_color(list, lv_color_hex(CO_MID), 0);
  lv_obj_set_style_bg_color(list, lv_color_hex(CO_CAERULEI_D), LV_PART_SELECTED | LV_STATE_CHECKED);
  if (cb) lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, ud);
  return dd;
}
lv_obj_t* addSlider(lv_obj_t* parent, const char* label, int min, int max, int val, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = row(parent, label);
  lv_obj_t* s = lv_slider_create(r);
  lv_slider_set_range(s, min, max); lv_slider_set_value(s, val, LV_ANIM_OFF);
  lv_obj_set_width(s, big() ? 200 : 120); lv_obj_set_height(s, big() ? 12 : 8);
  lv_obj_set_style_bg_color(s, lv_color_hex(CO_DEEP), 0);
  lv_obj_set_style_bg_color(s, lv_color_hex(CO_CAERULEI), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, lv_color_hex(CO_BONE), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, big() ? 6 : 4, LV_PART_KNOB);
  if (cb) lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, ud);
  return s;
}
lv_obj_t* addAction(lv_obj_t* parent, const char* label, const char* value, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = row(parent, label);
  if (cb) { lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_bg_color(r, lv_color_hex(CO_DEEP), LV_STATE_PRESSED); }
  lv_obj_t* v = makeLabel(r, value ? value : LV_SYMBOL_RIGHT, &muted);
  lv_label_set_long_mode(v, LV_LABEL_LONG_DOT); lv_obj_set_style_max_width(v, big() ? 260 : 150, 0);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  if (cb && value && *value) { lv_obj_t* ch = makeLabel(r, LV_SYMBOL_RIGHT, &muted); lv_obj_set_style_pad_left(ch, 6, 0); }
  if (cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
  return r;
}
static void confirmEv(lv_event_t* e) {
  lv_obj_t* mb = lv_event_get_current_target(e);
  const char* b = lv_msgbox_get_active_btn_text(mb);
  bool yes = b && !strcmp(b, "Yes");
  lv_msgbox_close_async(mb);
  auto cb = ui::s_confirmCb; ui::s_confirmCb = nullptr;
  if (yes && cb) cb();
}
void confirm(const char* title, const char* text, void (*onYes)()) {
  static const char* btns[] = {"Yes", "No", ""};
  ui::s_confirmCb = onYes;
  lv_obj_t* mb = lv_msgbox_create(nullptr, title, text, btns, false);
  lv_obj_set_style_bg_color(mb, lv_color_hex(CO_ABYSS), 0); lv_obj_set_style_text_color(mb, lv_color_hex(CO_BONE), 0);
  lv_obj_set_style_border_color(mb, lv_color_hex(CO_CAERULEI_D), 0); lv_obj_set_style_radius(mb, 10, 0); lv_obj_set_width(mb, big() ? 360 : 240);
  lv_obj_t* bm = lv_msgbox_get_btns(mb);
  lv_obj_set_style_bg_color(bm, lv_color_hex(CO_DEEP), LV_PART_ITEMS); lv_obj_set_style_text_color(bm, lv_color_hex(CO_BONE), LV_PART_ITEMS);
  lv_obj_set_height(bm, btnH());
  lv_obj_add_event_cb(mb, confirmEv, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_center(mb);
}
}  // namespace ui_int

namespace ui {
using namespace ui_int;

// ---------- boot ----------
void showBoot() {
  s_boot = newScreen();
  lv_obj_set_flex_align(s_boot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* mark = makeLabel(s_boot, "C" CO_BULLET "O", &monoBig);
  lv_obj_set_style_text_letter_space(mark, 6, 0);
  lv_obj_t* name = makeLabel(s_boot, AITL_PRODUCT, &title);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_28, 0); lv_obj_set_style_text_letter_space(name, 4, 0);
  lv_obj_t* sub = makeLabel(s_boot, "MUSE IN THE LOOP", &mono); lv_obj_set_style_text_color(sub, lv_color_hex(CO_ASH), 0);
  lv_obj_t* motto = makeLabel(s_boot, AITL_MOTTO, &muted); lv_obj_set_style_pad_top(motto, 2, 0);
  lv_obj_t* box = lv_obj_create(s_boot); lv_obj_remove_style_all(box); lv_obj_add_style(box, &panel, 0);
  lv_obj_set_size(box, LV_PCT(92), big() ? 96 : 72);
  s_bootLog = lv_label_create(box); lv_obj_set_width(s_bootLog, LV_PCT(100)); lv_label_set_long_mode(s_bootLog, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(s_bootLog, big() ? &lv_font_unscii_16 : &lv_font_unscii_8, 0);
  lv_obj_set_style_text_color(s_bootLog, lv_color_hex(CO_PHOSPHOR), 0);
  lv_obj_t* foot = makeLabel(s_boot, "UNCLASSIFIED // OPEN SOURCE  " AITL_ORG, &mono);
  lv_obj_set_style_text_color(foot, lv_color_hex(CO_MUTE), 0); lv_obj_set_style_pad_top(foot, 6, 0);
  lv_scr_load(s_boot);
}
void bootLog(const char* line) {
  if (!s_bootLog) return;
  s_bootText += "> "; s_bootText += line; s_bootText += "\n";
  int lines = 0; for (int i = s_bootText.length() - 1; i >= 0; i--) if (s_bootText[i] == '\n' && ++lines > (big() ? 5 : 6)) { s_bootText = s_bootText.substring(i + 1); break; }
  lv_label_set_text(s_bootLog, s_bootText.c_str());
  lv_timer_handler();
}

// ---------- toast ----------
static void toastHide(lv_timer_t*) { if (s_toast) { lv_obj_del(s_toast); s_toast = nullptr; } s_toastTimer = nullptr; }
void toast(const char* msg, bool error) {
  if (s_toast) { lv_obj_del(s_toast); s_toast = nullptr; }
  if (s_toastTimer) { lv_timer_del(s_toastTimer); s_toastTimer = nullptr; }
  s_toast = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(s_toast); lv_obj_add_style(s_toast, &panel, 0);
  lv_obj_set_style_bg_color(s_toast, lv_color_hex(error ? 0x2a0a10 : CO_DEEP), 0);
  lv_obj_set_style_border_color(s_toast, lv_color_hex(error ? CO_RED : CO_CAERULEI), 0);
  lv_obj_set_style_radius(s_toast, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_pad_hor(s_toast, big() ? 16 : 12, 0);
  lv_obj_set_width(s_toast, LV_SIZE_CONTENT); lv_obj_set_style_max_width(s_toast, LV_PCT(94), 0); lv_obj_set_height(s_toast, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_toast, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(s_toast, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(s_toast, 8, 0);
  lv_obj_t* ic = makeLabel(s_toast, error ? LV_SYMBOL_WARNING : LV_SYMBOL_OK, &body); lv_obj_set_style_text_color(ic, lv_color_hex(error ? CO_RED : CO_CAERULEI), 0);
  lv_obj_t* l = makeLabel(s_toast, msg, &body); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_style_max_width(l, displayW() - (big() ? 90 : 70), 0);
  lv_obj_set_style_text_font(l, fontSmall(), 0);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -(dockH() + 8));
  s_toastTimer = lv_timer_create(toastHide, error ? 4500 : 2500, nullptr); lv_timer_set_repeat_count(s_toastTimer, 1);
}

// ---------- home ----------
static void setChip(lv_obj_t* chip, const char* icon, const char* text, uint32_t iconColor) {
  lv_label_set_text(lv_obj_get_child(chip, 0), icon); lv_obj_set_style_text_color(lv_obj_get_child(chip, 0), lv_color_hex(iconColor), 0);
  lv_label_set_text(lv_obj_get_child(chip, 1), text);
}
static void homeRefreshChips() {
  if (!s_home) return;
  String m = app::currentModel.length() ? app::currentModel : String("choose a model");
  int slash = m.indexOf('/'); if (slash > 0 && m.startsWith("muse-code/")) m = "Muse Code " + m.substring(slash + 1); else if (slash > 0 && m.startsWith("muse/")) m = m.substring(slash + 1);
  setChip(s_modelChip, LV_SYMBOL_CHARGE, m.c_str(), CO_CAERULEI);
  String c = app::currentChatTitle.length() ? app::currentChatTitle : String("Chats");
  setChip(s_chatChip, LV_SYMBOL_LIST, c.c_str(), CO_CAERULEI);
  bool linked = app::bridgeLinked();
  String lk = linked ? app::linkSummary() : String("OFFLINE");
  if (linked && blehid::connected() && app::linkSummary() != "BLE") lk += " +BLE";
  lv_label_set_text(lv_obj_get_child(s_linkChip, 1), lk.c_str());
  lv_obj_set_style_bg_color(s_linkDot, lv_color_hex(linked ? CO_PHOSPHOR : CO_AMBER), 0);
  bool live = app::dictating || app::capturing;
  lv_obj_set_style_bg_color(s_dictBtn, lv_color_hex(live ? CO_AMBER : CO_CARD), 0);
  lv_obj_set_style_border_color(s_dictBtn, lv_color_hex(live ? CO_AMBER : CO_CAERULEI_D), 0);
  lv_obj_set_style_text_color(s_dictBtn, lv_color_hex(live ? CO_BLACK : CO_BONE), 0);
  lv_label_set_text(lv_obj_get_child(s_dictBtn, 0), live ? LV_SYMBOL_STOP : LV_SYMBOL_AUDIO);
  lv_label_set_text(lv_obj_get_child(s_dictBtn, 1), live ? "STOP" : (g_settings.dictSource == 1 ? "TALK" : "DICTATE"));
  lv_obj_set_style_text_color(lv_obj_get_child(s_dictBtn, 1), lv_color_hex(live ? CO_BLACK : CO_ASH), 0);
  if (app::streaming) lv_obj_clear_flag(s_stopBtn, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_stopBtn, LV_OBJ_FLAG_HIDDEN);
  if (app::hasReply && !app::streaming) { lv_obj_clear_flag(s_pasteBtn, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_copyBtn, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_add_flag(s_pasteBtn, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_copyBtn, LV_OBJ_FLAG_HIDDEN); }
}
static void emptyHint(lv_obj_t* parent, const char* icon, const char* text) {
  lv_obj_t* r = lv_obj_create(parent); lv_obj_remove_style_all(r); lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(r, 10, 0);
  lv_obj_t* i = makeLabel(r, icon, &body); lv_obj_set_style_text_color(i, lv_color_hex(CO_CAERULEI), 0);
  lv_obj_t* t = makeLabel(r, text, &muted); lv_obj_set_style_text_color(t, lv_color_hex(CO_ASH), 0);
}
// LVGL re-measures every line of a label once per draw strip, so long labels get quadratic. Keep each label to a
// paragraph of a few lines: a strip then only touches the one or two labels that intersect it.
static const size_t kChunkChars = 220;
static lv_obj_t* addChunkLabel(const app::Msg& m, const String& text, bool first) {
  lv_obj_t* l = lv_label_create(s_msgList);
  lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(l, big() ? &lv_font_montserrat_16 : &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_line_space(l, big() ? 3 : 2, 0);
  if (m.role == 0) { lv_obj_set_style_text_color(l, lv_color_hex(CO_CAERULEI2), 0); if (first) lv_obj_set_style_pad_top(l, big() ? 8 : 5, 0); }
  else if (m.role == 2) lv_obj_set_style_text_color(l, lv_color_hex(CO_RED), 0);
  else lv_obj_set_style_text_color(l, lv_color_hex(CO_BONE), 0);
  lv_label_set_text(l, text.c_str());
  return l;
}
static int addMsgLabels(const app::Msg& m) {
  int n = 0; size_t start = 0; const String& t = m.text;
  while (start < t.length() || n == 0) {
    size_t end = t.length();
    if (end - start > kChunkChars) {
      int cut = t.indexOf("\n\n", start + kChunkChars / 2); if (cut < 0 || (size_t)cut > start + kChunkChars) cut = t.indexOf('\n', start + kChunkChars / 2);
      if (cut < 0 || (size_t)cut > start + kChunkChars) cut = t.indexOf(". ", start + kChunkChars / 2);
      if (cut < 0 || (size_t)cut > start + kChunkChars + 40) cut = t.indexOf(' ', start + kChunkChars);
      end = (cut < 0) ? t.length() : (size_t)cut + 1;
    }
    String part = t.substring(start, end); part.trim();
    if (part.length() || n == 0) { addChunkLabel(m, part, n == 0); n++; }
    start = end;
    if (end >= t.length()) break;
  }
  return n;
}
static void scrollToEnd() { lv_obj_update_layout(s_transcriptPanel); lv_obj_scroll_to_y(s_transcriptPanel, LV_COORD_MAX, LV_ANIM_OFF); }
static void homeRefreshTranscript(bool deltaOnly) {
  if (!s_home) return;
  const auto& msgs = app::messages();
  if (msgs.empty()) {
    lv_obj_add_flag(s_msgList, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(s_empty); s_msgCount = 0; s_lastMsgLabels = 0;
    lv_obj_t* h = makeLabel(s_empty, app::bridgeLinked() ? "Ready." : "No bridge linked", &title);
    lv_obj_set_style_text_color(h, lv_color_hex(app::bridgeLinked() ? CO_BONE : CO_AMBER), 0); lv_obj_set_style_pad_bottom(h, big() ? 10 : 6, 0);
    if (app::bridgeLinked()) {
      emptyHint(s_empty, LV_SYMBOL_AUDIO, g_settings.dictSource == 1 ? "TALK or hold BOOT to speak" : "DICTATE speaks into your Mac");
      emptyHint(s_empty, LV_SYMBOL_KEYBOARD, "TYPE to ask or type into the Mac");
      emptyHint(s_empty, LV_SYMBOL_LIST, "APPS: tools Muse built for this device");
    } else {
      emptyHint(s_empty, LV_SYMBOL_USB, "Plug into the Mac: musecorder serve");
      emptyHint(s_empty, LV_SYMBOL_BLUETOOTH, "or Setup " LV_SYMBOL_RIGHT " Bluetooth link");
    }
    return;
  }
  lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_msgList, LV_OBJ_FLAG_HIDDEN);
  if (deltaOnly && s_msgCount == msgs.size() && s_msgCount > 0) {
    // only the streaming message changes: re-split just that message (its last chunk is what grows)
    for (int i = 0; i < s_lastMsgLabels; i++) lv_obj_del(lv_obj_get_child(s_msgList, -1));
    s_lastMsgLabels = addMsgLabels(msgs.back());
  } else if (deltaOnly && s_msgCount + 1 == msgs.size()) {
    s_lastMsgLabels = addMsgLabels(msgs.back()); s_msgCount = msgs.size();
  } else {
    lv_obj_clean(s_msgList); s_lastMsgLabels = 0; for (auto& m : msgs) s_lastMsgLabels = addMsgLabels(m); s_msgCount = msgs.size();
  }
  scrollToEnd();
}
static void evModels(lv_event_t*) { showModels(); }
static void evChats(lv_event_t*) { showChats(); }
static void evDict(lv_event_t*) { app::dictation("toggle"); }
static void evType(lv_event_t*) { showCompose(0); }
static void evApps(lv_event_t*) { showApps(); }
static void evSettings(lv_event_t*) { showSettings(); }
static void evStop(lv_event_t*) { app::stopGeneration(); }
static void evPaste(lv_event_t*) { app::pasteLastReply(); toast("Typing reply into Mac..."); }
static void evCopy(lv_event_t*) { app::copyLastReply(); toast("Reply copied to Mac clipboard"); }
static void evPageUp(lv_event_t*) { lv_obj_scroll_by_bounded(s_transcriptPanel, 0, (lv_obj_get_height(s_transcriptPanel) * 3) / 4, LV_ANIM_OFF); }
static void evPageDown(lv_event_t*) { lv_obj_scroll_by_bounded(s_transcriptPanel, 0, -(lv_obj_get_height(s_transcriptPanel) * 3) / 4, LV_ANIM_OFF); }
static void evNewChat(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) { app::newChat(); toast("New chat"); } }
static void evInbox(lv_event_t*) { showInbox(); }
static void evNpPrev(lv_event_t*) { app::mediaKey(0xB6); }
static void evNpPlay(lv_event_t*) { app::mediaKey(0xCD); }
static void evNpNext(lv_event_t*) { app::mediaKey(0xB5); }
static void homeRefreshBell() {
  if (!s_bell) return;
  if (app::inboxUnread) { lv_label_set_text_fmt(lv_obj_get_child(s_bell, 1), "%u", app::inboxUnread); lv_obj_set_style_border_color(s_bell, lv_color_hex(CO_AMBER), 0); lv_obj_set_style_text_color(lv_obj_get_child(s_bell, 0), lv_color_hex(CO_AMBER), 0); }
  else { lv_label_set_text(lv_obj_get_child(s_bell, 1), ""); lv_obj_set_style_border_color(s_bell, lv_color_hex(CO_MID), 0); lv_obj_set_style_text_color(lv_obj_get_child(s_bell, 0), lv_color_hex(CO_MUTE), 0); }
}
static void homeRefreshNp() {
  if (!s_np) return;
  if (!app::np.app.length() || !app::np.title.length()) { lv_obj_add_flag(s_np, LV_OBJ_FLAG_HIDDEN); return; }
  lv_obj_clear_flag(s_np, LV_OBJ_FLAG_HIDDEN);
  String t = app::np.title; if (app::np.artist.length()) t += "  " CO_BULLET "  " + app::np.artist;
  lv_label_set_text(s_npLabel, t.c_str());
  lv_label_set_text(lv_obj_get_child(s_npPlay, 0), app::np.playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  lv_obj_set_style_text_color(s_npLabel, lv_color_hex(app::np.playing() ? CO_BONE : CO_ASH), 0);
}

static lv_obj_t* dockBtn(lv_obj_t* dock, const char* sym, const char* text, lv_event_cb_t cb) {
  lv_obj_t* b = lv_btn_create(dock); lv_obj_remove_style_all(b); lv_obj_add_style(b, &btn, 0);
  lv_obj_add_style(b, &btnPressed, LV_STATE_PRESSED);
  lv_obj_set_flex_grow(b, 1); lv_obj_set_height(b, LV_PCT(100)); lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(b, big() ? 3 : 1, 0);
  lv_obj_t* i = lv_label_create(b); lv_label_set_text(i, sym); lv_obj_set_style_text_font(i, fontIcon(), 0);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text); lv_obj_set_style_text_font(l, fontLabel(), 0);
  lv_obj_set_style_text_letter_space(l, 1, 0); lv_obj_set_style_text_color(l, lv_color_hex(CO_ASH), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}
static lv_obj_t* chip(lv_obj_t* parent, lv_event_cb_t cb, lv_coord_t maxW) {
  lv_obj_t* c = lv_obj_create(parent); lv_obj_remove_style_all(c); lv_obj_add_style(c, &theme::chip, 0);
  lv_obj_set_size(c, LV_SIZE_CONTENT, chipH()); lv_obj_set_style_max_width(c, maxW, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(c, 6, 0);
  lv_obj_set_style_bg_color(c, lv_color_hex(CO_CAERULEI_D), LV_STATE_PRESSED);
  lv_obj_t* i = lv_label_create(c); lv_label_set_text(i, ""); lv_obj_set_style_text_font(i, fontSmall(), 0);
  lv_obj_t* l = lv_label_create(c); lv_label_set_text(l, ""); lv_obj_set_style_text_font(l, fontSmall(), 0); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(l, maxW - (big() ? 56 : 40), 0);
  if (cb) lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_set_ext_click_area(c, 6);
  return c;
}
static lv_obj_t* miniBtn(lv_obj_t* parent, const char* sym, lv_event_cb_t cb, uint32_t color) {
  lv_obj_t* b = lv_btn_create(parent); lv_obj_remove_style_all(b); lv_obj_add_style(b, &btn, 0); lv_obj_add_style(b, &btnPressed, LV_STATE_PRESSED);
  lv_obj_set_size(b, big() ? 46 : 32, big() ? 34 : 24); lv_obj_set_style_pad_all(b, 0, 0); lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_text_color(b, lv_color_hex(color), 0); lv_obj_set_style_bg_color(b, lv_color_hex(CO_ABYSS), 0);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, sym); lv_obj_set_style_text_font(l, fontSmall(), 0); lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

void showHome() {
  s_home = newScreen();
  const lv_coord_t W = displayW();
  // row 1: wordmark + link pill
  lv_obj_t* bar = lv_obj_create(s_home); lv_obj_remove_style_all(bar); lv_obj_set_size(bar, LV_PCT(100), chipH() + 2);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* brandRow = lv_obj_create(bar); lv_obj_remove_style_all(brandRow); lv_obj_set_size(brandRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(brandRow, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(brandRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END); lv_obj_set_style_pad_column(brandRow, 8, 0); lv_obj_set_style_pad_left(brandRow, 4, 0);
  lv_obj_t* wm = makeLabel(brandRow, AITL_PRODUCT, &body); lv_obj_set_style_text_font(wm, big() ? &lv_font_montserrat_20 : &lv_font_montserrat_16, 0); lv_obj_set_style_text_letter_space(wm, 3, 0);
  lv_obj_t* co = makeLabel(brandRow, "AITL", &mono); lv_obj_set_style_pad_bottom(co, big() ? 3 : 2, 0);
  lv_obj_t* right = lv_obj_create(bar); lv_obj_remove_style_all(right); lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(right, 6, 0); lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  s_bell = chip(right, evInbox, W / 3);
  lv_label_set_text(lv_obj_get_child(s_bell, 0), LV_SYMBOL_BELL);
  s_linkChip = chip(right, evSettings, W / 2);
  s_linkDot = lv_obj_create(s_linkChip); lv_obj_remove_style_all(s_linkDot); lv_obj_set_size(s_linkDot, big() ? 10 : 8, big() ? 10 : 8); lv_obj_set_style_radius(s_linkDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_linkDot, LV_OPA_COVER, 0); lv_obj_move_to_index(s_linkDot, 0);
  lv_obj_del(lv_obj_get_child(s_linkChip, 1));   // the icon slot: the dot replaces it (children: dot, label)
  // row 2: model + chat pills
  lv_obj_t* chips = lv_obj_create(s_home); lv_obj_remove_style_all(chips); lv_obj_set_size(chips, LV_PCT(100), chipH() + 2);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  s_modelChip = chip(chips, evModels, W / 2 - 6);
  s_chatChip = chip(chips, evChats, W / 2 - 6);
  lv_obj_add_event_cb(s_chatChip, evNewChat, LV_EVENT_LONG_PRESSED, nullptr);
  // transcript card
  s_transcriptPanel = lv_obj_create(s_home); lv_obj_remove_style_all(s_transcriptPanel); lv_obj_add_style(s_transcriptPanel, &panel, 0);
  lv_obj_set_width(s_transcriptPanel, LV_PCT(100)); lv_obj_set_flex_grow(s_transcriptPanel, 1);
  lv_obj_set_scroll_dir(s_transcriptPanel, LV_DIR_VER); lv_obj_set_scrollbar_mode(s_transcriptPanel, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_clear_flag(s_transcriptPanel, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);   // every animation frame costs ~100 ms of text rendering
  s_msgList = lv_obj_create(s_transcriptPanel); lv_obj_remove_style_all(s_msgList); lv_obj_set_width(s_msgList, LV_PCT(100)); lv_obj_set_height(s_msgList, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_msgList, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_msgList, big() ? 6 : 4, 0); lv_obj_clear_flag(s_msgList, LV_OBJ_FLAG_SCROLLABLE);
  s_msgCount = 0; s_lastMsgLabels = 0;
  s_empty = lv_obj_create(s_transcriptPanel); lv_obj_remove_style_all(s_empty); lv_obj_set_size(s_empty, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(s_empty, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(s_empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_row(s_empty, big() ? 8 : 5, 0);
  // floating mini buttons (top-right of the card)
  lv_obj_t* fl = lv_obj_create(s_home); lv_obj_remove_style_all(fl); lv_obj_add_flag(fl, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(fl, LV_SIZE_CONTENT, big() ? 36 : 26); lv_obj_set_flex_flow(fl, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(fl, 4, 0);
  lv_obj_align(fl, LV_ALIGN_TOP_RIGHT, -(big() ? 14 : 10), 2 * chipH() + (big() ? 26 : 16));
  miniBtn(fl, LV_SYMBOL_UP, evPageUp, CO_ASH);
  miniBtn(fl, LV_SYMBOL_DOWN, evPageDown, CO_ASH);
  s_stopBtn = miniBtn(fl, LV_SYMBOL_STOP, evStop, CO_AMBER);
  s_copyBtn = miniBtn(fl, LV_SYMBOL_COPY, evCopy, CO_CAERULEI2);
  s_pasteBtn = miniBtn(fl, LV_SYMBOL_PASTE, evPaste, CO_PHOSPHOR);
  // now playing strip (hidden unless Music/Spotify has a track)
  s_np = lv_obj_create(s_home); lv_obj_remove_style_all(s_np); lv_obj_add_style(s_np, &panel, 0);
  lv_obj_set_size(s_np, LV_PCT(100), chipH() + (big() ? 14 : 10)); lv_obj_set_style_pad_ver(s_np, 2, 0); lv_obj_set_style_pad_hor(s_np, big() ? 10 : 6, 0);
  lv_obj_set_flex_flow(s_np, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(s_np, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(s_np, big() ? 8 : 5, 0);
  lv_obj_clear_flag(s_np, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* npIcon = makeLabel(s_np, LV_SYMBOL_AUDIO, &muted); lv_obj_set_style_text_color(npIcon, lv_color_hex(CO_CAERULEI), 0);
  s_npLabel = makeLabel(s_np, "", &muted); lv_label_set_long_mode(s_npLabel, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_flex_grow(s_npLabel, 1); lv_obj_set_style_text_font(s_npLabel, fontSmall(), 0);
  miniBtn(s_np, LV_SYMBOL_PREV, evNpPrev, CO_ASH);
  s_npPlay = miniBtn(s_np, LV_SYMBOL_PLAY, evNpPlay, CO_BONE);
  miniBtn(s_np, LV_SYMBOL_NEXT, evNpNext, CO_ASH);
  lv_obj_add_flag(s_np, LV_OBJ_FLAG_HIDDEN);
  // dock
  lv_obj_t* dock = lv_obj_create(s_home); lv_obj_remove_style_all(dock); lv_obj_set_size(dock, LV_PCT(100), dockH());
  lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(dock, big() ? 6 : 4, 0);
  s_dictBtn = dockBtn(dock, LV_SYMBOL_AUDIO, "DICTATE", evDict);
  dockBtn(dock, LV_SYMBOL_KEYBOARD, "TYPE", evType);
  dockBtn(dock, LV_SYMBOL_LIST, "APPS", evApps);
  lv_obj_t* sb = dockBtn(dock, LV_SYMBOL_SETTINGS, "SETUP", evSettings); lv_obj_set_flex_grow(sb, 0); lv_obj_set_width(sb, big() ? 96 : 60);
  homeRefreshChips(); homeRefreshTranscript(false); homeRefreshBell(); homeRefreshNp();
  load(s_home);
}

// ---------- inbox: recent cards from any Claude Code session ----------
static void evInboxOpen(lv_event_t* e) {
  size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i >= app::inbox.size()) return;
  String sid = app::inbox[i].session;
  if (sid.length()) { app::openSession(sid); toast("Opening session..."); showHome(); }
}
static void evInboxClear(lv_event_t*) { app::inboxClear(); showInbox(); }
void showInbox() {
  app::inboxMarkRead();
  lv_obj_t* scr = newScreen(); statusBar(scr, "09", "INBOX", true);
  lv_obj_t* list = lv_obj_create(scr); lv_obj_remove_style_all(list); lv_obj_set_width(list, LV_PCT(100)); lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(list, 4, 0); lv_obj_set_scroll_dir(list, LV_DIR_VER); lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  if (app::inbox.empty()) { lv_obj_t* l = makeLabel(list, "Nothing yet. Finished turns, questions and permission requests from any Muse Code session land here.", &muted); lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); }
  for (size_t i = 0; i < app::inbox.size(); i++) {
    const app::Alert& a = app::inbox[i];
    lv_obj_t* b = lv_btn_create(list); lv_obj_remove_style_all(b); lv_obj_add_style(b, &panel, 0); lv_obj_add_style(b, &btnPressed, LV_STATE_PRESSED);
    lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(b, 2, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0); lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(a.kind == "permission" ? CO_AMBER : a.kind == "done" ? CO_PHOSPHOR : CO_CAERULEI), 0);
    lv_obj_t* h = makeLabel(b, (a.title + (a.project.length() ? "   " + a.project : "")).c_str(), &body); lv_obj_set_width(h, LV_PCT(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);
    lv_obj_t* x = makeLabel(b, a.text.c_str(), &muted); lv_obj_set_width(x, LV_PCT(100)); lv_label_set_long_mode(x, LV_LABEL_LONG_WRAP); lv_obj_set_style_max_height(x, big() ? 70 : 48, 0);
    lv_obj_add_event_cb(b, evInboxOpen, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }
  if (!app::inbox.empty()) { lv_obj_t* c = makeButton(scr, LV_SYMBOL_TRASH "  Clear inbox"); lv_obj_set_size(c, LV_PCT(100), btnH()); lv_obj_add_event_cb(c, evInboxClear, LV_EVENT_CLICKED, nullptr); }
  load(scr);
}

// ---------- alerts (Claude Code permission / notices) ----------
static void alertBtn(lv_event_t* e) { app::replyAlert((const char*)lv_event_get_user_data(e)); }
static void alertOpen(lv_event_t*) {
  String sid = app::alert.session; bool twoWay = app::alert.twoWay;
  if (sid.length()) { app::openSession(sid); toast("Opening that Muse Code session..."); }
  if (!twoWay) app::replyAlert("dismiss");
  showHome();
}
void hideAlert() { if (s_alertBox) { lv_obj_del(s_alertBox); s_alertBox = nullptr; } }
void showAlert() {
  hideAlert();
  const app::Alert& a = app::alert; if (!a.id.length()) return;
  s_alertBox = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(s_alertBox); lv_obj_add_style(s_alertBox, &panel, 0);
  lv_obj_set_style_border_color(s_alertBox, lv_color_hex(a.twoWay ? CO_AMBER : CO_CAERULEI), 0); lv_obj_set_style_border_width(s_alertBox, 2, 0);
  lv_obj_set_style_bg_color(s_alertBox, lv_color_hex(CO_ABYSS), 0); lv_obj_set_style_radius(s_alertBox, 12, 0);
  lv_obj_set_style_shadow_width(s_alertBox, 30, 0); lv_obj_set_style_shadow_color(s_alertBox, lv_color_hex(CO_BLACK), 0); lv_obj_set_style_shadow_opa(s_alertBox, LV_OPA_70, 0);
  lv_obj_set_width(s_alertBox, LV_PCT(94)); lv_obj_set_height(s_alertBox, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_alertBox, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_alertBox, big() ? 6 : 4, 0);
  lv_obj_t* h = makeLabel(s_alertBox, a.twoWay ? "MUSE CODE  //  PERMISSION" : (a.kind == "done" ? "MUSE CODE  //  FINISHED" : "MUSE CODE"), &mono);
  lv_obj_set_style_text_color(h, lv_color_hex(a.twoWay ? CO_AMBER : a.kind == "done" ? CO_PHOSPHOR : CO_CAERULEI), 0);
  lv_obj_set_style_border_color(s_alertBox, lv_color_hex(a.twoWay ? CO_AMBER : a.kind == "done" ? CO_PHOSPHOR : CO_CAERULEI), 0);
  lv_obj_t* t = makeLabel(s_alertBox, a.title.c_str(), &body); lv_obj_set_width(t, LV_PCT(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_t* box = lv_obj_create(s_alertBox); lv_obj_remove_style_all(box); lv_obj_set_width(box, LV_PCT(100));
  lv_obj_set_style_max_height(box, (lv_coord_t)(displayH() * 0.4f), 0); lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_scroll_dir(box, LV_DIR_VER); lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_t* x = makeLabel(box, a.text.c_str(), &body); lv_obj_set_width(x, LV_PCT(100)); lv_label_set_long_mode(x, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(x, fontSmall(), 0); lv_obj_set_style_text_color(x, lv_color_hex(CO_ASH), 0);
  if (a.project.length()) { lv_obj_t* pr = makeLabel(s_alertBox, a.project.c_str(), &mono); lv_obj_set_style_text_color(pr, lv_color_hex(CO_MUTE), 0); }
  lv_obj_t* row = lv_obj_create(s_alertBox); lv_obj_remove_style_all(row); lv_obj_set_size(row, LV_PCT(100), btnH() + 4); lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(row, 6, 0);
  if (a.twoWay) {
    lv_obj_t* ok = makeButton(row, LV_SYMBOL_OK "  Allow", true); lv_obj_set_flex_grow(ok, 1); lv_obj_set_height(ok, btnH() + 4); lv_obj_add_event_cb(ok, alertBtn, LV_EVENT_CLICKED, (void*)"allow");
    lv_obj_t* no = makeButton(row, LV_SYMBOL_CLOSE "  Deny"); lv_obj_set_flex_grow(no, 1); lv_obj_set_height(no, btnH() + 4); lv_obj_add_event_cb(no, alertBtn, LV_EVENT_CLICKED, (void*)"deny");
  } else {
    lv_obj_t* ok = makeButton(row, "OK", true); lv_obj_set_flex_grow(ok, 1); lv_obj_set_height(ok, btnH() + 4); lv_obj_add_event_cb(ok, alertBtn, LV_EVENT_CLICKED, (void*)"dismiss");
  }
  if (a.session.length()) {
    lv_obj_t* op = makeButton(row, LV_SYMBOL_EDIT " Open"); lv_obj_set_width(op, big() ? 120 : 84); lv_obj_set_height(op, btnH() + 4); lv_obj_add_event_cb(op, alertOpen, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_center(s_alertBox);
}

void onAppEvent(app::Ev ev, const char* arg) {
  bool onHome = s_home && lv_scr_act() == s_home;
  switch (ev) {
    case app::Ev::Transcript: if (onHome) { homeRefreshTranscript(arg && !strcmp(arg, "delta")); homeRefreshChips(); } break;
    case app::Ev::Lists: if (onHome) homeRefreshChips(); else listsUpdated(); break;
    case app::Ev::Status: if (onHome) homeRefreshChips(); break;
    case app::Ev::Link: if (onHome) { homeRefreshChips(); homeRefreshTranscript(false); } toast(!strcmp(arg, "linked") ? "Bridge linked" : "Bridge link lost", strcmp(arg, "linked") != 0); break;
    case app::Ev::Toast: toast(arg); break;
    case app::Ev::Error: toast(arg, true); if (onHome) homeRefreshChips(); break;
    case app::Ev::Pairing: showPairing(); break;
    case app::Ev::Alert: showAlert(); break;
    case app::Ev::AlertClear: hideAlert(); break;
    case app::Ev::NowPlaying: if (onHome) homeRefreshNp(); break;
    case app::Ev::Inbox: if (onHome) homeRefreshBell(); break;
    case app::Ev::Provisioned: toast(!strcmp(arg, "wifi") ? "Wi-Fi provisioned from bridge" : "Bridge credentials installed"); break;
  }
}

void begin() { theme::init(); app::setUiCb(onAppEvent); }
void tick() { lv_timer_handler(); }
}  // namespace ui
