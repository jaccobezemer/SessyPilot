#ifndef UI_ENERGY_H
#define UI_ENERGY_H

#include "lvgl.h"
#include "sessy_api.h"

void ui_energy_create(lv_obj_t *parent);
void ui_energy_update(const sessy_energy_response_t *data);

#endif
