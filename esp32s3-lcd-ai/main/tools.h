#pragma once

#include "cJSON.h"

/* Execute a tool that Claude requested by name, with its JSON input object.
 * Returns a heap-allocated text result (caller frees), or NULL on failure. */
char *tool_execute(const char *name, const cJSON *input);
