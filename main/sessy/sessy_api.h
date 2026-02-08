#ifndef SESSY_API_H
#define SESSY_API_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float    state_of_charge;
    int32_t  power;
    int32_t  external_power;
    int32_t  pack_voltage;
    int32_t  power_setpoint;
    char     system_state[48];
    char     system_state_details[64];
    int32_t  frequency;
    int32_t  inverter_current_ma;
    bool     strategy_overridden;
} sessy_power_status_t;

typedef struct {
    float   voltage_rms;
    float   current_rms;
    int32_t power;
} sessy_phase_t;

typedef struct {
    sessy_power_status_t sessy;
    sessy_phase_t phase[3];
} sessy_status_response_t;

typedef enum {
    STRATEGY_NOM = 0,
    STRATEGY_ROI,
    STRATEGY_API,
    STRATEGY_IDLE,
    STRATEGY_SESSY_CONNECT,
    STRATEGY_ECO,
    STRATEGY_COUNT
} sessy_strategy_t;

typedef struct {
    float import_wh;
    float export_wh;
} sessy_energy_meter_t;

typedef struct {
    sessy_energy_meter_t sessy_energy;
    sessy_energy_meter_t phase[3];
} sessy_energy_response_t;

esp_err_t sessy_api_init(const char *base_url);
esp_err_t sessy_api_set_url(const char *base_url);

esp_err_t sessy_api_get_power_status(sessy_status_response_t *out);
esp_err_t sessy_api_get_strategy(sessy_strategy_t *out);
esp_err_t sessy_api_set_strategy(sessy_strategy_t strategy);
esp_err_t sessy_api_set_setpoint(int32_t setpoint_watts);
esp_err_t sessy_api_get_energy(sessy_energy_response_t *out);

const char *sessy_strategy_to_string(sessy_strategy_t s);
sessy_strategy_t sessy_strategy_from_string(const char *str);
const char *sessy_state_to_label(const char *system_state);

#endif
