#include <stdio.h>
#include <freertos/FreeRTOS.h>
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

static int settingTarget = 0;

int overallInfoPageNumber = 1;
int tempDetailPageNumber = 1;

static float max_temp = 0;
static float min_temp = 100;

static lv_timer_t *temp_timer = NULL;
static lv_timer_t *time_timer = NULL;
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

#define MAX_TEMP_FIFO_SIZE 100

static void UpdateOverallInfoLabel(void);
void Led_LoopTask(void *arg);
void Lvgl_LoopTask(void *arg);
void InitializeButtons(void); /* button 初始化 */
void Button_LoopTask(void *arg);
void Touch_LoopTask(void *arg);
static void gpio_isr_handler(void *arg);
void Touch_ISR_GPIO_Init();


static void temp_update_timer_cb(lv_timer_t *timer)
{
    pcf85063a_datetime_t current_time = {};
    float t, h;
    Shtc3_ReadTempHumi(&t, &h);
    if (t == -1000 || h == -1000)
    {
        ESP_LOGE(TAG, "Failed to read temperature and humidity");
        return;
    }
    pcf85063a_get_time_date(&pcf85063, &current_time);
    user_data_t ud;
    // t = simuTemp++;
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
    ESP_LOGI(TAG, "New temp: %ld", temp_sample_count);
}

static void time_update_timer_cb(lv_timer_t *timer)
{
    pcf85063a_datetime_t current_time = {};
    pcf85063a_get_time_date(&pcf85063, &current_time);

    /* Only update the on-screen time when container 1 is visible */
    if (!lv_obj_has_flag(scr_ui.container_home, LV_OBJ_FLAG_HIDDEN) && (overallInfoPageNumber == 1) && scr_ui.label_homeClock)
    {
        snprintf(Lvgl_buffer, sizeof(Lvgl_buffer), "%02d:%02d:%02d", current_time.hour, current_time.min, current_time.sec);
        lv_label_set_text(scr_ui.label_homeClock, Lvgl_buffer);
    }
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
    Touch_ISR_GPIO_Init();
    Codec_StartInit();
    user_data_t *buf = (user_data_t *)heap_caps_malloc(sizeof(user_data_t) * MAX_TEMP_FIFO_SIZE, MALLOC_CAP_SPIRAM);
    lv_memset(buf, 0, sizeof(user_data_t) * MAX_TEMP_FIFO_SIZE);
    fifo_CreateLiteFifo(&tempDataFifo, MAX_TEMP_FIFO_SIZE, buf);
}

static void ShowOnlyContainer(int container_number)
{
    lv_obj_t *containers[4] = {
        scr_ui.container_home,
        scr_ui.container_image,
        scr_ui.container_setting,
        scr_ui.container_tempChart,
    };

    ESP_LOGI(TAG, "container: %d", container_number);

    if (Lvgl_lock(portMAX_DELAY))
    {
        // make all containers hidden first
        lv_obj_add_flag(scr_ui.container_tempChart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scr_ui.container_home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scr_ui.container_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scr_ui.container_setting, LV_OBJ_FLAG_HIDDEN);
        if (container_number >= 1 && container_number <= 4)
        {
            lv_obj_remove_flag(containers[container_number - 1], LV_OBJ_FLAG_HIDDEN);
        }

        if (container_number == 3)
        {
            char buf[80];
            if (settingTarget == 0)
            {
                // print current selected sample interval to the label
                uint32_t selected = temp_period_list[temp_period_selected_index] / 1000;
                snprintf(buf, sizeof(buf), "Sample Intv Sel:%lu", selected);
                lv_label_set_text(scr_ui.label_touchEvent, buf);
            }
            else if (settingTarget == 1)
            {
                // print current selected chart point count to the label
                snprintf(buf, sizeof(buf), "Chart Point Sel:%lu", chartPointList[chartPointsSelectedIndex]);
                lv_label_set_text(scr_ui.label_touchEvent, buf);
            }
        }

        if (container_number == 4)
        {
            // unhidden temp chart
            lv_obj_remove_flag(scr_ui.temp_chart, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(scr_ui.lable_tempStatics, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(scr_ui.lable_tempDetail, LV_OBJ_FLAG_HIDDEN);
        }

        Lvgl_unlock();
    }
    if (container_number == 1)
    {
        UpdateOverallInfoLabel();
    }
}

void UpdateOverallInfoLabel(void)
{
    if (!scr_ui.label_overall_info)
    {
        return;
    }
    char buf[140] = "";
    if (overallInfoPageNumber == 1)
    {
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "Power: %d%%", Get_Batterylevel());
        uint32_t active_s = temp_period_list[temp_period_active_index] / 1000;
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\n\nSample period: %lu", active_s);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nCount: %lu", temp_sample_count);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp offset: %u", TEMP_OFFSET);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp scaler: %u", TEMP_SCALER);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nChart size: %lu", chartPointList[chartPointsActiveIndex]);
    }
    else
    {
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "KEYS:");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nBOOT: show max/min");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nBOOT DOUBLE: home");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nPOWER: next page");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nPOWER LONG: power off");
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nPOWER DOUBLE: show chart");
    }

    if (Lvgl_lock(portMAX_DELAY))
    {
        lv_label_set_text(scr_ui.label_overall_info, buf);
        Lvgl_unlock();
    }
}

static void TouchContainer_NextPeriod(void)
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
    lv_label_set_text(scr_ui.label_touchEvent, buf);
}

static void TouchContainer_ActivatePeriod(void)
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

    lv_label_set_text(scr_ui.label_touchEvent, "actived");
}

static void TouchContainer_CancelSelection(void)
{

    char buf[80] = "default";
    if (settingTarget == 0)
    {
        temp_period_selected_index = temp_period_active_index;
        uint32_t selected = temp_period_list[temp_period_selected_index] / 1000;
        snprintf(buf, sizeof(buf), "Sample Intv | Sel:%lu", selected);
        lv_label_set_text(scr_ui.label_touchEvent, buf);
    }
    else if (settingTarget == 1)
    {
        chartPointsSelectedIndex = chartPointsActiveIndex;
        snprintf(buf, sizeof(buf), "Chart Point | Sel:%lu", chartPointList[chartPointsSelectedIndex]);
    }
    lv_label_set_text(scr_ui.label_touchEvent, buf);
}

static void TouchContainer_MoreSetting(void)
{
    if (settingTarget == 0)
    {
        // toggle chart point count between 10 and 30 for testing
        settingTarget = 1;
        char buf[80];
        snprintf(buf, sizeof(buf), "Chart Point Mode:%lu", chartPointList[chartPointsSelectedIndex]);
        lv_label_set_text(scr_ui.label_touchEvent, buf);
    }
    else if (settingTarget == 1)
    {
        settingTarget = 0;
        char buf[80];
        uint32_t selected = temp_period_list[temp_period_selected_index] / 1000;
        snprintf(buf, sizeof(buf), "Sample Intv Mode:%lu", selected);
        lv_label_set_text(scr_ui.label_touchEvent, buf);
    }
}

void UserUi_Init()
{
    setup_factest_ui(&scr_ui);

    // Show only container 1 after power on.
    ShowOnlyContainer(1);
    temp_timer = lv_timer_create(temp_update_timer_cb, temp_period_list[temp_period_active_index], NULL);
    lv_timer_set_repeat_count(temp_timer, -1);
    temp_update_timer_cb(0);
    /* create 1s timer to update time label when container 1 is shown */
    time_timer = lv_timer_create(time_update_timer_cb, 1000, NULL);
    lv_timer_set_repeat_count(time_timer, -1);
    time_update_timer_cb(0);
}

void UserApp_Start_Init()
{
    xTaskCreatePinnedToCore(Led_LoopTask, "Led_LoopTask", 4 * 1024, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(Lvgl_LoopTask, "Lvgl_LoopTask", 5 * 1024, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(Button_LoopTask, "Button_LoopTask", 4 * 1024, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(Touch_LoopTask, "Touch_LoppTask", 4 * 1024, NULL, 2, NULL, 1);
}

void Led_LoopTask(void *arg)
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

void Lvgl_LoopTask(void *arg)
{
    uint32_t times = 0;
    user_data_t temp_record[MAX_TEMP_FIFO_SIZE];
    user_data_t tmpTemp;
    float temp = 0;
    float totalValue = 0;

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
    if (scr_ui.lable_tempDetail)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "FS: %02d-%02d %02d:%02d:%02d", current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec);
        lv_label_set_text(scr_ui.lable_tempDetail, buf);
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        times++;
        if (times < 10)
        {
            continue;
        }
        times = 0;

        fifo_CopyData(&tempDataFifo, temp_record, MAX_TEMP_FIFO_SIZE);
        // dump all temperature data for debug
        for (int i = 0; i < MAX_TEMP_FIFO_SIZE; ++i)
        {
            ESP_LOGI(TAG, "temp_record[%d]: temp=%.1f, time=%02d:%02d:%02d", i, temp_record[i].temperature, temp_record[i].timestamp.hour, temp_record[i].timestamp.min, temp_record[i].timestamp.sec);
        }

        tmpTemp = temp_record[MAX_TEMP_FIFO_SIZE - 1];
        // update temperature chart with temp_record buffer
        if (Lvgl_lock(portMAX_DELAY))
        {
            for (int i = 0; i < MAX_TEMP_FIFO_SIZE; ++i)
            {
                temp = temp_record[i].temperature;
                totalValue += temp;
                temp = (temp - TEMP_OFFSET) * TEMP_SCALER;
                lv_chart_set_next_value(scr_ui.temp_chart, scr_ui.temp_series, temp);
            }

            totalValue = totalValue / MAX_TEMP_FIFO_SIZE + 0.5;
            TEMP_OFFSET = (uint16_t)((totalValue * 0.8) * 0.2 + TEMP_OFFSET * 0.8);

            // if the container 1 is showing, then update the label text, otherwise do nothing.
            if (!lv_obj_has_flag(scr_ui.container_tempChart, LV_OBJ_FLAG_HIDDEN))
            {
                lv_obj_invalidate(scr_ui.temp_chart);
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f<%.1f<%.1f°C %02d:%02d:%02d", max_temp, tmpTemp.temperature, min_temp, tmpTemp.timestamp.hour, tmpTemp.timestamp.min, tmpTemp.timestamp.sec);

                if (scr_ui.lable_tempStatics)
                {
                    lv_label_set_text(scr_ui.lable_tempStatics, buf);
                }
                // print log for debug
                ESP_LOGI(TAG, "chart updated");
            }
            else // redraw the chart of temperature data
            {

                ESP_LOGI(TAG, "chart is hidden");
            }
            Lvgl_unlock();
        }
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

    ShowOnlyContainer(click_count % 4 + 1);
}

void ButtonEvent_PowerKeyDoubleClick(void)
{
    ShowOnlyContainer(4);
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
        UpdateOverallInfoLabel();
    }

    if (!lv_obj_has_flag(scr_ui.container_tempChart, LV_OBJ_FLAG_HIDDEN))
    {
        // hide chart and static lable
        lv_obj_add_flag(scr_ui.temp_chart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scr_ui.lable_tempStatics, LV_OBJ_FLAG_HIDDEN);
        // remove detail hidden flag to show the detail lable
        lv_obj_remove_flag(scr_ui.lable_tempDetail, LV_OBJ_FLAG_HIDDEN);

        // copy all temperature records to temp_record buffer and update the detail label text according to tempDetailPageNumber
        user_data_t temp_record[MAX_TEMP_FIFO_SIZE];
        fifo_CopyData(&tempDataFifo, temp_record, MAX_TEMP_FIFO_SIZE);

        // log the value of tempDetailPageNumber
        ESP_LOGI(TAG, "tempDetailPageNumber: %d", tempDetailPageNumber);

        if (tempDetailPageNumber == 1)
        {
            // show all temperature records in detail label
            // char buf[512] = "";
            // for (int i = 0; i < 10; ++i)
            // {
            //     user_data_t record = temp_record[i];
            //     char record_buf[64];
            //     snprintf(record_buf, sizeof(record_buf), "%02d:%02d:%02d - %.1f°C\n", record.timestamp.hour, record.timestamp.min, record.timestamp.sec, record.temperature);
            //     strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
            // }
            // lv_label_set_text(scr_ui.lable_tempDetail, buf);
            lv_label_set_text(scr_ui.lable_tempDetail, "Showing latest 10 records, click to show again");
        }
        // else if (tempDetailPageNumber == 2)
        // {
        //     // show all temperature records in detail label
        //     char buf[512] = "";
        //     for (int i = 10; i < 20; ++i)
        //     {
        //         user_data_t record = temp_record[i];
        //         char record_buf[64];
        //         snprintf(record_buf, sizeof(record_buf), "%02d:%02d:%02d - %.1f°C\n", record.timestamp.hour, record.timestamp.min, record.timestamp.sec, record.temperature);
        //         strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
        //     }
        //     lv_label_set_text(scr_ui.lable_tempDetail, buf);
        // }
        else
        {
            tempDetailPageNumber = 0;
            // show only the latest temperature record in detail label
            user_data_t latest_record = temp_record[MAX_TEMP_FIFO_SIZE - 1];
            char buf[64];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d - %.1f°C", latest_record.timestamp.hour, latest_record.timestamp.min, latest_record.timestamp.sec, latest_record.temperature);
            lv_label_set_text(scr_ui.lable_tempDetail, buf);
        }

        tempDetailPageNumber++;
    }
}

void ButtonEvent_BootKeyDoubleClick(void)
{
    ShowOnlyContainer(1);
}

void ButtonEvent_BootKeyLongPress(void)
{
}

void Button_LoopTask(void *arg)
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

void Touch_LoopTask(void *arg)
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
                        TouchContainer_NextPeriod();
                        Lvgl_unlock();
                    }
                }
                else if (x >= 119 && x < 199 && y < 80)
                {
                    ESP_LOGI(TAG, "Touch button event: ACTIVATE clicked at (%d,%d)", x, y);
                    if (Lvgl_lock(portMAX_DELAY))
                    {
                        TouchContainer_ActivatePeriod();
                        Lvgl_unlock();
                    }
                }
                else if (x < 80 && y >= 118 && y < 198)
                {
                    ESP_LOGI(TAG, "Touch button event: MORE SETTING clicked at (%d,%d)", x, y);
                    if (Lvgl_lock(portMAX_DELAY))
                    {
                        TouchContainer_MoreSetting();
                        Lvgl_unlock();
                    }
                }
                else if (x >= 119 && x < 199 && y >= 118 && y < 198)
                {
                    ESP_LOGI(TAG, "Touch button event: CANCEL clicked at (%d,%d)", x, y);
                    if (Lvgl_lock(portMAX_DELAY))
                    {
                        TouchContainer_CancelSelection();
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
