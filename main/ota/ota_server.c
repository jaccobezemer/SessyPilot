#include "ota_server.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdatomic.h>

static const char *TAG = "ota_server";
static httpd_handle_t s_server = NULL;
static atomic_int s_ota_progress = -1;

int ota_get_progress(void)
{
    return atomic_load(&s_ota_progress);
}

/* ── HTML upload page ───────────────────────────────────────────── */
static const char UPLOAD_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Sessy OTA Update</title>"
    "<style>"
    "body{font-family:sans-serif;background:#1e1e1e;color:#ccc;"
    "display:flex;justify-content:center;align-items:center;height:100vh;margin:0}"
    ".box{background:#2a2a2a;padding:2em;border-radius:12px;text-align:center;max-width:400px;width:90%}"
    "h2{color:#2196F3;margin-top:0}"
    "input[type=file]{margin:1em 0;color:#ccc}"
    "button{background:#4CAF50;color:#fff;border:none;padding:12px 32px;"
    "border-radius:6px;font-size:16px;cursor:pointer}"
    "button:disabled{background:#555}"
    "#progress{display:none;margin-top:1em}"
    "#bar{width:100%;height:20px;background:#444;border-radius:10px;overflow:hidden}"
    "#fill{height:100%;width:0%;background:#2196F3;transition:width 0.3s}"
    "#status{margin-top:0.5em;font-size:14px}"
    "</style></head><body><div class='box'>"
    "<h2>Sessy OTA Update</h2>"
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
    "if(xhr.status==200){st.textContent='Success! Rebooting...';fill.style.width='100%';fill.style.background='#4CAF50';}"
    "else{st.textContent='Error: '+xhr.responseText;fill.style.background='#F44336';btn.disabled=false;}};"
    "xhr.onerror=function(){st.textContent='Upload failed';fill.style.background='#F44336';btn.disabled=false;};"
    "xhr.open('POST','/update',true);"
    "xhr.setRequestHeader('Content-Type','application/octet-stream');"
    "xhr.send(f);};"
    "</script></div></body></html>";

/* ── GET / — serve upload page ──────────────────────────────────── */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, UPLOAD_PAGE, HTTPD_RESP_USE_STRLEN);
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

    /* Give the response time to be sent and UI to show "Rebooting" */
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;  /* Not reached */
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

    httpd_uri_t index_uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = index_get_handler,
    };
    httpd_register_uri_handler(s_server, &index_uri);

    httpd_uri_t update_uri = {
        .uri      = "/update",
        .method   = HTTP_POST,
        .handler  = update_post_handler,
    };
    httpd_register_uri_handler(s_server, &update_uri);

    ESP_LOGI(TAG, "OTA server started on port 8080");
    return ESP_OK;
}
