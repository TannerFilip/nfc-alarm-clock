#include "core/calendar.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace clock_core;
int main() {
    DateTime v{};
    assert(parse_utc("2000-01-01T00:00:00Z", v));
    assert(epoch_seconds(v) == 946684800);
    assert(epoch_seconds(DateTime{1970,1,1,0,0,0}) == 0);
    assert(weekday(DateTime{1970,1,1,0,0,0}) == 4);
    assert(epoch_datetime(0).year == 1970 && epoch_datetime(0).day == 1);
    assert(parse_utc("2024-02-29T23:59:59Z", v));
    assert(epoch_seconds(v) == 1709251199);
    for (const char* bad : {"2023-02-29T00:00:00Z", "2100-01-01T00:00:00Z", "2024-04-31T00:00:00Z",
        "2024-00-01T00:00:00Z", "2024-01-00T00:00:00Z", "2024-01-01T24:00:00Z",
        "2024-01-01T00:60:00Z", "2024-01-01T00:00:60Z", "2024-01-01T00:00:00Zjunk",
        "2024-01-01T00:00:00", "202x-01-01T00:00:00Z"}) assert(!parse_utc(bad,v));
    // Round-trip every supported day; decoding must not normalize bad input.
    for (int y = 2000; y <= 2099; ++y) for (int m = 1; m <= 12; ++m) for (int d = 1; d <= 31; ++d) {
        DateTime source{y,m,d,23,59,58}, decoded{};
        if (!valid_date(source)) continue;
        uint8_t r[10]{};
        encode_rtc(source, r + 3);
        assert(decode_rtc(r, decoded));
        assert(epoch_seconds(source) == epoch_seconds(decoded));
    }
    uint8_t good[10]{};
    encode_rtc(DateTime{2024,2,29,12,34,56}, good + 3);
    for (auto pair : {std::pair<int,uint8_t>{0,0x20},{0,0x08},{3,0xd6},{3,0x6a},{4,0x60},
                     {5,0x24},{6,0x30},{7,7},{8,0},{9,0xfa}}) {
        uint8_t bad[10]; std::memcpy(bad,good,10); bad[pair.first] = pair.second;
        assert(!decode_rtc(bad,v));
    }
    Debouncer b;
    assert(!b.update(true,0)); assert(!b.update(false,10)); assert(!b.update(true,20));
    assert(!b.update(true,49)); assert(b.update(true,50)); assert(b.pressed());
    assert(!b.update(true,1000)); assert(!b.update(false,1010)); assert(b.update(false,1040));
    assert(!b.pressed());
    assert(encoder_delta(0xffffffffU,0) == 1);
    assert(encoder_delta(0,0xffffffffU) == -1);
    assert(encoder_delta(0x7fffffffU,0x80000000U) == 1);
    std::cout << "PASS: calendar/epoch/BCD, supported dates, UTC parsing, debounce, encoder rollover\n";
}
