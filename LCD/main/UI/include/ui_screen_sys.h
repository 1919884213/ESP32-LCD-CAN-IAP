/* MCU 资源占用显示页面头文件。 */
#ifndef UI_SCREEN_SYS_H
#define UI_SCREEN_SYS_H

#include "lvgl.h"

/* 创建 MCU 资源占用页面。 */
void ui_sys_screen_init(lv_obj_t *parent);
/* 刷新 MCU 资源占用显示（CPU 占用、内存、FPS）。 */
void ui_sys_screen_update(void);

#endif /* UI_SCREEN_SYS_H */
