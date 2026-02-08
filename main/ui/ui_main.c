#include "ui_main.h"
#include "ui_status.h"
#include "ui_strategy.h"
#include "ui_energy.h"
#include "ui_settings.h"
#include "esp_log.h"

static const char *TAG = "ui_main";

static lv_obj_t *tabview;
static lv_obj_t *status_bar_wifi;
static lv_obj_t *status_bar_sessy;

static app_shared_data_t *s_shared_data = NULL;

static void ui_refresh_timer_cb(lv_timer_t *timer)
{
    app_shared_data_t *data = (app_shared_data_t *)timer->user_data;
    if (!data) return;

    if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (data->power_status_valid) {
            ui_status_update(&data->power_status);
        }
        if (data->strategy_valid) {
            ui_strategy_update(data->active_strategy);
        }
        if (data->energy_status_valid) {
            ui_energy_update(&data->energy_status);
        }
        ui_set_connection_status(data->wifi_connected, data->sessy_reachable);
        xSemaphoreGive(data->mutex);
    }
}

void ui_init(app_shared_data_t *shared_data)
{
    s_shared_data = shared_data;

    // Apply dark theme
    lv_theme_t *theme = lv_theme_default_init(
        lv_disp_get_default(),
        lv_color_hex(0x2196F3),   // primary
        lv_color_hex(0x4CAF50),   // secondary
        true,                      // dark mode
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(lv_disp_get_default(), theme);

    // Status bar at top
    lv_obj_t *status_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(status_bar, 480, 28);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_pad_hor(status_bar, 10, 0);
    lv_obj_set_style_pad_ver(status_bar, 4, 0);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    status_bar_wifi = lv_label_create(status_bar);
    lv_label_set_text(status_bar_wifi, "WiFi: --");
    lv_obj_set_style_text_font(status_bar_wifi, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_bar_wifi, lv_color_hex(0x888888), 0);

    status_bar_sessy = lv_label_create(status_bar);
    lv_label_set_text(status_bar_sessy, "Sessy: --");
    lv_obj_set_style_text_font(status_bar_sessy, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_bar_sessy, lv_color_hex(0x888888), 0);

    // Tabview below status bar
    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_BOTTOM, 45);
    lv_obj_set_size(tabview, 480, 480 - 28);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x1E1E1E), 0);

    // Style the tab buttons
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_text_font(tab_btns, &lv_font_montserrat_12, 0);

    // Create tabs
    lv_obj_t *tab_status   = lv_tabview_add_tab(tabview, "Status");
    lv_obj_t *tab_control  = lv_tabview_add_tab(tabview, "Control");
    lv_obj_t *tab_energy   = lv_tabview_add_tab(tabview, "Energy");
    lv_obj_t *tab_settings = lv_tabview_add_tab(tabview, "Settings");

    // Build each screen's content
    ui_status_create(tab_status);
    ui_strategy_create(tab_control, shared_data);
    ui_energy_create(tab_energy);
    ui_settings_create(tab_settings, shared_data);

    // Periodic data refresh timer (every 1 second)
    lv_timer_create(ui_refresh_timer_cb, 1000, shared_data);

    ESP_LOGI(TAG, "UI initialized");
}

void ui_update_status(const sessy_status_response_t *data)
{
    ui_status_update(data);
}

void ui_update_strategy(sessy_strategy_t strategy)
{
    ui_strategy_update(strategy);
}

void ui_update_energy(const sessy_energy_response_t *data)
{
    ui_energy_update(data);
}

void ui_set_connection_status(bool wifi_ok, bool sessy_ok)
{
    if (wifi_ok) {
        lv_label_set_text(status_bar_wifi, "WiFi: OK");
        lv_obj_set_style_text_color(status_bar_wifi, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_label_set_text(status_bar_wifi, "WiFi: --");
        lv_obj_set_style_text_color(status_bar_wifi, lv_color_hex(0xF44336), 0);
    }

    if (sessy_ok) {
        lv_label_set_text(status_bar_sessy, "Sessy: OK");
        lv_obj_set_style_text_color(status_bar_sessy, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_label_set_text(status_bar_sessy, "Sessy: --");
        lv_obj_set_style_text_color(status_bar_sessy, lv_color_hex(0xF44336), 0);
    }
}
