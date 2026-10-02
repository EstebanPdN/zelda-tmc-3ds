#pragma once

#include <cstdint>

#define NUM_NOTES      128
#define PROG_UNDEFINED 0xFF
#define MAX_TRACKS     16

namespace {
    const uint32_t AGB_CYCLES_PER_SEC = 16 * 1024 * 1024;
    const uint32_t AGB_CYCLES_PER_FRAME = 280896;
    const double AGB_EXACT_FPS = static_cast<double>(AGB_CYCLES_PER_SEC) / static_cast<double>(AGB_CYCLES_PER_FRAME);
    const uint32_t AGB_APPROX_FPS = 60;
}

// for increased quality we process in subframes (including the base frame)
#define INTERFRAMES 4

#define SONG_FADE_OUT_TIME 10000
#define SONG_FINISH_TIME   1000

#define WINDOW_MIN_WIDTH  80
#define WINDOW_MIN_HEIGHT 24
#define WINDOW_MAX_WIDTH  512
#define WINDOW_MAX_HEIGHT 128

#define LOUDNESS_LP_FREQ 5.0f
