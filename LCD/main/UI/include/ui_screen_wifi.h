/* WiFi 设置页面头文件 */
#ifndef UI_SCREEN_WIFI_H
#define UI_SCREEN_WIFI_H

#include "lvgl.h"

/* 创建 WiFi 页面控件（SSID/密码输入、连接按钮、软键盘） */
void ui_wifi_screen_init(lv_obj_t *parent);
/* 刷新 WiFi 页面连接状态 */
void ui_wifi_screen_update(void);

#endif /* UI_SCREEN_WIFI_H */
