#pragma once
#include "i2c_bus.hpp"
#include "core/calendar.hpp"
namespace clock_hw {
struct ControlEvents { bool button1 = false, button2 = false, encoder_button = false; int64_t turn = 0; };
class Controls {
public:
    explicit Controls(I2cBus& bus) : bus_(bus) {}
    esp_err_t init_buttons();
    ControlEvents poll(int64_t now_ms);
    const char* status() const { return online_ ? "OK" : "MISSING/IO ERROR"; }
    bool button2_down() const { return buttons_[1].pressed(); }
private:
    bool init_encoder();
    bool read_seesaw(uint8_t base, uint8_t reg, uint8_t* data, size_t size);
    I2cBus& bus_;
    clock_core::Debouncer buttons_[3];
    bool gpio_ready_ = false, online_ = false;
    uint32_t position_ = 0;
    int64_t retry_at_ = 0;
};
}
