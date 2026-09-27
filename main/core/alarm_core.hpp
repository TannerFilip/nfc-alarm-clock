#pragma once
#include "calendar.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace clock_core {
constexpr size_t max_alarms = 8;
constexpr size_t max_tags = 8;
constexpr size_t max_tag_bytes = 10;
constexpr size_t max_active = 8;
constexpr size_t max_history = 32;
constexpr int64_t missed_window_seconds = 5 * 60;

enum class TimeZone : uint8_t { utc, america_los_angeles };
const char* timezone_name(TimeZone zone);
bool parse_timezone(const char* text, TimeZone& zone);

struct LocalTime {
    DateTime value{};
    int offset_minutes = 0;
    bool daylight = false;
};
LocalTime to_local(int64_t utc_seconds, TimeZone zone);

struct TagId {
    uint8_t size = 0;
    std::array<uint8_t,max_tag_bytes> bytes{};
};
bool parse_tag(const char* text, TagId& tag);
bool same_tag(const TagId& left, const TagId& right);

struct AlarmDefinition {
    uint32_t id = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t weekdays = 0; // Bit 0=Sunday through bit 6=Saturday.
    bool enabled = false;
};

struct Occurrence {
    uint32_t alarm_id = 0;
    int32_t local_date = 0; // YYYYMMDD; stable across UTC offset changes.
    int64_t due_utc = 0;
};

struct PersistentAlarmState {
    uint32_t schema = 1;
    bool cursor_valid = false;
    int64_t cursor_minute_utc = 0;
    std::array<Occurrence,max_active> active{};
    uint8_t active_count = 0;
    std::array<TagId,max_tags> authorized_tags{};
    uint8_t authorized_count = 0;
    std::array<Occurrence,max_history> history{};
    uint8_t history_count = 0;
    uint32_t missed_count = 0;
    uint32_t skipped_count = 0;
};

enum class LoadResult { empty, loaded, fault };
class AlarmStorage {
public:
    virtual ~AlarmStorage() = default;
    virtual LoadResult load(PersistentAlarmState& state) = 0;
    virtual bool save(const PersistentAlarmState& state) = 0;
};

enum class AlarmEvent {
    none,
    initialized,
    triggered,
    missed,
    skipped,
    dismissed,
    unknown_tag,
    authorized_while_idle,
    rejected_while_ringing,
    invalid,
    storage_fault
};
struct AlarmDecision {
    AlarmEvent event = AlarmEvent::none;
    bool start_audio = false;
    bool stop_audio = false;
};
struct AlarmView {
    bool initialized = false;
    bool ringing = false;
    bool storage_fault = false;
    uint8_t active_count = 0;
    uint32_t missed_count = 0;
    uint32_t skipped_count = 0;
    TimeZone zone = TimeZone::utc;
};

class AlarmCore {
public:
    explicit AlarmCore(AlarmStorage& storage) : storage_(storage) {}
    AlarmDecision initialize();
    bool restore_configuration(TimeZone zone, const AlarmDefinition* alarms, uint8_t alarm_count,
                               const TagId* tags, uint8_t tag_count);
    bool replace_configuration(TimeZone zone, const AlarmDefinition* alarms, uint8_t alarm_count,
                               const TagId* tags, uint8_t tag_count);
    bool upsert_alarm(const AlarmDefinition& alarm);
    bool set_timezone(TimeZone zone);
    bool enroll(const TagId& tag);
    AlarmDecision evaluate(bool time_valid, int64_t now_utc);
    AlarmDecision handle_tag(const TagId& tag);
    AlarmDecision development_trigger(int64_t now_utc);
    AlarmView view() const;
    bool ringing() const { return state_.active_count != 0; }
    bool mutations_allowed() const { return initialized_ && !recovery_blocked_ && !ringing(); }
private:
    bool valid_persistent(const PersistentAlarmState& state) const;
    bool commit(const PersistentAlarmState& state);
    bool history_contains(const PersistentAlarmState& state, const Occurrence& occurrence) const;
    void add_history(PersistentAlarmState& state, const Occurrence& occurrence) const;
    bool active_contains(const PersistentAlarmState& state, const Occurrence& occurrence) const;
    void capture_authorization(PersistentAlarmState& state) const;
    bool install_configuration(TimeZone zone, const AlarmDefinition* alarms, uint8_t alarm_count,
                               const TagId* tags, uint8_t tag_count);
    AlarmStorage& storage_;
    PersistentAlarmState state_{};
    std::array<AlarmDefinition,max_alarms> alarms_{};
    uint8_t alarm_count_ = 0;
    std::array<TagId,max_tags> enrolled_{};
    uint8_t enrolled_count_ = 0;
    TimeZone zone_ = TimeZone::utc;
    bool initialized_ = false;
    bool storage_fault_ = false;
    bool recovery_blocked_ = false;
    bool evaluated_cursor_valid_ = false;
    int64_t evaluated_cursor_minute_utc_ = 0;
};
}
