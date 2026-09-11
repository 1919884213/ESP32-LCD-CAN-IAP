/**
 * @file Can.c
 * @brief TWAI(CAN) 总线驱动封装 + 基于 CAN 的 IAP 固件升级（OTA over CAN）。
 *
 * 分两部分：
 * 1. 基础收发：Can_init/Can_transmit/Can_receive，供上层任务解析业务报文；
 * 2. IAP 升级：从 W25Q64 中已验证的固件槽读取镜像，通过
 *    控制帧(0x600/0x601)+ACK帧(0x610/0x611)+数据帧(0x620/0x621)三组 ID
 *    分块发送给目标节点（STM32 BootLoader），完成"复位->启动->传数据->
 *    校验->激活"流程，并用进度/状态接口供 UI 展示。
 */

#include "Can.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "hal/twai_types_deprecated.h"
#include "string.h"

static const char* TAG = "CAN";
/* 以下 IAP 状态对 UI 可见，均为 volatile：由 iap_task 写、UI 任务读 */
static volatile const char* s_iap_status =
    "就绪";                            /* 文本状态，如 "发送 42%" */
static volatile bool s_iap_running;     /* IAP 任务是否在运行 */
static volatile uint8_t s_iap_progress; /* 发送进度 0~100 */
static volatile can_iap_state_t s_iap_state = CAN_IAP_STATE_READY; /* 状态机 */
static bool s_can_ready; /* TWAI 驱动是否已启动 */
/* 二进制信号量，守护 CAN 接收权：空闲为 1，IAP 进行中为 0 */
static SemaphoreHandle_t s_rx_sem;

/* IAP 协议 ID：A 组对应节点2、B 组对应节点1（见 iap_task 中的选择逻辑）。
 * CTRL = 控制命令（复位/开始/校验/激活），ACK = 目标节点应答，
 * DATA = 6 字节有效载荷的数据分块帧 */
#define IAP_CTRL_A 0x600U
#define IAP_ACK_A 0x610U
#define IAP_DATA_A 0x620U
#define IAP_CTRL_B 0x601U
#define IAP_ACK_B 0x611U
#define IAP_DATA_B 0x621U

/* Modbus 风格 CRC16（初值 0xFFFF，多项式 0xA001），用于镜像完整性校验 */
static uint16_t iap_crc16_update(uint16_t crc, const uint8_t* data,
                                 size_t len) {
  while (len-- != 0U) {
    crc ^= *data++;
    for (uint8_t i = 0U; i < 8U; ++i)
      crc =
          (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
  }
  return crc;
}

/* 逐块读取 W25Q64 槽位，计算 IAP 协议所需的 CRC16。 */
static esp_err_t iap_slot_crc16(W25Q64_Slot_t slot,
                                size_t length,
                                uint16_t* crc) {
  if (crc == NULL || length == 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t buffer[W25Q64_TRANSFER_SIZE];
  uint32_t offset = 0;
  uint16_t value = 0xFFFFU;
  while (offset < length) {
    const size_t chunk = (length - offset) > sizeof(buffer)
                             ? sizeof(buffer)
                             : length - offset;
    esp_err_t err = W25Q64_ReadSlot(slot, offset, buffer, chunk);
    if (err != ESP_OK) {
      return err;
    }
    value = iap_crc16_update(value, buffer, chunk);
    offset += (uint32_t)chunk;
  }
  *crc = value;
  return ESP_OK;
}

/* 在 timeout 内等待目标节点的 ACK 帧：标准帧、data[0]=cmd 且
 * data[1]==expected 才算成功；状态不符或超时返回 false */
static bool iap_wait_ack(uint32_t ack_id,
                         uint8_t cmd,
                         uint8_t expected,
                         TickType_t timeout) {
  twai_message_t msg = {0};
  TickType_t deadline = xTaskGetTickCount() + timeout;
  while ((int32_t)(deadline - xTaskGetTickCount()) > 0) {
    TickType_t remain = deadline - xTaskGetTickCount();
    if (!Can_receive(&msg, remain))
      continue;
    if (!msg.extd && !msg.rtr && msg.identifier == ack_id &&
        msg.data_length_code >= 2U && msg.data[0] == cmd) {
      if (msg.data[1] != expected) {
        ESP_LOGE(TAG,
                 "IAP ACK error id=0x%03" PRIX32
                 " cmd=0x%02X status=0x%02X expected=0x%02X",
                 ack_id, cmd, msg.data[1], expected);
        return false;
      }
      return true;
    }
  }
  ESP_LOGE(TAG, "IAP ACK timeout id=0x%03" PRIX32 " cmd=0x%02X", ack_id, cmd);
  return false;
}

/* IAP 升级任务主体（由 Can_iap_start 创建，node_id: 1 或 2）。
 * 流程：
 *   1) CTRL(0x02,1) 请求 APP 复位，目标写 BKP 标志后进 BootLoader；
 *   2) CTRL(0x02,1)+len+crc16 再次发送，等 BootLoader
 * ACK(cmd=0x02,status=0x01)； 3) 按 6 字节/块循环发 DATA，每块等
 * ACK(cmd=0x03,status=0x02)，失败重试 3 次； 4) CTRL(0x04,1) 触发校验，等
 * ACK(cmd=0x04,status=0x04)； 5) CTRL(0x05,1) 激活新固件，等
 * ACK(cmd=0x05,status=0x05)。 任意一步失败跳转 fail，置 FAILED
 * 状态并归还接收权信号量。 */
static void iap_task(void* arg) {
  const uintptr_t request = (uintptr_t)arg;
  const uint32_t node_id = (uint32_t)(request >> 8U);
  const W25Q64_Slot_t slot = (W25Q64_Slot_t)(request & 0xFFU);
  const uint32_t ctrl = node_id == 1U ? IAP_CTRL_B : IAP_CTRL_A;
  const uint32_t ack = node_id == 1U ? IAP_ACK_B : IAP_ACK_A;
  const uint32_t data_id = node_id == 1U ? IAP_DATA_B : IAP_DATA_A;
  W25Q64_ImageInfo_t image_info;
  uint8_t payload[8] = {0};

  if (W25Q64_LoadImageInfo(slot, &image_info) != ESP_OK ||
      image_info.size > 48U * 1024U) {
    goto fail;
  }
  const size_t image_len = image_info.size;
  uint16_t crc = 0;
  if (iap_slot_crc16(slot, image_len, &crc) != ESP_OK) {
    goto fail;
  }

  /* 先让应用写 BKP 标志并复位进入 BootLoader。 */
  payload[0] = 0x02U;
  payload[1] = 1U;
  s_iap_status = "请求复位";
  ESP_LOGI(TAG, "IAP node%lu request APP reset: ctrl=0x%03" PRIX32,
           (unsigned long)node_id, ctrl);
  if (Can_transmit(ctrl, payload) != ESP_OK)
    goto fail;
  vTaskDelay(pdMS_TO_TICKS(1500));

  memset(payload, 0, sizeof(payload));
  payload[0] = 0x02U;
  payload[1] = 1U;
  memcpy(&payload[2], &image_len, 4U);
  memcpy(&payload[6], &crc, 2U);
  s_iap_status = "等待引导程序";
  ESP_LOGI(TAG, "IAP START len=%u crc16=0x%04X ctrl=0x%03" PRIX32,
           (unsigned)image_len, crc, ctrl);
  if (Can_transmit(ctrl, payload) != ESP_OK ||
      !iap_wait_ack(ack, 0x02U, 0x01U, pdMS_TO_TICKS(3000)))
    goto fail;
  s_iap_status = "发送 0%";
  s_iap_state = CAN_IAP_STATE_TRANSFERRING;

  /* 数据帧格式：byte0-1 = 块号(小端)，byte2-7 = 最多 6 字节镜像数据，
   * 不足 6 字节用 0xFF 填充到 8 字节 DLC */
  for (uint16_t block = 0U; (size_t)block * 6U < image_len; ++block) {
    size_t offset = (size_t)block * 6U;
    size_t count = image_len - offset;
    if (count > 6U)
      count = 6U;
    memset(payload, 0xFF, sizeof(payload));
    payload[0] = (uint8_t)block;
    payload[1] = (uint8_t)(block >> 8);
    if (W25Q64_ReadSlot(slot, offset, &payload[2], count) != ESP_OK) {
      goto fail;
    }
    bool block_ok = false;
    for (uint8_t retry = 0U; retry < 3U && !block_ok; ++retry) {
      esp_err_t tx_err = Can_transmit(data_id, payload);
      block_ok = (tx_err == ESP_OK) &&
                 iap_wait_ack(ack, 0x03U, 0x02U, pdMS_TO_TICKS(300));
      if (!block_ok && retry < 2U)
        ESP_LOGW(TAG, "IAP DATA retry block=%u attempt=%u tx=%s",
                 (unsigned)block, (unsigned)(retry + 2U),
                 esp_err_to_name(tx_err));
    }
    if (!block_ok) {
      ESP_LOGE(
          TAG,
          "IAP DATA failed block=%u offset=%u count=%u data_id=0x%03" PRIX32,
          (unsigned)block, (unsigned)offset, (unsigned)count, data_id);
      goto fail;
    }
    if ((block & 0x0FU) == 0U) /* 每 16 块打一条进度日志 */
      ESP_LOGI(TAG, "IAP DATA block=%u/%u (%u%%)", (unsigned)block,
               (unsigned)((image_len + 5U) / 6U),
               (unsigned)(((offset + count) * 100U) / image_len));
    /* 每 64 块（或收尾）刷新一次对外进度，减少 volatile 写与 UI 刷新频率 */
    if ((block & 0x3FU) == 0U || offset + count >= image_len) {
      static char progress[24];
      s_iap_progress = (uint8_t)(((offset + count) * 100U) / image_len);
      snprintf(progress, sizeof(progress), "发送 %u%%",
               (unsigned)s_iap_progress);
      s_iap_status = progress;
    }
  }

  /* 校验：让 BootLoader 回读 Flash 与 len/crc16 比对 */
  memset(payload, 0, sizeof(payload));
  payload[0] = 0x04U;
  payload[1] = 1U;
  s_iap_status = "校验中";
  s_iap_state = CAN_IAP_STATE_VERIFYING;
  if (Can_transmit(ctrl, payload) != ESP_OK ||
      !iap_wait_ack(ack, 0x04U, 0x04U, pdMS_TO_TICKS(3000)))
    goto fail;
  payload[0] = 0x05U;
  payload[1] = 1U;
  s_iap_status = "激活中";
  s_iap_state = CAN_IAP_STATE_ACTIVATING;
  if (Can_transmit(ctrl, payload) != ESP_OK ||
      !iap_wait_ack(ack, 0x05U, 0x05U, pdMS_TO_TICKS(1000)))
    goto fail;
  s_iap_status = "成功";
  s_iap_progress = 100U;
  s_iap_state = CAN_IAP_STATE_SUCCESS;
  s_iap_running = false;
  xSemaphoreGive(s_rx_sem);
  vTaskDelete(NULL);
  return;
fail:
  s_iap_status = "失败";
  s_iap_state = CAN_IAP_STATE_FAILED;
  s_iap_running = false;
  xSemaphoreGive(s_rx_sem);
  vTaskDelete(NULL);
}

/* 异步启动指定节点（1 或 2）的 IAP 升级：读取指定 W25 槽位记录并创建任务。 */
esp_err_t Can_iap_start(uint32_t node_id, W25Q64_Slot_t slot) {
  if (node_id != 1U && node_id != 2U)
    return ESP_ERR_INVALID_ARG;
  W25Q64_ImageInfo_t image_info;
  if (W25Q64_LoadImageInfo(slot, &image_info) != ESP_OK ||
      image_info.size > 48U * 1024U) {
    s_iap_status = "槽位为空/过大";
    s_iap_progress = 0U;
    s_iap_state = CAN_IAP_STATE_FAILED;
    return ESP_ERR_NOT_FOUND;
  }
  if (!s_can_ready || s_rx_sem == NULL) {
    s_iap_status = "CAN 未就绪";
    s_iap_state = CAN_IAP_STATE_FAILED;
    return ESP_ERR_INVALID_STATE;
  }
  if (s_iap_running) {
    return ESP_ERR_INVALID_STATE;
  }
  s_iap_running = true;
  s_iap_progress = 0U;
  s_iap_state = CAN_IAP_STATE_TRANSFERRING;
  s_iap_status = "启动中";
  /* IAP 期间持有接收权信号量，阻塞 Can_task 的接收，避免抢走 ACK 帧 */
  xSemaphoreTake(s_rx_sem, portMAX_DELAY);
  const uintptr_t request = ((uintptr_t)node_id << 8U) | (uintptr_t)slot;
  if (xTaskCreate(iap_task, "can_iap", 4096, (void*)request, 18, NULL) !=
      pdPASS) {
    s_iap_running = false;
    s_iap_status = "失败";
    s_iap_state = CAN_IAP_STATE_FAILED;
    xSemaphoreGive(s_rx_sem);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

/* 当前 IAP 文本状态（"就绪"/"发送 xx%"/"成功"/"失败" 等），供 UI 显示
 */
const char* Can_iap_status(void) {
  return (const char*)s_iap_status;
}

/* IAP 任务是否正在运行 */
bool Can_iap_running(void) {
  return s_iap_running;
}

/* 发送进度百分比 0~100 */
uint8_t Can_iap_progress(void) {
  return s_iap_progress;
}

/* 当前 IAP 状态机状态，见 can_iap_state_t */
can_iap_state_t Can_iap_get_state(void) {
  return s_iap_state;
}

/* 初始化 TWAI：TX=GPIO13，RX=GPIO12；波特率 50 kbit/s
 * （APB 80MHz/brp160=500kHz tq，位时间 1+8+1=10 tq，采样点 90%），
 * 不设硬件过滤（接收全部 ID）。成功创建接收权信号量并启动驱动 */
bool Can_init(void) {
  s_can_ready = false;
  if (s_rx_sem == NULL)
    s_rx_sem = xSemaphoreCreateBinary();
  if (s_rx_sem == NULL) {
    ESP_LOGE(TAG, "CAN receive semaphore allocation failed");
    return false;
  }
  xSemaphoreGive(s_rx_sem); /* 初始化为"接收权空闲" */
  twai_general_config_t cfg1 =
      TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_13, GPIO_NUM_12, TWAI_MODE_NORMAL);
  twai_timing_config_t cfg2 = {
      .brp = 160, .tseg_1 = 8, .tseg_2 = 1, .sjw = 2, .triple_sampling = false};
  twai_filter_config_t cfg3 = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  esp_err_t err = twai_driver_install(&cfg1, &cfg2, &cfg3);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "twai_driver_install failed: %s", esp_err_to_name(err));
    return false;
  }
  err = twai_start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "twai_start failed: %s", esp_err_to_name(err));
    return false;
  }
  s_can_ready = true;
  ESP_LOGI(TAG, "TWAI driver started");
  return true;
}
/* 发送一帧标准数据帧：固定 DLC=8，从 info 拷贝 8 字节（不足部分由调用方填充）。
 * 发送队列满时最多阻塞 100 ms */
esp_err_t Can_transmit(uint32_t id, uint8_t* info) {
  if (!s_can_ready || info == NULL)
    return ESP_ERR_INVALID_STATE;
  twai_message_t message = {
      .identifier = id, .extd = 0, .rtr = 0, .data_length_code = 8};
  memcpy(message.data, info, 8);
  return twai_transmit(&message, pdMS_TO_TICKS(100));
}

/* 接收一帧报文，timeout 内无报文返回 false。调用方需先持有接收权
 * （Can_rx_acquire 或由 IAP 任务独占），避免两个消费者争抢 RX 队列 */
bool Can_receive(twai_message_t* message, TickType_t timeout) {
  return s_can_ready && message != NULL &&
         twai_receive(message, timeout) == ESP_OK;
}

/* 尝试获取 CAN 接收权（非阻塞）。成功需用 Can_rx_release 释放。 */
bool Can_rx_acquire(void) {
  return s_rx_sem != NULL && xSemaphoreTake(s_rx_sem, 0) == pdPASS;
}

/* 释放 CAN 接收权，供获取成功后调用 */
void Can_rx_release(void) {
  if (s_rx_sem != NULL)
    xSemaphoreGive(s_rx_sem);
}
