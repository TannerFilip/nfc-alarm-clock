#pragma once
#include "core/alarm_core.hpp"
#include "core/configuration.hpp"
#include "nvs_storage.hpp"
#include "drivers/audio.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>

namespace clock_app {
class AlarmService {
public:
    esp_err_t init(clock_hw::Audio& audio);
    bool tick(bool time_valid, int64_t now_utc);
    bool request_status();
    bool configure(const clock_core::AlarmDefinition& alarm);
    bool set_timezone(clock_core::TimeZone zone);
    bool enroll(const clock_core::TagId& tag);
    bool remove_alarm(uint32_t id);
    bool remove_tag(const clock_core::TagId& tag);
    bool set_display(uint8_t brightness, bool ambient);
    bool set_device_name(const char* name, uint8_t length);
    bool submit_tag(const clock_core::TagId& tag);
    bool submit_physical_tag(const clock_core::TagId& tag);
    bool begin_tag_enrollment(uint32_t duration_seconds = 60);
    bool development_trigger();
    bool set_brightness(uint8_t value);
    bool set_ambient(bool value);
    bool set_alarm_volume(uint8_t value);
    unsigned brightness() const { return brightness_; }
    bool ambient() const { return ambient_; }
    unsigned alarm_volume() const { return alarm_volume_; }
    bool ringing() const { return ringing_; }
    unsigned active_count() const { return active_count_; }
    unsigned missed_count() const { return missed_count_; }
    unsigned skipped_count() const { return skipped_count_; }
    bool storage_fault() const { return storage_fault_; }
    bool settings_fault() const { return settings_fault_; }
    bool dismissal_blocked() const { return dismissal_blocked_; }
    bool enrollment_active() const { return enrollment_active_; }
    const char* state_name() const;
private:
    enum class EventType : uint8_t { tick, status, configure, timezone, enroll, begin_enrollment, physical_tag, tag, trigger, remove_alarm, remove_tag, display, device_name,
                                     brightness, ambient, volume };
    struct Event {
        EventType type = EventType::status;
        bool time_valid = false;
        int64_t now_utc = 0;
        clock_core::AlarmDefinition alarm{};
        clock_core::TagId tag{};
        clock_core::TimeZone zone = clock_core::TimeZone::utc;
        uint8_t value = 0;
        char name[clock_core::max_device_name + 1]{};
    };
    static void entry(void* argument);
    void run();
    bool send(const Event& event);
    void apply(const clock_core::AlarmDecision& decision);
    void publish();
    void report() const;
    bool persist_settings(const clock_core::ClockSettings& candidate);
    QueueHandle_t queue_ = nullptr;
    clock_hw::Audio* audio_ = nullptr;
    clock_storage::NvsAlarmStorage journal_{};
    clock_storage::NvsSettingsBlobStore settings_store_{};
    clock_core::ClockSettings settings_{};
    clock_core::AlarmCore core_{journal_};
    bool time_valid_ = false;
    int64_t now_utc_ = 0;
    std::atomic<bool> ringing_{false}, storage_fault_{false}, settings_fault_{false}, dismissal_blocked_{false};
    std::atomic<bool> enrollment_active_{false};
    int64_t enrollment_expires_at_us_ = 0;
    std::atomic<unsigned> active_count_{0}, missed_count_{0}, skipped_count_{0};
    std::atomic<unsigned> brightness_{79}, alarm_volume_{5};
    std::atomic<bool> ambient_{false};
    std::atomic<clock_core::TimeZone> zone_{clock_core::TimeZone::utc};
};
}
