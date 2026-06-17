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

    uint32_t mask = BSP_EXIO_LCD_RST | BSP_EXIO_TP_RST | BSP_EXIO_SD_CS;
    esp_io_expander_set_dir(*out_expander, mask, IO_EXPANDER_OUTPUT);
    /* Idle high: resets de-asserted, SD card deselected. */
    esp_io_expander_set_level(*out_expander, mask, 1);
    ESP_LOGI(TAG, "TCA9554 expander ready");
    return ESP_OK;
}

void bsp_reset_lcd(esp_io_expander_handle_t expander)
{
    esp_io_expander_set_level(expander, BSP_EXIO_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_io_expander_set_level(expander, BSP_EXIO_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));
}

void bsp_reset_touch(esp_io_expander_handle_t expander)
{
    esp_io_expander_set_level(expander, BSP_EXIO_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_io_expander_set_level(expander, BSP_EXIO_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));
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
