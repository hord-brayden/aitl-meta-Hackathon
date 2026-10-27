#pragma once
#include <stdint.h>
#include "sdkconfig.h"
#define HAPPY_ANIM_WIDTH     60
#define HAPPY_ANIM_HEIGHT    60
#define HAPPY_ANIM_FRAMES    32
#define HAPPY_ANIM_FRAME_MS  130
#if CONFIG_HOMEHUB_DISPLAY
extern const uint16_t happy_anim_palette[58];
extern const uint8_t happy_anim_frames[HAPPY_ANIM_FRAMES][HAPPY_ANIM_HEIGHT * HAPPY_ANIM_WIDTH];
#endif
