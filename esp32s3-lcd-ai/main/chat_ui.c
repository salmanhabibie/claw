#include "chat_ui.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "ui";

static lv_obj_t *s_status;
static lv_obj_t *s_response;
static lv_obj_t *s_eye_l;
static lv_obj_t *s_eye_r;
static lv_obj_t *s_mouth;
static SemaphoreHandle_t s_talk_sem;
static ui_state_t s_state = UI_IDLE;

/* Face geometry (412x412 round panel). */
#define EYE_W      66
#define EYE_H      88
#define EYE_GAP    58      /* horizontal distance of each eye from centre */
#define EYE_Y    (-24)

#define COL_IDLE    0x6fd0ff   /* cyan  */
#define COL_LISTEN  0x49e08b   /* green */
#define COL_THINK   0xffc24b   /* amber */
#define COL_SPEAK   0x6fd0ff

/* ---- animation exec callbacks (drive both eyes / the mouth together) ---- */

static void eye_h_exec(void *v, int32_t h)
{
    (void)v;
    lv_obj_set_height(s_eye_l, h);
    lv_obj_set_height(s_eye_r, h);
}

static void eye_x_exec(void *v, int32_t x)
{
    (void)v;
    lv_obj_align(s_eye_l, LV_ALIGN_CENTER, -EYE_GAP + x, EYE_Y);
    lv_obj_align(s_eye_r, LV_ALIGN_CENTER,  EYE_GAP + x, EYE_Y);
}

static void mouth_h_exec(void *v, int32_t h)
{
    (void)v;
    lv_obj_set_height(s_mouth, h);
}

/* ---- helpers ---- */

static void set_eyes(int w, int h, uint32_t color)
{
    lv_obj_set_size(s_eye_l, w, h);
    lv_obj_set_size(s_eye_r, w, h);
    lv_obj_set_style_bg_color(s_eye_l, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_eye_r, lv_color_hex(color), 0);
    lv_obj_align(s_eye_l, LV_ALIGN_CENTER, -EYE_GAP, EYE_Y);
    lv_obj_align(s_eye_r, LV_ALIGN_CENTER,  EYE_GAP, EYE_Y);
}

static void anim_eye_height(int from, int to, uint32_t dur, bool loop)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_eye_l);
    lv_anim_set_exec_cb(&a, eye_h_exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_reverse_duration(&a, dur);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    if (loop) lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void anim_eye_glance(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_eye_l);
    lv_anim_set_exec_cb(&a, eye_x_exec);
    lv_anim_set_values(&a, -14, 14);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_reverse_duration(&a, 600);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void anim_mouth(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_mouth);
    lv_anim_set_exec_cb(&a, mouth_h_exec);
    lv_anim_set_values(&a, 6, 30);
    lv_anim_set_duration(&a, 140);
    lv_anim_set_reverse_duration(&a, 140);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* Periodic blink, only while idle. */
static void blink_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state == UI_IDLE) {
        anim_eye_height(EYE_H, 8, 90, false);
    }
}

static void talk_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "tap -> start voice turn");
    if (s_talk_sem != NULL) {
        xSemaphoreGive(s_talk_sem);
    }
}

void chat_ui_init(SemaphoreHandle_t talk_sem)
{
    s_talk_sem = talk_sem;

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0f14), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Status line near the top. */
    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "Booting...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x7ec8ff), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 42);

    /* Two eyes. */
    lv_obj_t *eyes[2];
    s_eye_l = lv_obj_create(scr);
    s_eye_r = lv_obj_create(scr);
    eyes[0] = s_eye_l;
    eyes[1] = s_eye_r;
    for (int i = 0; i < 2; i++) {
        lv_obj_remove_style_all(eyes[i]);
        lv_obj_set_style_bg_opa(eyes[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(eyes[i], lv_color_hex(COL_IDLE), 0);
        lv_obj_set_style_radius(eyes[i], 28, 0);
        lv_obj_remove_flag(eyes[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(eyes[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* Mouth (hidden unless speaking). */
    s_mouth = lv_obj_create(scr);
    lv_obj_remove_style_all(s_mouth);
    lv_obj_set_style_bg_opa(s_mouth, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_mouth, lv_color_hex(COL_SPEAK), 0);
    lv_obj_set_style_radius(s_mouth, 8, 0);
    lv_obj_set_size(s_mouth, 96, 12);
    lv_obj_align(s_mouth, LV_ALIGN_CENTER, 0, 78);
    lv_obj_remove_flag(s_mouth, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_mouth, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);

    /* Response / transcript text near the bottom. */
    s_response = lv_label_create(scr);
    lv_label_set_long_mode(s_response, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_response, 300);
    lv_obj_set_style_text_align(s_response, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_response, lv_color_hex(0xe6edf3), 0);
    lv_label_set_text(s_response, "");
    lv_obj_align(s_response, LV_ALIGN_BOTTOM_MID, 0, -34);

    /* Transparent full-screen tap layer: tap anywhere to talk. */
    lv_obj_t *overlay = lv_button_create(scr);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, talk_cb, LV_EVENT_CLICKED, NULL);

    /* Blink driver. */
    lv_timer_create(blink_timer_cb, 3200, NULL);

    set_eyes(EYE_W, EYE_H, COL_IDLE);

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

void chat_ui_set_state(ui_state_t state)
{
    if (s_eye_l == NULL) {
        return;
    }
    lvgl_port_lock(0);
    s_state = state;

    /* Stop any state-specific animation before reconfiguring. */
    lv_anim_delete(s_eye_l, NULL);
    lv_anim_delete(s_mouth, NULL);
    lv_obj_add_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);

    switch (state) {
    case UI_IDLE:
        set_eyes(EYE_W, EYE_H, COL_IDLE);
        break;
    case UI_LISTENING:
        set_eyes(EYE_W, EYE_H, COL_LISTEN);
        anim_eye_height(EYE_H, EYE_H + 18, 850, true);   /* gentle breathing */
        break;
    case UI_THINKING:
        set_eyes(EYE_W, EYE_H, COL_THINK);
        anim_eye_glance();                                /* look side to side */
        break;
    case UI_SPEAKING:
        set_eyes(EYE_W, EYE_H - 20, COL_SPEAK);           /* happy squint */
        lv_obj_remove_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
        anim_mouth();                                     /* talking mouth */
        break;
    }
    lvgl_port_unlock();
}
