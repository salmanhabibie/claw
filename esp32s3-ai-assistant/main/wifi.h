#pragma once

#include "esp_err.h"

/* Connect to the WiFi network configured in menuconfig (station mode).
 * Blocks until connected or until all retries are exhausted.
 * Returns ESP_OK on success, ESP_FAIL otherwise. */
esp_err_t wifi_connect(void);
