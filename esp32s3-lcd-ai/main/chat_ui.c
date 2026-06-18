#include "chat_ui.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "ui";

static lv_obj_t *s_status;
static lv_obj_t *s_response;
static SemaphoreHandle_t s_talk_sem;

/* Log every touch that reaches LVGL, so we can tell whether the touch panel is
 * read at all and whether its coordinates line up with what is drawn. */
static void screen_touch_log_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (indev != NULL) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        ESP_LOGI(TAG, "touch at x=%d y=%d", (int)p.x, (int)p.y);
    }
}

/* Runs in the LVGL task when the TALK button is tapped. esp_lvgl_port uses a
 * recursive lock, so calling chat_ui_set_* (which locks) from here is fine. */
static void talk_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "TALK button pressed");
    if (s_talk_sem != NULL) {
        xSemaphoreGive(s_talk_sem);
    }
}

void chat_ui_init(SemaphoreHandle_t talk_sem)
{
    s_talk_sem = talk_sem;

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, screen_touch_log_cb, LV_EVENT_PRESSED, NULL);

    /* Status line */
    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "Booting...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x7ec8ff), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 26);

    /* Big round TALK button in the centre */
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 190, 190);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2563eb), 0);
    lv_obj_add_event_cb(btn, talk_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "TALK");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(btn_lbl);

    /* Response / transcript area near the bottom */
    s_response = lv_label_create(scr);
    lv_label_set_long_mode(s_response, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_response, 300);
    lv_obj_set_style_text_align(s_response, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_response, lv_color_hex(0xe6edf3), 0);
    lv_label_set_text(s_response, "");
    lv_obj_align(s_response, LV_ALIGN_BOTTOM_MID, 0, -28);

    lvgl_port_unlock();
}

void chat_ui_set_status(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_status, text);
    lvgl_port_unlock();
}

void chat_ui_set_response(const char *text)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_response, (text != NULL) ? text : "");
    lvgl_port_unlock();
}
