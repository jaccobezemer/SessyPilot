#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

/* Circular log buffer that captures esp_log output.
 * Call log_buffer_init() early in app_main (before WiFi/tasks).
 * Retrieve contents with log_buffer_dump() for the HTTP /log page. */

/* Install the custom vprintf hook so all ESP_LOGx output is captured. */
void log_buffer_init(void);

/* Copy the current ring buffer contents into `dst` (null-terminated).
 * Returns the number of bytes written (excluding NUL).
 * `dst_size` should be at least LOG_BUFFER_SIZE + 1. */
int log_buffer_dump(char *dst, int dst_size);

#define LOG_BUFFER_SIZE 8192

#endif
