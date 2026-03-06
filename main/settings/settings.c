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
    strlcpy(s_settings.dongle_hostname, CONFIG_SESSY_DONGLE_HOSTNAME, sizeof(s_settings.dongle_hostname));
    strlcpy(s_settings.p1_hostname, CONFIG_SESSY_P1_HOSTNAME, sizeof(s_settings.p1_hostname));
    strlcpy(s_settings.sessy_username, CONFIG_SESSY_USERNAME, sizeof(s_settings.sessy_username));
    strlcpy(s_settings.sessy_password, CONFIG_SESSY_PASSWORD, sizeof(s_settings.sessy_password));
#ifdef CONFIG_SESSY_IDLE_AT_SOC_ZERO
    s_settings.autoload_soc_zero = true;
#else
    s_settings.autoload_soc_zero = false;
#endif
#ifdef CONFIG_SESSY_EV_AUTO_IDLE
    s_settings.ev_auto_idle = true;
#else
    s_settings.ev_auto_idle = false;
#endif
    s_settings.car_charge_threshold = CONFIG_SESSY_CAR_CHARGE_THRESHOLD;
    s_settings.car_charge_stop_delay = CONFIG_SESSY_CAR_CHARGE_STOP_DELAY;
    s_settings.screen_dim = CONFIG_SESSY_SCREEN_DIM;
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

    len = sizeof(s_settings.dongle_hostname);
    if (nvs_get_str(handle, "host", s_settings.dongle_hostname, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded Dongle hostname from NVS");
    }

    len = sizeof(s_settings.p1_hostname);
    if (nvs_get_str(handle, "p1_host", s_settings.p1_hostname, &len) == ESP_OK) {
        ESP_LOGI(TAG, "Loaded P1 hostname from NVS");
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
        ESP_LOGI(TAG, "Loaded 'Treat SOC==0%% as Sessy Idle' setting from NVS");
    }

    uint8_t ev_auto_idle = 0;
    if (nvs_get_u8(handle, "ev_auto_idle", &ev_auto_idle) == ESP_OK) {
        s_settings.ev_auto_idle = (ev_auto_idle != 0);
        ESP_LOGI(TAG, "Loaded 'EV auto-idle' setting from NVS: %d", s_settings.ev_auto_idle);
    }

    int32_t car_thresh = 0;
    if (nvs_get_i32(handle, "car_thresh", &car_thresh) == ESP_OK) {
        s_settings.car_charge_threshold = car_thresh;
        ESP_LOGI(TAG, "Loaded car charge threshold from NVS: %d W", (int)car_thresh);
    }

    int32_t stop_delay = 0;
    if (nvs_get_i32(handle, "car_stopd", &stop_delay) == ESP_OK) {
        s_settings.car_charge_stop_delay = stop_delay;
        ESP_LOGI(TAG, "Loaded car charge stop delay from NVS: %d min", (int)stop_delay);
    }

    int32_t screen_dim = 0;
    if (nvs_get_i32(handle, "screen_dim", &screen_dim) == ESP_OK) {
        s_settings.screen_dim = screen_dim;
        ESP_LOGI(TAG, "Loaded screen dim from NVS: %d%%", (int)screen_dim);
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
    ESP_LOGI(TAG, "Settings initialized (SSID: %s, Dongle: %s, P1: %s, User=%s",
             s_settings.wifi_ssid, strlen(s_settings.dongle_hostname) > 0 ? s_settings.dongle_hostname : "(mDNS)",
             strlen(s_settings.p1_hostname) > 0 ? s_settings.p1_hostname : "(mDNS)", s_settings.sessy_username);
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

esp_err_t settings_set_dongle_hostname(const char *hostname)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return ret;
    }

    strlcpy(s_settings.dongle_hostname, hostname ? hostname : "", sizeof(s_settings.dongle_hostname));
    nvs_set_str(handle, "host", s_settings.dongle_hostname);
    nvs_commit(handle);
    nvs_close(handle);
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Dongle hostname saved: %s", strlen(s_settings.dongle_hostname) > 0 ? s_settings.dongle_hostname : "(mDNS)");
    return ESP_OK;
}

esp_err_t settings_set_p1_hostname(const char *hostname)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return ret;
    }

    strlcpy(s_settings.p1_hostname, hostname ? hostname : "", sizeof(s_settings.p1_hostname));
    nvs_set_str(handle, "p1_host", s_settings.p1_hostname);
    nvs_commit(handle);
    nvs_close(handle);
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "P1 hostname saved: %s", strlen(s_settings.p1_hostname) > 0 ? s_settings.p1_hostname : "(mDNS)");
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

esp_err_t settings_set_ev_auto_idle(bool enable)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_settings.ev_auto_idle = enable;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        uint8_t value = enable ? 1 : 0;
        ret = nvs_set_u8(handle, "ev_auto_idle", value);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }

    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Settings saved: EV auto-idle = %d", enable);
    } else {
        ESP_LOGE(TAG, "Failed to save EV auto-idle: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t settings_set_car_charge_threshold(int32_t watts)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_settings.car_charge_threshold = watts;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, "car_thresh", watts);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }

    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Car charge threshold saved: %d W", (int)watts);
    } else {
        ESP_LOGE(TAG, "Failed to save car charge threshold: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t settings_set_car_charge_stop_delay(int32_t minutes)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_settings.car_charge_stop_delay = minutes;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, "car_stopd", minutes);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }

    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Car charge stop delay saved: %d min", (int)minutes);
    } else {
        ESP_LOGE(TAG, "Failed to save car charge stop delay: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t settings_set_screen_dim(int32_t percent)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    s_settings.screen_dim = percent;

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        ret = nvs_set_i32(handle, "screen_dim", percent);
        if (ret == ESP_OK) {
            ret = nvs_commit(handle);
        }
        nvs_close(handle);
    }

    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Screen dim saved: %d%%", (int)percent);
    } else {
        ESP_LOGE(TAG, "Failed to save screen dim: %s", esp_err_to_name(ret));
    }

    return ret;
}
