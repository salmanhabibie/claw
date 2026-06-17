#include "chat_ui.h"

#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "ui";

static lv_obj_t *s_status;
static lv_obj_t *s_chat_ta;
static lv_obj_t *s_input_ta;
static QueueHandle_t s_prompt_q;

/* Fired when the user taps the check / enter key on the on-screen keyboard.
 * Runs in the LVGL task, so touching widgets directly is safe. esp_lvgl_port
 * uses a recursive lock, so calling lvgl_port_lock() again here is fine. */
static void keyboard_ready_cb(lv_event_t *e)
{
    (void)e;
    const char *txt = lv_textarea_get_text(s_input_ta);
    if (txt == NULL || strlen(txt) == 0) {
        return;
    }

    lv_textarea_add_text(s_chat_ta, "You: ");
    lv_textarea_add_text(s_chat_ta, txt);
    lv_textarea_add_text(s_chat_ta, "\n");

    char *copy = strdup(txt);
    if (copy != NULL && s_prompt_q != NULL) {
        if (xQueueSend(s_prompt_q, &copy, 0) != pdTRUE) {
            ESP_LOGW(TAG, "prompt queue full, dropping");
            free(copy);
        } else {
            lv_textarea_add_text(s_chat_ta, "Claude is thinking...\n");
        }
    }

    lv_textarea_set_text(s_input_ta, "");
}

void chat_ui_init(QueueHandle_t prompt_q)
{
    s_prompt_q = prompt_q;

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Status line */
    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "Booting...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x7ec8ff), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 10);

    /* Conversation log (read-only-ish, scrolls as it fills) */
    s_chat_ta = lv_textarea_create(scr);
    lv_obj_set_size(s_chat_ta, 380, 168);
    lv_obj_align(s_chat_ta, LV_ALIGN_TOP_MID, 0, 34);
    lv_textarea_set_text(s_chat_ta, "");
    lv_obj_set_style_text_color(s_chat_ta, lv_color_hex(0xe6edf3), 0);
    lv_obj_set_style_bg_color(s_chat_ta, lv_color_hex(0x161b22), 0);

    /* Input box */
    s_input_ta = lv_textarea_create(scr);
    lv_textarea_set_one_line(s_input_ta, true);
    lv_textarea_set_placeholder_text(s_input_ta, "Ask Claude...");
    lv_obj_set_size(s_input_ta, 380, 40);
    lv_obj_align(s_input_ta, LV_ALIGN_CENTER, 0, 24);

    /* On-screen keyboard, pinned to the bottom */
    lv_obj_t *kb = lv_keyboard_create(scr);
    lv_obj_set_size(kb, 412, 150);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_input_ta);
    lv_obj_add_event_cb(kb, keyboard_ready_cb, LV_EVENT_READY, NULL);

    lvgl_port_unlock();
}

void chat_ui_add_assistant(const char *text)
{
    lvgl_port_lock(0);
    lv_textarea_add_text(s_chat_ta, "Claude: ");
    lv_textarea_add_text(s_chat_ta, (text != NULL) ? text : "[no reply]");
    lv_textarea_add_text(s_chat_ta, "\n");
    lvgl_port_unlock();
}

void chat_ui_set_status(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_status, text);
    lvgl_port_unlock();
}
