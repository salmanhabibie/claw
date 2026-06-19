#include "audio.h"
#include "bsp_pins.h"

#include <math.h>

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx;

#define AUDIO_SAMPLE_RATE 16000

esp_err_t audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   /* PCM5101 runs off its internal PLL */
            .bclk = BSP_SPK_BCK,
            .ws   = BSP_SPK_LRCK,
            .dout = BSP_SPK_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_tx);
    ESP_LOGI(TAG, "I2S out BCK=%d WS=%d DOUT=%d @%dHz: %s",
             BSP_SPK_BCK, BSP_SPK_LRCK, BSP_SPK_DIN, AUDIO_SAMPLE_RATE,
             esp_err_to_name(err));
    return err;
}

void audio_play_test_tone(void)
{
    const int freq = 440;       /* Hz */
    const int dur_ms = 1500;
    const int amp = 18000;      /* loud, but below 32767 clipping */
    const int total = AUDIO_SAMPLE_RATE * dur_ms / 1000;

    ESP_LOGI(TAG, "playing %dHz test tone for %dms", freq, dur_ms);
    enum { CH = 256 };
    int16_t buf[CH];
    int i = 0;
    while (i < total) {
        int n = (total - i < CH) ? (total - i) : CH;
        for (int j = 0; j < n; j++) {
            float t = (float)(i + j) / AUDIO_SAMPLE_RATE;
            buf[j] = (int16_t)(amp * sinf(2.0f * (float)M_PI * freq * t));
        }
        audio_play_mono16((const uint8_t *)buf, (size_t)n * 2);
        i += n;
    }
}

void audio_play_mono16(const uint8_t *data, size_t len)
{
    if (s_tx == NULL || data == NULL || len < 2) {
        return;
    }
    const int16_t *mono = (const int16_t *)data;
    size_t nsamp = len / 2;

    enum { BLK = 256 };
    int16_t stereo[BLK * 2];
    size_t i = 0;
    while (i < nsamp) {
        size_t n = (nsamp - i < BLK) ? (nsamp - i) : BLK;
        for (size_t j = 0; j < n; j++) {
            stereo[j * 2]     = mono[i + j];
            stereo[j * 2 + 1] = mono[i + j];
        }
        size_t written = 0;
        i2s_channel_write(s_tx, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        i += n;
    }
}
