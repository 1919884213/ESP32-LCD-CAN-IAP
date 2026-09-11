#ifndef W25Q64_H
#define W25Q64_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 使用 SPI3，避免占用 Flash/PSRAM 使用的 SPI0、SPI1 及 LCD 的 SPI2。 */
#define W25Q64_SPI_HOST SPI3_HOST
/* W25Q64 引脚：DI、CLK、DO、CS。 */
#define W25Q64_PIN_MOSI GPIO_NUM_9
#define W25Q64_PIN_SCLK GPIO_NUM_10
#define W25Q64_PIN_MISO GPIO_NUM_46
#define W25Q64_PIN_CS GPIO_NUM_11
#define W25Q64_SPI_CLOCK_HZ (20 * 1000 * 1000) /* 初始通信频率 20 MHz */

#define W25Q64_CMD_JEDEC_ID 0x9F /* 读取厂商、型号和容量 ID */
#define W25Q64_CMD_READ_DATA 0x03
#define W25Q64_CMD_WRITE_ENABLE 0x06
#define W25Q64_CMD_PAGE_PROGRAM 0x02
#define W25Q64_CMD_READ_STATUS1 0x05
#define W25Q64_CMD_SECTOR_ERASE 0x20

#define W25Q64_TOTAL_SIZE (8U * 1024U * 1024U)
#define W25Q64_PAGE_SIZE 256U
#define W25Q64_SECTOR_SIZE 4096U
#define W25Q64_TRANSFER_SIZE W25Q64_PAGE_SIZE
#define W25Q64_READY_TIMEOUT_MS 1000U

/* W25Q64 的物理槽位（实际地址空间）。四个 STM32 逻辑槽各对半切成 A/B
 * 两个相同的半区，下载写非活动半区、成功后切换，从而保留上一版本。
 *
 * W25Q64 8 MiB Flash 分区布局表：
 * +----------------------+----------------+----------------+----------+
 * | 槽位 (W25Q64_Slot_t) | 起始地址       | 结束地址       | 容量     |
 * +----------------------+----------------+----------------+----------+
 * | META                 | 0x000000       | 0x020000       | 128 KiB  |
 * | STM32_1_A            | 0x020000       | 0x060000       | 256 KiB  |
 * | STM32_1_B            | 0x060000       | 0x0A0000       | 256 KiB  |
 * | STM32_2_A            | 0x0A0000       | 0x0E0000       | 256 KiB  |
 * | STM32_2_B            | 0x0E0000       | 0x120000       | 256 KiB  |
 * | STM32_3_A            | 0x120000       | 0x160000       | 256 KiB  |
 * | STM32_3_B            | 0x160000       | 0x1A0000       | 256 KiB  |
 * | STM32_4_A            | 0x1A0000       | 0x1E0000       | 256 KiB  |
 * | STM32_4_B            | 0x1E0000       | 0x220000       | 256 KiB  |
 * | ESP32S3              | 0x220000       | 0x420000       | 2 MiB    |
 * | COMMON               | 0x420000       | 0x800000       | 3.875 MiB|
 * +----------------------+----------------+----------------+----------+
 * 合计：0x800000（8 MiB），与 W25Q64_TOTAL_SIZE 一致。
 */
typedef enum {
  W25Q64_SLOT_META = 0,
  W25Q64_SLOT_STM32_1_A,
  W25Q64_SLOT_STM32_1_B,
  W25Q64_SLOT_STM32_2_A,
  W25Q64_SLOT_STM32_2_B,
  W25Q64_SLOT_STM32_3_A,
  W25Q64_SLOT_STM32_3_B,
  W25Q64_SLOT_STM32_4_A,
  W25Q64_SLOT_STM32_4_B,
  W25Q64_SLOT_ESP32S3,
  W25Q64_SLOT_COMMON,
  W25Q64_SLOT_COUNT,
} W25Q64_Slot_t;

/* 逻辑固件槽：每个对应 STM32_*_A / STM32_*_B 两个物理半区。 */
typedef enum {
  W25Q64_FW_STM32_1 = 0,
  W25Q64_FW_STM32_2,
  W25Q64_FW_STM32_3,
  W25Q64_FW_STM32_4,
  W25Q64_FW_COUNT,
} W25Q64_FwSlot_t;

/* bin 文件名最大长度（含结束符）。 */
#define W25Q64_BIN_NAME_MAX 40U

typedef struct {
  uint32_t address;
  uint32_t size;
} W25Q64_SlotInfo_t;

/* 下载成功后记录在 META 区的镜像信息，用于按实际长度读取槽位固件。
 * name 为下载的 bin 文件名（不含路径），供界面显示。 */
typedef struct {
  uint32_t size;
  uint32_t crc32;
  char name[W25Q64_BIN_NAME_MAX];
} W25Q64_ImageInfo_t;

esp_err_t W25Q64_Init(void);
esp_err_t W25Q64_ReadJedecId(uint32_t* jedec_id);

/* 原始 Flash 操作：地址必须位于 8 MiB W25Q64 地址范围内。 */
esp_err_t W25Q64_Read(uint32_t address, uint8_t* data, size_t length);
esp_err_t W25Q64_Write(uint32_t address, const uint8_t* data, size_t length);
esp_err_t W25Q64_EraseSector(uint32_t address);

/* 槽位操作：自动检查 offset 与 length，不会跨越槽位边界。 */
esp_err_t W25Q64_GetSlotInfo(W25Q64_Slot_t slot, W25Q64_SlotInfo_t* info);
esp_err_t W25Q64_ReadSlot(W25Q64_Slot_t slot,
                          uint32_t offset,
                          uint8_t* data,
                          size_t length);
esp_err_t W25Q64_WriteSlot(W25Q64_Slot_t slot,
                           uint32_t offset,
                           const uint8_t* data,
                           size_t length);
esp_err_t W25Q64_EraseSlot(W25Q64_Slot_t slot);

/* 保存和读取槽位已验证镜像的长度与 CRC32；未记录时读取返回 ESP_ERR_NOT_FOUND。
 */
esp_err_t W25Q64_SaveImageInfo(W25Q64_Slot_t slot,
                               const W25Q64_ImageInfo_t* info);
esp_err_t W25Q64_LoadImageInfo(W25Q64_Slot_t slot, W25Q64_ImageInfo_t* info);
esp_err_t W25Q64_ClearImageInfo(W25Q64_Slot_t slot);

/* 保存和读取槽位实际写入长度；未记录时读取返回 ESP_ERR_NOT_FOUND。 */
esp_err_t W25Q64_SaveUsetageInfo(W25Q64_Slot_t slot, uint32_t used_size);
esp_err_t W25Q64_GetUsetageInfo(W25Q64_Slot_t slot, uint32_t* used_size);
esp_err_t W25Q64_ClearUsetageInfo(W25Q64_Slot_t slot);

/* 逻辑槽 A/B 双备份：活动半区记录在 META，掉电保持。 */
/* 逻辑槽 + 半区（0=A,1=B）映射为物理槽。 */
W25Q64_Slot_t W25Q64_FwSlotToSlot(W25Q64_FwSlot_t fw, uint8_t half);
/* 返回逻辑槽当前活动半区；从未记录过时默认 STM32_x_A。 */
esp_err_t W25Q64_GetActiveSlot(W25Q64_FwSlot_t fw, W25Q64_Slot_t* slot);
/* 返回下一次下载应写入的物理半区：活动半区有有效镜像时返回另一半，
 * 否则返回活动半区（首次从 A 开始）。 */
W25Q64_Slot_t W25Q64_GetDownloadSlot(W25Q64_FwSlot_t fw);
/* CRC 校验通过后切换逻辑槽的活动半区。 */
esp_err_t W25Q64_SetActiveSlot(W25Q64_FwSlot_t fw, W25Q64_Slot_t slot);
/* 槽位文件名/活动半区变更计数，供 UI 判断是否需要重建下拉框。 */
uint32_t W25Q64_GetNameRevision(void);
#endif
