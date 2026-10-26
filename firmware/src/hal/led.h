#pragma once
#include <Arduino.h>
enum class LedMode : uint8_t { Off, Idle, Listening, Thinking, Done, Error, Pairing, Typing, Alert };
void ledBegin();
void ledSetMode(LedMode m);
void ledTapFlash();   // brief tap acknowledgement
void ledTick();
