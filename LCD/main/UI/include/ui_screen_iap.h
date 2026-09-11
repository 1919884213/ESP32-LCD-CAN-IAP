/* IAP 升级页面头文件 */
#ifndef UI_SCREEN_IAP_H
#define UI_SCREEN_IAP_H
#include "lvgl.h"
/* 创建 IAP 页面控件（升级/复位按钮、进度条） */
void ui_iap_screen_init(lv_obj_t *parent);
/* 刷新 IAP 页面状态与进度 */
void ui_iap_screen_update(void);
#endif
