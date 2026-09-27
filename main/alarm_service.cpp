#include "alarm_service.hpp"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace clock_app {
namespace {
const char* event_name(clock_core::AlarmEvent event) {
    using E = clock_core::AlarmEvent;
    switch (event) {
    case E::none: return "none";
    case E::initialized: return "initialized";
    case E::triggered: return "triggered";
    case E::missed: return "missed";
    case E::skipped: return "skipped nonexistent DST local time";
    case E::dismissed: return "dismissed";
    case E::unknown_tag: return "unknown tag (alarm continues)";
    case E::authorized_while_idle: return "authorized tag while idle (no action)";
    case E::rejected_while_ringing: return "rejected while ringing";
    case E::invalid: return "invalid";
    case E::storage_fault: return "storage fault (alarm state unchanged)";
    }
    return "unknown";
}
}
clock_core::LoadResult AlarmService::RamStorage::load(clock_core::PersistentAlarmState& state) {
    if (!present_) return clock_core::LoadResult::empty;
    state = state_;
    return clock_core::LoadResult::loaded;
}
bool AlarmService::RamStorage::save(const clock_core::PersistentAlarmState& state) {
    state_ = state;
    present_ = true;
    return true;
}
esp_err_t AlarmService::init(clock_hw::Audio& audio) {
    audio_ = &audio;
    queue_ = xQueueCreate(16,sizeof(Event));
    if (!queue_) return ESP_ERR_NO_MEM;
    if (xTaskCreate(entry,"clock_alarm",6144,this,4,nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
bool AlarmService::send(const Event& event) {
    return queue_ && xQueueSend(queue_,&event,0) == pdTRUE;
}
bool AlarmService::tick(bool valid, int64_t now) {
    Event event{}; event.type = EventType::tick; event.time_valid = valid; event.now_utc = now;
    return send(event);
}
bool AlarmService::request_status() { Event event{}; event.type = EventType::status; return send(event); }
bool AlarmService::configure(const clock_core::AlarmDefinition& alarm) {
    Event event{}; event.type = EventType::configure; event.alarm = alarm; return send(event);
}
bool AlarmService::set_timezone(clock_core::TimeZone zone) {
    Event event{}; event.type = EventType::timezone; event.zone = zone; return send(event);
}
bool AlarmService::enroll(const clock_core::TagId& tag) {
    Event event{}; event.type = EventType::enroll; event.tag = tag; return send(event);
}
bool AlarmService::submit_tag(const clock_core::TagId& tag) {
    Event event{}; event.type = EventType::tag; event.tag = tag; return send(event);
}
bool AlarmService::development_trigger() { Event event{}; event.type = EventType::trigger; return send(event); }
void AlarmService::entry(void* argument) { static_cast<AlarmService*>(argument)->run(); }
void AlarmService::apply(const clock_core::AlarmDecision& decision) {
    if (decision.event != clock_core::AlarmEvent::none && decision.event != clock_core::AlarmEvent::initialized)
        ESP_LOGW("alarm", "%s",event_name(decision.event));
    if (decision.start_audio && !audio_->start_alarm())
        ESP_LOGE("alarm", "ringing state active but audio start is busy/unavailable; retrying on ticks");
    if (decision.stop_audio && !audio_->stop_alarm())
        ESP_LOGW("alarm", "dismissed durably; audio was already stopped/unavailable");
    publish();
}
void AlarmService::publish() {
    const auto view = core_.view();
    ringing_ = view.ringing;
    storage_fault_ = view.storage_fault;
    active_count_ = view.active_count;
    missed_count_ = view.missed_count;
    skipped_count_ = view.skipped_count;
    zone_ = view.zone;
}
const char* AlarmService::state_name() const {
    if (storage_fault_) return "STORAGE FAULT";
    return ringing_ ? "RINGING" : "IDLE";
}
void AlarmService::report() const {
#ifdef CONFIG_CLOCK_SIMULATED_NFC
    constexpr const char* persistence = "RAM-ONLY-DEVELOPMENT";
#else
    constexpr const char* persistence = "RAM-ONLY; PRODUCTION CONFIG DISABLED";
#endif
    ESP_LOGI("alarm", "state=%s active=%u missed=%u skipped=%u zone=%s persistence=%s",
             state_name(),active_count_.load(),missed_count_.load(),skipped_count_.load(),
             clock_core::timezone_name(zone_.load()),persistence);
}
void AlarmService::run() {
    apply(core_.initialize());
    Event event{};
    while (true) {
        if (xQueueReceive(queue_,&event,portMAX_DELAY) != pdTRUE) continue;
        switch (event.type) {
        case EventType::tick:
            time_valid_ = event.time_valid; now_utc_ = event.now_utc;
            apply(core_.evaluate(time_valid_,now_utc_));
            if (core_.ringing() && !audio_->alarm_active() && !audio_->start_alarm())
                ESP_LOGE("alarm", "audio remains unavailable while alarm is active");
            break;
        case EventType::status: report(); break;
        case EventType::configure:
            ESP_LOGI("alarm", "RAM-only alarm configuration %s",core_.upsert_alarm(event.alarm)?"accepted":"rejected (enroll a tag first / ringing / invalid)");
            publish(); break;
        case EventType::timezone:
            ESP_LOGI("alarm", "RAM-only timezone %s",core_.set_timezone(event.zone)?"accepted":"rejected while ringing");
            publish(); break;
        case EventType::enroll:
            ESP_LOGI("alarm", "RAM-only tag enrollment %s",core_.enroll(event.tag)?"accepted":"rejected while ringing/full/invalid");
            publish(); break;
        case EventType::tag: apply(core_.handle_tag(event.tag)); break;
        case EventType::trigger:
            if (!time_valid_) ESP_LOGW("alarm", "development trigger rejected: set valid UTC first");
            else apply(core_.development_trigger(now_utc_));
            break;
        }
    }
}
}
