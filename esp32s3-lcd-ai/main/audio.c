#include "audio.h"
#include "bsp_pins.h"

#include <math.h>

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;

/* ElevenLabs is asked for pcm_24000, so the I2S output clock runs at 24 kHz.
 * This matches the Waveshare XiaoZhi config for the 1.46/1.46B board. */
#define AUDIO_SAMPLE_RATE 24000

/* Digital volume, as a Q16 gain. 65536 == unity (16-bit sample left-shifted
 * into the 32-bit slot). The amp/speaker on this board is quiet at unity, so
 * boost and saturate. Raise for louder (more clipping), lower for cleaner. */
#define AUDIO_GAIN_Q16    (65536 * 4)

esp_err_t audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }

    /* The PCM5101 on this board is wired for a 32-bit, mono, left-slot frame
     * (verified from the XiaoZhi NoAudioCodecSimplex config). 16-bit PCM is
     * placed in the upper bits of each 32-bit word in audio_play_mono16(). */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
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
    ESP_LOGI(TAG, "I2S out BCK=%d WS=%d DOUT=%d @%dHz (32-bit mono/left): %s",
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

esp_err_t mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mic i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }

    /* Same 32-bit frame as the speaker, but RX on the right slot. The mic
     * delivers 32-bit words; mic_record() shifts them down to 16-bit. */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = MIC_SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_RIGHT,
            .ws_width = I2S_DATA_BIT_WIDTH_32BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BSP_MIC_SCK,
            .ws   = BSP_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = BSP_MIC_SD,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    err = i2s_channel_init_std_mode(s_rx, &std_cfg);
    ESP_LOGI(TAG, "I2S mic SCK=%d WS=%d DIN=%d @%dHz: %s",
             BSP_MIC_SCK, BSP_MIC_WS, BSP_MIC_SD, MIC_SAMPLE_RATE,
             esp_err_to_name(err));
    return err;
}

size_t mic_record(int16_t *dest, int seconds)
{
    if (s_rx == NULL || dest == NULL || seconds <= 0) {
        return 0;
    }
    const size_t target = (size_t)seconds * MIC_SAMPLE_RATE;
    if (i2s_channel_enable(s_rx) != ESP_OK) {
        return 0;
    }

    enum { CHUNK = 512 };
    int32_t raw[CHUNK];
    size_t got = 0;
    while (got < target) {
        size_t want = (target - got < CHUNK) ? (target - got) : CHUNK;
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(s_rx, raw, want * sizeof(int32_t),
                                         &bytes_read, pdMS_TO_TICKS(1000));
        if (err != ESP_OK) {
            break;
        }
        size_t n = bytes_read / sizeof(int32_t);
        for (size_t i = 0; i < n; i++) {
            int32_t v = raw[i] >> 12;     /* 32-bit mic word -> ~16-bit sample */
            if (v > INT16_MAX)       v = INT16_MAX;
            else if (v < -INT16_MAX) v = -INT16_MAX;
            dest[got + i] = (int16_t)v;
        }
        got += n;
    }
    i2s_channel_disable(s_rx);
    ESP_LOGI(TAG, "mic recorded %u samples (%.1fs)", (unsigned)got,
             (float)got / MIC_SAMPLE_RATE);
    return got;
}

void audio_play_mono16(const uint8_t *data, size_t len)
{
    if (s_tx == NULL || data == NULL || len < 2) {
        return;
    }
    const int16_t *mono = (const int16_t *)data;
    size_t nsamp = len / 2;

    /* Each 16-bit sample is scaled by the Q16 volume gain into a 32-bit word
     * (unity == sample << 16), saturating to avoid overflow, which is what the
     * 32-bit-slot I2S frame on this board expects. */
    enum { BLK = 256 };
    int32_t out[BLK];
    size_t i = 0;
    while (i < nsamp) {
        size_t n = (nsamp - i < BLK) ? (nsamp - i) : BLK;
        for (size_t j = 0; j < n; j++) {
            int64_t v = (int64_t)mono[i + j] * AUDIO_GAIN_Q16;
            if (v > INT32_MAX)      v = INT32_MAX;
            else if (v < INT32_MIN) v = INT32_MIN;
            out[j] = (int32_t)v;
        }
        size_t written = 0;
        i2s_channel_write(s_tx, out, n * sizeof(int32_t), &written, portMAX_DELAY);
        i += n;
    }
}
