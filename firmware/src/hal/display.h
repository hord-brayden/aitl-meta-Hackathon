#pragma once
#include <Arduino.h>
#include <lvgl.h>

struct TouchRaw { bool touched; uint16_t x, y; };  // native controller coordinates (240x320 space)

void displayBegin();
void displayApplyPanel();        // re-init panel with current invert/bgr/rotation settings
void displayApplyBrightness();
void displayWake();
void displayPowerTick();
bool displayIsAwake();
TouchRaw displayReadTouchRaw();  // last sample, uncalibrated (used by the calibration wizard)
void displaySetCalibrating(bool on);  // when on, LVGL still gets mapped points but with identity flags
lv_coord_t displayW();
lv_coord_t displayH();
void displayPerfReport(uint32_t handlerUs, uint32_t handlerMaxUs, uint32_t loops);
