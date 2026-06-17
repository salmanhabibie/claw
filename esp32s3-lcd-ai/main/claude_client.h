#pragma once

/* Send a single-turn prompt to the Claude Messages API.
 *
 * Returns a heap-allocated string with Claude's reply (or an API error
 * message). The caller must free() it. Returns NULL on a transport-level
 * failure (no network, TLS error, out of memory, etc.). */
char *claude_ask(const char *prompt);
