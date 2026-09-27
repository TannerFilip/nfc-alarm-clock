# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

set(IDF_TARGET "esp32s3" CACHE STRING "Clock MCU")
set(PROJECT_VER "0.3.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nfc-alarm-clock)
if(NOT "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}" STREQUAL "6.1.0")
    message(FATAL_ERROR "This project requires ESP-IDF v6.1 (6.1.0)")
endif()
