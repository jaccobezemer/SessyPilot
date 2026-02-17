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
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "P1 meter URL set: %s", s_base_url);
    return ESP_OK;
}

static char *http_get(const char *path, int *out_len)
{
    char url[256];
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);
    xSemaphoreGive(s_mutex);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return NULL;

    esp_http_client_set_header(client, "Accept", "application/json");
    /* No auth needed for P1 meter */

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET %s open failed: %s", path, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return NULL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGE(TAG, "GET %s returned %d", path, status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int buf_size = content_length > 0 ? content_length + 1 : 2048;
    char *buf = malloc(buf_size);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return NULL;
    }

    int total_read = 0;
    int read_len;
    while (total_read < buf_size - 1) {
        read_len = esp_http_client_read(client, buf + total_read, buf_size - 1 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    buf[total_read] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

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

    cJSON_Delete(root);
    return ESP_OK;
}
