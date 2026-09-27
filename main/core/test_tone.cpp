#include "test_tone.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace clock_core {
bool parse_test_level(const char* argument, uint8_t& level) {
    if (!argument || !std::strcmp(argument,"1")) { level = 1; return true; }
    if (!std::strcmp(argument,"5")) { level = 5; return true; }
    return false;
}
int16_t test_tone_sample(int frame, uint8_t level) {
    if (frame < 0 || frame >= test_tone_frames || (level != 1 && level != 5)) return 0;
    const float envelope = std::min(1.0f,std::min(frame / 1600.0f,(test_tone_frames-1-frame)/1600.0f));
    return static_cast<int16_t>(327.0f * level * envelope *
        std::sin(6.28318530718f * 440 * frame / test_sample_rate));
}
}
