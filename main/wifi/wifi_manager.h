#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    WIFI_MGR_EVENT_CONNECTED,
    WIFI_MGR_EVENT_DISCONNECTED,
    WIFI_MGR_EVENT_SESSY_FOUND,
    WIFI_MGR_EVENT_SESSY_NOT_FOUND,
    WIFI_MGR_EVENT_P1_FOUND,
    WIFI_MGR_EVENT_P1_NOT_FOUND,
    WIFI_MGR_EVENT_NO_CREDENTIALS,  /* ssid is empty at startup */
    WIFI_MGR_EVENT_CONNECT_FAILED,  /* max retries reached */
} wifi_mgr_event_t;

typedef void (*wifi_mgr_callback_t)(wifi_mgr_event_t event, void *arg);

esp_err_t wifi_manager_init(wifi_mgr_callback_t callback);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_start_ap(void);
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_discover_sessy(void);
const char *wifi_manager_get_sessy_url(void);
const char *wifi_manager_get_p1_url(void);
bool wifi_manager_is_connected(void);
const char *wifi_manager_get_ip_str(void);

#endif
