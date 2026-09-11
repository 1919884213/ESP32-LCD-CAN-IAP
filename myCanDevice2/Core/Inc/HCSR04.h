#ifndef __HCSR04_H__
#define __HCSR04_H__

#include "main.h"

/*==========================================================================
 * HCSR04 超声波测距模块驱动
 *--------------------------------------------------------------------------
 * 引脚    : TRIG = PA1 (GPIO 输出), ECHO = PA2 (TIM2_CH3 输入捕获)
 * 定时器  : TIM2, Prescaler=71 -> 1us/计数, Period=65535 (最长 65.5ms)
 * 测量原理: TRIG 拉高 >=10us -> 模块发 8 个 40kHz 脉冲 -> ECHO 高电平时间
 *           与距离成正比。输入捕获双沿测量高电平持续时间。
 *           distance_cm = high_level_us / 58
 * 超时    : ECHO 无响应时 16 位计数溢出(更新事件) -> 本次测量置无效。
 *========================================================================*/

typedef struct {
  volatile uint8_t  busy;          /* 1 = 正在等待测量完成 */
  volatile uint8_t  valid;         /* 1 = 最近一次测量有效 */
  volatile uint8_t  edge;          /* 期望的下一个边沿: 0=上升沿, 1=下降沿 */
  volatile uint32_t rise;          /* 上升沿捕获计数值 */
  volatile uint32_t fall;          /* 下降沿捕获计数值 */
  volatile uint32_t last_us;       /* 最近一次有效高电平时间(us) */
  int32_t           distance_mm;   /* 最近一次有效距离(mm) */
} HCSR04_Handle_t;

/* 初始化: 使能 TIM2_CH3 输入捕获中断 */
void HCSR04_Init(HCSR04_Handle_t *dev);

/* 触发一次测量 (TRIG 拉高 10us), 需在任务中调用, 然后轮询 HCSR04_IsBusy() */
void HCSR04_StartMeasure(HCSR04_Handle_t *dev);

/* 返回最近一次有效距离(mm), 无有效数据返回 -1 */
int32_t HCSR04_GetDistanceMm(HCSR04_Handle_t *dev);

/* 是否正在测量 */
uint8_t HCSR04_IsBusy(HCSR04_Handle_t *dev);

/* 中止当前测量并复位状态 (任务超时无回波时调用, 避免 busy 卡死) */
void HCSR04_Abort(HCSR04_Handle_t *dev);

/* DWT 微秒延时 (72MHz 主频), 不依赖 HAL_Delay 的 1ms 粒度 */
void HCSR04_DelayUs(uint32_t us);

#endif
