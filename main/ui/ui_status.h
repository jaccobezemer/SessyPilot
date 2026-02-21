#ifndef UI_STATUS_H
#define UI_STATUS_H

#include "lvgl.h"
#include "sessy_api.h"

void ui_status_create(lv_obj_t *parent);
void ui_status_update(const sessy_status_response_t *data, int32_t house_power, bool car_charging, int32_t solar_power);

#endif
