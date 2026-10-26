#include "theme.h"
#include "../config.h"

namespace theme {
#if defined(AITL_UI_BIG)
bool big() { return true; }
lv_coord_t dockH() { return 76; }
lv_coord_t btnH() { return 50; }
lv_coord_t rowH() { return 50; }
lv_coord_t chipH() { return 30; }
const lv_font_t* fontTitle() { return &lv_font_montserrat_28; }
const lv_font_t* fontBody() { return &lv_font_montserrat_18; }
const lv_font_t* fontSmall() { return &lv_font_montserrat_16; }
const lv_font_t* fontLabel() { return &lv_font_montserrat_14; }
const lv_font_t* fontIcon() { return &lv_font_montserrat_28; }
#else
bool big() { return false; }
lv_coord_t dockH() { return 52; }
lv_coord_t btnH() { return 38; }
lv_coord_t rowH() { return 38; }
lv_coord_t chipH() { return 22; }
const lv_font_t* fontTitle() { return &lv_font_montserrat_20; }
const lv_font_t* fontBody() { return &lv_font_montserrat_14; }
const lv_font_t* fontSmall() { return &lv_font_montserrat_12; }
const lv_font_t* fontLabel() { return &lv_font_montserrat_10; }
const lv_font_t* fontIcon() { return &lv_font_montserrat_20; }
#endif
lv_style_t screen, panel, btn, btnPressed, btnPrimary, mono, monoBig, title, body, muted, chip;

void init() {
  lv_style_init(&screen);
  lv_style_set_bg_color(&screen, lv_color_hex(CO_BLACK));
  lv_style_set_bg_opa(&screen, LV_OPA_COVER);
  lv_style_set_text_color(&screen, lv_color_hex(CO_BONE));
  lv_style_set_text_font(&screen, fontBody());
  lv_style_set_pad_all(&screen, 0);
  lv_style_set_border_width(&screen, 0);
  lv_style_set_radius(&screen, 0);

  lv_style_init(&panel);
  lv_style_set_bg_color(&panel, lv_color_hex(CO_ABYSS));
  lv_style_set_bg_opa(&panel, LV_OPA_COVER);
  lv_style_set_border_color(&panel, lv_color_hex(CO_MID));
  lv_style_set_border_width(&panel, 1);
  lv_style_set_border_opa(&panel, LV_OPA_80);
  lv_style_set_radius(&panel, big() ? 8 : 6);
  lv_style_set_pad_all(&panel, big() ? 10 : 7);
  lv_style_set_text_color(&panel, lv_color_hex(CO_BONE));

  lv_style_init(&btn);
  lv_style_set_bg_color(&btn, lv_color_hex(CO_CARD));
  lv_style_set_bg_opa(&btn, LV_OPA_COVER);
  lv_style_set_border_color(&btn, lv_color_hex(CO_CAERULEI_D));
  lv_style_set_border_width(&btn, 1);
  lv_style_set_border_opa(&btn, LV_OPA_60);
  lv_style_set_radius(&btn, big() ? 10 : 8);
  lv_style_set_text_color(&btn, lv_color_hex(CO_BONE));
  lv_style_set_text_font(&btn, fontBody());
  lv_style_set_pad_ver(&btn, big() ? 12 : 8);
  lv_style_set_pad_hor(&btn, big() ? 14 : 10);
  lv_style_set_shadow_width(&btn, 0);

  lv_style_init(&btnPressed);
  lv_style_set_bg_color(&btnPressed, lv_color_hex(CO_CAERULEI_D));
  lv_style_set_border_color(&btnPressed, lv_color_hex(CO_CAERULEI));
  lv_style_set_text_color(&btnPressed, lv_color_hex(CO_BONE));

  lv_style_init(&btnPrimary);
  lv_style_set_bg_color(&btnPrimary, lv_color_hex(CO_CAERULEI));
  lv_style_set_border_color(&btnPrimary, lv_color_hex(CO_CAERULEI2));
  lv_style_set_text_color(&btnPrimary, lv_color_hex(CO_BLACK));

  lv_style_init(&mono);
  lv_style_set_text_font(&mono, fontLabel());
  lv_style_set_text_color(&mono, lv_color_hex(CO_CAERULEI_D));
  lv_style_set_text_letter_space(&mono, 1);

  lv_style_init(&monoBig);
  lv_style_set_text_font(&monoBig, fontSmall());
  lv_style_set_text_color(&monoBig, lv_color_hex(CO_CAERULEI));
  lv_style_set_text_letter_space(&monoBig, 2);

  lv_style_init(&title);
  lv_style_set_text_font(&title, fontTitle());
  lv_style_set_text_color(&title, lv_color_hex(CO_BONE));

  lv_style_init(&body);
  lv_style_set_text_font(&body, fontBody());
  lv_style_set_text_color(&body, lv_color_hex(CO_BONE));
  lv_style_set_text_line_space(&body, big() ? 4 : 3);

  lv_style_init(&muted);
  lv_style_set_text_font(&muted, fontSmall());
  lv_style_set_text_color(&muted, lv_color_hex(CO_MUTE));

  lv_style_init(&chip);
  lv_style_set_bg_color(&chip, lv_color_hex(CO_CARD));
  lv_style_set_bg_opa(&chip, LV_OPA_COVER);
  lv_style_set_radius(&chip, LV_RADIUS_CIRCLE);
  lv_style_set_pad_hor(&chip, big() ? 12 : 8);
  lv_style_set_pad_ver(&chip, 2);
  lv_style_set_text_font(&chip, fontSmall());
  lv_style_set_text_color(&chip, lv_color_hex(CO_BONE));
  lv_style_set_border_width(&chip, 1);
  lv_style_set_border_color(&chip, lv_color_hex(CO_MID));
  lv_style_set_border_opa(&chip, LV_OPA_80);
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, bool primary) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &btn, 0);
  if (primary) lv_obj_add_style(b, &btnPrimary, 0);
  lv_obj_add_style(b, &btnPressed, LV_STATE_PRESSED);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, lv_style_t* st) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  if (st) lv_obj_add_style(l, st, 0);
  return l;
}

lv_obj_t* makeHeader(lv_obj_t* parent, const char* section, const char* ttl) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), chipH());
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  makeLabel(row, section, &mono);
  lv_obj_t* bar = lv_obj_create(row); lv_obj_remove_style_all(bar); lv_obj_set_size(bar, 14, 1);
  lv_obj_set_style_bg_color(bar, lv_color_hex(CO_CAERULEI), 0); lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_t* t = makeLabel(row, ttl, &mono); lv_obj_set_style_text_color(t, lv_color_hex(CO_ASH), 0);
  return row;
}
}  // namespace theme
