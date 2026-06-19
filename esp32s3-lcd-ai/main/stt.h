#pragma once

#include <stddef.h>
#include <stdint.h>

/* Transcribe 16-bit mono 16 kHz PCM to text via the ElevenLabs Speech-to-Text
 * (Scribe) API. Returns a heap-allocated UTF-8 string (caller frees) or NULL
 * on failure / empty result. Language is auto-detected (Indonesian works). */
char *stt_transcribe(const int16_t *pcm, size_t nsamples);
