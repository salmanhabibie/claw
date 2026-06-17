#pragma once

#include "esp_io_expander.h"
#include "lvgl.h"

/* Initialize the SPD2010 QSPI panel + SPD2010 touch + LVGL (via esp_lvgl_port).
 * The LCD and touch reset lines are pulsed through the TCA9554 expander, and the
 * touch shares the legacy I2C bus (BSP_I2C_PORT).
 * Returns the LVGL display, or NULL on failure. */
lv_display_t *bsp_display_init(esp_io_expander_handle_t expander);
