#pragma once
#include "core/alarm_core.hpp"
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
    bool submit_tag(const clock_core::TagId& tag);
    bool development_trigger();
    bool ringing() const { return ringing_; }
    unsigned active_count() const { return active_count_; }
    unsigned missed_count() const { return missed_count_; }
    unsigned skipped_count() const { return skipped_count_; }
    bool storage_fault() const { return storage_fault_; }
    const char* state_name() const;
private:
    enum class EventType : uint8_t { tick, status, configure, timezone, enroll, tag, trigger };
    struct Event {
        EventType type = EventType::status;
        bool time_valid = false;
        int64_t now_utc = 0;
        clock_core::AlarmDefinition alarm{};
        clock_core::TagId tag{};
        clock_core::TimeZone zone = clock_core::TimeZone::utc;
    };
    class RamStorage final : public clock_core::AlarmStorage {
    public:
        clock_core::LoadResult load(clock_core::PersistentAlarmState& state) override;
        bool save(const clock_core::PersistentAlarmState& state) override;
    private:
        bool present_ = false;
        clock_core::PersistentAlarmState state_{};
    };
    static void entry(void* argument);
    void run();
    bool send(const Event& event);
    void apply(const clock_core::AlarmDecision& decision);
    void publish();
    void report() const;
    QueueHandle_t queue_ = nullptr;
    clock_hw::Audio* audio_ = nullptr;
    RamStorage storage_{};
    clock_core::AlarmCore core_{storage_};
    bool time_valid_ = false;
    int64_t now_utc_ = 0;
    std::atomic<bool> ringing_{false}, storage_fault_{false};
    std::atomic<unsigned> active_count_{0}, missed_count_{0}, skipped_count_{0};
    std::atomic<clock_core::TimeZone> zone_{clock_core::TimeZone::utc};
};
}
