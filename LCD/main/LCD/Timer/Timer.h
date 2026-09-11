#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/**
 * GPTimer 硬件定时器，1us 分辨率。
 * - GetUs/GetMs 提供高精度时间戳，后续可直接注册为 LVGL 时基：
 *     LVGL v9: lv_tick_set_cb(Timer_GetMs);
 *     LVGL v8: 用 alarm 回调周期性调用 lv_tick_inc(1)
 * - DelayUs/DelayMs 供驱动初始化序列与 SPI 时序延时使用
 */
void     Timer_Init(void);
uint64_t Timer_GetUs(void);
uint32_t Timer_GetMs(void);
void     Timer_DelayUs(uint32_t us);
void     Timer_DelayMs(uint32_t ms);

#endif
