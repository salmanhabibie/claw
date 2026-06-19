#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/* Initialize the I2S TX path to the PCM5101 DAC (speaker), 16 kHz. */
esp_err_t audio_init(void);

/* Play 16-bit little-endian MONO PCM at 16 kHz. Each mono sample is duplicated
 * to both stereo channels. Blocks until the data has been queued to I2S. */
void audio_play_mono16(const uint8_t *data, size_t len);

/* Play a loud ~1.5s 440 Hz sine tone locally (no network). Diagnostic to test
 * the I2S -> DAC -> amplifier -> speaker path on its own. */
void audio_play_test_tone(void);

/* Microphone capture rate (16 kHz, matches the STT request). */
#define MIC_SAMPLE_RATE 16000

/* Initialize the I2S RX path from the on-board microphone. */
esp_err_t mic_init(void);

/* Record `seconds` of audio into `dest` as 16-bit mono PCM at 16 kHz. `dest`
 * must hold at least seconds*MIC_SAMPLE_RATE samples. Returns samples captured. */
size_t mic_record(int16_t *dest, int seconds);
