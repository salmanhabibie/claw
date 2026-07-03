#pragma once

#include "esp_err.h"

/* QMI8658 6-axis IMU (accelerometer used only), on the shared I2C bus.
 *
 * A small background task watches the acceleration magnitude; a jolt away
 * from 1 g (shaking / bumping / picking the device up roughly) fires the
 * callback, debounced so it triggers at most once every few seconds.
 * Non-fatal: with no IMU found, imu_init() logs and does nothing. */

typedef void (*imu_shake_cb_t)(void);

/* Probe + configure the IMU and start the watcher task. */
esp_err_t imu_init(imu_shake_cb_t on_shake);
