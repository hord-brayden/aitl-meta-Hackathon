// Models, chats, actions, compose (on-device keyboard) and generic text entry.
#include "ui.h"
#include "theme.h"
#include "../config.h"
#include "../core/settings.h"
#include "../hal/display.h"

using namespace theme; using namespace ui_int;
namespace ui {

static lv_obj_t* listBtn(lv_obj_t* list, const char* text, const char* sub, bool current, bool dim, lv_event_cb_t cb, void* ud) {
  lv_obj_t* b = lv_btn_create(list); lv_obj_remove_style_all(b); lv_obj_add_style(b, &panel, 0);
  lv_obj_add_style(b, &btnPressed, LV_STATE_PRESSED);
  lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT); lv_obj_set_style_min_height(b, rowH(), 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (current) { lv_obj_set_style_border_color(b, lv_color_hex(CO_CAERULEI), 0); lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0); lv_obj_set_style_border_width(b, 3, 0); lv_obj_set_style_bg_color(b, lv_color_hex(CO_CARD), 0); }
  lv_obj_t* col = lv_obj_create(b); lv_obj_remove_style_all(col); lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN); lv_obj_set_flex_grow(col, 1); lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_t* l = makeLabel(col, text, &body); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); lv_obj_set_width(l, LV_PCT(100));
  if (dim) lv_obj_set_style_text_color(l, lv_color_hex(CO_MUTE), 0);
  if (sub && *sub) { lv_obj_t* s = makeLabel(col, sub, &muted); lv_obj_set_style_text_color(s, lv_color_hex(dim ? CO_MUTE : CO_ASH), 0); lv_label_set_long_mode(s, LV_LABEL_LONG_DOT); lv_obj_set_width(s, LV_PCT(100)); }
  if (current) { lv_obj_t* c = makeLabel(b, LV_SYMBOL_OK, &body); lv_obj_set_style_text_color(c, lv_color_hex(CO_CAERULEI), 0); }
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}
static lv_obj_t* scrollList(lv_obj_t* scr) {
  lv_obj_t* list = lv_obj_create(scr); lv_obj_remove_style_all(list);
  lv_obj_set_width(list, LV_PCT(100)); lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(list, 3, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER); lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  return list;
}

// ---------- models ----------
static void evPickModel(lv_event_t* e) { size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i < app::models.size()) { app::setModel(app::models[i].id); toast("Model set"); } showHome(); }
static lv_obj_t* s_modelsScr = nullptr; static lv_obj_t* s_chatsScr = nullptr; static lv_timer_t* s_rerender = nullptr;
static void renderModels();
static void renderChats();
void showModels() { if (app::bridgeLinked()) app::refreshLists(); renderModels(); }
static void renderModels() {
  lv_obj_t* scr = newScreen(); s_modelsScr = scr; statusBar(scr, "02", "MODELS", true);
  lv_obj_t* list = scrollList(scr);
  if (app::models.empty()) { lv_obj_t* l = makeLabel(list, app::bridgeLinked() ? "Bridge reported no models.\nAdd a key with: musecorder keys set muse" : "Link a bridge to list models.", &muted); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_width(l, LV_PCT(100)); }
  String lastProv;
  for (size_t i = 0; i < app::models.size(); i++) {
    auto& m = app::models[i];
    if (m.provider != lastProv) { sectionLabel(list, m.provider.c_str()); lastProv = m.provider; }
    listBtn(list, m.label.c_str(), m.ok ? m.id.c_str() : "no key configured", m.id == app::currentModel, !m.ok, evPickModel, (void*)(uintptr_t)i);
  }
  load(scr);
}

// ---------- chats ----------
static String s_delId;
static void doDelete() { app::deleteChat(s_delId); showChats(); }
static void evOpenChat(lv_event_t* e) {
  size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i >= app::chats.size()) return;
  String id = app::chats[i].id;
  if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) { s_delId = id; confirm("Delete chat?", "This removes it from the bridge.", doDelete); return; }
  app::openChat(id); showHome();
}
static void evNew(lv_event_t*) { app::newChat(); showHome(); }
static void evOpenSession(lv_event_t* e) { size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i < app::ccSessions.size()) { app::openSession(app::ccSessions[i].id); toast("Resuming Muse Code session..."); } showHome(); }
void showChats() { if (app::bridgeLinked()) { app::refreshLists(); app::listSessions(); } renderChats(); }
static void renderChats() {
  lv_obj_t* scr = newScreen(); s_chatsScr = scr; statusBar(scr, "03", "CHATS", true);
  lv_obj_t* list = scrollList(scr);
  lv_obj_t* nb = listBtn(list, LV_SYMBOL_PLUS "  New chat", "", false, false, evNew, nullptr);
  lv_obj_set_style_border_color(nb, lv_color_hex(CO_CAERULEI_D), 0);
  if (app::chats.empty()) { lv_obj_t* l = makeLabel(list, "No saved chats yet.", &muted); (void)l; }
  for (size_t i = 0; i < app::chats.size(); i++) {
    auto& c = app::chats[i];
    lv_obj_t* b = listBtn(list, c.title.length() ? c.title.c_str() : "(untitled)", c.id.c_str(), c.id == app::currentChatId, false, evOpenChat, (void*)(uintptr_t)i);
    lv_obj_add_event_cb(b, evOpenChat, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i);
  }
  sectionLabel(list, "MUSE CODE SESSIONS");
  if (app::ccSessions.empty()) { lv_obj_t* l = makeLabel(list, app::bridgeLinked() ? "none yet (pull down to retry: back, then reopen)" : "link a bridge", &muted); (void)l; }
  for (size_t i = 0; i < app::ccSessions.size(); i++) {
    auto& c = app::ccSessions[i];
    String sub = c.project + "  " + c.age + " ago";
    listBtn(list, c.title.c_str(), sub.c_str(), false, false, evOpenSession, (void*)(uintptr_t)i);
  }
  lv_obj_t* hint = makeLabel(scr, "hold a chat to delete", &mono); lv_obj_set_style_text_color(hint, lv_color_hex(CO_MUTE), 0);
  load(scr);
}

// ---------- actions ----------
static void evRunAction(lv_event_t* e) { size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e); if (i < app::actions.size()) app::runAction(app::actions[i].id); showHome(); }
static void evMedia(lv_event_t* e) { app::mediaKey((uint16_t)(uintptr_t)lv_event_get_user_data(e)); }
static void evKey(lv_event_t* e) { app::keyToMac((const char*)lv_event_get_user_data(e)); }
void showActions() {
  lv_obj_t* scr = newScreen(); statusBar(scr, "04", "ACTIONS", true);
  lv_obj_t* grid = lv_obj_create(scr); lv_obj_remove_style_all(grid); lv_obj_set_width(grid, LV_PCT(100)); lv_obj_set_flex_grow(grid, 1);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP); lv_obj_set_style_pad_gap(grid, 4, 0);
  lv_obj_set_scroll_dir(grid, LV_DIR_VER); lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
  auto tile = [&](const char* text, lv_event_cb_t cb, void* ud, bool primary) {
    lv_obj_t* b = lv_btn_create(grid); lv_obj_remove_style_all(b); lv_obj_add_style(b, &btn, 0); if (primary) lv_obj_add_style(b, &btnPrimary, 0);
    lv_obj_add_style(b, &btnPressed, LV_STATE_PRESSED);
    lv_obj_set_size(b, LV_PCT(49), btnH() + 8); lv_obj_set_style_pad_all(b, 4, 0);
    lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text); lv_obj_set_style_text_font(l, fontSmall(), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_width(l, LV_PCT(100)); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  };
  if (app::actions.empty()) { lv_obj_t* l = makeLabel(grid, "Link a bridge to load actions.\nEdit them in ~/.config/musecorder/config.toml", &muted); lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP); lv_obj_set_width(l, LV_PCT(100)); }
  for (size_t i = 0; i < app::actions.size(); i++) tile(app::actions[i].label.c_str(), evRunAction, (void*)(uintptr_t)i, false);
  sectionLabel(grid, "MAC CONTROLS");
  lv_obj_set_width(lv_obj_get_child(grid, -1), LV_PCT(100));
  tile(LV_SYMBOL_VOLUME_MAX " Vol +", evMedia, (void*)(uintptr_t)0xE9, false);
  tile(LV_SYMBOL_VOLUME_MID " Vol -", evMedia, (void*)(uintptr_t)0xEA, false);
  tile(LV_SYMBOL_PLAY " Play/Pause", evMedia, (void*)(uintptr_t)0xCD, false);
  tile(LV_SYMBOL_MUTE " Mute", evMedia, (void*)(uintptr_t)0xE2, false);
  tile(LV_SYMBOL_PREV " Previous", evMedia, (void*)(uintptr_t)0xB6, false);
  tile(LV_SYMBOL_NEXT " Next", evMedia, (void*)(uintptr_t)0xB5, false);
  tile("Cmd+Tab", evKey, (void*)"cmd+tab", false);
  tile("Cmd+Space", evKey, (void*)"cmd+space", false);
  tile("Esc", evKey, (void*)"esc", false);
  tile("Enter", evKey, (void*)"enter", false);
  load(scr);
}

// ---------- compose ----------
static lv_obj_t* s_ta = nullptr; static lv_obj_t* s_kb = nullptr; static int s_target = 0;
static void composeSend(int target) {
  const char* txt = lv_textarea_get_text(s_ta);
  if (!txt || !*txt) { toast(target == 0 ? "Type a question first" : "Type the text to send to the Mac first", true); return; }
  String t(txt);
  if (target == 0) { app::sendPrompt(t); showHome(); }
  else { app::typeToMac(t); toast("Typing into Mac..."); showHome(); }
}
static void evSendAI(lv_event_t*) { composeSend(0); }
static void evSendMac(lv_event_t*) { composeSend(1); }
static void evKb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY) composeSend(s_target);
  else if (c == LV_EVENT_CANCEL) showHome();
}
static void evDictInto(lv_event_t*) { app::dictation("toggle"); toast(app::dictating ? "Dictation stopped" : "Dictating on Mac..."); }
void showCompose(int target) {
  s_target = target;
  lv_obj_t* scr = newScreen(); lv_obj_set_style_pad_all(scr, 2, 0); lv_obj_set_style_pad_row(scr, 2, 0);
  lv_obj_t* top = lv_obj_create(scr); lv_obj_remove_style_all(top); lv_obj_set_size(top, LV_PCT(100), chipH() + 4);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(top, 3, 0);
  lv_obj_t* x = makeButton(top, LV_SYMBOL_CLOSE); lv_obj_set_size(x, big() ? 51 : 34, chipH() + 4); lv_obj_set_style_pad_all(x, 0, 0); lv_obj_add_event_cb(x, [](lv_event_t*) { showHome(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ttl = makeLabel(top, target == 0 ? "05 // ASK" : "05 // TYPE TO MAC", &mono); lv_obj_set_flex_grow(ttl, 1); lv_obj_set_style_pad_left(ttl, 4, 0);
  lv_obj_t* d = makeButton(top, LV_SYMBOL_AUDIO); lv_obj_set_size(d, big() ? 51 : 34, chipH() + 4); lv_obj_set_style_pad_all(d, 0, 0); lv_obj_add_event_cb(d, evDictInto, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ai = makeButton(top, "AI " LV_SYMBOL_RIGHT, target == 0); lv_obj_set_size(ai, big() ? 78 : 52, chipH() + 4); lv_obj_set_style_pad_all(ai, 0, 0); lv_obj_add_event_cb(ai, evSendAI, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* mac = makeButton(top, "MAC " LV_SYMBOL_RIGHT, target == 1); lv_obj_set_size(mac, big() ? 87 : 58, chipH() + 4); lv_obj_set_style_pad_all(mac, 0, 0); lv_obj_add_event_cb(mac, evSendMac, LV_EVENT_CLICKED, nullptr);
  s_ta = lv_textarea_create(scr); lv_obj_set_width(s_ta, LV_PCT(100)); lv_obj_set_flex_grow(s_ta, 1);
  lv_obj_set_style_bg_color(s_ta, lv_color_hex(CO_MIDNIGHT), 0); lv_obj_set_style_text_color(s_ta, lv_color_hex(CO_BONE), 0);
  lv_obj_set_style_border_color(s_ta, lv_color_hex(CO_CAERULEI_D), 0); lv_obj_set_style_text_font(s_ta, fontBody(), 0);
  lv_obj_set_style_pad_all(s_ta, 4, 0);
  lv_textarea_set_placeholder_text(s_ta, target == 0 ? "Ask anything..." : "Text to type into the Mac...");
  lv_obj_set_style_text_color(s_ta, lv_color_hex(CO_MUTE), LV_PART_TEXTAREA_PLACEHOLDER);
  s_kb = lv_keyboard_create(scr); lv_obj_set_size(s_kb, LV_PCT(100), (lv_coord_t)(displayH() * 0.52f));
  lv_keyboard_set_textarea(s_kb, s_ta); lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_set_style_bg_color(s_kb, lv_color_hex(CO_ABYSS), 0);
  lv_obj_set_style_bg_color(s_kb, lv_color_hex(CO_DEEP), LV_PART_ITEMS); lv_obj_set_style_text_color(s_kb, lv_color_hex(CO_BONE), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(s_kb, lv_color_hex(CO_CAERULEI_D), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(s_kb, lv_color_hex(CO_MID), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_width(s_kb, 0, LV_PART_ITEMS); lv_obj_set_style_radius(s_kb, 3, LV_PART_ITEMS);
  lv_obj_set_style_pad_all(s_kb, 2, 0); lv_obj_set_style_pad_gap(s_kb, 2, 0);
  lv_obj_set_style_text_font(s_kb, fontBody(), LV_PART_ITEMS);
  lv_obj_remove_event_cb(s_kb, lv_keyboard_def_event_cb); lv_obj_add_event_cb(s_kb, lv_keyboard_def_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_kb, evKb, LV_EVENT_ALL, nullptr);
  load(scr);
}

// ---------- generic text entry (wifi ssid/password, host, etc.) ----------
static void (*s_entryDone)(const char*) = nullptr; static void (*s_entryCancel)() = nullptr; static lv_obj_t* s_entryTa = nullptr;
static void evEntryKb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY) { String v = lv_textarea_get_text(s_entryTa); auto cb = s_entryDone; s_entryDone = nullptr; if (cb) cb(v.c_str()); }
  else if (c == LV_EVENT_CANCEL) { s_entryDone = nullptr; auto cc = s_entryCancel; s_entryCancel = nullptr; if (cc) cc(); else showSettings(); }
}
static void evEntryEye(lv_event_t*) { lv_textarea_set_password_mode(s_entryTa, !lv_textarea_get_password_mode(s_entryTa)); }
void showTextEntry(const char* ttl, const char* initial, bool password, void (*onDone)(const char*)) { showTextEntryEx(ttl, initial, password, onDone, nullptr); }
void showTextEntryEx(const char* ttl, const char* initial, bool password, void (*onDone)(const char*), void (*onCancel)()) {
  s_entryDone = onDone; s_entryCancel = onCancel;
  lv_obj_t* scr = newScreen(); lv_obj_set_style_pad_all(scr, 2, 0); lv_obj_set_style_pad_row(scr, 2, 0);
  lv_obj_t* top = lv_obj_create(scr); lv_obj_remove_style_all(top); lv_obj_set_size(top, LV_PCT(100), chipH() + 4);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(top, 4, 0); lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* t = makeLabel(top, ttl, &mono); lv_obj_set_flex_grow(t, 1); lv_obj_set_style_pad_left(t, 4, 0);
  if (password) { lv_obj_t* eye = makeButton(top, LV_SYMBOL_EYE_OPEN); lv_obj_set_size(eye, big() ? 51 : 34, chipH() + 4); lv_obj_set_style_pad_all(eye, 0, 0); lv_obj_add_event_cb(eye, evEntryEye, LV_EVENT_CLICKED, nullptr); }
  s_entryTa = lv_textarea_create(scr); lv_obj_set_width(s_entryTa, LV_PCT(100)); lv_obj_set_flex_grow(s_entryTa, 1);
  lv_textarea_set_one_line(s_entryTa, true); lv_textarea_set_text(s_entryTa, initial ? initial : ""); lv_textarea_set_password_mode(s_entryTa, password);
  lv_obj_set_style_bg_color(s_entryTa, lv_color_hex(CO_MIDNIGHT), 0); lv_obj_set_style_text_color(s_entryTa, lv_color_hex(CO_BONE), 0);
  lv_obj_set_style_border_color(s_entryTa, lv_color_hex(CO_CAERULEI_D), 0);
  lv_obj_t* kb = lv_keyboard_create(scr); lv_obj_set_size(kb, LV_PCT(100), (lv_coord_t)(displayH() * 0.55f));
  lv_keyboard_set_textarea(kb, s_entryTa);
  lv_obj_set_style_bg_color(kb, lv_color_hex(CO_ABYSS), 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(CO_DEEP), LV_PART_ITEMS); lv_obj_set_style_text_color(kb, lv_color_hex(CO_BONE), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, lv_color_hex(CO_CAERULEI_D), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS); lv_obj_set_style_pad_all(kb, 2, 0); lv_obj_set_style_pad_gap(kb, 2, 0);
  lv_obj_set_style_text_font(kb, fontBody(), LV_PART_ITEMS);
  lv_obj_add_event_cb(kb, evEntryKb, LV_EVENT_ALL, nullptr);
  load(scr);
}
}  // namespace ui

namespace ui {
// Re-render list screens when the bridge sends fresh lists (called from onAppEvent).
static void rerenderCb(lv_timer_t* t) {
  s_rerender = nullptr;
  lv_obj_t* act = lv_scr_act();
  if (act && act == s_chatsScr) renderChats();
  else if (act && act == s_modelsScr) renderModels();
}
void listsUpdated() {   // coalesce the burst of list replies into one redraw
  if (s_rerender) lv_timer_reset(s_rerender);
  else { s_rerender = lv_timer_create(rerenderCb, 250, nullptr); lv_timer_set_repeat_count(s_rerender, 1); }
}
}
