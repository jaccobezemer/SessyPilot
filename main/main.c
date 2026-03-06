#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "LCD_Driver/ST7701S.h"
#include "Touch/GT911.h"

#include "esp_sntp.h"
#include <time.h>

#include "app_data.h"
#include "settings.h"
#include "wifi_manager.h"
#include "sessy_api.h"
#include "p1_api.h"
#include "ota_server.h"
#include "log_buffer.h"
#include "ui_main.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          6
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT
#define LEDC_DUTY               (0)
#define LEDC_FREQUENCY          (4000)

static const char *TAG = "sessy_ctrl";
/********************* I2C *********************/
#define I2C_Touch_SCL_IO            7
#define I2C_Touch_SDA_IO            15
#define I2C_Touch_INT_IO            16
#define I2C_Touch_RST_IO            -1
#define I2C_MASTER_FREQ_HZ          400000
/********************* LCD *********************/
#define LCD_PIXEL_CLOCK_HZ     (18 * 1000 * 1000)
#define LCD_BK_LIGHT_ON_LEVEL  1
#define LCD_BK_LIGHT_OFF_LEVEL !LCD_BK_LIGHT_ON_LEVEL
#define PIN_NUM_BK_LIGHT       -1
#define PIN_NUM_HSYNC          38
#define PIN_NUM_VSYNC          39
#define PIN_NUM_DE             40
#define PIN_NUM_PCLK           41
#define PIN_NUM_DATA0          5  // B0
#define PIN_NUM_DATA1          45 // B1
#define PIN_NUM_DATA2          48 // B2
#define PIN_NUM_DATA3          47 // B3
#define PIN_NUM_DATA4          21 // B4
#define PIN_NUM_DATA5          14 // G0
#define PIN_NUM_DATA6          13 // G1
#define PIN_NUM_DATA7          12 // G2
#define PIN_NUM_DATA8          11 // G3
#define PIN_NUM_DATA9          10 // G4
#define PIN_NUM_DATA10         9  // G5
#define PIN_NUM_DATA11         46 // R0
#define PIN_NUM_DATA12         3  // R1
#define PIN_NUM_DATA13         8  // R2
#define PIN_NUM_DATA14         18 // R3
#define PIN_NUM_DATA15         17 // R4
#define PIN_NUM_DISP_EN        -1
#define LCD_H_RES              480
#define LCD_V_RES              480

#if CONFIG_EXAMPLE_DOUBLE_FB
#define LCD_NUM_FB             2
#else
#define LCD_NUM_FB             1
#endif

#define LVGL_TICK_PERIOD_MS    2

#define SPI_SDA 1
#define SPI_SCL 2
#define SPI_CS  42

/********************* Shared App Data *********************/
static app_shared_data_t s_shared_data = {0};

/********************* BackLight *********************/
static void ledc_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LEDC_OUTPUT_IO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
SemaphoreHandle_t sem_vsync_end;
SemaphoreHandle_t sem_gui_ready;
#endif

static bool on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    if (xSemaphoreTakeFromISR(sem_gui_ready, &high_task_awoken) == pdTRUE) {
        xSemaphoreGiveFromISR(sem_vsync_end, &high_task_awoken);
    }
#endif
    return high_task_awoken == pdTRUE;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
#endif
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

static void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    esp_lcd_touch_read_data(drv->user_data);
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

i2c_master_bus_handle_t i2c_bus = NULL;

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_Touch_SDA_IO,
        .scl_io_num = I2C_Touch_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&bus_config, &i2c_bus);
}

/********************* SNTP Time Sync *********************/
static void sntp_init_time_sync(void)
{
    static bool sntp_started = false;
    if (sntp_started) return;
    sntp_started = true;

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

/********************* WiFi Event Callback *********************/
static void wifi_event_callback(wifi_mgr_event_t event, void *arg)
{
    switch (event) {
    case WIFI_MGR_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected");
        xSemaphoreTake(s_shared_data.mutex, portMAX_DELAY);
        s_shared_data.wifi_connected = true;
        xSemaphoreGive(s_shared_data.mutex);
        sntp_init_time_sync();
        ota_server_start();
        wifi_manager_discover_sessy();
        break;

    case WIFI_MGR_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi disconnected");
        xSemaphoreTake(s_shared_data.mutex, portMAX_DELAY);
        s_shared_data.wifi_connected = false;
        s_shared_data.sessy_reachable = false;
        xSemaphoreGive(s_shared_data.mutex);
        break;

    case WIFI_MGR_EVENT_SESSY_FOUND: {
        const char *url = wifi_manager_get_sessy_url();
        ESP_LOGI(TAG, "Sessy found: %s", url ? url : "??");
        if (url) {
            sessy_api_init(url);
        }
        break;
    }

    case WIFI_MGR_EVENT_SESSY_NOT_FOUND:
        ESP_LOGW(TAG, "Sessy not found via mDNS, will retry...");
        break;

    case WIFI_MGR_EVENT_P1_FOUND: {
        const char *url = wifi_manager_get_p1_url();
        ESP_LOGI(TAG, "P1 meter found: %s", url ? url : "??");
        if (url) {
            p1_api_init(url);
        }
        break;
    }

    case WIFI_MGR_EVENT_P1_NOT_FOUND:
        ESP_LOGW(TAG, "P1 meter not found via mDNS, will retry...");
        break;
    }
}

/********************* Sessy Polling Task *********************/
void sessy_poll_now(app_shared_data_t *data)
{
    if (!data || !wifi_manager_is_connected() || !wifi_manager_get_sessy_url()) {
        return;
    }

    // Poll power status immediately
    sessy_status_response_t status;
    if (sessy_api_get_power_status(&status) == ESP_OK) {
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        data->power_status = status;
        data->power_status_valid = true;
        data->sessy_reachable = true;
        xSemaphoreGive(data->mutex);
    } else {
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        data->sessy_reachable = false;
        xSemaphoreGive(data->mutex);
    }

    // Poll strategy
    sessy_strategy_t strategy;
    if (sessy_api_get_strategy(&strategy) == ESP_OK) {
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        data->active_strategy = strategy;
        data->strategy_valid = true;
        xSemaphoreGive(data->mutex);
    }
}

static void sessy_poll_task(void *arg)
{
    app_shared_data_t *data = (app_shared_data_t *)arg;
    uint32_t counter = 0;
    bool was_connected = false;
    const uint32_t status_interval = CONFIG_SESSY_POLL_INTERVAL_MS / 1000;
    const uint32_t energy_interval = CONFIG_SESSY_ENERGY_POLL_INTERVAL_MS / 1000;

    ESP_LOGI(TAG, "Polling task started (status: %lus, energy: %lus)",
             (unsigned long)status_interval, (unsigned long)energy_interval);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!wifi_manager_is_connected() || !wifi_manager_get_sessy_url()) {
            // Retry mDNS discovery every 10 seconds when devices not found
            if (wifi_manager_is_connected() && counter % 10 == 0) {
                if (!wifi_manager_get_sessy_url() || !wifi_manager_get_p1_url()) {
                    wifi_manager_discover_sessy();
                }
            }
            counter++;
            was_connected = false;
            continue;
        }

        // Reset counter once on (re)connect so all polls fire immediately
        if (!was_connected) {
            counter = 0;
            was_connected = true;
        }

        // Process pending user commands
        xSemaphoreTake(data->mutex, portMAX_DELAY);
        // bool do_strategy = data->pending_strategy_change;
        // sessy_strategy_t req_strategy = data->requested_strategy;
        bool do_setpoint = data->pending_setpoint_change;
        int32_t req_setpoint = data->requested_setpoint;
        // data->pending_strategy_change = false;
        data->pending_setpoint_change = false;
        xSemaphoreGive(data->mutex);

        // if (do_strategy) {
        //     if (sessy_api_set_strategy(req_strategy) == ESP_OK) {
        //         ESP_LOGI(TAG, "Strategy changed to %s", sessy_strategy_to_string(req_strategy));
        //     }
        // }
        if (do_setpoint) {
            if (sessy_api_set_setpoint(req_setpoint) == ESP_OK) {
                ESP_LOGI(TAG, "Setpoint set to %d W", (int)req_setpoint);
            }
        }

        // Poll power status
        if (counter % status_interval == 0) {
            sessy_status_response_t status;
            if (sessy_api_get_power_status(&status) == ESP_OK) {
                xSemaphoreTake(data->mutex, portMAX_DELAY);
                data->power_status = status;
                data->power_status_valid = true;
                data->sessy_reachable = true;
                xSemaphoreGive(data->mutex);
            } else {
                xSemaphoreTake(data->mutex, portMAX_DELAY);
                data->sessy_reachable = false;
                xSemaphoreGive(data->mutex);
            }

            sessy_strategy_t strategy;
            if (sessy_api_get_strategy(&strategy) == ESP_OK) {
                xSemaphoreTake(data->mutex, portMAX_DELAY);
                data->active_strategy = strategy;
                data->strategy_valid = true;
                xSemaphoreGive(data->mutex);
            }
        }

        // Poll energy (less frequently)
        if (counter % energy_interval == 0) {
            sessy_energy_response_t energy;
            if (sessy_api_get_energy(&energy) == ESP_OK) {
                xSemaphoreTake(data->mutex, portMAX_DELAY);
                data->energy_status = energy;
                data->energy_status_valid = true;
                xSemaphoreGive(data->mutex);
            }
        }

        // Poll P1 meter (same interval as power status)
        if (counter % status_interval == 0 && wifi_manager_get_p1_url()) {
            p1_status_t p1;
            if (p1_api_get_details(&p1) == ESP_OK) {
                xSemaphoreTake(data->mutex, portMAX_DELAY);
                data->p1_status = p1;
                data->p1_status_valid = true;
                data->p1_reachable = true;
                xSemaphoreGive(data->mutex);
            } else {
                xSemaphoreTake(data->mutex, portMAX_DELAY);
                data->p1_reachable = false;
                xSemaphoreGive(data->mutex);
            }

            // Car charge detection based on per-phase grid consumption.
            // A 3-phase EV charger draws roughly equal power on all phases.
            // Start: ALL phases > threshold/3 for 3 consecutive polls.
            // Stop:  ANY phase  < threshold/3 for stop_delay polls.
            static int car_above_count = 0;
            static int car_below_count = 0;
            static int32_t s_ev_last_logged_w = 0;
            if (data->power_status_valid && data->p1_status_valid) {
                // Total house power for display (P1 net + solar + battery)
                int32_t solar_power = data->power_status.phase[0].power
                                    + data->power_status.phase[1].power
                                    + data->power_status.phase[2].power;
                int32_t total = data->p1_status.power_total + solar_power + data->power_status.sessy.power;

                const settings_t *cfg = settings_get();
                int32_t pt = cfg->car_charge_threshold / 3;  // per-phase threshold
                int stop_polls = (cfg->car_charge_stop_delay * 60 * 1000) / CONFIG_SESSY_POLL_INTERVAL_MS;
                if (stop_polls < 3) stop_polls = 3;

                int32_t l1 = data->p1_status.power_consumed_l1;
                int32_t l2 = data->p1_status.power_consumed_l2;
                int32_t l3 = data->p1_status.power_consumed_l3;
                int32_t phase_total = l1 + l2 + l3;
                bool all_above = (l1 > pt) && (l2 > pt) && (l3 > pt);

                if (all_above) {
                    car_above_count++;
                    car_below_count = 0;
                    s_ev_last_logged_w = 0;
                } else {
                    car_below_count++;
                    car_above_count = 0;
                }

                bool was_charging = data->car_charging;
                bool now_charging;
                if (was_charging) {
                    // Instant stop: totaalvermogen zakt ineens onder de per-fase drempel
                    // → handmatig gestopt, geen ramp-down afwachten
                    if (phase_total < pt) {
                        now_charging = false;
                        if (car_below_count == 1) {
                            ESP_LOGI(TAG, "EV instant stop: %dW (L1:%dW L2:%dW L3:%dW drempel:%dW)",
                                     (int)phase_total, (int)l1, (int)l2, (int)l3, (int)(pt * 3));
                        }
                    } else {
                        // Geleidelijke afbouw: wacht stop_polls voor zekerheid
                        now_charging = (car_below_count < stop_polls);
                    }
                } else {
                    now_charging = (car_above_count >= 3);
                }

                // Log ramp-down: eerste poll onder drempel, daarna elke 500W
                if (was_charging && !all_above && phase_total >= pt) {
                    if (car_below_count == 1) {
                        ESP_LOGI(TAG, "EV afbouw start: %dW (L1:%dW L2:%dW L3:%dW drempel:%dW)",
                                 (int)phase_total, (int)l1, (int)l2, (int)l3, (int)(pt * 3));
                        s_ev_last_logged_w = phase_total;
                    } else if (phase_total <= s_ev_last_logged_w - 500) {
                        ESP_LOGI(TAG, "EV afbouw: %dW (L1:%dW L2:%dW L3:%dW)",
                                 (int)phase_total, (int)l1, (int)l2, (int)l3);
                        s_ev_last_logged_w = phase_total;
                    }
                }

                xSemaphoreTake(data->mutex, portMAX_DELAY);
                data->total_house_power = total;
                data->car_charging = now_charging;
                xSemaphoreGive(data->mutex);

                if (now_charging != was_charging) {
                    ESP_LOGI(TAG, "Car charging %s (L1:%dW L2:%dW L3:%dW threshold/phase:%dW)",
                             now_charging ? "DETECTED" : "STOPPED",
                             (int)l1, (int)l2, (int)l3, (int)pt);

                    static sessy_strategy_t s_ev_saved_strategy = STRATEGY_NOM;
                    static bool s_ev_auto_idled = false;

                    if (cfg->ev_auto_idle) {
                        if (now_charging && !s_ev_auto_idled) {
                            xSemaphoreTake(data->mutex, portMAX_DELAY);
                            s_ev_saved_strategy = data->strategy_valid ? data->active_strategy : STRATEGY_NOM;
                            xSemaphoreGive(data->mutex);
                            if (sessy_api_set_strategy(STRATEGY_IDLE) == ESP_OK) {
                                s_ev_auto_idled = true;
                                ESP_LOGI(TAG, "EV auto-idle: strategy set to IDLE (was %s)",
                                         sessy_strategy_to_string(s_ev_saved_strategy));
                            }
                        } else if (!now_charging && s_ev_auto_idled) {
                            if (sessy_api_set_strategy(s_ev_saved_strategy) == ESP_OK) {
                                s_ev_auto_idled = false;
                                ESP_LOGI(TAG, "EV auto-idle: strategy restored to %s",
                                         sessy_strategy_to_string(s_ev_saved_strategy));
                            }
                        }
                    } else {
                        s_ev_auto_idled = false;
                    }
                }
            }
        }

        counter++;
    }
}

/********************* App Main *********************/
void app_main(void)
{
    // ===== PHASE 0: Log Buffer (before anything else) =====
    log_buffer_init();

    // ===== PHASE 1: Hardware Init =====
    ledc_init();
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

    ST7701S_handle st7701s = ST7701S_newObject(SPI_SDA, SPI_SCL, SPI_CS, SPI3_HOST, SPI_METHOD);
    ST7701S_screen_init(st7701s, 1);
    static lv_disp_draw_buf_t disp_buf;
    static lv_disp_drv_t disp_drv;

#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    ESP_LOGI(TAG, "Create semaphores");
    sem_vsync_end = xSemaphoreCreateBinary();
    assert(sem_vsync_end);
    sem_gui_ready = xSemaphoreCreateBinary();
    assert(sem_gui_ready);
#endif

#if PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif

    /********************* Touch *********************/
    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    esp_lcd_touch_handle_t tp = NULL;
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    tp_io_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
    ESP_LOGI(TAG, "Initialize touch IO (I2C)");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_V_RES,
        .y_max = LCD_H_RES,
        .rst_gpio_num = I2C_Touch_RST_IO,
        .int_gpio_num = I2C_Touch_INT_IO,
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_LOGI(TAG, "Initialize touch controller GT911");
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));

    /********************* RGB LCD panel driver *********************/
    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .psram_trans_align = 64,
        .num_fbs = LCD_NUM_FB,
        .bounce_buffer_size_px = 10 * LCD_H_RES,
        .clk_src = LCD_CLK_SRC_PLL240M,
        .disp_gpio_num = PIN_NUM_DISP_EN,
        .pclk_gpio_num = PIN_NUM_PCLK,
        .vsync_gpio_num = PIN_NUM_VSYNC,
        .hsync_gpio_num = PIN_NUM_HSYNC,
        .de_gpio_num = PIN_NUM_DE,
        .data_gpio_nums = {
            PIN_NUM_DATA0,
            PIN_NUM_DATA1,
            PIN_NUM_DATA2,
            PIN_NUM_DATA3,
            PIN_NUM_DATA4,
            PIN_NUM_DATA5,
            PIN_NUM_DATA6,
            PIN_NUM_DATA7,
            PIN_NUM_DATA8,
            PIN_NUM_DATA9,
            PIN_NUM_DATA10,
            PIN_NUM_DATA11,
            PIN_NUM_DATA12,
            PIN_NUM_DATA13,
            PIN_NUM_DATA14,
            PIN_NUM_DATA15,
        },
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = 10,
            .hsync_front_porch = 50,
            .hsync_pulse_width = 8,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .vsync_pulse_width = 3,
            .flags.pclk_active_neg = false,
        },
        .flags.fb_in_psram = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    ESP_LOGI(TAG, "Register event callbacks");
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = on_vsync_event,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, &disp_drv));

    ESP_LOGI(TAG, "Initialize RGB LCD panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

#if PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON_LEVEL);
#endif

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    void *buf1 = NULL;
    void *buf2 = NULL;
#if CONFIG_EXAMPLE_DOUBLE_FB
    ESP_LOGI(TAG, "Use frame buffers as LVGL draw buffers");
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LCD_V_RES);
#else
    ESP_LOGI(TAG, "Allocate separate LVGL draw buffers from PSRAM");
    buf1 = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1);
    buf2 = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LCD_H_RES * LCD_V_RES);
#endif

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
#if CONFIG_EXAMPLE_DOUBLE_FB
    disp_drv.full_refresh = true;
#endif
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };

    /********************* LVGL Input *********************/
    ESP_LOGI(TAG, "Register display indev to LVGL");
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = touchpad_read;
    indev_drv.user_data = tp;
    lv_indev_drv_register(&indev_drv);

    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // ===== PHASE 2: Settings + WiFi Init =====
    ESP_LOGI(TAG, "Initialize settings and WiFi");
    settings_init();

    s_shared_data.mutex = xSemaphoreCreateMutex();
    assert(s_shared_data.mutex);

    wifi_manager_init(wifi_event_callback);
    wifi_manager_start();

    // ===== PHASE 3: UI Init =====
    ESP_LOGI(TAG, "Initialize Sessy UI");
    ui_init(&s_shared_data);

    // ===== PHASE 4: Start Polling Task =====
    xTaskCreatePinnedToCore(
        sessy_poll_task,
        "sessy_poll",
        8192,
        &s_shared_data,
        5,
        NULL,
        1
    );

    // ===== PHASE 5: LVGL Main Loop =====
    // Turn on backlight
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 2048));  // 4096 / 8192 = 50% duty cycle
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

    ESP_LOGI(TAG, "Sessy Controller running");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
