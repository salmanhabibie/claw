#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Build the chat screen (conversation log + input box + on-screen keyboard).
 * When the user taps the keyboard's "enter", the typed text is pushed to
 * prompt_q as a malloc'd char* (the receiver must free it). */
void chat_ui_init(QueueHandle_t prompt_q);

/* Append Claude's reply to the conversation log. Thread-safe. */
void chat_ui_add_assistant(const char *text);

/* Update the small status line at the top. Thread-safe. */
void chat_ui_set_status(const char *text);
