#include "core/configuration.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace clock_core;

namespace {

ClockSettings complete_settings() {
    ClockSettings settings = default_clock_settings();
    settings.timezone = TimeZone::america_los_angeles;
    settings.alarms[0] = {17,6,45,0x3e,true};
    settings.alarms[1] = {42,9,5,0x41,false};
    settings.alarm_count = 2;
    settings.tags[0].size = 4;
    settings.tags[0].bytes[0] = 0x04;
    settings.tags[0].bytes[1] = 0xab;
    settings.tags[0].bytes[2] = 0x10;
    settings.tags[0].bytes[3] = 0xff;
    settings.tags[1].size = 10;
    for (uint8_t i = 0; i < 10; ++i) settings.tags[1].bytes[i] = i;
    settings.tag_count = 2;
    settings.brightness = 255;
    settings.ambient_brightness = true;
    settings.alarm_volume = 1;
    const char* name = "Bedside Clock";
    settings.device_name_length = static_cast<uint8_t>(std::strlen(name));
    std::memcpy(settings.device_name.data(),name,settings.device_name_length);
    return settings;
}

void expect_invalid(ClockSettings settings) { assert(!validate_clock_settings(settings)); }

} // namespace

int main() {
    const ClockSettings defaults = default_clock_settings();
    assert(validate_clock_settings(defaults));
    assert(defaults.schema == 1 && defaults.timezone == TimeZone::utc);
    assert(defaults.alarm_count == 0 && defaults.tag_count == 0);
    assert(defaults.brightness == 79 && !defaults.ambient_brightness && defaults.alarm_volume == 5);

    const ClockSettings original = complete_settings();
    assert(validate_clock_settings(original));
    std::array<uint8_t,clock_settings_encoded_size> encoded{};
    assert(encode_clock_settings(original,encoded));
    ClockSettings decoded{};
    assert(decode_clock_settings(encoded.data(),encoded.size(),decoded) == DecodeResult::loaded);
    assert(decoded == original);

    // The format is stable, little-endian and padding-free. These offsets are
    // format commitments, not offsets into ClockSettings.
    assert(encoded[0] == 'N' && encoded[1] == 'A' && encoded[2] == 'C' && encoded[3] == 'S');
    assert(encoded[4] == 1 && encoded[5] == 0 && encoded[6] == 0 && encoded[7] == 0);
    assert(encoded[8] == 1 && encoded[9] == 2);
    assert(encoded[10] == 17 && encoded[11] == 0 && encoded[12] == 0 && encoded[13] == 0);
    assert(encoded[14] == 6 && encoded[15] == 45 && encoded[16] == 0x3e && encoded[17] == 1);
    std::array<uint8_t,clock_settings_encoded_size> encoded_again{};
    assert(encode_clock_settings(original,encoded_again) && encoded_again == encoded);

    ClockSettings unchanged = complete_settings();
    assert(decode_clock_settings(nullptr,0,unchanged) == DecodeResult::empty && unchanged == original);
    assert(decode_clock_settings(encoded.data(),encoded.size() - 1,unchanged) == DecodeResult::corrupt);
    auto corrupt = encoded;
    corrupt[40] ^= 0x80;
    assert(decode_clock_settings(corrupt.data(),corrupt.size(),unchanged) == DecodeResult::corrupt);
    auto unsupported = encoded;
    unsupported[4] = 2;
    assert(decode_clock_settings(unsupported.data(),unsupported.size(),unchanged) == DecodeResult::unsupported_schema);

    // Every scalar boundary and both accepted timezone values.
    ClockSettings value = defaults;
    value.brightness = 1; assert(validate_clock_settings(value));
    value.brightness = 255; assert(validate_clock_settings(value));
    value.brightness = 0; expect_invalid(value);
    value = defaults; value.alarm_volume = 1; assert(validate_clock_settings(value));
    value.alarm_volume = 5; assert(validate_clock_settings(value));
    value.alarm_volume = 0; expect_invalid(value);
    value = defaults; value.alarm_volume = 6; expect_invalid(value);
    value = defaults; value.timezone = TimeZone::america_los_angeles; assert(validate_clock_settings(value));
    value.timezone = static_cast<TimeZone>(2); expect_invalid(value);
    value = defaults; value.alarm_count = static_cast<uint8_t>(max_alarms + 1); expect_invalid(value);
    value = defaults; value.tag_count = static_cast<uint8_t>(max_tags + 1); expect_invalid(value);

    // Alarm time, weekday, identifier, uniqueness and unused-slot boundaries.
    value = defaults; value.alarm_count = 1; value.alarms[0] = {1,23,59,0x7f,true}; value.tag_count = 1; value.tags[0].size = 1; assert(validate_clock_settings(value));
    value.alarms[0].hour = 24; expect_invalid(value);
    value.alarms[0] = {1,23,60,0x7f,true}; expect_invalid(value);
    value.alarms[0] = {1,23,59,0,true}; expect_invalid(value);
    value.alarms[0] = {1,23,59,0x80,true}; expect_invalid(value);
    value.alarms[0] = {0,23,59,1,true}; expect_invalid(value);
    value = defaults; value.alarm_count = 2; value.alarms[0] = {1,0,0,1,true}; value.alarms[1] = {1,1,0,1,true}; expect_invalid(value);
    value = defaults; value.alarms[1] = {1,0,0,1,true}; expect_invalid(value);
    value = defaults; value.alarm_count = 1; value.alarms[0] = {1,7,0,0x7f,true}; expect_invalid(value);
    value.alarms[0].enabled = false; assert(validate_clock_settings(value));

    // Tag length, canonical unused bytes, uniqueness and unused slots.
    value = defaults; value.tag_count = 1; value.tags[0].size = 1; value.tags[0].bytes[0] = 0; assert(validate_clock_settings(value));
    value.tags[0].size = max_tag_bytes; assert(validate_clock_settings(value));
    value.tags[0].size = 0; expect_invalid(value);
    value.tags[0].size = static_cast<uint8_t>(max_tag_bytes + 1); expect_invalid(value);
    value = defaults; value.tag_count = 1; value.tags[0].size = 1; value.tags[0].bytes[1] = 1; expect_invalid(value);
    value = defaults; value.tag_count = 2; value.tags[0].size = value.tags[1].size = 1; value.tags[0].bytes[0] = value.tags[1].bytes[0] = 7; expect_invalid(value);
    value = defaults; value.tags[1].size = 1; expect_invalid(value);

    // Optional printable ASCII name: empty through 24 bytes are accepted.
    value = defaults; value.device_name_length = 24;
    for (size_t i = 0; i < max_device_name; ++i) value.device_name[i] = (i == 0) ? ' ' : '~';
    assert(validate_clock_settings(value));
    value.device_name_length = 25; expect_invalid(value);
    value = defaults; value.device_name_length = 1; value.device_name[0] = 0x1f; expect_invalid(value);
    value.device_name[0] = 0x7f; expect_invalid(value);
    value = defaults; value.device_name[1] = 'x'; expect_invalid(value);

    // Invalid values cannot be serialized, and failed decodes never replace output.
    value = defaults; value.brightness = 0;
    assert(!encode_clock_settings(value,encoded_again));
    unchanged = original;
    assert(decode_clock_settings(nullptr,encoded.size(),unchanged) == DecodeResult::corrupt);
    assert(unchanged == original);

    std::cout << "PASS: bounded settings validation, stable codec, schema and corruption handling\n";
}
