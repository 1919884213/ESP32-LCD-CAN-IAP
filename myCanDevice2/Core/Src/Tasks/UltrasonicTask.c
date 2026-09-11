#include "FreeRTOS.h"
#include "HCSR04.h"
#include "SSD1306.h"
#include "cmsis_os.h"
#include "cmsis_os2.h"
#include "main.h"
#include "stm32f1xx_hal_def.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

extern osMessageQueueId_t DistanceQueueHandle;
/* 超声波测距实例 */
HCSR04_Handle_t g_hcsr04;

void StartUltrasonicTask(void *argument) {
  (void)argument;

  int32_t dist_mm = -1;
  uint32_t cm_int, cm_frac = 0;
  uint8_t sda_lvl = 0, scl_lvl = 0;

  /* 初始化超声波驱动 (TIM2_CH3 输入捕获) */
  HCSR04_Init(&g_hcsr04);

  /* OLED 上电复位需要约 100ms, 立即初始化会导致命令丢失全黑 */
  osDelay(100);

  /* 总线空闲电平自检: 任一为低说明接线/供电异常 */
  SSD1306_BusIdleCheck(&sda_lvl, &scl_lvl);
  printf("OLED bus: SDA=%u SCL=%u %s\r\n", sda_lvl, scl_lvl,
         (sda_lvl && scl_lvl) ? "OK" : "LOW! check wiring/power");
  osDelay(1000); /*串口发送后延时1s*/


  for (;;) {
    /* 触发一次测量 */
    HCSR04_StartMeasure(&g_hcsr04);

    /* 等待测量完成 (通常 1~30ms, 超时 100ms) */
    uint32_t timeout = osKernelGetTickCount() + 100;
    while (HCSR04_IsBusy(&g_hcsr04) && (osKernelGetTickCount() < timeout)) {
      osDelay(1);
    }
    if (HCSR04_IsBusy(&g_hcsr04)) {
      HCSR04_Abort(&g_hcsr04); /* 无回波, 复位避免卡死 */
    }

    dist_mm = HCSR04_GetDistanceMm(&g_hcsr04);

    if (dist_mm >= 0) {
      /* 整数拼一位小数: mm -> cm(整数位.小数位), 避免浮点 printf */
      cm_int = (uint32_t)dist_mm / 100u;
      cm_frac = ((uint32_t)dist_mm % 100u) / 10u;
      printf("US: %u.%u cm\r\n", (unsigned)cm_int, (unsigned)cm_frac);
      osDelay(1000); /*串口发送后延时1s*/
    } else {
      printf("US: no echo\r\n");
      osDelay(1000); /*串口发送后延时1s*/
    }

    osMessageQueuePut(DistanceQueueHandle,&g_hcsr04, 0, 0);



    osDelay(100);
  }
}
