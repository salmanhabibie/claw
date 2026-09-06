#include "chat_ui.h"

#include <math.h>
#include <time.h>
#include <stdio.h>

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_app_desc.h"

#include "audio.h"
#include "board.h"
#include "reminders.h"

#if CONFIG_WANDA_BLE_PROV
#include "esp_system.h"   /* esp_restart() */
#endif

static const char *TAG = "ui";

static lv_obj_t *s_status;
static lv_obj_t *s_response;
static lv_obj_t *s_clock;          /* big HH:MM, shown only when idle */
static lv_obj_t *s_clocksub;       /* date + next reminder, under the clock */
static lv_obj_t *s_eye_l;
static lv_obj_t *s_eye_r;
static lv_obj_t *s_mouth;
static lv_obj_t *s_dot;            /* orbiting "thinking" loader */
static lv_obj_t *s_smile;          /* friendly resting smile (idle only) */
static lv_obj_t *s_settings;       /* full-screen settings overlay (hidden) */
static lv_obj_t *s_vol_val;        /* "Volume  NN%" label */
static lv_obj_t *s_bri_val;        /* "Kecerahan  NN%" label */
static SemaphoreHandle_t s_talk_sem;
static ui_state_t s_state = UI_IDLE;

/* Current eye offset from the resting spot; x- and y- animations write these
 * independently so glance + bob can run together without fighting over align. */
static int32_t s_eye_dx = 0;
static int32_t s_eye_dy = 0;

/* Face geometry (412x412 round panel). */
#define EYE_W      66
#define EYE_H      88
#define EYE_GAP    58      /* horizontal distance of each eye from centre */
#define EYE_Y    (-24)

#define COL_IDLE    0x6fd0ff   /* cyan  */
#define COL_LISTEN  0x49e08b   /* green */
#define COL_THINK   0xffc24b   /* amber */
#define COL_SPEAK   0x6fd0ff

/* Pick the largest enabled Montserrat for the clock / titles, falling back to
 * the default font if the bigger ones aren't built in (so the build never
 * breaks on a stale sdkconfig that lacks them). */
#if LV_FONT_MONTSERRAT_48
static const lv_font_t *CLOCK_FONT = &lv_font_montserrat_48;
#elif LV_FONT_MONTSERRAT_28
static const lv_font_t *CLOCK_FONT = &lv_font_montserrat_28;
#else
static const lv_font_t *CLOCK_FONT = NULL;
#endif

#if LV_FONT_MONTSERRAT_28
static const lv_font_t *TITLE_FONT = &lv_font_montserrat_28;
#else
static const lv_font_t *TITLE_FONT = NULL;
#endif

/* ---- animation exec callbacks (drive both eyes / the mouth together) ---- */

/* Realign both eyes from the current dx/dy so x- and y- animations compose. */
static void eyes_realign(void)
{
    lv_obj_align(s_eye_l, LV_ALIGN_CENTER, -EYE_GAP + s_eye_dx, EYE_Y + s_eye_dy);
    lv_obj_align(s_eye_r, LV_ALIGN_CENTER,  EYE_GAP + s_eye_dx, EYE_Y + s_eye_dy);
}

/* Squash & stretch: as the eye shortens it bulges a little wider, so a blink
 * springs instead of collapsing flat. */
static void eye_h_exec(void *v, int32_t h)
{
    (void)v;
    int32_t w = EYE_W + (EYE_H - h) * 12 / 100;
    lv_obj_set_size(s_eye_l, w, h);
    lv_obj_set_size(s_eye_r, w, h);
}

static void eye_x_exec(void *v, int32_t x)
{
    (void)v;
    s_eye_dx = x;
    eyes_realign();
}

static void eye_y_exec(void *v, int32_t y)
{
    (void)v;
    s_eye_dy = y;
    eyes_realign();
}

static void mouth_h_exec(void *v, int32_t h)
{
    (void)v;
    lv_obj_set_height(s_mouth, h);
}

/* Drive the loader dot around a circle; `deg` is the angle in degrees. */
static void dot_orbit_exec(void *v, int32_t deg)
{
    (void)v;
    const float r = 128.0f;
    float rad = (float)deg * 3.1415926f / 180.0f;
    int x = (int)(r * cosf(rad));
    int y = (int)(r * sinf(rad));
    lv_obj_align(s_dot, LV_ALIGN_CENTER, x, y);
}

/* Gently pulse the loader dot's size so it breathes while it spins. */
static void dot_size_exec(void *v, int32_t s)
{
    (void)v;
    lv_obj_set_size(s_dot, s, s);
}

/* ---- helpers ---- */

static void set_eyes(int w, int h, uint32_t color)
{
    lv_obj_set_size(s_eye_l, w, h);
    lv_obj_set_size(s_eye_r, w, h);
    lv_obj_set_style_bg_color(s_eye_l, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(s_eye_r, lv_color_hex(color), 0);
    s_eye_dx = 0;
    s_eye_dy = 0;
    eyes_realign();
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

/* Gentle up/down nod, used while speaking so the face feels alive. */
static void anim_eye_bob(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_eye_l);
    lv_anim_set_exec_cb(&a, eye_y_exec);
    lv_anim_set_values(&a, -5, 5);
    lv_anim_set_duration(&a, 520);
    lv_anim_set_reverse_duration(&a, 520);
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

static void anim_dot_orbit(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_dot);
    lv_anim_set_exec_cb(&a, dot_orbit_exec);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_duration(&a, 1100);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);   /* continuous spin */
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

/* Size pulse for the loader dot (paired with the orbit spin). */
static void anim_dot_pulse(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_dot);
    lv_anim_set_exec_cb(&a, dot_size_exec);
    lv_anim_set_values(&a, 16, 26);
    lv_anim_set_duration(&a, 500);
    lv_anim_set_reverse_duration(&a, 500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* Two white catch-lights per eye — the single biggest "cute" cue. They are
 * children of the eye, so they move, squash and clip along with it for free. */
static void add_sparkle(lv_obj_t *eye)
{
    lv_obj_t *big = lv_obj_create(eye);
    lv_obj_remove_style_all(big);
    lv_obj_set_style_bg_opa(big, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(big, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(big, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(big, 20, 20);
    lv_obj_align(big, LV_ALIGN_TOP_MID, -8, 12);
    lv_obj_remove_flag(big, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(big, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tiny = lv_obj_create(eye);
    lv_obj_remove_style_all(tiny);
    lv_obj_set_style_bg_opa(tiny, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tiny, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(tiny, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(tiny, 9, 9);
    lv_obj_align(tiny, LV_ALIGN_TOP_MID, 12, 36);
    lv_obj_remove_flag(tiny, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tiny, LV_OBJ_FLAG_SCROLLABLE);
}

/* One blink: eyes snap closed and spring back (there-and-back once). */
static void do_blink(void)
{
    anim_eye_height(EYE_H, 8, 90, false);
}

/* Second half of a double blink, fired shortly after the first. */
static void second_blink_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state == UI_IDLE) {
        do_blink();
    }
}

/* A quick curious glance to one (random) side and back. */
static void idle_glance(void)
{
    int dir = (esp_random() & 1) ? 1 : -1;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_eye_l);
    lv_anim_set_exec_cb(&a, eye_x_exec);
    lv_anim_set_values(&a, 0, dir * 18);
    lv_anim_set_duration(&a, 340);
    lv_anim_set_reverse_duration(&a, 340);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

/* Idle antics: mostly single blinks, sometimes a double blink or a curious
 * glance, re-armed on an irregular interval so it never looks robotic. */
static void blink_timer_cb(lv_timer_t *t)
{
    if (s_state == UI_IDLE) {
        uint32_t r = esp_random() % 100;
        if (r < 15) {
            idle_glance();
        } else if (r < 35) {
            do_blink();
            lv_timer_t *again = lv_timer_create(second_blink_cb, 240, NULL);
            lv_timer_set_repeat_count(again, 1);   /* auto-deletes after firing */
        } else {
            do_blink();
        }
    }
    /* Re-arm at a random 2.0–4.4 s interval for natural, uneven blinking. */
    lv_timer_set_period(t, 2000 + (esp_random() % 2400));
}

static void talk_cb(lv_event_t *e)
{
    (void)e;
    /* Ignore taps meant for the settings panel. */
    if (s_settings != NULL && !lv_obj_has_flag(s_settings, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    ESP_LOGI(TAG, "tap -> start voice turn");
    if (s_talk_sem != NULL) {
        xSemaphoreGive(s_talk_sem);
    }
}

/* Refresh the idle clock from the system time once a second. */
static void clock_timer_cb(lv_timer_t *t)
{
    (void)t;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[8];
    if (tm.tm_year < (2020 - 1900)) {
        snprintf(buf, sizeof(buf), "--:--");          /* not yet NTP-synced */
        lv_label_set_text(s_clock, buf);
        lv_label_set_text(s_clocksub, "");
        return;
    }
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(s_clock, buf);

    /* Date line, plus the next reminder if there is one. */
    static const char *days[] = {
        "Minggu", "Senin", "Selasa", "Rabu", "Kamis", "Jumat", "Sabtu" };
    static const char *mons[] = {
        "Jan", "Feb", "Mar", "Apr", "Mei", "Jun",
        "Jul", "Agu", "Sep", "Okt", "Nov", "Des" };
    char sub[128];
    int w = snprintf(sub, sizeof(sub), "%s, %d %s",
                     days[tm.tm_wday], tm.tm_mday, mons[tm.tm_mon]);
    char next[64];
    reminders_next_summary(next, sizeof(next));
    if (next[0] != '\0' && w > 0 && (size_t)w < sizeof(sub)) {
        snprintf(sub + w, sizeof(sub) - w, "  -  %s", next);
    }
    lv_label_set_text(s_clocksub, sub);
}

/* ---- settings panel (long-press to open) ---- */

static void vol_slider_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int v = lv_slider_get_value(sl);
    audio_set_volume(v);
    bsp_nvs_set_u8("vol", (uint8_t)v);
    lv_label_set_text_fmt(s_vol_val, "Volume  %d%%", v);
}

static void bri_slider_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int v = lv_slider_get_value(sl);
    bsp_backlight_set(v);
    bsp_nvs_set_u8("bri", (uint8_t)v);
    lv_label_set_text_fmt(s_bri_val, "Kecerahan  %d%%", v);
}

static void wake_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    bsp_nvs_set_u8("wake", on ? 1 : 0);   /* applied on the next idle cycle */
}

#if CONFIG_WANDA_BLE_PROV
static void wifi_reset_cb(lv_event_t *e)
{
    (void)e;
    bsp_nvs_set_u8("reprov", 1);   /* re-provision over BLE on the next boot */
    esp_restart();
}
#endif

static void settings_open_cb(lv_event_t *e)
{
    (void)e;
    if (s_settings != NULL) {
        lv_obj_remove_flag(s_settings, LV_OBJ_FLAG_HIDDEN);
    }
}

static void settings_close_cb(lv_event_t *e)
{
    (void)e;
    if (s_settings != NULL) {
        lv_obj_add_flag(s_settings, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Build the (initially hidden) full-screen settings panel: volume + brightness
 * sliders and a close button. Top-most so it intercepts taps when open. */
static void build_settings(lv_obj_t *scr)
{
    s_settings = lv_obj_create(scr);
    lv_obj_remove_style_all(s_settings);
    lv_obj_set_size(s_settings, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_settings, lv_color_hex(0x0b0f14), 0);
    lv_obj_set_style_bg_opa(s_settings, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_settings, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_settings, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_settings, 14, 0);
    lv_obj_remove_flag(s_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_settings, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(s_settings);
    lv_label_set_text(title, "Setelan");
    if (TITLE_FONT) {
        lv_obj_set_style_text_font(title, TITLE_FONT, 0);
    }
    lv_obj_set_style_text_color(title, lv_color_hex(0x7ec8ff), 0);

    int vol = audio_get_volume();
    int bri = bsp_nvs_get_u8("bri", 100);

    /* Volume */
    s_vol_val = lv_label_create(s_settings);
    lv_label_set_text_fmt(s_vol_val, "Volume  %d%%", vol);
    lv_obj_set_style_text_color(s_vol_val, lv_color_hex(0xe6edf3), 0);
    lv_obj_t *vsl = lv_slider_create(s_settings);
    lv_obj_set_width(vsl, 250);
    lv_slider_set_range(vsl, 0, 100);
    lv_slider_set_value(vsl, vol, LV_ANIM_OFF);
    lv_obj_add_event_cb(vsl, vol_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Brightness */
    s_bri_val = lv_label_create(s_settings);
    lv_label_set_text_fmt(s_bri_val, "Kecerahan  %d%%", bri);
    lv_obj_set_style_text_color(s_bri_val, lv_color_hex(0xe6edf3), 0);
    lv_obj_t *bsl = lv_slider_create(s_settings);
    lv_obj_set_width(bsl, 250);
    lv_slider_set_range(bsl, 10, 100);   /* never fully dark */
    lv_slider_set_value(bsl, bri, LV_ANIM_OFF);
    lv_obj_add_event_cb(bsl, bri_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Wake-word listening toggle ("Wanda" via STT; uses data while on). */
    lv_obj_t *wlbl = lv_label_create(s_settings);
    lv_label_set_text(wlbl, "Dengar \"Wanda\"");
    lv_obj_set_style_text_color(wlbl, lv_color_hex(0xe6edf3), 0);
    lv_obj_t *wsw = lv_switch_create(s_settings);
    if (bsp_nvs_get_u8("wake", 1)) lv_obj_add_state(wsw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(wsw, wake_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

#if CONFIG_WANDA_BLE_PROV
    /* Forget WiFi + re-provision over BLE (reboots). */
    lv_obj_t *wbtn = lv_button_create(s_settings);
    lv_obj_add_event_cb(wbtn, wifi_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wblbl = lv_label_create(wbtn);
    lv_label_set_text(wblbl, "Atur ulang WiFi");
#endif

    /* Firmware version (short git hash), so anyone can read off exactly which
     * build is running - no serial monitor needed. */
    lv_obj_t *ver = lv_label_create(s_settings);
    lv_label_set_text_fmt(ver, "Versi  %s", esp_app_get_description()->version);
    lv_obj_set_style_text_color(ver, lv_color_hex(0x7d8b9c), 0);

    /* Close */
    lv_obj_t *btn = lv_button_create(s_settings);
    lv_obj_set_style_margin_top(btn, 10, 0);
    lv_obj_add_event_cb(btn, settings_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *blbl = lv_label_create(btn);
    lv_label_set_text(blbl, "Tutup");
}

void chat_ui_init(SemaphoreHandle_t talk_sem)
{
    s_talk_sem = talk_sem;

    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0f14), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Status line near the top (shown while active). */
    s_status = lv_label_create(scr);
    lv_label_set_text_fmt(s_status, "Booting... [%s]",
                          esp_app_get_description()->version);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x7ec8ff), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 42);

    /* Big clock (shown only while idle, in place of the status line). */
    s_clock = lv_label_create(scr);
    lv_label_set_text(s_clock, "--:--");
    if (CLOCK_FONT) {
        lv_obj_set_style_text_font(s_clock, CLOCK_FONT, 0);
    }
    lv_obj_set_style_text_color(s_clock, lv_color_hex(0xbfe6ff), 0);
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_add_flag(s_clock, LV_OBJ_FLAG_HIDDEN);

    /* Date + next reminder line, just under the clock (idle only). */
    s_clocksub = lv_label_create(scr);
    lv_label_set_long_mode(s_clocksub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_clocksub, 320);
    lv_obj_set_style_text_align(s_clocksub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_clocksub, lv_color_hex(0x7d8b9c) /* slate */, 0);
    lv_label_set_text(s_clocksub, "");
    lv_obj_align(s_clocksub, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_add_flag(s_clocksub, LV_OBJ_FLAG_HIDDEN);

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
        lv_obj_set_style_radius(eyes[i], 33, 0);   /* rounder, pill-shaped */
        lv_obj_set_style_clip_corner(eyes[i], true, 0);
        lv_obj_remove_flag(eyes[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(eyes[i], LV_OBJ_FLAG_SCROLLABLE);
        add_sparkle(eyes[i]);                       /* white catch-lights */
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

    /* Friendly resting smile: the lower arc of a circle (shown only while idle).
     * Only the indicator arc is drawn — background arc and knob are hidden. */
    s_smile = lv_arc_create(scr);
    lv_obj_remove_style_all(s_smile);
    lv_obj_set_size(s_smile, 118, 118);
    lv_arc_set_bg_angles(s_smile, 35, 145);
    lv_arc_set_angles(s_smile, 35, 145);
    lv_obj_set_style_arc_color(s_smile, lv_color_hex(COL_IDLE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_smile, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_smile, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_smile, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_smile, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_align(s_smile, LV_ALIGN_CENTER, 0, 44);
    lv_obj_remove_flag(s_smile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_smile, LV_OBJ_FLAG_HIDDEN);

    /* Orbiting loader dot (shown only while thinking). */
    s_dot = lv_obj_create(scr);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(COL_THINK), 0);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(s_dot, 20, 20);
    lv_obj_align(s_dot, LV_ALIGN_CENTER, 128, 0);
    lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);

    /* Response / transcript text near the bottom. */
    s_response = lv_label_create(scr);
    lv_label_set_long_mode(s_response, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_response, 300);
    lv_obj_set_style_text_align(s_response, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_response, lv_color_hex(0xe6edf3), 0);
    lv_label_set_text(s_response, "");
    lv_obj_align(s_response, LV_ALIGN_BOTTOM_MID, 0, -34);

    /* Transparent full-screen tap layer: short tap to talk, long-press for
     * settings. SHORT_CLICKED (not CLICKED) so a long-press doesn't also talk. */
    lv_obj_t *overlay = lv_button_create(scr);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(overlay, talk_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(overlay, settings_open_cb, LV_EVENT_LONG_PRESSED, NULL);

    build_settings(scr);

    /* Blink driver + 1 Hz clock. */
    lv_timer_create(blink_timer_cb, 3200, NULL);
    lv_timer_create(clock_timer_cb, 1000, NULL);

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
    lv_anim_delete(s_dot, NULL);
    lv_obj_add_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);

    /* Idle shows the clock (and date/next-reminder) instead of the status line. */
    if (state == UI_IDLE) {
        lv_obj_remove_flag(s_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_clocksub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_smile, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_clocksub, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_smile, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }

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
        s_eye_dy = -10;                                   /* look up, pondering */
        eyes_realign();
        anim_eye_glance();                                /* look side to side */
        lv_obj_remove_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        anim_dot_orbit();                                 /* spinning loader */
        anim_dot_pulse();                                 /* ...that also breathes */
        break;
    case UI_SPEAKING:
        set_eyes(EYE_W, EYE_H - 20, COL_SPEAK);           /* happy squint */
        anim_eye_bob();                                   /* nod along */
        lv_obj_remove_flag(s_mouth, LV_OBJ_FLAG_HIDDEN);
        anim_mouth();                                     /* talking mouth */
        break;
    }
    lvgl_port_unlock();
}
