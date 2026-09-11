/* 触摸待机模块头文件：5s 无触摸降低面板灯亮度并进入待机页，
 * 待机页只显示 SNTP 同步的日期时间，触摸立即唤醒。 */
#ifndef UI_STANDBY_H
#define UI_STANDBY_H

#include "lvgl.h"

/* 创建待机遮罩页面并启动无触摸检测软件定时器。 */
void ui_standby_init(void);

/* 设置无触摸进入待机的时限（秒）；0 表示关闭自动息屏。 */
void ui_standby_set_timeout_sec(uint32_t sec);

#endif /* UI_STANDBY_H */
