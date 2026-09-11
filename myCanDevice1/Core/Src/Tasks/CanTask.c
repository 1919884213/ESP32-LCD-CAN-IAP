/* =============================================================================
 * 文件   : CanTask.c
 * 功能   : CAN 通信任务 —— 负责传感器数据的周期上报、故障上报与 IAP 在线升级
 *          命令处理。本任务从 EnvironmentQueue 队列取回环境采样快照，并以固定
 *          周期通过 CAN 总线发送环境数据帧 / 诊断帧 / 心跳帧。
 *
 * 调度说明（见下方表格）：
 *   - 任务主循环周期     : CAN_TASK_PERIOD_MS (10 ms)
 *   - 周期上报周期       : REPORT_PERIOD_MS (1000 ms)
 *   - 本任务读取 EnvironmentTask 发布的队列数据（队列长度 1，保留最新快照）
 * ========================================================================== */
#include "CANProtocol.h"
#include "Environment.h"
#include "can.h"
#include "kernel.h"
#include "queue.h"
#include "port.h"
#include "main.h"
#include "stm32f1xx_hal_can.h"
#include "stm32f1xx_hal_pwr.h"
#include "stm32f1xx_hal_rcc.h"
#include <string.h>

/* 由 EnvironmentTask 维护的环境邮箱（长度 1，保留最新采样快照） */
extern queue_t EnvironmentQueue;
/* 由 EnvironmentTask 维护的传感器故障标志（位域，见 SENSOR_FAULT_*） */
extern volatile uint8_t environment_fault;

/* 备份寄存器写入的魔数，供 Bootloader 判断是否进入 IAP 模式 */
#define IAP_ENTRY_MAGIC 0x4941U
/* CAN 任务主循环周期：10 ms */
#define CAN_TASK_PERIOD_MS 10U
/* 环境数据 / 诊断 / 心跳帧的周期上报周期：1000 ms */
#define REPORT_PERIOD_MS 1000U

/* CAN 发送帧头与邮箱号（跨函数复用） */
static CAN_TxHeaderTypeDef s_txHeader;
static uint32_t s_txMailbox;
/* 缓存最近一次从队列取回的环境采样快照 */
static Environment_Data_t s_lastSample;
/* 环境数据帧计数（每上报一次自增） */
static uint8_t s_dataCounter;
/* 故障帧序号（每发送一帧自增） */
static uint8_t s_faultSequence;
/* 去重标志：DHT11 故障已上报 */
static uint8_t s_dhtFaultReported;
/* 去重标志：NTC 故障已上报 */
static uint8_t s_ntcFaultReported;
/* 去重标志：CAN 总线离线故障已上报 */
static uint8_t s_canFaultReported;
/* 标志：最近一次 CAN 发送失败 */
static uint8_t s_canTxFailed;

static void CanHandleRx(uint32_t id, const uint8_t *data);
static void Iap_SetEntryFlag(uint16_t magic);
static void EnterIapMode(void);

/* ---------------------------------------------------------------------------
 * 配置 CAN 硬件滤波器。
 *   Bank 0 : 掩码模式，接收 ID 高 11 位为 0x400 的帧（即 0x400~0x7FF），
 *            送入 FIFO0（用于接收 IAP 控制/数据帧）。
 *   Bank 1 : ID 列表模式，只放行 CAN_ID_IAP_CTRL / CAN_ID_IAP_DATA，
 *            送入 FIFO1。
 * ------------------------------------------------------------------------- */
static void ConfigureCANFilter(void) {
  CAN_FilterTypeDef filter = {0};

  filter.FilterBank = 0U;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterIdHigh = (uint16_t)((0x400U << 21) >> 16);
  filter.FilterIdLow = (uint16_t)(0x400U << 21);
  filter.FilterMaskIdHigh = (uint16_t)((0x700U << 21) >> 16);
  filter.FilterMaskIdLow = (uint16_t)((0x700U << 21) | 0x06U);
  filter.FilterActivation = CAN_FILTER_ENABLE;
  if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK) {
    Error_Handler();
  }

  filter.FilterBank = 1U;
  filter.FilterMode = CAN_FILTERMODE_IDLIST;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO1;
  filter.FilterIdHigh = (uint16_t)((CAN_ID_IAP_CTRL << 21) >> 16);
  filter.FilterIdLow = (uint16_t)(CAN_ID_IAP_CTRL << 21);
  filter.FilterMaskIdHigh = (uint16_t)((CAN_ID_IAP_DATA << 21) >> 16);
  filter.FilterMaskIdLow = (uint16_t)(CAN_ID_IAP_DATA << 21);
  if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK) {
    Error_Handler();
  }
}

/* 通过标准帧发送一帧数据；失败时置位 s_canTxFailed，成功则清除 */
static uint8_t CAN_SendFrame(uint16_t id, uint8_t dlc, uint8_t *data) {
  s_txHeader.StdId = id;
  s_txHeader.ExtId = 0U;
  s_txHeader.IDE = CAN_ID_STD;
  s_txHeader.RTR = CAN_RTR_DATA;
  s_txHeader.DLC = dlc;
  s_txHeader.TransmitGlobalTime = DISABLE;

  if (HAL_CAN_AddTxMessage(&hcan, &s_txHeader, data, &s_txMailbox) != HAL_OK) {
    s_canTxFailed = 1U;
    return 0U;
  }
  s_canTxFailed = 0U;
  return 1U;
}

/* 从队列取回最新采样快照（非阻塞，队列空则沿用上次缓存） */
static uint8_t GetLatestSample(Environment_Data_t *out) {
  Environment_Data_t sample;
  if (queue_try_recv(&EnvironmentQueue, &sample)) {
    s_lastSample = sample;
  }
  *out = s_lastSample;
  return 1U;
}

/* 综合传感器故障与 CAN 发送状态，得出心跳状态：
 *   无故障            -> HEARTBEAT_STATUS_OK
 *   传感器全故障       -> HEARTBEAT_STATUS_FAULT
 *   部分故障           -> HEARTBEAT_STATUS_DEGRADED */
static uint8_t GetNodeStatus(void) {
  uint8_t sensor_fault = environment_fault;
  if (s_canTxFailed != 0U) {
    sensor_fault |= SENSOR_FAULT_CAN;
  }
  if (sensor_fault == 0U) {
    return HEARTBEAT_STATUS_OK;
  }
  if ((sensor_fault & (SENSOR_FAULT_DHT11 | SENSOR_FAULT_LIGHT |
                       SENSOR_FAULT_NTC)) ==
      (SENSOR_FAULT_DHT11 | SENSOR_FAULT_LIGHT | SENSOR_FAULT_NTC)) {
    return HEARTBEAT_STATUS_FAULT;
  }
  return HEARTBEAT_STATUS_DEGRADED;
}

/* 组装并发送一帧故障帧（含级别 / 代码 / 参数 / 锁存标志 / 序号） */
static void SendFault(uint8_t level, uint16_t code, uint16_t parameter,
                      uint8_t latch) {
  uint8_t data[8];
  pack_fault_frame(data, level, code, parameter, latch, s_faultSequence++);
  (void)CAN_SendFrame(CAN_ID_FAULT, 8U, data);
}

/* 周期检查故障条件，去重上报（仅在首次触发时报一次）：
 *   - DHT11 连续错误计数 >= 10 -> 上报 WARNING 级故障
 *   - NTC  连续错误计数 >= 10 -> 上报 WARNING 级故障
 *   - CAN 控制器进入 Bus-Off    -> 上报 EMERGENCY 级故障，并重启 CAN */
static void CheckFaults(void) {
  if (s_lastSample.dht_error_count >= 10U) {
    if (s_dhtFaultReported == 0U) {
      SendFault(FAULT_LEVEL_WARNING, FAULT_CODE_DHT11,
                s_lastSample.dht_error_count, 0U);
      s_dhtFaultReported = 1U;
    }
  } else {
    s_dhtFaultReported = 0U;
  }

  if (s_lastSample.ntc_error_count >= 10U) {
    if (s_ntcFaultReported == 0U) {
      SendFault(FAULT_LEVEL_WARNING, FAULT_CODE_NTC,
                s_lastSample.ntc_error_count, 0U);
      s_ntcFaultReported = 1U;
    }
  } else {
    s_ntcFaultReported = 0U;
  }

  if ((hcan.Instance->ESR & CAN_ESR_BOFF) != 0U) {
    if (s_canFaultReported == 0U) {
      SendFault(FAULT_LEVEL_EMERGENCY, FAULT_CODE_CAN,
                (uint16_t)((hcan.Instance->ESR >> 16) & 0xFFU), 1U);
      s_canFaultReported = 1U;
    }
    (void)HAL_CAN_Stop(&hcan);
    (void)HAL_CAN_Start(&hcan);
  } else {
    s_canFaultReported = 0U;
  }
}

/* 处理接收到的 CAN 帧：仅响应 IAP 控制帧 CAN_ID_IAP_CTRL，
 *   按命令分发：查询信息 -> 回 ACK；启动 IAP -> 进入升级模式；
 *              激活复位 -> 写标志并软复位。 */
static void CanHandleRx(uint32_t id, const uint8_t *data) {
  uint8_t ack[IAP_ACK_DLC];

  if (id != CAN_ID_IAP_CTRL) {
    return;
  }

  switch (data[0]) {
  case IAP_CMD_QUERY_INFO:
    build_iap_ack(ack, IAP_CMD_QUERY_INFO, IAP_STATUS_OK, data[1], 0U, 0U);
    (void)CAN_SendFrame(CAN_ID_IAP_ACK, IAP_ACK_DLC, ack);
    break;
  case IAP_CMD_START:
    EnterIapMode();
    break;
  case IAP_CMD_ACTIVATE_RESET:
    Iap_SetEntryFlag(0U);
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    HAL_Delay(1000U);
    NVIC_SystemReset();
    break;
  default:
    break;
  }
}

/* 从 FIFO0 与 FIFO1 轮询读出收到的标准数据帧（DLC>=2），统一交给 CanHandleRx */
static void HandleCanReceive(void) {
  CAN_RxHeaderTypeDef header = {0};
  uint8_t data[8] = {0};

  while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0U) {
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &header, data) == HAL_OK &&
        header.IDE == CAN_ID_STD && header.RTR == CAN_RTR_DATA &&
        header.DLC >= 2U) {
      CanHandleRx(header.StdId, data);
    }
  }
  while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO1) > 0U) {
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO1, &header, data) == HAL_OK &&
        header.IDE == CAN_ID_STD && header.RTR == CAN_RTR_DATA &&
        header.DLC >= 2U) {
      CanHandleRx(header.StdId, data);
    }
  }
}

/* 向备份寄存器 DR1 写入魔数，供 Bootloader 识别 IAP 入口 */
static void Iap_SetEntryFlag(uint16_t magic) {
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  BKP->DR1 = magic;
}

/* 写入 IAP 魔数后点亮 LED 并软复位，跳转到 Bootloader 升级流程 */
static void EnterIapMode(void) {
  Iap_SetEntryFlag(IAP_ENTRY_MAGIC);
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
  HAL_Delay(100U);
  NVIC_SystemReset();
}

/* 周期上报四帧：
 *   - 环境数据帧      CAN_ID_ENV_DATA   (温度/湿度/光照/状态/计数)
 *   - NTC 数据帧      CAN_ID_ENV_NTC    (NTC 电压/状态/错误计数/计数)
 *   - 环境诊断帧      CAN_ID_ENV_DIAG   (各错误计数/CAN
 * 错误计数/节点状态/版本/运行秒数)
 *   - 心跳帧          CAN_ID_HEARTBEAT
 * (节点状态/版本/错误计数/传感器故障/任务状态/运行秒数) 若 CAN
 * 发送失败，则将对应故障位并入状态与心跳帧。 */
static void SendPeriodicFrames(uint16_t uptime_sec) {
  uint8_t data[8];
  uint32_t esr = hcan.Instance->ESR;
  uint8_t rx_error = (uint8_t)((esr >> 8) & 0xFFU);
  uint8_t tx_error = (uint8_t)((esr >> 16) & 0xFFU);
  uint8_t status = s_lastSample.status;
  uint8_t sensor_fault = environment_fault;

  if (s_canTxFailed != 0U) {
    status |= ENV_STATUS_CAN_TX_ERR;
    sensor_fault |= SENSOR_FAULT_CAN;
  }
  pack_environment_frame(data, s_lastSample.temperature_x100,
                         s_lastSample.humidity_x100, s_lastSample.light_mv,
                         status, s_dataCounter++);
  (void)CAN_SendFrame(CAN_ID_ENV_DATA, 8U, data);

  pack_ntc_frame(data, s_lastSample.ntc_mv, status,
                 s_lastSample.ntc_error_count, s_dataCounter);
  (void)CAN_SendFrame(CAN_ID_ENV_NTC, 8U, data);

  pack_environment_diag_frame(
      data, s_lastSample.dht_error_count, s_lastSample.light_error_count,
      rx_error, tx_error, GetNodeStatus(), SW_VERSION, uptime_sec);
  (void)CAN_SendFrame(CAN_ID_ENV_DIAG, 8U, data);

  pack_heartbeat_frame(
      data, GetNodeStatus(), SW_VERSION, rx_error, tx_error, sensor_fault,
      TASK_STATE_CAN | TASK_STATE_DHT11 | TASK_STATE_LIGHT | TASK_STATE_NTC,
      uptime_sec);
  (void)CAN_SendFrame(CAN_ID_HEARTBEAT, 8U, data);
}

/* =============================================================================
 * 任务入口：StartCANTask
 * 职责   : 初始化 CAN 滤波器并启动 CAN，随后以 10 ms 周期循环执行
 *          「取最新采样 -> 收 CAN 帧 -> 查故障 -> 到点上报」。
 *
 * 帧上报周期（每 1000 ms 发送一轮）：
 *   | 帧类型       | ID      | 内容要点                              |
 *   |--------------|---------|---------------------------------------|
 *   | 环境数据帧   | 0x200   | 温度/湿度/光照/状态/帧计数             |
 *   | 环境诊断帧   | 0x201   | 错误计数/CAN 错误/节点状态/版本/运行秒 |
 *   | NTC 数据帧   | 0x202   | NTC 电压/状态/错误计数/帧计数          |
 *   | 心跳帧       | 0x703   | 状态/版本/错误/传感器故障/任务状态     |
 *   | 故障帧(事件) | 0x080   | 级别/代码/参数/锁存/序号（仅触发时发） |
 * ========================================================================== */
void StartCANTask(void *argument) {
  uint32_t tick = os_get_tick();
  uint32_t next_report = tick;
  (void)argument;

  memset(&s_lastSample, 0, sizeof(s_lastSample));
  s_lastSample.temperature_x100 = ENV_TEMP_INVALID;
  s_lastSample.humidity_x100 = ENV_HUMI_INVALID;
  s_lastSample.light_mv = ENV_LDR_INVALID;
  s_lastSample.ntc_mv = ENV_NTC_INVALID;

  ConfigureCANFilter();
  if (HAL_CAN_Start(&hcan) != HAL_OK) {
    Error_Handler();
  }

  for (;;) {
    GetLatestSample(&s_lastSample);
    HandleCanReceive();
    CheckFaults();

    if ((int32_t)(tick - next_report) >= 0) {
      SendPeriodicFrames((uint16_t)(tick / 1000U));
      next_report += REPORT_PERIOD_MS;
    }

    tick += CAN_TASK_PERIOD_MS;
    task_delay(CAN_TASK_PERIOD_MS);
  }
}
