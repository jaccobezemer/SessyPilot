#include "ui_strategy.h"
#include "sessy_api.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui_strategy";

static app_shared_data_t *s_shared = NULL;
static lv_obj_t *current_strategy_label;
static lv_obj_t *btnmatrix;
static lv_obj_t *slider;
static lv_obj_t *slider_label;
static lv_obj_t *apply_btn;
static lv_obj_t *setpoint_section;

static sessy_strategy_t s_current_strategy = STRATEGY_NOM;

static const char *btn_map[] = {"NOM", "ROI", "API", "\n", "IDLE", "ECO", "SESSY+", ""};

static const sessy_strategy_t btn_strategy_map[] = {
    STRATEGY_NOM, STRATEGY_ROI, STRATEGY_API,
    STRATEGY_IDLE, STRATEGY_ECO, STRATEGY_SESSY_CONNECT,
};

static void btnmatrix_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(obj);
    if (id >= 6) return;

    sessy_strategy_t strategy = btn_strategy_map[id];
    ESP_LOGI(TAG, "Strategy selected: %s", sessy_strategy_to_string(strategy));

    if (!s_shared) return;

    /* Immediate visual feedback: mark button as checked */
    for (int i = 0; i < 6; i++) {
        if (i == id) {
            lv_btnmatrix_set_btn_ctrl(obj, i, LV_BTNMATRIX_CTRL_CHECKED);
        } else {
            lv_btnmatrix_clear_btn_ctrl(obj, i, LV_BTNMATRIX_CTRL_CHECKED);
        }
    }

    /* Execute strategy change directly */
    if (sessy_api_set_strategy(strategy) == ESP_OK) {
        ESP_LOGI(TAG, "Strategy changed to %s", sessy_strategy_to_string(strategy));
    } else {
        ESP_LOGW(TAG, "Failed to change strategy");
    }
    
    /* Poll status immediately */
    sessy_poll_now(s_shared);
}

static void update_slider_value_label(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d W", (int)lv_slider_get_value(slider));
    lv_label_set_text(slider_label, buf);
}

static void slider_event_cb(lv_event_t *e)
{
    update_slider_value_label();
}

static void minus_btn_cb(lv_event_t *e)
{
    int32_t val = lv_slider_get_value(slider);
    lv_slider_set_value(slider, val - 100, LV_ANIM_ON);
    update_slider_value_label();
}

static void plus_btn_cb(lv_event_t *e)
{
    int32_t val = lv_slider_get_value(slider);
    lv_slider_set_value(slider, val + 100, LV_ANIM_ON);
    update_slider_value_label();
}

static void apply_btn_reset_timer_cb(lv_timer_t *t)
{
    lv_obj_t *lbl = (lv_obj_t *)t->user_data;
    lv_label_set_text(lbl, "APPLY SETPOINT");
    lv_timer_del(t);
}

static void apply_btn_cb(lv_event_t *e)
{
    int32_t setpoint = lv_slider_get_value(slider);
    ESP_LOGI(TAG, "Apply setpoint: %d W", (int)setpoint);

    if (!s_shared) return;

    /* Execute setpoint change directly */
    if (sessy_api_set_setpoint(setpoint) == ESP_OK) {
        ESP_LOGI(TAG, "Setpoint set to %d W", (int)setpoint);
    } else {
        ESP_LOGW(TAG, "Failed to set setpoint");
    }
    
    /* Poll status immediately */
    sessy_poll_now(s_shared);

    lv_obj_t *label = lv_obj_get_child(apply_btn, 0);
    lv_label_set_text(label, "Sent!");
    lv_timer_create(apply_btn_reset_timer_cb, 1500, label);
}

void ui_strategy_create(lv_obj_t *parent, app_shared_data_t *shared_data)
{
    s_shared = shared_data;

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 10, 0);
    lv_obj_set_style_pad_gap(parent, 8, 0);

    // Current strategy display
    current_strategy_label = lv_label_create(parent);
    lv_label_set_text(current_strategy_label, "Strategy: --");
    lv_obj_set_style_text_font(current_strategy_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(current_strategy_label, lv_color_hex(0xFFFFFF), 0);

    // Strategy buttons
    btnmatrix = lv_btnmatrix_create(parent);
    lv_btnmatrix_set_map(btnmatrix, btn_map);
    lv_obj_set_size(btnmatrix, LV_PCT(100), 110);
    lv_btnmatrix_set_btn_ctrl_all(btnmatrix, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(btnmatrix, true);
    lv_obj_add_event_cb(btnmatrix, btnmatrix_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_color(btnmatrix, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_bg_color(btnmatrix, lv_color_hex(0x2196F3), LV_PART_ITEMS | LV_STATE_CHECKED);

    // Setpoint section
    setpoint_section = lv_obj_create(parent);
    lv_obj_set_size(setpoint_section, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(setpoint_section, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(setpoint_section, 0, 0);
    lv_obj_set_style_radius(setpoint_section, 8, 0);
    lv_obj_set_style_pad_all(setpoint_section, 10, 0);
    lv_obj_set_flex_flow(setpoint_section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(setpoint_section, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(setpoint_section, 6, 0);

    lv_obj_t *sp_title = lv_label_create(setpoint_section);
    lv_label_set_text(sp_title, "Power Setpoint (API mode)");
    lv_obj_set_style_text_color(sp_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(sp_title, &lv_font_montserrat_14, 0);

    // Slider row: [-100] [slider] [+100]
    lv_obj_t *slider_row = lv_obj_create(setpoint_section);
    lv_obj_set_size(slider_row, LV_PCT(100), 50);
    lv_obj_set_style_bg_opa(slider_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slider_row, 0, 0);
    lv_obj_set_style_pad_all(slider_row, 0, 0);
    lv_obj_set_flex_flow(slider_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slider_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(slider_row, 6, 0);

    lv_obj_t *minus_btn = lv_btn_create(slider_row);
    lv_obj_set_size(minus_btn, 55, 40);
    lv_obj_t *minus_lbl = lv_label_create(minus_btn);
    lv_label_set_text(minus_lbl, "-100");
    lv_obj_center(minus_lbl);
    lv_obj_add_event_cb(minus_btn, minus_btn_cb, LV_EVENT_CLICKED, NULL);

    slider = lv_slider_create(slider_row);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_height(slider, 10);
    lv_slider_set_range(slider, -2200, 2200);
    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);

    lv_obj_t *plus_btn = lv_btn_create(slider_row);
    lv_obj_set_size(plus_btn, 55, 40);
    lv_obj_t *plus_lbl = lv_label_create(plus_btn);
    lv_label_set_text(plus_lbl, "+100");
    lv_obj_center(plus_lbl);
    lv_obj_add_event_cb(plus_btn, plus_btn_cb, LV_EVENT_CLICKED, NULL);

    // Setpoint value label
    slider_label = lv_label_create(setpoint_section);
    lv_label_set_text(slider_label, "0 W");
    lv_obj_set_style_text_font(slider_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(slider_label, lv_color_hex(0xFFFFFF), 0);

    // Apply button
    apply_btn = lv_btn_create(setpoint_section);
    lv_obj_set_size(apply_btn, LV_PCT(80), 45);
    lv_obj_set_style_bg_color(apply_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_t *apply_lbl = lv_label_create(apply_btn);
    lv_label_set_text(apply_lbl, "APPLY SETPOINT");
    lv_obj_center(apply_lbl);
    lv_obj_add_event_cb(apply_btn, apply_btn_cb, LV_EVENT_CLICKED, NULL);
}

void ui_strategy_update(sessy_strategy_t strategy)
{
    s_current_strategy = strategy;

    // Update strategy label
    const char *names[] = {"NOM", "ROI", "API", "IDLE", "SESSY+", "ECO"};
    const char *name = (strategy < STRATEGY_COUNT) ? names[strategy] : "??";
    char buf[32];
    snprintf(buf, sizeof(buf), "Strategy: %s", name);
    lv_label_set_text(current_strategy_label, buf);

    // Highlight active button
    for (int i = 0; i < 6; i++) {
        if (btn_strategy_map[i] == strategy) {
            lv_btnmatrix_set_btn_ctrl(btnmatrix, i, LV_BTNMATRIX_CTRL_CHECKED);
        } else {
            lv_btnmatrix_clear_btn_ctrl(btnmatrix, i, LV_BTNMATRIX_CTRL_CHECKED);
        }
    }

    // Enable/disable setpoint section based on API mode
    bool api_mode = (strategy == STRATEGY_API);
    if (api_mode) {
        lv_obj_clear_state(slider, LV_STATE_DISABLED);
        lv_obj_clear_state(apply_btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(setpoint_section, LV_OPA_COVER, 0);
    } else {
        lv_obj_add_state(slider, LV_STATE_DISABLED);
        lv_obj_add_state(apply_btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(setpoint_section, LV_OPA_50, 0);
    }
}
