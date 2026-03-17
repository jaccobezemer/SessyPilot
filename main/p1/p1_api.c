#include "p1_api.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "p1_api";

static char s_base_url[128] = {0};
static SemaphoreHandle_t s_mutex = NULL;
static esp_http_client_handle_t s_client = NULL;

esp_err_t p1_api_init(const char *base_url)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    return p1_api_set_url(base_url);
}

esp_err_t p1_api_set_url(const char *base_url)
{
    if (!base_url) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strlcpy(s_base_url, base_url, sizeof(s_base_url));
    // Force client recreate with new base URL
    if (s_client) {
        esp_http_client_cleanup(s_client);
        s_client = NULL;
    }
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "P1 meter URL set: %s", s_base_url);
    return ESP_OK;
}

// Ensure client exists. Must be called with s_mutex held.
static bool ensure_client(void)
{
    if (s_client) return true;
    if (strlen(s_base_url) == 0) return false;
    esp_http_client_config_t config = {
        .url = s_base_url,
        .timeout_ms = 5000,
    };
    s_client = esp_http_client_init(&config);
    return s_client != NULL;
}

// Holds s_mutex for the duration of the request to serialise access to s_client.
static char *http_get(const char *path, int *out_len)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!ensure_client()) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);
    esp_http_client_set_url(s_client, url);
    esp_http_client_set_method(s_client, HTTP_METHOD_GET);
    esp_http_client_set_header(s_client, "Accept", "application/json");

    esp_err_t err = esp_http_client_open(s_client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET %s open failed: %s", path, esp_err_to_name(err));
        // Reset client so it reconnects fresh next time
        esp_http_client_cleanup(s_client);
        s_client = NULL;
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    int content_length = esp_http_client_fetch_headers(s_client);
    int status = esp_http_client_get_status_code(s_client);
    if (status != 200) {
        ESP_LOGE(TAG, "GET %s returned %d", path, status);
        esp_http_client_close(s_client);
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    int buf_size = content_length > 0 ? content_length + 1 : 2048;
    char *buf = malloc(buf_size);
    if (!buf) {
        esp_http_client_close(s_client);
        xSemaphoreGive(s_mutex);
        return NULL;
    }

    int total_read = 0;
    int read_len;
    while (total_read < buf_size - 1) {
        read_len = esp_http_client_read(s_client, buf + total_read, buf_size - 1 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    buf[total_read] = '\0';

    // Close but do NOT cleanup: reuse the connection (keep-alive) next request
    esp_http_client_close(s_client);
    xSemaphoreGive(s_mutex);

    if (out_len) *out_len = total_read;
    return buf;
}

esp_err_t p1_api_get_details(p1_status_t *out)
{
    if (!out || strlen(s_base_url) == 0) return ESP_ERR_INVALID_STATE;

    char *body = http_get("/api/v2/p1/details", NULL);
    if (!body) return ESP_FAIL;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed for p1/details");
        return ESP_FAIL;
    }

    memset(out, 0, sizeof(*out));

    cJSON *item;
    item = cJSON_GetObjectItem(root, "power_consumed");
    if (item) out->power_consumed = (int32_t)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "power_produced");
    if (item) out->power_produced = (int32_t)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "power_total");
    if (item) out->power_total = (int32_t)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "power_consumed_l1");
    if (item) out->power_consumed_l1 = (int32_t)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "power_consumed_l2");
    if (item) out->power_consumed_l2 = (int32_t)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(root, "power_consumed_l3");
    if (item) out->power_consumed_l3 = (int32_t)cJSON_GetNumberValue(item);

    cJSON_Delete(root);
    return ESP_OK;
}
