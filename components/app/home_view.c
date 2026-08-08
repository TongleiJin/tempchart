#include "home_view.h"

#include <stdio.h>
#include <string.h>

#include "chart_controller.h"
#include "device_status.h"
#include "input_handler.h"
#include "port_adc.h"
#include "temp_sampler.h"

static home_view_config_t s_cfg = {};

void home_view_bind(const home_view_config_t *config)
{
    if (config) {
        s_cfg = *config;
    }
}

void home_view_update_header(void)
{
    if (!s_cfg.ui || !s_cfg.overall_info_page || !s_cfg.rtc || !s_cfg.rtc_ok ||
        !s_cfg.header_buf || s_cfg.header_buf_size == 0) {
        return;
    }

    if (*s_cfg.overall_info_page != 1 || !s_cfg.ui->label_home_header) {
        return;
    }

    pcf85063a_datetime_t current_time = {};
    if (*s_cfg.rtc_ok) {
        pcf85063a_get_time_date(s_cfg.rtc, &current_time);
    }

    uint8_t bat_level = Get_Batterylevel();
    if (bat_level > 95) {
        strcpy(s_cfg.header_buf, LV_SYMBOL_BATTERY_FULL);
    } else if (bat_level > 75) {
        strcpy(s_cfg.header_buf, LV_SYMBOL_BATTERY_3);
    } else if (bat_level > 50) {
        strcpy(s_cfg.header_buf, LV_SYMBOL_BATTERY_2);
    } else {
        strcpy(s_cfg.header_buf, LV_SYMBOL_BATTERY_1);
    }

    snprintf(s_cfg.header_buf + strlen(s_cfg.header_buf),
               s_cfg.header_buf_size - strlen(s_cfg.header_buf),
               " %d%%   %02d:%02d:%02d",
               bat_level, current_time.hour, current_time.min, current_time.sec);
    lv_label_set_text(s_cfg.ui->label_home_header, s_cfg.header_buf);
}

void home_view_update_main_info(void)
{
    if (!s_cfg.ui || !s_cfg.overall_info_page || !s_cfg.ui->label_home_main_info) {
        return;
    }

    char buf[512] = "";
    if (*s_cfg.overall_info_page == 1) {

        device_status_format_system_info1(buf, sizeof(buf));

    } else {
        device_status_format_system_info(buf, sizeof(buf));
    }

    lv_label_set_text(s_cfg.ui->label_home_main_info, buf);
}
