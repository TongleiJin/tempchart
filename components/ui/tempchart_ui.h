#ifndef TEMPCHART_UI_H
#define TEMPCHART_UI_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "lvgl.h"

	typedef struct
	{
		lv_obj_t *screen;
		bool screen_del;

		lv_obj_t *container_home;
		lv_obj_t *label_home_main_info;
		lv_obj_t *label_home_header;

		lv_obj_t *container_image;
		lv_obj_t *screen_img_5;

		lv_obj_t *container_setting;
		lv_obj_t *label_touch_event;
		lv_obj_t *screen_btn_4;
		lv_obj_t *screen_btn_4_label;
		lv_obj_t *screen_btn_3;
		lv_obj_t *screen_btn_3_label;
		lv_obj_t *screen_btn_2;
		lv_obj_t *screen_btn_2_label;
		lv_obj_t *screen_btn_1;
		lv_obj_t *screen_btn_1_label;

		lv_obj_t *container_temp_chart;
		lv_obj_t *label_temp_list;
		lv_obj_t *label_tempchart_time;
		lv_obj_t *label_start_temp;
		lv_obj_t *label_end_temp;
		lv_obj_t *temp_chart;
		lv_chart_series_t *temp_series;
		
	} tempchart_ui_t;

	void tempchart_ui_create(tempchart_ui_t *ui);

	LV_IMAGE_DECLARE(_shidu_RGB565A8_20x20);
	LV_IMAGE_DECLARE(_wendu_RGB565A8_20x20);
	LV_IMAGE_DECLARE(_battery_RGB565A8_20x20);
	LV_IMAGE_DECLARE(_3_RGB565A8_200x200);

	LV_FONT_DECLARE(lv_font_montserratMedium_67)
	LV_FONT_DECLARE(lv_font_montserratMedium_16)
	LV_FONT_DECLARE(lv_font_montserratMedium_20)
	LV_FONT_DECLARE(lv_font_MISANSREGULAR_20)
	LV_FONT_DECLARE(lv_font_montserratMedium_17)
	LV_FONT_DECLARE(lv_font_montserratMedium_12)

#ifdef __cplusplus
}
#endif

#endif
