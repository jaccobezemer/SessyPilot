#ifndef UI_AUTO_LOAD_H
#define UI_AUTO_LOAD_H

#include "lvgl.h"
#include "sessy_api.h"
#include "app_data.h"

void ui_auto_load_create(lv_obj_t *parent, app_shared_data_t *shared_data);
void ui_auto_load_update(sessy_strategy_t strategy);

#endif // UI_AUTO_LOAD_H
