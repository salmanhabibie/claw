#include "display.h"
#include "board.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "esp_lcd_spd2010.h"
#include "esp_lcd_touch_spd2010.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display";

#define LCD_HOST           SPI2_HOST
#define LCD_BITS_PER_PIXEL 16

lv_display_t *bsp_display_init(esp_io_expander_handle_t expander)
{
    /* ---- 1. Hardware-reset the panel via the expander ---- */
    bsp_reset_lcd(expander);

    /* ---- 2. QSPI bus ---- */
    ESP_LOGI(TAG, "init QSPI bus");
    const spi_bus_config_t bus_config = SPD2010_PANEL_BUS_QSPI_CONFIG(
        BSP_LCD_SCK, BSP_LCD_D0, BSP_LCD_D1, BSP_LCD_D2, BSP_LCD_D3,
        BSP_LCD_H_RES * 80 * (LCD_BITS_PER_PIXEL / 8));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    /* ---- 3. Panel IO ---- */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config =
        SPD2010_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    /* ---- 4. SPD2010 panel ---- */
    esp_lcd_panel_handle_t panel_handle = NULL;
    spd2010_vendor_config_t vendor_config = {
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,   /* reset handled via the expander above */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_spd2010(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    bsp_backlight_on();

    /* ---- 5. LVGL port ---- */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = BSP_LCD_H_RES * 100,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_spiram = true,
            .swap_bytes = true,   /* RGB565 byte order for SPI panels */
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "lvgl_port_add_disp failed");
        return NULL;
    }

    /* ---- 6. SPD2010 touch ---- */
    bsp_reset_touch(expander);
    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config =
        ESP_LCD_TOUCH_IO_I2C_SPD2010_CONFIG();
    /* Pass the legacy i2c_port_t so the _Generic macro selects the v1 variant. */
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(BSP_I2C_PORT, &tp_io_config, &tp_io));

    const esp_lcd_touch_config_t tp_config = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = -1,   /* reset handled via the expander above */
        .int_gpio_num = BSP_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_spd2010(tp_io, &tp_config, &tp));

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
    };
    lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "display + touch ready (%dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return disp;
}
