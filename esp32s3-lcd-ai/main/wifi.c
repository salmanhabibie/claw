#include "wifi.h"

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#if CONFIG_WANDA_BLE_PROV
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "esp_system.h"
#include "board.h"
#include "chat_ui.h"
#endif

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          10

static const char *TAG = "wifi";
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num;

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retrying connection (%d/%d)", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Common post-connect tuning: disable modem power-save (a weak link otherwise
 * stalls sustained TLS reads mid audio-download) and force public DNS (some
 * home routers run a flaky resolver). */
static void wifi_post_connect(void)
{
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "WiFi power-save disabled");

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_dns_info_t main_dns = { .ip = { .type = ESP_IPADDR_TYPE_V4 } };
        esp_netif_dns_info_t backup_dns = { .ip = { .type = ESP_IPADDR_TYPE_V4 } };
        esp_netif_str_to_ip4("8.8.8.8", &main_dns.ip.u_addr.ip4);
        esp_netif_str_to_ip4("1.1.1.1", &backup_dns.ip.u_addr.ip4);
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &main_dns);
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &backup_dns);
        ESP_LOGI(TAG, "DNS set to 8.8.8.8 / 1.1.1.1");
    }
}

#if CONFIG_WANDA_BLE_PROV
/* During BLE provisioning the provisioning manager drives the Wi-Fi join, so we
 * only watch for GOT_IP here (no auto-reconnect/retry that would race it and
 * trip WIFI_FAIL_BIT before the phone even sends credentials). */
static void prov_ip_handler(void *arg, esp_event_base_t base,
                            int32_t id, void *data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void prov_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base != WIFI_PROV_EVENT) return;
    switch (id) {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "BLE provisioning started");
        break;
    case WIFI_PROV_CRED_RECV:
        ESP_LOGI(TAG, "received Wi-Fi credentials from phone");
        chat_ui_set_response("Kredensial diterima, menyambung...");
        break;
    case WIFI_PROV_CRED_FAIL:
        ESP_LOGE(TAG, "provisioning failed (wrong password / AP not found)");
        chat_ui_set_status("WiFi gagal, ulangi di HP");
        break;
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "provisioning successful");
        break;
    case WIFI_PROV_END:
        ESP_LOGI(TAG, "provisioning ended");
        wifi_prov_mgr_deinit();
        break;
    default:
        break;
    }
}

static void prov_service_name(char *out, size_t n)
{
    uint8_t mac[6] = { 0 };
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    /* The "ESP BLE Provisioning" app filters to names starting with "PROV_"
     * by default, so use that canonical Espressif prefix. */
    snprintf(out, n, "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
}
#endif /* CONFIG_WANDA_BLE_PROV */

esp_err_t wifi_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

#if CONFIG_WANDA_BLE_PROV
    /* WiFi comes from the phone (or NVS), never from menuconfig, when BLE
     * provisioning is enabled. */
    bool reprov = bsp_nvs_get_u8("reprov", 0);
    if (reprov) bsp_nvs_set_u8("reprov", 0);

    wifi_config_t stored = { 0 };
    esp_wifi_get_config(WIFI_IF_STA, &stored);
    bool have_stored = (stored.sta.ssid[0] != '\0');

    if (reprov || !have_stored) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &prov_ip_handler, NULL, NULL));

        wifi_prov_mgr_config_t pcfg = {
            .scheme = wifi_prov_scheme_ble,
            .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        };
        ESP_ERROR_CHECK(wifi_prov_mgr_init(pcfg));
        if (reprov) wifi_prov_mgr_reset_provisioning();

        char name[16];
        prov_service_name(name, sizeof(name));
        const char *pop = "wanda123";

        char msg[200];
        snprintf(msg, sizeof(msg),
                 "Buka app 'ESP BLE Provisioning', pilih %s, PoP: %s, "
                 "lalu masukkan WiFi.", name, pop);
        chat_ui_set_status("Atur WiFi lewat HP (BLE)");
        chat_ui_set_response(msg);
        ESP_LOGI(TAG, "%s", msg);

        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
            WIFI_PROV_SECURITY_1, pop, name, NULL));

        /* Wait until the phone's credentials get us online, then reboot into
         * the normal (BLE-free) path so esp-sr keeps all of the RAM. */
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                            pdFALSE, pdFALSE, portMAX_DELAY);
        chat_ui_set_status("WiFi tersambung!");
        chat_ui_set_response("Memulai ulang...");
        ESP_LOGI(TAG, "provisioned + connected; restarting");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

    /* Have stored credentials: join as a plain station (no Bluetooth). */
    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &stored));
    ESP_ERROR_CHECK(esp_wifi_start());

#else  /* compiled-in credentials (default) */
    esp_event_handler_instance_t any_id, got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            /* Accept open networks too: only enforce WPA2 when a password is set */
            .threshold.authmode =
                (sizeof(CONFIG_WIFI_PASSWORD) > 1) ? WIFI_AUTH_WPA2_PSK
                                                   : WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
#endif

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (!(bits & WIFI_CONNECTED_BIT)) {
        return ESP_FAIL;
    }

    wifi_post_connect();
    return ESP_OK;
}
