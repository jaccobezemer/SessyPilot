#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include <stdbool.h>

#define SETTINGS_SSID_MAX_LEN        32
#define SETTINGS_PASS_MAX_LEN        64
#define SETTINGS_HOSTNAME_MAX_LEN    64
#define SETTINGS_SESSY_USER_MAX_LEN  16
#define SETTINGS_SESSY_PASS_MAX_LEN  16

typedef struct {
    char wifi_ssid[SETTINGS_SSID_MAX_LEN + 1];
    char wifi_password[SETTINGS_PASS_MAX_LEN + 1];
    char dongle_hostname[SETTINGS_HOSTNAME_MAX_LEN + 1];
    char p1_hostname[SETTINGS_HOSTNAME_MAX_LEN + 1];
    char sessy_username[SETTINGS_SESSY_USER_MAX_LEN + 1];
    char sessy_password[SETTINGS_SESSY_PASS_MAX_LEN + 1];
    bool autoload_soc_zero;
    bool ev_auto_idle;               // auto-idle battery when EV charging detected
    int32_t car_charge_threshold;    // W, default 8000
    int32_t car_charge_stop_delay;   // minutes, default 2
    int32_t screen_dim;              // %, 0=no dim, 90=max dim, default 30
} settings_t;

esp_err_t settings_init(void);
const settings_t *settings_get(void);
esp_err_t settings_set_wifi(const char *ssid, const char *password);
esp_err_t settings_set_dongle_hostname(const char *hostname);
esp_err_t settings_set_p1_hostname(const char *hostname);
esp_err_t settings_set_sessy_creds(const char *username, const char *password);
esp_err_t settings_set_autoload_soc_zero(bool enable);
esp_err_t settings_set_ev_auto_idle(bool enable);
esp_err_t settings_set_car_charge_threshold(int32_t watts);
esp_err_t settings_set_car_charge_stop_delay(int32_t minutes);
esp_err_t settings_set_screen_dim(int32_t percent);
esp_err_t settings_reset(void);

#endif
