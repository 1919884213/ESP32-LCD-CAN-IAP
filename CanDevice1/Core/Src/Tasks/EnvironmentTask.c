/* =============================================================================
 * 文件   : EnvironmentTask.c
 * 功能   : 环境采样任务 —— 周期读取 DHT11（温湿度）、光照（GL5528 经 ADC1）
 *          与热敏电阻（NTC 经 ADC2）传感器，将最新快照通过队列发布给 CAN 任务，
 *          并同步维护全局传感器故障标志。
 *
 * 调度说明：
 *   - 任务周期          : 1000 ms
 *   - DHT11 读取频率    : 交替执行（每 2 个周期读一次，降低慢速传感器占用）
 *   - 队列              : EnvironmentQueue（长度 1，保留最新快照，供 CanTask 消费）
 * ========================================================================== */
#include "Environment.h"
#include "CANProtocol.h"
#include "cmsis_os.h"
#include <string.h>

/* 由 freertos.c 创建的队列句柄，向 CanTask 发布环境快照 */
extern osMessageQueueId_t EnvironmentQueueHandle;

/* 全局环境数据快照（最新一次采样结果） */
Environment_Data_t environment_data;
/* 全局传感器故障标志（位域，见 SENSOR_FAULT_*），供 CanTask 读取 */
volatile uint8_t environment_fault = 0U;

/* =============================================================================
 * 任务入口：StartEnvironmentTask
 * 职责   : 初始化传感器并周期性采样，将结果发布到队列并刷新故障标志。
 *
 * 处理流程（每周期）：
 *   1) 读取环境数据（DHT11 按 read_dht11 交替读取）
 *   2) 依据 status 位域刷新 environment_fault（DHT11 / LDR / NTC 各自独立判定）
 *   3) 将最新快照写入队列（覆盖旧值，长度 1）
 *   4) 等待至下一 1000 ms 周期
 *
 * 采样内容与判定表：
 *   | 传感器      | 数据          | 判定位                     | 故障标志             |
 *   |-------------|---------------|----------------------------|----------------------|
 *   | DHT11       | 温湿度        | ENV_STATUS_DHT11_OK        | SENSOR_FAULT_DHT11   |
 *   | GL5528(ADC) | 光照电压(mV)  | ENV_STATUS_LDR_OK          | SENSOR_FAULT_LIGHT   |
 *   | NTC(ADC2)   | 分压电压(mV)  | ENV_STATUS_NTC_OK          | SENSOR_FAULT_NTC     |
 * ========================================================================== */
void StartEnvironmentTask(void *argument) {
  uint32_t tick = osKernelGetTickCount();
  uint8_t read_dht11 = 1U;
  (void)argument;

  /* 初始化全局快照为“无效”占位值 */
  memset(&environment_data, 0, sizeof(environment_data));
  environment_data.temperature_x100 = ENV_TEMP_INVALID;
  environment_data.humidity_x100 = ENV_HUMI_INVALID;
  environment_data.light_mv = ENV_LDR_INVALID;
  environment_data.ntc_mv = ENV_NTC_INVALID;

  /* 传感器初始化失败则直接置位全部传感器故障 */
  if (Environment_Init() != 0U) {
    environment_fault = SENSOR_FAULT_DHT11 | SENSOR_FAULT_LIGHT |
                        SENSOR_FAULT_NTC;
  }

  for (;;) {
    Environment_Read(&environment_data, read_dht11);
    read_dht11 = (uint8_t)!read_dht11; /* 交替读取 DHT11 */

    /* 根据 status 位域重新计算故障标志 */
    environment_fault = 0U;
    if ((environment_data.status & ENV_STATUS_DHT11_OK) == 0U) {
      environment_fault |= SENSOR_FAULT_DHT11;
    }
    if ((environment_data.status & ENV_STATUS_LDR_OK) == 0U) {
      environment_fault |= SENSOR_FAULT_LIGHT;
    }
    if ((environment_data.status & ENV_STATUS_NTC_OK) == 0U) {
      environment_fault |= SENSOR_FAULT_NTC;
    }

    /* 发布最新快照到队列（供 CanTask 消费） */
    (void)osMessageQueuePut(EnvironmentQueueHandle, &environment_data, 0U, 0U);
    tick += 1000U;
    osDelayUntil(tick);
  }
}
