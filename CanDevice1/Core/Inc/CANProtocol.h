#ifndef __CAN_PROTOCOL_H__
#define __CAN_PROTOCOL_H__

#include "main.h"

/* Same public protocol API as Device2; only node-specific IDs differ. */
#define CAN_ID_ENV_DATA   0x200U
#define CAN_ID_ENV_DIAG   0x201U
#define CAN_ID_ENV_NTC    0x202U
#define CAN_ID_FAULT      0x080U
#define CAN_ID_HEARTBEAT  0x703U
#define CAN_ID_IAP_CTRL   0x601U
#define CAN_ID_IAP_ACK    0x611U
#define CAN_ID_IAP_DATA   0x621U

#define NODE_ID 0x02U
#define SW_VERSION 1U

#define ENV_TEMP_INVALID ((int16_t)0x8000)
#define ENV_HUMI_INVALID 0xFFFFU
#define ENV_LDR_INVALID  0xFFFFU
#define ENV_NTC_INVALID  0xFFFFU

#define ENV_STATUS_DHT11_OK       0x01U
#define ENV_STATUS_LDR_OK         0x02U
#define ENV_STATUS_DHT11_TIMEOUT  0x04U
#define ENV_STATUS_DHT11_CHECKSUM 0x08U
#define ENV_STATUS_ADC_INVALID    0x10U
#define ENV_STATUS_CAN_TX_ERR     0x20U
#define ENV_STATUS_NTC_OK         0x40U

#define HEARTBEAT_STATUS_BOOTING  0U
#define HEARTBEAT_STATUS_OK       1U
#define HEARTBEAT_STATUS_DEGRADED 2U
#define HEARTBEAT_STATUS_FAULT    3U

#define SENSOR_FAULT_DHT11 0x01U
#define SENSOR_FAULT_LIGHT 0x02U
#define SENSOR_FAULT_CAN   0x04U
#define SENSOR_FAULT_NTC   0x08U

#define TASK_STATE_DHT11 0x01U
#define TASK_STATE_LIGHT 0x02U
#define TASK_STATE_CAN   0x04U
#define TASK_STATE_NTC   0x08U

#define FAULT_LEVEL_INFO      0U
#define FAULT_LEVEL_WARNING   1U
#define FAULT_LEVEL_CRITICAL  2U
#define FAULT_LEVEL_EMERGENCY 3U

#define FAULT_CODE_DHT11 0x0001U
#define FAULT_CODE_LIGHT 0x0002U
#define FAULT_CODE_CAN   0x0003U
#define FAULT_CODE_NTC   0x0004U

#define IAP_CMD_QUERY_INFO     0x01U
#define IAP_CMD_START          0x02U
#define IAP_CMD_SEND_BLOCK     0x03U
#define IAP_CMD_END_VERIFY     0x04U
#define IAP_CMD_ACTIVATE       0x05U
#define IAP_CMD_ACTIVATE_RESET 0x05U
#define IAP_CMD_QUERY_PROG     0x06U
#define IAP_CMD_CANCEL         0x07U

#define IAP_STATUS_OK           0x00U
#define IAP_STATUS_READY        0x01U
#define IAP_STATUS_RECEIVING    0x02U
#define IAP_STATUS_WAIT_RETRANS 0x03U
#define IAP_STATUS_VERIFY_OK    0x04U
#define IAP_STATUS_DONE         0x05U
#define IAP_STATUS_LEN_ERR      0x80U
#define IAP_STATUS_BLOCK_CRC    0x81U
#define IAP_STATUS_CRC32_ERR    0x82U
#define IAP_STATUS_FLASH_ERR    0x83U
#define IAP_STATUS_VERSION_ERR  0x84U
#define IAP_STATUS_TIMEOUT      0x85U
#define IAP_STATUS_ADDR_ERR     0x86U

#define IAP_ACK_DLC  8U
#define IAP_CTRL_DLC 8U

static inline void pack_u16(uint8_t *buf, uint16_t value) {
  buf[0] = (uint8_t)(value & 0xFFU);
  buf[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static inline void pack_i16(uint8_t *buf, int16_t value) {
  pack_u16(buf, (uint16_t)value);
}

static inline void pack_u32(uint8_t *buf, uint32_t value) {
  buf[0] = (uint8_t)(value & 0xFFU);
  buf[1] = (uint8_t)((value >> 8) & 0xFFU);
  buf[2] = (uint8_t)((value >> 16) & 0xFFU);
  buf[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static inline uint16_t unpack_u16(const uint8_t *buf) {
  return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

static inline uint32_t unpack_u32(const uint8_t *buf) {
  return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static inline void pack_environment_frame(uint8_t *data, int16_t temperature,
                                          uint16_t humidity, uint16_t light_mv,
                                          uint8_t status, uint8_t counter) {
  pack_i16(&data[0], temperature);
  pack_u16(&data[2], humidity);
  pack_u16(&data[4], light_mv);
  data[6] = status;
  data[7] = counter;
}

static inline void pack_ntc_frame(uint8_t *data, uint16_t ntc_mv,
                                  uint8_t status, uint8_t ntc_error,
                                  uint8_t counter) {
  pack_u16(&data[0], ntc_mv);
  data[2] = status;
  data[3] = ntc_error;
  data[4] = counter;
  data[5] = 0U;
  data[6] = 0U;
  data[7] = 0U;
}

static inline void pack_environment_diag_frame(
    uint8_t *data, uint8_t dht_error, uint8_t light_error,
    uint8_t rx_error, uint8_t tx_error, uint8_t node_status,
    uint8_t sw_version, uint16_t uptime_sec) {
  data[0] = dht_error;
  data[1] = light_error;
  data[2] = rx_error;
  data[3] = tx_error;
  data[4] = node_status;
  data[5] = sw_version;
  pack_u16(&data[6], uptime_sec);
}

static inline void pack_heartbeat_frame(
    uint8_t *data, uint8_t status, uint8_t sw_version, uint8_t rx_error,
    uint8_t tx_error, uint8_t sensor_fault, uint8_t task_state,
    uint16_t uptime_sec) {
  data[0] = status;
  data[1] = sw_version;
  data[2] = rx_error;
  data[3] = tx_error;
  data[4] = sensor_fault;
  data[5] = task_state;
  pack_u16(&data[6], uptime_sec);
}

static inline void pack_fault_frame(uint8_t *data, uint8_t level,
                                    uint16_t code, uint16_t parameter,
                                    uint8_t latch, uint8_t sequence) {
  data[0] = NODE_ID;
  data[1] = level;
  pack_u16(&data[2], code);
  pack_u16(&data[4], parameter);
  data[6] = latch;
  data[7] = sequence;
}

static inline void build_iap_ack(uint8_t *data, uint8_t command,
                                 uint8_t status, uint8_t session,
                                 uint16_t block, uint16_t error) {
  data[0] = command;
  data[1] = status;
  data[2] = session;
  pack_u16(&data[3], block);
  pack_u16(&data[5], error);
  data[7] = 0U;
}

#endif
