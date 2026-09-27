#include "timekeeping.hpp"
#include "board.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <sys/time.h>
#include <cstring>

namespace clock_hw {
void Timekeeping::poll() {
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms < retry_at_ms_) return;
    uint8_t r[10]{};
    auto err = bus_.registers(rtc, 0, r, sizeof(r));
    const char* state;
    clock_core::DateTime value{};
    if (err != ESP_OK) { state = "MISSING/IO ERROR"; retry_at_ms_ = now_ms + 5000; }
    else if (!clock_core::decode_rtc(r,value)) state = "INVALID: SET UTC";
    else if (r[2] & 0x40) state = "BACKUP CONFIG: SET UTC"; // PM=010/011/110/111
    else {
        state = (r[2] & 0x80) ? "BATTERY CHECK DISABLED" : ((r[2] & 0x04) ? "BATTERY LOW" : "OK");
        if (!valid_) {
            timeval tv{static_cast<time_t>(clock_core::epoch_seconds(value)), 0};
            if (settimeofday(&tv, nullptr) == 0) { valid_ = true; source_ = "RTC"; }
        }
    }
    if (std::strcmp(state, rtc_status_)) ESP_LOGW("rtc", "%s", state);
    rtc_status_ = state;
}
void Timekeeping::set(const clock_core::DateTime& value) {
    if (!clock_core::valid_date(value)) return;
    // Manual setting works even without an RTC. No compile-time/date guessing.
    timeval tv{static_cast<time_t>(clock_core::epoch_seconds(value)), 0};
    if (settimeofday(&tv, nullptr) != 0) { ESP_LOGE("rtc", "system clock set failed"); return; }
    valid_ = true; source_ = "MANUAL";
    uint8_t control = 0;
    auto err = bus_.registers(rtc, 0, &control, 1);
    // Stop while writing a consistent date; preserve oscillator capacitance.
    uint8_t stop[]{0, static_cast<uint8_t>((control & 0x80) | 0x20)};
    uint8_t date[8]{3};
    clock_core::encode_rtc(value, date + 1);
    uint8_t backup[]{2,0}; // standard battery switchover + battery-low detection
    uint8_t start[]{0, static_cast<uint8_t>(control & 0x80)}; // 24h, running
    if (err == ESP_OK) err = bus_.write(rtc,stop,sizeof(stop));
    if (err == ESP_OK) err = bus_.write(rtc,date,sizeof(date));
    if (err == ESP_OK) err = bus_.write(rtc,backup,sizeof(backup));
    if (err == ESP_OK) err = bus_.write(rtc,start,sizeof(start));
    if (err != ESP_OK) ESP_LOGE("rtc", "UTC set in RAM only; RTC write failed: %s (retry clock_set)", esp_err_to_name(err));
    else ESP_LOGI("rtc", "UTC set; RTC backup enabled. Next poll verifies oscillator/date validity");
    retry_at_ms_ = 0;
    poll();
}
}
