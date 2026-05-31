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

static float max_temp = 0;
static float min_temp = 100;

static lv_timer_t *temp_timer = NULL;
static const uint32_t temp_period_list[] = {2000, 20000, 60000, 300000};
static const size_t temp_period_count = sizeof(temp_period_list) / sizeof(temp_period_list[0]);
static size_t temp_period_selected_index = 2; // default selected 3000ms
static size_t temp_period_active_index = 2;   // default active 3000ms

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
        src_ui.screen_cont_1,
        src_ui.screen_cont_2,
        src_ui.screen_cont_3,
        src_ui.temp_chart_container,
    };

    ESP_LOGI(TAG, "container: %d", container_number);

    if (Lvgl_lock(portMAX_DELAY))
    {

        // make all containers hidden first
        lv_obj_add_flag(src_ui.temp_chart_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(src_ui.screen_cont_1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(src_ui.screen_cont_2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(src_ui.screen_cont_3, LV_OBJ_FLAG_HIDDEN);
        if (container_number >= 1 && container_number <= 4)
        {
            lv_obj_remove_flag(containers[container_number - 1], LV_OBJ_FLAG_HIDDEN);
        }

        if (container_number == 1)
        {
            UpdateMainInfoLabel();
        }
        else if (container_number == 3)
        {
            UpdateTouchPeriodStatusLabel(NULL);
        }
        Lvgl_unlock();
    }
}

void UpdateMainInfoLabel(void)
{
    if (!src_ui.label_overall_info)
    {
        return;
    }

    uint32_t active_s = temp_period_list[temp_period_active_index] / 1000;
    char buf[140];
    snprintf(buf, sizeof(buf),
             "Welcome!\n\nBOOT: show max/min\nPOWER: next page\nLONG POWER: power off\nDOUBLE POWER: show chart\n\nTemp sample: %lus",
             active_s);
    lv_label_set_text(src_ui.label_overall_info, buf);
}

static void UpdateTouchPeriodStatusLabel(const char *action)
{
    if (!src_ui.screen_label_23)
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

    lv_label_set_text(src_ui.screen_label_23, buf);
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
    UpdateMainInfoLabel();
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

    pcf85063a_datetime_t current_time = {};
    pcf85063a_get_time_date(&pcf85063, &current_time);
    snprintf(Lvgl_buffer, sizeof(Lvgl_buffer), "%02d-%02d-%02d %02d:%02d", current_time.year, current_time.month, current_time.day, current_time.hour, current_time.min);
    lv_label_set_text(src_ui.screen_label_temp_info, Lvgl_buffer);

    UpdateMainInfoLabel();
    UpdateTouchPeriodStatusLabel(NULL);

    temp_timer = lv_timer_create(temp_update_timer_cb, temp_period_list[temp_period_active_index], NULL);
    lv_timer_set_repeat_count(temp_timer, -1);
    temp_update_timer_cb(0);
    // lv_timer_set_period(temp_update_timer_cb, new_period_ms);
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
    for (;;)
    {
        gpio_set_level(LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void Lvgl_LoopTask(void *arg)
{
    uint32_t times = 0;
    // uint32_t shtc3_time = 0;
    // uint32_t rtc_time = 0;
    // uint32_t adc_time = 0;
    temp_record_t temp_record[MAX_TEMP_QUEUE_SIZE];
    temp_record_t tmpTemp;

    pcf85063a_datetime_t datatime = {};
    datatime.year = 2026;
    datatime.month = 1;
    datatime.day = 1;
    datatime.hour = 8;
    datatime.min = 0;
    datatime.sec = 0;
    pcf85063a_set_time_date(&pcf85063, datatime);

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
                lv_chart_set_next_value(src_ui.temp_chart, src_ui.temp_series, temp_record[i].temperature);

                if (temp_record[i].temperature > max_temp)
                {
                    max_temp = temp_record[i].temperature;
                }
                if (temp_record[i].temperature < min_temp)
                {
                    min_temp = temp_record[i].temperature;
                }
                if (min_temp == 0) // wrong data, set it to a reasonable default value
                {
                    min_temp = 100;
                }
            }

            // if the container 1 is showing, then update the label text, otherwise do nothing.
            if (!lv_obj_has_flag(src_ui.temp_chart_container, LV_OBJ_FLAG_HIDDEN))
            {
                lv_obj_invalidate(src_ui.temp_chart);
                int temp = (int)tmpTemp.temperature;
                char buf[32];
                snprintf(buf, sizeof(buf), "%d<%d<%d°C %02d-%02d %02d:%02d", (int)max_temp, temp, (int)min_temp, tmpTemp.ts.month, tmpTemp.ts.day, tmpTemp.ts.hour, tmpTemp.ts.minute);

                if (src_ui.temp_label)
                {
                    lv_label_set_text(src_ui.temp_label, buf);
                }
                // print log for debug
                ESP_LOGI(TAG, "Temperature updated");
            }
            else // redraw the chart of temperature data
            {

                ESP_LOGI(TAG, "temperature chart is hidden");
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
                             xEventGroupSetBits(FacTestEventGroup, (0x10)); });

    boot_button->OnDoubleClick([]()
                               {
                                    ESP_LOGI(TAG, "boot_button double click");
                                    xEventGroupSetBits(FacTestEventGroup, (0x20)); });

    boot_button->OnLongPress([]()
                             {
                                 ESP_LOGI(TAG, "boot_button long press");
                                 xEventGroupSetBits(FacTestEventGroup, (0x40)); });

    power_button->OnClick([]()
                          {
                              ESP_LOGI(TAG, "power_button click");
                              xEventGroupSetBits(FacTestEventGroup, (0x01)); });

    power_button->OnDoubleClick([]()
                                {
                                    ESP_LOGI(TAG, "power_button double click");
                                    xEventGroupSetBits(FacTestEventGroup, (0x02)); });

    power_button->OnLongPress([]()
                              {
                                  ESP_LOGI(TAG, "power_button long press");
                                  xEventGroupSetBits(FacTestEventGroup, (0x04)); });
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
    if (lv_obj_has_flag(src_ui.screen_cont_1, LV_OBJ_FLAG_HIDDEN))
    {
        return;
    }

    char overall_text[64] = "";
    snprintf(overall_text, sizeof(overall_text), "Double Clicked!Max: %.1f°C, Min: %.1f°C", max_temp, min_temp);
    // update label text in container 1
    if (Lvgl_lock(portMAX_DELAY))
    {
        lv_label_set_text(src_ui.label_overall_info, overall_text);
        Lvgl_unlock();
    }
}

void ButtonEvent_BootKeyDoubleClick(void)
{
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
            // if (1 == touchflag)
            {
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
                    ESP_LOGW("touch", "(%d,%d)", x, y);
                }
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
