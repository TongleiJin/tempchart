#include "chart_controller.h"

#include <stdio.h>
#include <string.h>

#include "temp_sampler.h"

static tempchart_ui_t *s_ui = NULL;
static uint16_t s_temp_offset = 20;
static const uint16_t s_temp_scaler = 3;

static uint16_t map_temp_to_chart_y(float temp)
{
    const float chart_temp_min = 20.0f;
    const float chart_temp_max = 45.0f;
    const int font_height = 18;
    const int margin = 2;

    if (!s_ui || !s_ui->temp_chart) {
        return 0;
    }

    lv_area_t chart_coords;
    lv_obj_get_content_coords(s_ui->temp_chart, &chart_coords);
    int chart_top = chart_coords.y1;
    int chart_bottom = chart_coords.y2;
    int chart_height = chart_bottom - chart_top;
    if (chart_height <= 0) {
        chart_height = 1;
    }

    if (temp < chart_temp_min) {
        temp = chart_temp_min;
    }
    if (temp > chart_temp_max) {
        temp = chart_temp_max;
    }

    float rel = (temp - chart_temp_min) / (chart_temp_max - chart_temp_min);
    if (rel < 0.0f) {
        rel = 0.0f;
    }
    if (rel > 1.0f) {
        rel = 1.0f;
    }

    int y = chart_bottom - (int)(rel * chart_height * 1.1);
    int y_min = chart_top + margin;
    int y_max = chart_bottom - font_height;
    if (y < y_min) {
        y = y_min;
    }
    if (y > y_max) {
        y = y_max;
    }

    return (uint16_t)y;
}

void chart_controller_bind_ui(tempchart_ui_t *ui)
{
    s_ui = ui;
}

void chart_controller_update_chart(void)
{
    if (!s_ui || !s_ui->temp_chart || !s_ui->temp_series) {
        return;
    }

    temp_sample_t temp_record[TEMP_SAMPLER_MAX_SAMPLES];
    temp_sampler_copy_samples(temp_record, TEMP_SAMPLER_MAX_SAMPLES);

    int point_count = (int)lv_chart_get_point_count(s_ui->temp_chart);
    float total_value = 0.0f;

    for (int i = TEMP_SAMPLER_MAX_SAMPLES - point_count; i < TEMP_SAMPLER_MAX_SAMPLES; ++i) {
        float temp = temp_record[i].temperature;
        total_value += temp;
        temp = (temp - s_temp_offset) * s_temp_scaler;
        lv_chart_set_next_value(s_ui->temp_chart, s_ui->temp_series, (int32_t)temp);
    }

    if (point_count > 0) {
        total_value = total_value / point_count + 0.5f;
        s_temp_offset = (uint16_t)((total_value * 0.8f) * 0.2f + s_temp_offset * 0.8f);
    }

    lv_obj_invalidate(s_ui->temp_chart);

    int last_index = TEMP_SAMPLER_MAX_SAMPLES - 1;
    int first_index = last_index - point_count;
    if (first_index < 0) {
        first_index = 0;
    }

    char buf[128] = "";
    temp_sample_t tmp = temp_record[first_index];
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%02d:%02d:%02d",
             tmp.timestamp.hour, tmp.timestamp.min, tmp.timestamp.sec);
    tmp = temp_record[last_index];
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "              %02d:%02d:%02d",
             tmp.timestamp.hour, tmp.timestamp.min, tmp.timestamp.sec);

    if (s_ui->label_tempchart_time) {
        lv_label_set_text(s_ui->label_tempchart_time, buf);
    }

    if (s_ui->label_start_temp && s_ui->label_end_temp) {
        char pbuf[32];
        int yy_first = map_temp_to_chart_y(temp_record[first_index].temperature);
        int yy_last = map_temp_to_chart_y(temp_record[last_index].temperature);

        snprintf(pbuf, sizeof(pbuf), "%.1f", temp_record[first_index].temperature);
        lv_label_set_text(s_ui->label_start_temp, pbuf);
        lv_obj_set_pos(s_ui->label_start_temp, 5, yy_first);

        snprintf(pbuf, sizeof(pbuf), "%.1f", temp_record[last_index].temperature);
        lv_label_set_text(s_ui->label_end_temp, pbuf);
        lv_obj_set_pos(s_ui->label_end_temp, 165, yy_last);
    }
}

void chart_controller_update_list(int *detail_page_number)
{
    if (!s_ui || !s_ui->label_temp_list || !detail_page_number) {
        return;
    }

    temp_sample_t temp_record[TEMP_SAMPLER_MAX_SAMPLES];
    temp_sampler_copy_samples(temp_record, TEMP_SAMPLER_MAX_SAMPLES);

    char buf[256] = "";
    char record_buf[64];

    if (*detail_page_number >= CHART_DETAIL_TOTAL_PAGE) {
        snprintf(record_buf, sizeof(record_buf), "\nMax: %0.1f°C\n", temp_sampler_get_max());
        strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
        snprintf(record_buf, sizeof(record_buf), "Min: %0.1f°C\n", temp_sampler_get_min());
        strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
        *detail_page_number = CHART_DETAIL_TOTAL_PAGE;
    } else {
        int begin_index = *detail_page_number * CHART_DETAIL_PAGE_SIZE;
        snprintf(record_buf, sizeof(record_buf), "Page:%d from:%d\n\n",
                 *detail_page_number, begin_index);
        strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);

        for (int i = 0; i < CHART_DETAIL_PAGE_SIZE; ++i) {
            temp_sample_t record = temp_record[begin_index + (CHART_DETAIL_PAGE_SIZE - 1) - i];
            snprintf(record_buf, sizeof(record_buf), "%02d  %02d:%02d:%02d>%.1f°C\n",
                     i + 1, record.timestamp.hour, record.timestamp.min,
                     record.timestamp.sec, record.temperature);
            strncat(buf, record_buf, sizeof(buf) - strlen(buf) - 1);
        }
    }

    lv_label_set_text(s_ui->label_temp_list, buf);
}

void chart_controller_reset_list_page(int *detail_page_number)
{
    if (detail_page_number) {
        *detail_page_number = CHART_DETAIL_TOTAL_PAGE;
    }
}

uint16_t chart_controller_get_temp_offset(void)
{
    return s_temp_offset;
}

uint16_t chart_controller_get_temp_scaler(void)
{
    return s_temp_scaler;
}

uint32_t chart_controller_get_point_count(void)
{
    if (!s_ui || !s_ui->temp_chart) {
        return 0;
    }
    return lv_chart_get_point_count(s_ui->temp_chart);
}

void chart_controller_set_point_count(uint32_t count)
{
    if (s_ui && s_ui->temp_chart) {
        lv_chart_set_point_count(s_ui->temp_chart, count);
    }
}
