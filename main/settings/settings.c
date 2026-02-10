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
    strlcpy(s_settings.sessy_username, CONFIG_SESSY_USERNAME, sizeof(s_settings.sessy_username));
    strlcpy(s_settings.sessy_password, CONFIG_SESSY_PASSWORD, sizeof(s_settings.sessy_password));
#ifdef CONFIG_SESSY_IDLE_AT_SOC_ZERO
    s_settings.autoload_soc_zero = true;
#else
    s_settings.autoload_soc_zero = false;
#endif
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

    len = sizeof(s_settings.sessy_username);
    if (nvs_get_str(handle, "sessy_user", s_settings.sessy_username, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded Sessy username from NVS");
    }
    len = sizeof(s_settings.sessy_password);
    if (nvs_get_str(handle, "sessy_pass", s_settings.sessy_password, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded Sessy password from NVS");
    }

    uint8_t autoload_soc = 0;
    if (nvs_get_u8(handle, "autoload_soc", &autoload_soc) == ESP_OK) {
        s_settings.autoload_soc_zero = (autoload_soc != 0);
        ESP_LOGI(TAG, "Loaded 'Treat SOC==0% as Sessy Idle' setting from NVS");
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
    ESP_LOGI(TAG, "Settings initialized (SSID: %s, Host: %s, SessyUser=%s",
             s_settings.wifi_ssid,strlen(s_settings.sessy_hostname) > 0 ? s_settings.sessy_hostname : "(mDNS)", s_settings.sessy_username);
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

esp_err_t settings_set_sessy_creds(const char *username, const char *password)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return ret;
    }

    if (username) {
        strlcpy(s_settings.sessy_username, username, sizeof(s_settings.sessy_username));
        nvs_set_str(handle, "sessy_user", s_settings.sessy_username);
    }
    if (password) {
        strlcpy(s_settings.sessy_password, password, sizeof(s_settings.sessy_password));
        nvs_set_str(handle, "sessy_pass", s_settings.sessy_password);
    }

    nvs_commit(handle);
    nvs_close(handle);
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Sessy credentials saved");
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

esp_err_t settings_set_autoload_soc_zero(bool enable)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_settings.autoload_soc_zero = enable;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        uint8_t value = enable ? 1 : 0;
        ret = nvs_set_u8(handle, "autoload_soc", value);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }

    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Settings saved: Treat SOC==0%% as Sessy Idle = %d", enable);
    } else {
        ESP_LOGE(TAG, "Failed to save 'Treat SOC==0%% as Sessy Idle': %s", esp_err_to_name(ret));
    }

    return ret;
}
