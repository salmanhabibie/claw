#pragma once

#include "esp_err.h"
#include "esp_io_expander.h"
#include "driver/i2c.h"

/* The shared I2C bus uses the legacy driver, because the pulled
 * esp_io_expander_tca9554 (1.0.x) takes an i2c_port_t, not a new bus handle. */
#define BSP_I2C_PORT  I2C_NUM_0

/* Bring up the shared I2C master bus (GPIO10/11, legacy driver). */
esp_err_t bsp_i2c_init(void);

/* Create the TCA9554 expander and set the reset / CS lines as outputs (high). */
esp_err_t bsp_expander_init(esp_io_expander_handle_t *out_expander);

/* Pulse the LCD / touch hardware reset lines (which live on the expander). */
void bsp_reset_lcd(esp_io_expander_handle_t expander);
void bsp_reset_touch(esp_io_expander_handle_t expander);

/* Turn the LCD backlight on (GPIO5, direct). */
void bsp_backlight_on(void);
