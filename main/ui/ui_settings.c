#include "ui_settings.h"
#include "settings.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_settings";

static app_shared_data_t *s_shared = NULL;
static lv_obj_t *ta_ssid;
static lv_obj_t *ta_pass;
static lv_obj_t *ta_host;
static lv_obj_t *ta_sessy_user;
static lv_obj_t *ta_sessy_pass;
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

static void save_btn_cb(lv_event_t *e)
{
    const char *ssid = lv_textarea_get_text(ta_ssid);
    const char *pass = lv_textarea_get_text(ta_pass);
    const char *host = lv_textarea_get_text(ta_host);
    const char *sessy_user = lv_textarea_get_text(ta_sessy_user);
    const char *sessy_pass = lv_textarea_get_text(ta_sessy_pass);

    ESP_LOGI(TAG, "Saving settings: SSID=%s, Host=%s, Sessy-User=%s", ssid, host, sessy_user);

    settings_set_wifi(ssid, pass);
    settings_set_sessy_hostname(host);
    settings_set_sessy_creds(sessy_user, sessy_pass);

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
        lv_textarea_set_text(ta_host, cfg->sessy_hostname);
        lv_textarea_set_text(ta_sessy_user, cfg->sessy_username);
        lv_textarea_set_text(ta_sessy_pass, cfg->sessy_password);        
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
    lv_obj_set_style_pad_gap(parent, 4, 0);

    // WiFi SSID
    lv_obj_t *ssid_lbl = lv_label_create(parent);
    lv_label_set_text(ssid_lbl, "WiFi SSID:");
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_14, 0);

    ta_ssid = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta_ssid, true);
    lv_textarea_set_max_length(ta_ssid, SETTINGS_SSID_MAX_LEN);
    lv_textarea_set_text(ta_ssid, cfg->wifi_ssid);
    lv_obj_set_width(ta_ssid, LV_PCT(100));
    lv_obj_add_event_cb(ta_ssid, ta_event_cb, LV_EVENT_ALL, NULL);

    // WiFi Password
    lv_obj_t *pass_lbl = lv_label_create(parent);
    lv_label_set_text(pass_lbl, "WiFi Password:");
    lv_obj_set_style_text_color(pass_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(pass_lbl, &lv_font_montserrat_14, 0);

    ta_pass = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_max_length(ta_pass, SETTINGS_PASS_MAX_LEN);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_text(ta_pass, cfg->wifi_password);
    lv_obj_set_width(ta_pass, LV_PCT(100));
    lv_obj_add_event_cb(ta_pass, ta_event_cb, LV_EVENT_ALL, NULL);

    // Sessy Host
    lv_obj_t *host_lbl = lv_label_create(parent);
    lv_label_set_text(host_lbl, "Sessy Host (blank=mDNS):");
    lv_obj_set_style_text_color(host_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_14, 0);

    ta_host = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta_host, true);
    lv_textarea_set_max_length(ta_host, SETTINGS_HOSTNAME_MAX_LEN);
    lv_textarea_set_text(ta_host, cfg->sessy_hostname);
    lv_obj_set_width(ta_host, LV_PCT(100));
    lv_obj_add_event_cb(ta_host, ta_event_cb, LV_EVENT_ALL, NULL);

   // API Username  ← NIEUW
    lv_obj_t *user_lbl = lv_label_create(parent);
    lv_label_set_text(user_lbl, "Sessy Username (zie sticker):");
    lv_obj_set_style_text_color(user_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(user_lbl, &lv_font_montserrat_14, 0);

    ta_sessy_user = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta_sessy_user, true);
    lv_textarea_set_max_length(ta_sessy_user, SETTINGS_SESSY_USER_MAX_LEN);
    lv_textarea_set_text(ta_sessy_user, cfg->sessy_username);
    lv_obj_set_width(ta_sessy_user, LV_PCT(100));
    lv_obj_add_event_cb(ta_sessy_user, ta_event_cb, LV_EVENT_ALL, NULL);

    // API Password  ← NIEUW
    lv_obj_t *sessy_pass_lbl = lv_label_create(parent);
    lv_label_set_text(sessy_pass_lbl, "Sessy Password (zie sticker):");
    lv_obj_set_style_text_color(sessy_pass_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(sessy_pass_lbl, &lv_font_montserrat_14, 0);

    ta_sessy_pass = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta_sessy_pass, true);
    lv_textarea_set_max_length(ta_sessy_pass, SETTINGS_SESSY_PASS_MAX_LEN);
    lv_textarea_set_password_mode(ta_sessy_pass, true);
    lv_textarea_set_text(ta_sessy_pass, cfg->sessy_password);
    lv_obj_set_width(ta_sessy_pass, LV_PCT(100));
    lv_obj_add_event_cb(ta_sessy_pass, ta_event_cb, LV_EVENT_ALL, NULL);

    // Buttons row
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
