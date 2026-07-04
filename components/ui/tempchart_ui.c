#include "lvgl.h"
#include <stdio.h>
#include "tempchart_ui.h"


#define MAX_TEMP_CHART_POINT_COUNT 10

static void setup_scr_screen(tempchart_ui_t *ui);

void tempchart_ui_create(tempchart_ui_t *ui) {
	lv_theme_apply(lv_layer_bottom());
	ui->screen_del = true;
	/*ui 初始化*/
    setup_scr_screen(ui);
	lv_screen_load(ui->screen);
}


// 辅助函数：设置标签样式
static void set_label_style(lv_obj_t *label, lv_color_t color, int font_size)
{
    lv_obj_set_style_border_width(label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(label, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_montserratMedium_12, LV_PART_MAIN|LV_STATE_DEFAULT);
}

// 辅助函数：创建基础容器
static lv_obj_t * create_base_container(lv_obj_t *parent, int x, int y, int w, int h, bool hidden)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_size(cont, w, h);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    
    // 通用样式设置
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cont, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(cont, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(cont, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    
    if (hidden) {
        lv_obj_add_flag(cont, LV_OBJ_FLAG_HIDDEN);
    }
    
    return cont;
}

// 辅助函数：创建按钮（带文字）
static lv_obj_t * create_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    
    // 按钮样式
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 5, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn, &lv_font_montserratMedium_17, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    
    // 按钮标签
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(label, LV_PCT(100));
    
    return btn;
}



// 优化后的主函数
void setup_scr_screen(tempchart_ui_t *ui)
{
    // 创建主屏幕
    ui->screen = lv_obj_create(NULL);
    lv_obj_set_size(ui->screen, 200, 200);
    lv_obj_set_scrollbar_mode(ui->screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui->screen, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->screen, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->screen, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    
    // 容器 1：欢迎信息
    ui->container_home = create_base_container(ui->screen, 0, 0, 200, 200, false);
    
    ui->label_home_main_info = lv_label_create(ui->container_home);
    lv_obj_set_pos(ui->label_home_main_info, 6, 35);
    lv_obj_set_size(ui->label_home_main_info, 180, 160);
    lv_label_set_long_mode(ui->label_home_main_info, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ui->label_home_main_info, "Updating...");

    ui->label_home_header = lv_label_create(ui->container_home);
    set_label_style(ui->label_home_header, lv_color_hex(0x000000), 12);
    lv_obj_align(ui->label_home_header, LV_ALIGN_TOP_LEFT, 2, 4);
    lv_obj_set_style_text_font(ui->label_home_header, &lv_font_montserratMedium_16, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_label_set_text(ui->label_home_header, LV_SYMBOL_BATTERY_1);
    
    // 容器 2：图片显示
    ui->container_image = create_base_container(ui->screen, 0, 0, 200, 200, true);
    ui->screen_img_5 = lv_image_create(ui->container_image);
    lv_obj_set_pos(ui->screen_img_5, 0, 0);
    lv_obj_set_size(ui->screen_img_5, 200, 200);
    lv_obj_add_flag(ui->screen_img_5, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->screen_img_5, &_3_RGB565A8_200x200);
    lv_image_set_pivot(ui->screen_img_5, 50, 50);
    lv_image_set_rotation(ui->screen_img_5, 0);
    lv_obj_set_style_image_recolor_opa(ui->screen_img_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->screen_img_5, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    
    // 容器 3：4个按钮
    ui->container_setting = create_base_container(ui->screen, 0, 0, 200, 200, true);
    
    // 按钮标签
    ui->label_touch_event = lv_label_create(ui->container_setting);
    lv_obj_set_pos(ui->label_touch_event, 5, 86);
    lv_obj_set_size(ui->label_touch_event, 189, 25);
    lv_label_set_text(ui->label_touch_event, "");
    lv_label_set_long_mode(ui->label_touch_event, LV_LABEL_LONG_WRAP);
    set_label_style(ui->label_touch_event, lv_color_hex(0x000000), 17);
    lv_obj_set_style_text_align(ui->label_touch_event, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->label_touch_event, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    
    // 创建4个按钮
    create_button(ui->container_setting, 0, 0, 80, 80, "NEXT");
    create_button(ui->container_setting, 119, 0, 80, 80, "GO");
    create_button(ui->container_setting, 0, 118, 80, 80, "MORE");
    create_button(ui->container_setting, 119, 118, 80, 80, "CANCEL");
    
    // 温度图表容器
    // ui->container_temp_chart = lv_obj_create(ui->screen);
    ui->container_temp_chart = create_base_container(ui->screen, 0, 0, 200, 190, true);
    
    // 图表
    ui->temp_chart = lv_chart_create(ui->container_temp_chart);
    lv_obj_set_size(ui->temp_chart, 198, 160);
    lv_obj_align(ui->temp_chart, LV_ALIGN_TOP_MID, 0, 10);
    lv_chart_set_type(ui->temp_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ui->temp_chart, MAX_TEMP_CHART_POINT_COUNT);
    lv_chart_set_range(ui->temp_chart, LV_CHART_AXIS_PRIMARY_Y, 20, 40);
    lv_obj_set_style_bg_color(ui->temp_chart, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui->temp_chart, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui->temp_chart, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(ui->temp_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(ui->temp_chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_width(ui->temp_chart, 2, LV_PART_INDICATOR);
    lv_obj_set_style_height(ui->temp_chart, 2, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui->temp_chart, 1, LV_PART_MAIN);
    
    // 数据系列
    ui->temp_series = lv_chart_add_series(ui->temp_chart, lv_color_black(), LV_CHART_AXIS_PRIMARY_Y);
    for (uint32_t i = 0; i < 100; i++) {
        lv_chart_set_next_value(ui->temp_chart, ui->temp_series, 23);
    }
    
    ui->label_tempchart_time = lv_label_create(ui->container_temp_chart);
    lv_obj_set_pos(ui->label_tempchart_time, 2, 175);
    lv_obj_set_size(ui->label_tempchart_time, 180, 20);
    lv_label_set_long_mode(ui->label_tempchart_time, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->label_tempchart_time, "...");

    ui->label_start_temp = lv_label_create(ui->container_temp_chart);
    lv_obj_set_size(ui->label_start_temp, 60, 14);
    lv_label_set_long_mode(ui->label_start_temp, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->label_start_temp, "");
    set_label_style(ui->label_start_temp, lv_color_hex(0x000000), 12);
    lv_obj_add_flag(ui->label_start_temp, LV_OBJ_FLAG_HIDDEN);

    ui->label_end_temp = lv_label_create(ui->container_temp_chart);
    lv_obj_set_size(ui->label_end_temp, 60, 14);
    lv_label_set_long_mode(ui->label_end_temp, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui->label_end_temp, "");
    set_label_style(ui->label_end_temp, lv_color_hex(0x000000), 12);
    lv_obj_add_flag(ui->label_end_temp, LV_OBJ_FLAG_HIDDEN);

    ui->label_temp_list = lv_label_create(ui->container_temp_chart);
    lv_obj_set_pos(ui->label_temp_list, 0, 0);
    lv_obj_set_size(ui->label_temp_list, 200, 200);
    lv_label_set_long_mode(ui->label_temp_list, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(ui->label_temp_list, LV_OBJ_FLAG_HIDDEN);
    
    // 更新布局
    lv_obj_update_layout(ui->screen);
}

