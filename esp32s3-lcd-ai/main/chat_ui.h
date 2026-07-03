#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Visual states of the animated assistant face. */
typedef enum {
    UI_IDLE,        /* calm eyes, occasional blink */
    UI_LISTENING,   /* green, gently breathing eyes */
    UI_THINKING,    /* amber, eyes glancing side to side */
    UI_SPEAKING,    /* talking mouth animation */
} ui_state_t;

/* Build the animated assistant face. Tapping anywhere on the screen gives
 * `talk_sem` so a worker task can start a voice turn. */
void chat_ui_init(SemaphoreHandle_t talk_sem);

/* Update the status line at the top. Thread-safe. */
void chat_ui_set_status(const char *text);

/* Show response / transcript text in the lower area. Thread-safe. */
void chat_ui_set_response(const char *text);

/* Switch the face animation to reflect what the assistant is doing. */
void chat_ui_set_state(ui_state_t state);

/* Brief "kaget" reaction (wide trembling eyes) for when the device is shaken.
 * Only plays while idle; returns true if the reaction actually started.
 * Thread-safe. */
bool chat_ui_startle(void);
