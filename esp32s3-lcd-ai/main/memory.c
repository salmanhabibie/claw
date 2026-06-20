#include "memory.h"

#include <string.h>
#include <stdio.h>

#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "mem";

#define NAME_MAX   48
#define NOTES_MAX  800

static char s_name[NAME_MAX];
static char s_notes[NOTES_MAX];

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("memory", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "name", s_name);
    nvs_set_str(h, "notes", s_notes);
    nvs_commit(h);
    nvs_close(h);
}

void memory_init(void)
{
    nvs_handle_t h;
    if (nvs_open("memory", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(s_name);
        if (nvs_get_str(h, "name", s_name, &n) != ESP_OK) s_name[0] = '\0';
        n = sizeof(s_notes);
        if (nvs_get_str(h, "notes", s_notes, &n) != ESP_OK) s_notes[0] = '\0';
        nvs_close(h);
    }
    ESP_LOGI(TAG, "loaded name='%s', %u bytes of notes",
             s_name, (unsigned)strlen(s_notes));
}

void memory_set_name(const char *name)
{
    snprintf(s_name, sizeof(s_name), "%s", name ? name : "");
    save();
}

void memory_get_name(char *out, size_t outlen)
{
    snprintf(out, outlen, "%s", s_name);
}

bool memory_add(const char *note)
{
    if (note == NULL || note[0] == '\0') return false;
    size_t cur = strlen(s_notes);
    size_t add = strlen(note) + (cur ? 1 : 0);     /* +1 for the separator */
    if (cur + add + 1 > sizeof(s_notes)) return false;
    if (cur) s_notes[cur++] = '\n';
    snprintf(s_notes + cur, sizeof(s_notes) - cur, "%s", note);
    save();
    return true;
}

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool ci_contains(const char *hay, const char *needle)
{
    if (!*needle) return true;
    for (; *hay; hay++) {
        const char *h = hay, *n = needle;
        while (*h && *n && lc(*h) == lc(*n)) { h++; n++; }
        if (!*n) return true;
    }
    return false;
}

int memory_remove_matching(const char *substr)
{
    if (substr == NULL || substr[0] == '\0') return 0;
    char tmp[NOTES_MAX];
    char rebuilt[NOTES_MAX];
    snprintf(tmp, sizeof(tmp), "%s", s_notes);
    size_t off = 0;
    int removed = 0;
    char *sp = NULL;
    rebuilt[0] = '\0';
    for (char *line = strtok_r(tmp, "\n", &sp); line != NULL;
         line = strtok_r(NULL, "\n", &sp)) {
        if (ci_contains(line, substr)) { removed++; continue; }
        int w = snprintf(rebuilt + off, sizeof(rebuilt) - off,
                         "%s%s", off ? "\n" : "", line);
        if (w > 0) off += (size_t)w;
    }
    if (removed) {
        snprintf(s_notes, sizeof(s_notes), "%s", rebuilt);
        save();
    }
    return removed;
}

int memory_clear(void)
{
    int n = 0;
    if (s_notes[0]) {
        n = 1;
        for (char *p = s_notes; *p; p++) if (*p == '\n') n++;
    }
    s_notes[0] = '\0';
    save();
    return n;
}

void memory_get_prompt(char *out, size_t outlen)
{
    if (outlen == 0) return;
    if (s_name[0] == '\0' && s_notes[0] == '\0') { out[0] = '\0'; return; }
    size_t off = 0;
    off += snprintf(out + off, outlen - off,
                    "Yang kamu ingat tentang pengguna (pakai bila relevan, "
                    "jangan diungkit kecuali perlu):");
    if (s_name[0] && off < outlen)
        off += snprintf(out + off, outlen - off, "\n- Nama pengguna: %s", s_name);
    if (s_notes[0] && off < outlen)
        off += snprintf(out + off, outlen - off, "\n%s", s_notes);
}
