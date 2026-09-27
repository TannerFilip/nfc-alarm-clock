#pragma once
#include <cstdint>

namespace clock_core {
struct DateTime { int year, month, day, hour, minute, second; };
bool valid_date(const DateTime& value);
bool parse_utc(const char* text, DateTime& result);
int64_t epoch_seconds(const DateTime& value);
// Input is Control_1 through Years (10 bytes). Reject stopped/12h/OS clocks,
// reserved bits, invalid BCD and impossible calendar dates.
bool decode_rtc(const uint8_t* registers, DateTime& result);
void encode_rtc(const DateTime& value, uint8_t* seven_time_registers);

class Debouncer {
public:
    bool update(bool pressed, int64_t now_ms);
    bool pressed() const { return stable_; }
private:
    bool candidate_ = false, stable_ = false;
    int64_t changed_at_ = 0;
};
// Modulo arithmetic avoids undefined signed overflow at counter rollover.
int64_t encoder_delta(uint32_t previous, uint32_t current);
}
