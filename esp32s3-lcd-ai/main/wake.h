#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Wake-word detection via esp-sr WakeNet (raw, single-threaded).
 *
 * The whole module is a no-op unless CONFIG_VEE_WAKE_WORD is set; the stubs
 * below let the rest of the firmware compile and link either way. */

/* Load the WakeNet model from the 'model' partition. Returns true if a wake
 * word is ready. Safe to call once at startup; failure is non-fatal (the
 * caller should fall back to tap-to-talk). */
bool wake_init(void);

/* Number of 16-bit mono samples (16 kHz) WakeNet expects per detect() call. */
int wake_chunk_samples(void);

/* Feed exactly wake_chunk_samples() samples; returns true on a wake detection. */
bool wake_detect(const int16_t *chunk);
