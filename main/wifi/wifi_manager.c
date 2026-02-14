#include "wifi_manager.h"
#include "settings.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_mgr";

static wifi_mgr_callback_t s_callback = NULL;
static bool s_connected = false;
static char s_sessy_url[128] = {0};
static char s_ip_str[16] = {0};
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY          10

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_retry_count++;
        int delay_s = s_retry_count < 5 ? s_retry_count : 5;
        ESP_LOGW(TAG, "Disconnected, retry %d in %ds...", s_retry_count, delay_s);

        if (s_callback) {
            s_callback(WIFI_MGR_EVENT_DISCONNECTED, NULL);
        }

        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
        s_connected = true;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        if (s_callback) {
            s_callback(WIFI_MGR_EVENT_CONNECTED, NULL);
        }
    }
}

esp_err_t wifi_manager_init(wifi_mgr_callback_t callback)
{
    s_callback = callback;
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    const settings_t *cfg = settings_get();

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, cfg->wifi_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, cfg->wifi_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = strlen(cfg->wifi_password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to %s...", cfg->wifi_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password)
{
    esp_wifi_disconnect();

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = strlen(password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    s_retry_count = 0;
    esp_wifi_connect();

    ESP_LOGI(TAG, "Reconnecting to %s...", ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_discover_sessy(void)
{
    const settings_t *cfg = settings_get();

    // If a hostname/IP is manually configured, use it directly
    if (strlen(cfg->sessy_hostname) > 0) {
        snprintf(s_sessy_url, sizeof(s_sessy_url), "http://%s", cfg->sessy_hostname);
        ESP_LOGI(TAG, "Using configured Sessy URL: %s", s_sessy_url);
        if (s_callback) {
            s_callback(WIFI_MGR_EVENT_SESSY_FOUND, NULL);
        }
        return ESP_OK;
    }

    // mDNS discovery
    ESP_LOGI(TAG, "Starting mDNS discovery for Sessy...");
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    mdns_result_t *results = NULL;
    ret = mdns_query_ptr("_http", "_tcp", 5000, 20, &results);
    if (ret != ESP_OK || results == NULL) {
        ESP_LOGW(TAG, "mDNS query returned no results");
        mdns_query_results_free(results);
        if (s_callback) {
            s_callback(WIFI_MGR_EVENT_SESSY_NOT_FOUND, NULL);
        }
        return ESP_ERR_NOT_FOUND;
    }

    // Look for a service with hostname starting with "sessy" and device = "Sessy Dongle"
    mdns_result_t *r = results;
    bool found = false;
    while (r) {
        if (r->hostname && strncasecmp(r->hostname, "sessy", 5) == 0) {
            // Check TXT records for device = "Sessy Dongle"
            bool is_sessy_dongle = false;
            for (size_t i = 0; i < r->txt_count; i++) {
                mdns_txt_item_t *txt = &r->txt[i];
                if (txt->key && strcmp(txt->key, "device") == 0 && txt->value) {
                    if (strcmp(txt->value, "Sessy Dongle") == 0) {
                        is_sessy_dongle = true;
                        break;
                    }
                }
            }
            
            if (is_sessy_dongle && r->addr && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
                snprintf(s_sessy_url, sizeof(s_sessy_url), "http://" IPSTR,
                         IP2STR(&r->addr->addr.u_addr.ip4));
                ESP_LOGI(TAG, "Found Sessy via mDNS: %s (%s)", r->hostname, s_sessy_url);
                found = true;
                break;
            }
        }
        r = r->next;
    }

    mdns_query_results_free(results);

    if (found) {
        if (s_callback) {
            s_callback(WIFI_MGR_EVENT_SESSY_FOUND, NULL);
        }
        return ESP_OK;
    }

    ESP_LOGW(TAG, "No Sessy device found via mDNS");
    if (s_callback) {
        s_callback(WIFI_MGR_EVENT_SESSY_NOT_FOUND, NULL);
    }
    return ESP_ERR_NOT_FOUND;
}

const char *wifi_manager_get_sessy_url(void)
{
    return strlen(s_sessy_url) > 0 ? s_sessy_url : NULL;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

const char *wifi_manager_get_ip_str(void)
{
    return strlen(s_ip_str) > 0 ? s_ip_str : NULL;
}
