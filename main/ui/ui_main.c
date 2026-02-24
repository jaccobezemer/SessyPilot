#include "ui_main.h"
#include "ui_auto_load.h"
#include "ui_status.h"
#include "ui_strategy.h"
#include "ui_energy.h"
#include "ui_settings.h"
#include "ota_server.h"
#include "wifi_manager.h"
#include "settings.h"
#include "esp_log.h"

static const char *TAG = "ui_main";

static lv_obj_t *tabview;
static lv_obj_t *status_bar_wifi;
static lv_obj_t *status_bar_ip;
static lv_obj_t *status_bar_sessy;
static lv_obj_t *status_bar_p1;

/* OTA overlay widgets */
static lv_obj_t *ota_overlay = NULL;
static lv_obj_t *ota_bar     = NULL;
static lv_obj_t *ota_pct_lbl = NULL;
static lv_obj_t *ota_status_lbl = NULL;

static app_shared_data_t *s_shared_data = NULL;

static void ota_overlay_create(void)
{
    ota_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ota_overlay, 480, 480);
    lv_obj_align(ota_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(ota_overlay, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(ota_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ota_overlay, 0, 0);
    lv_obj_set_style_radius(ota_overlay, 0, 0);
    lv_obj_set_flex_flow(ota_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ota_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(ota_overlay, 20, 0);

    lv_obj_t *title = lv_label_create(ota_overlay);
    lv_label_set_text(title, "Firmware Update");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x2196F3), 0);

    ota_bar = lv_bar_create(ota_overlay);
    lv_obj_set_size(ota_bar, 360, 24);
    lv_bar_set_range(ota_bar, 0, 100);
    lv_bar_set_value(ota_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ota_bar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_color(ota_bar, lv_color_hex(0x2196F3), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ota_bar, 6, 0);
    lv_obj_set_style_radius(ota_bar, 6, LV_PART_INDICATOR);

    ota_pct_lbl = lv_label_create(ota_overlay);
    lv_label_set_text(ota_pct_lbl, "0%");
    lv_obj_set_style_text_font(ota_pct_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ota_pct_lbl, lv_color_hex(0xCCCCCC), 0);

    ota_status_lbl = lv_label_create(ota_overlay);
    lv_label_set_text(ota_status_lbl, "Uploading firmware...");
    lv_obj_set_style_text_font(ota_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ota_status_lbl, lv_color_hex(0x888888), 0);
}

static void ui_refresh_timer_cb(lv_timer_t *timer)
{
    app_shared_data_t *data = (app_shared_data_t *)timer->user_data;
    if (!data) return;

    /* Check OTA progress (lock-free atomic read) */
    int ota_pct = ota_get_progress();
    if (ota_pct >= 0) {
        if (!ota_overlay) {
            ota_overlay_create();
        }
        if (ota_pct <= 100) {
            lv_bar_set_value(ota_bar, ota_pct, LV_ANIM_ON);
            lv_label_set_text_fmt(ota_pct_lbl, "%d%%", ota_pct);
        }
        if (ota_pct > 100) {
            lv_bar_set_value(ota_bar, 100, LV_ANIM_OFF);
            lv_label_set_text(ota_pct_lbl, "100%");
            lv_label_set_text(ota_status_lbl, "Rebooting...");
            lv_obj_set_style_bg_color(ota_bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
        }
        return;  /* Skip normal UI updates during OTA */
    }

    if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (data->power_status_valid) {
            int32_t solar_power = data->power_status.phase[0].power
                                + data->power_status.phase[1].power
                                + data->power_status.phase[2].power;
            int32_t grid_power = data->p1_status_valid ? data->p1_status.power_total : 0;
            ui_status_update(&data->power_status, data->total_house_power, data->car_charging,
                             solar_power, grid_power);
        }
        if (data->strategy_valid) {
            ui_update_strategy(data->active_strategy);
        }
        if (data->energy_status_valid) {
            ui_energy_update(&data->energy_status);
        }
        ui_set_connection_status(data->wifi_connected, data->sessy_reachable, data->p1_reachable);
        bool wifi_ok = data->wifi_connected;
        xSemaphoreGive(data->mutex);

        const char *ip = wifi_manager_get_ip_str();
        if (wifi_ok && ip) {
            lv_label_set_text(status_bar_ip, ip);
            lv_obj_set_style_text_color(status_bar_ip, lv_color_hex(0xAAAAAA), 0);
        } else {
            lv_label_set_text(status_bar_ip, "");
        }
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

    status_bar_ip = lv_label_create(status_bar);
    lv_label_set_text(status_bar_ip, "");
    lv_obj_set_style_text_font(status_bar_ip, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_bar_ip, lv_color_hex(0x888888), 0);

    status_bar_sessy = lv_label_create(status_bar);
    lv_label_set_text(status_bar_sessy, "Sessy: --");
    lv_obj_set_style_text_font(status_bar_sessy, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_bar_sessy, lv_color_hex(0x888888), 0);

    status_bar_p1 = lv_label_create(status_bar);
    lv_label_set_text(status_bar_p1, "P1: --");
    lv_obj_set_style_text_font(status_bar_p1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(status_bar_p1, lv_color_hex(0x888888), 0);

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
    lv_obj_t *tab_auto_load = lv_tabview_add_tab(tabview, "Auto Laden");
    lv_obj_t *tab_status    = lv_tabview_add_tab(tabview, "Status");
    lv_obj_t *tab_control   = lv_tabview_add_tab(tabview, "Control");
    lv_obj_t *tab_energy    = lv_tabview_add_tab(tabview, "Energy");
    lv_obj_t *tab_settings  = lv_tabview_add_tab(tabview, "Settings");

    // Build each screen's content
    ui_auto_load_create(tab_auto_load, shared_data);
    ui_status_create(tab_status);
    ui_strategy_create(tab_control, shared_data);
    ui_energy_create(tab_energy);
    ui_settings_create(tab_settings, shared_data);

    // Dim overlay: style lv_layer_top() directly so touch events pass through
    lv_obj_set_style_bg_color(lv_layer_top(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);

    // Apply saved dim level
    ui_set_screen_dim(settings_get()->screen_dim);

    // Periodic data refresh timer (every 1 second)
    lv_timer_create(ui_refresh_timer_cb, 1000, shared_data);

    ESP_LOGI(TAG, "UI initialized");
}

void ui_update_status(const sessy_status_response_t *data, int32_t house_power, bool car_charging,
                      int32_t solar_power, int32_t grid_power)
{
    ui_status_update(data, house_power, car_charging, solar_power, grid_power);
}

void ui_update_strategy(sessy_strategy_t strategy)
{
    ui_auto_load_update(strategy);
    ui_strategy_update(strategy);
}

void ui_update_energy(const sessy_energy_response_t *data)
{
    ui_energy_update(data);
}

void ui_set_screen_dim(int32_t percent)
{
    if (percent < 0) percent = 0;
    if (percent > 90) percent = 90;
    lv_opa_t opa = (lv_opa_t)((percent * 255) / 100);
    lv_obj_set_style_bg_opa(lv_layer_top(), opa, 0);
}

void ui_set_connection_status(bool wifi_ok, bool sessy_ok, bool p1_ok)
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

    if (p1_ok) {
        lv_label_set_text(status_bar_p1, "P1: OK");
        lv_obj_set_style_text_color(status_bar_p1, lv_color_hex(0x4CAF50), 0);
    } else {
        lv_label_set_text(status_bar_p1, "P1: --");
        lv_obj_set_style_text_color(status_bar_p1, lv_color_hex(0xF44336), 0);
    }
}
