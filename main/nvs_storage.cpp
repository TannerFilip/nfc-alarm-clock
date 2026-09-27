#include "nvs_storage.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace clock_storage {
namespace {

constexpr char alarm_namespace[] = "clock_alarm";
constexpr char alarm_key[] = "journal_v1";
constexpr char settings_namespace[] = "clock_config";
constexpr char settings_key[] = "settings";
constexpr uint32_t alarm_magic = 0x4d4c414e; // "NALM" in little-endian bytes.
constexpr uint16_t alarm_codec_version = 1;
constexpr uint16_t alarm_header_size = 16;
constexpr size_t max_alarm_record_bytes = 1024;

NvsInitStatus init_status{};

class Handle {
public:
    ~Handle() { if (open_) nvs_close(value_); }
    esp_err_t open(const char* name, nvs_open_mode_t mode) {
        const esp_err_t result = nvs_open(name,mode,&value_);
        open_ = result == ESP_OK;
        return result;
    }
    nvs_handle_t get() const { return value_; }
private:
    nvs_handle_t value_ = 0;
    bool open_ = false;
};

class Writer {
public:
    Writer(uint8_t* data, size_t capacity) : data_(data), capacity_(capacity) {}
    bool u8(uint8_t value) { return bytes(&value,sizeof(value)); }
    bool u16(uint16_t value) {
        const uint8_t encoded[]{static_cast<uint8_t>(value),static_cast<uint8_t>(value >> 8)};
        return bytes(encoded,sizeof(encoded));
    }
    bool u32(uint32_t value) {
        uint8_t encoded[4]{};
        for (unsigned i = 0; i < 4; ++i) encoded[i] = static_cast<uint8_t>(value >> (i * 8));
        return bytes(encoded,sizeof(encoded));
    }
    bool i32(int32_t value) { return u32(static_cast<uint32_t>(value)); }
    bool i64(int64_t value) {
        const uint64_t bits = static_cast<uint64_t>(value);
        uint8_t encoded[8]{};
        for (unsigned i = 0; i < 8; ++i) encoded[i] = static_cast<uint8_t>(bits >> (i * 8));
        return bytes(encoded,sizeof(encoded));
    }
    bool bytes(const void* source, size_t count) {
        if (count > capacity_ - size_) return false;
        std::memcpy(data_ + size_,source,count);
        size_ += count;
        return true;
    }
    size_t size() const { return size_; }
private:
    uint8_t* data_;
    size_t capacity_;
    size_t size_ = 0;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    bool u8(uint8_t& value) { return bytes(&value,sizeof(value)); }
    bool u16(uint16_t& value) {
        uint8_t encoded[2]{};
        if (!bytes(encoded,sizeof(encoded))) return false;
        value = static_cast<uint16_t>(encoded[0]) | static_cast<uint16_t>(encoded[1]) << 8;
        return true;
    }
    bool u32(uint32_t& value) {
        uint8_t encoded[4]{};
        if (!bytes(encoded,sizeof(encoded))) return false;
        value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(encoded[i]) << (i * 8);
        return true;
    }
    bool i32(int32_t& value) {
        uint32_t encoded = 0;
        if (!u32(encoded)) return false;
        value = static_cast<int32_t>(encoded);
        return true;
    }
    bool i64(int64_t& value) {
        uint8_t encoded[8]{};
        if (!bytes(encoded,sizeof(encoded))) return false;
        uint64_t bits = 0;
        for (unsigned i = 0; i < 8; ++i) bits |= static_cast<uint64_t>(encoded[i]) << (i * 8);
        value = static_cast<int64_t>(bits);
        return true;
    }
    bool bytes(void* destination, size_t count) {
        if (count > size_ - position_) return false;
        std::memcpy(destination,data_ + position_,count);
        position_ += count;
        return true;
    }
    bool finished() const { return position_ == size_; }
private:
    const uint8_t* data_;
    size_t size_;
    size_t position_ = 0;
};

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool write_occurrence(Writer& writer, const clock_core::Occurrence& occurrence) {
    return writer.u32(occurrence.alarm_id) && writer.i32(occurrence.local_date) &&
        writer.i64(occurrence.due_utc);
}

bool read_occurrence(Reader& reader, clock_core::Occurrence& occurrence) {
    return reader.u32(occurrence.alarm_id) && reader.i32(occurrence.local_date) &&
        reader.i64(occurrence.due_utc);
}

bool encode_alarm(const clock_core::PersistentAlarmState& state,
                  std::array<uint8_t,max_alarm_record_bytes>& output, size_t& output_size) {
    if (state.schema != 1 || state.active_count > clock_core::max_active ||
        state.authorized_count > clock_core::max_tags || state.history_count > clock_core::max_history)
        return false;

    Writer payload(output.data() + alarm_header_size,output.size() - alarm_header_size);
    if (!payload.u32(state.schema) || !payload.u8(state.cursor_valid ? 1 : 0) ||
        !payload.i64(state.cursor_minute_utc) || !payload.u8(state.active_count)) return false;
    for (uint8_t i = 0; i < state.active_count; ++i)
        if (!write_occurrence(payload,state.active[i])) return false;
    if (!payload.u8(state.authorized_count)) return false;
    for (uint8_t i = 0; i < state.authorized_count; ++i) {
        const auto& tag = state.authorized_tags[i];
        if (tag.size == 0 || tag.size > clock_core::max_tag_bytes || !payload.u8(tag.size) ||
            !payload.bytes(tag.bytes.data(),tag.size)) return false;
    }
    if (!payload.u8(state.history_count)) return false;
    for (uint8_t i = 0; i < state.history_count; ++i)
        if (!write_occurrence(payload,state.history[i])) return false;
    if (!payload.u32(state.missed_count) || !payload.u32(state.skipped_count)) return false;

    Writer header(output.data(),alarm_header_size);
    if (!header.u32(alarm_magic) || !header.u16(alarm_codec_version) ||
        !header.u16(alarm_header_size) || !header.u32(static_cast<uint32_t>(payload.size())) ||
        !header.u32(crc32(output.data() + alarm_header_size,payload.size()))) return false;
    output_size = alarm_header_size + payload.size();
    return true;
}

esp_err_t decode_alarm(const uint8_t* record, size_t record_size,
                       clock_core::PersistentAlarmState& output) {
    if (record_size < alarm_header_size) return ESP_ERR_INVALID_SIZE;
    Reader header(record,alarm_header_size);
    uint32_t magic = 0, payload_size = 0, checksum = 0;
    uint16_t version = 0, header_size = 0;
    if (!header.u32(magic) || !header.u16(version) || !header.u16(header_size) ||
        !header.u32(payload_size) || !header.u32(checksum) || !header.finished())
        return ESP_ERR_INVALID_SIZE;
    if (magic != alarm_magic || version != alarm_codec_version || header_size != alarm_header_size)
        return ESP_ERR_INVALID_VERSION;
    if (payload_size != record_size - alarm_header_size) return ESP_ERR_INVALID_SIZE;
    const uint8_t* payload_data = record + alarm_header_size;
    if (crc32(payload_data,payload_size) != checksum) return ESP_ERR_INVALID_CRC;

    clock_core::PersistentAlarmState decoded{};
    Reader payload(payload_data,payload_size);
    uint8_t cursor_valid = 0;
    if (!payload.u32(decoded.schema) || decoded.schema != 1 || !payload.u8(cursor_valid) ||
        cursor_valid > 1 || !payload.i64(decoded.cursor_minute_utc) ||
        !payload.u8(decoded.active_count) || decoded.active_count > clock_core::max_active)
        return ESP_ERR_INVALID_RESPONSE;
    decoded.cursor_valid = cursor_valid != 0;
    for (uint8_t i = 0; i < decoded.active_count; ++i)
        if (!read_occurrence(payload,decoded.active[i])) return ESP_ERR_INVALID_SIZE;
    if (!payload.u8(decoded.authorized_count) || decoded.authorized_count > clock_core::max_tags)
        return ESP_ERR_INVALID_RESPONSE;
    for (uint8_t i = 0; i < decoded.authorized_count; ++i) {
        auto& tag = decoded.authorized_tags[i];
        if (!payload.u8(tag.size) || tag.size == 0 || tag.size > clock_core::max_tag_bytes ||
            !payload.bytes(tag.bytes.data(),tag.size)) return ESP_ERR_INVALID_RESPONSE;
    }
    if (!payload.u8(decoded.history_count) || decoded.history_count > clock_core::max_history)
        return ESP_ERR_INVALID_RESPONSE;
    for (uint8_t i = 0; i < decoded.history_count; ++i)
        if (!read_occurrence(payload,decoded.history[i])) return ESP_ERR_INVALID_SIZE;
    if (!payload.u32(decoded.missed_count) || !payload.u32(decoded.skipped_count) || !payload.finished())
        return ESP_ERR_INVALID_SIZE;
    output = decoded;
    return ESP_OK;
}

esp_err_t open_namespace(Handle& handle, const char* name, nvs_open_mode_t mode) {
    if (!init_status.ready()) return init_status.error;
    return handle.open(name,mode);
}

esp_err_t save_if_changed(nvs_handle_t handle, const char* key, const void* data, size_t size,
                          uint8_t* compare_buffer, size_t compare_capacity) {
    size_t existing_size = 0;
    esp_err_t result = nvs_get_blob(handle,key,nullptr,&existing_size);
    if (result == ESP_OK && existing_size == size) {
        if (existing_size > compare_capacity) return ESP_ERR_INVALID_SIZE;
        size_t read_size = existing_size;
        result = nvs_get_blob(handle,key,compare_buffer,&read_size);
        if (result != ESP_OK) return result;
        if (read_size != existing_size) return ESP_ERR_INVALID_SIZE;
        if (std::memcmp(compare_buffer,data,size) == 0) return ESP_OK;
    } else if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) {
        return result;
    }
    result = nvs_set_blob(handle,key,data,size);
    return result == ESP_OK ? nvs_commit(handle) : result;
}

} // namespace

NvsInitStatus initialize_nvs() {
    const esp_err_t result = nvs_flash_init();
    init_status.error = result;
    if (result == ESP_OK) init_status.result = NvsInitResult::ready;
    else if (result == ESP_ERR_NVS_NO_FREE_PAGES) init_status.result = NvsInitResult::no_free_pages;
    else if (result == ESP_ERR_NVS_NEW_VERSION_FOUND) init_status.result = NvsInitResult::newer_format;
    else init_status.result = NvsInitResult::fault;
    return init_status;
}

NvsInitStatus nvs_init_status() { return init_status; }

clock_core::LoadResult NvsAlarmStorage::load(clock_core::PersistentAlarmState& state) {
    Handle handle;
    last_error_ = open_namespace(handle,alarm_namespace,NVS_READONLY);
    if (last_error_ == ESP_ERR_NVS_NOT_FOUND) {
        last_error_ = ESP_OK;
        return clock_core::LoadResult::empty;
    }
    if (last_error_ != ESP_OK) return clock_core::LoadResult::fault;

    size_t size = 0;
    last_error_ = nvs_get_blob(handle.get(),alarm_key,nullptr,&size);
    if (last_error_ == ESP_ERR_NVS_NOT_FOUND) {
        last_error_ = ESP_OK;
        return clock_core::LoadResult::empty;
    }
    if (last_error_ != ESP_OK || size > max_alarm_record_bytes || size < alarm_header_size) {
        if (last_error_ == ESP_OK) last_error_ = ESP_ERR_INVALID_SIZE;
        return clock_core::LoadResult::fault;
    }
    std::array<uint8_t,max_alarm_record_bytes> record{};
    const size_t expected_size = size;
    last_error_ = nvs_get_blob(handle.get(),alarm_key,record.data(),&size);
    if (last_error_ != ESP_OK || size != expected_size) {
        if (last_error_ == ESP_OK) last_error_ = ESP_ERR_INVALID_SIZE;
        return clock_core::LoadResult::fault;
    }
    clock_core::PersistentAlarmState decoded{};
    last_error_ = decode_alarm(record.data(),size,decoded);
    if (last_error_ != ESP_OK) return clock_core::LoadResult::fault;
    state = decoded;
    return clock_core::LoadResult::loaded;
}

bool NvsAlarmStorage::save(const clock_core::PersistentAlarmState& state) {
    std::array<uint8_t,max_alarm_record_bytes> record{};
    size_t size = 0;
    if (!encode_alarm(state,record,size)) {
        last_error_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    Handle handle;
    last_error_ = open_namespace(handle,alarm_namespace,NVS_READWRITE);
    if (last_error_ != ESP_OK) return false;
    std::array<uint8_t,max_alarm_record_bytes> existing{};
    last_error_ = save_if_changed(handle.get(),alarm_key,record.data(),size,
                                  existing.data(),existing.size());
    return last_error_ == ESP_OK;
}

BlobLoadResult NvsSettingsBlobStore::load(void* destination, size_t expected_size) {
    if (!destination || expected_size == 0 || expected_size > max_blob_bytes) {
        last_error_ = ESP_ERR_INVALID_ARG;
        return BlobLoadResult::fault;
    }
    Handle handle;
    last_error_ = open_namespace(handle,settings_namespace,NVS_READONLY);
    if (last_error_ == ESP_ERR_NVS_NOT_FOUND) {
        last_error_ = ESP_OK;
        return BlobLoadResult::empty;
    }
    if (last_error_ != ESP_OK) return BlobLoadResult::fault;
    size_t size = 0;
    last_error_ = nvs_get_blob(handle.get(),settings_key,nullptr,&size);
    if (last_error_ == ESP_ERR_NVS_NOT_FOUND) {
        last_error_ = ESP_OK;
        return BlobLoadResult::empty;
    }
    if (last_error_ != ESP_OK) return BlobLoadResult::fault;
    if (size != expected_size) {
        last_error_ = ESP_ERR_INVALID_SIZE;
        return BlobLoadResult::fault;
    }
    last_error_ = nvs_get_blob(handle.get(),settings_key,destination,&size);
    if (last_error_ != ESP_OK || size != expected_size) {
        if (last_error_ == ESP_OK) last_error_ = ESP_ERR_INVALID_SIZE;
        return BlobLoadResult::fault;
    }
    return BlobLoadResult::loaded;
}

bool NvsSettingsBlobStore::save(const void* data, size_t size) {
    if (!data || size == 0 || size > max_blob_bytes) {
        last_error_ = ESP_ERR_INVALID_ARG;
        return false;
    }
    Handle handle;
    last_error_ = open_namespace(handle,settings_namespace,NVS_READWRITE);
    if (last_error_ != ESP_OK) return false;
    last_error_ = save_if_changed(handle.get(),settings_key,data,size,
                                  compare_buffer_.data(),compare_buffer_.size());
    return last_error_ == ESP_OK;
}

} // namespace clock_storage
