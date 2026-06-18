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
