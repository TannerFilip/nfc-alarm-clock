#include "alarm_core.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace clock_core {
namespace {
int date_key(const DateTime& value) { return value.year * 10000 + value.month * 100 + value.day; }
bool same_occurrence(const Occurrence& a, const Occurrence& b) {
    return a.alarm_id == b.alarm_id && a.local_date == b.local_date;
}
bool same_local(const DateTime& a, const DateTime& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day &&
        a.hour == b.hour && a.minute == b.minute;
}
int sunday_on_or_after(int year, int month, int day) {
    DateTime value{year,month,day,0,0,0};
    return day + (7 - weekday(value)) % 7;
}
int sunday_on_or_before(int year, int month, int day) {
    DateTime value{year,month,day,0,0,0};
    return day - weekday(value);
}
void la_boundaries(int year, int64_t& start, int64_t& end) {
    const int start_day = year >= 2007 ? sunday_on_or_after(year,3,8) : sunday_on_or_after(year,4,1);
    const int end_day = year >= 2007 ? sunday_on_or_after(year,11,1) : sunday_on_or_before(year,10,31);
    // 02:00 local: PST (UTC-8) at start, PDT (UTC-7) at end.
    start = epoch_seconds({year,year >= 2007 ? 3 : 4,start_day,10,0,0});
    end = epoch_seconds({year,year >= 2007 ? 11 : 10,end_day,9,0,0});
}
bool valid_tag(const TagId& tag) { return tag.size > 0 && tag.size <= max_tag_bytes; }
bool valid_occurrence(const Occurrence& value) {
    return value.alarm_id != 0 && value.local_date >= 19700101 && value.local_date <= 20991231 && value.due_utc >= 0;
}
}

const char* timezone_name(TimeZone zone) {
    return zone == TimeZone::america_los_angeles ? "America/Los_Angeles" : "UTC";
}
bool parse_timezone(const char* text, TimeZone& zone) {
    if (!text) return false;
    if (!std::strcmp(text,"UTC")) { zone = TimeZone::utc; return true; }
    if (!std::strcmp(text,"America/Los_Angeles")) { zone = TimeZone::america_los_angeles; return true; }
    return false;
}
LocalTime to_local(int64_t utc, TimeZone zone) {
    if (zone == TimeZone::utc) return {epoch_datetime(utc),0,false};
    const int year = epoch_datetime(utc).year;
    int64_t start = 0, end = 0;
    la_boundaries(year,start,end);
    const bool daylight = utc >= start && utc < end;
    const int offset = daylight ? -7 * 60 : -8 * 60;
    return {epoch_datetime(utc + offset * 60),offset,daylight};
}
bool parse_tag(const char* text, TagId& tag) {
    if (!text) return false;
    const size_t length = std::strlen(text);
    if (length < 2 || length > max_tag_bytes * 2 || length % 2) return false;
    TagId result{};
    result.size = static_cast<uint8_t>(length / 2);
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < result.size; ++i) {
        const int high = digit(text[i*2]), low = digit(text[i*2+1]);
        if (high < 0 || low < 0) return false;
        result.bytes[i] = static_cast<uint8_t>(high * 16 + low);
    }
    tag = result;
    return true;
}
bool same_tag(const TagId& left, const TagId& right) {
    return left.size == right.size && left.size > 0 &&
        std::equal(left.bytes.begin(),left.bytes.begin()+left.size,right.bytes.begin());
}

bool AlarmCore::valid_persistent(const PersistentAlarmState& value) const {
    if (value.schema != 1 || value.active_count > max_active || value.authorized_count > max_tags ||
        value.history_count > max_history || (value.cursor_valid && value.cursor_minute_utc % 60)) return false;
    for (uint8_t i = 0; i < value.active_count; ++i) if (!valid_occurrence(value.active[i])) return false;
    for (uint8_t i = 0; i < value.history_count; ++i) if (!valid_occurrence(value.history[i])) return false;
    for (uint8_t i = 0; i < value.authorized_count; ++i) if (!valid_tag(value.authorized_tags[i])) return false;
    return value.active_count == 0 || value.authorized_count > 0;
}
bool AlarmCore::commit(const PersistentAlarmState& value) {
    if (!storage_.save(value)) { storage_fault_ = true; return false; }
    state_ = value;
    storage_fault_ = false;
    return true;
}
AlarmDecision AlarmCore::initialize() {
    PersistentAlarmState loaded{};
    const auto result = storage_.load(loaded);
    initialized_ = true;
    if (result == LoadResult::fault || (result == LoadResult::loaded && !valid_persistent(loaded))) {
        storage_fault_ = true;
        recovery_blocked_ = true;
        return {AlarmEvent::storage_fault};
    }
    if (result == LoadResult::loaded) {
        state_ = loaded;
        evaluated_cursor_valid_ = state_.cursor_valid;
        evaluated_cursor_minute_utc_ = state_.cursor_minute_utc;
    }
    return {AlarmEvent::initialized,state_.active_count != 0,false};
}
bool AlarmCore::install_configuration(TimeZone zone, const AlarmDefinition* alarms,
                                      uint8_t alarm_count, const TagId* tags, uint8_t tag_count) {
    if ((zone != TimeZone::utc && zone != TimeZone::america_los_angeles) ||
        alarm_count > max_alarms || tag_count > max_tags ||
        (alarm_count && !alarms) || (tag_count && !tags)) return false;
    bool enabled = false;
    for (uint8_t i = 0; i < alarm_count; ++i) {
        const auto& alarm = alarms[i];
        if (alarm.id == 0 || alarm.hour > 23 || alarm.minute > 59 ||
            (alarm.weekdays & 0x7f) == 0 || (alarm.weekdays & 0x80)) return false;
        enabled |= alarm.enabled;
        for (uint8_t earlier = 0; earlier < i; ++earlier)
            if (alarms[earlier].id == alarm.id) return false;
    }
    if (enabled && tag_count == 0) return false;
    for (uint8_t i = 0; i < tag_count; ++i) {
        if (!valid_tag(tags[i])) return false;
        for (uint8_t earlier = 0; earlier < i; ++earlier)
            if (same_tag(tags[earlier],tags[i])) return false;
    }
    alarms_ = {};
    enrolled_ = {};
    for (uint8_t i = 0; i < alarm_count; ++i) alarms_[i] = alarms[i];
    for (uint8_t i = 0; i < tag_count; ++i) enrolled_[i] = tags[i];
    alarm_count_ = alarm_count;
    enrolled_count_ = tag_count;
    zone_ = zone;
    return true;
}
bool AlarmCore::restore_configuration(TimeZone zone, const AlarmDefinition* alarms,
                                      uint8_t alarm_count, const TagId* tags, uint8_t tag_count) {
    return !initialized_ && install_configuration(zone,alarms,alarm_count,tags,tag_count);
}
bool AlarmCore::replace_configuration(TimeZone zone, const AlarmDefinition* alarms,
                                      uint8_t alarm_count, const TagId* tags, uint8_t tag_count) {
    return mutations_allowed() && install_configuration(zone,alarms,alarm_count,tags,tag_count);
}
bool AlarmCore::upsert_alarm(const AlarmDefinition& alarm) {
    if (!mutations_allowed() || enrolled_count_ == 0 || alarm.id == 0 || alarm.hour > 23 ||
        alarm.minute > 59 || (alarm.weekdays & 0x7f) == 0 || (alarm.weekdays & 0x80)) return false;
    for (uint8_t i = 0; i < alarm_count_; ++i) if (alarms_[i].id == alarm.id) { alarms_[i] = alarm; return true; }
    if (alarm_count_ == max_alarms) return false;
    alarms_[alarm_count_++] = alarm;
    return true;
}
bool AlarmCore::set_timezone(TimeZone zone) {
    if (!mutations_allowed()) return false;
    zone_ = zone;
    return true;
}
bool AlarmCore::enroll(const TagId& tag) {
    if (!mutations_allowed() || !valid_tag(tag)) return false;
    for (uint8_t i = 0; i < enrolled_count_; ++i) if (same_tag(enrolled_[i],tag)) return true;
    if (enrolled_count_ == max_tags) return false;
    enrolled_[enrolled_count_++] = tag;
    return true;
}
bool AlarmCore::history_contains(const PersistentAlarmState& value, const Occurrence& occurrence) const {
    for (uint8_t i = 0; i < value.history_count; ++i) if (same_occurrence(value.history[i],occurrence)) return true;
    return false;
}
void AlarmCore::add_history(PersistentAlarmState& value, const Occurrence& occurrence) const {
    if (value.history_count < max_history) value.history[value.history_count++] = occurrence;
    else {
        std::move(value.history.begin()+1,value.history.end(),value.history.begin());
        value.history.back() = occurrence;
    }
}
bool AlarmCore::active_contains(const PersistentAlarmState& value, const Occurrence& occurrence) const {
    for (uint8_t i = 0; i < value.active_count; ++i) if (same_occurrence(value.active[i],occurrence)) return true;
    return false;
}
void AlarmCore::capture_authorization(PersistentAlarmState& value) const {
    value.authorized_count = enrolled_count_;
    for (uint8_t i = 0; i < enrolled_count_; ++i) value.authorized_tags[i] = enrolled_[i];
}
AlarmDecision AlarmCore::evaluate(bool time_valid, int64_t now_utc) {
    if (!initialized_ || !time_valid || now_utc < 0) return {};
    if (recovery_blocked_) return {AlarmEvent::storage_fault};
    const int64_t now_minute = now_utc - now_utc % 60;
    if (!evaluated_cursor_valid_) {
        auto candidate = state_;
        candidate.cursor_valid = true;
        candidate.cursor_minute_utc = now_minute;
        if (!commit(candidate)) return {AlarmEvent::storage_fault};
        evaluated_cursor_valid_ = true;
        evaluated_cursor_minute_utc_ = now_minute;
        return {AlarmEvent::initialized};
    }
    if (now_minute <= evaluated_cursor_minute_utc_) return {};
    auto candidate = state_;
    const bool was_ringing = candidate.active_count != 0;
    bool triggered = false, missed = false;
    bool skipped = false;
    const int64_t first_local_day = (evaluated_cursor_minute_utc_ / 86400) - 2;
    const int64_t last_local_day = (now_minute / 86400) + 2;
    for (int64_t day = first_local_day; day <= last_local_day; ++day) {
        const auto date = epoch_datetime(day * 86400);
        for (uint8_t index = 0; index < alarm_count_; ++index) {
            const auto& alarm = alarms_[index];
            if (!alarm.enabled || !(alarm.weekdays & (1U << weekday(date)))) continue;
            const DateTime local{date.year,date.month,date.day,alarm.hour,alarm.minute,0};
            // Daylight candidate comes first so the first fall-fold occurrence wins.
            const int offsets[2]{zone_ == TimeZone::america_los_angeles ? -7*60 : 0,
                                 zone_ == TimeZone::america_los_angeles ? -8*60 : 0};
            const int offset_count = zone_ == TimeZone::america_los_angeles ? 2 : 1;
            bool valid_candidate = false, candidate_reached = false;
            for (int oi = 0; oi < offset_count; ++oi) {
                const int64_t due = epoch_seconds(local) - offsets[oi] * 60;
                if (due > evaluated_cursor_minute_utc_ && due <= now_minute) candidate_reached = true;
                if (!same_local(to_local(due,zone_).value,local)) continue; // Invalid side of an offset change.
                valid_candidate = true;
                if (due <= evaluated_cursor_minute_utc_ || due > now_minute) continue;
                Occurrence occurrence{alarm.id,date_key(local),due};
                if (history_contains(candidate,occurrence)) continue; // DST fall fold and duplicate evaluation.
                add_history(candidate,occurrence);
                if (now_utc - due <= missed_window_seconds) {
                    if (!active_contains(candidate,occurrence) && candidate.active_count < max_active) {
                        if (candidate.active_count == 0) capture_authorization(candidate);
                        candidate.active[candidate.active_count++] = occurrence;
                        triggered = true;
                    }
                } else {
                    if (candidate.missed_count != std::numeric_limits<uint32_t>::max()) ++candidate.missed_count;
                    missed = true;
                }
            }
            if (!valid_candidate && candidate_reached) {
                Occurrence occurrence{alarm.id,date_key(local),epoch_seconds(local) + 8 * 3600};
                if (!history_contains(candidate,occurrence)) {
                    add_history(candidate,occurrence);
                    if (candidate.skipped_count != std::numeric_limits<uint32_t>::max()) ++candidate.skipped_count;
                    skipped = true;
                }
            }
        }
    }
    candidate.cursor_minute_utc = now_minute;
    // Cursor-only checkpoints are at most once per five minutes. Occurrences and
    // missed counts are always committed immediately with their new cursor.
    if (!triggered && !missed && !skipped && now_minute - state_.cursor_minute_utc < 5 * 60) {
        evaluated_cursor_minute_utc_ = now_minute;
        return {};
    }
    if (!commit(candidate)) return {AlarmEvent::storage_fault};
    evaluated_cursor_minute_utc_ = now_minute;
    if (triggered) return {AlarmEvent::triggered,!was_ringing,false};
    if (missed) return {AlarmEvent::missed};
    if (skipped) return {AlarmEvent::skipped};
    return {};
}
AlarmDecision AlarmCore::handle_tag(const TagId& tag) {
    if (!initialized_ || !valid_tag(tag)) return {AlarmEvent::invalid};
    if (recovery_blocked_) return {AlarmEvent::storage_fault};
    if (!ringing()) {
        for (uint8_t i = 0; i < enrolled_count_; ++i)
            if (same_tag(enrolled_[i],tag)) return {AlarmEvent::authorized_while_idle};
        return {AlarmEvent::unknown_tag};
    }
    bool authorized = false;
    for (uint8_t i = 0; i < state_.authorized_count; ++i) authorized |= same_tag(state_.authorized_tags[i],tag);
    if (!authorized) return {AlarmEvent::unknown_tag};
    auto candidate = state_;
    candidate.active_count = 0;
    candidate.authorized_count = 0;
    if (!commit(candidate)) return {AlarmEvent::storage_fault};
    return {AlarmEvent::dismissed,false,true};
}
AlarmDecision AlarmCore::development_trigger(int64_t now_utc) {
    if (!mutations_allowed() || enrolled_count_ == 0 || now_utc < 0) return {AlarmEvent::rejected_while_ringing};
    auto candidate = state_;
    const auto local = to_local(now_utc,zone_).value;
    Occurrence occurrence{0xffffffffU,date_key(local),now_utc};
    capture_authorization(candidate);
    candidate.active[0] = occurrence;
    candidate.active_count = 1;
    add_history(candidate,occurrence);
    if (!commit(candidate)) return {AlarmEvent::storage_fault};
    return {AlarmEvent::triggered,true,false};
}
AlarmView AlarmCore::view() const {
    return {initialized_,ringing(),storage_fault_,state_.active_count,state_.missed_count,state_.skipped_count,zone_};
}
}
