#include <cinttypes>
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bringup.hpp"
#include "nvs_storage.hpp"

#if !CONFIG_IDF_TARGET_ESP32S3 || !CONFIG_ESPTOOLPY_OCT_FLASH || !CONFIG_SPIRAM_MODE_OCT
#error "This firmware requires ESP32-S3 with octal flash and octal PSRAM"
#endif
#if defined(CONFIG_CLOCK_SIMULATED_NFC) && !defined(CONFIG_CLOCK_DEVELOPMENT_BUILD)
#error "Simulated NFC is forbidden in production"
#endif

extern "C" void app_main()
{
    constexpr auto tag = "clock";
    ESP_LOGI(tag, "NFC alarm clock milestone 4 / 0.4.0 / IDF %s", esp_get_idf_version());
#ifdef CONFIG_CLOCK_DEVELOPMENT_BUILD
    ESP_LOGW(tag, "DEVELOPMENT BUILD - not production firmware");
#else
    ESP_LOGI(tag, "PRODUCTION profile: simulated dismissal disabled");
#endif
#ifdef CONFIG_CLOCK_SIMULATED_NFC
    ESP_LOGW(tag, "SIMULATED NFC selected; development tag injection enabled");
#endif
    esp_chip_info_t chip{};
    esp_chip_info(&chip);
    ESP_LOGI(tag, "cores=%u revision=%u reset_reason=%d", chip.cores, chip.revision,
             static_cast<int>(esp_reset_reason()));
    uint32_t flash_bytes = 0;
    const esp_err_t flash_result = esp_flash_get_size(nullptr, &flash_bytes);
    const size_t psram_bytes = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
    const bool memory_ok = flash_result == ESP_OK && flash_bytes == 32U * 1024 * 1024
                          && psram_bytes == 16U * 1024 * 1024;
    ESP_LOGI(tag, "flash=%" PRIu32 " bytes (%s), PSRAM=%zu bytes", flash_bytes,
             esp_err_to_name(flash_result), psram_bytes);
    if (!memory_ok) {
        ESP_LOGE(tag, "MEMORY MISMATCH: expected 33554432 flash / 16777216 PSRAM bytes");
    }
    const auto nvs = clock_storage::initialize_nvs();
    ESP_LOGI(tag, "NVS initialization: %s (%s; never auto-erased)",
             nvs.ready() ? "READY" : "FAULT", esp_err_to_name(nvs.error));
    ESP_LOGW(tag, "Milestone 4 persistence and time-limited local setup; physical NFC remains pending");
    start_bringup();
    while (true) {
        ESP_LOGI(tag, "uptime=%" PRIi64 "s memory=%s internal_free=%zu psram_free=%zu",
                 esp_timer_get_time() / 1000000, memory_ok ? "OK" : "FAULT",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
