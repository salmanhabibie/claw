#include "wifi.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

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

esp_err_t wifi_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    if (!(bits & WIFI_CONNECTED_BIT)) {
        return ESP_FAIL;
    }

    /* Some home routers run a flaky DNS resolver (we saw getaddrinfo() fail
     * for api.elevenlabs.io). Override with public DNS so name resolution is
     * reliable. The DHCP lease already set IP/gateway by now. */
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
    return ESP_OK;
}
