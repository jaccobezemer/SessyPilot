#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <stdint.h>

/* Circular log buffer that captures esp_log output.
 * Call log_buffer_init() early in app_main (before WiFi/tasks).
 * Retrieve contents with log_buffer_dump() for the HTTP /log page. */

/* Install the custom vprintf hook so all ESP_LOGx output is captured. */
void log_buffer_init(void);

/* Copy the current ring buffer contents into `dst` (null-terminated).
 * Returns the number of bytes written (excluding NUL).
 * `dst_size` should be at least LOG_BUFFER_SIZE + 1. */
int log_buffer_dump(char *dst, int dst_size);

/* Returns monotonically increasing total bytes written (wraps at UINT32_MAX). */
uint32_t log_buffer_total(void);

/* Copy only bytes written since `from` total into `dst`.
 * Sets *out_total to current total. Returns bytes copied.
 * If more than LOG_BUFFER_SIZE bytes were written since `from`, returns full buffer. */
int log_buffer_since(uint32_t from, char *dst, int dst_size, uint32_t *out_total);

#define LOG_BUFFER_SIZE 8192

#endif
