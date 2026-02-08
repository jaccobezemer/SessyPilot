#ifndef UI_STRATEGY_H
#define UI_STRATEGY_H

#include "lvgl.h"
#include "sessy_api.h"
#include "app_data.h"

void ui_strategy_create(lv_obj_t *parent, app_shared_data_t *shared_data);
void ui_strategy_update(sessy_strategy_t strategy);

#endif
