#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Initialize the I2S TX path to the PCM5101 DAC (speaker), 16 kHz. */
esp_err_t audio_init(void);

/* Play 16-bit little-endian MONO PCM at 16 kHz. Each mono sample is duplicated
 * to both stereo channels. Blocks until the data has been queued to I2S. */
void audio_play_mono16(const uint8_t *data, size_t len);

/* Set/get output volume as 0..100 percent (100 == the board's loud, slightly
 * clipping max). Applied to subsequent audio_play_mono16() calls. */
void audio_set_volume(int percent);
int  audio_get_volume(void);

/* Play a loud ~1.5s 440 Hz sine tone locally (no network). Diagnostic to test
 * the I2S -> DAC -> amplifier -> speaker path on its own. */
void audio_play_test_tone(void);

/* Play a short two-note chime (no network). Used to announce a reminder. */
void audio_play_chime(void);

/* Microphone capture rate (16 kHz, matches the STT request). */
#define MIC_SAMPLE_RATE 16000

/* Initialize the I2S RX path from the on-board microphone. */
esp_err_t mic_init(void);

/* Record `seconds` of audio into `dest` as 16-bit mono PCM at 16 kHz. `dest`
 * must hold at least seconds*MIC_SAMPLE_RATE samples. Returns samples captured. */
size_t mic_record(int16_t *dest, int seconds);

/* Like mic_record, but also reports via *speech_out whether any speech was
 * detected, so callers (e.g. the wake-word loop) can skip pure silence. */
size_t mic_record_vad(int16_t *dest, int seconds, bool *speech_out);

/* Low-level continuous capture, used by the wake-word loop. Start/stop enable
 * and disable the I2S RX channel; mic_read() blocks until `nsamp` 16-bit mono
 * samples are filled (or an I2S error). Do NOT mix with mic_record() while a
 * stream is started — stop it first. */
esp_err_t mic_stream_start(void);
void      mic_stream_stop(void);
size_t    mic_read(int16_t *dest, size_t nsamp);
