#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "main.h"

#define BL_FLASH_BASE      0x08000000UL
#define BL_SIZE            0x00004000UL
#define APP_START_ADDR     0x08004000UL
#define APP_END_ADDR       0x08010000UL
#define BL_FLASH_PAGE_SIZE 0x00000400UL
#define BL_FLAG_PAGE_ADDR  (BL_FLASH_BASE + BL_SIZE - BL_FLASH_PAGE_SIZE)
#define BL_FLAG_ADDR       (BL_FLASH_BASE + BL_SIZE - 2U)
#define BL_VALID_FLAG      0xA55AU
#define BL_CAN_CTRL        0x600U
#define BL_CAN_ACK         0x610U
#define BL_CAN_DATA        0x620U
#define BL_IAP_MAGIC       0x4941U

typedef void (*pFunction)(void);

void BootLoader_JumpToApp(void);
void CAN_IAP_Init(void);
void CAN_IAP_Process(void);
uint8_t CAN_IAP_IsActive(void);

#endif
