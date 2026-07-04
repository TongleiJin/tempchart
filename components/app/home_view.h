#ifndef HOME_VIEW_H
#define HOME_VIEW_H

#include <stddef.h>
#include <stdbool.h>

#include "pcf85063a.h"
#include "tempchart_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct home_view_config {
    tempchart_ui_t *ui;
    pcf85063a_dev_t *rtc;
    bool *rtc_ok;
    int *overall_info_page;
    char *header_buf;
    size_t header_buf_size;
} home_view_config_t;

void home_view_bind(const home_view_config_t *config);
void home_view_update_header(void);
void home_view_update_main_info(void);

#ifdef __cplusplus
}
#endif

#endif /* HOME_VIEW_H */
