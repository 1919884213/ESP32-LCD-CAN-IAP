/*!
 * @file    Bootloader.c
 * @brief   基于 CAN 总线的 IAP（In-Application Programming，应用内编程）引导加载程序
 * @details 上位机通过 CAN 报文向本设备下发固件数据，本文件负责：
 *          - 查询 / 开始 / 传输 / 校验 / 激活 / 进度查询 / 取消 等 IAP 会话管理
 *          - 将收到的固件数据写入 APP 区 Flash（地址范围 APP_START_ADDR ~ APP_END_ADDR）
 *          - 掉电保护：通过备份寄存器（BKP->DR1）中的魔数标记是否处于 IAP 升级状态
 *          - 升级成功后跳转到用户 APP
 *
 *          CAN 报文地址约定（见 BootLoader.h）：
 *          - BL_CAN_CTRL (0x601)  控制报文（命令下发）
 *          - BL_CAN_DATA (0x621)  数据报文（固件内容）
 *          - BL_CAN_ACK  (0x611)  应答报文（状态回传）
 *
 * @note    数据报文每帧携带 6 字节固件数据，一帧对应一个 "块(block)"。
 *          总块数 = ceil(image_len / 6)。块号 16 位，低位在前。
 */

#include "BootLoader.h"
#include "can.h"
#include "stm32f1xx_hal_can.h"
#include "stm32f1xx_hal_pwr.h"
#include "stm32f1xx_hal_rcc.h"

/* ------------------------- 控制命令定义 ------------------------- */
#define IAP_QUERY 0x01U    /*!< 查询命令：询问 BootLoader 是否在线/空闲 */
#define IAP_START 0x02U    /*!< 开始命令：携带固件长度与 CRC，并擦除 Flash */
#define IAP_DATA 0x03U     /*!< 数据命令：传输一帧固件数据（本文件内未用，控制流仅用于标识） */
#define IAP_FINISH 0x04U   /*!< 结束命令：对整个固件做 CRC 校验 */
#define IAP_ACTIVATE 0x05U /*!< 激活命令：校验通过后复位并跳转至用户 APP */
#define IAP_PROGRESS 0x06U /*!< 进度命令：查询当前升级进度状态 */
#define IAP_CANCEL 0x07U   /*!< 取消命令：中止本次升级会话 */

/* ------------------------- 应答状态定义 ------------------------- */
#define IAP_OK 0x00U          /*!< 成功 / 空闲 */
#define IAP_READY 0x01U       /*!< 已就绪（Flash 已擦除，等待数据） */
#define IAP_RECEIVING 0x02U   /*!< 接收中（数据块写入成功） */
#define IAP_VERIFY_OK 0x04U   /*!< CRC 校验通过 */
#define IAP_DONE 0x05U        /*!< 升级完成 */
#define IAP_ERR_LENGTH 0x80U  /*!< 长度错误 */
#define IAP_ERR_CRC 0x82U     /*!< CRC 校验错误 */
#define IAP_ERR_FLASH 0x83U   /*!< Flash 操作错误（擦除/编程失败） */
#define IAP_ERR_ADDRESS 0x86U /*!< 地址错误（越界或会话无效） */

/* ------------------------- 会话状态变量 ------------------------- */
static uint8_t s_active;     /*!< 是否处于活动升级会话（1=正在接收固件） */
static uint8_t s_verified;   /*!< 固件 CRC 是否已校验通过（1=通过） */
static uint8_t s_session;    /*!< 会话号：用于区分多次独立的升级流程 */
static uint16_t s_block;     /*!< 最近一次成功写入的数据块号 */
static uint8_t s_have_block; /*!< 是否已成功写入过至少一个数据块 */
static uint32_t s_image_len; /*!< 固件总长度（字节数，由 IAP_START 下发） */
static uint16_t s_image_crc; /*!< 固件期望 CRC16 校验值（由 IAP_START 下发） */

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

/**
 * @brief  跳转到用户 APP 程序
 * @note   仅在 main 中确认不需要进入 IAP 时调用
 * @warning 跳转前会彻底关闭外设、中断并重设向量表
 */
void BootLoader_JumpToApp(void) {
  if (app_is_marked_valid() == 0U) return;
  /* 从 APP 起始地址读取初始栈指针和复位向量 */
  uint32_t app_sp = *(volatile uint32_t *)APP_START_ADDR;
  uint32_t app_pc = *(volatile uint32_t *)(APP_START_ADDR + 4U);
  pFunction jump;

  /* 合法性校验：栈指针必须在 SRAM 区间(0x20000000~0x20005000)内且 4 字节对齐 */
  if (app_sp < 0x20000000U || app_sp > 0x20005000U || (app_sp & 3U) != 0U) {
    return;
  }
  /* 合法性校验：复位向量必须在 APP 区范围内，且为 Thumb 指令（最低位为 1） */
  if (app_pc < APP_START_ADDR || app_pc >= APP_END_ADDR ||
      (app_pc & 1U) == 0U) {
    return;
  }

  /* 关闭 HAL 外设初始化状态，避免与外设中断冲突 */
  HAL_DeInit();
  __disable_irq();
  /* 清空所有 NVIC 中断使能与挂起标志，防止 APP 初始化前被残留中断打断 */
  for (uint32_t i = 0U; i < 8U; ++i) {
    NVIC->ICER[i] = 0xFFFFFFFFU;
    NVIC->ICPR[i] = 0xFFFFFFFFU;
  }
  /* 停止 SysTick 定时器 */
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL = 0U;
  /* 将中断向量表重定位到 APP 起始地址 */
  SCB->VTOR = APP_START_ADDR;
  __DSB();
  __ISB();

  /* 设置主栈指针为 APP 的初始栈指针，然后跳转到复位向量执行 */
  __set_MSP(app_sp);
  jump = (pFunction)app_pc;
  jump();
}

/**
 * @brief  计算 CRC16（Modbus 多项式 0xA001，初始值 0xFFFF）
 * @param  data  待校验数据缓冲区指针
 * @param  length 数据长度（字节数）
 * @return 计算得到的 16 位 CRC 值
 */
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

/**
 * @brief  将魔数值写入备份寄存器 DR1，标记当前处于 IAP 升级状态
 * @param  value  要写入的魔数值（一般用 BL_IAP_MAGIC）
 * @note   备份寄存器在软件复位后内容保持，可跨复位传递状态。
 *         写入后由 BootLoader 启动判断是否继续 IAP 会话。
 */
static void set_magic(uint16_t value) {
  __HAL_RCC_PWR_CLK_ENABLE(); /* 使能电源接口时钟（访问 BKP 所需） */
  __HAL_RCC_BKP_CLK_ENABLE(); /* 使能备份域时钟 */
  HAL_PWR_EnableBkUpAccess(); /* 开放备份域写访问权限 */
  BKP->DR1 = value;           /* 写入魔数 */
}

/**
 * @brief  读取备份寄存器 DR1 中的魔数值
 * @return 存储的 16 位魔数
 */
static uint16_t get_magic(void) {
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_BKP_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();
  return (uint16_t)BKP->DR1;
}

/**
 * @brief  通过 CAN 发送应答帧
 * @param  command 应答对应的原始命令字
 * @param  status  应答状态码（见顶部 IAP_* 定义）
 * @param  retry   重试/续传标识（例如 CRC 失败时提示上位机重传）
 * @note   数据场布局：[cmd, status, session, block_l, block_h, 0, 0, retry]
 */
static void send_ack(uint8_t command, uint8_t status, uint8_t retry) {
  CAN_TxHeaderTypeDef header = {0};
  uint8_t data[8] = {
      command, status, s_session, (uint8_t)s_block, (uint8_t)(s_block >> 8),
      0U,      0U,     retry};
  uint32_t mailbox;

  header.StdId = BL_CAN_ACK; /* 应答使用固定标准帧 ID */
  header.IDE = CAN_ID_STD;
  header.RTR = CAN_RTR_DATA;
  header.DLC = 8U;
  (void)HAL_CAN_AddTxMessage(&hcan, &header, data, &mailbox);
}

/**
 * @brief  判断 APP 起始处的栈指针与复位向量是否合法
 * @return 1=存在合法 APP，0=无效
 * @note   用于激活前和开机跳转前双重校验
 */
static uint8_t valid_app(void) {
  uint32_t sp = *(volatile uint32_t *)APP_START_ADDR;
  uint32_t pc = *(volatile uint32_t *)(APP_START_ADDR + 4U);
  return sp >= 0x20000000U && sp <= 0x20005000U && (sp & 3U) == 0U &&
         pc >= APP_START_ADDR && pc < APP_END_ADDR && (pc & 1U) != 0U;
}

/**
 * @brief  擦除 APP 区全部 Flash 页
 * @return 1=擦除成功，0=失败
 * @note   STM32F1 中 BL_FLASH_PAGE_SIZE=1KB，页内地址由 PageAddress 对齐后擦除
 */
static uint8_t erase_app(void) {
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;

  if (HAL_FLASH_Unlock() != HAL_OK)
    return 0U; /* 解锁失败 */
  erase.TypeErase = FLASH_TYPEERASE_PAGES;
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

/**
 * @brief  以半字(16bit)为单位向 Flash 写入一段数据
 * @param  address 目标起始地址（必须在 APP 区且 2 字节对齐）
 * @param  data    待写入数据指针
 * @param  length  写入字节数（必须为偶数，且不能越出 APP 区）
 * @return 1=全部写入成功（含读回校验），0=失败
 */
static uint8_t write_bytes(uint32_t address, const uint8_t *data,
                           uint16_t length) {
  /* 参数合法性检查：长度非 0、偶数、地址对齐、不越界 */
  if (length == 0U || (length & 1U) != 0U || (address & 1U) != 0U ||
      address < APP_START_ADDR || address >= APP_END_ADDR ||
      length > APP_END_ADDR - address)
    return 0U;
  if (HAL_FLASH_Unlock() != HAL_OK)
    return 0U;
  for (uint16_t i = 0U; i < length; i += 2U) {
    uint16_t half_word = (uint16_t)data[i] | ((uint16_t)data[i + 1U] << 8);
    /* 编程半字并立即读回比对，确保写入正确 */
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + i, half_word) !=
            HAL_OK ||
        *(volatile uint16_t *)(address + i) != half_word) {
      HAL_FLASH_Lock();
      return 0U;
    }
  }
  HAL_FLASH_Lock();
  return 1U;
}

/**
 * @brief  处理控制帧（标准帧 ID = BL_CAN_CTRL）
 * @param  data  控制帧 8 字节数据：data[0] 为命令字
 * @note   各命令的数据布局：
 *         - IAP_START : [cmd][session][len_lo..len_hi][crc_lo][crc_hi]
 *         - 其余命令  : [cmd][...]
 */
static void handle_control(const uint8_t *data) {
  uint8_t command = data[0];

  if (command == IAP_QUERY) {
    /* 查询：直接应答 OK，表示 BootLoader 在线 */
    send_ack(command, IAP_OK, 1U);
  } else if (command == IAP_START) {
    /* 开始：解析会话号、固件长度与期望 CRC，并擦除 APP 区 */
    s_session = data[1];
    s_image_len = (uint32_t)data[2] | ((uint32_t)data[3] << 8) |
                  ((uint32_t)data[4] << 16) | ((uint32_t)data[5] << 24);
    s_image_crc = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
    s_block = 0U;         /* 重置块计数 */
    s_have_block = 0U;    /* 尚未接收任何数据块 */
    s_verified = 0U;      /* 复位校验状态 */
    /* 长度合法且擦除成功才激活会话 */
    s_active = s_image_len != 0U &&
               s_image_len <= APP_END_ADDR - APP_START_ADDR &&
               app_mark_invalid() && erase_app();
    send_ack(command, s_active != 0U ? IAP_READY : IAP_ERR_FLASH, 0U);
  } else if (command == IAP_FINISH) {
    /* 结束：对整个已写入的固件做 CRC16 校验 */
    if (s_active == 0U || s_image_len == 0U)
      send_ack(command, IAP_ERR_LENGTH, 0U); /* 会话无效或长度为 0 */
    else if (crc16((const uint8_t *)APP_START_ADDR, s_image_len) != s_image_crc)
      send_ack(command, IAP_ERR_CRC, 1U); /* 校验不匹配，retry=1 提示重传 */
    else if (app_mark_valid() == 0U)
      send_ack(command, IAP_ERR_FLASH, 0U);
    else {
      s_verified = 1U; /* CRC 与持久化有效标志均已通过 */
      send_ack(command, IAP_VERIFY_OK, 0U);
    }
  } else if (command == IAP_ACTIVATE) {
    /* 激活：校验通过且 APP 向量合法则复位跳转，否则报 CRC 错误 */
    if (s_verified == 0U || valid_app() == 0U)
      send_ack(command, IAP_ERR_CRC, 0U);
    else {
      send_ack(command, IAP_DONE, 0U);
      HAL_Delay(20U);       /* 留出时间让上位机收到应答 */
      set_magic(0U);        /* 清除 IAP 魔数，下次开机不再进入 IAP */
      HAL_CAN_Stop(&hcan);  /* 停止 CAN，避免跳转后残留中断 */
      NVIC_SystemReset();   /* 软件复位，复位后 BootLoader 将直接跳转 APP */
    }
  } else if (command == IAP_PROGRESS) {
    /* 进度查询：会话中返回 RECEIVING，否则返回 OK */
    send_ack(command, s_active != 0U ? IAP_RECEIVING : IAP_OK, 0U);
  } else if (command == IAP_CANCEL) {
    /* 取消：结束当前会话并复位状态 */
    s_active = 0U;
    s_verified = 0U;
    send_ack(command, IAP_OK, 0U);
  }
}

/**
 * @brief  处理数据帧（标准帧 ID = BL_CAN_DATA）
 * @param  data  数据帧 8 字节：[block_lo][block_hi][d0..d5]
 * @note   block 为 0 起始的块号，每块固定 6 字节固件数据；
 *         最后一帧若不足 6 字节，其余字节用 0xFF 填充后仍按偶数长度写入。
 */
static void handle_data(const uint8_t *data) {
  uint16_t block = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
  uint32_t offset = (uint32_t)block * 6U; /* 块号换算为 APP 区内偏移 */
  uint16_t count = 6U;
  uint8_t write_data[6] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};

  /* 会话未激活或块偏移超出固件长度：地址错误 */
  if (s_active == 0U || offset >= s_image_len) {
    send_ack(IAP_DATA, IAP_ERR_ADDRESS, 0U);
    return;
  }
  /* 重复块：说明上位机未收到上一条应答，重发同样 ACK 实现断点续传 */
  if (s_have_block != 0U && block == s_block) {
    send_ack(IAP_DATA, IAP_RECEIVING, 0U);
    return;
  }
  /* 计算本块实际有效字节数（末块可能不足 6 字节） */
  if (offset + count > s_image_len)
    count = (uint16_t)(s_image_len - offset);
  /* 保证写入长度为偶数（Flash 半字编程要求） */
  if ((count & 1U) != 0U)
    count++;
  /* 拷贝有效数据，不足部分保持 0xFF 填充 */
  for (uint16_t i = 0U; i < count && i < 6U; ++i)
    write_data[i] = data[i + 2U];

  /* 写入 APP 区，失败报 Flash 错误；成功记录块号并应答 RECEIVING */
  if (write_bytes(APP_START_ADDR + offset, write_data, count) == 0U) {
    send_ack(IAP_DATA, IAP_ERR_FLASH, 0U);
  } else {
    s_block = block;
    s_have_block = 1U;
    send_ack(IAP_DATA, IAP_RECEIVING, 0U);
  }
}

/**
 * @brief  初始化 CAN IAP：配置接收过滤器并启动 CAN，恢复 IAP 会话状态
 * @note   通过备份寄存器魔数判断：上次复位前是否处于升级状态。
 *         若魔数等于 BL_IAP_MAGIC，则进入 IAP 等待上位机续传。
 */
void CAN_IAP_Init(void) {
  CAN_FilterTypeDef filter = {0};
  /* 接收过滤器：ID 掩码模式，32 位，挂在 FIFO0，放行全部标准帧（掩码为 0） */
  filter.FilterBank = 0U;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = CAN_FILTER_ENABLE;
  if (HAL_CAN_ConfigFilter(&hcan, &filter) != HAL_OK ||
      HAL_CAN_Start(&hcan) != HAL_OK) {
    Error_Handler();
  }
  /* 魔数判断是否需要留在 BootLoader 进行升级 */
  s_active = get_magic() == BL_IAP_MAGIC ? 1U : 0U;
}

/**
 * @brief  处理 CAN IAP 主循环：轮询 FIFO0 并分发控制/数据帧
 * @note   需在 main 主循环中周期调用；一次调用会清空当前 FIFO0 所有报文
 */
void CAN_IAP_Process(void) {
  CAN_RxHeaderTypeDef header = {0};
  uint8_t data[8] = {0};
  while (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) != 0U) {
    if (HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &header, data) != HAL_OK)
      break;
    if (header.IDE != CAN_ID_STD)
      continue; /* 只处理标准帧 */
    if (header.StdId == BL_CAN_CTRL)
      handle_control(data);
    else if (header.StdId == BL_CAN_DATA)
      handle_data(data);
  }
}

/**
 * @brief  查询当前是否处于 IAP 模式
 * @return 1=处于 IAP（升级中或魔数标记未清除），0=正常应用模式
 * @note   用于主循环决定是否调用 BootLoader_JumpToApp 跳转 APP
 */
uint8_t CAN_IAP_IsActive(void) {
  return s_active != 0U || get_magic() == BL_IAP_MAGIC;
}
