#include "sessy_api.h"
#include "settings.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "sessy_api";

static char s_base_url[128] = {0};
static SemaphoreHandle_t s_mutex = NULL;

extern const settings_t *settings_get(void);  // Declaration

static const char *strategy_strings[STRATEGY_COUNT] = {
    [STRATEGY_NOM]            = "POWER_STRATEGY_NOM",
    [STRATEGY_ROI]            = "POWER_STRATEGY_ROI",
    [STRATEGY_API]            = "POWER_STRATEGY_API",
    [STRATEGY_IDLE]           = "POWER_STRATEGY_IDLE",
    [STRATEGY_SESSY_CONNECT]  = "POWER_STRATEGY_SESSY_CONNECT",
    [STRATEGY_ECO]            = "POWER_STRATEGY_ECO",
};

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const char *input) {
    static char output[512];
    size_t in_len = strlen(input);
    size_t out_len = 0;
    
    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t val = 0;
        val = (input[i] << 16);
        if (i+1 < in_len) val |= (input[i+1] << 8);
        if (i+2 < in_len) val |= input[i+2];
        
        output[out_len++] = base64_table[(val >> 18) & 0x3F];
        output[out_len++] = base64_table[(val >> 12) & 0x3F];
        output[out_len++] = (i+1 < in_len) ? base64_table[(val >> 6) & 0x3F] : '=';
        output[out_len++] = (i+2 < in_len) ? base64_table[val & 0x3F] : '=';
    }
    output[out_len] = '\0';
    return output;
}

static const char* get_sessy_auth(void) {
    static char auth_header[256];
    
    const settings_t *settings = settings_get();        
    char userpass[64];
    snprintf(userpass, sizeof(userpass), "%s:%s", 
             settings->sessy_username, settings->sessy_password);
            
    char *b64 = base64_encode(userpass);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(auth_header, sizeof(auth_header), "Basic %s", b64);
    #pragma GCC diagnostic pop

    return auth_header;
}

esp_err_t sessy_api_init(const char *base_url)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;
    return sessy_api_set_url(base_url);
}

esp_err_t sessy_api_set_url(const char *base_url)
{
    if (!base_url) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strlcpy(s_base_url, base_url, sizeof(s_base_url));
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Base URL set: %s", s_base_url);
    return ESP_OK;
}

// HTTP GET helper - caller must free returned buffer
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
    esp_http_client_set_header(client, "Authorization", get_sessy_auth());

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

// HTTP POST helper
static esp_err_t http_post(const char *path, const char *json_body)
{
    char url[256];
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snprintf(url, sizeof(url), "%s%s", s_base_url, path);
    xSemaphoreGive(s_mutex);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Authorization", get_sessy_auth());

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "POST %s failed: %s", path, esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "POST %s returned %d", path, status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void parse_phase(cJSON *json, sessy_phase_t *phase)
{
    cJSON *item;
    item = cJSON_GetObjectItem(json, "voltage_rms");
    if (item) phase->voltage_rms = (float)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(json, "current_rms");
    if (item) phase->current_rms = (float)cJSON_GetNumberValue(item);
    item = cJSON_GetObjectItem(json, "power");
    if (item) phase->power = (int32_t)cJSON_GetNumberValue(item);
}

esp_err_t sessy_api_get_power_status(sessy_status_response_t *out)
{
    if (!out || strlen(s_base_url) == 0) return ESP_ERR_INVALID_STATE;

    char *body = http_get("/api/v1/power/status", NULL);
    if (!body) return ESP_FAIL;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed for power/status");
        return ESP_FAIL;
    }

    memset(out, 0, sizeof(*out));

    cJSON *sessy = cJSON_GetObjectItem(root, "sessy");
    if (sessy) {
        cJSON *item;
        item = cJSON_GetObjectItem(sessy, "state_of_charge");
        if (item) out->sessy.state_of_charge = (float)cJSON_GetNumberValue(item);
        item = cJSON_GetObjectItem(sessy, "power");
        if (item) out->sessy.power = (int32_t)cJSON_GetNumberValue(item);
        item = cJSON_GetObjectItem(sessy, "external_power");
        if (item) out->sessy.external_power = (int32_t)cJSON_GetNumberValue(item);
        item = cJSON_GetObjectItem(sessy, "pack_voltage");
        if (item) out->sessy.pack_voltage = (int32_t)cJSON_GetNumberValue(item);
        item = cJSON_GetObjectItem(sessy, "power_setpoint");
        if (item) out->sessy.power_setpoint = (int32_t)cJSON_GetNumberValue(item);
        item = cJSON_GetObjectItem(sessy, "system_state");
        if (item && cJSON_IsString(item)) strlcpy(out->sessy.system_state, item->valuestring, sizeof(out->sessy.system_state));
        item = cJSON_GetObjectItem(sessy, "system_state_details");
        if (item && cJSON_IsString(item)) strlcpy(out->sessy.system_state_details, item->valuestring, sizeof(out->sessy.system_state_details));
        item = cJSON_GetObjectItem(sessy, "frequency");
        if (item) out->sessy.frequency = (int32_t)cJSON_GetNumberValue(item);
        item = cJSON_GetObjectItem(sessy, "inverter_current_ma");
        if (item) out->sessy.inverter_current_ma = (int32_t)cJSON_GetNumberValue(item);
        item = cJSON_GetObjectItem(sessy, "strategy_overridden");
        if (item) out->sessy.strategy_overridden = cJSON_IsTrue(item);
    }

    const char *phase_keys[] = {"renewable_energy_phase1", "renewable_energy_phase2", "renewable_energy_phase3"};
    for (int i = 0; i < 3; i++) {
        cJSON *ph = cJSON_GetObjectItem(root, phase_keys[i]);
        if (ph) parse_phase(ph, &out->phase[i]);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t sessy_api_get_strategy(sessy_strategy_t *out)
{
    if (!out || strlen(s_base_url) == 0) return ESP_ERR_INVALID_STATE;

    char *body = http_get("/api/v1/power/active_strategy", NULL);
    if (!body) return ESP_FAIL;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return ESP_FAIL;

    cJSON *strategy = cJSON_GetObjectItem(root, "strategy");
    if (strategy && cJSON_IsString(strategy)) {
        *out = sessy_strategy_from_string(strategy->valuestring);
    } else {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t sessy_api_set_strategy(sessy_strategy_t strategy)
{
    if (strategy >= STRATEGY_COUNT || strlen(s_base_url) == 0) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "strategy", strategy_strings[strategy]);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Setting strategy: %s", json_str);
    esp_err_t ret = http_post("/api/v1/power/active_strategy", json_str);
    free(json_str);
    return ret;
}

esp_err_t sessy_api_set_setpoint(int32_t setpoint_watts)
{
    if (strlen(s_base_url) == 0) return ESP_ERR_INVALID_STATE;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "setpoint", setpoint_watts);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Setting setpoint: %s", json_str);
    esp_err_t ret = http_post("/api/v1/power/setpoint", json_str);
    free(json_str);
    return ret;
}

esp_err_t sessy_api_get_energy(sessy_energy_response_t *out)
{
    if (!out || strlen(s_base_url) == 0) return ESP_ERR_INVALID_STATE;

    char *body = http_get("/api/v1/energy/status", NULL);
    if (!body) return ESP_FAIL;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return ESP_FAIL;

    memset(out, 0, sizeof(*out));

    const char *keys[] = {"sessy_energy", "energy_phase1", "energy_phase2", "energy_phase3"};
    sessy_energy_meter_t *meters[] = {&out->sessy_energy, &out->phase[0], &out->phase[1], &out->phase[2]};

    for (int i = 0; i < 4; i++) {
        cJSON *obj = cJSON_GetObjectItem(root, keys[i]);
        if (obj) {
            cJSON *item;
            item = cJSON_GetObjectItem(obj, "import_wh");
            if (item) meters[i]->import_wh = (float)cJSON_GetNumberValue(item);
            item = cJSON_GetObjectItem(obj, "export_wh");
            if (item) meters[i]->export_wh = (float)cJSON_GetNumberValue(item);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

const char *sessy_strategy_to_string(sessy_strategy_t s)
{
    if (s < STRATEGY_COUNT) return strategy_strings[s];
    return "UNKNOWN";
}

sessy_strategy_t sessy_strategy_from_string(const char *str)
{
    if (!str) return STRATEGY_NOM;
    for (int i = 0; i < STRATEGY_COUNT; i++) {
        if (strcmp(str, strategy_strings[i]) == 0) return (sessy_strategy_t)i;
    }
    return STRATEGY_NOM;
}

const char *sessy_state_to_label(const char *system_state)
{
    if (!system_state) return "Unknown";
    if (strstr(system_state, "RUNNING_SAFE"))          return "Running";
    if (strstr(system_state, "STANDBY"))               return "Standby";
    if (strstr(system_state, "ERROR"))                 return "ERROR";
    if (strstr(system_state, "BATTERY_FULL"))          return "Full";
    if (strstr(system_state, "BATTERY_EMPTY"))         return "Empty";
    if (strstr(system_state, "INIT"))                  return "Init";
    if (strstr(system_state, "WAIT_FOR_PERIPHERALS"))  return "Waiting";
    if (strstr(system_state, "DISCONNECT"))            return "Disconn.";
    if (strstr(system_state, "RECONNECT"))             return "Reconn.";
    if (strstr(system_state, "OVERFREQUENCY"))         return "Over freq";
    if (strstr(system_state, "UNDERFREQUENCY"))        return "Under freq";
    if (strstr(system_state, "UNDERVOLTAGE"))          return "Low volt";
    if (strstr(system_state, "WAITING"))               return "Waiting";
    return "Unknown";
}
