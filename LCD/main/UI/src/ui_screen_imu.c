#include "ui_screen_imu.h"
#include "ui_data.h"
#include "ui_helpers.h"

/* IMU 页各轴数值控件指针：三轴加速度与三轴角速度 */
static lv_obj_t *s_accel_x;
static lv_obj_t *s_accel_y;
static lv_obj_t *s_accel_z;
static lv_obj_t *s_gyro_x;
static lv_obj_t *s_gyro_y;
static lv_obj_t *s_gyro_z;

/* 创建一个轴卡片（左侧轴名、右侧数值），返回数值标签用于更新 */
static lv_obj_t *create_axis_card(lv_obj_t *parent, const char *axis,
                                  lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 146, 42);
    lv_obj_set_pos(card, x, y);
    ui_apply_card_style(card);
    lv_obj_t *label = ui_create_caption(card, axis);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *value = ui_create_value(card, "0", &lv_font_montserrat_16);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
    return value;
}

/* 创建 IMU 页面控件：加速度/角速度两组 6 个轴卡片 */
void ui_imu_screen_init(lv_obj_t *parent)
{
    ui_apply_page_style(parent);
    lv_obj_set_style_pad_all(parent, 8, LV_PART_MAIN);

    lv_obj_t *title = ui_create_title(parent, "姿态运动");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 标题：加速度（单位 0.001g）与角速度（dps） */
    lv_obj_t *accel_title = ui_create_caption(parent, "加速度 (0.001g)");
    lv_obj_set_style_text_color(accel_title, lv_color_hex(0x1677C8), LV_PART_MAIN);
    lv_obj_align(accel_title, LV_ALIGN_TOP_LEFT, 0, 25);
    lv_obj_t *gyro_title = ui_create_caption(parent, "角速度 (dps)");
    lv_obj_set_style_text_color(gyro_title, lv_color_hex(0x0F9B8E), LV_PART_MAIN);
    lv_obj_align(gyro_title, LV_ALIGN_TOP_RIGHT, 0, 25);

    /* 六轴卡片布局：左列加速度、右列角速度 */
    s_accel_x = create_axis_card(parent, "X", 0, 44);
    s_gyro_x = create_axis_card(parent, "X", 158, 44);
    s_accel_y = create_axis_card(parent, "Y", 0, 92);
    s_gyro_y = create_axis_card(parent, "Y", 158, 92);
    s_accel_z = create_axis_card(parent, "Z", 0, 140);
    s_gyro_z = create_axis_card(parent, "Z", 158, 140);
}

/* 刷新 IMU 页面：读取共享数据并更新各轴数值 */
void ui_imu_screen_update(void)
{
    if (s_accel_x == NULL) return;

    ui_imu_data_t data;
    ui_get_imu_data(&data);
    lv_label_set_text_fmt(s_accel_x, "%d", data.ax);
    lv_label_set_text_fmt(s_accel_y, "%d", data.ay);
    lv_label_set_text_fmt(s_accel_z, "%d", data.az);
    lv_label_set_text_fmt(s_gyro_x, "%d", data.gx);
    lv_label_set_text_fmt(s_gyro_y, "%d", data.gy);
    lv_label_set_text_fmt(s_gyro_z, "%d", data.gz);
}
