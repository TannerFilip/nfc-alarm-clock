#pragma once
#include "i2c_bus.hpp"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
namespace clock_hw {
class Sensing {
public:
    explicit Sensing(I2cBus& bus) : bus_(bus) {}
    esp_err_t init_adc();
    void poll(int64_t now_ms);
    float lux = -1;
    int battery_mv = -1, raw_adc = -1;
    bool calibrated = false;
private:
    I2cBus& bus_;
    adc_oneshot_unit_handle_t adc_ = nullptr;
    adc_cali_handle_t calibration_ = nullptr;
    bool adc_ready_ = false, light_started_ = false;
    int64_t light_ready_at_ = 0, retry_at_ = 0;
};
}
