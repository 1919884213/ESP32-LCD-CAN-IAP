/* W25Q64 占用情况显示页面头文件。 */
#ifndef UI_SCREEN_W25_H
#define UI_SCREEN_W25_H

#include "lvgl.h"

/* 创建 W25Q64 占用情况页面。 */
void ui_w25_screen_init(lv_obj_t *parent);
/* 刷新 W25Q64 占用情况显示。 */
void ui_w25_screen_update(void);

#endif /* UI_SCREEN_W25_H */