// LVGL renderer for applets + the launcher screen.
#include "applet.h"
#include "../ui/ui.h"
#include "../ui/theme.h"
#include "../config.h"
#include "../hal/display.h"
#include "../hal/led.h"

using namespace theme; using namespace ui_int;

namespace applets {
struct Binding { lv_obj_t* obj; uint8_t kind; JsonObject w; };   // kind: 0 text 1 bar 2 hidden 3 toggle 4 slider 5 input 6 log
static std::vector<Binding> s_bind;
static lv_obj_t* s_screen = nullptr;
static JsonObject s_inputW;   // input widget awaiting the keyboard

static bool resolverCb(const String& n, expr::Value& o, void*) { return resolve(n, o); }
static String R(JsonVariant v) { if (v.is<const char*>()) return expr::render(v.as<const char*>(), resolverCb, nullptr); if (v.isNull()) return ""; return expr::Value::num(v.as<double>()).toStr(); }
static double N(JsonVariant v, double def) { if (v.isNull()) return def; if (v.is<const char*>()) return expr::renderTyped(v.as<const char*>(), resolverCb, nullptr).toNum(); return v.as<double>(); }

const char* symbolFor(const String& n) {
  struct S { const char* k; const char* v; };
  static const S tab[] = {{"plus", LV_SYMBOL_PLUS}, {"minus", LV_SYMBOL_MINUS}, {"ok", LV_SYMBOL_OK}, {"close", LV_SYMBOL_CLOSE}, {"wifi", LV_SYMBOL_WIFI}, {"bell", LV_SYMBOL_BELL}, {"power", LV_SYMBOL_POWER},
    {"play", LV_SYMBOL_PLAY}, {"pause", LV_SYMBOL_PAUSE}, {"stop", LV_SYMBOL_STOP}, {"list", LV_SYMBOL_LIST}, {"edit", LV_SYMBOL_EDIT}, {"charge", LV_SYMBOL_CHARGE}, {"gps", LV_SYMBOL_GPS}, {"audio", LV_SYMBOL_AUDIO},
    {"image", LV_SYMBOL_IMAGE}, {"eye_open", LV_SYMBOL_EYE_OPEN}, {"eye", LV_SYMBOL_EYE_OPEN}, {"warning", LV_SYMBOL_WARNING}, {"home", LV_SYMBOL_HOME}, {"refresh", LV_SYMBOL_REFRESH}, {"upload", LV_SYMBOL_UPLOAD}, {"download", LV_SYMBOL_DOWNLOAD},
    {"settings", LV_SYMBOL_SETTINGS}, {"keyboard", LV_SYMBOL_KEYBOARD}, {"bluetooth", LV_SYMBOL_BLUETOOTH}, {"usb", LV_SYMBOL_USB}, {"sd_card", LV_SYMBOL_SD_CARD}, {"battery", LV_SYMBOL_BATTERY_FULL}, {"battery_full", LV_SYMBOL_BATTERY_FULL},
    {"left", LV_SYMBOL_LEFT}, {"right", LV_SYMBOL_RIGHT}, {"up", LV_SYMBOL_UP}, {"down", LV_SYMBOL_DOWN}, {"trash", LV_SYMBOL_TRASH}, {"copy", LV_SYMBOL_COPY}, {"paste", LV_SYMBOL_PASTE}, {"save", LV_SYMBOL_SAVE}, {"file", LV_SYMBOL_FILE},
    {"directory", LV_SYMBOL_DIRECTORY}, {"call", LV_SYMBOL_CALL}, {"envelope", LV_SYMBOL_ENVELOPE}, {"volume", LV_SYMBOL_VOLUME_MAX}, {"mute", LV_SYMBOL_MUTE}, {"shuffle", LV_SYMBOL_SHUFFLE}, {"loop", LV_SYMBOL_LOOP}, {"bars", LV_SYMBOL_BARS},
    {"tint", LV_SYMBOL_TINT}, {"cut", LV_SYMBOL_CUT}, {"prev", LV_SYMBOL_PREV}, {"next", LV_SYMBOL_NEXT}, {"drive", LV_SYMBOL_DRIVE}, {"video", LV_SYMBOL_VIDEO}, {"backspace", LV_SYMBOL_BACKSPACE}, {"new_line", LV_SYMBOL_NEW_LINE}};
  for (auto& s : tab) if (n == s.k) return s.v;
  return nullptr;
}

static const lv_font_t* fontFor(const String& size) {
  if (size == "s") return fontSmall(); if (size == "l") return &lv_font_montserrat_20;
  if (size == "xl") return big() ? &lv_font_montserrat_36 : &lv_font_montserrat_28; if (size == "xxl") return &lv_font_montserrat_48;
  if (size == "l") return big() ? &lv_font_montserrat_24 : &lv_font_montserrat_20;
  return fontBody();
}
static uint32_t colorFor(JsonObject w, uint32_t def) {
  String c = R(w["color"]); if (c.startsWith("#") && c.length() == 7) return strtoul(c.c_str() + 1, nullptr, 16);
  String st = R(w["style"]);
  if (st == "muted") return MU_MUTED; if (st == "accent" || st == "mono") return MU_TEAL; if (st == "warn") return MU_WARN; if (st == "ok") return MU_OK; if (st == "error") return MU_DANGER;
  return def;
}
static bool hiddenNow(JsonObject w) { JsonVariant h = w["hidden"]; if (h.isNull()) return false; if (h.is<const char*>()) return expr::renderTyped(h.as<const char*>(), resolverCb, nullptr).truthy(); return h.as<bool>(); }
static void applyHidden(lv_obj_t* o, JsonObject w) { if (hiddenNow(w)) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); }

static void evBtn(lv_event_t* e) {
  uintptr_t ud = (uintptr_t)lv_event_get_user_data(e); size_t doIdx = ud & 0xFFFF, holdIdx = (ud >> 16) & 0xFFFF;
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_LONG_PRESSED && holdIdx != 0xFFFF) { ledTapFlash(); runActionsAt(holdIdx); lv_obj_add_state(lv_event_get_target(e), LV_STATE_USER_1); }
  else if (c == LV_EVENT_CLICKED) {
    if (lv_obj_has_state(lv_event_get_target(e), LV_STATE_USER_1)) { lv_obj_clear_state(lv_event_get_target(e), LV_STATE_USER_1); return; }   // the release after a hold
    if (doIdx != 0xFFFF) { ledTapFlash(); runActionsAt(doIdx); }
  }
}
static void evToggle(lv_event_t* e) {
  size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i >= s_bind.size()) return;
  JsonObject w = s_bind[i].w; setState(R(w["bind"]), expr::Value::boolean(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)));
  runActions(w["on_change"].as<JsonArray>());
}
static void evSlider(lv_event_t* e) {
  size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i >= s_bind.size()) return;
  JsonObject w = s_bind[i].w; setState(R(w["bind"]), expr::Value::num(lv_slider_get_value(lv_event_get_target(e))));
  runActions(w["on_change"].as<JsonArray>());
}
static void inputDone(const char* v) {
  keepAlive(false);
  if (!s_inputW.isNull()) { setState(R(s_inputW["bind"]), expr::Value::str(v)); runActions(s_inputW["on_change"].as<JsonArray>()); }
  s_inputW = JsonObject();
  if (running()) buildScreen(); else ui::showHome();
}
static void inputCancel() { keepAlive(false); s_inputW = JsonObject(); if (running()) buildScreen(); else ui::showHome(); }
static void evInput(lv_event_t* e) {
  size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i >= s_bind.size()) return;
  s_inputW = s_bind[i].w; keepAlive(true);
  String ttl = R(s_inputW["placeholder"]); if (!ttl.length()) ttl = R(s_inputW["bind"]); ttl.toUpperCase();
  ui::showTextEntryEx(ttl.c_str(), getState(R(s_inputW["bind"])).toStr().c_str(), s_inputW["password"] | false, inputDone, inputCancel);
}

static void build(lv_obj_t* parent, JsonObject w, int depth);
static void buildList(lv_obj_t* parent, JsonArray items, int depth) { for (JsonObject w : items) build(parent, w, depth); }

static void build(lv_obj_t* parent, JsonObject w, int depth) {
  String type = (const char*)(w["type"] | "label");
  lv_obj_t* o = nullptr;
  if (type == "label" || type == "value") {
    o = lv_label_create(parent); lv_obj_set_width(o, LV_PCT(100)); lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(o, fontFor(R(w["size"])), 0); lv_obj_set_style_text_color(o, lv_color_hex(colorFor(w, MU_TEXT)), 0);
    String st = R(w["style"]); if (st == "mono") lv_obj_set_style_text_font(o, fontSmall(), 0);
    String al = R(w["align"]); lv_obj_set_style_text_align(o, al == "center" ? LV_TEXT_ALIGN_CENTER : al == "right" ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(o, R(w["text"]).c_str());
    s_bind.push_back({o, 0, w});
  } else if (type == "button") {
    String st = R(w["style"]);
    o = makeButton(parent, "", st == "primary");
    if (st == "danger") { lv_obj_set_style_bg_color(o, lv_color_hex(0x3a1a24), 0); lv_obj_set_style_text_color(o, lv_color_hex(MU_DANGER), 0); }
    if (st == "ghost") { lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(o, 1, 0); lv_obj_set_style_border_color(o, lv_color_hex(MU_LINE), 0); }
    lv_obj_set_style_radius(o, 14, 0);
    int h = (int)N(w["h"], 0); lv_obj_set_height(o, h > 0 ? h : btnH() + 6);
    int grow = (int)N(w["grow"], depth > 0 ? 1 : 0); if (grow > 0) lv_obj_set_flex_grow(o, grow); else lv_obj_set_width(o, LV_PCT(100));
    lv_obj_t* l = lv_obj_get_child(o, 0); lv_label_set_text(l, R(w["text"]).c_str()); lv_obj_set_style_text_font(l, fontFor(R(w["size"]).length() ? R(w["size"]) : "m"), 0); lv_obj_center(l);
    size_t doIdx = w["do"].isNull() ? 0xFFFF : registerActions(w["do"].as<JsonArray>()); size_t holdIdx = w["hold"].isNull() ? 0xFFFF : registerActions(w["hold"].as<JsonArray>());
    void* ud = (void*)(uintptr_t)((doIdx & 0xFFFF) | ((holdIdx & 0xFFFF) << 16));
    lv_obj_add_event_cb(o, evBtn, LV_EVENT_CLICKED, ud); if (holdIdx != 0xFFFF) lv_obj_add_event_cb(o, evBtn, LV_EVENT_LONG_PRESSED, ud);
    s_bind.push_back({l, 0, w});
  } else if (type == "row") {
    o = lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_width(o, LV_PCT(100)); lv_obj_set_height(o, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, (int)N(w["gap"], 4), 0); lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    if (depth < 3) buildList(o, w["items"].as<JsonArray>(), depth + 1);
  } else if (type == "toggle") {
    o = row(parent, R(w["text"]).c_str());
    lv_obj_t* sw = lv_switch_create(o); lv_obj_set_size(sw, big() ? 62 : 50, big() ? 32 : 26);
    lv_obj_set_style_bg_color(sw, lv_color_hex(MU_RAISED), 0); lv_obj_set_style_bg_color(sw, lv_color_hex(MU_TEAL), LV_PART_INDICATOR | LV_STATE_CHECKED); lv_obj_set_style_bg_color(sw, lv_color_hex(MU_TEXT), LV_PART_KNOB);
    if (getState(R(w["bind"])).truthy()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    s_bind.push_back({sw, 3, w}); lv_obj_add_event_cb(sw, evToggle, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)(s_bind.size() - 1));
    s_bind.push_back({lv_obj_get_child(o, 0), 0, w});   // the label text is a template too
  } else if (type == "slider") {
    o = lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_width(o, LV_PCT(100)); lv_obj_set_height(o, LV_SIZE_CONTENT); lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_all(o, 6, 0); lv_obj_set_style_pad_row(o, 8, 0);
    if (!w["text"].isNull()) { lv_obj_t* l = makeLabel(o, R(w["text"]).c_str(), &caption); s_bind.push_back({l, 0, w}); }
    lv_obj_t* s = lv_slider_create(o); lv_slider_set_range(s, (int)N(w["min"], 0), (int)N(w["max"], 100)); lv_slider_set_value(s, (int)getState(R(w["bind"])).toNum(), LV_ANIM_OFF);
    lv_obj_set_width(s, LV_PCT(96)); lv_obj_set_height(s, big() ? 14 : 10); lv_obj_set_style_bg_color(s, lv_color_hex(MU_RAISED), 0); lv_obj_set_style_bg_color(s, lv_color_hex(MU_TEAL), LV_PART_INDICATOR); lv_obj_set_style_bg_grad_color(s, lv_color_hex(MU_BLUE), LV_PART_INDICATOR); lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_HOR, LV_PART_INDICATOR); lv_obj_set_style_bg_color(s, lv_color_hex(MU_TEXT), LV_PART_KNOB); lv_obj_set_style_pad_all(s, big() ? 8 : 6, LV_PART_KNOB);
    s_bind.push_back({s, 4, w}); lv_obj_add_event_cb(s, evSlider, LV_EVENT_RELEASED, (void*)(uintptr_t)(s_bind.size() - 1));
  } else if (type == "bar") {
    o = lv_bar_create(parent); lv_obj_set_size(o, LV_PCT(100), big() ? 18 : 12); lv_bar_set_range(o, (int)N(w["min"], 0), (int)N(w["max"], 100));
    lv_obj_set_style_bg_color(o, lv_color_hex(MU_SURFACE2), 0); lv_obj_set_style_bg_color(o, lv_color_hex(colorFor(w, MU_TEAL)), LV_PART_INDICATOR); if (w["color"].isNull()) { lv_obj_set_style_bg_grad_color(o, lv_color_hex(MU_BLUE), LV_PART_INDICATOR); lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, LV_PART_INDICATOR); } lv_obj_set_style_radius(o, 8, 0); lv_obj_set_style_radius(o, 8, LV_PART_INDICATOR);
    lv_bar_set_value(o, (int)N(w["value"], 0), LV_ANIM_OFF);
    s_bind.push_back({o, 1, w});
  } else if (type == "input") {
    o = lv_btn_create(parent); lv_obj_remove_style_all(o); lv_obj_add_style(o, &card, 0); lv_obj_add_style(o, &pressed, LV_STATE_PRESSED);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_style_min_height(o, rowH(), 0); lv_obj_set_style_radius(o, 14, 0);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(o, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* l = makeLabel(o, "", &body); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_flex_grow(l, 1);
    lv_obj_t* ic = makeLabel(o, LV_SYMBOL_EDIT, &caption); (void)ic;
    s_bind.push_back({l, 5, w}); lv_obj_add_event_cb(o, evInput, LV_EVENT_CLICKED, (void*)(uintptr_t)(s_bind.size() - 1));
  } else if (type == "log") {
    o = lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_add_style(o, &card, 0); lv_obj_set_width(o, LV_PCT(100));
    int lines = (int)N(w["lines"], 6); lv_obj_set_height(o, lines * (big() ? 18 : 11) + 14);
    lv_obj_set_scroll_dir(o, LV_DIR_VER); lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF); lv_obj_set_style_pad_all(o, 5, 0);
    lv_obj_t* l = lv_label_create(o); lv_obj_set_width(l, LV_PCT(100)); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, big() ? &lv_font_unscii_16 : &lv_font_unscii_8, 0); lv_obj_set_style_text_color(l, lv_color_hex(colorFor(w, MU_OK)), 0);
    s_bind.push_back({l, 6, w});
  } else if (type == "chart") {
    o = lv_chart_create(parent); lv_obj_set_size(o, LV_PCT(100), (int)N(w["h"], big() ? 140 : 80));
    lv_chart_set_type(o, R(w["kind"]) == "bar" ? LV_CHART_TYPE_BAR : LV_CHART_TYPE_LINE);
    int pts = (int)N(w["points"], 30); if (pts < 2) pts = 2; if (pts > 120) pts = 120; lv_chart_set_point_count(o, pts);
    lv_chart_set_range(o, LV_CHART_AXIS_PRIMARY_Y, (int)N(w["min"], 0), (int)N(w["max"], 100));
    lv_chart_set_div_line_count(o, 3, 0); lv_chart_set_update_mode(o, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_color(o, lv_color_hex(MU_SURFACE), 0); lv_obj_set_style_border_width(o, 0, 0); lv_obj_set_style_line_color(o, lv_color_hex(MU_LINE), 0); lv_obj_set_style_radius(o, 12, 0); lv_obj_set_style_pad_all(o, 4, 0);
    lv_obj_set_style_size(o, 0, LV_PART_INDICATOR); lv_obj_set_style_line_width(o, 2, LV_PART_ITEMS);
    lv_chart_add_series(o, lv_color_hex(colorFor(w, MU_TEAL)), LV_CHART_AXIS_PRIMARY_Y);
    s_bind.push_back({o, 7, w});
  } else if (type == "spacer") {
    o = lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_width(o, LV_PCT(100)); int h = (int)N(w["h"], 0); if (h > 0) lv_obj_set_height(o, h); else { lv_obj_set_height(o, 1); lv_obj_set_flex_grow(o, 1); }
  } else if (type == "divider") {
    o = lv_obj_create(parent); lv_obj_remove_style_all(o); lv_obj_set_size(o, LV_PCT(100), 1); lv_obj_set_style_bg_color(o, lv_color_hex(MU_LINE), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  } else {
    o = makeLabel(parent, (String("? ") + type).c_str(), &caption);
  }
  if (o && !w["hidden"].isNull()) { s_bind.push_back({o, 2, w}); applyHidden(o, w); }
}

void refreshBindings() {
  for (auto& b : s_bind) {
    if (!b.obj) continue;
    switch (b.kind) {
      case 0: { String t = R(b.w["text"]); if (strcmp(lv_label_get_text(b.obj), t.c_str())) lv_label_set_text(b.obj, t.c_str()); break; }
      case 1: lv_bar_set_value(b.obj, (int)N(b.w["value"], 0), LV_ANIM_OFF); break;
      case 2: applyHidden(b.obj, b.w); break;
      case 3: { bool on = getState(R(b.w["bind"])).truthy(); if (on != lv_obj_has_state(b.obj, LV_STATE_CHECKED)) { if (on) lv_obj_add_state(b.obj, LV_STATE_CHECKED); else lv_obj_clear_state(b.obj, LV_STATE_CHECKED); } break; }
      case 4: { int v = (int)getState(R(b.w["bind"])).toNum(); if (v != lv_slider_get_value(b.obj)) lv_slider_set_value(b.obj, v, LV_ANIM_OFF); break; }
      case 5: { String v = getState(R(b.w["bind"])).toStr(); bool pw = b.w["password"] | false; String shown = v.length() ? (pw ? String("••••••") : v) : R(b.w["placeholder"]); lv_label_set_text(b.obj, shown.c_str()); lv_obj_set_style_text_color(b.obj, lv_color_hex(v.length() ? MU_TEXT : MU_MUTED), 0); break; }
      case 6: { String v = getState(R(b.w["bind"])).toStr(); if (strcmp(lv_label_get_text(b.obj), v.c_str())) { lv_label_set_text(b.obj, v.c_str()); lv_obj_t* box = lv_obj_get_parent(b.obj); lv_obj_update_layout(box); lv_obj_scroll_to_y(box, LV_COORD_MAX, LV_ANIM_OFF); } break; }
      case 7: {   // comma-separated numbers in a state string -> the last `points` values
        String v = getState(R(b.w["bind"])).toStr(); lv_chart_series_t* ser = lv_chart_get_series_next(b.obj, nullptr); if (!ser) break;
        uint16_t n = lv_chart_get_point_count(b.obj); lv_chart_set_all_value(b.obj, ser, LV_CHART_POINT_NONE);
        std::vector<int> vals; int st = 0; while (st <= (int)v.length() && vals.size() < 240) { int c = v.indexOf(',', st); String tok = c < 0 ? v.substring(st) : v.substring(st, c); tok.trim(); if (tok.length()) vals.push_back((int)tok.toFloat()); if (c < 0) break; st = c + 1; }
        size_t start = vals.size() > n ? vals.size() - n : 0; for (size_t i = start; i < vals.size(); i++) lv_chart_set_value_by_id(b.obj, ser, i - start, vals[i]);
        lv_chart_refresh(b.obj); break; }
    }
  }
}

static void evScrDeleted(lv_event_t* e) {
  if (lv_event_get_target(e) != s_screen) return;   // a previous screen being torn down after a rebuild: not ours any more
  s_screen = nullptr; s_bind.clear(); onScreenGone();
}
static void evMenu(lv_event_t*) { ui::showApps(); }
void buildScreen() {
  s_bind.clear(); resetActions();
  lv_obj_t* scr = newScreen();
  lv_obj_t* bar = statusBar(scr, "", runningName().c_str(), true);
  lv_obj_t* menu = makeIconButton(bar, LV_SYMBOL_LIST, chipH() + 8, evMenu);
  (void)menu;
  if (big()) lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);   // content column centered
  lv_obj_t* body = lv_obj_create(scr); lv_obj_remove_style_all(body); lv_obj_set_width(body, big() ? 420 : LV_PCT(100)); lv_obj_set_flex_grow(body, 1);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(body, big() ? 12 : 6, 0); lv_obj_set_style_pad_hor(body, 2, 0);
  lv_obj_set_scroll_dir(body, LV_DIR_VER); lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
  buildList(body, uiArray(), 0);
  lv_obj_add_event_cb(scr, evScrDeleted, LV_EVENT_DELETE, nullptr);
  s_screen = scr; onScreenShown(scr);
  load(scr);
}

static void dumpList(JsonArray items, String& out, int depth) {
  for (JsonObject w : items) {
    String type = (const char*)(w["type"] | "label");
    if (hiddenNow(w)) continue;
    if (type == "row") { out += "[row] "; String inner; dumpList(w["items"].as<JsonArray>(), inner, depth + 1); inner.replace("\n", " | "); out += inner; out += "\n"; continue; }
    out += "["; out += type; String st = R(w["style"]); String sz = R(w["size"]); if (st.length()) out += " " + st; if (sz.length()) out += " " + sz; out += "] ";
    if (type == "label" || type == "value" || type == "button") out += R(w["text"]);
    else if (type == "toggle") out += R(w["text"]) + " = " + (getState(R(w["bind"])).truthy() ? "on" : "off");
    else if (type == "slider") out += R(w["text"]) + " = " + getState(R(w["bind"])).toStr();
    else if (type == "bar") out += String((int)N(w["value"], 0)) + "/" + String((int)N(w["max"], 100));
    else if (type == "input") { String v = getState(R(w["bind"])).toStr(); out += v.length() ? v : "(" + R(w["placeholder"]) + ")"; }
    else if (type == "log") { String v = getState(R(w["bind"])).toStr(); v.replace("\n", " / "); out += v; }
    else if (type == "chart") { String v = getState(R(w["bind"])).toStr(); out += "series(" + String(v.length() ? v.substring(v.lastIndexOf(',', v.length() - 2) + 1) : "") + " latest)"; }
    out += "\n";
  }
}
String uiText() {
  String out = "APPLET " + runningId() + " \"" + runningName() + "\"" + (onScreen() ? "" : " (running in background, home screen shown)") + "\n";
  dumpList(uiArray(), out, 0);
  return out.substring(0, 3000);
}
}  // namespace applets

// ---------------- launcher ----------------
namespace ui {
static std::vector<applets::Info> s_apps; static String s_rmId;
static void doRemove() { applets::remove(s_rmId); toast("Applet removed"); showApps(); }
static void evAppMenu(lv_event_t* e) {
  lv_obj_t* mb = lv_event_get_current_target(e); const char* b = lv_msgbox_get_active_btn_text(mb); String choice = b ? b : "";
  lv_msgbox_close_async(mb);
  if (choice.startsWith("Autostart")) { bool on = applets::setAutostart(s_rmId, !choice.endsWith("on")); toast(on ? "Opens on boot. Hold BOOT 3 s to leave it." : "Autostart off"); showApps(); }
  else if (choice == "Remove") doRemove();
}
static void evOpenApp(lv_event_t* e) {
  size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i >= s_apps.size()) return;
  if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
    s_rmId = s_apps[i].id;
    static const char* btns[] = {"Autostart: off", "Remove", "Cancel", ""}; static const char* btnsOn[] = {"Autostart: on", "Remove", "Cancel", ""};
    lv_obj_t* mb = lv_msgbox_create(nullptr, s_apps[i].name.c_str(), "Autostart opens this applet at power-on (kiosk).", s_apps[i].autostart ? btnsOn : btns, false);
    lv_obj_set_style_bg_color(mb, lv_color_hex(MU_SURFACE), 0); lv_obj_set_style_text_color(mb, lv_color_hex(MU_TEXT), 0); lv_obj_set_style_border_width(mb, 0, 0); lv_obj_set_style_radius(mb, 16, 0); lv_obj_set_width(mb, big() ? 360 : 236); lv_obj_set_style_shadow_width(mb, 30, 0); lv_obj_set_style_shadow_color(mb, lv_color_hex(0x000000), 0); lv_obj_set_style_shadow_opa(mb, LV_OPA_60, 0);
    lv_obj_t* bm = lv_msgbox_get_btns(mb); lv_obj_set_style_bg_color(bm, lv_color_hex(MU_SURFACE2), LV_PART_ITEMS); lv_obj_set_style_text_color(bm, lv_color_hex(MU_TEXT), LV_PART_ITEMS); lv_obj_set_style_radius(bm, LV_RADIUS_CIRCLE, LV_PART_ITEMS); lv_obj_set_style_border_width(bm, 0, LV_PART_ITEMS); lv_obj_set_height(bm, btnH()); lv_obj_set_style_pad_gap(bm, 6, 0);
    lv_obj_add_event_cb(mb, evAppMenu, LV_EVENT_VALUE_CHANGED, nullptr); lv_obj_center(mb);
    return;
  }
  String err; if (!applets::open(s_apps[i].id, err)) toast(err.c_str(), true);
}
static void evMacActions(lv_event_t*) { showActions(); }
static void evResume(lv_event_t*) { applets::buildScreen(); }
void showApps() {
  s_apps = applets::list();
  lv_obj_t* scr = newScreen(); statusBar(scr, "", "Apps", true);
  lv_obj_t* grid = lv_obj_create(scr); lv_obj_remove_style_all(grid); lv_obj_set_width(grid, LV_PCT(100)); lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP); lv_obj_set_style_pad_gap(grid, big() ? 10 : 6, 0); lv_obj_set_scroll_dir(grid, LV_DIR_VER); lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
  if (big()) {
    lv_obj_t* top = lv_obj_create(grid); lv_obj_remove_style_all(top); lv_obj_set_size(top, LV_PCT(100), btnH() - 8); lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(top, 8, 0);
    if (applets::running() && !applets::onScreen()) { lv_obj_t* b = makeButton(top, (String(LV_SYMBOL_PLAY "  Back to ") + applets::runningName()).c_str(), true); lv_obj_set_height(b, btnH() - 8); lv_obj_set_style_pad_ver(b, 0, 0); lv_obj_add_event_cb(b, evResume, LV_EVENT_CLICKED, nullptr); }
    lv_obj_t* sp = lv_obj_create(top); lv_obj_remove_style_all(sp); lv_obj_set_height(sp, 1); lv_obj_set_flex_grow(sp, 1);
    lv_obj_t* mb = makeButton(top, "Mac actions  " LV_SYMBOL_RIGHT); lv_obj_set_height(mb, btnH() - 8); lv_obj_set_style_pad_ver(mb, 0, 0); lv_obj_add_event_cb(mb, evMacActions, LV_EVENT_CLICKED, nullptr);
  } else if (applets::running() && !applets::onScreen()) {
    lv_obj_t* b = makeButton(grid, (String(LV_SYMBOL_PLAY "  Back to ") + applets::runningName()).c_str(), true); lv_obj_set_size(b, LV_PCT(100), btnH()); lv_obj_add_event_cb(b, evResume, LV_EVENT_CLICKED, nullptr);
  }
  if (s_apps.empty()) {
    lv_obj_t* wrap = lv_obj_create(grid); lv_obj_remove_style_all(wrap); lv_obj_set_size(wrap, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_row(wrap, 6, 0); lv_obj_set_style_pad_ver(wrap, 10, 0);
    badge(wrap, LV_SYMBOL_PLUS, big() ? 56 : 44);
    lv_obj_t* l = makeLabel(wrap, "No apps yet", &body);
    lv_obj_t* h = makeLabel(wrap, "Ask Muse on your Mac: \"make the device a people counter\". It writes a small applet and installs it here in seconds.", &caption);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP); lv_obj_set_width(h, LV_PCT(96)); lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0); (void)l;
  }
  for (size_t i = 0; i < s_apps.size(); i++) {
    auto& a = s_apps[i];
    lv_obj_t* b = lv_btn_create(grid); lv_obj_remove_style_all(b); lv_obj_add_style(b, &card, 0); lv_obj_add_style(b, &pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 14, 0); lv_obj_set_size(b, big() ? LV_PCT(32) : LV_PCT(49), big() ? 104 : btnH() + 28); lv_obj_set_style_pad_all(b, big() ? 10 : 6, 0);
    if (a.id == applets::runningId()) lv_obj_set_style_bg_color(b, lv_color_hex(MU_SURFACE2), 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_row(b, 4, 0);
    const char* sym = applets::symbolFor(a.icon);
    badge(b, sym ? sym : (a.icon.length() ? a.icon.substring(0, 2).c_str() : LV_SYMBOL_FILE), big() ? 44 : 30);
    lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, a.name.c_str()); lv_obj_set_style_text_font(l, fontSmall(), 0); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_width(l, LV_PCT(100)); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(b, evOpenApp, LV_EVENT_CLICKED, (void*)(uintptr_t)i); lv_obj_add_event_cb(b, evOpenApp, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i);
  }
  if (big()) { lv_obj_t* hint = makeLabel(scr, s_apps.empty() ? "" : "Hold a tile for options", &caption); lv_obj_set_style_text_color(hint, lv_color_hex(MU_DIM), 0); }
  else {
    lv_obj_t* foot = lv_obj_create(scr); lv_obj_remove_style_all(foot); lv_obj_set_size(foot, LV_PCT(100), btnH()); lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* hint = makeLabel(foot, s_apps.empty() ? "" : "Hold a tile for options", &caption); lv_obj_set_style_text_color(hint, lv_color_hex(MU_DIM), 0);
    lv_obj_t* mb = makeButton(foot, "Mac actions  " LV_SYMBOL_RIGHT); lv_obj_set_height(mb, btnH() - 4); lv_obj_set_style_pad_ver(mb, 0, 0); lv_obj_add_event_cb(mb, evMacActions, LV_EVENT_CLICKED, nullptr);
  }
  load(scr);
}
void showApplet() { if (applets::running()) applets::buildScreen(); else showApps(); }
}  // namespace ui
