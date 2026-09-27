#include "sensing.hpp"
#include "board.hpp"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
namespace clock_hw {
esp_err_t Sensing::init_adc() {
    adc_oneshot_unit_init_cfg_t unit{}; unit.unit_id = ADC_UNIT_1;
    auto err = adc_oneshot_new_unit(&unit,&adc_);
    if (err != ESP_OK) return err;
    adc_oneshot_chan_cfg_t channel{};
    channel.atten = ADC_ATTEN_DB_12; channel.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_oneshot_config_channel(adc_,ADC_CHANNEL_0,&channel);
    if (err != ESP_OK) return err;
    adc_ready_ = true;
    adc_cali_curve_fitting_config_t cal{};
    cal.unit_id = ADC_UNIT_1; cal.chan = ADC_CHANNEL_0;
    cal.atten = ADC_ATTEN_DB_12; cal.bitwidth = ADC_BITWIDTH_DEFAULT;
    calibrated = adc_cali_create_scheme_curve_fitting(&cal,&calibration_) == ESP_OK;
    if (!calibrated) ESP_LOGW("battery", "ADC calibration unavailable: raw counts only");
    return ESP_OK;
}
void Sensing::poll(int64_t now_ms) {
    battery_mv = -1; raw_adc = -1;
    if (adc_ready_) {
        int sum = 0; bool ok = true;
        for (int i = 0; i < 16; ++i) {
            int raw = 0;
            if (adc_oneshot_read(adc_,ADC_CHANNEL_0,&raw) != ESP_OK) { ok = false; break; }
            sum += raw;
        }
        if (ok) {
            raw_adc = sum / 16;
            int mv = 0;
            if (calibrated && adc_cali_raw_to_voltage(calibration_,raw_adc,&mv) == ESP_OK) battery_mv = mv * 2;
        }
    }
    if (!light_started_) {
        if (now_ms < retry_at_) return;
        const uint8_t power = 0x01, continuous_high = 0x10;
        if (bus_.probe(light) != ESP_OK || bus_.write(light,&power,1) != ESP_OK ||
            bus_.write(light,&continuous_high,1) != ESP_OK) {
            lux = -1; retry_at_ = now_ms + 5000; return;
        }
        light_started_ = true; light_ready_at_ = now_ms + 180;
        return;
    }
    if (now_ms < light_ready_at_) return;
    uint8_t data[2]{};
    if (bus_.read(light,data,2) != ESP_OK) {
        light_started_ = false; lux = -1; retry_at_ = now_ms + 5000;
    } else lux = ((unsigned(data[0]) << 8) | data[1]) / 1.2f;
}
}
