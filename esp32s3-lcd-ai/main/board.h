#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"

/* Bring up the shared I2C master bus (GPIO10/11). */
esp_err_t bsp_i2c_init(i2c_master_bus_handle_t *out_bus);

/* Create the TCA9554 expander and set the reset / CS lines as outputs (high). */
esp_err_t bsp_expander_init(i2c_master_bus_handle_t bus,
                            esp_io_expander_handle_t *out_expander);

/* Pulse the LCD / touch hardware reset lines (which live on the expander). */
void bsp_reset_lcd(esp_io_expander_handle_t expander);
void bsp_reset_touch(esp_io_expander_handle_t expander);

/* Turn the LCD backlight on (GPIO5, direct). */
void bsp_backlight_on(void);
