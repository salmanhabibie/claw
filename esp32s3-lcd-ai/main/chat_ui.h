#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Build the voice UI: a status line, a big round "TALK" button, and a response
 * area. Tapping TALK gives `talk_sem` so a worker task can start a voice turn. */
void chat_ui_init(SemaphoreHandle_t talk_sem);

/* Update the status line at the top. Thread-safe. */
void chat_ui_set_status(const char *text);

/* Show response / transcript text in the lower area. Thread-safe. */
void chat_ui_set_response(const char *text);
