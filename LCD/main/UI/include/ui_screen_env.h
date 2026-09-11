/* 环境（ENV）页面头文件 */
#ifndef UI_SCREEN_ENV_H
#define UI_SCREEN_ENV_H

#include "lvgl.h"

/* 创建环境页面控件（温度/湿度/光照/距离卡片） */
void ui_env_screen_init(lv_obj_t *parent);
/* 刷新环境页面数据 */
void ui_env_screen_update(void);

#endif /* UI_SCREEN_ENV_H */
