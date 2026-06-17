# Top-level project CMake file for the ESP32-S3-Touch-LCD-1.46B AI Assistant.
# Requires ESP-IDF v5.3 or newer (new I2C master driver + esp_lvgl_port v2).
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3-lcd-ai)
