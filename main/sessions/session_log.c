#include "session_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "session_log";

#define SESSIONS_FILE   "/spiffs/sessions.csv"
#define SESSIONS_TMP    "/spiffs/sessions.tmp"
#define CSV_HEADER      "start_unix,stop_unix,start_local,stop_local\n"
#define SPIFFS_MIN_FREE (500 * 1024)   /* trim when < 500 KB free */

static ev_session_t s_sessions[SESSION_MAX];
static int s_count = 0;
static int s_head  = 0;   /* index of oldest entry */
static SemaphoreHandle_t s_mutex;

/* ── File I/O ────────────────────────────────────────────────────── */

static void load_from_file(void)
{
    FILE *f = fopen(SESSIONS_FILE, "r");
    if (!f) {
        ESP_LOGI(TAG, "No sessions file found, starting fresh");
        return;
    }

    char line[160];
    /* Skip CSV header */
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return;
    }

    int loaded = 0;
    while (fgets(line, sizeof(line), f)) {
        long long ts_start = 0, ts_stop = 0;
        if (sscanf(line, "%lld,%lld", &ts_start, &ts_stop) >= 2
            && ts_start > 0 && ts_stop > 0) {
            int idx = (s_head + s_count) % SESSION_MAX;
            if (s_count < SESSION_MAX) {
                s_count++;
            } else {
                s_head = (s_head + 1) % SESSION_MAX;
            }
            s_sessions[idx].start = (time_t)ts_start;
            s_sessions[idx].stop  = (time_t)ts_stop;
            loaded++;
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Loaded %d sessions from file (showing newest %d)",
             loaded, s_count);
}

/* Append a single completed session to the CSV file. */
static void append_to_file(ev_session_t *s)
{
    /* Write header if file is new or empty */
    bool need_header = false;
    FILE *f = fopen(SESSIONS_FILE, "r");
    if (!f) {
        need_header = true;
    } else {
        fseek(f, 0, SEEK_END);
        if (ftell(f) == 0) need_header = true;
        fclose(f);
    }

    f = fopen(SESSIONS_FILE, "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for append", SESSIONS_FILE);
        return;
    }

    if (need_header) fputs(CSV_HEADER, f);

    char t_start[32], t_stop[32];
    struct tm tm_s, tm_e;
    localtime_r(&s->start, &tm_s);
    localtime_r(&s->stop,  &tm_e);
    strftime(t_start, sizeof(t_start), "%Y-%m-%d %H:%M:%S", &tm_s);
    strftime(t_stop,  sizeof(t_stop),  "%Y-%m-%d %H:%M:%S", &tm_e);

    fprintf(f, "%lld,%lld,%s,%s\n",
            (long long)s->start, (long long)s->stop,
            t_start, t_stop);
    fclose(f);
}

/* Count data lines in file (header excluded). */
static int count_file_lines(void)
{
    FILE *f = fopen(SESSIONS_FILE, "r");
    if (!f) return 0;

    char line[160];
    int count = 0;
    bool first = true;
    while (fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }  /* skip header */
        count++;
    }
    fclose(f);
    return count;
}

/* Trim file by removing the oldest half of data lines.
 * Uses a temp file because SPIFFS does not support rename(). */
static void trim_file(void)
{
    int total = count_file_lines();
    if (total <= 1) return;

    int keep = total / 2;
    int skip = total - keep;
    ESP_LOGI(TAG, "Trimming sessions file: %d entries, keeping newest %d", total, keep);

    /* Pass 1: copy newest 'keep' lines to temp file */
    FILE *src = fopen(SESSIONS_FILE, "r");
    FILE *dst = fopen(SESSIONS_TMP, "w");
    if (!src || !dst) {
        if (src) fclose(src);
        if (dst) fclose(dst);
        return;
    }

    char line[160];
    fgets(line, sizeof(line), src);   /* consume header */
    fputs(CSV_HEADER, dst);

    for (int i = 0; i < skip; i++) { /* skip oldest entries */
        if (!fgets(line, sizeof(line), src)) break;
    }
    while (fgets(line, sizeof(line), src)) {
        fputs(line, dst);
    }
    fclose(src);
    fclose(dst);

    /* Pass 2: copy temp back to original */
    remove(SESSIONS_FILE);
    src = fopen(SESSIONS_TMP, "r");
    dst = fopen(SESSIONS_FILE, "w");
    if (!src || !dst) {
        if (src) fclose(src);
        if (dst) fclose(dst);
        return;
    }
    while (fgets(line, sizeof(line), src)) {
        fputs(line, dst);
    }
    fclose(src);
    fclose(dst);
    remove(SESSIONS_TMP);

    ESP_LOGI(TAG, "Trim complete, kept %d sessions", keep);
}

/* Check SPIFFS free space; trim if below threshold. */
static void check_and_trim(void)
{
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    size_t free_bytes = total > used ? total - used : 0;
    if (free_bytes >= SPIFFS_MIN_FREE) return;

    ESP_LOGW(TAG, "SPIFFS low (free=%u KB), trimming sessions",
             (unsigned)(free_bytes / 1024));
    trim_file();
}

/* ── Public API ──────────────────────────────────────────────────── */

void session_log_init(void)
{
    s_mutex = xSemaphoreCreateMutex();

    esp_vfs_spiffs_conf_t conf = {
        .base_path             = "/spiffs",
        .partition_label       = NULL,
        .max_files             = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %u KB total, %u KB used",
             (unsigned)(total / 1024), (unsigned)(used / 1024));

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    load_from_file();
    xSemaphoreGive(s_mutex);
}

void session_log_start(void)
{
    time_t now = time(NULL);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int idx = (s_head + s_count) % SESSION_MAX;
    if (s_count < SESSION_MAX) {
        s_count++;
    } else {
        s_head = (s_head + 1) % SESSION_MAX;
    }
    s_sessions[idx].start = now;
    s_sessions[idx].stop  = 0;

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "EV session started (unix=%lld)", (long long)now);
}

void session_log_stop(void)
{
    time_t now = time(NULL);
    ev_session_t completed = {0, 0};

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (s_count > 0) {
        int last = (s_head + s_count - 1) % SESSION_MAX;
        if (s_sessions[last].stop == 0) {
            s_sessions[last].stop = now;
            completed = s_sessions[last];
            time_t dur = now - completed.start;
            ESP_LOGI(TAG, "EV session stopped, duration %lldm%llds",
                     (long long)(dur / 60), (long long)(dur % 60));
        }
    }

    xSemaphoreGive(s_mutex);

    /* File I/O outside mutex: slow, but only polling task calls this */
    if (completed.stop != 0) {
        append_to_file(&completed);
        check_and_trim();
    }
}

void session_log_clear(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    remove(SESSIONS_FILE);
    s_count = 0;
    s_head  = 0;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Sessions cleared");
}

int session_log_get(ev_session_t *buf, int max)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int n = s_count < max ? s_count : max;
    /* Return newest first */
    for (int i = 0; i < n; i++) {
        int idx = (s_head + s_count - 1 - i) % SESSION_MAX;
        buf[i] = s_sessions[idx];
    }

    xSemaphoreGive(s_mutex);
    return n;
}
