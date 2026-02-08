#include "ui_energy.h"
#include <stdio.h>

static lv_obj_t *import_labels[4]; // sessy, phase1, phase2, phase3
static lv_obj_t *export_labels[4];

static void create_energy_row(lv_obj_t *parent, const char *title, int index)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2D2D2D), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(card, 4, 0);

    // Title
    lv_obj_t *title_lbl = lv_label_create(card);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0xFFFFFF), 0);

    // Values row
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Import
    lv_obj_t *imp_cont = lv_obj_create(row);
    lv_obj_set_size(imp_cont, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(imp_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(imp_cont, 0, 0);
    lv_obj_set_style_pad_all(imp_cont, 2, 0);
    lv_obj_set_flex_flow(imp_cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *imp_title = lv_label_create(imp_cont);
    lv_label_set_text(imp_title, "Import");
    lv_obj_set_style_text_color(imp_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(imp_title, &lv_font_montserrat_12, 0);

    import_labels[index] = lv_label_create(imp_cont);
    lv_label_set_text(import_labels[index], "-- kWh");
    lv_obj_set_style_text_color(import_labels[index], lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_text_font(import_labels[index], &lv_font_montserrat_16, 0);

    // Export
    lv_obj_t *exp_cont = lv_obj_create(row);
    lv_obj_set_size(exp_cont, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(exp_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(exp_cont, 0, 0);
    lv_obj_set_style_pad_all(exp_cont, 2, 0);
    lv_obj_set_flex_flow(exp_cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *exp_title = lv_label_create(exp_cont);
    lv_label_set_text(exp_title, "Export");
    lv_obj_set_style_text_color(exp_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(exp_title, &lv_font_montserrat_12, 0);

    export_labels[index] = lv_label_create(exp_cont);
    lv_label_set_text(export_labels[index], "-- kWh");
    lv_obj_set_style_text_color(export_labels[index], lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_text_font(export_labels[index], &lv_font_montserrat_16, 0);
}

void ui_energy_create(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(parent, 8, 0);
    lv_obj_set_style_pad_gap(parent, 6, 0);

    create_energy_row(parent, "Sessy Total", 0);
    create_energy_row(parent, "Phase 1", 1);
    create_energy_row(parent, "Phase 2", 2);
    create_energy_row(parent, "Phase 3", 3);
}

void ui_energy_update(const sessy_energy_response_t *data)
{
    if (!data) return;

    char buf[32];
    const sessy_energy_meter_t *meters[] = {
        &data->sessy_energy, &data->phase[0], &data->phase[1], &data->phase[2]
    };

    for (int i = 0; i < 4; i++) {
        snprintf(buf, sizeof(buf), "%.1f kWh", meters[i]->import_wh / 1000.0f);
        lv_label_set_text(import_labels[i], buf);

        snprintf(buf, sizeof(buf), "%.1f kWh", meters[i]->export_wh / 1000.0f);
        lv_label_set_text(export_labels[i], buf);
    }
}
