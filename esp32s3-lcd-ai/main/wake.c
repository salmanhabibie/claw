#include "wake.h"

#include "sdkconfig.h"
#include "esp_log.h"

#if CONFIG_VEE_WAKE_WORD

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

static const char *TAG = "wake";

static srmodel_list_t      *s_models;
static const esp_wn_iface_t *s_wn;
static model_iface_data_t   *s_data;
static int                   s_chunk;

bool wake_init(void)
{
    /* Models live in a SPIFFS-format partition labelled "model", flashed by
     * the esp-sr build integration from the wake word selected in menuconfig. */
    s_models = esp_srmodel_init("model");
    if (s_models == NULL || s_models->num <= 0) {
        ESP_LOGW(TAG, "no models in 'model' partition (wake word disabled)");
        return false;
    }
    char *name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
    if (name == NULL) {
        ESP_LOGW(TAG, "no WakeNet model present (select one in menuconfig)");
        return false;
    }
    s_wn = esp_wn_handle_from_name(name);
    if (s_wn == NULL) {
        ESP_LOGW(TAG, "no handle for model '%s'", name);
        return false;
    }
    /* DET_MODE_95: higher threshold -> fewer false triggers (single mic). */
    s_data = s_wn->create(name, DET_MODE_95);
    if (s_data == NULL) {
        ESP_LOGW(TAG, "WakeNet create('%s') failed", name);
        return false;
    }
    s_chunk = s_wn->get_samp_chunksize(s_data);
    ESP_LOGI(TAG, "wake word '%s' ready (chunk=%d samples, rate=%d Hz)",
             name, s_chunk, s_wn->get_samp_rate(s_data));
    return true;
}

int wake_chunk_samples(void)
{
    return s_chunk;
}

bool wake_detect(const int16_t *chunk)
{
    if (s_wn == NULL || s_data == NULL || chunk == NULL) {
        return false;
    }
    return s_wn->detect(s_data, (int16_t *)chunk) == WAKENET_DETECTED;
}

#else  /* !CONFIG_VEE_WAKE_WORD : stubs so the firmware still links */

bool wake_init(void)            { return false; }
int  wake_chunk_samples(void)   { return 0; }
bool wake_detect(const int16_t *chunk) { (void)chunk; return false; }

#endif
