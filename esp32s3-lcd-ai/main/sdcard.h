#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_io_expander.h"

/* microSD support (SPI mode, FATFS at /sdcard).
 *
 * The card's chip-select is NOT a direct GPIO on this board: it hangs off the
 * TCA9554 I2C expander (EXIO3). The sdspi driver can't toggle that fast, so we
 * park EXIO3 LOW permanently and mount with SDSPI_SLOT_NO_CS - legal because
 * the SD card is alone on its SPI bus (the LCD uses a separate QSPI bus).
 *
 * Everything here degrades gracefully: with no card inserted (or a mount
 * error) all calls become cheap no-ops / "not available" answers, so Wanda
 * runs exactly as before. */

/* Mount the card. Call once from app_main after bsp_expander_init(). */
esp_err_t sd_init(esp_io_expander_handle_t expander);

bool sd_mounted(void);

/* Capacity in MiB. Returns false when no card is mounted. */
bool sd_info(uint32_t *total_mib, uint32_t *free_mib);

/* ---- conversation journal (one file per month, /sdcard/wanda/jurnal) ---- */

/* Append one line: "[YYYY-MM-DD HH:MM] who: text". No-op without a card. */
void sd_journal_append(const char *who, const char *text);

/* Case-insensitive substring search over the current + previous month,
 * newest matches last. Writes up to outlen bytes; returns the number of
 * matching lines found (0 also when no card / no match). */
int sd_journal_search(const char *query, char *out, size_t outlen);

/* ---- TTS audio cache (/sdcard/wanda/tts, raw PCM keyed by text hash) ---- */

/* Returns a heap buffer (caller frees) with the cached clip, or NULL. */
uint8_t *sd_tts_cache_get(const char *key, size_t *out_len);

/* Store a clip; silently evicts the oldest clips when the cache is full. */
void sd_tts_cache_put(const char *key, const uint8_t *pcm, size_t len);
