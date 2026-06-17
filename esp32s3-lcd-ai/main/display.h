#pragma once

#include "esp_io_expander.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

/* Initialize the SPD2010 QSPI panel + SPD2010 touch + LVGL (via esp_lvgl_port).
 * The LCD and touch reset lines are pulsed through the TCA9554 expander.
 * Returns the LVGL display, or NULL on failure. */
lv_display_t *bsp_display_init(i2c_master_bus_handle_t i2c_bus,
                               esp_io_expander_handle_t expander);
