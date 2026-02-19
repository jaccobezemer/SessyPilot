#include "ui_auto_load.h"
#include "app_data.h"
#include "sdkconfig.h"
#include "sessy_api.h"
#include "settings.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui_auto_load";

static app_shared_data_t *s_shared = NULL;
static lv_obj_t *auto_load_btn = NULL;
static lv_obj_t *btn_label = NULL;
static bool s_is_active = false;

static void auto_load_btn_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    if (!s_shared) return;

    sessy_strategy_t new_strategy;

    if (s_is_active) {
        /* Currently IDLE, switch to NOM */
        s_is_active = false;
        new_strategy = STRATEGY_NOM;
        ESP_LOGI(TAG, "Button pressed: Switching Sessy to NOM, car charging disallowed");
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
        if (btn_label) lv_label_set_text(btn_label, "Auto niet laden");
    } else {
        /* Currently NOM, switch to IDLE */
        s_is_active = true;
        new_strategy = STRATEGY_IDLE;
        ESP_LOGI(TAG, "Button pressed: Wwitching Sessy to IDLE, car charging allowed");
        lv_obj_add_state(obj, LV_STATE_CHECKED);
        if (btn_label) lv_label_set_text(btn_label, "Auto Laden");
    }

    if (sessy_api_set_strategy(new_strategy) == ESP_OK) {
        ESP_LOGI(TAG, "Strategy changed to %s", sessy_strategy_to_string(new_strategy));
    } else {
        ESP_LOGW(TAG, "Failed to change strategy");
    }

    sessy_poll_now(s_shared);
}

void ui_auto_load_create(lv_obj_t *parent, app_shared_data_t *shared_data)
{
    s_shared = shared_data;
    s_is_active = false;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 20, 0);

    /* Big toggle button */
    auto_load_btn = lv_btn_create(parent);
    lv_obj_set_size(auto_load_btn, 300, 150);
    lv_obj_set_style_radius(auto_load_btn, 16, 0);
    lv_obj_set_style_bg_color(auto_load_btn, lv_color_hex(0xF44336), 0); /* red */
    lv_obj_set_style_bg_color(auto_load_btn, lv_color_hex(0x4CAF50), LV_STATE_CHECKED); /* green when checked */

    /* Raised shadow (visible on dark background) */
    lv_obj_set_style_shadow_width(auto_load_btn, 18, 0);
    lv_obj_set_style_shadow_ofs_x(auto_load_btn, 0, 0);
    lv_obj_set_style_shadow_ofs_y(auto_load_btn, 8, 0);
    lv_obj_set_style_shadow_color(auto_load_btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_spread(auto_load_btn, 0, 0);

    /* Pressed/checked appearance: smaller shadow + translate + inset border */
    lv_obj_set_style_shadow_width(auto_load_btn, 2, LV_STATE_CHECKED);
    lv_obj_set_style_shadow_ofs_y(auto_load_btn, 2, LV_STATE_CHECKED);
    lv_obj_set_style_translate_y(auto_load_btn, 6, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(auto_load_btn, 2, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(auto_load_btn, lv_color_hex(0x000000), LV_STATE_CHECKED);

    lv_obj_t *btn_label_temp = lv_label_create(auto_load_btn);
    btn_label = btn_label_temp;
    lv_label_set_text(btn_label, "Auto niet laden");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);

    lv_obj_add_event_cb(auto_load_btn, auto_load_btn_cb, LV_EVENT_CLICKED, NULL);
}

void ui_auto_load_update(sessy_strategy_t strategy)
{
    if (!auto_load_btn) return;

    bool active = (strategy == STRATEGY_IDLE);

    /* Also treat SOC == 0% as Sessy Idle (if user enabled this feature) */
    const settings_t *cfg = settings_get();
    if (!active && cfg->autoload_soc_zero && s_shared && s_shared->power_status_valid) {
        float soc = s_shared->power_status.sessy.state_of_charge;
        float power = s_shared->power_status.sessy.power;
        if (soc <= 0.01f && power <= 0.0f) {
            active = true;
        }
    }

    if (active) {
        if (!s_is_active) {
            s_is_active = true;
            lv_obj_add_state(auto_load_btn, LV_STATE_CHECKED);
        }
        if (btn_label) lv_label_set_text(btn_label, "Auto mag laden");
    } else {
        if (s_is_active) {
            s_is_active = false;
            lv_obj_clear_state(auto_load_btn, LV_STATE_CHECKED);
        }
        lv_obj_set_style_bg_color(auto_load_btn, lv_color_hex(0xF44336), 0);
        if (btn_label) lv_label_set_text(btn_label, "Auto mag niet laden");
    }
}
