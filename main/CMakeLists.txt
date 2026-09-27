idf_component_register(SRCS "main.cpp" "core/calendar.cpp" "core/test_tone.cpp" "core/alarm_core.cpp" "core/configuration.cpp"
                    "drivers/i2c_bus.cpp" "drivers/timekeeping.cpp"
                    "drivers/controls.cpp" "drivers/sensing.cpp"
                    "drivers/display.cpp" "drivers/audio.cpp" "nvs_storage.cpp" "web_wifi.cpp" "alarm_service.cpp" "bringup.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_psram spi_flash esp_timer esp_driver_i2c esp_driver_gpio esp_adc esp_driver_i2s console nvs_flash esp_wifi esp_http_server esp_netif esp_event)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
