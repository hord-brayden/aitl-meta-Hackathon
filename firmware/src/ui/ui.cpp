// Musecorder UI: shared helpers, boot, home (conversation), inbox, alerts, toasts. Muse look: see theme.h.
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
static lv_obj_t* s_boot = nullptr; static lv_obj_t* s_bootLog = nullptr; static String s_bootText;
static lv_obj_t* s_home = nullptr; static lv_obj_t* s_msgList = nullptr; static lv_obj_t* s_transcriptPanel = nullptr; static lv_obj_t* s_empty = nullptr;
static size_t s_msgCount = 0; static int s_lastMsgLabels = 0;
static lv_obj_t* s_orb = nullptr; static lv_obj_t* s_modelChip = nullptr; static lv_obj_t* s_chatChip = nullptr; static lv_obj_t* s_linkPill = nullptr; static lv_obj_t* s_linkDot = nullptr;
static lv_obj_t* s_dictBtn = nullptr; static lv_obj_t* s_stopBtn = nullptr; static lv_obj_t* s_pasteBtn = nullptr; static lv_obj_t* s_copyBtn = nullptr;
static lv_obj_t* s_bell = nullptr; static lv_obj_t* s_np = nullptr; static lv_obj_t* s_npLabel = nullptr; static lv_obj_t* s_npPlay = nullptr;
static lv_obj_t* s_toast = nullptr; static lv_timer_t* s_toastTimer = nullptr;
static void (*s_confirmCb)() = nullptr;
static lv_obj_t* s_alertBox = nullptr;
}

// ---------------- shared helpers ----------------
namespace ui_int {
lv_obj_t* newScreen() {
  lv_obj_t* s = lv_obj_create(nullptr); lv_obj_add_style(s, &screen, 0); lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(s, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(s, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(s, big() ? 10 : 6, 0); lv_obj_set_style_pad_row(s, big() ? 8 : 5, 0);
  return s;
}
void load(lv_obj_t* scr) { lv_obj_t* old = lv_scr_act(); if (old == scr) return; lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 120, 0, old != nullptr); }
static void backCb(lv_event_t*) { ui::showHome(); }
lv_obj_t* statusBar(lv_obj_t* scr, const char*, const char* ttl, bool backBtn) {
  lv_obj_t* bar = lv_obj_create(scr); lv_obj_remove_style_all(bar); lv_obj_set_size(bar, LV_PCT(100), chipH() + 8);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(bar, 8, 0);
  if (backBtn) { lv_obj_t* b = makeIconButton(bar, LV_SYMBOL_LEFT, chipH() + 8, backCb); lv_obj_set_ext_click_area(b, 10); }
  else { lv_obj_t* o = orb(bar, chipH() - 4, false); (void)o; }
  lv_obj_t* t = makeLabel(bar, ttl, &title); lv_obj_set_style_text_font(t, big() ? &lv_font_montserrat_20 : &lv_font_montserrat_18, 0); lv_obj_set_flex_grow(t, 1); lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  return bar;
}
lv_obj_t* scrollList(lv_obj_t* scr) {
  lv_obj_t* list = lv_obj_create(scr); lv_obj_remove_style_all(list); lv_obj_set_width(list, LV_PCT(100)); lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(list, big() ? 8 : 6, 0); lv_obj_set_style_pad_bottom(list, 6, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER); lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  return list;
}
lv_obj_t* sectionLabel(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = makeLabel(parent, text, &caption); lv_obj_set_style_pad_top(l, big() ? 8 : 5, 0); lv_obj_set_style_pad_left(l, 6, 0); return l;
}
lv_obj_t* sectionCard(lv_obj_t* parent, const char* ttl) {
  lv_obj_t* c = makeCard(parent); lv_obj_set_style_pad_row(c, 0, 0);
  lv_obj_t* l = makeLabel(c, ttl, &body); lv_obj_set_style_text_font(l, big() ? &lv_font_montserrat_18 : &lv_font_montserrat_16, 0); lv_obj_set_style_pad_bottom(l, big() ? 6 : 4, 0);
  return c;
}
lv_obj_t* row(lv_obj_t* parent, const char* label) {
  lv_obj_t* r = lv_obj_create(parent); lv_obj_remove_style_all(r);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_style_min_height(r, rowH(), 0); lv_obj_set_style_pad_ver(r, big() ? 6 : 4, 0);
  lv_obj_set_style_border_color(r, lv_color_hex(MU_LINE), 0); lv_obj_set_style_border_width(r, 1, 0); lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(r, 8, 0);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = makeLabel(r, label, &body); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_flex_grow(l, 1);
  return r;
}
lv_obj_t* addSwitch(lv_obj_t* parent, const char* label, bool on, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = row(parent, label);
  lv_obj_t* sw = lv_switch_create(r); lv_obj_set_size(sw, big() ? 60 : 44, big() ? 32 : 24);
  lv_obj_set_style_bg_color(sw, lv_color_hex(MU_RAISED), 0); lv_obj_set_style_bg_color(sw, lv_color_hex(MU_TEAL), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(sw, lv_color_hex(MU_TEXT), LV_PART_KNOB); lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  if (cb) lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, ud);
  return sw;
}
lv_obj_t* addDropdown(lv_obj_t* parent, const char* label, const char* opts, uint16_t sel, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = row(parent, label);
  lv_obj_t* dd = lv_dropdown_create(r); lv_dropdown_set_options(dd, opts); lv_dropdown_set_selected(dd, sel);
  lv_obj_set_width(dd, big() ? 220 : 136); lv_obj_set_style_bg_color(dd, lv_color_hex(MU_SURFACE2), 0); lv_obj_set_style_text_color(dd, lv_color_hex(MU_TEXT), 0);
  lv_obj_set_style_border_width(dd, 0, 0); lv_obj_set_style_radius(dd, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_text_font(dd, fontSmall(), 0); lv_obj_set_style_pad_ver(dd, big() ? 8 : 5, 0);
  lv_obj_t* list = lv_dropdown_get_list(dd);
  lv_obj_set_style_bg_color(list, lv_color_hex(MU_SURFACE2), 0); lv_obj_set_style_text_color(list, lv_color_hex(MU_TEXT), 0); lv_obj_set_style_text_font(list, fontSmall(), 0);
  lv_obj_set_style_border_width(list, 0, 0); lv_obj_set_style_radius(list, 12, 0); lv_obj_set_style_bg_color(list, lv_color_hex(MU_BLUE), LV_PART_SELECTED | LV_STATE_CHECKED);
  if (cb) lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, ud);
  return dd;
}
lv_obj_t* addSlider(lv_obj_t* parent, const char* label, int min, int max, int val, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = row(parent, label);
  lv_obj_t* s = lv_slider_create(r); lv_slider_set_range(s, min, max); lv_slider_set_value(s, val, LV_ANIM_OFF);
  lv_obj_set_width(s, big() ? 190 : 110); lv_obj_set_height(s, big() ? 10 : 6);
  lv_obj_set_style_bg_color(s, lv_color_hex(MU_RAISED), 0); lv_obj_set_style_bg_color(s, lv_color_hex(MU_TEAL), LV_PART_INDICATOR); lv_obj_set_style_bg_grad_color(s, lv_color_hex(MU_BLUE), LV_PART_INDICATOR); lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, lv_color_hex(MU_TEXT), LV_PART_KNOB); lv_obj_set_style_pad_all(s, big() ? 7 : 5, LV_PART_KNOB);
  if (cb) lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, ud);
  return s;
}
lv_obj_t* addAction(lv_obj_t* parent, const char* label, const char* value, lv_event_cb_t cb, void* ud) {
  lv_obj_t* r = row(parent, label);
  if (cb) { lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_style_bg_color(r, lv_color_hex(MU_SURFACE2), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_PRESSED); lv_obj_set_style_radius(r, 8, 0); }
  lv_obj_t* v = makeLabel(r, value && *value ? value : (cb ? LV_SYMBOL_RIGHT : ""), &caption);
  lv_label_set_long_mode(v, LV_LABEL_LONG_DOT); lv_obj_set_style_max_width(v, big() ? 250 : 150, 0); lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  if (cb && value && *value) { lv_obj_t* ch = makeLabel(r, LV_SYMBOL_RIGHT, &caption); lv_obj_set_style_text_color(ch, lv_color_hex(MU_DIM), 0); }
  if (cb) lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, ud);
  return r;
}
static void confirmEv(lv_event_t* e) {
  lv_obj_t* mb = lv_event_get_current_target(e); const char* b = lv_msgbox_get_active_btn_text(mb); bool yes = b && !strcmp(b, "Yes");
  lv_msgbox_close_async(mb); auto cb = ui::s_confirmCb; ui::s_confirmCb = nullptr; if (yes && cb) cb();
}
void confirm(const char* ttl, const char* text, void (*onYes)()) {
  static const char* btns[] = {"Yes", "No", ""};
  ui::s_confirmCb = onYes;
  lv_obj_t* mb = lv_msgbox_create(nullptr, ttl, text, btns, false);
  lv_obj_set_style_bg_color(mb, lv_color_hex(MU_SURFACE), 0); lv_obj_set_style_text_color(mb, lv_color_hex(MU_TEXT), 0); lv_obj_set_style_border_width(mb, 0, 0);
  lv_obj_set_style_radius(mb, 16, 0); lv_obj_set_width(mb, big() ? 360 : 240); lv_obj_set_style_pad_all(mb, big() ? 16 : 12, 0);
  lv_obj_set_style_shadow_width(mb, 30, 0); lv_obj_set_style_shadow_color(mb, lv_color_hex(0x000000), 0); lv_obj_set_style_shadow_opa(mb, LV_OPA_60, 0);
  lv_obj_t* bm = lv_msgbox_get_btns(mb); lv_obj_set_style_bg_color(bm, lv_color_hex(MU_SURFACE2), LV_PART_ITEMS); lv_obj_set_style_text_color(bm, lv_color_hex(MU_TEXT), LV_PART_ITEMS);
  lv_obj_set_style_radius(bm, LV_RADIUS_CIRCLE, LV_PART_ITEMS); lv_obj_set_style_border_width(bm, 0, LV_PART_ITEMS); lv_obj_set_height(bm, btnH()); lv_obj_set_style_pad_gap(bm, 6, 0);
  lv_obj_add_event_cb(mb, confirmEv, LV_EVENT_VALUE_CHANGED, nullptr); lv_obj_center(mb);
}
}  // namespace ui_int

namespace ui {
using namespace ui_int;

// ---------------- boot ----------------
void showBoot() {
  s_boot = newScreen(); lv_obj_set_flex_align(s_boot, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_row(s_boot, big() ? 8 : 4, 0);
  orb(s_boot, big() ? 72 : 56);
  lv_obj_t* name = makeLabel(s_boot, "Musecorder", &title); lv_obj_set_style_text_font(name, big() ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
  lv_obj_t* tag = makeLabel(s_boot, "Your Muse, in your hand.", &caption);
  lv_obj_t* box = lv_obj_create(s_boot); lv_obj_remove_style_all(box); lv_obj_set_size(box, LV_PCT(86), big() ? 84 : 58); lv_obj_set_style_pad_top(box, 4, 0);
  s_bootLog = lv_label_create(box); lv_label_set_text(s_bootLog, s_bootText.c_str()); lv_obj_set_width(s_bootLog, LV_PCT(100)); lv_label_set_long_mode(s_bootLog, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(s_bootLog, fontLabel(), 0); lv_obj_set_style_text_color(s_bootLog, lv_color_hex(MU_DIM), 0); lv_obj_set_style_text_align(s_bootLog, LV_TEXT_ALIGN_CENTER, 0);
  (void)tag; lv_scr_load(s_boot);
}
void bootLog(const char* line) {
  if (!s_bootLog) return;
  s_bootText += line; s_bootText += "\n";
  int lines = 0; for (int i = s_bootText.length() - 1; i >= 0; i--) if (s_bootText[i] == '\n' && ++lines > (big() ? 5 : 4)) { s_bootText = s_bootText.substring(i + 1); break; }
  lv_label_set_text(s_bootLog, s_bootText.c_str()); lv_timer_handler();
}

// ---------------- toast ----------------
static void toastHide(lv_timer_t*) { if (s_toast) { lv_obj_del(s_toast); s_toast = nullptr; } s_toastTimer = nullptr; }
void toast(const char* msg, bool error) {
  if (s_toast) { lv_obj_del(s_toast); s_toast = nullptr; }
  if (s_toastTimer) { lv_timer_del(s_toastTimer); s_toastTimer = nullptr; }
  s_toast = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(s_toast); lv_obj_add_style(s_toast, &pill, 0);
  lv_obj_set_style_bg_color(s_toast, lv_color_hex(error ? 0x3a1a24 : MU_SURFACE2), 0); lv_obj_set_style_pad_ver(s_toast, big() ? 10 : 7, 0);
  lv_obj_set_style_shadow_width(s_toast, 24, 0); lv_obj_set_style_shadow_color(s_toast, lv_color_hex(0x000000), 0); lv_obj_set_style_shadow_opa(s_toast, LV_OPA_50, 0);
  lv_obj_set_width(s_toast, LV_SIZE_CONTENT); lv_obj_set_style_max_width(s_toast, LV_PCT(94), 0); lv_obj_set_height(s_toast, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_toast, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(s_toast, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(s_toast, 8, 0);
  lv_obj_t* ic = makeLabel(s_toast, error ? LV_SYMBOL_WARNING : LV_SYMBOL_OK, &body); lv_obj_set_style_text_color(ic, lv_color_hex(error ? MU_DANGER : MU_TEAL), 0); lv_obj_set_style_text_font(ic, fontSmall(), 0);
  lv_obj_t* l = makeLabel(s_toast, msg, &body); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_style_max_width(l, displayW() - (big() ? 100 : 76), 0); lv_obj_set_style_text_font(l, fontSmall(), 0);
  lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -(dockH() + 10));
  s_toastTimer = lv_timer_create(toastHide, error ? 4500 : 2500, nullptr); lv_timer_set_repeat_count(s_toastTimer, 1);
}

// ---------------- home ----------------
static void setChip(lv_obj_t* chip, const char* icon, const char* text) { lv_label_set_text(lv_obj_get_child(chip, 0), icon); lv_label_set_text(lv_obj_get_child(chip, 1), text); }
static String modelName() {
  String m = app::currentModel; if (!m.length()) return "Pick a model";
  if (m.startsWith("muse-code/")) return "Muse Code"; int slash = m.indexOf('/'); if (slash > 0) m = m.substring(slash + 1);
  m.replace("muse-spark-", "Muse Spark "); return m;
}
static void homeRefreshChips() {
  if (!s_home) return;
  setChip(s_modelChip, LV_SYMBOL_CHARGE, modelName().c_str());
  setChip(s_chatChip, LV_SYMBOL_LIST, app::currentChatTitle.length() ? app::currentChatTitle.c_str() : "Chats");
  bool linked = app::bridgeLinked();
  String lk = linked ? app::linkSummary() : String("Offline"); if (linked && blehid::connected() && app::linkSummary() != "BLE") lk += " +BLE";
  if (linked) { lk.toLowerCase(); lk[0] = toupper(lk[0]); }
  lv_label_set_text(lv_obj_get_child(s_linkPill, 1), lk.c_str());
  lv_obj_set_style_bg_color(s_linkDot, lv_color_hex(linked ? MU_OK : MU_WARN), 0);
  bool live = app::dictating || app::capturing;
  lv_obj_t* di = lv_obj_get_child(s_dictBtn, 0); lv_obj_t* dl = lv_obj_get_child(s_dictBtn, 1);
  lv_label_set_text(di, live ? LV_SYMBOL_STOP : LV_SYMBOL_AUDIO); lv_label_set_text(dl, live ? "Stop" : (g_settings.dictSource == 1 ? "Talk" : "Dictate"));
  lv_obj_set_style_text_color(di, lv_color_hex(live ? MU_WARN : MU_TEXT), 0);
  if (app::streaming) lv_obj_clear_flag(s_stopBtn, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s_stopBtn, LV_OBJ_FLAG_HIDDEN);
  if (app::hasReply && !app::streaming) { lv_obj_clear_flag(s_pasteBtn, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_copyBtn, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_add_flag(s_pasteBtn, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_copyBtn, LV_OBJ_FLAG_HIDDEN); }
  orbSetMode(s_orb, !linked ? Orb::Offline : app::alert.id.length() ? Orb::Attention : (app::streaming || app::capturing) ? Orb::Busy : Orb::Idle);
}
static void emptyHint(lv_obj_t* parent, const char* icon, const char* text) {
  lv_obj_t* r = lv_obj_create(parent); lv_obj_remove_style_all(r); lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(r, 8, 0);
  lv_obj_t* i = makeLabel(r, icon, &caption); lv_obj_set_style_text_color(i, lv_color_hex(MU_TEAL), 0);
  makeLabel(r, text, &caption);
}
// Long labels get quadratic in LVGL; keep each label to a paragraph. One top-level child of s_msgList per chunk.
static const size_t kChunkChars = 220;
static lv_obj_t* addChunk(const app::Msg& m, const String& text, bool first) {
  if (m.role == 0) {   // user: right-aligned bubble
    lv_obj_t* r = lv_obj_create(s_msgList); lv_obj_remove_style_all(r); lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); if (first) lv_obj_set_style_pad_top(r, big() ? 6 : 4, 0);
    lv_obj_t* b = lv_obj_create(r); lv_obj_remove_style_all(b); lv_obj_set_style_bg_color(b, lv_color_hex(MU_BUBBLE), 0); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, big() ? 18 : 14, 0); lv_obj_set_style_pad_ver(b, big() ? 8 : 6, 0); lv_obj_set_style_pad_hor(b, big() ? 12 : 10, 0);
    lv_obj_set_width(b, LV_SIZE_CONTENT); lv_obj_set_style_max_width(b, LV_PCT(84), 0); lv_obj_set_height(b, LV_SIZE_CONTENT); lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* l = lv_label_create(b); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, fontBody(), 0); lv_obj_set_style_text_color(l, lv_color_hex(MU_TEXT), 0); lv_label_set_text(l, text.c_str());
    lv_coord_t maxw = (lv_coord_t)(displayW() * 0.84f) - (big() ? 24 : 20);
    lv_point_t sz; lv_txt_get_size(&sz, text.c_str(), fontBody(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(l, sz.x < maxw ? sz.x : maxw);   // short messages keep a tight bubble; long ones wrap at 84%
    return r;
  }
  lv_obj_t* r = lv_obj_create(s_msgList); lv_obj_remove_style_all(r); lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START); lv_obj_set_style_pad_column(r, 8, 0); if (first) lv_obj_set_style_pad_top(r, big() ? 6 : 4, 0);
  lv_obj_t* dot = lv_obj_create(r); lv_obj_remove_style_all(dot); lv_obj_set_size(dot, big() ? 14 : 10, big() ? 14 : 10); lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  if (first) { gradient(dot, false); if (m.role == 2) { lv_obj_set_style_bg_color(dot, lv_color_hex(MU_DANGER), 0); lv_obj_set_style_bg_grad_color(dot, lv_color_hex(MU_DANGER), 0); } } else lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
  lv_obj_set_style_translate_y(dot, big() ? 4 : 3, 0);
  lv_obj_t* l = lv_label_create(r); lv_obj_set_flex_grow(l, 1); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(l, fontBody(), 0); lv_obj_set_style_text_color(l, lv_color_hex(m.role == 2 ? MU_DANGER : MU_TEXT), 0); lv_obj_set_style_text_line_space(l, big() ? 3 : 2, 0);
  lv_label_set_text(l, text.c_str());
  return r;
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
    if (part.length() || n == 0) { addChunk(m, part, n == 0); n++; }
    start = end; if (end >= t.length()) break;
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
    orb(s_empty, big() ? 60 : 34);
    lv_obj_t* h = makeLabel(s_empty, app::bridgeLinked() ? "Hi, I'm your Muse device." : "Not linked yet", &body); lv_obj_set_style_text_font(h, big() ? &lv_font_montserrat_20 : &lv_font_montserrat_16, 0);
    if (app::bridgeLinked()) {
      emptyHint(s_empty, LV_SYMBOL_KEYBOARD, "Ask anything, or type into the Mac");
      emptyHint(s_empty, LV_SYMBOL_LIST, "Apps: tools Muse built for this device");
    } else {
      emptyHint(s_empty, LV_SYMBOL_USB, "Plug in and run: musecorder serve");
      emptyHint(s_empty, LV_SYMBOL_BLUETOOTH, "or Settings > Bluetooth link");
    }
    return;
  }
  lv_obj_add_flag(s_empty, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(s_msgList, LV_OBJ_FLAG_HIDDEN);
  if (deltaOnly && s_msgCount == msgs.size() && s_msgCount > 0) {
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
static void evPaste(lv_event_t*) { app::pasteLastReply(); toast("Typing the reply into the Mac"); }
static void evCopy(lv_event_t*) { app::copyLastReply(); toast("Copied to the Mac clipboard"); }
static void evPageUp(lv_event_t*) { lv_obj_scroll_by_bounded(s_transcriptPanel, 0, (lv_obj_get_height(s_transcriptPanel) * 3) / 4, LV_ANIM_OFF); }
static void evPageDown(lv_event_t*) { lv_obj_scroll_by_bounded(s_transcriptPanel, 0, -(lv_obj_get_height(s_transcriptPanel) * 3) / 4, LV_ANIM_OFF); }
static void evNewChat(lv_event_t* e) { if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) { app::newChat(); toast("New chat"); } }
static void evInbox(lv_event_t*) { showInbox(); }
static void evNpPrev(lv_event_t*) { app::mediaKey(0xB6); }
static void evNpPlay(lv_event_t*) { app::mediaKey(0xCD); }
static void evNpNext(lv_event_t*) { app::mediaKey(0xB5); }
static void homeRefreshBell() {
  if (!s_bell) return;
  if (app::inboxUnread) { lv_label_set_text_fmt(lv_obj_get_child(s_bell, 1), "%u", app::inboxUnread); lv_obj_set_style_text_color(lv_obj_get_child(s_bell, 0), lv_color_hex(MU_WARN), 0); lv_obj_clear_flag(s_bell, LV_OBJ_FLAG_HIDDEN); }
  else lv_obj_add_flag(s_bell, LV_OBJ_FLAG_HIDDEN);
}
static void homeRefreshNp() {
  if (!s_np) return;
  if (!app::np.app.length() || !app::np.title.length()) { lv_obj_add_flag(s_np, LV_OBJ_FLAG_HIDDEN); return; }
  lv_obj_clear_flag(s_np, LV_OBJ_FLAG_HIDDEN);
  String t = app::np.title; if (app::np.artist.length()) t += "  " MU_BULLET "  " + app::np.artist;
  lv_label_set_text(s_npLabel, t.c_str()); lv_label_set_text(lv_obj_get_child(s_npPlay, 0), app::np.playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}
static lv_obj_t* dockBtn(lv_obj_t* dock, const char* sym, const char* text, lv_event_cb_t cb) {
  lv_obj_t* b = lv_btn_create(dock); lv_obj_remove_style_all(b); lv_obj_set_style_radius(b, 12, 0); lv_obj_set_style_bg_color(b, lv_color_hex(MU_RAISED), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_flex_grow(b, 1); lv_obj_set_height(b, LV_PCT(100)); lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_row(b, big() ? 3 : 1, 0);
  lv_obj_t* i = lv_label_create(b); lv_label_set_text(i, sym); lv_obj_set_style_text_font(i, fontIcon(), 0); lv_obj_set_style_text_color(i, lv_color_hex(MU_TEXT), 0);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text); lv_obj_set_style_text_font(l, fontLabel(), 0); lv_obj_set_style_text_color(l, lv_color_hex(MU_MUTED), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}
static lv_obj_t* miniBtn(lv_obj_t* parent, const char* sym, lv_event_cb_t cb, uint32_t color) {
  lv_obj_t* b = makeIconButton(parent, sym, big() ? 36 : 26, cb); lv_obj_set_style_text_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(MU_SURFACE), 0); lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0); return b;
}

void showHome() {
  s_home = newScreen(); lv_obj_set_style_pad_row(s_home, big() ? 6 : 4, 0);
  const lv_coord_t W = displayW();
  // top bar: orb + wordmark | bell · link pill
  lv_obj_t* bar = lv_obj_create(s_home); lv_obj_remove_style_all(bar); lv_obj_set_size(bar, LV_PCT(100), chipH() + 6);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(bar, 6, 0);
  s_orb = orb(bar, chipH() - 4);
  lv_obj_t* wm = makeLabel(bar, "Musecorder", &body); lv_obj_set_style_text_font(wm, big() ? &lv_font_montserrat_20 : &lv_font_montserrat_16, 0); lv_obj_set_flex_grow(wm, 1);
  s_bell = makeChip(bar, LV_SYMBOL_BELL, "", evInbox, W / 3); lv_obj_set_style_text_color(lv_obj_get_child(s_bell, 0), lv_color_hex(MU_WARN), 0);
  s_linkPill = makeChip(bar, "", "", evSettings, W / 2);
  s_linkDot = lv_obj_create(s_linkPill); lv_obj_remove_style_all(s_linkDot); lv_obj_set_size(s_linkDot, big() ? 10 : 8, big() ? 10 : 8); lv_obj_set_style_radius(s_linkDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_linkDot, LV_OPA_COVER, 0); lv_obj_move_to_index(s_linkDot, 0); lv_obj_del(lv_obj_get_child(s_linkPill, 1));   // children: dot, label
  // chips: model · chat
  lv_obj_t* chips = lv_obj_create(s_home); lv_obj_remove_style_all(chips); lv_obj_set_size(chips, LV_PCT(100), big() ? 36 : 26);
  lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(chips, 6, 0);
  s_modelChip = makeChip(chips, LV_SYMBOL_CHARGE, "", evModels, W / 2 - 30);
  s_chatChip = makeChip(chips, LV_SYMBOL_LIST, "", evChats, W / 2 - 34); lv_obj_add_event_cb(s_chatChip, evNewChat, LV_EVENT_LONG_PRESSED, nullptr);
  lv_obj_t* sp = lv_obj_create(chips); lv_obj_remove_style_all(sp); lv_obj_set_height(sp, 1); lv_obj_set_flex_grow(sp, 1);
  miniBtn(chips, LV_SYMBOL_UP, evPageUp, MU_MUTED); miniBtn(chips, LV_SYMBOL_DOWN, evPageDown, MU_MUTED);
  // conversation
  s_transcriptPanel = lv_obj_create(s_home); lv_obj_remove_style_all(s_transcriptPanel); lv_obj_set_width(s_transcriptPanel, LV_PCT(100)); lv_obj_set_flex_grow(s_transcriptPanel, 1);
  lv_obj_set_style_pad_hor(s_transcriptPanel, 4, 0); lv_obj_set_scroll_dir(s_transcriptPanel, LV_DIR_VER); lv_obj_set_scrollbar_mode(s_transcriptPanel, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_clear_flag(s_transcriptPanel, LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_ELASTIC);
  s_msgList = lv_obj_create(s_transcriptPanel); lv_obj_remove_style_all(s_msgList); lv_obj_set_width(s_msgList, LV_PCT(100)); lv_obj_set_height(s_msgList, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_msgList, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_msgList, big() ? 6 : 4, 0); lv_obj_clear_flag(s_msgList, LV_OBJ_FLAG_SCROLLABLE);
  s_msgCount = 0; s_lastMsgLabels = 0;
  s_empty = lv_obj_create(s_transcriptPanel); lv_obj_remove_style_all(s_empty); lv_obj_set_size(s_empty, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(s_empty, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(s_empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_row(s_empty, big() ? 6 : 3, 0);
  // floating mini buttons
  lv_obj_t* fl = lv_obj_create(s_home); lv_obj_remove_style_all(fl); lv_obj_add_flag(fl, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(fl, LV_SIZE_CONTENT, big() ? 38 : 28); lv_obj_set_flex_flow(fl, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(fl, 4, 0);
  lv_obj_align(fl, LV_ALIGN_BOTTOM_RIGHT, -(big() ? 14 : 8), -(dockH() + (big() ? 12 : 8)));
  s_stopBtn = miniBtn(fl, LV_SYMBOL_STOP, evStop, MU_WARN); s_copyBtn = miniBtn(fl, LV_SYMBOL_COPY, evCopy, MU_TEAL); s_pasteBtn = miniBtn(fl, LV_SYMBOL_PASTE, evPaste, MU_OK);
  // now playing strip
  s_np = lv_obj_create(s_home); lv_obj_remove_style_all(s_np); lv_obj_add_style(s_np, &pill, 0); lv_obj_set_style_pad_ver(s_np, 2, 0); lv_obj_set_style_pad_hor(s_np, big() ? 10 : 8, 0);
  lv_obj_set_size(s_np, LV_PCT(100), chipH() + (big() ? 14 : 10)); lv_obj_set_flex_flow(s_np, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(s_np, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(s_np, big() ? 8 : 5, 0);
  lv_obj_clear_flag(s_np, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* npIcon = makeLabel(s_np, LV_SYMBOL_AUDIO, &caption); lv_obj_set_style_text_color(npIcon, lv_color_hex(MU_TEAL), 0);
  s_npLabel = makeLabel(s_np, "", &caption); lv_label_set_long_mode(s_npLabel, LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_flex_grow(s_npLabel, 1);
  miniBtn(s_np, LV_SYMBOL_PREV, evNpPrev, MU_MUTED); s_npPlay = miniBtn(s_np, LV_SYMBOL_PLAY, evNpPlay, MU_TEXT); miniBtn(s_np, LV_SYMBOL_NEXT, evNpNext, MU_MUTED);
  lv_obj_add_flag(s_np, LV_OBJ_FLAG_HIDDEN);
  // background applet strip
  if (applets::running() && !applets::onScreen()) {
    lv_obj_t* st = lv_btn_create(s_home); lv_obj_remove_style_all(st); lv_obj_add_style(st, &pill, 0); lv_obj_add_style(st, &pressed, LV_STATE_PRESSED);
    lv_obj_set_size(st, LV_PCT(100), chipH() + (big() ? 12 : 8)); lv_obj_set_style_pad_ver(st, 0, 0); lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(st, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(st, 8, 0);
    lv_obj_t* d = lv_obj_create(st); lv_obj_remove_style_all(d); lv_obj_set_size(d, 10, 10); lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0); gradient(d, false);
    lv_obj_t* l = makeLabel(st, (applets::runningName() + " is running").c_str(), &body); lv_obj_set_style_text_font(l, fontSmall(), 0); lv_obj_set_flex_grow(l, 1);
    makeLabel(st, LV_SYMBOL_RIGHT, &caption);
    lv_obj_add_event_cb(st, [](lv_event_t*) { applets::buildScreen(); }, LV_EVENT_CLICKED, nullptr);
  }
  // dock
  lv_obj_t* dock = lv_obj_create(s_home); lv_obj_remove_style_all(dock); lv_obj_add_style(dock, &card, 0); lv_obj_set_style_radius(dock, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(dock, 3, 0); lv_obj_set_size(dock, LV_PCT(100), dockH()); lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(dock, 2, 0);
  s_dictBtn = dockBtn(dock, LV_SYMBOL_AUDIO, "Dictate", evDict);
  dockBtn(dock, LV_SYMBOL_KEYBOARD, "Ask", evType);
  dockBtn(dock, LV_SYMBOL_LIST, "Apps", evApps);
  dockBtn(dock, LV_SYMBOL_SETTINGS, "Settings", evSettings);
  homeRefreshChips(); homeRefreshTranscript(false); homeRefreshBell(); homeRefreshNp();
  load(s_home);
}

// ---------------- inbox ----------------
static void evInboxOpen(lv_event_t* e) {
  size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i >= app::inbox.size()) return;
  String sid = app::inbox[i].session; if (sid.length()) { app::openSession(sid); toast("Opening session"); showHome(); }
}
static void evInboxClear(lv_event_t*) { app::inboxClear(); showInbox(); }
void showInbox() {
  app::inboxMarkRead();
  lv_obj_t* scr = newScreen(); statusBar(scr, "", "Inbox", true);
  lv_obj_t* list = scrollList(scr);
  if (app::inbox.empty()) { lv_obj_t* l = makeLabel(list, "Nothing yet. Finished turns, questions and permission requests from Muse Code land here.", &muted); lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); }
  for (size_t i = 0; i < app::inbox.size(); i++) {
    const app::Alert& a = app::inbox[i];
    lv_obj_t* b = lv_btn_create(list); lv_obj_remove_style_all(b); lv_obj_add_style(b, &card, 0); lv_obj_add_style(b, &pressed, LV_STATE_PRESSED);
    lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(b, 10, 0); lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t* d = lv_obj_create(b); lv_obj_remove_style_all(d); lv_obj_set_size(d, 10, 10); lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(a.kind == "permission" ? MU_WARN : a.kind == "done" ? MU_OK : MU_TEAL), 0); lv_obj_set_style_translate_y(d, 4, 0);
    lv_obj_t* col = lv_obj_create(b); lv_obj_remove_style_all(col); lv_obj_set_flex_grow(col, 1); lv_obj_set_height(col, LV_SIZE_CONTENT); lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_t* h = makeLabel(col, (a.title + (a.project.length() ? "  " MU_BULLET "  " + a.project : "")).c_str(), &body); lv_obj_set_width(h, LV_PCT(100)); lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);
    lv_obj_t* x = makeLabel(col, a.text.c_str(), &caption); lv_obj_set_width(x, LV_PCT(100)); lv_label_set_long_mode(x, LV_LABEL_LONG_WRAP); lv_obj_set_style_max_height(x, big() ? 70 : 48, 0);
    lv_obj_add_event_cb(b, evInboxOpen, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }
  if (!app::inbox.empty()) { lv_obj_t* c = makeButton(scr, LV_SYMBOL_TRASH "  Clear inbox"); lv_obj_set_size(c, LV_PCT(100), btnH()); lv_obj_add_event_cb(c, evInboxClear, LV_EVENT_CLICKED, nullptr); }
  load(scr);
}

// ---------------- alerts (Muse Code permission / notices) ----------------
static void alertBtn(lv_event_t* e) { app::replyAlert((const char*)lv_event_get_user_data(e)); }
static void alertOpen(lv_event_t*) {
  String sid = app::alert.session; bool twoWay = app::alert.twoWay;
  if (sid.length()) { app::openSession(sid); toast("Opening that Muse Code session"); }
  if (!twoWay) app::replyAlert("dismiss");
  showHome();
}
void hideAlert() { if (s_alertBox) { lv_obj_del(s_alertBox); s_alertBox = nullptr; } }
void showAlert() {
  hideAlert();
  const app::Alert& a = app::alert; if (!a.id.length()) return;
  uint32_t accent = a.twoWay ? MU_WARN : a.kind == "done" ? MU_OK : MU_TEAL;
  s_alertBox = lv_obj_create(lv_layer_top()); lv_obj_remove_style_all(s_alertBox); lv_obj_add_style(s_alertBox, &card, 0);
  lv_obj_set_style_bg_color(s_alertBox, lv_color_hex(MU_SURFACE2), 0); lv_obj_set_style_shadow_width(s_alertBox, 30, 0); lv_obj_set_style_shadow_color(s_alertBox, lv_color_hex(0x000000), 0); lv_obj_set_style_shadow_opa(s_alertBox, LV_OPA_70, 0);
  lv_obj_set_width(s_alertBox, LV_PCT(94)); lv_obj_set_height(s_alertBox, LV_SIZE_CONTENT); lv_obj_set_flex_flow(s_alertBox, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(s_alertBox, big() ? 6 : 4, 0);
  lv_obj_t* hdr = lv_obj_create(s_alertBox); lv_obj_remove_style_all(hdr); lv_obj_set_size(hdr, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(hdr, 8, 0);
  lv_obj_t* o = orb(hdr, big() ? 22 : 16, false); (void)o;
  lv_obj_t* h = makeLabel(hdr, a.twoWay ? "Muse Code needs your OK" : (a.kind == "done" ? "Muse Code finished" : "Muse Code"), &caption); lv_obj_set_style_text_color(h, lv_color_hex(accent), 0);
  lv_obj_t* t = makeLabel(s_alertBox, a.title.c_str(), &body); lv_obj_set_width(t, LV_PCT(100)); lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_t* box = lv_obj_create(s_alertBox); lv_obj_remove_style_all(box); lv_obj_set_width(box, LV_PCT(100)); lv_obj_set_style_max_height(box, (lv_coord_t)(displayH() * 0.38f), 0); lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_scroll_dir(box, LV_DIR_VER); lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_t* x = makeLabel(box, a.text.c_str(), &caption); lv_obj_set_width(x, LV_PCT(100)); lv_label_set_long_mode(x, LV_LABEL_LONG_WRAP);
  if (a.project.length()) { lv_obj_t* pr = makeLabel(s_alertBox, a.project.c_str(), &caption); lv_obj_set_style_text_color(pr, lv_color_hex(MU_DIM), 0); }
  lv_obj_t* r = lv_obj_create(s_alertBox); lv_obj_remove_style_all(r); lv_obj_set_size(r, LV_PCT(100), btnH() + 2); lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(r, 6, 0);
  if (a.twoWay) {
    lv_obj_t* ok = makeButton(r, LV_SYMBOL_OK "  Allow", true); lv_obj_set_flex_grow(ok, 1); lv_obj_set_height(ok, btnH() + 2); lv_obj_add_event_cb(ok, alertBtn, LV_EVENT_CLICKED, (void*)"allow");
    lv_obj_t* no = makeButton(r, LV_SYMBOL_CLOSE "  Deny"); lv_obj_set_flex_grow(no, 1); lv_obj_set_height(no, btnH() + 2); lv_obj_add_event_cb(no, alertBtn, LV_EVENT_CLICKED, (void*)"deny");
  } else {
    lv_obj_t* ok = makeButton(r, "OK", true); lv_obj_set_flex_grow(ok, 1); lv_obj_set_height(ok, btnH() + 2); lv_obj_add_event_cb(ok, alertBtn, LV_EVENT_CLICKED, (void*)"dismiss");
  }
  if (a.session.length()) { lv_obj_t* op = makeButton(r, LV_SYMBOL_EDIT " Open"); lv_obj_set_width(op, big() ? 120 : 84); lv_obj_set_height(op, btnH() + 2); lv_obj_add_event_cb(op, alertOpen, LV_EVENT_CLICKED, nullptr); }
  lv_obj_center(s_alertBox);
  orbSetMode(s_orb, Orb::Attention);
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
    case app::Ev::AlertClear: hideAlert(); if (onHome) homeRefreshChips(); break;
    case app::Ev::NowPlaying: if (onHome) homeRefreshNp(); break;
    case app::Ev::Inbox: if (onHome) homeRefreshBell(); break;
    case app::Ev::Provisioned: toast(!strcmp(arg, "wifi") ? "Wi-Fi set up from the Mac" : "Bridge credentials installed"); break;
  }
}

void begin() { theme::init(); app::setUiCb(onAppEvent); }
void tick() { lv_timer_handler(); }
}  // namespace ui
