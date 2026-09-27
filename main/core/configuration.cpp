#include "configuration.hpp"

#include <algorithm>

namespace clock_core {
namespace {

constexpr std::array<uint8_t,4> magic{{'N','A','C','S'}};

void put_u32(std::array<uint8_t,clock_settings_encoded_size>& output, size_t& offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) output[offset++] = static_cast<uint8_t>(value >> shift);
}

uint32_t get_u32(const uint8_t* input, size_t& offset) {
    uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= uint32_t(input[offset++]) << shift;
    return value;
}

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool zero_alarm(const AlarmDefinition& alarm) {
    return alarm.id == 0 && alarm.hour == 0 && alarm.minute == 0 && alarm.weekdays == 0 && !alarm.enabled;
}

bool zero_tag(const TagId& tag) {
    return tag.size == 0 && std::all_of(tag.bytes.begin(),tag.bytes.end(),[](uint8_t byte) { return byte == 0; });
}

bool valid_alarm(const AlarmDefinition& alarm) {
    return alarm.id != 0 && alarm.hour <= 23 && alarm.minute <= 59 &&
           (alarm.weekdays & 0x7fU) != 0 && (alarm.weekdays & 0x80U) == 0;
}

bool valid_tag(const TagId& tag) {
    if (tag.size == 0 || tag.size > max_tag_bytes) return false;
    return std::all_of(tag.bytes.begin() + tag.size,tag.bytes.end(),[](uint8_t byte) { return byte == 0; });
}

} // namespace

ClockSettings default_clock_settings() { return {}; }

bool validate_clock_settings(const ClockSettings& value) {
    if (value.schema != clock_settings_schema ||
        (value.timezone != TimeZone::utc && value.timezone != TimeZone::america_los_angeles) ||
        value.alarm_count > max_alarms || value.tag_count > max_tags ||
        value.brightness == 0 || value.alarm_volume == 0 || value.alarm_volume > 5 ||
        value.device_name_length > max_device_name) return false;

    bool any_enabled_alarm = false;
    for (size_t i = 0; i < value.alarms.size(); ++i) {
        if (i < value.alarm_count) {
            if (!valid_alarm(value.alarms[i])) return false;
            any_enabled_alarm |= value.alarms[i].enabled;
            for (size_t earlier = 0; earlier < i; ++earlier)
                if (value.alarms[earlier].id == value.alarms[i].id) return false;
        } else if (!zero_alarm(value.alarms[i])) return false;
    }

    for (size_t i = 0; i < value.tags.size(); ++i) {
        if (i < value.tag_count) {
            if (!valid_tag(value.tags[i])) return false;
            for (size_t earlier = 0; earlier < i; ++earlier)
                if (same_tag(value.tags[earlier],value.tags[i])) return false;
        } else if (!zero_tag(value.tags[i])) return false;
    }

    if (any_enabled_alarm && value.tag_count == 0) return false;

    for (size_t i = 0; i < value.device_name_length; ++i) {
        const auto byte = static_cast<unsigned char>(value.device_name[i]);
        if (byte < 0x20 || byte > 0x7e) return false;
    }
    for (size_t i = value.device_name_length; i < value.device_name.size(); ++i)
        if (value.device_name[i] != '\0') return false;
    return true;
}

bool operator==(const ClockSettings& left, const ClockSettings& right) {
    if (left.schema != right.schema || left.timezone != right.timezone ||
        left.alarm_count != right.alarm_count || left.tag_count != right.tag_count ||
        left.brightness != right.brightness || left.ambient_brightness != right.ambient_brightness ||
        left.alarm_volume != right.alarm_volume ||
        left.device_name_length != right.device_name_length || left.device_name != right.device_name)
        return false;
    for (size_t i = 0; i < left.alarms.size(); ++i) {
        const auto& a = left.alarms[i];
        const auto& b = right.alarms[i];
        if (a.id != b.id || a.hour != b.hour || a.minute != b.minute ||
            a.weekdays != b.weekdays || a.enabled != b.enabled) return false;
    }
    for (size_t i = 0; i < left.tags.size(); ++i)
        if (left.tags[i].size != right.tags[i].size || left.tags[i].bytes != right.tags[i].bytes) return false;
    return true;
}

bool encode_clock_settings(
    const ClockSettings& value,
    std::array<uint8_t,clock_settings_encoded_size>& output) {
    if (!validate_clock_settings(value)) return false;
    output.fill(0);
    size_t offset = 0;
    for (uint8_t byte : magic) output[offset++] = byte;
    put_u32(output,offset,value.schema);
    output[offset++] = static_cast<uint8_t>(value.timezone);
    output[offset++] = value.alarm_count;
    for (const auto& alarm : value.alarms) {
        put_u32(output,offset,alarm.id);
        output[offset++] = alarm.hour;
        output[offset++] = alarm.minute;
        output[offset++] = alarm.weekdays;
        output[offset++] = alarm.enabled ? 1 : 0;
    }
    output[offset++] = value.tag_count;
    for (const auto& tag : value.tags) {
        output[offset++] = tag.size;
        for (uint8_t byte : tag.bytes) output[offset++] = byte;
    }
    output[offset++] = value.brightness;
    output[offset++] = value.ambient_brightness ? 1 : 0;
    output[offset++] = value.alarm_volume;
    output[offset++] = value.device_name_length;
    for (size_t i = 0; i < max_device_name; ++i)
        output[offset++] = static_cast<uint8_t>(value.device_name[i]);

    const size_t checksum_offset = output.size() - sizeof(uint32_t);
    if (offset != checksum_offset) return false;
    put_u32(output,offset,crc32(output.data(),checksum_offset));
    return offset == output.size();
}

DecodeResult decode_clock_settings(const uint8_t* input, size_t size, ClockSettings& output) {
    if (size == 0) return DecodeResult::empty;
    if (input == nullptr || size != clock_settings_encoded_size ||
        !std::equal(magic.begin(),magic.end(),input)) return DecodeResult::corrupt;

    size_t offset = magic.size();
    const uint32_t schema = get_u32(input,offset);
    if (schema != clock_settings_schema) return DecodeResult::unsupported_schema;
    const size_t checksum_offset = size - sizeof(uint32_t);
    size_t checksum_read_offset = checksum_offset;
    if (get_u32(input,checksum_read_offset) != crc32(input,checksum_offset)) return DecodeResult::corrupt;

    ClockSettings decoded{};
    decoded.schema = schema;
    decoded.timezone = static_cast<TimeZone>(input[offset++]);
    decoded.alarm_count = input[offset++];
    for (auto& alarm : decoded.alarms) {
        alarm.id = get_u32(input,offset);
        alarm.hour = input[offset++];
        alarm.minute = input[offset++];
        alarm.weekdays = input[offset++];
        const uint8_t enabled = input[offset++];
        if (enabled > 1) return DecodeResult::corrupt;
        alarm.enabled = enabled != 0;
    }
    decoded.tag_count = input[offset++];
    for (auto& tag : decoded.tags) {
        tag.size = input[offset++];
        for (auto& byte : tag.bytes) byte = input[offset++];
    }
    decoded.brightness = input[offset++];
    const uint8_t ambient = input[offset++];
    if (ambient > 1) return DecodeResult::corrupt;
    decoded.ambient_brightness = ambient != 0;
    decoded.alarm_volume = input[offset++];
    decoded.device_name_length = input[offset++];
    for (size_t i = 0; i < max_device_name; ++i) decoded.device_name[i] = static_cast<char>(input[offset++]);
    decoded.device_name[max_device_name] = '\0';
    if (offset != checksum_offset || !validate_clock_settings(decoded)) return DecodeResult::corrupt;
    output = decoded;
    return DecodeResult::loaded;
}

} // namespace clock_core
