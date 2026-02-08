#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "sessy_cfg";

static settings_t s_settings;
static SemaphoreHandle_t s_mutex;
static bool s_initialized = false;

static void load_defaults(void)
{
    strlcpy(s_settings.wifi_ssid, CONFIG_SESSY_WIFI_SSID, sizeof(s_settings.wifi_ssid));
    strlcpy(s_settings.wifi_password, CONFIG_SESSY_WIFI_PASSWORD, sizeof(s_settings.wifi_password));
    strlcpy(s_settings.sessy_hostname, CONFIG_SESSY_HOSTNAME, sizeof(s_settings.sessy_hostname));
}

static void load_from_nvs(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGI(TAG, "No NVS data found, using Kconfig defaults");
        return;
    }

    size_t len;

    len = sizeof(s_settings.wifi_ssid);
    if (nvs_get_str(handle, "ssid", s_settings.wifi_ssid, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded WiFi SSID from NVS");
    }

    len = sizeof(s_settings.wifi_password);
    if (nvs_get_str(handle, "pass", s_settings.wifi_password, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded WiFi password from NVS");
    }

    len = sizeof(s_settings.sessy_hostname);
    if (nvs_get_str(handle, "host", s_settings.sessy_hostname, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded Sessy hostname from NVS");
    }

    nvs_close(handle);
}

esp_err_t settings_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    load_defaults();
    load_from_nvs();

    s_initialized = true;
    ESP_LOGI(TAG, "Settings initialized (SSID: %s, Host: %s)",
             s_settings.wifi_ssid,
             strlen(s_settings.sessy_hostname) > 0 ? s_settings.sessy_hostname : "(mDNS)");
    return ESP_OK;
}

const settings_t *settings_get(void)
{
    return &s_settings;
}

esp_err_t settings_set_wifi(const char *ssid, const char *password)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return ret;
    }

    if (ssid) {
        strlcpy(s_settings.wifi_ssid, ssid, sizeof(s_settings.wifi_ssid));
        nvs_set_str(handle, "ssid", s_settings.wifi_ssid);
    }
    if (password) {
        strlcpy(s_settings.wifi_password, password, sizeof(s_settings.wifi_password));
        nvs_set_str(handle, "pass", s_settings.wifi_password);
    }

    nvs_commit(handle);
    nvs_close(handle);
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "WiFi settings saved");
    return ESP_OK;
}

esp_err_t settings_set_sessy_hostname(const char *hostname)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return ret;
    }

    strlcpy(s_settings.sessy_hostname, hostname ? hostname : "", sizeof(s_settings.sessy_hostname));
    nvs_set_str(handle, "host", s_settings.sessy_hostname);
    nvs_commit(handle);
    nvs_close(handle);
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Sessy hostname saved: %s", strlen(s_settings.sessy_hostname) > 0 ? s_settings.sessy_hostname : "(mDNS)");
    return ESP_OK;
}

esp_err_t settings_reset(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    load_defaults();
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Settings reset to defaults");
    return ESP_OK;
}
