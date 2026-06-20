#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* Local reminders/alarms, persisted in NVS so they survive a reboot. Times are
 * absolute (epoch seconds). The voice task fires them while idle. */

/* Load saved reminders from NVS. Call once at startup. */
void reminders_init(void);

/* Add a reminder firing at `when`; if `daily`, it repeats every 24h. Returns
 * false if the (small) table is full. */
bool reminders_add(time_t when, const char *msg, bool daily);

/* True if any reminder is due at/Before `now` (peek; does not consume). */
bool reminders_any_due(time_t now);

/* Consume the earliest due reminder: copies its text into `out`, then removes
 * it (or reschedules to the next day if daily). Returns false if none due. */
bool reminders_pop_due(time_t now, char *out, size_t outlen);

/* Write a human-readable list of active reminders into `out`. */
void reminders_list(char *out, size_t outlen);

/* Short one-line summary of the soonest upcoming reminder ("20:00 minum obat"),
 * for the idle screen. Sets out[0]='\0' when there are none. */
void reminders_next_summary(char *out, size_t outlen);

/* Cancel all reminders; returns how many were active. */
int reminders_clear(void);
