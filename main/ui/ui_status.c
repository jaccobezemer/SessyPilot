#include "ui_status.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include <stdio.h>

// Battery arc
static lv_obj_t *soc_arc;
static lv_obj_t *soc_label;
static lv_obj_t *power_label;

// Solar arc
static lv_obj_t *solar_arc;
static lv_obj_t *solar_label;

// Grid meter (needle: left=export, right=import)
static lv_obj_t            *grid_meter;
static lv_meter_indicator_t *grid_needle;
static lv_obj_t            *grid_value_label;

// House arc
static lv_obj_t *house_arc;
static lv_obj_t *house_label;

// Info row
static lv_obj_t *state_label;
static lv_obj_t *setpoint_label;
static lv_obj_t *ext_power_label;
static lv_obj_t *ev_charging_label;
// static lv_obj_t *voltage_label;   // remarked
// static lv_obj_t *freq_label;      // remarked
// static lv_obj_t *current_label;   // remarked

#define SOLAR_MAX_W CONFIG_SESSY_SOLAR_MAX_W
#define HOUSE_MAX_W 17250   // 3 × 25A × 230V
#define GRID_MAX_W  5000

// 2×2 grid: each cell is half the screen wide, QUAD_H tall
// Screen tab content: 407px. Info row ~42px. Gaps/padding ~8px. Left for arcs: ~357px → 178 per row.
#define QUAD_H   175
#define ARC_SIZE 160
#define ARC_PW   14

// Transparent quadrant container — size is set by the LVGL grid layout (STRETCH)
static lv_obj_t *create_quad(lv_obj_t *parent)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    return cont;
}

// Standard arc: ARC_SIZE × ARC_SIZE, centered in its parent, 270° sweep
static lv_obj_t *create_arc(lv_obj_t *cont, lv_color_t color, int range_max)
{
    lv_obj_t *arc = lv_arc_create(cont);
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, range_max);
    lv_arc_set_value(arc, 0);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, ARC_PW, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_PW, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    return arc;
}

// Info column: 25% wide, small title + value label
static lv_obj_t *create_info_col(lv_obj_t *parent, const char *title, lv_obj_t **value_label)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(25), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 4, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_lbl = lv_label_create(cont);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_12, 0);

    *value_label = lv_label_create(cont);
    lv_label_set_text(*value_label, "--");
    lv_obj_set_style_text_color(*value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(*value_label, &lv_font_montserrat_14, 0);

    return cont;
}

void ui_status_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 2, 0);
    lv_obj_set_style_pad_gap(parent, 2, 0);

    // ── 2×2 arc grid (LVGL grid layout) ──────────────────────────────────────
    // Row 0: Solar (left)  | Battery (right)
    // Row 1: House  (left) | Grid    (right)
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {QUAD_H, QUAD_H, LV_GRID_TEMPLATE_LAST};

    lv_obj_t *arcs_grid = lv_obj_create(parent);
    lv_obj_set_size(arcs_grid, LV_PCT(100), 2 * QUAD_H);
    lv_obj_set_style_bg_opa(arcs_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arcs_grid, 0, 0);
    lv_obj_set_style_pad_all(arcs_grid, 0, 0);
    lv_obj_set_style_pad_row(arcs_grid, 0, 0);
    lv_obj_set_style_pad_column(arcs_grid, 0, 0);
    lv_obj_clear_flag(arcs_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_grid_dsc_array(arcs_grid, col_dsc, row_dsc);

    // ── Top-left: Solar ──────────────────────────────────────────────────────
    lv_obj_t *solar_cont = create_quad(arcs_grid);
    lv_obj_set_grid_cell(solar_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    solar_arc = create_arc(solar_cont, lv_color_hex(0xFFD600), SOLAR_MAX_W);

    solar_label = lv_label_create(solar_cont);
    lv_label_set_text(solar_label, "-- W");
    lv_obj_set_style_text_font(solar_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(solar_label, lv_color_hex(0xFFD600), 0);
    lv_obj_align(solar_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *solar_title = lv_label_create(solar_cont);
    lv_label_set_text(solar_title, "Solar");
    lv_obj_set_style_text_font(solar_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(solar_title, lv_color_hex(0x888888), 0);
    lv_obj_align(solar_title, LV_ALIGN_CENTER, 0, 48);

    // ── Top-right: Battery ───────────────────────────────────────────────────
    lv_obj_t *soc_cont = create_quad(arcs_grid);
    lv_obj_set_grid_cell(soc_cont, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    soc_arc = create_arc(soc_cont, lv_color_hex(0x4CAF50), 100);

    soc_label = lv_label_create(soc_cont);
    lv_label_set_text(soc_label, "--%");
    lv_obj_set_style_text_font(soc_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(soc_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(soc_label, LV_ALIGN_CENTER, 0, -12);

    power_label = lv_label_create(soc_cont);
    lv_label_set_text(power_label, "-- W");
    lv_obj_set_style_text_font(power_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(power_label, lv_color_hex(0x4CAF50), 0);
    lv_obj_align(power_label, LV_ALIGN_CENTER, 0, 12);

    lv_obj_t *bat_title = lv_label_create(soc_cont);
    lv_label_set_text(bat_title, "Battery");
    lv_obj_set_style_text_font(bat_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bat_title, lv_color_hex(0x888888), 0);
    lv_obj_align(bat_title, LV_ALIGN_CENTER, 0, 48);

    // ── Bottom-left: House consumption ───────────────────────────────────────
    lv_obj_t *house_cont = create_quad(arcs_grid);
    lv_obj_set_grid_cell(house_cont, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    house_arc = create_arc(house_cont, lv_color_hex(0x4CAF50), HOUSE_MAX_W);

    house_label = lv_label_create(house_cont);
    lv_label_set_text(house_label, "-- W");
    lv_obj_set_style_text_font(house_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(house_label, lv_color_hex(0x4CAF50), 0);
    lv_obj_align(house_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *house_title = lv_label_create(house_cont);
    lv_label_set_text(house_title, "House");
    lv_obj_set_style_text_font(house_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(house_title, lv_color_hex(0x888888), 0);
    lv_obj_align(house_title, LV_ALIGN_CENTER, 0, 48);

    // ── Bottom-right: Grid import / export ───────────────────────────────────
    lv_obj_t *grid_cont = create_quad(arcs_grid);
    lv_obj_set_grid_cell(grid_cont, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

    grid_meter = lv_meter_create(grid_cont);
    lv_obj_set_size(grid_meter, ARC_SIZE, ARC_SIZE);
    lv_obj_center(grid_meter);
    lv_obj_set_style_bg_opa(grid_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(grid_meter, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(grid_meter, 0, 0);
    lv_obj_set_style_pad_all(grid_meter, 4, 0);

    lv_meter_scale_t *scale = lv_meter_add_scale(grid_meter);
    lv_meter_set_scale_range(grid_meter, scale, -GRID_MAX_W, GRID_MAX_W, 270, 135);
    lv_meter_set_scale_ticks(grid_meter, scale, 2, 0, 0, lv_color_black());

    // Green arc: export to grid (left side, negative values)
    lv_meter_indicator_t *arc_export = lv_meter_add_arc(grid_meter, scale, ARC_PW, lv_color_hex(0x4CAF50), 0);
    lv_meter_set_indicator_start_value(grid_meter, arc_export, -GRID_MAX_W);
    lv_meter_set_indicator_end_value(grid_meter, arc_export, 0);

    // Red arc: import from grid (right side, positive values)
    lv_meter_indicator_t *arc_import = lv_meter_add_arc(grid_meter, scale, ARC_PW, lv_color_hex(0xF44336), 0);
    lv_meter_set_indicator_start_value(grid_meter, arc_import, 0);
    lv_meter_set_indicator_end_value(grid_meter, arc_import, GRID_MAX_W);

    grid_needle = lv_meter_add_needle_line(grid_meter, scale, 2, lv_color_hex(0xFFFFFF), -8);
    lv_meter_set_indicator_value(grid_meter, grid_needle, 0);

    grid_value_label = lv_label_create(grid_cont);
    lv_label_set_text(grid_value_label, "0 W");
    lv_obj_set_style_text_font(grid_value_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(grid_value_label, lv_color_hex(0x888888), 0);
    lv_obj_align(grid_value_label, LV_ALIGN_CENTER, 0, 12);

    lv_obj_t *grid_title = lv_label_create(grid_cont);
    lv_label_set_text(grid_title, "Grid");
    lv_obj_set_style_text_font(grid_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(grid_title, lv_color_hex(0x888888), 0);
    lv_obj_align(grid_title, LV_ALIGN_CENTER, 0, 48);

    // ── Info row: 4 values at the bottom ─────────────────────────────────────
    lv_obj_t *info_row = lv_obj_create(parent);
    lv_obj_set_size(info_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info_row, 0, 0);
    lv_obj_set_style_pad_all(info_row, 0, 0);
    lv_obj_clear_flag(info_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(info_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    create_info_col(info_row, "Status",     &state_label);
    create_info_col(info_row, "Setpoint",   &setpoint_label);
    create_info_col(info_row, "Ext.Power",  &ext_power_label);
    create_info_col(info_row, "EV Charge",  &ev_charging_label);
    // create_info_col(info_row, "Voltage",  &voltage_label);   // remarked
    // create_info_col(info_row, "Freq",     &freq_label);      // remarked
    // create_info_col(info_row, "Current",  &current_label);   // remarked
}

void ui_status_update(const sessy_status_response_t *data, int32_t house_power, bool car_charging,
                      int32_t solar_power, int32_t grid_power)
{
    if (!data) return;

    char buf[32];

    // ── Battery arc ──────────────────────────────────────────────────────────
    int soc_pct = (int)(data->sessy.state_of_charge * 100.0f);
    if (soc_pct < 0)   soc_pct = 0;
    if (soc_pct > 100) soc_pct = 100;
    lv_arc_set_value(soc_arc, soc_pct);

    lv_color_t soc_color;
    if (soc_pct > 20)      soc_color = lv_color_hex(0x4CAF50);
    else if (soc_pct > 10) soc_color = lv_color_hex(0xFF9800);
    else                   soc_color = lv_color_hex(0xF44336);
    lv_obj_set_style_arc_color(soc_arc, soc_color, LV_PART_INDICATOR);

    snprintf(buf, sizeof(buf), "%d%%", soc_pct);
    lv_label_set_text(soc_label, buf);

    snprintf(buf, sizeof(buf), "%d W", (int)data->sessy.power);
    lv_label_set_text(power_label, buf);
    lv_obj_set_style_text_color(power_label,
        data->sessy.power >= 0 ? lv_color_hex(0xF44336) : lv_color_hex(0x4CAF50), 0);

    // ── Solar arc ────────────────────────────────────────────────────────────
    int32_t sp = solar_power < 0 ? 0 : (solar_power > SOLAR_MAX_W ? SOLAR_MAX_W : solar_power);
    lv_arc_set_value(solar_arc, (int)sp);
    snprintf(buf, sizeof(buf), "%d W", (int)solar_power);
    lv_label_set_text(solar_label, buf);
    lv_color_t solar_color = (solar_power > 50) ? lv_color_hex(0xFFD600) : lv_color_hex(0x555555);
    lv_obj_set_style_arc_color(solar_arc, solar_color, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(solar_label, solar_color, 0);

    // ── House arc (green → orange → red) ────────────────────────────────────
    int32_t hp = house_power < 0 ? 0 : (house_power > HOUSE_MAX_W ? HOUSE_MAX_W : house_power);
    lv_arc_set_value(house_arc, (int)hp);
    snprintf(buf, sizeof(buf), "%d W", (int)house_power);
    lv_label_set_text(house_label, buf);
    lv_color_t house_color;
    if (house_power < 5000)       house_color = lv_color_hex(0x4CAF50);
    else if (house_power < 10000) house_color = lv_color_hex(0xFF9800);
    else                          house_color = lv_color_hex(0xF44336);
    lv_obj_set_style_arc_color(house_arc, house_color, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(house_label, house_color, 0);

    // ── Grid meter ───────────────────────────────────────────────────────────
    int32_t gp = grid_power < -GRID_MAX_W ? -GRID_MAX_W : (grid_power > GRID_MAX_W ? GRID_MAX_W : grid_power);
    lv_meter_set_indicator_value(grid_meter, grid_needle, (int)gp);
    if (grid_power > 0) {
        snprintf(buf, sizeof(buf), "+%d W", (int)grid_power);
        lv_obj_set_style_text_color(grid_value_label, lv_color_hex(0xF44336), 0);
    } else if (grid_power < 0) {
        snprintf(buf, sizeof(buf), "%d W", (int)grid_power);
        lv_obj_set_style_text_color(grid_value_label, lv_color_hex(0x4CAF50), 0);
    } else {
        snprintf(buf, sizeof(buf), "0 W");
        lv_obj_set_style_text_color(grid_value_label, lv_color_hex(0x888888), 0);
    }
    lv_label_set_text(grid_value_label, buf);

    // ── Info row ─────────────────────────────────────────────────────────────
    lv_label_set_text(state_label, sessy_state_to_label(data->sessy.system_state));

    snprintf(buf, sizeof(buf), "%d W", (int)data->sessy.power_setpoint);
    lv_label_set_text(setpoint_label, buf);

    snprintf(buf, sizeof(buf), "%d W", (int)data->sessy.external_power);
    lv_label_set_text(ext_power_label, buf);

    // snprintf(buf, sizeof(buf), "%.1f V", data->sessy.pack_voltage / 1000.0f);
    // lv_label_set_text(voltage_label, buf);   // remarked
    // snprintf(buf, sizeof(buf), "%.2f Hz", data->sessy.frequency / 1000.0f);
    // lv_label_set_text(freq_label, buf);      // remarked
    // snprintf(buf, sizeof(buf), "%.1f A", data->sessy.inverter_current_ma / 1000.0f);
    // lv_label_set_text(current_label, buf);   // remarked

    if (car_charging) {
        lv_label_set_text(ev_charging_label, "Yes");
        lv_obj_set_style_text_color(ev_charging_label, lv_color_hex(0xFF9800), 0);
    } else {
        lv_label_set_text(ev_charging_label, "No");
        lv_obj_set_style_text_color(ev_charging_label, lv_color_hex(0x888888), 0);
    }
}
