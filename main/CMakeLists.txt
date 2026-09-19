idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_psram spi_flash esp_timer)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
