#include "log_buffer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static char s_buf[LOG_BUFFER_SIZE];
static int  s_head = 0;       /* next write position */
static bool s_wrapped = false; /* buffer has wrapped at least once */
static uint32_t s_total = 0;  /* monotonic total bytes written */
static SemaphoreHandle_t s_lock;
static vprintf_like_t s_orig_vprintf;

static int log_vprintf(const char *fmt, va_list args)
{
    /* va_list can only be consumed once — copy it before first use */
    va_list args_copy;
    va_copy(args_copy, args);

    /* Format into ring buffer */
    char tmp[256];
    int len = vsnprintf(tmp, sizeof(tmp), fmt, args_copy);
    va_end(args_copy);

    if (len > 0) {
        if (len >= (int)sizeof(tmp)) len = sizeof(tmp) - 1;
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
            for (int i = 0; i < len; i++) {
                s_buf[s_head] = tmp[i];
                s_head = (s_head + 1) % LOG_BUFFER_SIZE;
                if (s_head == 0) s_wrapped = true;
            }
            s_total += (uint32_t)len;
            xSemaphoreGive(s_lock);
        }
    }

    /* Forward to original (serial) output with untouched args */
    return s_orig_vprintf(fmt, args);
}

void log_buffer_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_orig_vprintf = esp_log_set_vprintf(log_vprintf);
}

uint32_t log_buffer_total(void)
{
    return s_total; /* 32-bit read is atomic on ESP32 */
}

int log_buffer_since(uint32_t from, char *dst, int dst_size, uint32_t *out_total)
{
    if (!dst || dst_size <= 0) return 0;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        if (out_total) *out_total = from;
        return 0;
    }

    uint32_t current = s_total;
    if (out_total) *out_total = current;
    uint32_t bytes_since = current - from;

    if (bytes_since == 0) {
        xSemaphoreGive(s_lock);
        return 0;
    }

    int len;
    if (bytes_since >= (uint32_t)LOG_BUFFER_SIZE) {
        /* More than full buffer written since last check — return full current buffer */
        if (s_wrapped) {
            int tail = LOG_BUFFER_SIZE - s_head;
            if (tail > dst_size - 1) tail = dst_size - 1;
            memcpy(dst, &s_buf[s_head], tail);
            len = tail;
            int head = s_head;
            if (head > dst_size - 1 - len) head = dst_size - 1 - len;
            memcpy(dst + len, s_buf, head);
            len += head;
        } else {
            len = s_head;
            if (len > dst_size - 1) len = dst_size - 1;
            memcpy(dst, s_buf, len);
        }
    } else {
        /* Only the new bytes, ending at s_head */
        len = (int)bytes_since;
        if (len > dst_size - 1) len = dst_size - 1;
        int start = (int)(((uint32_t)s_head + LOG_BUFFER_SIZE - (uint32_t)len) % LOG_BUFFER_SIZE);
        if (start + len <= LOG_BUFFER_SIZE) {
            memcpy(dst, &s_buf[start], len);
        } else {
            int part1 = LOG_BUFFER_SIZE - start;
            memcpy(dst, &s_buf[start], part1);
            memcpy(dst + part1, s_buf, len - part1);
        }
    }

    xSemaphoreGive(s_lock);
    dst[len] = '\0';
    return len;
}

int log_buffer_dump(char *dst, int dst_size)
{
    if (!dst || dst_size <= 0) return 0;

    int written = 0;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_wrapped) {
            /* Oldest data starts at s_head, wraps around */
            int tail_len = LOG_BUFFER_SIZE - s_head;
            if (tail_len > dst_size - 1) tail_len = dst_size - 1;
            memcpy(dst, &s_buf[s_head], tail_len);
            written = tail_len;

            int head_len = s_head;
            if (head_len > dst_size - 1 - written) head_len = dst_size - 1 - written;
            memcpy(dst + written, s_buf, head_len);
            written += head_len;
        } else {
            int len = s_head;
            if (len > dst_size - 1) len = dst_size - 1;
            memcpy(dst, s_buf, len);
            written = len;
        }
        xSemaphoreGive(s_lock);
    }
    dst[written] = '\0';
    return written;
}
