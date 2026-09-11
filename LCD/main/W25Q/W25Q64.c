#include "W25Q64.h"
#include "freertos/semphr.h"

/* META 128 KiB；四个 STM32 逻辑槽各对半切成 A/B 两个 256 KiB 半区；
 * ESP32 槽 2 MiB；其余为公共槽。 */
static const W25Q64_SlotInfo_t s_slots[W25Q64_SLOT_COUNT] = {
    [W25Q64_SLOT_META] = {.address = 0x000000, .size = 0x020000},
    [W25Q64_SLOT_STM32_1_A] = {.address = 0x020000, .size = 0x040000},
    [W25Q64_SLOT_STM32_1_B] = {.address = 0x060000, .size = 0x040000},
    [W25Q64_SLOT_STM32_2_A] = {.address = 0x0A0000, .size = 0x040000},
    [W25Q64_SLOT_STM32_2_B] = {.address = 0x0E0000, .size = 0x040000},
    [W25Q64_SLOT_STM32_3_A] = {.address = 0x120000, .size = 0x040000},
    [W25Q64_SLOT_STM32_3_B] = {.address = 0x160000, .size = 0x040000},
    [W25Q64_SLOT_STM32_4_A] = {.address = 0x1A0000, .size = 0x040000},
    [W25Q64_SLOT_STM32_4_B] = {.address = 0x1E0000, .size = 0x040000},
    [W25Q64_SLOT_ESP32S3] = {.address = 0x220000, .size = 0x200000},
    [W25Q64_SLOT_COMMON] = {.address = 0x420000, .size = 0x3E0000},
};

static spi_device_handle_t s_w25q64;
static SemaphoreHandle_t s_w25q64_mutex;

/* 槽位文件名/活动半区变更计数（仅 RAM）。 */
static volatile uint32_t s_name_revision;

#define W25Q64_IMAGE_INFO_MAGIC 0x57494D49U
#define W25Q64_USAGE_INFO_MAGIC 0x57555348U
#define W25Q64_ACTIVE_INFO_MAGIC 0x57414348U

/* 记录在 META 中的镜像数量：8 个 STM32 半区 + ESP32S3。 */
#define W25Q64_RECORD_COUNT 9U
/* 活动半区记录单独占一个扇区，位于用量记录之后。 */
#define W25Q64_ACTIVE_SECTOR (2U * W25Q64_RECORD_COUNT)

typedef struct {
  uint32_t magic;
  uint32_t slot;
  W25Q64_ImageInfo_t image;
} w25q64_image_record_t;

typedef struct {
  uint32_t magic;
  uint32_t slot;
  uint32_t used_size;
} w25q64_usage_record_t;

/* 各逻辑槽各自的活动半区（存物理槽号）。 */
typedef struct {
  uint32_t magic;
  uint32_t active[W25Q64_FW_COUNT];
} w25q64_active_record_t;

/* 返回槽位在 META 中的紧凑记录索引；META/COMMON 无记录返回 -1。 */
static int w25q64_record_index(W25Q64_Slot_t slot) {
  switch (slot) {
    case W25Q64_SLOT_STM32_1_A:
      return 0;
    case W25Q64_SLOT_STM32_1_B:
      return 1;
    case W25Q64_SLOT_STM32_2_A:
      return 2;
    case W25Q64_SLOT_STM32_2_B:
      return 3;
    case W25Q64_SLOT_STM32_3_A:
      return 4;
    case W25Q64_SLOT_STM32_3_B:
      return 5;
    case W25Q64_SLOT_STM32_4_A:
      return 6;
    case W25Q64_SLOT_STM32_4_B:
      return 7;
    case W25Q64_SLOT_ESP32S3:
      return 8;
    default:
      return -1;
  }
}

/* 逻辑槽 + 半区（0=A, 1=B）映射为物理槽。 */
static W25Q64_Slot_t w25q64_fw_slot(W25Q64_FwSlot_t fw, uint32_t half) {
  return (W25Q64_Slot_t)((uint32_t)W25Q64_SLOT_STM32_1_A + (uint32_t)fw * 2U +
                         (half != 0U ? 1U : 0U));
}

static bool w25q64_range_valid(uint32_t address, size_t length) {
  return address < W25Q64_TOTAL_SIZE && length <= W25Q64_TOTAL_SIZE - address;
}

static esp_err_t w25q64_lock(void) {
  if (s_w25q64 == NULL || s_w25q64_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  return xSemaphoreTake(s_w25q64_mutex, portMAX_DELAY) == pdPASS
             ? ESP_OK
             : ESP_ERR_TIMEOUT;
}

static void w25q64_unlock(void) {
  xSemaphoreGive(s_w25q64_mutex);
}

static esp_err_t w25q64_transmit(const uint8_t* tx_data, size_t length) {
  spi_transaction_t transaction = {
      .length = length * 8,
      .tx_buffer = tx_data,
  };
  return spi_device_transmit(s_w25q64, &transaction);
}

static esp_err_t w25q64_write_enable(void) {
  const uint8_t command = W25Q64_CMD_WRITE_ENABLE;
  return w25q64_transmit(&command, sizeof(command));
}

static esp_err_t w25q64_read_status(uint8_t* status) {
  if (status == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  const uint8_t tx_data[2] = {W25Q64_CMD_READ_STATUS1, 0xFF};
  uint8_t rx_data[2] = {0};
  spi_transaction_t transaction = {
      .length = sizeof(tx_data) * 8,
      .tx_buffer = tx_data,
      .rx_buffer = rx_data,
  };
  esp_err_t err = spi_device_transmit(s_w25q64, &transaction);
  if (err == ESP_OK) {
    *status = rx_data[1];
  }
  return err;
}

static esp_err_t w25q64_wait_ready(uint32_t timeout_ms) {
  const TickType_t start = xTaskGetTickCount();
  const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
  while ((xTaskGetTickCount() - start) < timeout_ticks) {
    uint8_t status = 0;
    esp_err_t err = w25q64_read_status(&status);
    if (err != ESP_OK) {
      return err;
    }
    if ((status & 0x01U) == 0U) {
      return ESP_OK;
    }
    vTaskDelay(1);
  }
  return ESP_ERR_TIMEOUT;
}

static esp_err_t w25q64_write_page(uint32_t address,
                                   const uint8_t* data,
                                   size_t length) {
  uint8_t tx_data[4 + W25Q64_PAGE_SIZE];
  esp_err_t err = w25q64_write_enable();
  if (err != ESP_OK) {
    return err;
  }

  tx_data[0] = W25Q64_CMD_PAGE_PROGRAM;
  tx_data[1] = (uint8_t)(address >> 16);
  tx_data[2] = (uint8_t)(address >> 8);
  tx_data[3] = (uint8_t)address;
  memcpy(&tx_data[4], data, length);
  err = w25q64_transmit(tx_data, length + 4U);
  return err == ESP_OK ? w25q64_wait_ready(W25Q64_READY_TIMEOUT_MS) : err;
}

esp_err_t W25Q64_Init(void) {
  /* 已初始化时直接返回，避免重复申请 SPI3 总线。 */
  if (s_w25q64 != NULL) {
    return ESP_OK;
  }

  s_w25q64_mutex = xSemaphoreCreateMutex();
  if (s_w25q64_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  /* 配置 W25Q64 的四线 SPI 总线。 */
  const spi_bus_config_t bus_config = {
      .mosi_io_num = W25Q64_PIN_MOSI,
      .miso_io_num = W25Q64_PIN_MISO,
      .sclk_io_num = W25Q64_PIN_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };
  esp_err_t err =
      spi_bus_initialize(W25Q64_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    vSemaphoreDelete(s_w25q64_mutex);
    s_w25q64_mutex = NULL;
    return err;
  }

  /* W25Q64 使用 SPI 模式 0，CS 由 SPI 驱动自动控制。 */
  const spi_device_interface_config_t device_config = {
      .clock_speed_hz = W25Q64_SPI_CLOCK_HZ,
      .mode = 0,
      .spics_io_num = W25Q64_PIN_CS,
      .queue_size = 1,
  };
  err = spi_bus_add_device(W25Q64_SPI_HOST, &device_config, &s_w25q64);
  if (err != ESP_OK) {
    spi_bus_free(W25Q64_SPI_HOST);
    vSemaphoreDelete(s_w25q64_mutex);
    s_w25q64_mutex = NULL;
  }
  return err;
}

esp_err_t W25Q64_ReadJedecId(uint32_t* jedec_id) {
  if (jedec_id == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t err = w25q64_lock();
  if (err != ESP_OK) {
    return err;
  }

  /* 全双工发送 9F FF FF FF，回读第 2~4 字节为 JEDEC ID。 */
  const uint8_t tx_data[4] = {W25Q64_CMD_JEDEC_ID, 0xFF, 0xFF, 0xFF};
  uint8_t rx_data[4] = {0};
  spi_transaction_t transaction = {
      .length = sizeof(tx_data) * 8,
      .tx_buffer = tx_data,
      .rx_buffer = rx_data,
  };
  err = spi_device_transmit(s_w25q64, &transaction);
  if (err == ESP_OK) {
    /* W25Q64 正常值为 0xEF4017。 */
    *jedec_id =
        ((uint32_t)rx_data[1] << 16) | ((uint32_t)rx_data[2] << 8) | rx_data[3];
  }
  w25q64_unlock();
  return err;
}

esp_err_t W25Q64_Read(uint32_t address, uint8_t* data, size_t length) {
  if ((data == NULL && length != 0U) || !w25q64_range_valid(address, length)) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = w25q64_lock();
  if (err != ESP_OK) {
    return err;
  }
  while (length != 0U) {
    const size_t chunk =
        length > W25Q64_TRANSFER_SIZE ? W25Q64_TRANSFER_SIZE : length;
    uint8_t tx_data[4 + W25Q64_TRANSFER_SIZE] = {0};
    uint8_t rx_data[4 + W25Q64_TRANSFER_SIZE] = {0};
    tx_data[0] = W25Q64_CMD_READ_DATA;
    tx_data[1] = (uint8_t)(address >> 16);
    tx_data[2] = (uint8_t)(address >> 8);
    tx_data[3] = (uint8_t)address;

    spi_transaction_t transaction = {
        .length = (chunk + 4U) * 8,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    err = spi_device_transmit(s_w25q64, &transaction);
    if (err != ESP_OK) {
      break;
    }
    memcpy(data, &rx_data[4], chunk);
    address += chunk;
    data += chunk;
    length -= chunk;
  }
  w25q64_unlock();
  return err;
}

esp_err_t W25Q64_Write(uint32_t address, const uint8_t* data, size_t length) {
  if ((data == NULL && length != 0U) || !w25q64_range_valid(address, length)) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = w25q64_lock();
  if (err != ESP_OK) {
    return err;
  }
  while (length != 0U) {
    const size_t page_remaining =
        W25Q64_PAGE_SIZE - (address % W25Q64_PAGE_SIZE);
    const size_t chunk = length > page_remaining ? page_remaining : length;
    err = w25q64_write_page(address, data, chunk);
    if (err != ESP_OK) {
      break;
    }
    address += chunk;
    data += chunk;
    length -= chunk;
  }
  w25q64_unlock();
  return err;
}

esp_err_t W25Q64_EraseSector(uint32_t address) {
  if (address % W25Q64_SECTOR_SIZE != 0U ||
      !w25q64_range_valid(address, W25Q64_SECTOR_SIZE)) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = w25q64_lock();
  if (err != ESP_OK) {
    return err;
  }
  err = w25q64_write_enable();
  if (err != ESP_OK) {
    w25q64_unlock();
    return err;
  }
  const uint8_t command[4] = {
      W25Q64_CMD_SECTOR_ERASE,
      (uint8_t)(address >> 16),
      (uint8_t)(address >> 8),
      (uint8_t)address,
  };
  err = w25q64_transmit(command, sizeof(command));
  if (err == ESP_OK) {
    err = w25q64_wait_ready(W25Q64_READY_TIMEOUT_MS);
  }
  w25q64_unlock();
  return err;
}

esp_err_t W25Q64_GetSlotInfo(W25Q64_Slot_t slot, W25Q64_SlotInfo_t* info) {
  if (slot >= W25Q64_SLOT_COUNT || info == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  *info = s_slots[slot];
  return ESP_OK;
}

static esp_err_t w25q64_slot_address(W25Q64_Slot_t slot,
                                     uint32_t offset,
                                     size_t length,
                                     uint32_t* address) {
  if (slot >= W25Q64_SLOT_COUNT || address == NULL ||
      offset > s_slots[slot].size || length > s_slots[slot].size - offset) {
    return ESP_ERR_INVALID_ARG;
  }
  *address = s_slots[slot].address + offset;
  return ESP_OK;
}

esp_err_t W25Q64_ReadSlot(W25Q64_Slot_t slot,
                          uint32_t offset,
                          uint8_t* data,
                          size_t length) {
  uint32_t address = 0;
  esp_err_t err = w25q64_slot_address(slot, offset, length, &address);
  return err == ESP_OK ? W25Q64_Read(address, data, length) : err;
}

esp_err_t W25Q64_WriteSlot(W25Q64_Slot_t slot,
                           uint32_t offset,
                           const uint8_t* data,
                           size_t length) {
  uint32_t address = 0;
  esp_err_t err = w25q64_slot_address(slot, offset, length, &address);
  return err == ESP_OK ? W25Q64_Write(address, data, length) : err;
}

esp_err_t W25Q64_EraseSlot(W25Q64_Slot_t slot) {
  W25Q64_SlotInfo_t info;
  esp_err_t err = W25Q64_GetSlotInfo(slot, &info);
  if (err != ESP_OK) {
    return err;
  }
  if (slot != W25Q64_SLOT_META && w25q64_record_index(slot) >= 0) {
    err = W25Q64_ClearUsetageInfo(slot);
    if (err != ESP_OK) {
      return err;
    }
  }

  for (uint32_t offset = 0; offset < info.size; offset += W25Q64_SECTOR_SIZE) {
    err = W25Q64_EraseSector(info.address + offset);
    if (err != ESP_OK) {
      return err;
    }
  }
  return ESP_OK;
}

static esp_err_t w25q64_image_record_address(W25Q64_Slot_t slot,
                                             uint32_t* address) {
  if (address == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const int index = w25q64_record_index(slot);
  if (index < 0) {
    return ESP_ERR_INVALID_ARG;
  }

  *address = s_slots[W25Q64_SLOT_META].address +
             (uint32_t)index * W25Q64_SECTOR_SIZE;
  return ESP_OK;
}

esp_err_t W25Q64_SaveImageInfo(W25Q64_Slot_t slot,
                               const W25Q64_ImageInfo_t* info) {
  if (info == NULL || info->size == 0U || slot >= W25Q64_SLOT_COUNT ||
      w25q64_record_index(slot) < 0 || info->size > s_slots[slot].size) {
    return ESP_ERR_INVALID_ARG;
  }

  uint32_t address = 0;
  esp_err_t err = w25q64_image_record_address(slot, &address);
  if (err != ESP_OK) {
    return err;
  }

  const w25q64_image_record_t record = {
      .magic = W25Q64_IMAGE_INFO_MAGIC,
      .slot = (uint32_t)slot,
      .image = *info,
  };
  err = W25Q64_EraseSector(address);
  if (err != ESP_OK) {
    return err;
  }
  err = W25Q64_Write(address, (const uint8_t*)&record, sizeof(record));
  if (err == ESP_OK) {
    ++s_name_revision;
  }
  return err;
}

esp_err_t W25Q64_LoadImageInfo(W25Q64_Slot_t slot, W25Q64_ImageInfo_t* info) {
  if (info == NULL || slot >= W25Q64_SLOT_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }

  uint32_t address = 0;
  esp_err_t err = w25q64_image_record_address(slot, &address);
  if (err != ESP_OK) {
    return err;
  }

  w25q64_image_record_t record = {0};
  err = W25Q64_Read(address, (uint8_t*)&record, sizeof(record));
  if (err != ESP_OK) {
    return err;
  }
  if (record.magic != W25Q64_IMAGE_INFO_MAGIC ||
      record.slot != (uint32_t)slot || record.image.size == 0U ||
      record.image.size > s_slots[slot].size) {
    return ESP_ERR_NOT_FOUND;
  }
  *info = record.image;
  return ESP_OK;
}

esp_err_t W25Q64_ClearImageInfo(W25Q64_Slot_t slot) {
  uint32_t address = 0;
  esp_err_t err = w25q64_image_record_address(slot, &address);
  return err == ESP_OK ? W25Q64_EraseSector(address) : err;
}

static esp_err_t w25q64_usage_record_address(W25Q64_Slot_t slot,
                                              uint32_t* address) {
  if (address == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  const int index = w25q64_record_index(slot);
  if (index < 0) {
    return ESP_ERR_INVALID_ARG;
  }

  const uint32_t offset =
      (W25Q64_RECORD_COUNT + (uint32_t)index) * W25Q64_SECTOR_SIZE;
  *address = s_slots[W25Q64_SLOT_META].address + offset;
  return ESP_OK;
}

esp_err_t W25Q64_SaveUsetageInfo(W25Q64_Slot_t slot, uint32_t used_size) {
  if (w25q64_record_index(slot) < 0 || used_size > s_slots[slot].size) {
    return ESP_ERR_INVALID_ARG;
  }

  uint32_t address = 0;
  esp_err_t err = w25q64_usage_record_address(slot, &address);
  if (err != ESP_OK) {
    return err;
  }

  const w25q64_usage_record_t record = {
      .magic = W25Q64_USAGE_INFO_MAGIC,
      .slot = (uint32_t)slot,
      .used_size = used_size,
  };
  err = W25Q64_EraseSector(address);
  if (err != ESP_OK) {
    return err;
  }
  return W25Q64_Write(address, (const uint8_t*)&record, sizeof(record));
}

esp_err_t W25Q64_GetUsetageInfo(W25Q64_Slot_t slot, uint32_t* used_size) {
  if (used_size == NULL || w25q64_record_index(slot) < 0) {
    return ESP_ERR_INVALID_ARG;
  }

  uint32_t address = 0;
  esp_err_t err = w25q64_usage_record_address(slot, &address);
  if (err != ESP_OK) {
    return err;
  }

  w25q64_usage_record_t record = {0};
  err = W25Q64_Read(address, (uint8_t*)&record, sizeof(record));
  if (err != ESP_OK) {
    return err;
  }
  if (record.magic != W25Q64_USAGE_INFO_MAGIC ||
      record.slot != (uint32_t)slot ||
      record.used_size > s_slots[slot].size) {
    return ESP_ERR_NOT_FOUND;
  }
  *used_size = record.used_size;
  return ESP_OK;
}

esp_err_t W25Q64_ClearUsetageInfo(W25Q64_Slot_t slot) {
  uint32_t address = 0;
  esp_err_t err = w25q64_usage_record_address(slot, &address);
  return err == ESP_OK ? W25Q64_EraseSector(address) : err;
}

static esp_err_t w25q64_active_record_address(uint32_t* address) {
  if (address == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  *address = s_slots[W25Q64_SLOT_META].address +
             W25Q64_ACTIVE_SECTOR * W25Q64_SECTOR_SIZE;
  return ESP_OK;
}

/* 无有效活动记录时，各逻辑槽默认都指向 A 半区。 */
static void w25q64_active_defaults(w25q64_active_record_t* record) {
  record->magic = W25Q64_ACTIVE_INFO_MAGIC;
  for (uint32_t fw = 0; fw < W25Q64_FW_COUNT; ++fw) {
    record->active[fw] = (uint32_t)w25q64_fw_slot((W25Q64_FwSlot_t)fw, 0U);
  }
}

static esp_err_t w25q64_active_load(w25q64_active_record_t* record) {
  uint32_t address = 0;
  esp_err_t err = w25q64_active_record_address(&address);
  if (err != ESP_OK) {
    return err;
  }
  err = W25Q64_Read(address, (uint8_t*)record, sizeof(*record));
  if (err != ESP_OK) {
    return err;
  }
  if (record->magic != W25Q64_ACTIVE_INFO_MAGIC) {
    w25q64_active_defaults(record);
  }
  return ESP_OK;
}

esp_err_t W25Q64_GetActiveSlot(W25Q64_FwSlot_t fw, W25Q64_Slot_t* slot) {
  if (slot == NULL || fw >= W25Q64_FW_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }

  w25q64_active_record_t record;
  esp_err_t err = w25q64_active_load(&record);
  if (err != ESP_OK) {
    return err;
  }

  /* 记录值非法时回退到 A 半区。 */
  W25Q64_Slot_t value = (W25Q64_Slot_t)record.active[fw];
  if (value != w25q64_fw_slot(fw, 0U) && value != w25q64_fw_slot(fw, 1U)) {
    value = w25q64_fw_slot(fw, 0U);
  }
  *slot = value;
  return ESP_OK;
}

W25Q64_Slot_t W25Q64_GetDownloadSlot(W25Q64_FwSlot_t fw) {
  const W25Q64_Slot_t fallback = w25q64_fw_slot(fw, 0U);
  if (fw >= W25Q64_FW_COUNT) {
    return fallback;
  }

  W25Q64_Slot_t active = fallback;
  if (W25Q64_GetActiveSlot(fw, &active) != ESP_OK) {
    return fallback;
  }

  W25Q64_ImageInfo_t info;
  if (W25Q64_LoadImageInfo(active, &info) != ESP_OK) {
    /* 活动半区没有有效镜像，直接复用它。 */
    return active;
  }
  return active == w25q64_fw_slot(fw, 0U) ? w25q64_fw_slot(fw, 1U)
                                          : w25q64_fw_slot(fw, 0U);
}

esp_err_t W25Q64_SetActiveSlot(W25Q64_FwSlot_t fw, W25Q64_Slot_t slot) {
  if (fw >= W25Q64_FW_COUNT ||
      (slot != w25q64_fw_slot(fw, 0U) && slot != w25q64_fw_slot(fw, 1U))) {
    return ESP_ERR_INVALID_ARG;
  }

  w25q64_active_record_t record;
  esp_err_t err = w25q64_active_load(&record);
  if (err != ESP_OK) {
    return err;
  }
  if (record.active[fw] == (uint32_t)slot) {
    return ESP_OK;
  }
  record.active[fw] = (uint32_t)slot;
  record.magic = W25Q64_ACTIVE_INFO_MAGIC;

  uint32_t address = 0;
  err = w25q64_active_record_address(&address);
  if (err != ESP_OK) {
    return err;
  }
  err = W25Q64_EraseSector(address);
  if (err != ESP_OK) {
    return err;
  }
  err = W25Q64_Write(address, (const uint8_t*)&record, sizeof(record));
  if (err == ESP_OK) {
    ++s_name_revision;
  }
  return err;
}

uint32_t W25Q64_GetNameRevision(void) { return s_name_revision; }

W25Q64_Slot_t W25Q64_FwSlotToSlot(W25Q64_FwSlot_t fw, uint8_t half) {
  if (fw >= W25Q64_FW_COUNT) {
    return W25Q64_SLOT_STM32_1_A;
  }
  return w25q64_fw_slot(fw, half != 0U ? 1U : 0U);
}
