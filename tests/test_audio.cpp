#include "core/test_tone.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace clock_core;
int main() {
    uint8_t level = 0;
    assert(parse_test_level(nullptr,level) && level == 1);
    assert(parse_test_level("1",level) && level == 1);
    assert(parse_test_level("5",level) && level == 5);
    for (const char* bad : {"", "0", "2", "6", "100", "-5", "5.0", "5x", " 5", "05"})
        assert(!parse_test_level(bad,level));
    double energy[2]{};
    for (int k = 0; k < 2; ++k) {
        const uint8_t percent = k ? 5 : 1;
        int crossings = 0, previous = 0;
        assert(test_tone_sample(-1,percent) == 0);
        assert(test_tone_sample(0,percent) == 0);
        assert(test_tone_sample(test_tone_frames-1,percent) == 0);
        assert(test_tone_sample(test_tone_frames,percent) == 0);
        for (int frame = 0; frame < test_tone_frames; ++frame) {
            const int sample = test_tone_sample(frame,percent);
            assert(std::abs(sample) <= 327 * percent);
            if (frame < 1600) assert(std::abs(sample) <= 327 * percent * frame / 1600.0 + 1);
            if (frame >= test_tone_frames-1600)
                assert(std::abs(sample) <= 327 * percent * (test_tone_frames-1-frame) / 1600.0 + 1);
            if (frame >= 1600 && frame < 17600) { // One second of constant amplitude
                if (previous <= 0 && sample > 0) ++crossings;
                previous = sample;
                energy[k] += double(sample)*sample;
            }
        }
        assert(crossings >= 439 && crossings <= 441);
        assert(energy[k] > 0);
    }
    const double ratio = std::sqrt(energy[1]/energy[0]);
    assert(ratio > 4.95 && ratio < 5.05);
    for (int invalid : {0,2,6,100,255}) assert(test_tone_sample(1701,invalid) == 0);
    std::cout << "PASS: audio levels, duration, frequency, peak limits, fades, nonzero PCM\n";
}
