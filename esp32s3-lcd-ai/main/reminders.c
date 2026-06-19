#include "reminders.h"

#include <string.h>
#include <stdio.h>

#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "rem";

#define MAX_REM 8

typedef struct {
    time_t when;
    bool   daily;
    bool   active;
    char   msg[96];
} reminder_t;

static reminder_t s_rem[MAX_REM];

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("rem", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "list", s_rem, sizeof(s_rem));
    nvs_commit(h);
    nvs_close(h);
}

void reminders_init(void)
{
    nvs_handle_t h;
    if (nvs_open("rem", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_rem);
        if (nvs_get_blob(h, "list", s_rem, &sz) != ESP_OK || sz != sizeof(s_rem)) {
            memset(s_rem, 0, sizeof(s_rem));   /* version/size mismatch: reset */
        }
        nvs_close(h);
    }
}

bool reminders_add(time_t when, const char *msg, bool daily)
{
    for (int i = 0; i < MAX_REM; i++) {
        if (!s_rem[i].active) {
            s_rem[i].active = true;
            s_rem[i].when   = when;
            s_rem[i].daily  = daily;
            snprintf(s_rem[i].msg, sizeof(s_rem[i].msg), "%s", msg ? msg : "");
            save();
            ESP_LOGI(TAG, "added reminder @%ld daily=%d: %s",
                     (long)when, daily, s_rem[i].msg);
            return true;
        }
    }
    return false;
}

bool reminders_any_due(time_t now)
{
    for (int i = 0; i < MAX_REM; i++) {
        if (s_rem[i].active && s_rem[i].when <= now) return true;
    }
    return false;
}

bool reminders_pop_due(time_t now, char *out, size_t outlen)
{
    for (;;) {
        int best = -1;
        for (int i = 0; i < MAX_REM; i++) {
            if (s_rem[i].active && s_rem[i].when <= now) {
                if (best < 0 || s_rem[i].when < s_rem[best].when) best = i;
            }
        }
        if (best < 0) return false;

        /* Drop a one-shot reminder missed by >1h (e.g. device was off) so it
         * doesn't speak stale on the next boot. */
        if (!s_rem[best].daily && (now - s_rem[best].when) > 3600) {
            s_rem[best].active = false;
            save();
            continue;
        }

        snprintf(out, outlen, "%s", s_rem[best].msg);
        if (s_rem[best].daily) {
            do { s_rem[best].when += 24 * 3600; } while (s_rem[best].when <= now);
        } else {
            s_rem[best].active = false;
        }
        save();
        return true;
    }
}

void reminders_list(char *out, size_t outlen)
{
    size_t off = 0;
    int n = 0;
    if (outlen == 0) return;
    out[0] = '\0';
    for (int i = 0; i < MAX_REM && off + 1 < outlen; i++) {
        if (!s_rem[i].active) continue;
        struct tm tm;
        localtime_r(&s_rem[i].when, &tm);
        int w = snprintf(out + off, outlen - off, "%02d:%02d%s - %s\n",
                         tm.tm_hour, tm.tm_min,
                         s_rem[i].daily ? " (harian)" : "", s_rem[i].msg);
        if (w > 0) off += (size_t)w;
        n++;
    }
    if (n == 0) snprintf(out, outlen, "(tidak ada pengingat)");
}

int reminders_clear(void)
{
    int n = 0;
    for (int i = 0; i < MAX_REM; i++) {
        if (s_rem[i].active) { s_rem[i].active = false; n++; }
    }
    save();
    return n;
}
