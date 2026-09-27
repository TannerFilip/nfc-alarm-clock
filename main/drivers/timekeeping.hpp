#pragma once
#include "i2c_bus.hpp"
#include "core/calendar.hpp"
namespace clock_hw {
class Timekeeping {
public:
    explicit Timekeeping(I2cBus& bus) : bus_(bus) {}
    void poll();
    void set(const clock_core::DateTime& value);
    bool valid() const { return valid_; }
    const char* rtc_status() const { return rtc_status_; }
    const char* source() const { return source_; }
private:
    I2cBus& bus_;
    bool valid_ = false;
    int64_t retry_at_ms_ = 0;
    const char* rtc_status_ = "NOT PROBED";
    const char* source_ = "NONE";
};
}
