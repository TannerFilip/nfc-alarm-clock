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
int16_t alarm_tone_sample(int64_t frame) {
    if (frame < 0) return 0;
    const float progress = std::min(1.0f,frame / static_cast<float>(alarm_ramp_frames));
    const float peak = 327.0f * (1.0f + 4.0f * progress);
    const int phase_frame = static_cast<int>(frame % test_sample_rate);
    return static_cast<int16_t>(peak *
        std::sin(6.28318530718f * 440 * phase_frame / test_sample_rate));
}
}
