/* IMU 姿态页面头文件 */
#ifndef UI_SCREEN_IMU_H
#define UI_SCREEN_IMU_H

#include "lvgl.h"

/* 创建 IMU 页面控件（三轴加速度/角速度卡片） */
void ui_imu_screen_init(lv_obj_t *parent);
/* 刷新 IMU 页面数据 */
void ui_imu_screen_update(void);

#endif /* UI_SCREEN_IMU_H */
