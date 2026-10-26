#include "led.h"
#include "../config.h"

static LedMode s_mode = LedMode::Idle;
static uint32_t s_flashUntil = 0;
static uint32_t s_modeSince = 0;
static void ledWrite(uint8_t r, uint8_t g, uint8_t b) {   // active low
  ledcWrite(0, 255 - r); ledcWrite(1, 255 - g); ledcWrite(2, 255 - b);
}
void ledBegin() {
  ledcSetup(0, 5000, 8); ledcSetup(1, 5000, 8); ledcSetup(2, 5000, 8);
  ledcAttachPin(PIN_LED_R, 0); ledcAttachPin(PIN_LED_G, 1); ledcAttachPin(PIN_LED_B, 2);
  ledWrite(0, 0, 0);
}
void ledSetMode(LedMode m) { if (m != s_mode) { s_mode = m; s_modeSince = millis(); } }
void ledTapFlash() { s_flashUntil = millis() + 40; }
static uint8_t breathe(uint32_t period, uint8_t lo, uint8_t hi) {
  float ph = (millis() % period) / (float)period;
  float v = 0.5f - 0.5f * cosf(ph * 6.2831853f);
  return lo + (uint8_t)((hi - lo) * v);
}
void ledTick() {
  uint32_t now = millis();
  if (now < s_flashUntil) { ledWrite(0, 0, 40); return; }
  switch (s_mode) {
    case LedMode::Off:       ledWrite(0, 0, 0); break;
    case LedMode::Idle:      { uint8_t v = breathe(4000, 1, 10); ledWrite(0, (uint8_t)(v * 0.8f), v); break; }  // caerulei breathe
    case LedMode::Listening: { uint8_t v = breathe(900, 20, 160); ledWrite(v, (uint8_t)(v * 0.6f), 0); break; } // amber
    case LedMode::Thinking:  { uint8_t v = breathe(600, 5, 120); ledWrite(0, (uint8_t)(v * 0.7f), v); break; }  // caerulei pulse
    case LedMode::Done:      { ledWrite(0, 90, 40); if (now - s_modeSince > 1200) ledSetMode(LedMode::Idle); break; }
    case LedMode::Error:     { bool on = (now / 150) & 1; ledWrite(on ? 160 : 0, 0, 0); if (now - s_modeSince > 2500) ledSetMode(LedMode::Idle); break; }
    case LedMode::Pairing:   { bool on = (now / 400) & 1; ledWrite(0, on ? 60 : 0, on ? 140 : 0); break; }
    case LedMode::Alert:     { bool on = (now / 250) & 1; ledWrite(on ? 200 : 0, on ? 110 : 0, 0); break; }  // amber strobe: needs you
    case LedMode::Typing:    { bool on = (now / 80) & 1; ledWrite(0, on ? 40 : 0, on ? 100 : 0); break; }
  }
}
