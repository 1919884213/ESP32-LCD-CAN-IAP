#include "HCSR04.h"
#include "tim.h"
#include <stdint.h>

/*==========================================================================
 * HCSR04 超声波测距驱动实现
 * 使用 TIM2_CH3 (PA2) 输入捕获测量 ECHO 高电平时间。
 * 测量在中断回调中完成, 主程序通过 HCSR04_IsBusy() 轮询结果。
 *========================================================================*/

/* 距离换算: 声速 340m/s, 往返 -> 单程距离 = us * 0.17 mm
   整数实现: mm = us * 17 / 100, 避免浮点运算 */
#define HCSR04_MM_NUMERATOR 17
#define HCSR04_MM_DENOMINATOR 100

/* 当前驱动实例 (单模块假设, 由 HCSR04_Init 绑定) */
static HCSR04_Handle_t *s_dev = NULL;

void HCSR04_DelayUs(uint32_t us) {
  /* 启用 DWT 周期计数器 (72MHz -> 1 tick = 1/72 us) */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  uint32_t start = DWT->CYCCNT;
  uint32_t ticks = us * (SystemCoreClock / 1000000U);
  while ((DWT->CYCCNT - start) < ticks) {
  }
}

void HCSR04_Init(HCSR04_Handle_t *dev) {
  s_dev = dev;
  dev->busy = 0;
  dev->valid = 0;
  dev->edge = 0; /* 从上升沿开始 */
  dev->rise = 0;
  dev->fall = 0;
  dev->last_us = 0;
  dev->distance_mm = -1;

  /* F103 的 TIM2 为 16 位计数器, ARR 拉满 0xFFFF (CubeMX 已配置),
     ECHO 最长约 38ms < 65.5ms, 不会回绕 */
  __HAL_TIM_SET_AUTORELOAD(&htim2, 0xFFFFU);
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);

  /* 启动输入捕获中断 (初始为上升沿, 由 CubeMX 配置) */
  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_3);
}

void HCSR04_StartMeasure(HCSR04_Handle_t *dev) {
  if (dev->busy) {
    return; /* 上一次测量尚未完成 */
  }

  dev->busy = 1;
  dev->valid = 0;
  dev->edge = 0; /* 等待上升沿 */

  /* 清超时标志, 确保捕获极性为上升沿 */
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
  TIM_IC_InitTypeDef sConfigIC = {
      .ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING,
      .ICSelection = TIM_ICSELECTION_DIRECTTI,
      .ICPrescaler = TIM_ICPSC_DIV1,
      .ICFilter = 0,
  };
  HAL_TIM_IC_ConfigChannel(&htim2, &sConfigIC, TIM_CHANNEL_3);

  /* TRIG 拉高 >=10us 触发模块 */
  HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_SET);
  HCSR04_DelayUs(10);
  HAL_GPIO_WritePin(TRIG_GPIO_Port, TRIG_Pin, GPIO_PIN_RESET);
}

uint8_t HCSR04_IsBusy(HCSR04_Handle_t *dev) { return dev->busy; }

void HCSR04_Abort(HCSR04_Handle_t *dev) {
  dev->busy = 0;
  dev->valid = 0;
  dev->edge = 0;
  /* 恢复上升沿, 清残留标志, 准备下一次测量 */
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE | TIM_FLAG_CC3);
  TIM_IC_InitTypeDef s = {
      .ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING,
      .ICSelection = TIM_ICSELECTION_DIRECTTI,
      .ICPrescaler = TIM_ICPSC_DIV1,
      .ICFilter = 0,
  };
  HAL_TIM_IC_ConfigChannel(&htim2, &s, TIM_CHANNEL_3);
}

int32_t HCSR04_GetDistanceMm(HCSR04_Handle_t *dev) {
  if (!dev->valid) {
    return -1;
  }
  return dev->distance_mm;
}

/*==========================================================================
 * HAL 输入捕获回调 (中断上下文)
 * 上升沿记录起点并切换为下降沿, 下降沿计算高电平时长并切换回上升沿。
 * 32 位计数器无回绕, 无回波超时由任务层通过 HCSR04_Abort() 处理。
 *========================================================================*/
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance != TIM2 || s_dev == NULL) {
    return;
  }

  uint32_t capture = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);

  if (s_dev->edge == 0) {
    /* 上升沿: 记录起点, 切换为下降沿 */
    s_dev->rise = capture;
    s_dev->edge = 1;
    TIM_IC_InitTypeDef s = {
        .ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING,
        .ICSelection = TIM_ICSELECTION_DIRECTTI,
        .ICPrescaler = TIM_ICPSC_DIV1,
        .ICFilter = 0,
    };
    HAL_TIM_IC_ConfigChannel(htim, &s, TIM_CHANNEL_3);
  } else {
    /* 下降沿: 计算高电平时长 (1us/计数) */
    s_dev->fall = capture;
    uint32_t diff = s_dev->fall - s_dev->rise;
    s_dev->last_us = diff;
    s_dev->distance_mm = (int32_t)((diff * HCSR04_MM_NUMERATOR) /
                                   HCSR04_MM_DENOMINATOR);
    s_dev->valid = 1;
    s_dev->busy = 0;
    s_dev->edge = 0;
    /* 恢复上升沿, 准备下一次测量 */
    TIM_IC_InitTypeDef s = {
        .ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING,
        .ICSelection = TIM_ICSELECTION_DIRECTTI,
        .ICPrescaler = TIM_ICPSC_DIV1,
        .ICFilter = 0,
    };
    HAL_TIM_IC_ConfigChannel(htim, &s, TIM_CHANNEL_3);
  }
}
