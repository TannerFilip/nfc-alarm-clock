#include "core/alarm_core.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace clock_core;

namespace {
struct MemoryStorage final : AlarmStorage {
    LoadResult result = LoadResult::empty;
    PersistentAlarmState state{};
    bool fail_save = false;
    int saves = 0;
    LoadResult load(PersistentAlarmState& output) override {
        if (result == LoadResult::loaded) output = state;
        return result;
    }
    bool save(const PersistentAlarmState& value) override {
        ++saves;
        if (fail_save) return false;
        state = value;
        result = LoadResult::loaded;
        return true;
    }
};
int64_t utc(const char* text) {
    DateTime value{};
    assert(parse_utc(text,value));
    return epoch_seconds(value);
}
TagId tag(const char* text) {
    TagId value{};
    assert(parse_tag(text,value));
    return value;
}
void prepare(AlarmCore& core, const TagId& enrolled, uint8_t hour, uint8_t minute,
             uint8_t weekdays = 0x7f, uint32_t id = 1) {
    assert(core.initialize().event == AlarmEvent::initialized);
    assert(core.enroll(enrolled));
    assert(core.upsert_alarm({id,hour,minute,weekdays,true}));
}
}

int main() {
    // Tag parsing is bounded and canonical byte comparison ignores text case.
    TagId parsed{};
    assert(parse_tag("04aB10ff",parsed) && parsed.size == 4);
    assert(same_tag(parsed,tag("04AB10FF")));
    const char* invalid_tags[]{nullptr,"","0","xyz","00112233445566778899aa"};
    for (const char* bad : invalid_tags) assert(!parse_tag(bad,parsed));

    TimeZone zone{};
    assert(parse_timezone("UTC",zone) && zone == TimeZone::utc);
    assert(parse_timezone("America/Los_Angeles",zone) && zone == TimeZone::america_los_angeles);
    assert(!parse_timezone("PST",zone));
    auto local = to_local(utc("2026-03-08T09:59:00Z"),TimeZone::america_los_angeles);
    assert(local.value.hour == 1 && local.value.minute == 59 && !local.daylight);
    local = to_local(utc("2026-03-08T10:00:00Z"),TimeZone::america_los_angeles);
    assert(local.value.hour == 3 && local.value.minute == 0 && local.daylight);
    local = to_local(utc("2026-11-01T08:30:00Z"),TimeZone::america_los_angeles);
    assert(local.value.hour == 1 && local.value.minute == 30 && local.daylight);
    local = to_local(utc("2026-11-01T09:30:00Z"),TimeZone::america_los_angeles);
    assert(local.value.hour == 1 && local.value.minute == 30 && !local.daylight);
    assert(!to_local(utc("2006-03-12T10:00:00Z"),TimeZone::america_los_angeles).daylight);
    assert(to_local(utc("2006-04-02T10:00:00Z"),TimeZone::america_los_angeles).daylight);
    assert(!to_local(utc("2006-10-29T09:00:00Z"),TimeZone::america_los_angeles).daylight);

    const TagId enrolled = tag("01020304"), unknown = tag("aabbccdd");

    // Durable settings are installed atomically before journal recovery and can
    // only be replaced while initialized, healthy and idle.
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        const AlarmDefinition alarm{7,7,0,0x7f,true};
        assert(core.restore_configuration(TimeZone::utc,&alarm,1,&enrolled,1));
        assert(core.initialize().event == AlarmEvent::initialized);
        assert(!core.restore_configuration(TimeZone::utc,nullptr,0,nullptr,0));
        core.evaluate(true,utc("2026-09-28T06:59:00Z"));
        assert(core.evaluate(true,utc("2026-09-28T07:00:00Z")).event == AlarmEvent::triggered);
        assert(!core.replace_configuration(TimeZone::utc,nullptr,0,nullptr,0));
        assert(core.handle_tag(enrolled).event == AlarmEvent::dismissed);
        assert(core.replace_configuration(TimeZone::america_los_angeles,nullptr,0,nullptr,0));
    }

    // Initial time acquisition never rings retroactively.
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        prepare(core,enrolled,7,0);
        assert(core.evaluate(true,utc("2026-09-28T07:01:00Z")).event == AlarmEvent::initialized);
        assert(!core.ringing());
    }

    // Cursor-only journal writes are checkpointed, not performed every tick/minute.
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        assert(core.initialize().event == AlarmEvent::initialized);
        core.evaluate(true,utc("2026-09-28T06:00:00Z"));
        const int initial_saves = storage.saves;
        for (int minute = 1; minute < 5; ++minute)
            core.evaluate(true,utc(minute == 1 ? "2026-09-28T06:01:00Z" :
                                   minute == 2 ? "2026-09-28T06:02:00Z" :
                                   minute == 3 ? "2026-09-28T06:03:00Z" : "2026-09-28T06:04:00Z"));
        assert(storage.saves == initial_saves);
        core.evaluate(true,utc("2026-09-28T06:05:00Z"));
        assert(storage.saves == initial_saves + 1);
    }

    // Weekday schedule, overlaps, unknown tags and durable-before-silence dismissal.
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        prepare(core,enrolled,7,0,1U << 1,1); // Monday.
        assert(core.upsert_alarm({2,7,0,1U << 1,true}));
        core.evaluate(true,utc("2026-09-28T06:59:00Z"));
        const auto fired = core.evaluate(true,utc("2026-09-28T07:00:00Z"));
        assert(fired.event == AlarmEvent::triggered && fired.start_audio);
        assert(core.view().active_count == 2);
        assert(core.handle_tag(unknown).event == AlarmEvent::unknown_tag && core.ringing());
        assert(!core.upsert_alarm({1,8,0,0x7f,true}));
        assert(!core.set_timezone(TimeZone::america_los_angeles));
        assert(!core.enroll(unknown));
        storage.fail_save = true;
        assert(core.handle_tag(enrolled).event == AlarmEvent::storage_fault && core.ringing());
        storage.fail_save = false;
        const auto dismissed = core.handle_tag(enrolled);
        assert(dismissed.event == AlarmEvent::dismissed && dismissed.stop_audio && !core.ringing());
        assert(core.evaluate(true,utc("2026-09-28T06:58:00Z")).event == AlarmEvent::none);
        assert(core.evaluate(true,utc("2026-09-28T07:00:00Z")).event == AlarmEvent::none);
    }

    // A four-minute outage catches up; six minutes records a miss without ringing.
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        prepare(core,enrolled,7,0);
        core.evaluate(true,utc("2026-09-28T06:59:00Z"));
        assert(core.evaluate(true,utc("2026-09-28T07:04:00Z")).event == AlarmEvent::triggered);
    }
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        prepare(core,enrolled,7,0);
        core.evaluate(true,utc("2026-09-28T06:59:00Z"));
        assert(core.evaluate(true,utc("2026-09-28T07:06:00Z")).event == AlarmEvent::missed);
        assert(!core.ringing() && core.view().missed_count == 1);
    }

    // Spring-gap time is skipped. Fall-fold time fires only at the first occurrence.
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        prepare(core,enrolled,2,30,1U << 0);
        assert(core.set_timezone(TimeZone::america_los_angeles));
        core.evaluate(true,utc("2026-03-08T09:59:00Z"));
        assert(core.evaluate(true,utc("2026-03-08T10:31:00Z")).event == AlarmEvent::skipped);
        assert(!core.ringing() && core.view().skipped_count == 1 && core.view().missed_count == 0);
    }
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        prepare(core,enrolled,1,30,1U << 0);
        assert(core.set_timezone(TimeZone::america_los_angeles));
        core.evaluate(true,utc("2026-11-01T07:59:00Z"));
        assert(core.evaluate(true,utc("2026-11-01T08:30:00Z")).event == AlarmEvent::triggered);
        assert(core.handle_tag(enrolled).event == AlarmEvent::dismissed);
        assert(core.evaluate(true,utc("2026-11-01T09:30:00Z")).event == AlarmEvent::none);
        assert(!core.ringing());
    }

    // Failed activation persistence prevents sound and retries the occurrence.
    {
        MemoryStorage storage;
        AlarmCore core(storage);
        prepare(core,enrolled,7,0);
        core.evaluate(true,utc("2026-09-28T06:59:00Z"));
        storage.fail_save = true;
        const auto failed = core.evaluate(true,utc("2026-09-28T07:00:00Z"));
        assert(failed.event == AlarmEvent::storage_fault && !failed.start_audio && !core.ringing());
        storage.fail_save = false;
        assert(core.evaluate(true,utc("2026-09-28T07:00:00Z")).start_audio);
    }

    // A journaled active occurrence resumes after reboot with its frozen tag snapshot.
    {
        MemoryStorage storage;
        AlarmCore first(storage);
        assert(first.initialize().event == AlarmEvent::initialized);
        assert(first.enroll(enrolled));
        assert(first.development_trigger(utc("2026-09-28T07:00:00Z")).start_audio);
        AlarmCore recovered(storage);
        const auto restored = recovered.initialize();
        assert(restored.start_audio && recovered.ringing());
        assert(recovered.handle_tag(enrolled).event == AlarmEvent::dismissed);
    }
    {
        MemoryStorage storage;
        storage.result = LoadResult::loaded;
        storage.state.active_count = static_cast<uint8_t>(max_active + 1);
        AlarmCore corrupt(storage);
        assert(corrupt.initialize().event == AlarmEvent::storage_fault);
        const int saves = storage.saves;
        assert(!corrupt.enroll(enrolled));
        assert(corrupt.evaluate(true,utc("2026-09-28T07:00:00Z")).event == AlarmEvent::storage_fault);
        assert(storage.saves == saves);
    }

    std::cout << "PASS: scheduling, weekdays, DST gap/fold, missed/overlap, recovery, tags, storage faults\n";
}
