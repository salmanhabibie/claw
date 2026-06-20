#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Small persistent memory so Wanda can remember the user across reboots:
 * a name plus free-form notes/preferences/lists. Stored in NVS. */

void memory_init(void);

/* User's name, used for a personalised greeting and to address them. */
void memory_set_name(const char *name);
void memory_get_name(char *out, size_t outlen);   /* "" if unknown */

/* Free-form notes/preferences/lists, one per line. */
bool memory_add(const char *note);                 /* false if full */
int  memory_remove_matching(const char *substr);   /* lines removed (case-insens.) */
int  memory_clear(void);                           /* clears notes; returns count */

/* Compose the "things I remember" block for the system prompt. Sets out[0]='\0'
 * when there is nothing remembered. */
void memory_get_prompt(char *out, size_t outlen);
