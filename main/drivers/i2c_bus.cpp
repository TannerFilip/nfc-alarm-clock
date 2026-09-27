#include "i2c_bus.hpp"
#include "board.hpp"
#include <initializer_list>

namespace clock_hw {
esp_err_t I2cBus::init() {
    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    i2c_master_bus_config_t cfg{};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = sda;
    cfg.scl_io_num = scl;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    // External pull-ups are supplied by the modules.
    auto err = i2c_new_master_bus(&cfg, &bus_);
    if (err != ESP_OK) return err;
    for (uint8_t address : {oled, rtc, light, encoder,
                            static_cast<uint8_t>(0x28), static_cast<uint8_t>(0x29),
                            static_cast<uint8_t>(0x2a), static_cast<uint8_t>(0x2b)}) {
        i2c_device_config_t dev{};
        dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev.device_address = address;
        dev.scl_speed_hz = 100000;
        err = i2c_master_bus_add_device(bus_, &dev, &devices_[address]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
esp_err_t I2cBus::probe(uint8_t address) {
    if (!bus_ || !mutex_) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(30)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const auto err = i2c_master_probe(bus_, address, 20);
    xSemaphoreGive(mutex_);
    return err;
}
esp_err_t I2cBus::transfer(uint8_t address, const uint8_t* tx, size_t ntx, uint8_t* rx, size_t nrx) {
    if (address >= 128 || !devices_[address]) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(30)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err;
    if (ntx && nrx) err = i2c_master_transmit_receive(devices_[address], tx, ntx, rx, nrx, 20);
    else if (ntx) err = i2c_master_transmit(devices_[address], tx, ntx, 20);
    else err = i2c_master_receive(devices_[address], rx, nrx, 20);
    xSemaphoreGive(mutex_);
    return err;
}
esp_err_t I2cBus::write(uint8_t a, const uint8_t* p, size_t n) { return transfer(a, p, n, nullptr, 0); }
esp_err_t I2cBus::read(uint8_t a, uint8_t* p, size_t n) { return transfer(a, nullptr, 0, p, n); }
esp_err_t I2cBus::read_nci_frame(uint8_t address, uint8_t* data, size_t capacity,
                                 size_t& frame_size) {
    constexpr size_t header_size = 3;
    constexpr size_t maximum_frame_size = header_size + 255;
    frame_size = 0;
    if (!data || capacity < maximum_frame_size) return ESP_ERR_INVALID_ARG;
    if (address >= 128 || !devices_[address] || !mutex_) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(30)) != pdTRUE) return ESP_ERR_TIMEOUT;

    auto err = i2c_master_receive(devices_[address], data, header_size, 20);
    if (err == ESP_OK) {
        const size_t payload_size = data[2];
        if (payload_size != 0) {
            err = i2c_master_receive(devices_[address], data + header_size, payload_size, 20);
        }
        if (err == ESP_OK) frame_size = header_size + payload_size;
    }
    xSemaphoreGive(mutex_);
    return err;
}
esp_err_t I2cBus::registers(uint8_t a, uint8_t r, uint8_t* p, size_t n) { return transfer(a, &r, 1, p, n); }
}
