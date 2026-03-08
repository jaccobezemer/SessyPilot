#include "ota_server.h"
#include "log_buffer.h"
#include "session_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <time.h>

static const char *TAG = "ota_server";
static httpd_handle_t s_server = NULL;
static atomic_int s_ota_progress = -1;

int ota_get_progress(void)
{
    return atomic_load(&s_ota_progress);
}

/* ── HTML upload page (split around version placeholder) ────────── */
static const char UPLOAD_PAGE_PRE[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SessyPilot OTA Update</title>"
    "<style>"
    "body{font-family:sans-serif;background:#1e1e1e;color:#ccc;"
    "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
    ".box{background:#2a2a2a;padding:2em;border-radius:12px;text-align:center;max-width:400px;width:90%}"
    "h2{color:#2196F3;margin-top:0}"
    ".ver{color:#888;font-size:13px;margin-top:-0.5em;margin-bottom:1em}"
    "input[type=file]{margin:1em 0;color:#ccc}"
    "button{background:#4CAF50;color:#fff;border:none;padding:12px 32px;"
    "border-radius:6px;font-size:16px;cursor:pointer}"
    "button:disabled{background:#555}"
    "#progress{display:none;margin-top:1em}"
    "#bar{width:100%;height:20px;background:#444;border-radius:10px;overflow:hidden}"
    "#fill{height:100%;width:0%;background:#2196F3;transition:width 0.3s}"
    "#status{margin-top:0.5em;font-size:14px}"
    "</style></head><body><div class='box'>"
    "<h2>SessyPilot OTA Update</h2>"
    "<p class='ver'>Current firmware: ";

static const char UPLOAD_PAGE_POST[] =
    "</p>"
    "<form id='f'><input type='file' id='fw' accept='.bin'><br>"
    "<button type='submit' id='btn'>Upload Firmware</button></form>"
    "<div id='progress'><div id='bar'><div id='fill'></div></div>"
    "<div id='status'>Uploading...</div></div>"
    "<script>"
    "document.getElementById('f').onsubmit=function(e){"
    "e.preventDefault();"
    "var f=document.getElementById('fw').files[0];"
    "if(!f){alert('Select a .bin file first');return;}"
    "var xhr=new XMLHttpRequest();"
    "var p=document.getElementById('progress');"
    "var fill=document.getElementById('fill');"
    "var st=document.getElementById('status');"
    "var btn=document.getElementById('btn');"
    "btn.disabled=true;p.style.display='block';"
    "xhr.upload.onprogress=function(e){"
    "if(e.lengthComputable){var pct=Math.round(e.loaded/e.total*100);"
    "fill.style.width=pct+'%';st.textContent='Uploading: '+pct+'%';}};"
    "xhr.onload=function(){"
    "if(xhr.status==200){st.textContent='Success! Rebooting...';fill.style.width='100%';fill.style.background='#4CAF50';"
    "setTimeout(function(){st.textContent='Waiting for device...';fill.style.background='#FF9800';"
    "var iv=setInterval(function(){fetch('/update').then(function(){clearInterval(iv);"
    "st.textContent='Device is back! Reloading...';fill.style.background='#4CAF50';"
    "setTimeout(function(){location.href='/';},500);}).catch(function(){});},2000);},4000);}"
    "else{st.textContent='Error: '+xhr.responseText;fill.style.background='#F44336';btn.disabled=false;}};"
    "xhr.onerror=function(){st.textContent='Upload failed';fill.style.background='#F44336';btn.disabled=false;};"
    "xhr.open('POST','/update',true);"
    "xhr.setRequestHeader('Content-Type','application/octet-stream');"
    "xhr.send(f);};"
    "</script></div></body></html>";

/* ── GET /update — serve upload page ───────────────────────────── */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, UPLOAD_PAGE_PRE, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, app->version, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, UPLOAD_PAGE_POST, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static void ota_restart_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

/* ── POST /update — receive firmware and flash ──────────────────── */
static esp_err_t update_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA update started, content length: %d", req->content_len);
    atomic_store(&s_ota_progress, 0);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        atomic_store(&s_ota_progress, -1);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (offset 0x%lx)",
             update_partition->label, (unsigned long)update_partition->address);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        atomic_store(&s_ota_progress, -1);
        return ESP_FAIL;
    }

    /* Read firmware in chunks */
    char buf[1024];
    int total_read = 0;
    int remaining = req->content_len;

    while (remaining > 0) {
        int read_len = httpd_req_recv(req, buf, sizeof(buf) < remaining ? sizeof(buf) : remaining);
        if (read_len <= 0) {
            if (read_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  /* Retry on timeout */
            }
            ESP_LOGE(TAG, "Receive error");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            atomic_store(&s_ota_progress, -1);
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            atomic_store(&s_ota_progress, -1);
            return ESP_FAIL;
        }

        total_read += read_len;
        remaining -= read_len;

        int pct = (int)((int64_t)total_read * 100 / req->content_len);
        atomic_store(&s_ota_progress, pct);

        /* Yield briefly after each write so the RGB LCD DMA can catch up.
         * Without this, sustained flash writes cause the display to lose sync. */
        vTaskDelay(pdMS_TO_TICKS(2));

        if (total_read % (64 * 1024) == 0) {
            ESP_LOGI(TAG, "Written %d / %d bytes", total_read, req->content_len);
        }
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        atomic_store(&s_ota_progress, -1);
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        atomic_store(&s_ota_progress, -1);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful (%d bytes), rebooting...", total_read);
    atomic_store(&s_ota_progress, 101);
    httpd_resp_sendstr(req, "OK");

    /* Schedule reboot via timer so the HTTP response can be sent first */
    const esp_timer_create_args_t restart_args = {
        .callback = ota_restart_cb,
        .name = "ota_restart",
    };
    esp_timer_handle_t restart_timer;
    if (esp_timer_create(&restart_args, &restart_timer) == ESP_OK) {
        esp_timer_start_once(restart_timer, 2000 * 1000);  /* 2 seconds */
    } else {
        ESP_LOGE(TAG, "Failed to create restart timer, rebooting immediately");
        esp_restart();
    }

    return ESP_OK;
}

/* ── GET /log — serve log viewer page (uses SSE) ────────────────── */
static const char LOG_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Sessy Log</title>"
    "<style>"
    "body{margin:0;background:#1e1e1e;color:#ccc;font-family:monospace;font-size:13px}"
    "#hdr{display:flex;align-items:center;justify-content:space-between;"
    "padding:8px 12px;background:#2a2a2a;position:sticky;top:0;z-index:1}"
    "#hdr h2{margin:0;color:#2196F3;font-size:15px}"
    "#conn{font-size:12px;padding:3px 10px;border-radius:10px;background:#333}"
    "#log{padding:8px 12px;white-space:pre-wrap;word-break:break-all;min-height:100vh}"
    "</style></head><body>"
    "<div id='hdr'><h2>SessyPilot Log</h2><span id='conn'>Connecting...</span></div>"
    "<pre id='log'></pre>"
    "<script>"
    "var C={'31':'#F44336','32':'#4CAF50','33':'#FFB74D','35':'#CE93D8','36':'#4DD0E1'};"
    "function ansi(s){"
    "s=s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');"
    "s=s.replace(/\\x1b\\[0;(\\d+)m/g,function(_,c){return'<span style=\"color:'+(C[c]||'#ccc')+'\">';});"
    "s=s.replace(/\\x1b\\[0m/g,'</span>');"
    "s=s.replace(/\\x1b\\[[0-9;]*m/g,'');"
    "return s;}"
    "var el=document.getElementById('log');"
    "var cs=document.getElementById('conn');"
    "function atBottom(){return(window.innerHeight+window.scrollY)>=document.body.offsetHeight-60;}"
    "var es=new EventSource('/log/stream');"
    "es.addEventListener('init',function(e){"
    "el.innerHTML=ansi(e.data+'\\n');"
    "window.scrollTo(0,document.body.scrollHeight);"
    "cs.textContent='Live';cs.style.color='#4CAF50';});"
    "es.onmessage=function(e){"
    "if(!e.data)return;"
    "var sb=atBottom();"
    "el.innerHTML+=ansi(e.data+'\\n');"
    "if(sb)window.scrollTo(0,document.body.scrollHeight);};"
    "es.onerror=function(){cs.textContent='Reconnecting...';cs.style.color='#FF9800';};"
    "</script></body></html>";

static esp_err_t log_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, LOG_PAGE);
    return ESP_OK;
}

/* ── SSE helpers ─────────────────────────────────────────────────── */
static esp_err_t send_sse_event(httpd_req_t *req, const char *event_type,
                                const char *text, int len)
{
    esp_err_t ret = ESP_OK;
    if (event_type) {
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "event: %s\n", event_type);
        ret = httpd_resp_send_chunk(req, hdr, HTTPD_RESP_USE_STRLEN);
    }
    const char *p = text;
    const char *end = text + len;
    while (p < end && ret == ESP_OK) {
        const char *nl = memchr(p, '\n', end - p);
        int line = nl ? (nl - p) : (end - p);
        ret = httpd_resp_send_chunk(req, "data: ", 6);
        if (ret == ESP_OK && line > 0)
            ret = httpd_resp_send_chunk(req, p, line);
        if (ret == ESP_OK)
            ret = httpd_resp_send_chunk(req, "\n", 1);
        p = nl ? nl + 1 : end;
    }
    if (ret == ESP_OK)
        ret = httpd_resp_send_chunk(req, "\n", 1);
    return ret;
}

/* ── SSE stream task ─────────────────────────────────────────────── */
static void log_sse_task(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;

    char *buf = malloc(LOG_BUFFER_SIZE + 1);
    if (!buf) goto done;

    int len = log_buffer_dump(buf, LOG_BUFFER_SIZE + 1);
    uint32_t pos = log_buffer_total();
    if (send_sse_event(req, "init", buf, len) != ESP_OK) goto done;

    TickType_t last_ka = xTaskGetTickCount();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t new_pos;
        len = log_buffer_since(pos, buf, LOG_BUFFER_SIZE + 1, &new_pos);
        if (len > 0) {
            pos = new_pos;
            if (send_sse_event(req, NULL, buf, len) != ESP_OK) break;
        } else if ((xTaskGetTickCount() - last_ka) >= pdMS_TO_TICKS(20000)) {
            if (httpd_resp_send_chunk(req, ": ka\n\n", 6) != ESP_OK) break;
            last_ka = xTaskGetTickCount();
        }
    }

done:
    free(buf);
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
}

/* ── GET /log/stream — SSE endpoint ────────────────────────────── */
static esp_err_t log_sse_handler(httpd_req_t *req)
{
    httpd_req_t *async_req;
    esp_err_t ret = httpd_req_async_handler_begin(req, &async_req);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "async_handler_begin failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    httpd_resp_set_type(async_req, "text/event-stream");
    httpd_resp_set_hdr(async_req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(async_req, "X-Accel-Buffering", "no");

    if (xTaskCreate(log_sse_task, "log_sse", 4096, async_req, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SSE task");
        httpd_req_async_handler_complete(async_req);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* ── GET /sessions.csv[?clear=1] — download sessions as CSV file ── */
static esp_err_t sessions_csv_handler(httpd_req_t *req)
{
    /* Check for ?clear=1 query parameter */
    bool do_clear = false;
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0 && qlen < 64) {
        char query[64];
        httpd_req_get_url_query_str(req, query, sizeof(query));
        char val[4];
        if (httpd_query_key_value(query, "clear", val, sizeof(val)) == ESP_OK) {
            do_clear = (val[0] == '1');
        }
    }

    FILE *f = fopen("/spiffs/sessions.csv", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No sessions file yet");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"ev_sessions.csv\"");

    char buf[256];
    while (fgets(buf, sizeof(buf), f)) {
        httpd_resp_send_chunk(req, buf, strlen(buf));
    }
    fclose(f);

    if (do_clear) {
        session_log_clear();
        ESP_LOGI(TAG, "Sessions cleared after download");
    }

    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ── GET / — status page with EV charging sessions ─────────────── */
static esp_err_t status_get_handler(httpd_req_t *req)
{
    static const char *HEAD =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='30'>"
        "<title>SessyPilot</title>"
        "<style>"
        "body{font-family:sans-serif;background:#1e1e1e;color:#ccc;margin:0;padding:20px}"
        "h1{color:#2196F3;margin-bottom:4px}"
        ".meta{color:#666;font-size:13px;margin-bottom:16px}"
        ".links{margin-bottom:24px}"
        ".links a{color:#2196F3;text-decoration:none;margin-right:20px;font-size:14px}"
        "h3{color:#aaa;margin-bottom:8px;font-size:15px}"
        "table{border-collapse:collapse;width:100%;max-width:620px}"
        "th{background:#2a2a2a;color:#aaa;padding:8px 14px;text-align:left;font-size:13px}"
        "td{padding:8px 14px;border-bottom:1px solid #2a2a2a;font-size:13px}"
        ".active{color:#4CAF50;font-weight:bold}"
        ".dur{color:#888}"
        ".empty{color:#555;font-style:italic}"
        "</style></head><body>"
        "<h1>SessyPilot</h1>";

    const esp_app_desc_t *app = esp_app_get_description();
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M:%S", &tm_now);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, HEAD, HTTPD_RESP_USE_STRLEN);

    char meta[128];
    snprintf(meta, sizeof(meta),
             "<p class='meta'>Versie: %s &bull; Tijd: %s</p>",
             app->version, time_str);
    httpd_resp_send_chunk(req, meta, HTTPD_RESP_USE_STRLEN);

    static const char *LINKS =
        "<p class='links'>"
        "<a href='/update'>&#x2B06; Firmware update</a>"
        "<a href='/log'>&#x1F4CB; Log viewer</a>"
        "<a href='/sessions.csv' download>&#x2B07; Download sessies (.csv)</a>"
        "<a href='/sessions.csv?clear=1' download>&#x1F5D1; Download &amp; leeg bestand</a>"
        "</p>";
    httpd_resp_send_chunk(req, LINKS, HTTPD_RESP_USE_STRLEN);

    static const char *TABLE_HEAD =
        "<h3>EV Laadsessies</h3>"
        "<table><thead><tr>"
        "<th>Start</th><th>Stop</th><th>Duur</th>"
        "</tr></thead><tbody>";
    httpd_resp_send_chunk(req, TABLE_HEAD, HTTPD_RESP_USE_STRLEN);

    ev_session_t sessions[SESSION_MAX];
    int n = session_log_get(sessions, SESSION_MAX);

    if (n == 0) {
        static const char *EMPTY =
            "<tr><td colspan='3' class='empty'>Nog geen laadsessies geregistreerd.</td></tr>";
        httpd_resp_send_chunk(req, EMPTY, HTTPD_RESP_USE_STRLEN);
    } else {
        char row[320];
        for (int i = 0; i < n; i++) {
            char t_start[32], t_stop[64], t_dur[64];
            struct tm tm_s;
            localtime_r(&sessions[i].start, &tm_s);
            strftime(t_start, sizeof(t_start), "%d-%m-%Y %H:%M:%S", &tm_s);

            time_t dur;
            if (sessions[i].stop == 0) {
                dur = now - sessions[i].start;
                snprintf(t_stop, sizeof(t_stop), "<span class='active'>Actief</span>");
                snprintf(t_dur,  sizeof(t_dur),
                         "<span class='active'>%lldh %02lldm</span>",
                         (long long)(dur / 3600), (long long)((dur % 3600) / 60));
            } else {
                struct tm tm_e;
                localtime_r(&sessions[i].stop, &tm_e);
                strftime(t_stop, sizeof(t_stop), "%d-%m-%Y %H:%M:%S", &tm_e);
                dur = sessions[i].stop - sessions[i].start;
                snprintf(t_dur, sizeof(t_dur),
                         "<span class='dur'>%lldh %02lldm</span>",
                         (long long)(dur / 3600), (long long)((dur % 3600) / 60));
            }

            snprintf(row, sizeof(row),
                     "<tr><td>%s</td><td>%s</td><td>%s</td></tr>",
                     t_start, t_stop, t_dur);
            httpd_resp_send_chunk(req, row, HTTPD_RESP_USE_STRLEN);
        }
    }

    static const char *TAIL = "</tbody></table></body></html>";
    httpd_resp_send_chunk(req, TAIL, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* ── Start HTTP server ──────────────────────────────────────────── */
esp_err_t ota_server_start(void)
{
    if (s_server) {
        return ESP_OK;  /* Already running */
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t status_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = status_get_handler,
    };
    httpd_register_uri_handler(s_server, &status_uri);

    httpd_uri_t update_get_uri = {
        .uri      = "/update",
        .method   = HTTP_GET,
        .handler  = index_get_handler,
    };
    httpd_register_uri_handler(s_server, &update_get_uri);

    httpd_uri_t update_uri = {
        .uri      = "/update",
        .method   = HTTP_POST,
        .handler  = update_post_handler,
    };
    httpd_register_uri_handler(s_server, &update_uri);

    httpd_uri_t sessions_csv_uri = {
        .uri      = "/sessions.csv",
        .method   = HTTP_GET,
        .handler  = sessions_csv_handler,
    };
    httpd_register_uri_handler(s_server, &sessions_csv_uri);

    httpd_uri_t log_uri = {
        .uri      = "/log",
        .method   = HTTP_GET,
        .handler  = log_get_handler,
    };
    httpd_register_uri_handler(s_server, &log_uri);

    httpd_uri_t log_sse_uri = {
        .uri     = "/log/stream",
        .method  = HTTP_GET,
        .handler = log_sse_handler,
    };
    httpd_register_uri_handler(s_server, &log_sse_uri);

    ESP_LOGI(TAG, "OTA server started on port 8080");
    return ESP_OK;
}
