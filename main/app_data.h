#ifndef APP_DATA_H
#define APP_DATA_H

#include "sessy_api.h"
#include "p1_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>

typedef struct {
    SemaphoreHandle_t mutex;

    // Written by polling task, read by LVGL task
    sessy_status_response_t power_status;
    sessy_strategy_t        active_strategy;
    sessy_energy_response_t energy_status;
    bool                    power_status_valid;
    bool                    energy_status_valid;
    bool                    strategy_valid;

    // P1 meter data
    p1_status_t             p1_status;
    bool                    p1_status_valid;
    bool                    p1_reachable;

    // Car charge detection
    bool                    car_charging;
    int32_t                 total_house_power;

    // Written by WiFi manager callback, read by both tasks
    bool                    wifi_connected;
    bool                    sessy_reachable;

    // Written by UI (strategy/setpoint controls), read by polling task
    bool                    pending_strategy_change;
    sessy_strategy_t        requested_strategy;
    bool                    pending_setpoint_change;
    int32_t                 requested_setpoint;
} app_shared_data_t;

/* Poll Sessy immediately (e.g., when user triggers via UI) */
void sessy_poll_now(app_shared_data_t *data);

#endif
