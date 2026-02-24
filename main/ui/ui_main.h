#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "sessy_api.h"
#include "app_data.h"
#include <stdbool.h>

void ui_init(app_shared_data_t *shared_data);
void ui_update_status(const sessy_status_response_t *data, int32_t house_power, bool car_charging,
                      int32_t solar_power, int32_t grid_power);
void ui_update_strategy(sessy_strategy_t strategy);
void ui_update_energy(const sessy_energy_response_t *data);
void ui_set_connection_status(bool wifi_ok, bool sessy_ok, bool p1_ok);
void ui_set_screen_dim(int32_t percent);

#endif
