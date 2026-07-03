#include "sdcard.h"
#include "bsp_pins.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

#define MOUNT_POINT   "/sdcard"
#define SD_HOST       SPI3_HOST          /* the LCD's QSPI owns SPI2 */
#define DIR_BASE      MOUNT_POINT "/wanda"
#define DIR_JOURNAL   DIR_BASE "/jurnal"
#define DIR_TTS       DIR_BASE "/tts"
#define TTS_MAX_FILES 400                /* ~10 s clip = ~480 KB; cap the cache */

/* FATFS here runs with 8.3 names (LFN is off in the default sdkconfig), so
 * every name below stays within 8 chars + 3-char extension. */

static sdmmc_card_t *s_card;
static SemaphoreHandle_t s_lock;         /* serialises journal/boot-log writes */

bool sd_mounted(void)
{
    return s_card != NULL;
}

bool sd_info(uint32_t *total_mib, uint32_t *free_mib)
{
    if (s_card == NULL) return false;
    uint64_t total = 0, freeb = 0;
    if (esp_vfs_fat_info(MOUNT_POINT, &total, &freeb) != ESP_OK) return false;
    if (total_mib) *total_mib = (uint32_t)(total >> 20);
    if (free_mib)  *free_mib  = (uint32_t)(freeb >> 20);
    return true;
}

esp_err_t sd_init(esp_io_expander_handle_t expander)
{
    s_lock = xSemaphoreCreateMutex();

    /* Park the expander-driven CS low for good (see header). Do this before
     * any clocks so the card sees CS asserted from the first command. */
    esp_err_t err = esp_io_expander_set_level(expander, BSP_EXIO_SD_CS, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not drive EXIO3 (SD CS) low: %s", esp_err_to_name(err));
        return err;
    }

    const spi_bus_config_t bus = {
        .mosi_io_num = BSP_SD_MOSI,
        .miso_io_num = BSP_SD_MISO,
        .sclk_io_num = BSP_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    err = spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SD_HOST;
    slot.gpio_cs = SDSPI_SLOT_NO_CS;     /* CS lives on the expander, held low */

    const esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false,
        .max_files = 6,
        .allocation_unit_size = 16 * 1024,
    };
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (err != ESP_OK) {
        /* Most common cause: no card in the slot. Not an error for Wanda. */
        ESP_LOGW(TAG, "no SD card mounted (%s)", esp_err_to_name(err));
        s_card = NULL;
        spi_bus_free(SD_HOST);
        return err;
    }

    mkdir(DIR_BASE, 0775);
    mkdir(DIR_JOURNAL, 0775);
    mkdir(DIR_TTS, 0775);

    uint32_t total = 0, freem = 0;
    sd_info(&total, &freem);
    ESP_LOGI(TAG, "mounted %s: %lu MiB total, %lu MiB free",
             s_card->cid.name, (unsigned long)total, (unsigned long)freem);

    /* Tiny boot log - handy when debugging crashes in the field. */
    FILE *f = fopen(DIR_BASE "/boot.log", "a");
    if (f != NULL) {
        fprintf(f, "boot: reset_reason=%d free_heap=%u\n",
                (int)esp_reset_reason(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        fclose(f);
    }
    return ESP_OK;
}

/* ---- journal ---- */

/* Current month's journal path, e.g. /sdcard/wanda/jurnal/2026-07.txt.
 * months_back shifts to earlier months (for searching). */
static void journal_path(char *out, size_t outlen, int months_back)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int mon = tm.tm_mon - months_back;
    int year = tm.tm_year + 1900;
    while (mon < 0) { mon += 12; year--; }
    snprintf(out, outlen, DIR_JOURNAL "/%04d-%02d.txt", year, mon + 1);
}

void sd_journal_append(const char *who, const char *text)
{
    if (s_card == NULL || text == NULL || text[0] == '\0') return;

    char path[64];
    journal_path(path, sizeof(path), 0);

    char stamp[48] = "boot";
    time_t now = time(NULL);
    if (now > 1700000000) {
        struct tm tm;
        localtime_r(&now, &tm);
        snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    FILE *f = fopen(path, "a");
    if (f != NULL) {
        /* Keep each entry on one line so searching stays line-based. */
        fprintf(f, "[%s] %s: ", stamp, who);
        for (const char *p = text; *p; p++) {
            fputc((*p == '\n' || *p == '\r') ? ' ' : *p, f);
        }
        fputc('\n', f);
        fclose(f);
    }
    xSemaphoreGive(s_lock);
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

int sd_journal_search(const char *query, char *out, size_t outlen)
{
    if (outlen == 0) return 0;
    out[0] = '\0';
    if (s_card == NULL || query == NULL || query[0] == '\0') return 0;

    /* Ring of the newest matches: scanning previous month first, then the
     * current one, keeps entries in chronological order in the ring. */
    enum { MAX_HITS = 12, HIT_MAX = 200 };
    static char ring[MAX_HITS][HIT_MAX];
    int head = 0, count = 0, found = 0;

    for (int back = 1; back >= 0; back--) {
        char path[64];
        journal_path(path, sizeof(path), back);
        FILE *f = fopen(path, "r");
        if (f == NULL) continue;
        char line[320];
        while (fgets(line, sizeof(line), f) != NULL) {
            if (!ci_contains(line, query)) continue;
            found++;
            size_t n = strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
            if (n >= HIT_MAX) n = HIT_MAX - 1;
            memcpy(ring[head], line, n);
            ring[head][n] = '\0';
            head = (head + 1) % MAX_HITS;
            if (count < MAX_HITS) count++;
        }
        fclose(f);
    }

    size_t off = 0;
    int start = (head - count + MAX_HITS) % MAX_HITS;
    for (int i = 0; i < count && off + 2 < outlen; i++) {
        const char *s = ring[(start + i) % MAX_HITS];
        int w = snprintf(out + off, outlen - off, "%s%s", off ? "\n" : "", s);
        if (w <= 0) break;
        off += (size_t)w;
        if (off >= outlen) { out[outlen - 1] = '\0'; break; }
    }
    return found;
}

/* ---- TTS cache ---- */

static void tts_path(char *out, size_t outlen, const char *key)
{
    snprintf(out, outlen, DIR_TTS "/%.8s.pcm", key);
}

uint8_t *sd_tts_cache_get(const char *key, size_t *out_len)
{
    if (s_card == NULL || key == NULL) return NULL;
    char path[64];
    tts_path(path, sizeof(path), key);

    struct stat st;
    if (stat(path, &st) != 0 || st.st_size < 2) return NULL;

    uint8_t *buf = heap_caps_malloc((size_t)st.st_size, MALLOC_CAP_SPIRAM);
    if (buf == NULL) return NULL;

    FILE *f = fopen(path, "rb");
    if (f == NULL) { free(buf); return NULL; }
    size_t n = fread(buf, 1, (size_t)st.st_size, f);
    fclose(f);
    if (n != (size_t)st.st_size) { free(buf); return NULL; }

    if (out_len) *out_len = n;
    ESP_LOGI(TAG, "tts cache HIT %s (%u bytes)", path, (unsigned)n);
    return buf;
}

/* Delete the oldest clip when the cache directory is over budget. */
static void tts_evict_if_full(void)
{
    DIR *d = opendir(DIR_TTS);
    if (d == NULL) return;

    int count = 0;
    time_t oldest_t = 0;
    char oldest[32] = {0};
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        count++;
        char path[64];
        /* Cache names are 12 chars ("xxxxxxxx.pcm"); .20s keeps the compiler
         * happy about d_name's theoretical 255. */
        snprintf(path, sizeof(path), DIR_TTS "/%.20s", e->d_name);
        struct stat st;
        if (stat(path, &st) == 0 &&
            (oldest[0] == '\0' || st.st_mtime < oldest_t)) {
            oldest_t = st.st_mtime;
            snprintf(oldest, sizeof(oldest), "%.20s", e->d_name);
        }
    }
    closedir(d);

    if (count >= TTS_MAX_FILES && oldest[0] != '\0') {
        char path[64];
        snprintf(path, sizeof(path), DIR_TTS "/%s", oldest);
        unlink(path);
        ESP_LOGI(TAG, "tts cache full (%d), evicted %s", count, path);
    }
}

void sd_tts_cache_put(const char *key, const uint8_t *pcm, size_t len)
{
    if (s_card == NULL || key == NULL || pcm == NULL || len < 2) return;

    tts_evict_if_full();

    char path[64];
    tts_path(path, sizeof(path), key);
    /* Write to a temp name first so a mid-write reset never leaves a corrupt
     * clip under a valid cache key. */
    char tmp[64];
    snprintf(tmp, sizeof(tmp), DIR_TTS "/wr.tmp");

    FILE *f = fopen(tmp, "wb");
    if (f == NULL) return;
    size_t n = fwrite(pcm, 1, len, f);
    fclose(f);
    if (n != len) { unlink(tmp); return; }

    unlink(path);                        /* rename() won't overwrite on FATFS */
    if (rename(tmp, path) == 0) {
        ESP_LOGI(TAG, "tts cache stored %s (%u bytes)", path, (unsigned)len);
    } else {
        unlink(tmp);
    }
}
