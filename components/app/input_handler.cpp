#include "input_handler.h"

#include <stdio.h>

#include <driver/gpio.h>
#include <esp_log.h>

#include "button.h"
#include "chart_controller.h"
#include "epaper_config.h"
#include "port_ft6336.h"
#include "port_i2c.h"
#include "port_lvgl.h"
#include "port_power.h"
#include "temp_sampler.h"
#include "setting_actions_iface.h"

#define TAG "input"

#define SETTING_MODE_CONFIG_MAX SETTING_MODE_SENSOR_SRC
#define TOUCH_DEBOUNCE_MS         30
#define TOUCH_COOLDOWN_MS         300
#define TOUCH_RELEASE_TIMEOUT_MS  1000
#define TOUCH_RELEASE_POLL_MS     20
#define TOUCH_QUEUE_LEN           8

static Button *s_boot_button = nullptr;
static Button *s_power_button = nullptr;
static QueueHandle_t s_gpio_evt_queue = NULL;
static input_handler_config_t s_cfg = {};
static TaskHandle_t s_key_task = NULL;
static TaskHandle_t s_touch_task = NULL;

static int s_setting_target_id = 0;
static const uint32_t s_temp_period_list[] = {3000, 20000, 60000, 300000};
static const uint32_t s_chart_point_list[] = {20, 40, 100};
static const size_t s_temp_period_count = sizeof(s_temp_period_list) / sizeof(s_temp_period_list[0]);
static const size_t s_chart_point_count = sizeof(s_chart_point_list) / sizeof(s_chart_point_list[0]);

static size_t s_temp_period_selected_index = 0;
static size_t s_temp_period_active_index = 0;
static size_t s_chart_points_selected_index = 0;
static size_t s_chart_points_active_index = 0;
static size_t s_temp_sensor_source_selected_index = 0;
static TickType_t s_touch_last_accept_tick = 0;

static void touch_on_next(void);
static void touch_on_active_button(void);
static void touch_on_cancel_button(void);
static void touch_on_more_button(void);

static void touch_drain_pending_events(void)
{
    uint32_t io_num;
    while (xQueueReceive(s_gpio_evt_queue, &io_num, 0)) {
    }
}

static bool touch_cooldown_elapsed(void)
{
    if (s_touch_last_accept_tick == 0) {
        return true;
    }

    TickType_t elapsed = xTaskGetTickCount() - s_touch_last_accept_tick;
    return elapsed >= pdMS_TO_TICKS(TOUCH_COOLDOWN_MS);
}

static bool touch_read_stable_point(uint16_t *x, uint16_t *y)
{
    if (!s_cfg.touch_dev) {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(TOUCH_DEBOUNCE_MS));

    if (!s_cfg.touch_dev->GetTouchPoint(x, y)) {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    uint16_t x2 = 0;
    uint16_t y2 = 0;
    if (!s_cfg.touch_dev->GetTouchPoint(&x2, &y2)) {
        return false;
    }

    *x = x2;
    *y = y2;
    return true;
}

static void touch_wait_release(void)
{
    if (!s_cfg.touch_dev) {
        return;
    }

    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(TOUCH_RELEASE_TIMEOUT_MS);
    uint16_t x = 0;
    uint16_t y = 0;

    while (xTaskGetTickCount() < deadline) {
        if (!s_cfg.touch_dev->GetTouchPoint(&x, &y)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_RELEASE_POLL_MS));
    }
}

static void touch_handle_point(uint16_t x, uint16_t y)
{
    if (x < 80 && y < 80) {
        ESP_LOGD(TAG, "Touch NEXT at (%u,%u)", x, y);
        if (Lvgl_lock(portMAX_DELAY)) {
            touch_on_next();
            Lvgl_unlock();
        }
    } else if (x >= 119 && x < 199 && y < 80) {
        ESP_LOGD(TAG, "Touch GO at (%u,%u)", x, y);
        if (Lvgl_lock(portMAX_DELAY)) {
            touch_on_active_button();
            Lvgl_unlock();
        }
    } else if (x < 80 && y >= 118 && y < 198) {
        ESP_LOGD(TAG, "Touch MORE at (%u,%u)", x, y);
        if (Lvgl_lock(portMAX_DELAY)) {
            touch_on_more_button();
            Lvgl_unlock();
        }
    } else if (x >= 119 && x < 199 && y >= 118 && y < 198) {
        ESP_LOGD(TAG, "Touch CANCEL at (%u,%u)", x, y);
        if (Lvgl_lock(portMAX_DELAY)) {
            touch_on_cancel_button();
            Lvgl_unlock();
        }
    }
}

static void handle_power_key_click(void);
static void handle_power_key_double_click(void);
static void handle_power_key_long_press(void);
static void handle_boot_key_click(void);
static void handle_boot_key_double_click(void);
static void handle_boot_key_long_press(void);
static void task_key_loop(void *arg);
static void task_touch_loop(void *arg);
static void gpio_isr_handler(void *arg);

static void setting_format_label(char *buf, size_t len)
{
    const setting_actions_ops_t *ops = setting_actions_get();
    if (ops && ops->format_mode_label && s_setting_target_id >= SETTING_MODE_WIFI) {
        ops->format_mode_label(s_setting_target_id, buf, len);
        return;
    }

    if (s_setting_target_id == SETTING_MODE_SAMPLE_INTV) {
        uint32_t selected = s_temp_period_list[s_temp_period_selected_index] / 1000;
        snprintf(buf, len, "Sample Intv Sel:%lu", selected);
    } else if (s_setting_target_id == SETTING_MODE_CHART_POINTS) {
        snprintf(buf, len, "Chart Point Sel:%lu", s_chart_point_list[s_chart_points_selected_index]);
    } else {
        snprintf(buf, len, "Sensor Src Sel:%s",
                 temp_sampler_get_sensor_source_name(s_temp_sensor_source_selected_index));
    }
}

static bool setting_is_action_mode(void)
{
    return s_setting_target_id > SETTING_MODE_CONFIG_MAX;
}

static void setting_run_action(void)
{
    const setting_actions_ops_t *ops = setting_actions_get();
    if (!ops || !s_cfg.ui) {
        return;
    }

    char buf[80];
    switch (s_setting_target_id) {
    case SETTING_MODE_WIFI:
        if (ops->run_wifi_connect) {
            ops->run_wifi_connect();
        }
        break;
    case SETTING_MODE_NTP:
        if (ops->run_ntp_sync) {
            ops->run_ntp_sync();
        }
        break;
    case SETTING_MODE_OTA:
        if (ops->run_ota) {
            ops->run_ota();
        }
        break;
    case SETTING_MODE_RESTART:
        if (ops->run_reboot) {
            ops->run_reboot();
        }
        return;
    case SETTING_MODE_EPD_REINIT:
        if (ops->run_epd_reinit) {
            ops->run_epd_reinit();
        }
        break;
    default:
        return;
    }

    setting_format_label(buf, sizeof(buf));
    lv_label_set_text(s_cfg.ui->label_touch_event, buf);
}

static void touch_on_next(void)
{
    if (!s_cfg.ui) {
        return;
    }

    char buf[80] = "default";
    if (setting_is_action_mode()) {
        setting_format_label(buf, sizeof(buf));
        lv_label_set_text(s_cfg.ui->label_touch_event, buf);
        return;
    }

    if (s_setting_target_id == SETTING_MODE_SAMPLE_INTV) {
        s_temp_period_selected_index = (s_temp_period_selected_index + 1) % s_temp_period_count;
        uint32_t selected = s_temp_period_list[s_temp_period_selected_index] / 1000;
        snprintf(buf, sizeof(buf), "Sample Intv Sel:%lu", selected);
    } else if (s_setting_target_id == SETTING_MODE_CHART_POINTS) {
        s_chart_points_selected_index = (s_chart_points_selected_index + 1) % s_chart_point_count;
        snprintf(buf, sizeof(buf), "Chart Point Sel:%lu", s_chart_point_list[s_chart_points_selected_index]);
    } else {
        s_temp_sensor_source_selected_index =
            (s_temp_sensor_source_selected_index + 1) % temp_sampler_get_sensor_source_count();
        snprintf(buf, sizeof(buf), "Sensor Src Sel:%s",
                 temp_sampler_get_sensor_source_name(s_temp_sensor_source_selected_index));
    }
    lv_label_set_text(s_cfg.ui->label_touch_event, buf);
}

static void touch_on_active_button(void)
{
    if (!s_cfg.ui) {
        return;
    }

    if (setting_is_action_mode()) {
        setting_run_action();
        return;
    }

    if (s_setting_target_id == SETTING_MODE_SAMPLE_INTV) {
        s_temp_period_active_index = s_temp_period_selected_index;
        if (s_cfg.set_sample_timer_period) {
            s_cfg.set_sample_timer_period(s_temp_period_list[s_temp_period_active_index]);
        }
    } else if (s_setting_target_id == SETTING_MODE_CHART_POINTS) {
        s_chart_points_active_index = s_chart_points_selected_index;
        chart_controller_set_point_count(s_chart_point_list[s_chart_points_active_index]);
    } else {
        temp_sampler_set_sensor_source(s_temp_sensor_source_selected_index);
    }

    lv_label_set_text(s_cfg.ui->label_touch_event, "actived");
}

static void touch_on_cancel_button(void)
{
    if (!s_cfg.ui) {
        return;
    }

    char buf[80] = "default";
    if (setting_is_action_mode()) {
        setting_format_label(buf, sizeof(buf));
        lv_label_set_text(s_cfg.ui->label_touch_event, buf);
        return;
    }

    if (s_setting_target_id == SETTING_MODE_SAMPLE_INTV) {
        s_temp_period_selected_index = s_temp_period_active_index;
        uint32_t selected = s_temp_period_list[s_temp_period_selected_index] / 1000;
        snprintf(buf, sizeof(buf), "Sample Intv | Sel:%lu", selected);
    } else if (s_setting_target_id == SETTING_MODE_CHART_POINTS) {
        s_chart_points_selected_index = s_chart_points_active_index;
        snprintf(buf, sizeof(buf), "Chart Point | Sel:%lu", s_chart_point_list[s_chart_points_selected_index]);
    } else {
        s_temp_sensor_source_selected_index = temp_sampler_get_sensor_source();
        snprintf(buf, sizeof(buf), "Temp Src | Sel:%s",
                 temp_sampler_get_sensor_source_name(s_temp_sensor_source_selected_index));
    }
    lv_label_set_text(s_cfg.ui->label_touch_event, buf);
}

static void touch_on_more_button(void)
{
    if (!s_cfg.ui) {
        return;
    }

    char buf[80];
    s_setting_target_id = (s_setting_target_id + 1) % SETTING_MODE_COUNT;
    setting_format_label(buf, sizeof(buf));
    lv_label_set_text(s_cfg.ui->label_touch_event, buf);
}

void input_handler_refresh_setting_label(void)
{
    if (!s_cfg.ui || !s_cfg.ui->label_touch_event) {
        return;
    }

    char buf[80];
    setting_format_label(buf, sizeof(buf));
    lv_label_set_text(s_cfg.ui->label_touch_event, buf);
}

void input_handler_poll_setting_label(void)
{
    const setting_actions_ops_t *ops = setting_actions_get();
    if (!s_cfg.ui || !s_cfg.ui->label_touch_event || lv_obj_has_flag(s_cfg.ui->container_setting, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    if (ops && ops->poll) {
        ops->poll();
    }

    if (!setting_is_action_mode()) {
        return;
    }

    char buf[80];
    setting_format_label(buf, sizeof(buf));
    lv_label_set_text(s_cfg.ui->label_touch_event, buf);
}

uint32_t input_handler_get_active_sample_period_ms(void)
{
    return s_temp_period_list[s_temp_period_active_index];
}

void input_handler_setup_buttons(gpio_num_t boot_pin, gpio_num_t pwr_pin, EventGroupHandle_t event_group)
{
    s_cfg.event_group = event_group;
    s_boot_button = new Button(boot_pin);
    s_power_button = new Button(pwr_pin);

    s_boot_button->OnClick([event_group]() {
        ESP_LOGI(TAG, "boot_button click");
        xEventGroupSetBits(event_group, INPUT_EVT_BOOT_CLICK | INPUT_LED_BLINK_BIT);
    });
    s_boot_button->OnDoubleClick([event_group]() {
        ESP_LOGI(TAG, "boot_button double click");
        xEventGroupSetBits(event_group, INPUT_EVT_BOOT_DCLICK | INPUT_LED_BLINK_BIT);
    });
    s_boot_button->OnLongPress([event_group]() {
        ESP_LOGI(TAG, "boot_button long press");
        xEventGroupSetBits(event_group, INPUT_EVT_BOOT_LONG | INPUT_LED_BLINK_BIT);
    });
    s_power_button->OnClick([event_group]() {
        ESP_LOGI(TAG, "power_button click");
        xEventGroupSetBits(event_group, INPUT_EVT_POWER_CLICK | INPUT_LED_BLINK_BIT);
    });
    s_power_button->OnDoubleClick([event_group]() {
        ESP_LOGI(TAG, "power_button double click");
        xEventGroupSetBits(event_group, INPUT_EVT_POWER_DCLICK | INPUT_LED_BLINK_BIT);
    });
    s_power_button->OnLongPress([event_group]() {
        ESP_LOGI(TAG, "power_button long press");
        xEventGroupSetBits(event_group, INPUT_EVT_POWER_LONG | INPUT_LED_BLINK_BIT);
    });
}

void input_handler_touch_gpio_init(void)
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
    s_gpio_evt_queue = xQueueCreate(TOUCH_QUEUE_LEN, sizeof(uint32_t));
}

void input_handler_bind(const input_handler_config_t *config)
{
    if (config) {
        s_cfg = *config;
    }
}

static void handle_power_key_click(void)
{
    static uint16_t click_count = 0;
    click_count++;

    if (s_cfg.show_container) {
        s_cfg.show_container(click_count % 4 + 1);
    }
}

static void handle_power_key_double_click(void)
{
    if (s_cfg.show_container) {
        s_cfg.show_container(1);
    }
}

static void handle_power_key_long_press(void)
{
    if (s_cfg.power_off) {
        s_cfg.power_off();
    } else {
        BoardPower_VBAT_OFF();
    }
}

static void handle_boot_key_click(void)
{
    if (!s_cfg.ui) {
        return;
    }

    if (!lv_obj_has_flag(s_cfg.ui->container_home, LV_OBJ_FLAG_HIDDEN) && s_cfg.overall_info_page) {
        (*s_cfg.overall_info_page)++;
        (*s_cfg.overall_info_page) %= 2;
    }

    if (!lv_obj_has_flag(s_cfg.ui->container_temp_chart, LV_OBJ_FLAG_HIDDEN) && s_cfg.detail_page) {
        if (Lvgl_lock(portMAX_DELAY)) {
            if (s_cfg.ui->temp_chart) {
                lv_obj_add_flag(s_cfg.ui->temp_chart, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_cfg.ui->label_tempchart_time) {
                lv_obj_add_flag(s_cfg.ui->label_tempchart_time, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_cfg.ui->label_start_temp) {
                lv_obj_add_flag(s_cfg.ui->label_start_temp, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_cfg.ui->label_end_temp) {
                lv_obj_add_flag(s_cfg.ui->label_end_temp, LV_OBJ_FLAG_HIDDEN);
            }
            if (s_cfg.ui->label_temp_list) {
                lv_obj_remove_flag(s_cfg.ui->label_temp_list, LV_OBJ_FLAG_HIDDEN);
            }
            Lvgl_unlock();
        }

        if (--(*s_cfg.detail_page) <= (CHART_DETAIL_TOTAL_PAGE - 3)) {
            *s_cfg.detail_page = CHART_DETAIL_TOTAL_PAGE;
        }
    }
}

static void handle_boot_key_double_click(void)
{
    if (s_cfg.show_container) {
        s_cfg.show_container(2);
    }
}

static void handle_boot_key_long_press(void)
{
}

static void task_key_loop(void *arg)
{
    (void)arg;
    const EventBits_t key_mask = INPUT_EVT_POWER_CLICK | INPUT_EVT_POWER_DCLICK |
                                 INPUT_EVT_POWER_LONG | INPUT_EVT_BOOT_CLICK |
                                 INPUT_EVT_BOOT_DCLICK | INPUT_EVT_BOOT_LONG;

    for (;;) {
        EventBits_t events = xEventGroupWaitBits(s_cfg.event_group, key_mask, pdTRUE, pdFALSE, portMAX_DELAY);
        if (events & INPUT_EVT_POWER_CLICK) {
            handle_power_key_click();
        }
        if (events & INPUT_EVT_POWER_DCLICK) {
            handle_power_key_double_click();
        }
        if (events & INPUT_EVT_POWER_LONG) {
            handle_power_key_long_press();
        }
        if (events & INPUT_EVT_BOOT_CLICK) {
            handle_boot_key_click();
        }
        if (events & INPUT_EVT_BOOT_DCLICK) {
            handle_boot_key_double_click();
        }
        if (events & INPUT_EVT_BOOT_LONG) {
            handle_boot_key_long_press();
        }
    }
}

static void task_touch_loop(void *arg)
{
    (void)arg;
    uint32_t io_num;

    for (;;) {
        if (!xQueueReceive(s_gpio_evt_queue, &io_num, portMAX_DELAY)) {
            continue;
        }

        touch_drain_pending_events();

        if (!touch_cooldown_elapsed()) {
            touch_wait_release();
            continue;
        }

        if (!s_cfg.ui || lv_obj_has_flag(s_cfg.ui->container_setting, LV_OBJ_FLAG_HIDDEN)) {
            touch_wait_release();
            continue;
        }

        uint16_t x = 0;
        uint16_t y = 0;
        if (!touch_read_stable_point(&x, &y)) {
            continue;
        }

        touch_handle_point(x, y);
        s_touch_last_accept_tick = xTaskGetTickCount();
        touch_wait_release();
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (s_gpio_evt_queue) {
        xQueueSendFromISR(s_gpio_evt_queue, &gpio_num, NULL);
    }
}

void input_handler_start_tasks(TaskHandle_t *key_task, TaskHandle_t *touch_task)
{
    xTaskCreatePinnedToCore(task_key_loop, "Task_key_loop", 4 * 1024, NULL, 2, &s_key_task, 1);
    xTaskCreatePinnedToCore(task_touch_loop, "Task_touch_loop", 4 * 1024, NULL, 2, &s_touch_task, 1);
    if (key_task) {
        *key_task = s_key_task;
    }
    if (touch_task) {
        *touch_task = s_touch_task;
    }
}

void input_handler_stop_tasks(void)
{
    if (s_touch_task != NULL) {
        vTaskDelete(s_touch_task);
        s_touch_task = NULL;
    }
    if (s_key_task != NULL) {
        vTaskDelete(s_key_task);
        s_key_task = NULL;
    }
}
