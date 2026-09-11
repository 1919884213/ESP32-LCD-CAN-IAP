#include "CANProtocol.h"
#include "FreeRTOS.h"
#include "HCSR04.h"
#include "MPU6050.h"
#include "SSD1306.h"
#include "can.h"
#include "cmsis_os.h"
#include "cmsis_os2.h"
#include "main.h"
#include "stm32f1xx.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_can.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_pwr.h"
#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_hal_tim.h"
#include "task.h"
#include "usart.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern osMessageQueueId_t MPU6050QueueHandle;
extern volatile uint8_t mpu6050_fault;
extern osMessageQueueId_t DistanceQueueHandle;

#define IAP_ENTRY_MAGIC 0x4941U /* BKP_DR1 中的 IAP 入口标志 */

static CAN_TxHeaderTypeDef s_txHeader; /*发送报文头*/
static uint32_t s_txMailbox;           /*发送邮箱*/
static MPU6050_Data_t s_lastSample;    /*陀螺仪上次采样的数据*/
static HCSR04_Handle_t s_lastDist;     /*超声波上次采样数据*/
static uint8_t s_dataCounter = 0;      /*加速度报文序列号*/
static uint32_t s_uptimeSec = 0;       /*设备运行时长*/
static uint16_t s_lastTxId = 0;        /*最近发送帧 ID, 供 OLED 显示*/
static uint8_t s_lastTxDlc = 0;        /*最近发送帧 DLC*/
static uint8_t s_lastTxData[8] = {0};  /*最近发送帧数据域*/
static int16_t s_lastRoll = 0;         /*最近姿态角 0.01°, 供 OLED 显示*/
static int16_t s_lastPitch = 0;
static char ssd_buf[32];               /*SSD缓冲区*/
static void CanHandleRx(uint32_t id, const uint8_t *data); /*处理上位机命令*/
static void Iap_SetEntryFlag(uint16_t magic); /*向备份寄存器写升级标志位*/
static void EnterIapMode(void);               /*软件复位,进入BootLoader程序*/

/*
 * 配置 bxCAN 的两个硬件滤波器组，将收到的报文按 ID 分类路由到两个接收
 * FIFO，硬件不匹配的帧直接丢弃。
 *
 * 说明：32 位缩放模式下标准 ID 占寄存器 [31:21]，故写入前先 id << 21 再
 * 拆成高低 16 位；掩码/列表位定义：bit1 = RTR，bit2 = IDE。
 *
 * 滤波器配置一览：
 *
 * +-------+-------+---------------+------------------------------------------+
 * | 组号  | 队列  | 模式          | 接收的报文                               |
 * +-------+-------+---------------+------------------------------------------+
 * | 0     | FIFO0 | 掩码(32位)    | 标准数据帧，ID 0x400~0x4FF（控制命令）   |
 * |       |       |               |   FilterId = 0x400 << 21（基准 ID）      |
 * |       |       |               |   Mask = 0x700 << 21 | 0x06：            |
 * |       |       |               |     高 3 位须为 100b -> 0x4XX            |
 * |       |       |               |     0x06 强制 IDE=0、RTR=0               |
 * +-------+-------+---------------+------------------------------------------+
 * | 1     | FIFO1 | 列表(32位)    | 仅精确匹配两个 ID（每组最多 2 个）：     |
 * |       |       |               |   第 1 个 = CAN_ID_IAP_CTRL（IAP 控制）  |
 * |       |       |               |   第 2 个 = CAN_ID_IAP_DATA（IAP 数据）  |
 * +-------+-------+---------------+------------------------------------------+
 *
 * 主循环中 HandleCanReceive() 分别排空 FIFO0/FIFO1 后统一交给
 * CanHandleRx() 分发处理。
 */
static void ConfigureCANFilter(void) {
  CAN_FilterTypeDef filter = {0};
  /* Bank 0 / FIFO0：预留 0x400~0x4FF 给运行时控制命令 */
  filter.FilterBank = 0;
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
  /* Bank 1 / FIFO1：只收 IAP 控制帧和数据帧 */
  filter.FilterBank = 1;
  filter.FilterMode = CAN_FILTERMODE_IDLIST;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO1;
  filter.FilterIdHigh = (uint16_t)((CAN_ID_IAP_CTRL << 21) >> 16);
  filter.FilterIdLow = (uint16_t)(CAN_ID_IAP_CTRL << 21);
  filter.FilterMaskIdHigh = (uint16_t)((CAN_ID_IAP_DATA << 21) >> 16);
  filter.FilterMaskIdLow = (uint16_t)(CAN_ID_IAP_DATA << 21);
  filter.FilterActivation = CAN_FILTER_ENABLE;
  if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK) {
    Error_Handler();
  }
}
/*发送一帧CAN报文*/
static uint8_t CAN_SendFrame(uint16_t id, uint8_t dlc, uint8_t *data) {
  s_txHeader.StdId = id;         /*报文ID*/
  s_txHeader.IDE = CAN_ID_STD;   /*标准帧*/
  s_txHeader.RTR = CAN_RTR_DATA; /*数据帧*/
  s_txHeader.DLC = dlc;          /*数据长度*/
  if (HAL_CAN_AddTxMessage(&hcan, &s_txHeader, data, &s_txMailbox) != HAL_OK) {
    printf("CAN tx failed id=0x%03lX err=0x%08lX ESR=0x%08lX\r\n",
           (unsigned long)id, hcan.ErrorCode,
           (unsigned long)hcan.Instance->ESR);
    osDelay(1000); /*串口发送后延时1s*/
    return 0;
  }
  /*记录最近一帧, 供 OLED 显示*/
  s_lastTxId = id;
  s_lastTxDlc = dlc;
  memcpy(s_lastTxData, data, 8);
  return 1;
}
/*从消息队列中获取MPU6050数据*/
static uint8_t GetLatestSample(MPU6050_Data_t *out) {
  MPU6050_Data_t tmp;
  if (osMessageQueueGet(MPU6050QueueHandle, &tmp, NULL, 0) == osOK) {
    s_lastSample = tmp;
  }
  *out = s_lastSample;
  return 1;
}
/*从消息队列中获取HCSR04数据*/
static uint32_t GetLatestDist(HCSR04_Handle_t *out) {
  HCSR04_Handle_t tmp;
  if (osMessageQueueGet(DistanceQueueHandle, &tmp, 0, 0) == osOK)
    s_lastDist = tmp;
  *out = s_lastDist;
  return 1;
}

/*==========================================================================
 * CanHandleRx() —— CAN 接收命令分发
 *--------------------------------------------------------------------------
 * 功能    : 解析并响应上位机下发的控制帧（当前处理 0x600 命令）
 * 入参    : id   - CAN 标准帧 ID
 *           data - 帧数据首地址（DLC 至少 2 字节）
 * 返回    : void
 *--------------------------------------------------------------------------
 * 命令表 (ID = 0x600) :
 * +-------------------+-------+------------------------------------------+
 * | 命令 (data[0])    | 值    | 行为                                      |
 * +-------------------+-------+------------------------------------------+
 * | IAP_CMD_QUERY_INFO| 0x01  | 回 ACK(0x610)：cmd=0x01, status=OK,      |
 * |                   |       | session=data[1], block=0, err=0           |
 * | IAP_CMD_START     | 0x02  | 进入 IAP 模式                              |
 * | IAP_CMD_ACTIVATE_ | 0x05  | 清除引导标志、LED 指示、延时 1s 后软复位   |
 * | RESET             |       |                                          |
 * +-------------------+-------+------------------------------------------+
 *========================================================================*/
static void CanHandleRx(uint32_t id, const uint8_t *data) {
  uint8_t ack[IAP_ACK_DLC];

  if (id == CAN_ID_IAP_CTRL) {
    switch (data[0]) {
    case IAP_CMD_QUERY_INFO:
      build_iap_ack(ack, IAP_CMD_QUERY_INFO, IAP_STATUS_OK, data[1], 0U, 0U);
      CAN_SendFrame(CAN_ID_IAP_ACK, IAP_ACK_DLC, ack);
      break;
    case IAP_CMD_START:
      EnterIapMode();
      break;
    case IAP_CMD_ACTIVATE_RESET:
      Iap_SetEntryFlag(0U);
      HAL_GPIO_WritePin(BOARD_LED_GPIO_Port, BOARD_LED_Pin, RESET);
      HAL_Delay(1000);
      NVIC_SystemReset();
      break;
    default:
      break;
    }
  }
}
/*获取CAN报文(只接收标准帧) --> CanHandleRx()分发报文*/
static void HandleCanReceive(void) {
  CAN_RxHeaderTypeDef rxHeader = {0};
  uint8_t rxData[8] = {0};
  while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) > 0) {
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &rxHeader, rxData) ==
        HAL_OK) {
      if (rxHeader.IDE == CAN_ID_STD) {
        CanHandleRx(rxHeader.StdId, rxData);
      }
    }
  }
  while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO1) > 0) {
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO1, &rxHeader, rxData) ==
        HAL_OK) {
      if (rxHeader.IDE == CAN_ID_STD) {
        CanHandleRx(rxHeader.StdId, rxData);
      }
    }
  }
}
/*向备份寄存器写入升级标志*/
static void Iap_SetEntryFlag(uint16_t magic) {
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  BKP->DR1 = magic;
}
/*软件复位 --> 进入BootLoader*/
static void EnterIapMode(void) {
  Iap_SetEntryFlag(IAP_ENTRY_MAGIC);
  HAL_GPIO_WritePin(BOARD_LED_GPIO_Port, BOARD_LED_Pin, SET);
  HAL_Delay(100U);
  NVIC_SystemReset();
}

static int16_t ComputeRollFromAccel(const MPU6050_Data_t *s) {
  double roll = atan2((double)s->ay, (double)s->az);
  return (int16_t)(roll * (180.0 / 3.14159265358979) * 100.0);
}

static int16_t ComputePitchFromAccel(const MPU6050_Data_t *s) {
  double pitch = atan2((double)(-s->ax),
                       sqrt((double)s->ay * s->ay + (double)s->az * s->az));
  return (int16_t)(pitch * (180.0 / 3.14159265358979) * 100.0);
}

/*==========================================================================
 * OLED 显示辅助 —— 仅在 CANTask 上下文调用
 * (SSD1306 驱动共用一帧缓冲且无锁, 禁止多个任务同时绘制)
 *========================================================================*/
/*按 21 字符定宽写一行(不足补空格), 避免上一帧较长内容留下残影*/
static void OLED_DrawLine(uint8_t y, const char *text) {
  char line[22];
  uint8_t i = 0;
  for (; i < 21 && text[i] != '\0'; i++) {
    line[i] = text[i];
  }
  for (; i < 21; i++) {
    line[i] = ' ';
  }
  line[21] = '\0';
  SSD1306_DrawString(0, y, line);
}

/*百分之一度 -> "+12.34" 带符号字符串, 负数先取绝对值再补符号*/
static void OLED_FormatDeg(char *dst, int16_t centideg) {
  uint32_t a = (centideg < 0) ? (uint32_t)(-centideg) : (uint32_t)centideg;
  snprintf(dst, 8, "%c%lu.%02lu", (centideg < 0) ? '-' : '+',
           (unsigned long)(a / 100u), (unsigned long)(a % 100u));
}

/*刷新 OLED: 运行时长/超声波距离/最近 CAN 帧(原始 hex)/IMU 状态*/
static void OLED_Refresh(void) {
  char deg[8], deg2[8];
  uint8_t i;

  /*y=0: 运行时长*/
  snprintf(ssd_buf, sizeof(ssd_buf), "UP %lu s",
           (unsigned long)s_uptimeSec);
  OLED_DrawLine(0, ssd_buf);

  /*y=8: 超声波距离, 无效时显示占位避免误读旧值*/
  if (s_lastDist.valid) {
    snprintf(ssd_buf, sizeof(ssd_buf), "D:%ld.%ldcm V:1 B:%u",
             (long)(s_lastDist.distance_mm / 100),
             (long)((s_lastDist.distance_mm % 100) / 10),
             (unsigned)s_lastDist.busy);
  } else {
    snprintf(ssd_buf, sizeof(ssd_buf), "D:----.-cm V:0 B:%u",
             (unsigned)s_lastDist.busy);
  }
  OLED_DrawLine(8, ssd_buf);

  /*y=16+24: 最近发送帧 ID/DLC + 数据域十六进制*/
  snprintf(ssd_buf, sizeof(ssd_buf), "TX 0x%03X [%u]",
           (unsigned)s_lastTxId, (unsigned)s_lastTxDlc);
  OLED_DrawLine(16, ssd_buf);
  for (i = 0; i < 8; i++) {
    sprintf(&ssd_buf[i * 2], "%02X", s_lastTxData[i]);
  }
  OLED_DrawLine(24, ssd_buf);

  /*y=32: 加速度 mg (与 0x101 帧相同换算)*/
  snprintf(ssd_buf, sizeof(ssd_buf), "A%6d%6d%6d",
           (int)((int32_t)s_lastSample.ax * 1000 / ACCEL_LSB_PER_G),
           (int)((int32_t)s_lastSample.ay * 1000 / ACCEL_LSB_PER_G),
           (int)((int32_t)s_lastSample.az * 1000 / ACCEL_LSB_PER_G));
  OLED_DrawLine(32, ssd_buf);

  /*y=40: 姿态角(度), 0.01° 拆整数+两位小数*/
  OLED_FormatDeg(deg, s_lastRoll);
  OLED_FormatDeg(deg2, s_lastPitch);
  snprintf(ssd_buf, sizeof(ssd_buf), "R%s P%s", deg, deg2);
  OLED_DrawLine(40, ssd_buf);

  /*y=48: 传感器健康状态*/
  snprintf(ssd_buf, sizeof(ssd_buf), "IMU:%s US:%s",
           mpu6050_fault ? "FAULT" : "OK",
           s_lastDist.valid ? "OK" : "NO");
  OLED_DrawLine(48, ssd_buf);

  SSD1306_UpdateScreen();
}

void StartCANTask(void *argument) {

  uint32_t tick = osKernelGetTickCount(); /*设备心跳*/
  uint8_t altToggle = 0;

  memset(&s_lastSample, 0, sizeof(s_lastSample));
  ConfigureCANFilter();
  if (HAL_CAN_Start(&hcan) != HAL_OK) {
    Error_Handler();
  }
  printf("CAN started: 50k, normal, PA11/PA12\r\n");
  osDelay(1000); /*串口发送后延时1s*/

  while (1) {
    tick += 500;
    /*从队列中取数据*/
    GetLatestSample(&s_lastSample);
    GetLatestDist(&s_lastDist);
    /* 交替发送两类报文：偶数周期发加速度帧(0x101)，奇数周期发运动帧(0x100)，
       两条 ID 各占一半发送周期，节省总线带宽 */
    if (altToggle) {
      uint8_t accelData[8];
      uint8_t flags = MOTION_FLAG_IMU_VALID;
      if (mpu6050_fault) {
        flags &=
            ~MOTION_FLAG_IMU_VALID; /*IMU故障时清除有效标志，告知上位机数据不可用*/
      }
      /* 打包加速度帧：把原始 LSB 计数值换算成毫g(mg)单位。
         ax*1000/ACCEL_LSB_PER_G 先乘后除避免整数除法丢精度，
         (int32_t)中间变量防止 int16_t 相乘溢出，±2g 量程下 1g=16384 计数 */
      pack_accel_frame(
          accelData,
          (int16_t)((int32_t)s_lastSample.ax * 1000 / ACCEL_LSB_PER_G),
          (int16_t)((int32_t)s_lastSample.ay * 1000 / ACCEL_LSB_PER_G),
          (int16_t)((int32_t)s_lastSample.az * 1000 / ACCEL_LSB_PER_G), flags,
          s_dataCounter++); /*数据域固定为 8
                               字节，加速度字节不满，剩余位用来放标志和序列号*/
      CAN_SendFrame(CAN_ID_ACCEL, 8, accelData);

    } else {
      uint8_t motionData[8];
      /* 用加速度计计算姿态角：静止时等效于重力方向倾角，单位为百分之一度(0.01°)
       */
      int16_t roll = ComputeRollFromAccel(&s_lastSample);   /*绕X轴角度*/
      int16_t pitch = ComputePitchFromAccel(&s_lastSample); /*绕Y轴角度*/
      s_lastRoll = roll; /*留档供 OLED 显示*/
      s_lastPitch = pitch;
      /* 打包运动帧：speed/rpm 在本任务中未使用，填 0 占位 */
      pack_motion_frame(motionData, 0, 0, roll, pitch);
      CAN_SendFrame(CAN_ID_MOTION, 8, motionData);

      /* 打包距离帧： */
      uint8_t distData[8];
      pack_dist_frame(distData, s_lastDist.distance_mm, s_lastDist.valid,
                      s_lastDist.busy);
      CAN_SendFrame(CAN_ID_DIST, 8, distData);
    }
    altToggle = !altToggle;

    if ((tick % 1000u) == 0) {
      uint8_t wheelData[8];
      pack_wheel_frame(wheelData, 0, 0, 0);
      CAN_SendFrame(CAN_ID_WHEEL, 8, wheelData);

      uint8_t hbData[8];
      uint32_t esr = hcan.Instance->ESR;
      uint8_t rxErr = (uint8_t)((esr >> 8) & 0xFF);
      uint8_t txErr = (uint8_t)((esr >> 16) & 0xFF);
      uint8_t sensorFault = mpu6050_fault ? SENSOR_FAULT_IMU : 0;
      pack_heartbeat_frame(hbData, HEARTBEAT_STATUS_OK, SW_VERSION, rxErr,
                           txErr, sensorFault, 1,
                           (uint16_t)(s_uptimeSec & 0xFFFF));
      CAN_SendFrame(CAN_ID_HEARTBEAT, 8, hbData);
    }

    s_uptimeSec = tick / 1000U;
    HandleCanReceive(); /*接收报文然后分发*/
    OLED_Refresh();     /*刷新 OLED 显示(运行时长/距离/最近CAN帧/IMU)*/
    osDelayUntil(tick);
  }
}
