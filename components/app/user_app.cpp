#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include "user_app.h"
#include "port_power.h"
#include "port_ft6336.h"
#include "port_i2c.h"
#include "epaper_config.h"
#include "FacTest_ui.h"
#include "pcf85063a.h"
#include "button.h"
#include "port_sdcard.h"
#include "port_lvgl.h"
#include "port_adc.h"
#include "port_shtc3.h"
#include "port_sht31.h"
#include "port_codec.h"
#include "esp_timer.h"
#include "user_data_type.h"
#include "lite_fifo.h"

#define TAG "app"
#define LED_BLINK_BIT 0x80

static I2cMasterBus *i2c_bus = NULL;
static I2cFt6336Dev *ft6336_dev = NULL;
static lv_factest_ui scr_ui;
static pcf85063a_dev_t pcf85063;  //  rtc句柄
static bool pcf85063initflag = 1; //  rtc初始化成功标志位
static Button *boot_button = nullptr;
static Button *power_button = nullptr;
static EventGroupHandle_t FacTestEventGroup = NULL; // 事件组句柄
static char Lvgl_buffer[60];
static QueueHandle_t gpio_evt_queue = NULL;
static uint8_t *audio_data_ptr = NULL;
QueueHandle_t xTempDataQueue = NULL;
static SemaphoreHandle_t temp_read_mutex = NULL;

static bool read_temp_humidity(float *temperature, float *humidity);

static int settingTarget = 0;

int overallInfoPageNumber = 1;

#define MAX_TEMP_FIFO_SIZE 100
#define MAX_TEMP_DETAIL_TOTAL_PAGE (MAX_TEMP_FIFO_SIZE / 10)

int tempDetailPageNumber = MAX_TEMP_DETAIL_TOTAL_PAGE;
static float max_temp = 0;
static float min_temp = 100;

static lv_timer_t *temp_timer = NULL;
static const uint32_t temp_period_list[] = {3000, 20000, 60000, 300000};

static const uint32_t chartPointList[] = {20, 40, 100};
static const size_t temp_period_count = sizeof(temp_period_list) / sizeof(temp_period_list[0]);
static size_t temp_period_selected_index = 0; // default selected 3000ms
static size_t temp_period_active_index = 0;   // default active 3000ms
static size_t chartPointsSelectedIndex = 0;   // default selected 10 points
static size_t chartPointsActiveIndex = 0;     // default active 10 points

static uint32_t temp_sample_count = 0;
uint16_t TEMP_OFFSET = 20;
const uint16_t TEMP_SCALER = 3;
// define the fifo for user data, with a capacity of 100 records
static liteFifo_t tempDataFifo;

static void update_overall_label(void);
void Task_led_loop(void *arg);
void Task_lvgl_loop(void *arg);
void InitializeButtons(void); /* button 初始化 */
void Task_key_loop(void *arg);
void Task_touch_loop(void *arg);
static void gpio_isr_handler(void *arg);
void Touch_ISR_GPIO_Init();

static bool read_temp_humidity(float *temperature, float *humidity)
{
    if (!temperature || !humidity)
    {
        return false;
    }

    if (!temp_read_mutex)
    {
        return false;
    }

    if (xSemaphoreTake(temp_read_mutex, portMAX_DELAY) != pdTRUE)
    {
        return false;
    }

    bool success = false;
    float t = -1000.0f;
    float h = -1000.0f;

    Shtc3_ReadTempHumi(&t, &h);
    if (t != -1000.0f && h != -1000.0f)
    {
        success = true;
    }
    else
    {
        Sht31_ReadTempHumi(&t, &h);
        if (t != -1000.0f && h != -1000.0f)
        {
            success = true;
        }
    }

    if (success)
    {
        *temperature = t;
        *humidity = h;
    }

    xSemaphoreGive(temp_read_mutex);
    return success;
}



static void temp_update_timer_cb(lv_timer_t *timer)
{
    pcf85063a_datetime_t current_time = {};
    float t, h;

    if (!read_temp_humidity(&t, &h))
    {
        ESP_LOGE(TAG, "Failed to read temperature and humidity");
        return;
    }

    pcf85063a_get_time_date(&pcf85063, &current_time);
    
    user_data_t ud;

    ud.temperature = t;
    ud.timestamp = current_time;
    fifo_PushData(&tempDataFifo, ud, true);
    temp_sample_count++;
    if (t > max_temp)
    {
        max_temp = t;
    }
    if (t < min_temp)
    {
        min_temp = t;
    }
    ESP_LOGI(TAG, "Temp%ld: %.1f %.1f", temp_sample_count, t, h);
}



void UserApp_Init()
{
    audio_data_ptr = (uint8_t *)heap_caps_malloc(102400, MALLOC_CAP_SPIRAM);
    assert(audio_data_ptr);
    BoardPower_Init();
    BoardPower_EPD_ON();
    BoardPower_Audio_ON();
    BoardPower_VBAT_ON();
    FacTestEventGroup = xEventGroupCreate();
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
    boot_button = new Button(BOOT_BUTTON_PIN);
    power_button = new Button(PWR_BUTTON_PIN);
    while (0 == gpio_get_level(PWR_BUTTON_PIN))
    { // 等待上电按钮释放
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    InitializeButtons();
    BoardAdc_Init();
    Shtc3_Init(i2c_bus);
    Sht31_Init(i2c_bus);
    temp_read_mutex = xSemaphoreCreateMutex();
    assert(temp_read_mutex);
    Touch_ISR_GPIO_Init();
    Codec_StartInit();
    user_data_t *buf = (user_data_t *)heap_caps_malloc(sizeof(user_data_t) * MAX_TEMP_FIFO_SIZE, MALLOC_CAP_SPIRAM);
    lv_memset(buf, 0, sizeof(user_data_t) * MAX_TEMP_FIFO_SIZE);
    fifo_CreateLiteFifo(&tempDataFifo, MAX_TEMP_FIFO_SIZE, buf);
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

        if (container_number == 3) // Setting page
        {
            char buf[80];
            if (settingTarget == 0)
            {
                // print current selected sample interval to the label
                uint32_t selected = temp_period_list[temp_period_selected_index] / 1000;
                snprintf(buf, sizeof(buf), "Sample Intv Sel:%lu", selected);
                lv_label_set_text(scr_ui.label_touch_event, buf);
            }
            else if (settingTarget == 1)
            {
                // print current selected chart point count to the label
                snprintf(buf, sizeof(buf), "Chart Point Sel:%lu", chartPointList[chartPointsSelectedIndex]);
                lv_label_set_text(scr_ui.label_touch_event, buf);
            }
        }

        if (container_number == 1) // temp chart page
        {
            // unhidden temp chart
            lv_obj_remove_flag(scr_ui.temp_chart, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(scr_ui.label_tempchart_time, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(scr_ui.label_start_temp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(scr_ui.label_end_temp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(scr_ui.label_temp_list, LV_OBJ_FLAG_HIDDEN);
            tempDetailPageNumber = MAX_TEMP_DETAIL_TOTAL_PAGE;
        }

        Lvgl_unlock();
    }
}


void update_overall_label(void)
{
    char buf[140] = "";
    if (overallInfoPageNumber == 1)
    {
        uint32_t active_s = temp_period_list[temp_period_active_index] / 1000;
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "Sample period: %lu", active_s);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nCount: %lu", temp_sample_count);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp offset: %u", TEMP_OFFSET);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp scaler: %u", TEMP_SCALER);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nChart size: %lu", lv_chart_get_point_count(scr_ui.temp_chart));

        float t = 0.0f;
        float h = 0.0f;
        if (read_temp_humidity(&t, &h))
        {
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp: %.1f°C\nHum: %.1f%%", t, h);
        }
        else
        {
            snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp: --°C\nHum: --%%");
        }
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nSIZE: %u", strlen(buf)+9);
    }
    else
    {
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "KEYS:");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nBOOT: more detail");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nBOOT DOUBLE: home");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nPOWER: next page");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nPOWER LONG: power off");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nPOWER DOUBLE: show chart");
    }

    lv_label_set_text(scr_ui.label_home_main_info, buf);
}

static void touch_on_next(void)
{
    char buf[80] = "default";
    if (settingTarget == 0)
    {
        temp_period_selected_index = (temp_period_selected_index + 1) % temp_period_count;
        uint32_t selected = temp_period_list[temp_period_selected_index] / 1000;
        snprintf(buf, sizeof(buf), "Sample Intv Sel:%lu", selected);
    }
    else
    {
        chartPointsSelectedIndex = (chartPointsSelectedIndex + 1) % (sizeof(chartPointList) / sizeof(chartPointList[0]));
        snprintf(buf, sizeof(buf), "Chart Point Sel:%lu", chartPointList[chartPointsSelectedIndex]);
    }
    lv_label_set_text(scr_ui.label_touch_event, buf);
}

static void touch_on_active_button(void)
{
    if (settingTarget == 0)
    {
        temp_period_active_index = temp_period_selected_index;
        lv_timer_set_period(temp_timer, temp_period_list[temp_period_active_index]);
    }
    else
    {
        chartPointsActiveIndex = chartPointsSelectedIndex;
        lv_chart_set_point_count(scr_ui.temp_chart, chartPointList[chartPointsActiveIndex]);
    }

    lv_label_set_text(scr_ui.label_touch_event, "actived");
}

static void touch_on_cancel_button(void)
{
    char buf[80] = "default";
    if (settingTarget == 0)
    {
        temp_period_selected_index = temp_period_active_index;
        uint32_t selected = temp_period_list[temp_period_selected_index] / 1000;
        snprintf(buf, sizeof(buf), "Sample Intv | Sel:%lu", selected);
        lv_label_set_text(scr_ui.label_touch_event, buf);
    }
    else if (settingTarget == 1)
    {
        chartPointsSelectedIndex = chartPointsActiveIndex;
        snprintf(buf, sizeof(buf), "Chart Point | Sel:%lu", chartPointList[chartPointsSelectedIndex]);
    }
    lv_label_set_text(scr_ui.label_touch_event, buf);
}

static void touch_on_more_button(void)
{
    if (settingTarget == 0)
    {
        // toggle chart point count between 10 and 30 for testing
        settingTarget = 1;
        char buf[80];
        snprintf(buf, sizeof(buf), "Chart Point Mode:%lu", chartPointList[chartPointsSelectedIndex]);
        lv_label_set_text(scr_ui.label_touch_event, buf);
    }
    else if (settingTarget == 1)
    {
        settingTarget = 0;
        char buf[80];
        uint32_t selected = temp_period_list[temp_period_selected_index] / 1000;
        snprintf(buf, sizeof(buf), "Sample Intv Mode:%lu", selected);
        lv_label_set_text(scr_ui.label_touch_event, buf);
    }
}

void UserUi_Init()
{
    setup_factest_ui(&scr_ui);

    // Show only container 1 after power on.
    show_container(1);
    temp_timer = lv_timer_create(temp_update_timer_cb, temp_period_list[temp_period_active_index], NULL);
    lv_timer_set_repeat_count(temp_timer, -1);
    temp_update_timer_cb(0);
}

void UserApp_Start_Init()
{
    xTaskCreatePinnedToCore(Task_led_loop, "Task_led_loop", 4 * 1024, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(Task_lvgl_loop, "Task_lvgl_loop", 5 * 1024, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(Task_key_loop, "Task_key_loop", 4 * 1024, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(Task_touch_loop, "Touch_LoppTask", 4 * 1024, NULL, 2, NULL, 1);
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
        EventBits_t bits = xEventGroupWaitBits(FacTestEventGroup, LED_BLINK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & LED_BLINK_BIT)
        {
            gpio_set_level(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(20));
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}


uint16_t get_float_pos_y(float temp)
{
    /* Map actual temperature value into chart Y using assumed chart range 20..45 */
    const float chart_temp_min = 20.0f;
    const float chart_temp_max = 45.0f;
    const int fontHigt = 18;
    const int margin = 2; // small padding from top

    lv_area_t chart_coords;
    lv_obj_get_content_coords(scr_ui.temp_chart, &chart_coords);
    int chart_top = chart_coords.y1;
    int chart_bottom = chart_coords.y2;
    int chart_height = chart_bottom - chart_top;
    if (chart_height <= 0)
        chart_height = 1;

    // Clamp temperature into range
    if (temp < chart_temp_min)
        temp = chart_temp_min;
    if (temp > chart_temp_max)
        temp = chart_temp_max;

    // Compute relative position [0..1]
    float rel = (temp - chart_temp_min) / (chart_temp_max - chart_temp_min);
    if (rel < 0.0f)
        rel = 0.0f;
    if (rel > 1.0f)
        rel = 1.0f;

    // LVGL y increases downward: top = smaller y, bottom = larger y
    int y = chart_bottom - (int)(rel * chart_height * 1.1);

    // Ensure label fits inside chart and leave space for font height
    int y_min = chart_top + margin;
    int y_max = chart_bottom - fontHigt;
    if (y < y_min)
        y = y_min;
    if (y > y_max)
        y = y_max;

    ESP_LOGI(TAG, "Temp: %.1f rel: %.3f mapped to Y: %d (top:%d bottom:%d height:%d)", temp, rel, y, chart_top, chart_bottom, chart_height);

    return (uint16_t)y;
}

// don't handle locker or efficency
void do_time_bat_update(void)
{
    pcf85063a_datetime_t current_time = {};
    
    if ((overallInfoPageNumber == 1) && scr_ui.label_home_header)
    {
        pcf85063a_get_time_date(&pcf85063, &current_time);
        uint8_t batLevel = Get_Batterylevel();
        if (batLevel > 95)
        {
            strcpy(Lvgl_buffer, LV_SYMBOL_BATTERY_FULL);
        }
        else if (batLevel > 75)
        {
            strcpy(Lvgl_buffer, LV_SYMBOL_BATTERY_3);
        }
        else if (batLevel > 50)
        {
            strcpy(Lvgl_buffer, LV_SYMBOL_BATTERY_2);
        }
        else
        {
            strcpy(Lvgl_buffer, LV_SYMBOL_BATTERY_1);
        }

        snprintf(Lvgl_buffer+strlen(Lvgl_buffer), sizeof(Lvgl_buffer), " %d%%   %02d:%02d:%02d", batLevel, current_time.hour, current_time.min, current_time.sec);
        lv_label_set_text(scr_ui.label_home_header, Lvgl_buffer);
    }
}

void do_temp_chart_update(void)
{
    user_data_t tmpTemp;
    float temp = 0;
    float totalValue = 0;

    user_data_t temp_record[MAX_TEMP_FIFO_SIZE];

    fifo_CopyData(&tempDataFifo, temp_record, MAX_TEMP_FIFO_SIZE);
    int point_count = (int)lv_chart_get_point_count(scr_ui.temp_chart);
    ESP_LOGI(TAG, "Updating chart with %d points", point_count);
    totalValue = 0;
    for (int i = MAX_TEMP_FIFO_SIZE - point_count; i < MAX_TEMP_FIFO_SIZE; ++i)
    {
        temp = temp_record[i].temperature;
        totalValue += temp;
        temp = (temp - TEMP_OFFSET) * TEMP_SCALER;
        lv_chart_set_next_value(scr_ui.temp_chart, scr_ui.temp_series, temp);
    }
    ESP_LOGI(TAG, "Total temp value: %.1f", totalValue);

    totalValue = totalValue / point_count + 0.5;
    TEMP_OFFSET = (uint16_t)((totalValue * 0.8) * 0.2 + TEMP_OFFSET * 0.8);
    ESP_LOGI(TAG, "Updated TEMP_OFFSET: %u", TEMP_OFFSET);

    lv_obj_invalidate(scr_ui.temp_chart);
    char buf[128] = "";

    int lastIndex = MAX_TEMP_FIFO_SIZE - 1;
    int firstIndex = lastIndex - point_count;
    if (firstIndex < 0)
    {
        firstIndex = 0;
    }

    tmpTemp = temp_record[firstIndex];
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%02d:%02d:%02d", tmpTemp.timestamp.hour, tmpTemp.timestamp.min, tmpTemp.timestamp.sec);
    tmpTemp = temp_record[lastIndex];
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "              %02d:%02d:%02d", tmpTemp.timestamp.hour, tmpTemp.timestamp.min, tmpTemp.timestamp.sec);
    if (scr_ui.label_tempchart_time)
    {
        lv_label_set_text(scr_ui.label_tempchart_time, buf);
    }

    /* Position small labels near first and last visible chart points inside the chart area */
    if (scr_ui.label_start_temp && scr_ui.label_end_temp && scr_ui.temp_chart)
    {
        char pbuf[32];
        int yy_first = get_float_pos_y(temp_record[firstIndex].temperature);
        int yy_last = get_float_pos_y(temp_record[lastIndex].temperature);

        snprintf(pbuf, sizeof(pbuf), "%.1f", temp_record[firstIndex].temperature);
        lv_label_set_text(scr_ui.label_start_temp, pbuf);
        lv_obj_set_pos(scr_ui.label_start_temp, 5, yy_first);

        snprintf(pbuf, sizeof(pbuf), "%.1f", temp_record[lastIndex].temperature);
        lv_label_set_text(scr_ui.label_end_temp, pbuf);
        lv_obj_set_pos(scr_ui.label_end_temp, 165, yy_last);
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
            do_time_bat_update();
        }

        // update temp chart or list
        if (!lv_obj_has_flag(scr_ui.container_temp_chart, LV_OBJ_FLAG_HIDDEN))
        {
            do_temp_chart_update();
        }

        update_overall_label();

        Lvgl_unlock();
    }
}

void InitializeButtons(void)
{
    boot_button->OnClick([]()
                         {
                             ESP_LOGI(TAG, "boot_button click");
                             xEventGroupSetBits(FacTestEventGroup, (0x10) | LED_BLINK_BIT); });

    boot_button->OnDoubleClick([]()
                               {
                                    ESP_LOGI(TAG, "boot_button double click");
                                    xEventGroupSetBits(FacTestEventGroup, (0x20) | LED_BLINK_BIT); });

    boot_button->OnLongPress([]()
                             {
                                 ESP_LOGI(TAG, "boot_button long press");
                                 xEventGroupSetBits(FacTestEventGroup, (0x40) | LED_BLINK_BIT); });

    power_button->OnClick([]()
                          {
                              ESP_LOGI(TAG, "power_button click");
                              xEventGroupSetBits(FacTestEventGroup, (0x01) | LED_BLINK_BIT); });

    power_button->OnDoubleClick([]()
                                {
                                    ESP_LOGI(TAG, "power_button double click");
                                    xEventGroupSetBits(FacTestEventGroup, (0x02) | LED_BLINK_BIT); });

    power_button->OnLongPress([]()
                              {
                                  ESP_LOGI(TAG, "power_button long press");
                                  xEventGroupSetBits(FacTestEventGroup, (0x04) | LED_BLINK_BIT); });
}

void ButtonEvent_PowerKeyClick(void)
{
    static uint16_t click_count = 0;
    click_count++;

    show_container(click_count % 4 + 1);
}

void ButtonEvent_PowerKeyDoubleClick(void)
{
    show_container(1);
}

void ButtonEvent_PowerKeyLongPress(void)
{
    BoardPower_VBAT_OFF();
}

void ButtonEvent_BootKeyClick(void)
{
    // if being container 1 showing, then update the label text, otherwise do nothing.
    if (!lv_obj_has_flag(scr_ui.container_home, LV_OBJ_FLAG_HIDDEN))
    {
        overallInfoPageNumber++;
        overallInfoPageNumber %= 2;
    }

    // for container 4
    if (!lv_obj_has_flag(scr_ui.container_temp_chart, LV_OBJ_FLAG_HIDDEN))
    {
        // hide chart and static label
        if (Lvgl_lock(portMAX_DELAY))
        {
            // hide chart and static label
            if (scr_ui.temp_chart)
                lv_obj_add_flag(scr_ui.temp_chart, LV_OBJ_FLAG_HIDDEN);
            if (scr_ui.label_tempchart_time)
                lv_obj_add_flag(scr_ui.label_tempchart_time, LV_OBJ_FLAG_HIDDEN);
            if (scr_ui.label_start_temp)
                lv_obj_add_flag(scr_ui.label_start_temp, LV_OBJ_FLAG_HIDDEN);
            if (scr_ui.label_end_temp)
                lv_obj_add_flag(scr_ui.label_end_temp, LV_OBJ_FLAG_HIDDEN);
            // remove detail hidden flag to show the detail label
            if (scr_ui.label_temp_list)
                lv_obj_remove_flag(scr_ui.label_temp_list, LV_OBJ_FLAG_HIDDEN);
            Lvgl_unlock();
        }

        // copy all temperature records to temp_record buffer and update the detail label text according to tempDetailPageNumber
        user_data_t temp_record[MAX_TEMP_FIFO_SIZE];
        fifo_CopyData(&tempDataFifo, temp_record, MAX_TEMP_FIFO_SIZE);

        char buf[256] = "";
        char record_buf[64];
        if (tempDetailPageNumber >= MAX_TEMP_DETAIL_TOTAL_PAGE)
        {
            snprintf(record_buf, sizeof(record_buf), "\nMax: %0.1f°C\n", max_temp);
            strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
            snprintf(record_buf, sizeof(record_buf), "Min: %0.1f°C\n", min_temp);
            strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
            tempDetailPageNumber = MAX_TEMP_DETAIL_TOTAL_PAGE;
        }
        else
        {
            int beginIndex = tempDetailPageNumber * 10;
            snprintf(record_buf, sizeof(record_buf), "Page:%d from:%d\n\n", tempDetailPageNumber, beginIndex);
            strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
            for (int i = 0; i < 10; ++i)
            {
                user_data_t record = temp_record[beginIndex + i];
                snprintf(record_buf, sizeof(record_buf), "%02d  %02d:%02d:%02d>%.1f°C\n", i+1, record.timestamp.hour, record.timestamp.min, record.timestamp.sec, record.temperature);
                strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
            }
        }

        ESP_LOGI(TAG, "tempDetailPageNumber: %d", tempDetailPageNumber);
        ESP_LOGI(TAG, "size of detail: %d, content: \n%s", strlen(buf), buf);
        if (Lvgl_lock(portMAX_DELAY))
        {
            lv_label_set_text(scr_ui.label_temp_list, buf);
            Lvgl_unlock();
        }

        if (--tempDetailPageNumber <= (MAX_TEMP_DETAIL_TOTAL_PAGE - 3)) // Only latest 3 pages needed
        {
            tempDetailPageNumber = MAX_TEMP_DETAIL_TOTAL_PAGE;
        }
    }
}

void ButtonEvent_BootKeyDoubleClick(void)
{
    show_container(2);
}

void ButtonEvent_BootKeyLongPress(void)
{
}

void Task_key_loop(void *arg)
{
    for (;;)
    {
        EventBits_t even = xEventGroupWaitBits(FacTestEventGroup, (0x01) | (0x02) | (0x04) | (0x10) | (0x20) | (0x40), pdTRUE, pdFALSE, portMAX_DELAY);
        if (even & 0x01)
        {
            ButtonEvent_PowerKeyClick();
        }
        if (even & 0x02)
        {
            ButtonEvent_PowerKeyDoubleClick();
        }
        if (even & 0x04)
        {
            ButtonEvent_PowerKeyLongPress();
        }

        if (even & 0x10)
        {
            ButtonEvent_BootKeyClick();
        }
        if (even & 0x20)
        {
            ButtonEvent_BootKeyDoubleClick();
        }
        if (even & 0x40)
        {
            ButtonEvent_BootKeyLongPress();
        }
    }
}

void Task_touch_loop(void *arg)
{
    uint32_t io_num;
    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            if (lv_obj_has_flag(scr_ui.container_setting, LV_OBJ_FLAG_HIDDEN))
            {
                ESP_LOGI(TAG, "touched");
                continue;
            }

            uint16_t x, y;
            if (ft6336_dev->GetTouchPoint(&x, &y))
            {
                if (x < 80 && y < 80)
                {
                    ESP_LOGI(TAG, "Touch button event: NEXT PERIOD clicked at (%d,%d)", x, y);
                    if (Lvgl_lock(portMAX_DELAY))
                    {
                        touch_on_next();
                        Lvgl_unlock();
                    }
                }
                else if (x >= 119 && x < 199 && y < 80)
                {
                    ESP_LOGI(TAG, "Touch button event: ACTIVATE clicked at (%d,%d)", x, y);
                    if (Lvgl_lock(portMAX_DELAY))
                    {
                        touch_on_active_button();
                        Lvgl_unlock();
                    }
                }
                else if (x < 80 && y >= 118 && y < 198)
                {
                    ESP_LOGI(TAG, "Touch button event: MORE SETTING clicked at (%d,%d)", x, y);
                    if (Lvgl_lock(portMAX_DELAY))
                    {
                        touch_on_more_button();
                        Lvgl_unlock();
                    }
                }
                else if (x >= 119 && x < 199 && y >= 118 && y < 198)
                {
                    ESP_LOGI(TAG, "Touch button event: CANCEL clicked at (%d,%d)", x, y);
                    if (Lvgl_lock(portMAX_DELAY))
                    {
                        touch_on_cancel_button();
                        Lvgl_unlock();
                    }
                }
                // ESP_LOGW("touch", "(%d,%d)", x, y);
            }
        }
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

void Touch_ISR_GPIO_Init()
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << EPD_TP_INT_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(EPD_TP_INT_PIN, gpio_isr_handler, (void *)EPD_TP_INT_PIN);
    gpio_evt_queue = xQueueCreate(3, sizeof(uint32_t));
}
