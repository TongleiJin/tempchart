#include <stdio.h>
#include <assert.h>
#include <freertos/FreeRTOS.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include "user_app.h"
#include "port_power.h"
#include "port_ft6336.h"
#include "port_i2c.h"
#include "epaper_config.h"
#include "tempchart_ui.h"
#include "pcf85063a.h"
#include "port_sdcard.h"
#include "port_display.h"
#include "port_lvgl.h"
#include "port_adc.h"
#include "esp_timer.h"
#include "temp_sampler.h"
#include "chart_controller.h"
#include "input_handler.h"
#include "home_view.h"
#include "port_codec.h"

#define TAG "app"

static I2cMasterBus *i2c_bus = NULL;
static I2cFt6336Dev *ft6336_dev = NULL;
static tempchart_ui_t scr_ui;
static pcf85063a_dev_t pcf85063;  //  rtc句柄
static bool pcf85063initflag = 1; //  rtc初始化成功标志位
static EventGroupHandle_t tempchart_event_group = NULL;
static char Lvgl_buffer[60];
static uint8_t *audio_data_ptr = NULL;
QueueHandle_t xTempDataQueue = NULL;
static TaskHandle_t s_led_task = NULL;
static TaskHandle_t s_lvgl_loop_task = NULL;
static TaskHandle_t s_key_task = NULL;
static TaskHandle_t s_touch_task = NULL;

int overallInfoPageNumber = 1;
int tempDetailPageNumber = CHART_DETAIL_TOTAL_PAGE;

static lv_timer_t *temp_timer = NULL;

static void show_container(int container_number);
static void app_power_off(void);
static void app_set_sample_period(uint32_t period_ms);

void Task_led_loop(void *arg);
void Task_lvgl_loop(void *arg);

bool UserApp_ReadTempHumidity(float *temperature, float *humidity)
{
    return temp_sampler_read(temperature, humidity);
}

void UserApp_GetTimeStr(char *buf, size_t len)
{
    if (!buf || len == 0) {
        return;
    }

    if (pcf85063initflag) {
        pcf85063a_datetime_t current_time = {};
        pcf85063a_get_time_date(&pcf85063, &current_time);
        snprintf(buf, len, "%02d:%02d:%02d",
                 current_time.hour, current_time.min, current_time.sec);
        return;
    }

    snprintf(buf, len, "--:--:--");
}


static void temp_update_timer_cb(lv_timer_t *timer)
{
    pcf85063a_datetime_t current_time = {};
    float t, h;

    if (!temp_sampler_read(&t, &h))
    {
        ESP_LOGE(TAG, "Failed to read temperature and humidity");
        return;
    }

    pcf85063a_get_time_date(&pcf85063, &current_time);
    temp_sampler_push(t, &current_time);
}



void UserApp_Init()
{
    audio_data_ptr = (uint8_t *)heap_caps_malloc(102400, MALLOC_CAP_SPIRAM);
    assert(audio_data_ptr);
    BoardPower_Init();
    BoardPower_EPD_ON();
    BoardPower_Audio_ON();
    BoardPower_VBAT_ON();
    tempchart_event_group = xEventGroupCreate();
    i2c_bus = I2cMasterBus::requestInstance(ESP32_I2C_SCL_PIN, ESP32_I2C_SDA_PIN, ESP32_I2C_DEV_NUM);
    assert(i2c_bus);
    ft6336_dev = I2cFt6336Dev::requestInstance(i2c_bus->Get_I2cBusHandle(), I2C_FT6336_DEV_Address, EPD_WIDTH, EPD_HEIGHT);
    assert(ft6336_dev);
    ft6336_dev->Ft6336_Reset(EPD_TP_RST_PIN);

    esp_err_t ret = pcf85063a_init(&pcf85063, i2c_bus->Get_I2cBusHandle(), PCF85063A_ADDRESS);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize PCF85063A (error: %d)", ret);
        pcf85063initflag = 0;
    }
    while (0 == gpio_get_level(PWR_BUTTON_PIN))
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    input_handler_setup_buttons(BOOT_BUTTON_PIN, PWR_BUTTON_PIN, tempchart_event_group);
    BoardAdc_Init();
    temp_sampler_init(i2c_bus);
    input_handler_touch_gpio_init();
    Codec_StartInit();
}

static void show_container(int container_number)
{
    lv_obj_t *containers[4] = {
        scr_ui.container_temp_chart,
        scr_ui.container_home,
        scr_ui.container_setting,
        scr_ui.container_image,
    };

    ESP_LOGI(TAG, "container: %d", container_number);

    if (Lvgl_lock(portMAX_DELAY))
    {
        // make all containers hidden first
        lv_obj_add_flag(scr_ui.container_temp_chart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scr_ui.container_home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scr_ui.container_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scr_ui.container_setting, LV_OBJ_FLAG_HIDDEN);
        if (container_number >= 1 && container_number <= 4)
        {
            lv_obj_remove_flag(containers[container_number - 1], LV_OBJ_FLAG_HIDDEN);
        }

        if (container_number == 3)
        {
            input_handler_refresh_setting_label();
        }

        if (container_number == 1) // temp chart page
        {
            // unhidden temp chart
            lv_obj_remove_flag(scr_ui.temp_chart, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(scr_ui.label_tempchart_time, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(scr_ui.label_start_temp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(scr_ui.label_end_temp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(scr_ui.label_temp_list, LV_OBJ_FLAG_HIDDEN);
            chart_controller_reset_list_page(&tempDetailPageNumber);
        }

        Lvgl_unlock();
    }
}


static void app_show_container(int container_number)
{
    show_container(container_number);
}

static void app_power_off(void)
{
    BoardPower_VBAT_OFF();
}

static void app_set_sample_period(uint32_t period_ms)
{
    if (temp_timer) {
        lv_timer_set_period(temp_timer, period_ms);
    }
}

void UserUi_Init()
{
    tempchart_ui_create(&scr_ui);
    chart_controller_bind_ui(&scr_ui);

    input_handler_config_t input_cfg = {
        .ui = &scr_ui,
        .touch_dev = ft6336_dev,
        .event_group = tempchart_event_group,
        .show_container = app_show_container,
        .power_off = app_power_off,
        .set_sample_timer_period = app_set_sample_period,
        .overall_info_page = &overallInfoPageNumber,
        .detail_page = &tempDetailPageNumber,
    };
    input_handler_bind(&input_cfg);

    home_view_config_t home_cfg = {
        .ui = &scr_ui,
        .rtc = &pcf85063,
        .rtc_ok = &pcf85063initflag,
        .overall_info_page = &overallInfoPageNumber,
        .header_buf = Lvgl_buffer,
        .header_buf_size = sizeof(Lvgl_buffer),
    };
    home_view_bind(&home_cfg);

    show_container(1);
    temp_timer = lv_timer_create(temp_update_timer_cb, input_handler_get_active_sample_period_ms(), NULL);
    lv_timer_set_repeat_count(temp_timer, -1);
    temp_update_timer_cb(0);
}

void UserApp_Start_Init()
{
    xTaskCreatePinnedToCore(Task_led_loop, "Task_led_loop", 4 * 1024, NULL, 4, &s_led_task, 1);
    xTaskCreatePinnedToCore(Task_lvgl_loop, "Task_lvgl_loop", 5 * 1024, NULL, 2, &s_lvgl_loop_task, 1);
    input_handler_start_tasks(&s_key_task, &s_touch_task);
}

void TempchartApp_Start(void)
{
    UserApp_Init();
    PortLvgl_Start_Init();
    Lvgl_PortInitEpaper();
    UserUi_Init();
    UserApp_Start_Init();
}

void UserApp_ShutdownForOta(void)
{
    ESP_LOGI(TAG, "Stopping app tasks for OTA...");

    if (temp_timer != NULL) {
        lv_timer_delete(temp_timer);
        temp_timer = NULL;
    }

    input_handler_stop_tasks();
    s_key_task = NULL;
    s_touch_task = NULL;

    if (s_lvgl_loop_task != NULL) {
        vTaskDelete(s_lvgl_loop_task);
        s_lvgl_loop_task = NULL;
    }
    if (s_led_task != NULL) {
        vTaskDelete(s_led_task);
        s_led_task = NULL;
    }

    if (audio_data_ptr != NULL) {
        heap_caps_free(audio_data_ptr);
        audio_data_ptr = NULL;
    }
}

void Task_led_loop(void *arg)
{
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = 0x1ULL << LED_PIN;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;

    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    gpio_set_level(LED_PIN, 1);

    for (;;)
    {
        EventBits_t bits = xEventGroupWaitBits(tempchart_event_group, INPUT_LED_BLINK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & INPUT_LED_BLINK_BIT)
        {
            gpio_set_level(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(20));
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}


void Task_lvgl_loop(void *arg)
{
    // use current time as system time
    // pcf85063a_datetime_t datatime = {};
    // datatime.year = 2026;
    // datatime.month = 5;
    // datatime.day = 31;
    // datatime.hour = 15;
    // datatime.min = 54;
    // datatime.sec = 0;
    // pcf85063a_set_time_date(&pcf85063, datatime);

    pcf85063a_datetime_t current_time = {};
    pcf85063a_get_time_date(&pcf85063, &current_time);

    // update the first sample timestamp label
    if (scr_ui.label_temp_list)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "FS: %02d-%02d %02d:%02d:%02d", current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec);
        lv_label_set_text(scr_ui.label_temp_list, buf);
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // if (!Lvgl_lock(500))
        if (!Lvgl_lock(portMAX_DELAY))
        {
            continue;
        }

        // update time, battery state
        if (!lv_obj_has_flag(scr_ui.container_home, LV_OBJ_FLAG_HIDDEN))
        {
            home_view_update_header();
        }

        // update temp chart or list
        if (!lv_obj_has_flag(scr_ui.container_temp_chart, LV_OBJ_FLAG_HIDDEN))
        {
            chart_controller_update_chart();
            chart_controller_update_list(&tempDetailPageNumber);
        }

        // update home page overall information
        if (!lv_obj_has_flag(scr_ui.container_home, LV_OBJ_FLAG_HIDDEN))
        {
            home_view_update_main_info();
        }

        Lvgl_unlock();
    }
}
