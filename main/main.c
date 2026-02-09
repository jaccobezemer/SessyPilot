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
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "LCD_Driver/ST7701S.h"
#include "Touch/GT911.h"

#include "app_data.h"
#include "settings.h"
#include "wifi_manager.h"
#include "sessy_api.h"
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
#define I2C_MASTER_NUM              0
#define I2C_MASTER_FREQ_HZ          400000
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0
#define I2C_MASTER_TIMEOUT_MS       1000
/********************* LCD *********************/
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (18 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT       -1
#define EXAMPLE_PIN_NUM_HSYNC          38
#define EXAMPLE_PIN_NUM_VSYNC          39
#define EXAMPLE_PIN_NUM_DE             40
#define EXAMPLE_PIN_NUM_PCLK           41
#define EXAMPLE_PIN_NUM_DATA0          5  // B0
#define EXAMPLE_PIN_NUM_DATA1          45 // B1
#define EXAMPLE_PIN_NUM_DATA2          48 // B2
#define EXAMPLE_PIN_NUM_DATA3          47 // B3
#define EXAMPLE_PIN_NUM_DATA4          21 // B4
#define EXAMPLE_PIN_NUM_DATA5          14 // G0
#define EXAMPLE_PIN_NUM_DATA6          13 // G1
#define EXAMPLE_PIN_NUM_DATA7          12 // G2
#define EXAMPLE_PIN_NUM_DATA8          11 // G3
#define EXAMPLE_PIN_NUM_DATA9          10 // G4
#define EXAMPLE_PIN_NUM_DATA10         9  // G5
#define EXAMPLE_PIN_NUM_DATA11         46 // R0
#define EXAMPLE_PIN_NUM_DATA12         3  // R1
#define EXAMPLE_PIN_NUM_DATA13         8  // R2
#define EXAMPLE_PIN_NUM_DATA14         18 // R3
#define EXAMPLE_PIN_NUM_DATA15         17 // R4
#define EXAMPLE_PIN_NUM_DISP_EN        -1
#define EXAMPLE_LCD_H_RES              480
#define EXAMPLE_LCD_V_RES              480

#if CONFIG_EXAMPLE_DOUBLE_FB
#define EXAMPLE_LCD_NUM_FB             2
#else
#define EXAMPLE_LCD_NUM_FB             1
#endif

#define EXAMPLE_LVGL_TICK_PERIOD_MS    2

#define SPI_SDA 1
#define SPI_SCL 2
#define SPI_CS  42

/********************* Shared App Data *********************/
static app_shared_data_t s_shared_data = {0};

/********************* BackLight *********************/
static void example_ledc_init(void)
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

static bool example_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;
#if CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM
    if (xSemaphoreTakeFromISR(sem_gui_ready, &high_task_awoken) == pdTRUE) {
        xSemaphoreGiveFromISR(sem_vsync_end, &high_task_awoken);
    }
#endif
    return high_task_awoken == pdTRUE;
}

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
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

static void example_increase_lvgl_tick(void *arg)
{
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void example_touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
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

static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_Touch_SDA_IO,
        .scl_io_num = I2C_Touch_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(i2c_master_port, &conf);
    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
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
    const uint32_t status_interval = CONFIG_SESSY_POLL_INTERVAL_MS / 1000;
    const uint32_t energy_interval = CONFIG_SESSY_ENERGY_POLL_INTERVAL_MS / 1000;

    ESP_LOGI(TAG, "Polling task started (status: %lus, energy: %lus)",
             (unsigned long)status_interval, (unsigned long)energy_interval);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!wifi_manager_is_connected() || !wifi_manager_get_sessy_url()) {
            // Retry mDNS discovery every 10 seconds when Sessy not found
            if (wifi_manager_is_connected() && counter % 10 == 0) {
                wifi_manager_discover_sessy();
            }
            counter++;
            continue;
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

        counter++;
    }
}

/********************* App Main *********************/
void app_main(void)
{
    // ===== PHASE 1: Hardware Init =====
    example_ledc_init();
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

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif

    /********************* Touch *********************/
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    esp_lcd_touch_handle_t tp = NULL;
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    ESP_LOGI(TAG, "Initialize touch IO (I2C)");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_MASTER_NUM, &tp_io_config, &tp_io_handle));
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_V_RES,
        .y_max = EXAMPLE_LCD_H_RES,
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
        .num_fbs = EXAMPLE_LCD_NUM_FB,
#if CONFIG_EXAMPLE_USE_BOUNCE_BUFFER
        .bounce_buffer_size_px = 10 * EXAMPLE_LCD_H_RES,
#endif
        .clk_src = LCD_CLK_SRC_PLL240M,
        .disp_gpio_num = EXAMPLE_PIN_NUM_DISP_EN,
        .pclk_gpio_num = EXAMPLE_PIN_NUM_PCLK,
        .vsync_gpio_num = EXAMPLE_PIN_NUM_VSYNC,
        .hsync_gpio_num = EXAMPLE_PIN_NUM_HSYNC,
        .de_gpio_num = EXAMPLE_PIN_NUM_DE,
        .data_gpio_nums = {
            EXAMPLE_PIN_NUM_DATA0,
            EXAMPLE_PIN_NUM_DATA1,
            EXAMPLE_PIN_NUM_DATA2,
            EXAMPLE_PIN_NUM_DATA3,
            EXAMPLE_PIN_NUM_DATA4,
            EXAMPLE_PIN_NUM_DATA5,
            EXAMPLE_PIN_NUM_DATA6,
            EXAMPLE_PIN_NUM_DATA7,
            EXAMPLE_PIN_NUM_DATA8,
            EXAMPLE_PIN_NUM_DATA9,
            EXAMPLE_PIN_NUM_DATA10,
            EXAMPLE_PIN_NUM_DATA11,
            EXAMPLE_PIN_NUM_DATA12,
            EXAMPLE_PIN_NUM_DATA13,
            EXAMPLE_PIN_NUM_DATA14,
            EXAMPLE_PIN_NUM_DATA15,
        },
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = EXAMPLE_LCD_H_RES,
            .v_res = EXAMPLE_LCD_V_RES,
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
        .on_vsync = example_on_vsync_event,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, &disp_drv));

    ESP_LOGI(TAG, "Initialize RGB LCD panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    void *buf1 = NULL;
    void *buf2 = NULL;
#if CONFIG_EXAMPLE_DOUBLE_FB
    ESP_LOGI(TAG, "Use frame buffers as LVGL draw buffers");
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);
#else
    ESP_LOGI(TAG, "Allocate separate LVGL draw buffers from PSRAM");
    buf1 = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1);
    buf2 = heap_caps_malloc(EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);
#endif

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
#if CONFIG_EXAMPLE_DOUBLE_FB
    disp_drv.full_refresh = true;
#endif
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };

    /********************* LVGL Input *********************/
    ESP_LOGI(TAG, "Register display indev to LVGL");
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_touchpad_read;
    indev_drv.user_data = tp;
    lv_indev_drv_register(&indev_drv);

    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

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
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 4096));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));

    ESP_LOGI(TAG, "Sessy Controller running");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
