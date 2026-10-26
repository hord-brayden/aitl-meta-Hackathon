#pragma once
#include <lvgl.h>
// Caeruli Obscuri palette (caeruli-obscuri.com/styles.css) + UI metrics
#define CO_BLACK      0x000000
#define CO_VOID       0x02050d
#define CO_SHADOW     0x00030a
#define CO_ABYSS      0x050a18
#define CO_MIDNIGHT   0x08122a
#define CO_DEEP       0x0e1d3d
#define CO_CARD       0x0b1a33
#define CO_MID        0x1a3a5c
#define CO_CAERULEI   0x4ac6ff
#define CO_CAERULEI2  0x6fd4ff
#define CO_CAERULEI_D 0x1e6fa3
#define CO_BONE       0xe8eef5
#define CO_ASH        0xaab6c8
#define CO_MUTE       0x6b7a8f
#define CO_AMBER      0xf5c26b
#define CO_PHOSPHOR   0xb8e1d1
#define CO_RED        0xff5c6c
#define CO_BULLET     "\xE2\x80\xA2"   // "•" (in LVGL's Montserrat)

namespace theme {
  extern lv_style_t screen;     // black background
  extern lv_style_t panel;      // card: abyss fill, mid border, rounded
  extern lv_style_t btn;        // card-colored button
  extern lv_style_t btnPressed;
  extern lv_style_t btnPrimary; // caerulei filled
  extern lv_style_t mono;       // small uppercase label, caerulei-deep (section labels, hints)
  extern lv_style_t monoBig;    // same, larger, caerulei
  extern lv_style_t title;      // 20/28 bone
  extern lv_style_t body;       // 14/18 bone
  extern lv_style_t muted;      // 12/16 mute
  extern lv_style_t chip;       // pill
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
  lv_obj_t* makeButton(lv_obj_t* parent, const char* text, bool primary = false);
  lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, lv_style_t* st);
  lv_obj_t* makeHeader(lv_obj_t* parent, const char* section, const char* title);
}
