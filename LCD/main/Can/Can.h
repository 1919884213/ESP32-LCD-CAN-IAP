#pragma once
#include <stdbool.h>
#include "W25Q64.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"

bool Can_init(void);
esp_err_t Can_transmit(uint32_t id, uint8_t* info);
bool Can_receive(twai_message_t* message, TickType_t timeout);
bool Can_rx_acquire(void);
void Can_rx_release(void);

typedef enum {
  CAN_IAP_STATE_READY,
  CAN_IAP_STATE_TRANSFERRING,
  CAN_IAP_STATE_VERIFYING,
  CAN_IAP_STATE_ACTIVATING,
  CAN_IAP_STATE_SUCCESS,
  CAN_IAP_STATE_FAILED,
} can_iap_state_t;

/* 异步启动指定节点的 CAN IAP，从指定 W25Q64 槽读取已验证的 bin。 */
esp_err_t Can_iap_start(uint32_t node_id, W25Q64_Slot_t slot);
const char* Can_iap_status(void);
bool Can_iap_running(void);
uint8_t Can_iap_progress(void);
can_iap_state_t Can_iap_get_state(void);
