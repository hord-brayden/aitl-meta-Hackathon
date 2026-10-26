#include "theme.h"
#include "../config.h"
#if __has_include("avatar_img.h")
#include "avatar_img.h"
#define AITL_HAS_AVATAR 1
#endif

namespace theme {
#if defined(AITL_UI_BIG)
// 480x320: 12/16/24 px rhythm, 52 px rows, 56 px buttons, 18 px body.
bool big() { return true; }
lv_coord_t dockH() { return 60; }
lv_coord_t btnH() { return 56; }
lv_coord_t rowH() { return 52; }
lv_coord_t chipH() { return 34; }
const lv_font_t* fontTitle() { return &lv_font_montserrat_24; }
const lv_font_t* fontBody() { return &lv_font_montserrat_18; }
const lv_font_t* fontSmall() { return &lv_font_montserrat_16; }
const lv_font_t* fontLabel() { return &lv_font_montserrat_14; }
const lv_font_t* fontIcon() { return &lv_font_montserrat_24; }
#else
bool big() { return false; }
lv_coord_t dockH() { return 50; }
lv_coord_t btnH() { return 38; }
lv_coord_t rowH() { return 36; }
lv_coord_t chipH() { return 24; }
const lv_font_t* fontTitle() { return &lv_font_montserrat_20; }
const lv_font_t* fontBody() { return &lv_font_montserrat_14; }
const lv_font_t* fontSmall() { return &lv_font_montserrat_12; }
const lv_font_t* fontLabel() { return &lv_font_montserrat_10; }
const lv_font_t* fontIcon() { return &lv_font_montserrat_18; }
#endif
lv_style_t screen, card, pill, pillPrimary, pressed, title, body, caption, muted;

void gradient(lv_obj_t* o, bool horizontal) {
  lv_obj_set_style_bg_color(o, lv_color_hex(MU_TEAL), 0);
  lv_obj_set_style_bg_grad_color(o, lv_color_hex(MU_MAGENTA), 0);
  lv_obj_set_style_bg_grad_dir(o, horizontal ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

void init() {
  lv_style_init(&screen);
  lv_style_set_bg_color(&screen, lv_color_hex(MU_BG)); lv_style_set_bg_opa(&screen, LV_OPA_COVER);
  lv_style_set_text_color(&screen, lv_color_hex(MU_TEXT)); lv_style_set_text_font(&screen, fontBody());
  lv_style_set_pad_all(&screen, 0); lv_style_set_border_width(&screen, 0); lv_style_set_radius(&screen, 0);

  lv_style_init(&card);
  lv_style_set_bg_color(&card, lv_color_hex(MU_SURFACE)); lv_style_set_bg_opa(&card, LV_OPA_COVER);
  lv_style_set_border_width(&card, 0); lv_style_set_radius(&card, big() ? 20 : 16);
  lv_style_set_pad_all(&card, big() ? 16 : 10); lv_style_set_text_color(&card, lv_color_hex(MU_TEXT));
  lv_style_set_shadow_width(&card, 0);

  lv_style_init(&pill);
  lv_style_set_bg_color(&pill, lv_color_hex(MU_SURFACE2)); lv_style_set_bg_opa(&pill, LV_OPA_COVER);
  lv_style_set_border_width(&pill, 0); lv_style_set_radius(&pill, LV_RADIUS_CIRCLE);
  lv_style_set_text_color(&pill, lv_color_hex(MU_TEXT)); lv_style_set_text_font(&pill, fontBody());
  lv_style_set_pad_ver(&pill, big() ? 12 : 8); lv_style_set_pad_hor(&pill, big() ? 20 : 14); lv_style_set_shadow_width(&pill, 0);

  lv_style_init(&pillPrimary);
  lv_style_set_bg_color(&pillPrimary, lv_color_hex(MU_TEAL)); lv_style_set_bg_grad_color(&pillPrimary, lv_color_hex(MU_MAGENTA));
  lv_style_set_bg_grad_dir(&pillPrimary, LV_GRAD_DIR_HOR); lv_style_set_text_color(&pillPrimary, lv_color_hex(0x0b0d14));

  lv_style_init(&pressed);
  lv_style_set_bg_color(&pressed, lv_color_hex(MU_RAISED)); lv_style_set_bg_grad_color(&pressed, lv_color_hex(MU_RAISED));
  lv_style_set_bg_grad_dir(&pressed, LV_GRAD_DIR_NONE); lv_style_set_text_color(&pressed, lv_color_hex(MU_TEXT));

  lv_style_init(&title); lv_style_set_text_font(&title, fontTitle()); lv_style_set_text_color(&title, lv_color_hex(MU_TEXT));
  lv_style_init(&body); lv_style_set_text_font(&body, fontBody()); lv_style_set_text_color(&body, lv_color_hex(MU_TEXT)); lv_style_set_text_line_space(&body, big() ? 4 : 3);
  lv_style_init(&caption); lv_style_set_text_font(&caption, fontSmall()); lv_style_set_text_color(&caption, lv_color_hex(MU_MUTED));
  lv_style_init(&muted); lv_style_set_text_font(&muted, fontBody()); lv_style_set_text_color(&muted, lv_color_hex(MU_MUTED));
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, bool primary) {
  lv_obj_t* b = lv_btn_create(parent); lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &pill, 0); if (primary) lv_obj_add_style(b, &pillPrimary, 0);
  lv_obj_add_style(b, &pressed, LV_STATE_PRESSED);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, text); lv_obj_center(l);
  return b;
}
lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, lv_style_t* st) {
  lv_obj_t* l = lv_label_create(parent); lv_label_set_text(l, text); if (st) lv_obj_add_style(l, st, 0); return l;
}
lv_obj_t* makeCard(lv_obj_t* parent) {
  lv_obj_t* c = lv_obj_create(parent); lv_obj_remove_style_all(c); lv_obj_add_style(c, &card, 0);
  lv_obj_set_width(c, LV_PCT(100)); lv_obj_set_height(c, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN); lv_obj_set_style_pad_row(c, big() ? 6 : 4, 0); lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}
lv_obj_t* makeChip(lv_obj_t* parent, const char* icon, const char* text, lv_event_cb_t cb, lv_coord_t maxW) {
  lv_obj_t* c = lv_obj_create(parent); lv_obj_remove_style_all(c); lv_obj_add_style(c, &pill, 0);
  lv_obj_set_style_pad_ver(c, 0, 0); lv_obj_set_style_pad_hor(c, big() ? 12 : 9, 0);
  lv_obj_set_size(c, LV_SIZE_CONTENT, chipH()); lv_obj_set_style_max_width(c, maxW, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); lv_obj_set_style_pad_column(c, 5, 0);
  lv_obj_add_style(c, &pressed, LV_STATE_PRESSED);
  lv_obj_t* i = lv_label_create(c); lv_label_set_text(i, icon ? icon : ""); lv_obj_set_style_text_font(i, fontSmall(), 0); lv_obj_set_style_text_color(i, lv_color_hex(MU_TEAL), 0);
  lv_obj_t* l = lv_label_create(c); lv_label_set_text(l, text ? text : ""); lv_obj_set_style_text_font(l, fontSmall(), 0); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(l, maxW - (big() ? 54 : 38), 0);
  if (cb) lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_set_ext_click_area(c, 6);
  return c;
}
lv_obj_t* makeIconButton(lv_obj_t* parent, const char* sym, lv_coord_t size, lv_event_cb_t cb, void* ud) {
  lv_obj_t* b = lv_btn_create(parent); lv_obj_remove_style_all(b); lv_obj_add_style(b, &pill, 0); lv_obj_add_style(b, &pressed, LV_STATE_PRESSED);
  lv_obj_set_size(b, size, size); lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_t* l = lv_label_create(b); lv_label_set_text(l, sym); lv_obj_set_style_text_font(l, fontSmall(), 0); lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  lv_obj_set_ext_click_area(b, 4);
  return b;
}
lv_obj_t* badge(lv_obj_t* parent, const char* sym, lv_coord_t size) {
  lv_obj_t* c = lv_obj_create(parent); lv_obj_remove_style_all(c); lv_obj_set_size(c, size, size);
  lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0); gradient(c, false); lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = lv_label_create(c); lv_label_set_text(l, sym); lv_obj_set_style_text_color(l, lv_color_hex(0x0b0d14), 0);
  lv_obj_set_style_text_font(l, size >= 40 ? fontIcon() : fontSmall(), 0); lv_obj_center(l);
  return c;
}

// ---------- the orb ----------
// root (transparent, 1.4x) > halo (animated circle) > core (gradient circle, or the avatar image ringed by gradient)
struct OrbInfo { lv_coord_t size; Orb mode; };
static void haloAnim(void* var, int32_t v) {   // v: 0..100
  lv_obj_t* halo = (lv_obj_t*)var; lv_obj_t* root = lv_obj_get_parent(halo);
  OrbInfo* info = (OrbInfo*)lv_obj_get_user_data(root); if (!info) return;
  lv_coord_t s = info->size + (info->size * 40 / 100) * v / 100;
  lv_obj_set_size(halo, s, s); lv_obj_center(halo);
  lv_obj_set_style_bg_opa(halo, (lv_opa_t)(90 - 85 * v / 100), 0);
}
static void orbStart(lv_obj_t* halo, uint32_t period) {
  lv_anim_del(halo, haloAnim);
  lv_anim_t a; lv_anim_init(&a); lv_anim_set_var(&a, halo); lv_anim_set_exec_cb(&a, haloAnim);
  lv_anim_set_values(&a, 0, 100); lv_anim_set_time(&a, period); lv_anim_set_playback_time(&a, period);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE); lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out); lv_anim_start(&a);
}
static void orbDeleted(lv_event_t* e) { lv_obj_t* root = lv_event_get_target(e); OrbInfo* i = (OrbInfo*)lv_obj_get_user_data(root); if (i) { lv_obj_t* halo = lv_obj_get_child(root, 0); if (halo) lv_anim_del(halo, haloAnim); lv_mem_free(i); lv_obj_set_user_data(root, nullptr); } }
lv_obj_t* orb(lv_obj_t* parent, lv_coord_t size, bool animate) {
  lv_obj_t* root = lv_obj_create(parent); lv_obj_remove_style_all(root);
  lv_coord_t R = size + size * 40 / 100; lv_obj_set_size(root, R, R); lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  OrbInfo* info = (OrbInfo*)lv_mem_alloc(sizeof(OrbInfo)); info->size = size; info->mode = Orb::Idle; lv_obj_set_user_data(root, info);
  lv_obj_add_event_cb(root, orbDeleted, LV_EVENT_DELETE, nullptr);
  lv_obj_t* halo = lv_obj_create(root); lv_obj_remove_style_all(halo); lv_obj_set_size(halo, size, size); lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
  gradient(halo, false); lv_obj_set_style_bg_opa(halo, 60, 0); lv_obj_center(halo); lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* core = lv_obj_create(root); lv_obj_remove_style_all(core); lv_obj_set_size(core, size, size); lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
  gradient(core, false); lv_obj_center(core); lv_obj_clear_flag(core, LV_OBJ_FLAG_SCROLLABLE);
#if defined(AITL_HAS_AVATAR)
  if (size >= 40) {   // the avatar sits inside the gradient ring
    lv_obj_t* av = lv_img_create(core); lv_img_set_src(av, &avatar_img);
    lv_coord_t inner = size - 6; lv_img_set_zoom(av, (256 * inner) / AITL_AVATAR_SIZE); lv_img_set_size_mode(av, LV_IMG_SIZE_MODE_REAL);
    lv_obj_set_size(av, inner, inner); lv_obj_set_style_radius(av, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_clip_corner(av, true, 0); lv_obj_center(av);
  }
#endif
  if (animate) orbStart(halo, 2400); else lv_obj_add_flag(halo, LV_OBJ_FLAG_HIDDEN);
  return root;
}
void orbSetMode(lv_obj_t* root, Orb mode) {
  if (!root) return; OrbInfo* info = (OrbInfo*)lv_obj_get_user_data(root); if (!info || info->mode == mode) return;
  info->mode = mode;
  lv_obj_t* halo = lv_obj_get_child(root, 0); lv_obj_t* core = lv_obj_get_child(root, 1);
  lv_obj_clear_flag(halo, LV_OBJ_FLAG_HIDDEN);
  switch (mode) {
    case Orb::Idle: gradient(halo, false); gradient(core, false); orbStart(halo, 2400); break;
    case Orb::Busy: gradient(halo, false); gradient(core, false); orbStart(halo, 650); break;
    case Orb::Attention: lv_obj_set_style_bg_color(halo, lv_color_hex(MU_WARN), 0); lv_obj_set_style_bg_grad_color(halo, lv_color_hex(MU_WARN), 0); orbStart(halo, 900); break;
    case Orb::Offline: lv_anim_del(halo, haloAnim); lv_obj_add_flag(halo, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(core, lv_color_hex(MU_RAISED), 0); lv_obj_set_style_bg_grad_color(core, lv_color_hex(MU_LINE), 0); break;
  }
}
}  // namespace theme
