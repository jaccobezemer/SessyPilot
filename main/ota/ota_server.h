#ifndef OTA_SERVER_H
#define OTA_SERVER_H

#include "esp_err.h"

/* Start HTTP server on port 8080 for OTA firmware uploads.
 * GET  /        → upload page
 * POST /update  → receive firmware binary, write to OTA partition, reboot
 * Safe to call multiple times; only starts once. */
esp_err_t ota_server_start(void);

/* OTA progress (thread-safe, lock-free).
 * Returns -1 when idle, 0-100 during upload, 101 when rebooting. */
int ota_get_progress(void);

#endif
