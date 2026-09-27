#pragma once

#include "core/alarm_core.hpp"
#include "esp_err.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace clock_storage {

enum class NvsInitResult : uint8_t {
    ready,
    no_free_pages,
    newer_format,
    fault,
};

struct NvsInitStatus {
    NvsInitResult result = NvsInitResult::fault;
    esp_err_t error = ESP_ERR_INVALID_STATE;

    bool ready() const { return result == NvsInitResult::ready; }
};

// Initializes the default NVS partition without ever erasing it. In particular,
// recovery from a full or newer-format partition is a policy decision for the
// caller rather than an implicit data-loss operation here.
NvsInitStatus initialize_nvs();
NvsInitStatus nvs_init_status();

class NvsAlarmStorage final : public clock_core::AlarmStorage {
public:
    clock_core::LoadResult load(clock_core::PersistentAlarmState& state) override;
    bool save(const clock_core::PersistentAlarmState& state) override;

    esp_err_t last_error() const { return last_error_; }
    bool faulted() const { return last_error_ != ESP_OK; }

private:
    esp_err_t last_error_ = ESP_OK;
};

enum class BlobLoadResult : uint8_t { empty, loaded, fault };

// Storage for an already encoded, versioned settings record. The configuration
// codec owns its schema and validation; this adapter enforces an exact requested
// size on load and a hard upper bound on all records.
class NvsSettingsBlobStore final {
public:
    static constexpr size_t max_blob_bytes = 2048;

    BlobLoadResult load(void* destination, size_t expected_size);
    bool save(const void* data, size_t size);

    esp_err_t last_error() const { return last_error_; }
    bool faulted() const { return last_error_ != ESP_OK; }

private:
    std::array<uint8_t,max_blob_bytes> compare_buffer_{};
    esp_err_t last_error_ = ESP_OK;
};

} // namespace clock_storage
