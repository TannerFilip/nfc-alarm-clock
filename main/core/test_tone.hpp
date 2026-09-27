#pragma once
#include <cstdint>

namespace clock_core {
constexpr int test_sample_rate = 16000;
constexpr int test_tone_frames = test_sample_rate * 2;
// Deliberately limited diagnostic levels, not a general volume control.
bool parse_test_level(const char* argument, uint8_t& level);
int16_t test_tone_sample(int frame, uint8_t level);
}
