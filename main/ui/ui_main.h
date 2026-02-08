#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "sessy_api.h"
#include "app_data.h"
#include <stdbool.h>

void ui_init(app_shared_data_t *shared_data);
void ui_update_status(const sessy_status_response_t *data);
void ui_update_strategy(sessy_strategy_t strategy);
void ui_update_energy(const sessy_energy_response_t *data);
void ui_set_connection_status(bool wifi_ok, bool sessy_ok);

#endif
