#include "board.h"
#include "bsp_pins.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"

static const char *TAG = "board";

esp_err_t bsp_i2c_init(void)
{
    const i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t err = i2c_param_config(BSP_I2C_PORT, &conf);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_driver_install(BSP_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    ESP_LOGI(TAG, "I2C (legacy) on SDA=%d SCL=%d: %s",
             BSP_I2C_SDA, BSP_I2C_SCL, esp_err_to_name(err));
    return err;
}

esp_err_t bsp_expander_init(esp_io_expander_handle_t *out_expander)
{
    esp_err_t err = esp_io_expander_new_i2c_tca9554(
        BSP_I2C_PORT, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, out_expander);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9554 init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Drive P0..P3 as outputs (high). P0 is included because the EXIOn->Pn
     * mapping is uncertain: if the labels are off by one, the touch reset is
     * actually on P0. Driving it too is harmless. */
    uint32_t mask = IO_EXPANDER_PIN_NUM_0 | BSP_EXIO_LCD_RST |
                    BSP_EXIO_TP_RST | BSP_EXIO_SD_CS;
    esp_io_expander_set_dir(*out_expander, mask, IO_EXPANDER_OUTPUT);
    /* Idle high: resets de-asserted, SD card deselected. */
    esp_io_expander_set_level(*out_expander, mask, 1);
    ESP_LOGI(TAG, "TCA9554 expander ready");
    return ESP_OK;
}

#define BSP_PANEL_RST_PINS (IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1 | \
                            IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3)

void bsp_reset_panel(esp_io_expander_handle_t expander)
{
    /* Reset the whole SPD2010 TDDI chip: assert P0..P3 low together, then
     * release. Whichever pin is the real reset gets toggled, and the touch
     * firmware then has time to boot. */
    esp_io_expander_set_level(expander, BSP_PANEL_RST_PINS, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_io_expander_set_level(expander, BSP_PANEL_RST_PINS, 1);
    vTaskDelay(pdMS_TO_TICKS(250));
}

/* Probe a single 7-bit I2C address: returns true if it ACKs. */
static bool i2c_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t e = i2c_master_cmd_begin(BSP_I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return e == ESP_OK;
}

void bsp_probe_touch_reset(esp_io_expander_handle_t expander)
{
    ESP_LOGW(TAG, "probing which expander pin wakes the touch...");
    for (int p = 0; p <= 3; p++) {
        uint32_t pin = (uint32_t)1 << p;   /* IO_EXPANDER_PIN_NUM_p */
        esp_io_expander_set_dir(expander, pin, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(expander, pin, 0);
        vTaskDelay(pdMS_TO_TICKS(60));
        esp_io_expander_set_level(expander, pin, 1);
        vTaskDelay(pdMS_TO_TICKS(250));
        /* Report any device that is NOT one of the known fixed ones. */
        for (uint8_t addr = 0x03; addr < 0x78; addr++) {
            if (addr == 0x20 || addr == 0x51 || addr == 0x6B) {
                continue;
            }
            if (i2c_probe(addr)) {
                ESP_LOGW(TAG, "  pulsing P%d -> NEW device at 0x%02X (likely touch!)",
                         p, addr);
            }
        }
        ESP_LOGW(TAG, "  pulsing P%d done", p);
    }
}

void bsp_i2c_scan(void)
{
    ESP_LOGI(TAG, "I2C scan (looking for expander 0x20, touch ~0x53):");
    int found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t e = i2c_master_cmd_begin(BSP_I2C_PORT, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (e == ESP_OK) {
            ESP_LOGI(TAG, "  device ACK at 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "I2C scan done (%d device(s))", found);
}

void bsp_backlight_on(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BSP_LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(BSP_LCD_BL, 1);
}
