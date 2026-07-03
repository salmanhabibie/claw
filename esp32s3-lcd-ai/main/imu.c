#include "imu.h"
#include "board.h"

#include <math.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"

static const char *TAG = "imu";

/* QMI8658 register map (the handful we need). */
#define REG_WHO_AM_I   0x00     /* reads 0x05 */
#define REG_CTRL1      0x02     /* 0x40 = auto-increment register address */
#define REG_CTRL2      0x03     /* accel full-scale + output data rate */
#define REG_CTRL7      0x08     /* sensor enables (bit0 = accel) */
#define REG_AX_L       0x35     /* AX_L..AZ_H, 6 bytes, little-endian */
#define REG_RESET      0x60     /* write 0xb0 = soft reset */

#define WHO_AM_I_VAL   0x05
#define ACCEL_LSB_PER_G 8192.0f /* at +/-4 g, 16-bit */

/* A jolt is |magnitude - 1 g| beyond this. 0.5 g ignores normal handling
 * (slow tilting changes direction, not magnitude) but catches real shakes. */
#define SHAKE_THRESHOLD_G  0.5f
#define SHAKE_COOLDOWN_MS  4000
#define POLL_MS            50

/* The detector only arms after this many consecutive plausible readings
 * (a resting accelerometer must show ~1 g of gravity). Bogus data - zeros,
 * byte-swapped values, a wedged bus - never arms it, so a misbehaving sensor
 * can't fire the reaction (and its chime) over and over. */
#define ARM_SANE_SAMPLES   20
#define SANE_MIN_G         0.6f
#define SANE_MAX_G         1.4f
/* A real jolt must persist for 2 consecutive samples; single-sample spikes
 * (bus glitches) are ignored. */
#define TRIGGER_SAMPLES    2

static uint8_t s_addr;          /* 0x6a or 0x6b, discovered at probe time */
static imu_shake_cb_t s_cb;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(BSP_I2C_PORT, s_addr, buf, 2,
                                      pdMS_TO_TICKS(100));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *dst, size_t len)
{
    return i2c_master_write_read_device(BSP_I2C_PORT, s_addr, &reg, 1,
                                        dst, len, pdMS_TO_TICKS(100));
}

/* Acceleration magnitude in g, or -1 on a read error. */
static float accel_magnitude(void)
{
    uint8_t raw[6];
    if (reg_read(REG_AX_L, raw, sizeof(raw)) != ESP_OK) return -1.0f;
    int16_t x = (int16_t)(raw[0] | (raw[1] << 8));
    int16_t y = (int16_t)(raw[2] | (raw[3] << 8));
    int16_t z = (int16_t)(raw[4] | (raw[5] << 8));
    float fx = x / ACCEL_LSB_PER_G;
    float fy = y / ACCEL_LSB_PER_G;
    float fz = z / ACCEL_LSB_PER_G;
    return sqrtf(fx * fx + fy * fy + fz * fz);
}

static void imu_task(void *arg)
{
    (void)arg;
    TickType_t last_shake = 0;
    int sane = 0;               /* consecutive plausible readings */
    int over = 0;               /* consecutive over-threshold readings */
    bool armed = false;
    bool warned = false;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        float mag = accel_magnitude();
        if (mag < 0.0f) continue;                      /* I2C read error */

        if (!armed) {
            /* Prove the sensor is sane (resting reads ~1 g) before trusting
             * it. Junk data keeps the detector disarmed - and silent. */
            sane = (mag >= SANE_MIN_G && mag <= SANE_MAX_G) ? sane + 1 : 0;
            if (sane >= ARM_SANE_SAMPLES) {
                armed = true;
                over = 0;
                if (!warned) ESP_LOGI(TAG, "shake detector armed (|a|=%.2f g)", mag);
            } else if (sane == 0 && !warned) {
                warned = true;
                ESP_LOGW(TAG, "implausible accel data (|a|=%.2f g); "
                              "shake reaction stays off until it settles", mag);
            }
            continue;
        }

        over = (fabsf(mag - 1.0f) > SHAKE_THRESHOLD_G) ? over + 1 : 0;
        if (over >= TRIGGER_SAMPLES) {
            over = 0;
            armed = false;                              /* re-prove sanity */
            sane = 0;
            TickType_t now = xTaskGetTickCount();
            if ((now - last_shake) >= pdMS_TO_TICKS(SHAKE_COOLDOWN_MS)) {
                last_shake = now;
                ESP_LOGI(TAG, "shake! |a|=%.2f g", mag);
                if (s_cb != NULL) s_cb();
            }
        }
    }
}

esp_err_t imu_init(imu_shake_cb_t on_shake)
{
    /* The QMI8658's address pin can strap it to 0x6a or 0x6b; try both. */
    static const uint8_t addrs[] = { 0x6b, 0x6a };
    for (size_t i = 0; i < sizeof(addrs); i++) {
        s_addr = addrs[i];
        uint8_t who = 0;
        if (reg_read(REG_WHO_AM_I, &who, 1) == ESP_OK && who == WHO_AM_I_VAL) {
            goto found;
        }
    }
    ESP_LOGW(TAG, "QMI8658 not found; shake reaction disabled");
    return ESP_ERR_NOT_FOUND;

found:
    reg_write(REG_RESET, 0xb0);          /* known state, whatever ran before */
    vTaskDelay(pdMS_TO_TICKS(20));
    reg_write(REG_CTRL1, 0x40);          /* auto-increment reads */
    reg_write(REG_CTRL2, 0x16);          /* accel +/-4 g, ~125 Hz */
    reg_write(REG_CTRL7, 0x01);          /* enable the accelerometer */
    vTaskDelay(pdMS_TO_TICKS(10));       /* first samples need a moment */

    s_cb = on_shake;
    if (xTaskCreate(imu_task, "imu", 3072, NULL, 3, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "QMI8658 ready at 0x%02x, watching for shakes", s_addr);
    return ESP_OK;
}
