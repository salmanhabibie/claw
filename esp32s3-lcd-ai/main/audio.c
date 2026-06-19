#include "audio.h"
#include "bsp_pins.h"

#include <math.h>
#include <stdbool.h>

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
 * 100% maps to a 4x boost (loud, slightly clipping); lower is cleaner/quieter.
 * s_gain_q16 is the live gain, scaled from the 0..100 percent volume. */
#define AUDIO_GAIN_MAX_Q16  (65536 * 4)

static int     s_volume_pct = 80;
static int64_t s_gain_q16   = (int64_t)AUDIO_GAIN_MAX_Q16 * 80 / 100;

void audio_set_volume(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    s_volume_pct = percent;
    s_gain_q16 = (int64_t)AUDIO_GAIN_MAX_Q16 * percent / 100;
}

int audio_get_volume(void)
{
    return s_volume_pct;
}

esp_err_t audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    /* Deeper DMA queue (~160 ms) so playback rides out scheduling hiccups. */
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 480;
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

void audio_play_chime(void)
{
    const int notes[2] = { 880, 1245 };   /* a rising two-note ding */
    const int dur_ms = 150;
    const int amp = 12000;
    enum { CH = 256 };
    int16_t buf[CH];
    for (int k = 0; k < 2; k++) {
        const int total = AUDIO_SAMPLE_RATE * dur_ms / 1000;
        int i = 0;
        while (i < total) {
            int n = (total - i < CH) ? (total - i) : CH;
            for (int j = 0; j < n; j++) {
                float t = (float)(i + j) / AUDIO_SAMPLE_RATE;
                buf[j] = (int16_t)(amp * sinf(2.0f * (float)M_PI * notes[k] * t));
            }
            audio_play_mono16((const uint8_t *)buf, (size_t)n * 2);
            i += n;
        }
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

size_t mic_record(int16_t *dest, int max_seconds)
{
    return mic_record_vad(dest, max_seconds, NULL);
}

size_t mic_record_vad(int16_t *dest, int max_seconds, bool *speech_out)
{
    if (speech_out) *speech_out = false;
    if (s_rx == NULL || dest == NULL || max_seconds <= 0) {
        return 0;
    }
    const size_t cap = (size_t)max_seconds * MIC_SAMPLE_RATE;
    if (i2s_channel_enable(s_rx) != ESP_OK) {
        return 0;
    }

    enum { CHUNK = 512 };                          /* ~32 ms @ 16 kHz */
    const int CHUNK_MS = (CHUNK * 1000) / MIC_SAMPLE_RATE;
    const int BASELINE_CHUNKS = 8;                 /* ~256 ms to gauge noise floor */
    const int TRAIL_MS = 1200;                     /* stop this long after speech ends */
    const int MIN_MS = 700;                        /* never stop before this */

    int32_t raw[CHUNK];
    size_t got = 0;
    long baseline_sum = 0;
    int  baseline_n = 0;
    long threshold = 0;
    bool speech = false;
    int  silence_ms = 0, elapsed_ms = 0;

    while (got < cap) {
        size_t want = (cap - got < CHUNK) ? (cap - got) : CHUNK;
        size_t bytes_read = 0;
        if (i2s_channel_read(s_rx, raw, want * sizeof(int32_t),
                             &bytes_read, pdMS_TO_TICKS(1000)) != ESP_OK) {
            break;
        }
        size_t n = bytes_read / sizeof(int32_t);
        long absum = 0;
        for (size_t i = 0; i < n; i++) {
            int32_t v = raw[i] >> 12;     /* 32-bit mic word -> ~16-bit sample */
            if (v > INT16_MAX)       v = INT16_MAX;
            else if (v < -INT16_MAX) v = -INT16_MAX;
            dest[got + i] = (int16_t)v;
            absum += (v < 0) ? -v : v;
        }
        long energy = n ? absum / (long)n : 0;
        got += n;
        elapsed_ms += CHUNK_MS;

        /* Calibrate the noise floor from the first few chunks (assumed quiet). */
        if (baseline_n < BASELINE_CHUNKS) {
            baseline_sum += energy;
            if (++baseline_n == BASELINE_CHUNKS) {
                threshold = (baseline_sum / baseline_n) * 3 + 350;
            }
            continue;
        }

        /* Voice-activity: once speech starts, stop after a trailing silence.
         * If speech is never detected, we fall through and record the full cap. */
        if (energy > threshold) { speech = true; silence_ms = 0; }
        else if (speech)        { silence_ms += CHUNK_MS; }

        if (speech && silence_ms >= TRAIL_MS && elapsed_ms >= MIN_MS) {
            break;
        }
    }

    i2s_channel_disable(s_rx);
    ESP_LOGI(TAG, "mic recorded %u samples (%.1fs)%s", (unsigned)got,
             (float)got / MIC_SAMPLE_RATE, speech ? "" : " [no speech detected]");
    if (speech_out) *speech_out = speech;
    return got;
}

esp_err_t mic_stream_start(void)
{
    return (s_rx != NULL) ? i2s_channel_enable(s_rx) : ESP_FAIL;
}

void mic_stream_stop(void)
{
    if (s_rx != NULL) {
        i2s_channel_disable(s_rx);
    }
}

size_t mic_read(int16_t *dest, size_t nsamp)
{
    if (s_rx == NULL || dest == NULL) {
        return 0;
    }
    size_t got = 0;
    int32_t raw[256];
    while (got < nsamp) {
        size_t want = nsamp - got;
        if (want > 256) want = 256;
        size_t bytes_read = 0;
        if (i2s_channel_read(s_rx, raw, want * sizeof(int32_t),
                             &bytes_read, pdMS_TO_TICKS(1000)) != ESP_OK) {
            break;
        }
        size_t n = bytes_read / sizeof(int32_t);
        if (n == 0) {
            break;
        }
        for (size_t i = 0; i < n; i++) {
            int32_t v = raw[i] >> 12;     /* 32-bit mic word -> ~16-bit sample */
            if (v > INT16_MAX)       v = INT16_MAX;
            else if (v < -INT16_MAX) v = -INT16_MAX;
            dest[got + i] = (int16_t)v;
        }
        got += n;
    }
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
            int64_t v = (int64_t)mono[i + j] * s_gain_q16;
            if (v > INT32_MAX)      v = INT32_MAX;
            else if (v < INT32_MIN) v = INT32_MIN;
            out[j] = (int32_t)v;
        }
        size_t written = 0;
        i2s_channel_write(s_tx, out, n * sizeof(int32_t), &written, portMAX_DELAY);
        i += n;
    }
}
