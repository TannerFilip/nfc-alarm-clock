#pragma once

#include "alarm_core.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace clock_core {

constexpr uint32_t clock_settings_schema = 1;
constexpr size_t max_device_name = 24;

// The byte representation is deliberately fixed and independent of compiler
// padding. Keep format changes behind a new schema version.
constexpr size_t clock_settings_encoded_size = 195;

struct ClockSettings {
    uint32_t schema = clock_settings_schema;
    TimeZone timezone = TimeZone::utc;
    std::array<AlarmDefinition,max_alarms> alarms{};
    uint8_t alarm_count = 0;
    std::array<TagId,max_tags> tags{};
    uint8_t tag_count = 0;
    uint8_t brightness = 79;
    bool ambient_brightness = false;
    uint8_t alarm_volume = 5;
    uint8_t device_name_length = 0;
    // One extra byte makes the in-memory value convenient as a C string. Only
    // the first max_device_name bytes are stored; validation requires a zeroed
    // terminator and zeroed unused tail for a canonical representation.
    std::array<char,max_device_name + 1> device_name{};
};

ClockSettings default_clock_settings();
bool validate_clock_settings(const ClockSettings& settings);
bool operator==(const ClockSettings& left, const ClockSettings& right);
inline bool operator!=(const ClockSettings& left, const ClockSettings& right) {
    return !(left == right);
}

enum class DecodeResult { empty, loaded, unsupported_schema, corrupt };

// Returns false rather than serializing an invalid or non-canonical value.
bool encode_clock_settings(
    const ClockSettings& settings,
    std::array<uint8_t,clock_settings_encoded_size>& encoded);

// A missing blob is represented by size == 0. Stored malformed or erased data
// is corrupt, not empty, so callers never silently replace a bad record.
DecodeResult decode_clock_settings(const uint8_t* encoded, size_t size, ClockSettings& settings);

} // namespace clock_core
