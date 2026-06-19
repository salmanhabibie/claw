#include "board.h"
#include "bsp_pins.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "board";

void bsp_power_on(void)
{
    gpio_reset_pin(BSP_PWR_CONTROL);
    gpio_set_direction(BSP_PWR_CONTROL, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_PWR_CONTROL, 1);   /* latch power / enable speaker amp */
    ESP_LOGI(TAG, "power control GPIO%d set HIGH", BSP_PWR_CONTROL);
}

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

    /* Drive only P0..P3 as outputs (high): touch/LCD resets + SD CS. Do NOT
     * touch P4..P7 — on this board a spare expander pin is tied to power/reset
     * control, and driving it causes a boot loop. */
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

/* ---- Backlight dimming via LEDC PWM (GPIO5) ---- */

#define BL_LEDC_TIMER     LEDC_TIMER_0
#define BL_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define BL_LEDC_CHANNEL   LEDC_CHANNEL_0
#define BL_LEDC_RES       LEDC_TIMER_10_BIT   /* duty 0..1023 */
#define BL_LEDC_FREQ_HZ   5000
#define BL_DUTY_MAX       1023

static bool s_bl_ready = false;

static void backlight_init(void)
{
    if (s_bl_ready) {
        return;
    }
    ledc_timer_config_t tcfg = {
        .speed_mode      = BL_LEDC_MODE,
        .timer_num       = BL_LEDC_TIMER,
        .duty_resolution = BL_LEDC_RES,
        .freq_hz         = BL_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);
    ledc_channel_config_t ccfg = {
        .gpio_num   = BSP_LCD_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel    = BL_LEDC_CHANNEL,
        .timer_sel  = BL_LEDC_TIMER,
        .duty       = BL_DUTY_MAX,
        .hpoint     = 0,
    };
    ledc_channel_config(&ccfg);
    s_bl_ready = true;
}

void bsp_backlight_set(int percent)
{
    backlight_init();
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * BL_DUTY_MAX / 100;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

void bsp_backlight_on(void)
{
    bsp_backlight_set(100);
}

/* ---- NVS helpers (namespace "settings") ---- */

uint8_t bsp_nvs_get_u8(const char *key, uint8_t def_val)
{
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) != ESP_OK) {
        return def_val;
    }
    uint8_t v = def_val;
    if (nvs_get_u8(h, key, &v) != ESP_OK) {
        v = def_val;
    }
    nvs_close(h);
    return v;
}

void bsp_nvs_set_u8(const char *key, uint8_t val)
{
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, key, val);
    nvs_commit(h);
    nvs_close(h);
}
