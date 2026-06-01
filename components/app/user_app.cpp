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

#define TAG "user_app"
#define LED_BLINK_BIT 0x80

static I2cMasterBus *i2c_bus = NULL;
static I2cFt6336Dev *ft6336_dev = NULL;
static lv_factest_ui src_ui;
static pcf85063a_dev_t pcf85063;  //  rtc句柄
static bool pcf85063initflag = 1; //  rtc初始化成功标志位
static Button *boot_button = nullptr;
static Button *power_button = nullptr;
static EventGroupHandle_t FacTestEventGroup = NULL; // 事件组句柄
static char Lvgl_buffer[60];
static QueueHandle_t gpio_evt_queue = NULL;
static uint8_t *audio_data_ptr = NULL;
QueueHandle_t xTempDataQueue = NULL;

int overallInfoPageNumber = 1;
static float max_temp = 0;
static float min_temp = 100;

static lv_timer_t *temp_timer = NULL;
static lv_timer_t *time_timer = NULL;
static const uint32_t temp_period_list[] = {3000, 20000, 60000, 300000};
static const size_t temp_period_count = sizeof(temp_period_list) / sizeof(temp_period_list[0]);
static size_t temp_period_selected_index = 0; // default selected 3000ms
static size_t temp_period_active_index = 0;   // default active 3000ms
static uint32_t temp_sample_count = 0;
uint16_t TEMP_OFFSET = 20;
const uint16_t TEMP_SCALER = 3;

#define MAX_TEMP_QUEUE_SIZE 100

static void UpdateMainInfoLabel(void);
static void UpdateTouchPeriodStatusLabel(const char *action);
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
    temp_record_t record = {
        .temperature = t,
        .ts = {
            .year = current_time.year,
            .month = current_time.month,
            .day = current_time.day,
            .hour = current_time.hour,
            .minute = current_time.min,
            .second = current_time.sec,
        }};

    // send to temp queue
    if (xQueueSend(xTempDataQueue, &record, 0) != pdPASS)
    {
        ESP_LOGW(TAG, "Temperature queue is full, dropping data");
    }
    else
    {
        temp_sample_count++;
    }

    ESP_LOGI(TAG, "temperature updated: %ld", temp_sample_count);
}

static void time_update_timer_cb(lv_timer_t *timer)
{
    pcf85063a_datetime_t current_time = {};
    pcf85063a_get_time_date(&pcf85063, &current_time);

    /* Only update the on-screen time when container 1 is visible */
    if (!lv_obj_has_flag(src_ui.container_home, LV_OBJ_FLAG_HIDDEN) && src_ui.label_homeClock)
    {
        snprintf(Lvgl_buffer, sizeof(Lvgl_buffer), "%02d:%02d:%02d", current_time.hour, current_time.min, current_time.sec);
        lv_label_set_text(src_ui.label_homeClock, Lvgl_buffer);
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

    // initialize temperature queue
    xTempDataQueue = xQueueCreate(MAX_TEMP_QUEUE_SIZE, sizeof(temp_record_t));
    assert(xTempDataQueue);
}

static void ShowOnlyContainer(int container_number)
{
    lv_obj_t *containers[4] = {
        src_ui.container_home,
        src_ui.container_image,
        src_ui.container_touchButton,
        src_ui.container_tempChart,
    };

    ESP_LOGI(TAG, "container: %d", container_number);

    if (Lvgl_lock(portMAX_DELAY))
    {
        // make all containers hidden first
        lv_obj_add_flag(src_ui.container_tempChart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(src_ui.container_home, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(src_ui.container_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(src_ui.container_touchButton, LV_OBJ_FLAG_HIDDEN);
        if (container_number >= 1 && container_number <= 4)
        {
            lv_obj_remove_flag(containers[container_number - 1], LV_OBJ_FLAG_HIDDEN);
        }

        if (container_number == 3)
        {
            UpdateTouchPeriodStatusLabel(NULL);
        }
        Lvgl_unlock();
    }
    if (container_number == 1)
    {
        UpdateMainInfoLabel();
    }
}

void UpdateMainInfoLabel(void)
{
    if (!src_ui.label_overall_info)
    {
        return;
    }
    uint32_t active_s = temp_period_list[temp_period_active_index] / 1000;
    char buf[140] = "";
    if (overallInfoPageNumber == 1)
    {
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "Power: %d%%", Get_Batterylevel());
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\n\nSample period: %lu", active_s);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nCount: %lu", temp_sample_count);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nTemp offset: %u", TEMP_OFFSET);
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
        lv_label_set_text(src_ui.label_overall_info, buf);
        Lvgl_unlock();
    }
}

static void UpdateTouchPeriodStatusLabel(const char *action)
{
    if (!src_ui.label_touchEvent)
    {
        return;
    }

    uint32_t selected_s = temp_period_list[temp_period_selected_index] / 1000;
    uint32_t active_s = temp_period_list[temp_period_active_index] / 1000;
    char buf[80];

    if (action)
    {
        snprintf(buf, sizeof(buf), "%s | Sel:%lus Act:%lus", action, selected_s, active_s);
    }
    else
    {
        snprintf(buf, sizeof(buf), "Sel:%lus Act:%lus", selected_s, active_s);
    }

    lv_label_set_text(src_ui.label_touchEvent, buf);
}

static void TouchContainer_NextPeriod(void)
{
    temp_period_selected_index = (temp_period_selected_index + 1) % temp_period_count;
    UpdateTouchPeriodStatusLabel("Next");
}

static void TouchContainer_ActivatePeriod(void)
{
    temp_period_active_index = temp_period_selected_index;
    if (temp_timer)
    {
        lv_timer_set_period(temp_timer, temp_period_list[temp_period_active_index]);
        UpdateTouchPeriodStatusLabel("Go");
    }
    else
    {
        UpdateTouchPeriodStatusLabel("No Timer");
    }
}

static void TouchContainer_CancelSelection(void)
{
    temp_period_selected_index = temp_period_active_index;
    UpdateTouchPeriodStatusLabel("Cancel");
}

static void TouchContainer_MoreSetting(void)
{
    UpdateTouchPeriodStatusLabel("More");
}

void UserUi_Init()
{
    setup_factest_ui(&src_ui);

    // Show only container 1 after power on.
    ShowOnlyContainer(1);

    UpdateTouchPeriodStatusLabel(NULL);

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
    temp_record_t temp_record[MAX_TEMP_QUEUE_SIZE];
    temp_record_t tmpTemp;
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
    if (src_ui.label_startTime)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "FS: %02d-%02d %02d:%02d:%02d", current_time.month, current_time.day, current_time.hour, current_time.min, current_time.sec);
        lv_label_set_text(src_ui.label_startTime, buf);
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        times++;
        if (times < 3)
        {
            continue;
        }
        times = 0;

        // receive all temperature data from queue and update chart
        while (xQueueReceive(xTempDataQueue, &tmpTemp, 0) == pdPASS)
        {
            // step in one new data in temp_record buffer, keep the latest in the end of the buffer
            for (int i = 1; i < MAX_TEMP_QUEUE_SIZE; ++i)
            {
                temp_record[i - 1] = temp_record[i];
            }
            temp_record[MAX_TEMP_QUEUE_SIZE - 1] = tmpTemp;
        }

        // update temperature chart with temp_record buffer
        if (Lvgl_lock(portMAX_DELAY))
        {
            for (int i = 0; i < MAX_TEMP_QUEUE_SIZE; ++i)
            {
                temp = temp_record[i].temperature;
                totalValue += temp;

                temp = (temp - TEMP_OFFSET) * TEMP_SCALER;
                lv_chart_set_next_value(src_ui.temp_chart, src_ui.temp_series, temp);

                if (temp_record[i].temperature > max_temp)
                {
                    max_temp = temp_record[i].temperature;
                }
                if (temp_record[i].temperature < min_temp)
                {
                    min_temp = temp_record[i].temperature;
                }
                if (min_temp < 5) // wrong data, set it to a reasonable default value
                {
                    min_temp = 100;
                }
            }
            totalValue = totalValue / MAX_TEMP_QUEUE_SIZE + 0.5;
            TEMP_OFFSET = (uint16_t)((totalValue * 0.8) * 0.2 + TEMP_OFFSET * 0.8);

            // if the container 1 is showing, then update the label text, otherwise do nothing.
            if (!lv_obj_has_flag(src_ui.container_tempChart, LV_OBJ_FLAG_HIDDEN))
            {
                lv_obj_invalidate(src_ui.temp_chart);
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f<%.1f<%.1f°C %02d:%02d:%02d", max_temp, tmpTemp.temperature, min_temp, tmpTemp.ts.hour, tmpTemp.ts.minute, tmpTemp.ts.second);

                if (src_ui.lable_tempStatics)
                {
                    lv_label_set_text(src_ui.lable_tempStatics, buf);
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
    if (lv_obj_has_flag(src_ui.container_home, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }
    overallInfoPageNumber++;
    overallInfoPageNumber %= 2;
    UpdateMainInfoLabel();
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
            if (lv_obj_has_flag(src_ui.container_touchButton, LV_OBJ_FLAG_HIDDEN))
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
