/* 控制中心（主页面下拉）头文件 */
#ifndef UI_CONTROL_CENTER_H
#define UI_CONTROL_CENTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* 创建控制中心覆盖层（默认隐藏），挂在全屏顶层 */
void ui_control_center_init(void);
/* 展开/收起控制中心（带下滑/上滑动画） */
void ui_control_center_open(void);
void ui_control_center_close(void);
/* 当前是否处于展开状态 */
bool ui_control_center_is_open(void);
/* 自动亮度/待机恢复调整亮度后，反向同步面板上的亮度滑块 */
void ui_control_center_sync_brightness(uint32_t duty);

#ifdef __cplusplus
}
#endif

#endif /* UI_CONTROL_CENTER_H */
