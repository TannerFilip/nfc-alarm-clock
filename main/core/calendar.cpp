#include "calendar.hpp"
#include <cstring>

namespace clock_core {
namespace {
bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
int days_in_month(int y, int m) {
    constexpr int days[]{31,28,31,30,31,30,31,31,30,31,30,31};
    return days[m - 1] + (m == 2 && leap(y));
}
int bcd(uint8_t v) { return (v & 15) <= 9 && (v >> 4) <= 9 ? (v >> 4) * 10 + (v & 15) : -1; }
uint8_t to_bcd(int v) { return static_cast<uint8_t>((v / 10) * 16 + v % 10); }
}
bool valid_date(const DateTime& v) {
    return v.year >= 2000 && v.year <= 2099 && v.month >= 1 && v.month <= 12 &&
        v.day >= 1 && v.day <= days_in_month(v.year, v.month) &&
        v.hour >= 0 && v.hour < 24 && v.minute >= 0 && v.minute < 60 && v.second >= 0 && v.second < 60;
}
bool parse_utc(const char* s, DateTime& out) {
    if (!s || std::strlen(s) != 20) return false;
    if (s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') return false;
    int values[6]{};
    constexpr int positions[]{0,5,8,11,14,17}, lengths[]{4,2,2,2,2,2};
    for (int i = 0; i < 6; ++i) for (int j = 0; j < lengths[i]; ++j) {
        char c = s[positions[i] + j];
        if (c < '0' || c > '9') return false;
        values[i] = values[i] * 10 + c - '0';
    }
    DateTime v{values[0],values[1],values[2],values[3],values[4],values[5]};
    if (!valid_date(v)) return false;
    out = v;
    return true;
}
int64_t epoch_seconds(const DateTime& v) {
    int64_t days = 10957; // 1970-01-01 to 2000-01-01
    for (int y = 2000; y < v.year; ++y) days += leap(y) ? 366 : 365;
    for (int m = 1; m < v.month; ++m) days += days_in_month(v.year, m);
    return ((days + v.day - 1) * 24 + v.hour) * 3600 + v.minute * 60 + v.second;
}
bool decode_rtc(const uint8_t* r, DateTime& out) {
    if ((r[0] & 0x28) || (r[3] & 0x80) || (r[4] & 0x80) || (r[5] & 0xc0) ||
        (r[6] & 0xc0) || r[7] > 6 || (r[8] & 0xe0)) return false;
    int year = bcd(r[9]);
    if (year < 0) return false;
    DateTime v{2000 + year, bcd(r[8]), bcd(r[6]), bcd(r[5]), bcd(r[4]), bcd(r[3])};
    if (!valid_date(v)) return false;
    out = v;
    return true;
}
void encode_rtc(const DateTime& v, uint8_t* r) {
    r[0] = to_bcd(v.second); r[1] = to_bcd(v.minute); r[2] = to_bcd(v.hour);
    r[3] = to_bcd(v.day); r[4] = (epoch_seconds(v) / 86400 + 4) % 7;
    r[5] = to_bcd(v.month); r[6] = to_bcd(v.year - 2000);
}
bool Debouncer::update(bool pressed, int64_t now_ms) {
    if (pressed != candidate_) { candidate_ = pressed; changed_at_ = now_ms; }
    if (stable_ != candidate_ && now_ms - changed_at_ >= 30) { stable_ = candidate_; return true; }
    return false;
}
int64_t encoder_delta(uint32_t previous, uint32_t current) {
    const uint32_t d = current - previous;
    return d <= 0x7fffffffU ? d : static_cast<int64_t>(d) - 0x100000000LL;
}
}
