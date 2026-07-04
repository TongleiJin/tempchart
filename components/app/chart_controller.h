#ifndef CHART_CONTROLLER_H
#define CHART_CONTROLLER_H

#include <stdint.h>

#include "temp_sampler.h"
#include "tempchart_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHART_DETAIL_PAGE_SIZE 10
#define CHART_DETAIL_TOTAL_PAGE (TEMP_SAMPLER_MAX_SAMPLES / CHART_DETAIL_PAGE_SIZE)

void chart_controller_bind_ui(tempchart_ui_t *ui);
void chart_controller_update_chart(void);
void chart_controller_update_list(int *detail_page_number);
void chart_controller_reset_list_page(int *detail_page_number);

uint16_t chart_controller_get_temp_offset(void);
uint16_t chart_controller_get_temp_scaler(void);
uint32_t chart_controller_get_point_count(void);
void chart_controller_set_point_count(uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* CHART_CONTROLLER_H */
