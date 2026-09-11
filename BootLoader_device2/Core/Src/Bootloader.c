#include "BootLoader.h"
#include "can.h"
#include "stm32f1xx_hal_can.h"
#include "stm32f1xx_hal_pwr.h"
#include "stm32f1xx_hal_rcc.h"

#define IAP_QUERY       0x01U
#define IAP_START       0x02U
#define IAP_DATA        0x03U
#define IAP_FINISH      0x04U
#define IAP_ACTIVATE    0x05U
#define IAP_PROGRESS    0x06U
#define IAP_CANCEL      0x07U

#define IAP_OK          0x00U
#define IAP_READY       0x01U
#define IAP_RECEIVING   0x02U
#define IAP_VERIFY_OK   0x04U
#define IAP_DONE        0x05U
#define IAP_ERR_LENGTH  0x80U
#define IAP_ERR_CRC     0x82U
#define IAP_ERR_FLASH   0x83U
#define IAP_ERR_ADDRESS 0x86U

static uint8_t s_active;
static uint8_t s_verified;
static uint8_t s_session;
static uint16_t s_block;
static uint8_t s_have_block;
static uint32_t s_image_len;
static uint16_t s_image_crc;

static uint8_t app_is_marked_valid(void) {
  return *(volatile const uint16_t*)BL_FLAG_ADDR == BL_VALID_FLAG;
}

static uint8_t app_mark_invalid(void) {
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;
  if (HAL_FLASH_Unlock() != HAL_OK) return 0U;
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = FLASH_BANK_1;
  erase.PageAddress = BL_FLAG_PAGE_ADDR;
  erase.NbPages = 1U;
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &page_error);
  HAL_FLASH_Lock();
  return status == HAL_OK;
}

static uint8_t app_mark_valid(void) {
  uint8_t ok = 0U;
  if (HAL_FLASH_Unlock() == HAL_OK) {
    ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, BL_FLAG_ADDR,
                           BL_VALID_FLAG) == HAL_OK;
    HAL_FLASH_Lock();
  }
  return ok;
}

void BootLoader_JumpToApp(void) {
  if (app_is_marked_valid() == 0U) return;
  uint32_t app_sp = *(volatile uint32_t *)APP_START_ADDR;
  uint32_t app_pc = *(volatile uint32_t *)(APP_START_ADDR + 4U);
  pFunction jump;

  if (app_sp < 0x20000000U || app_sp > 0x20005000U ||
      (app_sp & 3U) != 0U) {
    return;
  }
  if (app_pc < APP_START_ADDR || app_pc >= APP_END_ADDR ||
      (app_pc & 1U) == 0U) {
    return;
  }

  HAL_DeInit();
  __disable_irq();
  for (uint32_t i = 0U; i < 8U; ++i) {
    NVIC->ICER[i] = 0xFFFFFFFFU;
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  SCB->VTOR = APP_START_ADDR;
  __DSB();
  __ISB();

  __set_MSP(app_sp);
  jump = (pFunction)app_pc;
  jump();
}

static uint16_t crc16(const uint8_t *data, uint32_t length) {
  uint16_t crc = 0xFFFFU;
  while (length-- != 0U) {
    crc ^= *data++;
    for (uint8_t i = 0U; i < 8U; ++i) {
      crc = (crc & 1U) != 0U ? (uint16_t)((crc >> 1) ^ 0xA001U)
                             : (uint16_t)(crc >> 1);
    }
  }
  return crc;
}

static void set_magic(uint16_t value) {
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  BKP->DR1 = value;
}

static uint16_t get_magic(void) {
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  return (uint16_t)BKP->DR1;
}

static void send_ack(uint8_t command, uint8_t status, uint8_t retry) {
  CAN_TxHeaderTypeDef header = {0};
  uint8_t data[8] = {command, status, s_session, (uint8_t)s_block,
                     (uint8_t)(s_block >> 8), 0U, 0U, retry};
  uint32_t mailbox;

  header.StdId = BL_CAN_ACK;
  header.IDE = CAN_ID_STD;
  header.RTR = CAN_RTR_DATA;
  header.DLC = 8U;
  (void)HAL_CAN_AddTxMessage(&hcan, &header, data, &mailbox);
}

static uint8_t valid_app(void) {
  uint32_t sp = *(volatile uint32_t *)APP_START_ADDR;
  uint32_t pc = *(volatile uint32_t *)(APP_START_ADDR + 4U);
  return sp >= 0x20000000U && sp <= 0x20005000U && (sp & 3U) == 0U &&
         pc >= APP_START_ADDR && pc < APP_END_ADDR && (pc & 1U) != 0U;
}

static uint8_t erase_app(void) {
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;

  if (HAL_FLASH_Unlock() != HAL_OK) return 0U;
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
  erase.Banks = FLASH_BANK_1;
  erase.NbPages = 1U;
  for (uint32_t address = APP_START_ADDR; address < APP_END_ADDR;
       address += BL_FLASH_PAGE_SIZE) {
    erase.PageAddress = address;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
      HAL_FLASH_Lock();
      return 0U;
    }
  }
  HAL_FLASH_Lock();
  return 1U;
}

static uint8_t write_bytes(uint32_t address, const uint8_t *data,
                           uint16_t length) {
  if (length == 0U || (length & 1U) != 0U || (address & 1U) != 0U ||
      address < APP_START_ADDR || address >= APP_END_ADDR ||
      length > APP_END_ADDR - address) return 0U;
  if (HAL_FLASH_Unlock() != HAL_OK) return 0U;
  for (uint16_t i = 0U; i < length; i += 2U) {
    uint16_t half_word = (uint16_t)data[i] | ((uint16_t)data[i + 1U] << 8);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + i, half_word) != HAL_OK ||
        *(volatile uint16_t *)(address + i) != half_word) {
      HAL_FLASH_Lock();
      return 0U;
    }
  }
  HAL_FLASH_Lock();
  return 1U;
}

static void handle_control(const uint8_t *data) {
  uint8_t command = data[0];

  if (command == IAP_QUERY) {
    send_ack(command, IAP_OK, 1U);
  } else if (command == IAP_START) {
    s_session = data[1];
    s_image_len = (uint32_t)data[2] | ((uint32_t)data[3] << 8) |
                  ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 24);
    s_image_crc = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
    s_block = 0U;
    s_have_block = 0U;
    s_verified = 0U;
    s_active = s_image_len != 0U &&
                s_image_len <= APP_END_ADDR - APP_START_ADDR &&
                app_mark_invalid() && erase_app();
    send_ack(command, s_active != 0U ? IAP_READY : IAP_ERR_FLASH, 0U);
  } else if (command == IAP_FINISH) {
    if (s_active == 0U || s_image_len == 0U) send_ack(command, IAP_ERR_LENGTH, 0U);
    else if (crc16((const uint8_t *)APP_START_ADDR, s_image_len) != s_image_crc)
      send_ack(command, IAP_ERR_CRC, 1U);
    else if (app_mark_valid() == 0U)
      send_ack(command, IAP_ERR_FLASH, 0U);
    else { s_verified = 1U; send_ack(command, IAP_VERIFY_OK, 0U); }
  } else if (command == IAP_ACTIVATE) {
    if (s_verified == 0U || valid_app() == 0U) send_ack(command, IAP_ERR_CRC, 0U);
    else {
      send_ack(command, IAP_DONE, 0U);
      HAL_Delay(20U);
      set_magic(0U);
      HAL_CAN_Stop(&hcan);
      NVIC_SystemReset();
    }
  } else if (command == IAP_PROGRESS) {
    send_ack(command, s_active != 0U ? IAP_RECEIVING : IAP_OK, 0U);
  } else if (command == IAP_CANCEL) {
    s_active = 0U;
    s_verified = 0U;
    send_ack(command, IAP_OK, 0U);
  }
}

static void handle_data(const uint8_t *data) {
  uint16_t block = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
  uint32_t offset = (uint32_t)block * 6U;
  uint16_t count = 6U;
  uint8_t write_data[6] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};

  if (s_active == 0U || offset >= s_image_len) {
    send_ack(IAP_DATA, IAP_ERR_ADDRESS, 0U);
    return;
  }
  if (s_have_block != 0U && block == s_block) {
    send_ack(IAP_DATA, IAP_RECEIVING, 0U);
    return;
  }
  if (offset + count > s_image_len) count = (uint16_t)(s_image_len - offset);
  if ((count & 1U) != 0U) count++;
  for (uint16_t i = 0U; i < count && i < 6U; ++i) write_data[i] = data[i + 2U];

  if (write_bytes(APP_START_ADDR + offset, write_data, count) == 0U) {
    send_ack(IAP_DATA, IAP_ERR_FLASH, 0U);
  } else {
    s_block = block;
    s_have_block = 1U;
    send_ack(IAP_DATA, IAP_RECEIVING, 0U);
  }
}

void CAN_IAP_Init(void) {
  CAN_FilterTypeDef filter = {0};
  filter.FilterBank = 0U;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = CAN_FILTER_ENABLE;
  if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK || HAL_CAN_Start(&hcan) != HAL_OK) {
    Error_Handler();
  }
  s_active = get_magic() == BL_IAP_MAGIC ? 1U : 0U;
}

void CAN_IAP_Process(void) {
  CAN_RxHeaderTypeDef header = {0};
  uint8_t data[8] = {0};
  while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) != 0U) {
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &header, data) != HAL_OK) break;
    if (header.IDE != CAN_ID_STD) continue;
    if (header.StdId == BL_CAN_CTRL) handle_control(data);
    else if (header.StdId == BL_CAN_DATA) handle_data(data);
  }
}

uint8_t CAN_IAP_IsActive(void) {
  return s_active != 0U || get_magic() == BL_IAP_MAGIC;
}
