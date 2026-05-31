#include "lvgl.h"
#include <stdio.h>
#include "FacTest_ui.h"

static void setup_scr_screen(lv_factest_ui *ui);

void setup_factest_ui(lv_factest_ui *ui) {
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
    
    // 使用存在的字体
    // if (font_size == 17) {
    //     lv_obj_set_style_text_font(label, &lv_font_montserratMedium_17, LV_PART_MAIN|LV_STATE_DEFAULT);
    // } else {
        // 使用默认字体或存在的字体
        lv_obj_set_style_text_font(label, &lv_font_montserratMedium_12, LV_PART_MAIN|LV_STATE_DEFAULT);
    // }
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
void setup_scr_screen(lv_factest_ui *ui)
{
    // 创建主屏幕
    ui->screen = lv_obj_create(NULL);
    lv_obj_set_size(ui->screen, 200, 200);
    lv_obj_set_scrollbar_mode(ui->screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(ui->screen, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->screen, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->screen, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);
    
    // 容器 1：欢迎信息
    ui->screen_cont_1 = create_base_container(ui->screen, 0, 0, 200, 200, false);
    
    ui->label_overall_info = lv_label_create(ui->screen_cont_1);
    lv_obj_set_pos(ui->label_overall_info, 10, 30);
    lv_obj_set_size(ui->label_overall_info, 180, 140);
    lv_label_set_long_mode(ui->label_overall_info, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ui->label_overall_info,
        "Welcome!\n\nBOOT: show max/min\nPOWER: next page\nLONG POWER: power off\nDOUBLE POWER: show chart\n\nTemp sample: 3s");
    lv_obj_set_style_text_align(ui->label_overall_info, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_color(ui->label_overall_info, lv_color_hex(0x000000), LV_PART_MAIN);
    set_label_style(ui->label_overall_info, lv_color_hex(0x000000), 14);
    
    // 容器 2：图片显示
    ui->screen_cont_2 = create_base_container(ui->screen, 0, 0, 200, 200, true);
    
    ui->screen_img_5 = lv_image_create(ui->screen_cont_2);
    lv_obj_set_pos(ui->screen_img_5, 0, 0);
    lv_obj_set_size(ui->screen_img_5, 200, 200);
    lv_obj_add_flag(ui->screen_img_5, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(ui->screen_img_5, &_3_RGB565A8_200x200);
    lv_image_set_pivot(ui->screen_img_5, 50, 50);
    lv_image_set_rotation(ui->screen_img_5, 0);
    lv_obj_set_style_image_recolor_opa(ui->screen_img_5, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(ui->screen_img_5, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    
    // 容器 3：4个按钮
    ui->screen_cont_3 = create_base_container(ui->screen, 0, 0, 200, 200, true);
    
    // 按钮标签
    ui->screen_label_23 = lv_label_create(ui->screen_cont_3);
    lv_obj_set_pos(ui->screen_label_23, 5, 86);
    lv_obj_set_size(ui->screen_label_23, 189, 25);
    lv_label_set_text(ui->screen_label_23, "");
    lv_label_set_long_mode(ui->screen_label_23, LV_LABEL_LONG_WRAP);
    set_label_style(ui->screen_label_23, lv_color_hex(0x000000), 17);
    lv_obj_set_style_text_align(ui->screen_label_23, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_label_23, 3, LV_PART_MAIN|LV_STATE_DEFAULT);
    
    // 创建4个按钮
    create_button(ui->screen_cont_3, 0, 0, 80, 80, "NEXT");
    create_button(ui->screen_cont_3, 119, 0, 80, 80, "GO");
    create_button(ui->screen_cont_3, 0, 118, 80, 80, "MORE");
    create_button(ui->screen_cont_3, 119, 118, 80, 80, "CANCEL");
    
    // 温度图表容器
    // ui->temp_chart_container = lv_obj_create(ui->screen);
    ui->temp_chart_container = create_base_container(ui->screen, 0, 0, 200, 190, true);
    // lv_obj_set_size(ui->temp_chart_container, 200, 190);
    // lv_obj_set_pos(ui->temp_chart_container, 0, 0);
    // lv_obj_set_scrollbar_mode(ui->temp_chart_container, LV_SCROLLBAR_MODE_OFF);
    // lv_obj_set_style_bg_opa(ui->temp_chart_container, 0, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(ui->temp_chart_container, lv_color_hex(0xffffff), LV_PART_MAIN);
    // lv_obj_set_style_border_width(ui->temp_chart_container, 0, LV_PART_MAIN);
    // lv_obj_set_style_pad_all(ui->temp_chart_container, 0, LV_PART_MAIN);
    // lv_obj_set_style_shadow_width(ui->temp_chart_container, 0, LV_PART_MAIN);
    
    // 图表
    ui->temp_chart = lv_chart_create(ui->temp_chart_container);
    lv_obj_set_size(ui->temp_chart, 180, 120);
    lv_obj_align(ui->temp_chart, LV_ALIGN_TOP_MID, 0, 10);
    lv_chart_set_type(ui->temp_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ui->temp_chart, 100);
    lv_chart_set_range(ui->temp_chart, LV_CHART_AXIS_PRIMARY_Y, 10, 45);
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
    
        ui->temp_label = lv_label_create(ui->temp_chart_container);
    lv_obj_set_pos(ui->temp_label, 10, 170);
    lv_obj_set_size(ui->temp_label, 180, 20);
    lv_label_set_text(ui->temp_label, "Waiting 20s...");

    // ====

    ui->screen_label_temp_info = lv_label_create(ui->temp_chart_container);
    lv_obj_set_pos(ui->screen_label_temp_info, 10, 135);
    lv_obj_set_size(ui->screen_label_temp_info, 180, 20);
    // lv_label_set_text(ui->screen_label_temp_info, "temp info here to display yes here youa re hereyouu");
    lv_label_set_long_mode(ui->screen_label_temp_info, LV_LABEL_LONG_WRAP);
    
    // 更新布局
    lv_obj_update_layout(ui->screen);
}


