#ifndef P1_API_H
#define P1_API_H

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    int32_t power_consumed;    // W drawn from grid (total)
    int32_t power_produced;    // W sent to grid (total)
    int32_t power_total;       // net (consumed - produced)
    int32_t power_consumed_l1; // W drawn from grid per phase
    int32_t power_consumed_l2;
    int32_t power_consumed_l3;
} p1_status_t;

esp_err_t p1_api_init(const char *base_url);
esp_err_t p1_api_set_url(const char *base_url);
esp_err_t p1_api_get_details(p1_status_t *out);

#endif
