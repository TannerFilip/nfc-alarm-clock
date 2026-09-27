#pragma once
#include <cstddef>
#include <cstdint>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace clock_hw {
// Device drivers run on one peripheral task. The mutex also protects future
// independent clients. Device registration is completed before tasks start.
class I2cBus {
public:
    esp_err_t init();
    esp_err_t probe(uint8_t address);
    esp_err_t write(uint8_t address, const uint8_t* data, size_t size);
    esp_err_t read(uint8_t address, uint8_t* data, size_t size);
    // Read one complete NCI packet while retaining the shared-bus lock across
    // the three-byte header and its declared payload.
    esp_err_t read_nci_frame(uint8_t address, uint8_t* data, size_t capacity,
                             size_t& frame_size);
    esp_err_t registers(uint8_t address, uint8_t reg, uint8_t* data, size_t size);
private:
    esp_err_t transfer(uint8_t address, const uint8_t* tx, size_t ntx, uint8_t* rx, size_t nrx);
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t devices_[128]{};
    StaticSemaphore_t mutex_storage_{};
    SemaphoreHandle_t mutex_ = nullptr;
};
}
