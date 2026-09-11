/* 驾驶（DRIVE）页面头文件 */
#ifndef UI_SCREEN_DRIVE_H
#define UI_SCREEN_DRIVE_H

#include "lvgl.h"

/* 创建驾驶页面控件（速度弧表、时钟、灯光图标） */
void ui_drive_screen_init(lv_obj_t *parent);
/* 刷新驾驶页面数据 */
void ui_drive_screen_update(void);

/* 自动亮度状态（供控制中心读取/设置，驾驶页仍是逻辑持有者） */
bool ui_drive_is_auto_brightness(void);
void ui_drive_set_auto_brightness(bool enabled);

#endif /* UI_SCREEN_DRIVE_H */
