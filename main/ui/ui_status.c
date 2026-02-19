#include "ui_status.h"
#include "esp_log.h"
#include <stdio.h>

static lv_obj_t *soc_arc;
static lv_obj_t *soc_label;
static lv_obj_t *power_label;
static lv_obj_t *state_label;
static lv_obj_t *setpoint_label;
static lv_obj_t *voltage_label;
static lv_obj_t *freq_label;
static lv_obj_t *current_label;
static lv_obj_t *ext_power_label;
static lv_obj_t *house_power_label;
static lv_obj_t *ev_charging_label;

static lv_obj_t *create_info_row(lv_obj_t *parent, const char *title, lv_obj_t **value_label)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(31), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *title_lbl = lv_label_create(cont);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_12, 0);

    *value_label = lv_label_create(cont);
    lv_label_set_text(*value_label, "--");
    lv_obj_set_style_text_color(*value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(*value_label, &lv_font_montserrat_16, 0);

    return cont;
}

void ui_status_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_gap(parent, 4, 0);

    // SoC Arc section
    lv_obj_t *arc_cont = lv_obj_create(parent);
    lv_obj_set_size(arc_cont, 200, 200);
    lv_obj_set_style_bg_opa(arc_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc_cont, 0, 0);
    lv_obj_set_style_pad_all(arc_cont, 0, 0);

    soc_arc = lv_arc_create(arc_cont);
    lv_obj_set_size(soc_arc, 190, 190);
    lv_obj_center(soc_arc);
    lv_arc_set_rotation(soc_arc, 135);
    lv_arc_set_bg_angles(soc_arc, 0, 270);
    lv_arc_set_range(soc_arc, 0, 100);
    lv_arc_set_value(soc_arc, 0);
    lv_obj_clear_flag(soc_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(soc_arc, 15, LV_PART_MAIN);
    lv_obj_set_style_arc_width(soc_arc, 15, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(soc_arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(soc_arc, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
    // Hide the knob
    lv_obj_set_style_bg_opa(soc_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(soc_arc, 0, LV_PART_KNOB);

    soc_label = lv_label_create(arc_cont);
    lv_label_set_text(soc_label, "--%");
    lv_obj_set_style_text_font(soc_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(soc_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(soc_label, LV_ALIGN_CENTER, 0, -15);

    power_label = lv_label_create(arc_cont);
    lv_label_set_text(power_label, "-- W");
    lv_obj_set_style_text_font(power_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(power_label, lv_color_hex(0x4CAF50), 0);
    lv_obj_align(power_label, LV_ALIGN_CENTER, 0, 20);

    // Info grid
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    create_info_row(grid, "Status",   &state_label);
    create_info_row(grid, "Voltage",  &voltage_label);
    create_info_row(grid, "Setpoint", &setpoint_label);
    create_info_row(grid, "Freq",     &freq_label);
    create_info_row(grid, "Current",  &current_label);
    create_info_row(grid, "Ext. Power", &ext_power_label);
    create_info_row(grid, "House",     &house_power_label);
    create_info_row(grid, "EV Charge", &ev_charging_label);
}

void ui_status_update(const sessy_status_response_t *data, int32_t house_power, bool car_charging)
{
    if (!data) return;

    int soc_pct = (int)(data->sessy.state_of_charge * 100.0f);
    if (soc_pct < 0) soc_pct = 0;
    if (soc_pct > 100) soc_pct = 100;

    lv_arc_set_value(soc_arc, soc_pct);

    // Color arc based on SoC level
    lv_color_t arc_color;
    if (soc_pct > 20) {
        arc_color = lv_color_hex(0x4CAF50); // green
    } else if (soc_pct > 10) {
        arc_color = lv_color_hex(0xFF9800); // orange
    } else {
        arc_color = lv_color_hex(0xF44336); // red
    }
    lv_obj_set_style_arc_color(soc_arc, arc_color, LV_PART_INDICATOR);

    char buf[32];

    snprintf(buf, sizeof(buf), "%d%%", soc_pct);
    lv_label_set_text(soc_label, buf);

    snprintf(buf, sizeof(buf), "%d W", (int)data->sessy.power);
    lv_label_set_text(power_label, buf);
    // Green for generating/charing (negative), red for power delivery (positive)
    if (data->sessy.power >= 0) {
        lv_obj_set_style_text_color(power_label, lv_color_hex(0xF44336), 0);
    } else {
        lv_obj_set_style_text_color(power_label, lv_color_hex(0x4CAF50), 0);
    }

    lv_label_set_text(state_label, sessy_state_to_label(data->sessy.system_state));

    snprintf(buf, sizeof(buf), "%.1f V", data->sessy.pack_voltage / 1000.0f);
    lv_label_set_text(voltage_label, buf);

    snprintf(buf, sizeof(buf), "%d W", (int)data->sessy.power_setpoint);
    lv_label_set_text(setpoint_label, buf);

    snprintf(buf, sizeof(buf), "%.2f Hz", data->sessy.frequency / 1000.0f);
    lv_label_set_text(freq_label, buf);

    snprintf(buf, sizeof(buf), "%.1f A", data->sessy.inverter_current_ma / 1000.0f);
    lv_label_set_text(current_label, buf);

    snprintf(buf, sizeof(buf), "%d W", (int)data->sessy.external_power);
    lv_label_set_text(ext_power_label, buf);

    snprintf(buf, sizeof(buf), "%d W", (int)house_power);
    lv_label_set_text(house_power_label, buf);

    if (car_charging) {
        // snprintf(buf, sizeof(buf), "Yes (%d W)", (int)house_power);
        // lv_label_set_text(ev_charging_label, buf);
        lv_label_set_text(ev_charging_label, "Yes");
        lv_obj_set_style_text_color(ev_charging_label, lv_color_hex(0xFF9800), 0);
    } else {
        lv_label_set_text(ev_charging_label, "No");
        lv_obj_set_style_text_color(ev_charging_label, lv_color_hex(0x888888), 0);
    }
}
