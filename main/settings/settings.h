#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"

#define SETTINGS_SSID_MAX_LEN        32
#define SETTINGS_PASS_MAX_LEN        64
#define SETTINGS_HOSTNAME_MAX_LEN    64
#define SETTINGS_SESSY_USER_MAX_LEN  16
#define SETTINGS_SESSY_PASS_MAX_LEN  16

typedef struct {
    char wifi_ssid[SETTINGS_SSID_MAX_LEN + 1];
    char wifi_password[SETTINGS_PASS_MAX_LEN + 1];
    char sessy_hostname[SETTINGS_HOSTNAME_MAX_LEN + 1];
    char sessy_username[SETTINGS_SESSY_USER_MAX_LEN + 1];  // ← Nieuw
    char sessy_password[SETTINGS_SESSY_PASS_MAX_LEN + 1];  // ← Nieuw
} settings_t;

esp_err_t settings_init(void);
const settings_t *settings_get(void);
esp_err_t settings_set_wifi(const char *ssid, const char *password);
esp_err_t settings_set_sessy_hostname(const char *hostname);
esp_err_t settings_set_sessy_creds(const char *username, const char *password);
esp_err_t settings_reset(void);

#endif
