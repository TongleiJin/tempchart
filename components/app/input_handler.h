#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include <stdint.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "tempchart_ui.h"

#ifdef __cplusplus
class I2cFt6336Dev;
extern "C" {
#else
typedef struct I2cFt6336Dev I2cFt6336Dev;
#endif

#define INPUT_EVT_POWER_CLICK  0x01
#define INPUT_EVT_POWER_DCLICK 0x02
#define INPUT_EVT_POWER_LONG   0x04
#define INPUT_EVT_BOOT_CLICK   0x10
#define INPUT_EVT_BOOT_DCLICK  0x20
#define INPUT_EVT_BOOT_LONG    0x40
#define INPUT_LED_BLINK_BIT    0x80

typedef struct input_handler_config {
    tempchart_ui_t *ui;
    I2cFt6336Dev *touch_dev;
    EventGroupHandle_t event_group;
    void (*show_container)(int container_number);
    void (*power_off)(void);
    void (*set_sample_timer_period)(uint32_t period_ms);
    int *overall_info_page;
    int *detail_page;
} input_handler_config_t;

void input_handler_setup_buttons(gpio_num_t boot_pin, gpio_num_t pwr_pin, EventGroupHandle_t event_group);
void input_handler_touch_gpio_init(void);
void input_handler_bind(const input_handler_config_t *config);
void input_handler_refresh_setting_label(void);
void input_handler_poll_setting_label(void);
uint32_t input_handler_get_active_sample_period_ms(void);
void input_handler_start_tasks(TaskHandle_t *key_task, TaskHandle_t *touch_task);
void input_handler_stop_tasks(void);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_HANDLER_H */
