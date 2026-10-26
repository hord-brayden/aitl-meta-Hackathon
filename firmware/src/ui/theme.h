#pragma once
#include <lvgl.h>
// Musecorder design system: a Muse-flavoured look. Deep charcoal surfaces, an iridescent teal -> blue -> magenta
// accent used sparingly (the orb, primary pills, badges), pill-shaped actions, 16 px cards, Montserrat only.
#define MU_BG         0x0f1117
#define MU_SURFACE    0x171a23
#define MU_SURFACE2   0x1f2330
#define MU_LINE       0x2a2f3d
#define MU_RAISED     0x343a4b
#define MU_TEXT       0xf3f4f8
#define MU_MUTED      0x9aa3b5
#define MU_DIM        0x5c6478
#define MU_TEAL       0x2ee6d6
#define MU_BLUE       0x3b82f6
#define MU_MAGENTA    0xe879f9
#define MU_OK         0x34d399
#define MU_WARN       0xfbbf24
#define MU_DANGER     0xfb7185
#define MU_BUBBLE     0x1e3a5f   // user message bubble
#define MU_BULLET     "\xE2\x80\xA2"

namespace theme {
  enum class Orb : uint8_t { Idle, Busy, Attention, Offline };
  extern lv_style_t screen;       // page background
  extern lv_style_t card;         // surface, radius 16
  extern lv_style_t pill;         // secondary action: surface-2, full radius
  extern lv_style_t pillPrimary;  // accent gradient, dark text
  extern lv_style_t pressed;      // pressed state tint
  extern lv_style_t title;        // 20 px text
  extern lv_style_t body;         // 14/18 px text
  extern lv_style_t caption;      // 12 px muted
  extern lv_style_t muted;        // body-size muted
  void init();
  bool big();
  lv_coord_t dockH();
  lv_coord_t btnH();
  lv_coord_t rowH();
  lv_coord_t chipH();
  const lv_font_t* fontTitle();
  const lv_font_t* fontBody();
  const lv_font_t* fontSmall();
  const lv_font_t* fontLabel();
  const lv_font_t* fontIcon();
  void gradient(lv_obj_t* o, bool horizontal = true);                 // teal -> magenta fill
  lv_obj_t* makeButton(lv_obj_t* parent, const char* text, bool primary = false);   // pill button with a label child
  lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, lv_style_t* st);
  lv_obj_t* makeCard(lv_obj_t* parent);                                // column-flex card
  lv_obj_t* makeChip(lv_obj_t* parent, const char* icon, const char* text, lv_event_cb_t cb, lv_coord_t maxW);   // children: icon label, text label
  lv_obj_t* makeIconButton(lv_obj_t* parent, const char* sym, lv_coord_t size, lv_event_cb_t cb, void* ud = nullptr);
  lv_obj_t* orb(lv_obj_t* parent, lv_coord_t size, bool animate = true);   // gradient orb (+ breathing halo); container sized ~1.4x
  void orbSetMode(lv_obj_t* orbRoot, Orb mode);
  lv_obj_t* badge(lv_obj_t* parent, const char* sym, lv_coord_t size);     // gradient circle with a symbol
}
