#pragma once

#include "esp_err.h"
#include "esp_io_expander.h"
#include "driver/i2c.h"

/* The shared I2C bus uses the legacy driver, because the pulled
 * esp_io_expander_tca9554 (1.0.x) takes an i2c_port_t, not a new bus handle. */
#define BSP_I2C_PORT  I2C_NUM_0

/* Latch board power / enable the speaker amplifier (GPIO7 HIGH). Call this
 * first thing in app_main, before anything else. */
void bsp_power_on(void);

/* Bring up the shared I2C master bus (GPIO10/11, legacy driver). */
esp_err_t bsp_i2c_init(void);

/* Create the TCA9554 expander and set the reset / CS lines as outputs (high). */
esp_err_t bsp_expander_init(esp_io_expander_handle_t *out_expander);

/* Reset the SPD2010 (display + touch are one TDDI chip) via the expander.
 * Pulses P0..P3 together because the exact EXIO->pin mapping is uncertain. */
void bsp_reset_panel(esp_io_expander_handle_t expander);

/* Diagnostic: pulse each expander pin P0..P3 in turn and report which one
 * brings a new I2C device (the touch) alive. */
void bsp_probe_touch_reset(esp_io_expander_handle_t expander);

/* Probe the I2C bus and log every address that ACKs (diagnostic). */
void bsp_i2c_scan(void);

/* Turn the LCD backlight on (GPIO5, direct). */
void bsp_backlight_on(void);
