#include "ui_settings.h"
#include "ui_main.h"
#include "settings.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ui_settings";

static app_shared_data_t *s_shared = NULL;
static lv_obj_t *ta_ssid;
static lv_obj_t *ta_pass;
static lv_obj_t *ta_dongle_host;
static lv_obj_t *ta_p1_host;
static lv_obj_t *ta_sessy_user;
static lv_obj_t *ta_sessy_pass;
static lv_obj_t *sw_autoload_soc;
static lv_obj_t *ta_car_thresh;
static lv_obj_t *ta_car_stop_delay;
static lv_obj_t *slider_dim;
static lv_obj_t *lbl_dim_val;
static lv_obj_t *kb;

static void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void autoload_soc_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool state = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_set_autoload_soc_zero(state);
    ESP_LOGI(TAG, "Treat SOC==0%% as Sessy Idle toggle: %s", state ? "enabled" : "disabled");
}

static void slider_dim_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)val);
    lv_label_set_text(lbl_dim_val, buf);
    // Live preview — saves on SAVE button
    ui_set_screen_dim(val);
}

static void restart_msgbox_cb(lv_event_t *e)
{
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const char *btn_text = lv_msgbox_get_active_btn_text(msgbox);

    if (btn_text && strcmp(btn_text, "Yes") == 0) {
        ESP_LOGI(TAG, "User requested restart");
        esp_restart();
    }
    lv_msgbox_close(msgbox);
}

static void restart_btn_cb(lv_event_t *e)
{
    static const char *btns[] = {"Yes", "No", ""};
    lv_obj_t *msgbox = lv_msgbox_create(NULL, "Restart", "Restart the controller?", btns, true);
    lv_obj_center(msgbox);
    lv_obj_add_event_cb(msgbox, restart_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void save_btn_cb(lv_event_t *e)
{
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *pass = lv_textarea_get_text(ta_pass);
    const char *dongle_host = lv_textarea_get_text(ta_dongle_host);
    const char *p1_host = lv_textarea_get_text(ta_p1_host);
    const char *sessy_user = lv_textarea_get_text(ta_sessy_user);
    const char *sessy_pass = lv_textarea_get_text(ta_sessy_pass);

    ESP_LOGI(TAG, "Saving settings: SSID=%s, Dongle=%s, P1=%s, User=%s", ssid, dongle_host, p1_host, sessy_user);

    settings_set_wifi(ssid, pass);
    settings_set_dongle_hostname(dongle_host);
    settings_set_p1_hostname(p1_host);
    settings_set_sessy_creds(sessy_user, sessy_pass);

    // Save car charge settings
    const char *thresh_str = lv_textarea_get_text(ta_car_thresh);
    int32_t thresh_val = atoi(thresh_str);
    if (thresh_val >= 500 && thresh_val <= 20000) {
        settings_set_car_charge_threshold(thresh_val);
    } else {
        ESP_LOGW(TAG, "Invalid threshold value: %d (must be 500-20000)", (int)thresh_val);
    }

    const char *delay_str = lv_textarea_get_text(ta_car_stop_delay);
    int32_t delay_val = atoi(delay_str);
    if (delay_val >= 1 && delay_val <= 30) {
        settings_set_car_charge_stop_delay(delay_val);
    } else {
        ESP_LOGW(TAG, "Invalid stop delay value: %d (must be 1-30)", (int)delay_val);
    }

    int32_t dim_val = lv_slider_get_value(slider_dim);
    settings_set_screen_dim(dim_val);

    // Reconnect WiFi with new credentials
    wifi_manager_set_credentials(ssid, pass);

    // Trigger Sessy re-discovery
    wifi_manager_discover_sessy();
}

static void reset_msgbox_cb(lv_event_t *e)
{
    lv_obj_t *msgbox = lv_event_get_current_target(e);
    const char *btn_text = lv_msgbox_get_active_btn_text(msgbox);

    if (btn_text && strcmp(btn_text, "Yes") == 0) {
        settings_reset();
        const settings_t *cfg = settings_get();
        lv_textarea_set_text(ta_ssid, cfg->wifi_ssid);
        lv_textarea_set_text(ta_pass, cfg->wifi_password);
        lv_textarea_set_text(ta_dongle_host, cfg->dongle_hostname);
        lv_textarea_set_text(ta_p1_host, cfg->p1_hostname);
        lv_textarea_set_text(ta_sessy_user, cfg->sessy_username);
        lv_textarea_set_text(ta_sessy_pass, cfg->sessy_password);
        if (cfg->autoload_soc_zero) {
            lv_obj_add_state(sw_autoload_soc, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw_autoload_soc, LV_STATE_CHECKED);
        }
        char thresh_buf[8];
        snprintf(thresh_buf, sizeof(thresh_buf), "%d", (int)cfg->car_charge_threshold);
        lv_textarea_set_text(ta_car_thresh, thresh_buf);
        char delay_buf[4];
        snprintf(delay_buf, sizeof(delay_buf), "%d", (int)cfg->car_charge_stop_delay);
        lv_textarea_set_text(ta_car_stop_delay, delay_buf);
        lv_slider_set_value(slider_dim, cfg->screen_dim, LV_ANIM_OFF);
        char dim_buf[8];
        snprintf(dim_buf, sizeof(dim_buf), "%d%%", (int)cfg->screen_dim);
        lv_label_set_text(lbl_dim_val, dim_buf);
        ui_set_screen_dim(cfg->screen_dim);
        ESP_LOGI(TAG, "Settings reset to defaults");
    }

    lv_msgbox_close(msgbox);
}

static void reset_btn_cb(lv_event_t *e)
{
    static const char *btns[] = {"Yes", "No", ""};
    lv_obj_t *msgbox = lv_msgbox_create(NULL, "Reset", "Reset all settings to defaults?", btns, true);
    lv_obj_center(msgbox);
    lv_obj_add_event_cb(msgbox, reset_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void ui_settings_create(lv_obj_t *parent, app_shared_data_t *shared_data)
{
    s_shared = shared_data;
    const settings_t *cfg = settings_get();

    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 10, 0);
    lv_obj_set_style_pad_gap(parent, 12, 0);

    // ===== WiFi Credentials (side-by-side) =====
    lv_obj_t *wifi_lbl = lv_label_create(parent);
    lv_label_set_text(wifi_lbl, "WiFi Credentials:");
    lv_obj_set_style_text_color(wifi_lbl, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *wifi_row = lv_obj_create(parent);
    lv_obj_set_size(wifi_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wifi_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_row, 0, 0);
    lv_obj_set_style_pad_all(wifi_row, 0, 0);
    lv_obj_set_flex_flow(wifi_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wifi_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(wifi_row, 8, 0);

    // SSID column (left)
    lv_obj_t *ssid_col = lv_obj_create(wifi_row);
    lv_obj_set_size(ssid_col, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ssid_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ssid_col, 0, 0);
    lv_obj_set_style_pad_all(ssid_col, 0, 0);
    lv_obj_set_flex_flow(ssid_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(ssid_col, 2, 0);

    lv_obj_t *ssid_lbl = lv_label_create(ssid_col);
    lv_label_set_text(ssid_lbl, "SSID:");
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_12, 0);

    ta_ssid = lv_textarea_create(ssid_col);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_max_length(ta_ssid, SETTINGS_SSID_MAX_LEN);
    lv_textarea_set_text(ta_ssid, cfg->wifi_ssid);
    lv_obj_set_width(ta_ssid, LV_PCT(100));
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

    // Password column (right)
    lv_obj_t *pass_col = lv_obj_create(wifi_row);
    lv_obj_set_size(pass_col, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(pass_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pass_col, 0, 0);
    lv_obj_set_style_pad_all(pass_col, 0, 0);
    lv_obj_set_flex_flow(pass_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(pass_col, 2, 0);

    lv_obj_t *pass_lbl = lv_label_create(pass_col);
    lv_label_set_text(pass_lbl, "Password:");
    lv_obj_set_style_text_color(pass_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(pass_lbl, &lv_font_montserrat_12, 0);

    ta_pass = lv_textarea_create(pass_col);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_max_length(ta_pass, SETTINGS_PASS_MAX_LEN);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_text(ta_pass, cfg->wifi_password);
    lv_obj_set_width(ta_pass, LV_PCT(100));
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);

    // ===== Sessy Devices (side-by-side, blank=mDNS) =====
    lv_obj_t *devices_lbl = lv_label_create(parent);
    lv_label_set_text(devices_lbl, "Sessy Devices (blank=mDNS):");
    lv_obj_set_style_text_color(devices_lbl, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(devices_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *host_row = lv_obj_create(parent);
    lv_obj_set_size(host_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(host_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(host_row, 0, 0);
    lv_obj_set_style_pad_all(host_row, 0, 0);
    lv_obj_set_flex_flow(host_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(host_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(host_row, 8, 0);

    // Dongle column (left)
    lv_obj_t *dongle_col = lv_obj_create(host_row);
    lv_obj_set_size(dongle_col, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dongle_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dongle_col, 0, 0);
    lv_obj_set_style_pad_all(dongle_col, 0, 0);
    lv_obj_set_flex_flow(dongle_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(dongle_col, 2, 0);

    lv_obj_t *dongle_lbl = lv_label_create(dongle_col);
    lv_label_set_text(dongle_lbl, "Dongle:");
    lv_obj_set_style_text_color(dongle_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(dongle_lbl, &lv_font_montserrat_12, 0);

    ta_dongle_host = lv_textarea_create(dongle_col);
    lv_textarea_set_one_line(ta_dongle_host, true);
    lv_textarea_set_max_length(ta_dongle_host, SETTINGS_HOSTNAME_MAX_LEN);
    lv_textarea_set_text(ta_dongle_host, cfg->dongle_hostname);
    lv_obj_set_width(ta_dongle_host, LV_PCT(100));
    lv_obj_add_event_cb(ta_dongle_host, ta_event_cb, LV_EVENT_ALL, NULL);

    // P1 Meter column (right)
    lv_obj_t *p1_col = lv_obj_create(host_row);
    lv_obj_set_size(p1_col, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(p1_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(p1_col, 0, 0);
    lv_obj_set_style_pad_all(p1_col, 0, 0);
    lv_obj_set_flex_flow(p1_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(p1_col, 2, 0);

    lv_obj_t *p1_lbl = lv_label_create(p1_col);
    lv_label_set_text(p1_lbl, "P1 Meter:");
    lv_obj_set_style_text_color(p1_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(p1_lbl, &lv_font_montserrat_12, 0);

    ta_p1_host = lv_textarea_create(p1_col);
    lv_textarea_set_one_line(ta_p1_host, true);
    lv_textarea_set_max_length(ta_p1_host, SETTINGS_HOSTNAME_MAX_LEN);
    lv_textarea_set_text(ta_p1_host, cfg->p1_hostname);
    lv_obj_set_width(ta_p1_host, LV_PCT(100));
    lv_obj_add_event_cb(ta_p1_host, ta_event_cb, LV_EVENT_ALL, NULL);

    // ===== Sessy API Credentials (side-by-side) =====
    lv_obj_t *sessy_lbl = lv_label_create(parent);
    lv_label_set_text(sessy_lbl, "Sessy API Credentials (zie sticker):");
    lv_obj_set_style_text_color(sessy_lbl, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(sessy_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *sessy_row = lv_obj_create(parent);
    lv_obj_set_size(sessy_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(sessy_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sessy_row, 0, 0);
    lv_obj_set_style_pad_all(sessy_row, 0, 0);
    lv_obj_set_flex_flow(sessy_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sessy_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(sessy_row, 8, 0);

    // Username column (left)
    lv_obj_t *user_col = lv_obj_create(sessy_row);
    lv_obj_set_size(user_col, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(user_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(user_col, 0, 0);
    lv_obj_set_style_pad_all(user_col, 0, 0);
    lv_obj_set_flex_flow(user_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(user_col, 2, 0);

    lv_obj_t *user_lbl = lv_label_create(user_col);
    lv_label_set_text(user_lbl, "Username:");
    lv_obj_set_style_text_color(user_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_12, 0);

    ta_sessy_user = lv_textarea_create(user_col);
    lv_textarea_set_one_line(ta_sessy_user, true);
    lv_textarea_set_max_length(ta_sessy_user, SETTINGS_SESSY_USER_MAX_LEN);
    lv_textarea_set_text(ta_sessy_user, cfg->sessy_username);
    lv_obj_set_width(ta_sessy_user, LV_PCT(100));
    lv_obj_add_event_cb(ta_sessy_user, ta_event_cb, LV_EVENT_ALL, NULL);

    // Password column (right)
    lv_obj_t *sessy_pass_col = lv_obj_create(sessy_row);
    lv_obj_set_size(sessy_pass_col, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(sessy_pass_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sessy_pass_col, 0, 0);
    lv_obj_set_style_pad_all(sessy_pass_col, 0, 0);
    lv_obj_set_flex_flow(sessy_pass_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(sessy_pass_col, 2, 0);

    lv_obj_t *sessy_pass_lbl = lv_label_create(sessy_pass_col);
    lv_label_set_text(sessy_pass_lbl, "Password:");
    lv_obj_set_style_text_color(sessy_pass_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(sessy_pass_lbl, &lv_font_montserrat_12, 0);

    ta_sessy_pass = lv_textarea_create(sessy_pass_col);
    lv_textarea_set_one_line(ta_sessy_pass, true);
    lv_textarea_set_max_length(ta_sessy_pass, SETTINGS_SESSY_PASS_MAX_LEN);
    lv_textarea_set_password_mode(ta_sessy_pass, true);
    lv_textarea_set_text(ta_sessy_pass, cfg->sessy_password);
    lv_obj_set_width(ta_sessy_pass, LV_PCT(100));
    lv_obj_add_event_cb(ta_sessy_pass, ta_event_cb, LV_EVENT_ALL, NULL);

    // ===== Features =====
    lv_obj_t *features_lbl = lv_label_create(parent);
    lv_label_set_text(features_lbl, "Features:");
    lv_obj_set_style_text_color(features_lbl, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(features_lbl, &lv_font_montserrat_14, 0);

    lv_obj_t *feature_row = lv_obj_create(parent);
    lv_obj_set_size(feature_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(feature_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(feature_row, 0, 0);
    lv_obj_set_style_pad_all(feature_row, 0, 0);
    lv_obj_set_style_pad_right(feature_row, 5, 0);
    lv_obj_set_flex_flow(feature_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(feature_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(feature_row, 8, 0);

    lv_obj_t *soc_text_lbl = lv_label_create(feature_row);
    lv_label_set_text(soc_text_lbl, "Treat SOC=0% as Sessy Idle:");
    lv_obj_set_style_text_color(soc_text_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(soc_text_lbl, &lv_font_montserrat_14, 0);

    sw_autoload_soc = lv_switch_create(feature_row);
    lv_obj_set_size(sw_autoload_soc, 40, 24);
    if (cfg->autoload_soc_zero) {
        lv_obj_add_state(sw_autoload_soc, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_autoload_soc, autoload_soc_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Push restart button to right edge */
    lv_obj_t *feature_spacer = lv_obj_create(feature_row);
    lv_obj_set_height(feature_spacer, 1);
    lv_obj_set_style_bg_opa(feature_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(feature_spacer, 0, 0);
    lv_obj_set_style_pad_all(feature_spacer, 0, 0);
    lv_obj_set_flex_grow(feature_spacer, 1);

    lv_obj_t *restart_btn = lv_btn_create(feature_row);
    lv_obj_set_size(restart_btn, 80, 24);
    lv_obj_set_style_bg_color(restart_btn, lv_color_hex(0xFF9800), 0);
    lv_obj_t *restart_lbl = lv_label_create(restart_btn);
    lv_label_set_text(restart_lbl, "Restart");
    lv_obj_set_style_text_font(restart_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(restart_lbl);
    lv_obj_add_event_cb(restart_btn, restart_btn_cb, LV_EVENT_CLICKED, NULL);

    // ===== EV detect threshold row =====
    lv_obj_t *thresh_row = lv_obj_create(parent);
    lv_obj_set_size(thresh_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(thresh_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(thresh_row, 0, 0);
    lv_obj_set_style_pad_all(thresh_row, 0, 0);
    lv_obj_set_flex_flow(thresh_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(thresh_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(thresh_row, 8, 0);

    lv_obj_t *thresh_lbl = lv_label_create(thresh_row);
    lv_label_set_text(thresh_lbl, "EV detect threshold:");
    lv_obj_set_style_text_color(thresh_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(thresh_lbl, &lv_font_montserrat_14, 0);

    ta_car_thresh = lv_textarea_create(thresh_row);
    lv_textarea_set_one_line(ta_car_thresh, true);
    lv_textarea_set_max_length(ta_car_thresh, 5);
    lv_textarea_set_accepted_chars(ta_car_thresh, "0123456789");
    char thresh_init[8];
    snprintf(thresh_init, sizeof(thresh_init), "%d", (int)cfg->car_charge_threshold);
    lv_textarea_set_text(ta_car_thresh, thresh_init);
    lv_obj_set_width(ta_car_thresh, 70);
    lv_obj_add_event_cb(ta_car_thresh, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *thresh_unit = lv_label_create(thresh_row);
    lv_label_set_text(thresh_unit, "W");
    lv_obj_set_style_text_color(thresh_unit, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(thresh_unit, &lv_font_montserrat_14, 0);

    // ===== EV charge stop delay row =====
    lv_obj_t *delay_row = lv_obj_create(parent);
    lv_obj_set_size(delay_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(delay_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(delay_row, 0, 0);
    lv_obj_set_style_pad_all(delay_row, 0, 0);
    lv_obj_set_flex_flow(delay_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(delay_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(delay_row, 8, 0);

    lv_obj_t *delay_lbl = lv_label_create(delay_row);
    lv_label_set_text(delay_lbl, "EV charge stop delay:");
    lv_obj_set_style_text_color(delay_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(delay_lbl, &lv_font_montserrat_14, 0);

    ta_car_stop_delay = lv_textarea_create(delay_row);
    lv_textarea_set_one_line(ta_car_stop_delay, true);
    lv_textarea_set_max_length(ta_car_stop_delay, 2);
    lv_textarea_set_accepted_chars(ta_car_stop_delay, "0123456789");
    char delay_init[4];
    snprintf(delay_init, sizeof(delay_init), "%d", (int)cfg->car_charge_stop_delay);
    lv_textarea_set_text(ta_car_stop_delay, delay_init);
    lv_obj_set_width(ta_car_stop_delay, 50);
    lv_obj_add_event_cb(ta_car_stop_delay, ta_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *delay_unit = lv_label_create(delay_row);
    lv_label_set_text(delay_unit, "min");
    lv_obj_set_style_text_color(delay_unit, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(delay_unit, &lv_font_montserrat_14, 0);

    // ===== Screen Dim row =====
    lv_obj_t *dim_row = lv_obj_create(parent);
    lv_obj_set_size(dim_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dim_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dim_row, 0, 0);
    lv_obj_set_style_pad_all(dim_row, 4, 0);
    lv_obj_set_flex_flow(dim_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dim_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(dim_row, 8, 0);

    lv_obj_t *dim_lbl = lv_label_create(dim_row);
    lv_label_set_text(dim_lbl, "Screen dim");
    lv_obj_set_style_text_color(dim_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(dim_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(dim_lbl, 0);

    slider_dim = lv_slider_create(dim_row);
    lv_slider_set_range(slider_dim, 0, 90);
    lv_slider_set_value(slider_dim, cfg->screen_dim, LV_ANIM_OFF);
    lv_obj_set_width(slider_dim, 160);
    lv_obj_set_flex_grow(slider_dim, 0);
    lv_obj_add_event_cb(slider_dim, slider_dim_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lbl_dim_val = lv_label_create(dim_row);
    char dim_init[8];
    snprintf(dim_init, sizeof(dim_init), "%d%%", (int)cfg->screen_dim);
    lv_label_set_text(lbl_dim_val, dim_init);
    lv_obj_set_style_text_color(lbl_dim_val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_dim_val, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_dim_val, 40);

    // ===== Spacer (grows to push buttons up) =====
    lv_obj_t *spacer = lv_obj_create(parent);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    // ===== Buttons row =====
    lv_obj_t *btn_row = lv_obj_create(parent);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(btn_row, 10, 0);

    lv_obj_t *save_btn = lv_btn_create(btn_row);
    lv_obj_set_size(save_btn, LV_PCT(48), 40);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x4CAF50), 0);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "SAVE");
    lv_obj_center(save_lbl);
    lv_obj_add_event_cb(save_btn, save_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reset_btn = lv_btn_create(btn_row);
    lv_obj_set_size(reset_btn, LV_PCT(48), 40);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0xF44336), 0);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "RESET");
    lv_obj_center(reset_lbl);
    lv_obj_add_event_cb(reset_btn, reset_btn_cb, LV_EVENT_CLICKED, NULL);

    // On-screen keyboard (hidden by default)
    kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, LV_PCT(100), 180);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}


