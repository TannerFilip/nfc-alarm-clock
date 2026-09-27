#include "alarm_service.hpp"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <cstring>

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
esp_err_t AlarmService::init(clock_hw::Audio& audio) {
    audio_ = &audio;
    settings_ = clock_core::default_clock_settings();
    std::array<uint8_t,clock_core::clock_settings_encoded_size> encoded{};
    const auto loaded = settings_store_.load(encoded.data(),encoded.size());
    if (loaded == clock_storage::BlobLoadResult::loaded) {
        const auto decoded = clock_core::decode_clock_settings(encoded.data(),encoded.size(),settings_);
        if (decoded != clock_core::DecodeResult::loaded) {
            settings_ = clock_core::default_clock_settings();
            settings_fault_ = true;
            ESP_LOGE("alarm", "settings record rejected: schema/corruption fault; not erased");
        }
    } else if (loaded == clock_storage::BlobLoadResult::fault) {
        settings_fault_ = true;
        ESP_LOGE("alarm", "settings NVS load fault: %s; not erased",esp_err_to_name(settings_store_.last_error()));
    }
#ifndef CONFIG_CLOCK_SIMULATED_NFC
    auto runtime_settings = settings_;
    bool suppressed = false;
    for (uint8_t i = 0; i < runtime_settings.alarm_count; ++i) {
        suppressed |= runtime_settings.alarms[i].enabled;
        runtime_settings.alarms[i].enabled = false;
    }
    if (suppressed) {
        ESP_LOGE("alarm", "enabled schedules suppressed: production has no physical NFC dismissal; record not erased");
    }
#else
    const auto& runtime_settings = settings_;
#endif
    if (!core_.restore_configuration(runtime_settings.timezone,runtime_settings.alarms.data(),
                                     runtime_settings.alarm_count,runtime_settings.tags.data(),
                                     runtime_settings.tag_count)) return ESP_ERR_INVALID_STATE;
    brightness_ = settings_.brightness;
    ambient_ = settings_.ambient_brightness;
    alarm_volume_ = settings_.alarm_volume;
    zone_ = settings_.timezone;
    queue_ = xQueueCreate(16,sizeof(Event));
    if (!queue_) return ESP_ERR_NO_MEM;
    if (xTaskCreate(entry,"clock_alarm",7168,this,4,nullptr) != pdPASS) {
        vQueueDelete(queue_); queue_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
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
#ifndef CONFIG_CLOCK_SIMULATED_NFC
    if (alarm.enabled) return false;
#endif
    Event event{}; event.type = EventType::configure; event.alarm = alarm; return send(event);
}
bool AlarmService::set_timezone(clock_core::TimeZone zone) {
    Event event{}; event.type = EventType::timezone; event.zone = zone; return send(event);
}
bool AlarmService::enroll(const clock_core::TagId& tag) {
    Event event{}; event.type = EventType::enroll; event.tag = tag; return send(event);
}
bool AlarmService::remove_alarm(uint32_t id) { Event event{}; event.type = EventType::remove_alarm; event.alarm.id = id; return send(event); }
bool AlarmService::remove_tag(const clock_core::TagId& tag) { Event event{}; event.type = EventType::remove_tag; event.tag = tag; return send(event); }
bool AlarmService::set_display(uint8_t brightness, bool ambient) { Event event{}; event.type = EventType::display; event.value = brightness; event.time_valid = ambient; return send(event); }
bool AlarmService::set_device_name(const char* name, uint8_t length) {
    if (!name || length > clock_core::max_device_name) return false;
    Event event{}; event.type = EventType::device_name; event.value = length;
    std::memcpy(event.name,name,length);
    return send(event);
}
bool AlarmService::submit_tag(const clock_core::TagId& tag) {
    Event event{}; event.type = EventType::tag; event.tag = tag; return send(event);
}
bool AlarmService::development_trigger() { Event event{}; event.type = EventType::trigger; return send(event); }
bool AlarmService::set_brightness(uint8_t value) { Event event{}; event.type = EventType::brightness; event.value = value; return send(event); }
bool AlarmService::set_ambient(bool value) { Event event{}; event.type = EventType::ambient; event.value = value; return send(event); }
bool AlarmService::set_alarm_volume(uint8_t value) { Event event{}; event.type = EventType::volume; event.value = value; return send(event); }
void AlarmService::entry(void* argument) { static_cast<AlarmService*>(argument)->run(); }
void AlarmService::apply(const clock_core::AlarmDecision& decision) {
    if (decision.event != clock_core::AlarmEvent::none && decision.event != clock_core::AlarmEvent::initialized)
        ESP_LOGW("alarm", "%s",event_name(decision.event));
#ifdef CONFIG_CLOCK_SIMULATED_NFC
    if (decision.start_audio && !audio_->start_alarm(alarm_volume_.load()))
        ESP_LOGE("alarm", "ringing state active but audio start is busy/unavailable; retrying on ticks");
#else
    if (decision.start_audio) {
        dismissal_blocked_ = true;
        ESP_LOGE("alarm", "active journal blocked and silenced: production has no dismissal path; reflash development to recover");
    }
#endif
    if (decision.stop_audio && !audio_->stop_alarm())
        ESP_LOGW("alarm", "dismissed durably; audio was already stopped/unavailable");
    publish();
}
void AlarmService::publish() {
    const auto view = core_.view();
    ringing_ = view.ringing;
    storage_fault_ = view.storage_fault || settings_fault_.load();
    active_count_ = view.active_count;
    missed_count_ = view.missed_count;
    skipped_count_ = view.skipped_count;
    zone_ = view.zone;
}
const char* AlarmService::state_name() const {
    if (storage_fault_) return "STORAGE FAULT";
    if (dismissal_blocked_) return "ACTIVE BLOCKED";
    return ringing_ ? "RINGING" : "IDLE";
}
void AlarmService::report() const {
    constexpr const char* persistence = "NVS SETTINGS + JOURNAL";
    ESP_LOGI("alarm", "state=%s active=%u missed=%u skipped=%u zone=%s persistence=%s",
             state_name(),active_count_.load(),missed_count_.load(),skipped_count_.load(),
             clock_core::timezone_name(zone_.load()),persistence);
    ESP_LOGI("alarm", "settings brightness=%u ambient=%d volume=%u fault=%d",
             brightness_.load(),ambient_.load(),alarm_volume_.load(),settings_fault_.load());
    if (dismissal_blocked_) ESP_LOGE("alarm", "active journal preserved but audio blocked: no production dismissal path");
}
bool AlarmService::persist_settings(const clock_core::ClockSettings& candidate) {
    if (settings_fault_ || !core_.mutations_allowed() || !clock_core::validate_clock_settings(candidate)) return false;
    std::array<uint8_t,clock_core::clock_settings_encoded_size> encoded{};
    if (!clock_core::encode_clock_settings(candidate,encoded) ||
        !settings_store_.save(encoded.data(),encoded.size())) {
        settings_fault_ = true;
        publish();
        ESP_LOGE("alarm", "settings save failed: %s",esp_err_to_name(settings_store_.last_error()));
        return false;
    }
#ifndef CONFIG_CLOCK_SIMULATED_NFC
    auto runtime_candidate = candidate;
    for (uint8_t i = 0; i < runtime_candidate.alarm_count; ++i) runtime_candidate.alarms[i].enabled = false;
#else
    const auto& runtime_candidate = candidate;
#endif
    if (!core_.replace_configuration(runtime_candidate.timezone,runtime_candidate.alarms.data(),
                                     runtime_candidate.alarm_count,runtime_candidate.tags.data(),
                                     runtime_candidate.tag_count)) {
        settings_fault_ = true;
        publish();
        ESP_LOGE("alarm", "settings persisted but core rejected atomic apply");
        return false;
    }
    settings_ = candidate;
    brightness_ = settings_.brightness;
    ambient_ = settings_.ambient_brightness;
    alarm_volume_ = settings_.alarm_volume;
    publish();
    return true;
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
#ifdef CONFIG_CLOCK_SIMULATED_NFC
            if (core_.ringing() && !audio_->alarm_active() && !audio_->start_alarm(alarm_volume_.load()))
                ESP_LOGE("alarm", "audio remains unavailable while alarm is active");
#endif
            break;
        case EventType::status: report(); break;
        case EventType::configure: {
            auto candidate = settings_;
            bool found = false;
            for (uint8_t i = 0; i < candidate.alarm_count; ++i) {
                if (candidate.alarms[i].id == event.alarm.id) { candidate.alarms[i] = event.alarm; found = true; break; }
            }
            const bool accepted = found || candidate.alarm_count < clock_core::max_alarms;
            if (accepted && !found) candidate.alarms[candidate.alarm_count++] = event.alarm;
            ESP_LOGI("alarm", "persistent alarm configuration %s",
                     accepted && persist_settings(candidate) ? "accepted" : "rejected");
            break;
        }
        case EventType::timezone: {
            auto candidate = settings_; candidate.timezone = event.zone;
            ESP_LOGI("alarm", "persistent timezone %s",persist_settings(candidate)?"accepted":"rejected");
            break;
        }
        case EventType::enroll: {
            auto candidate = settings_;
            bool found = false;
            for (uint8_t i = 0; i < candidate.tag_count; ++i) found |= clock_core::same_tag(candidate.tags[i],event.tag);
            const bool accepted = found || candidate.tag_count < clock_core::max_tags;
            if (accepted && !found) candidate.tags[candidate.tag_count++] = event.tag;
            ESP_LOGI("alarm", "persistent tag enrollment %s",
                     accepted && persist_settings(candidate) ? "accepted" : "rejected");
            break;
        }
        case EventType::remove_alarm: {
            auto candidate = settings_;
            uint8_t index = candidate.alarm_count;
            for (uint8_t i = 0; i < candidate.alarm_count; ++i) if (candidate.alarms[i].id == event.alarm.id) { index = i; break; }
            if (index < candidate.alarm_count) {
                for (uint8_t i = index + 1; i < candidate.alarm_count; ++i) candidate.alarms[i-1] = candidate.alarms[i];
                candidate.alarms[--candidate.alarm_count] = {};
            }
            ESP_LOGI("alarm", "persistent alarm removal %s",
                     index < settings_.alarm_count && persist_settings(candidate) ? "accepted" : "rejected");
            break;
        }
        case EventType::remove_tag: {
            auto candidate = settings_;
            uint8_t index = candidate.tag_count;
            for (uint8_t i = 0; i < candidate.tag_count; ++i) if (clock_core::same_tag(candidate.tags[i],event.tag)) { index = i; break; }
            if (index < candidate.tag_count) {
                for (uint8_t i = index + 1; i < candidate.tag_count; ++i) candidate.tags[i-1] = candidate.tags[i];
                candidate.tags[--candidate.tag_count] = {};
            }
            ESP_LOGI("alarm", "persistent tag removal %s",
                     index < settings_.tag_count && persist_settings(candidate) ? "accepted" : "rejected");
            break;
        }
        case EventType::display: {
            auto candidate = settings_; candidate.brightness = event.value; candidate.ambient_brightness = event.time_valid;
            ESP_LOGI("alarm", "persistent display settings %s",persist_settings(candidate)?"accepted":"rejected");
            break;
        }
        case EventType::device_name: {
            auto candidate = settings_; candidate.device_name = {}; candidate.device_name_length = event.value;
            std::memcpy(candidate.device_name.data(),event.name,event.value);
            ESP_LOGI("alarm", "persistent device name %s",persist_settings(candidate)?"accepted":"rejected");
            break;
        }
        case EventType::tag: apply(core_.handle_tag(event.tag)); break;
        case EventType::trigger:
            if (!time_valid_) ESP_LOGW("alarm", "development trigger rejected: set valid UTC first");
            else apply(core_.development_trigger(now_utc_));
            break;
        case EventType::brightness: {
            auto candidate = settings_; candidate.brightness = event.value;
            ESP_LOGI("alarm", "persistent brightness %s",persist_settings(candidate)?"accepted":"rejected");
            break;
        }
        case EventType::ambient: {
            auto candidate = settings_; candidate.ambient_brightness = event.value != 0;
            ESP_LOGI("alarm", "persistent ambient mode %s",persist_settings(candidate)?"accepted":"rejected");
            break;
        }
        case EventType::volume: {
            auto candidate = settings_; candidate.alarm_volume = event.value;
            ESP_LOGI("alarm", "persistent alarm volume %s",persist_settings(candidate)?"accepted":"rejected");
            break;
        }
        }
    }
}
}
