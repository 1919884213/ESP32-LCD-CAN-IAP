#include <stdbool.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "http.h"

static const char* TAG = "HTTPBIN";

#define HTTP_CRC32_READ_SIZE 256U

/* CRC-32/IEEE: init=0xFFFFFFFF, poly=0xEDB88320, xorout=0xFFFFFFFF.
 * 与 tools/calc_bin_crc.py 的 zlib.crc32() 输出一致。 */
static uint32_t crc32_ieee_update(uint32_t crc,
                                  const uint8_t* data,
                                  size_t length) {
  while (length-- != 0U) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
    }
  }
  return crc;
}

/* 单次下载上下文：记录写入槽位、进度与错误状态。 */
typedef struct {
  W25Q64_Slot_t slot;
  uint32_t offset;
  size_t slot_size;
  bool failed;
  esp_err_t err;
  http_download_progress_cb_t on_progress;
  void* arg;
} http_download_ctx_t;

/* 下载事件回调：在 ON_DATA 中边收边写入 W25Q64 目标槽位。 */
static esp_err_t download_event_handler(esp_http_client_event_t* evt) {
  http_download_ctx_t* ctx = (http_download_ctx_t*)evt->user_data;
  if (ctx == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
      /* 检查剩余容量，避免写入超出槽位边界。 */
      if ((size_t)ctx->offset + evt->data_len > ctx->slot_size) {
        ESP_LOGE(TAG, "data exceeds slot capacity");
        ctx->failed = true;
        ctx->err = ESP_ERR_INVALID_SIZE;
        return ctx->err;
      }
      /* evt->data 仅在回调内有效，必须当下写入 Flash。 */
      ctx->err =
          W25Q64_WriteSlot(ctx->slot, ctx->offset, evt->data, evt->data_len);
      if (ctx->err != ESP_OK) {
        ESP_LOGE(TAG, "W25Q64 write failed at offset %lu: %s",
                 (unsigned long)ctx->offset, esp_err_to_name(ctx->err));
        ctx->failed = true;
        return ctx->err;
      }
      ctx->offset += evt->data_len;
      if (ctx->on_progress != NULL) {
        ctx->on_progress(ctx->offset, (uint32_t)ctx->slot_size, ctx->arg);
      }
      break;
    default:
      /* 建连/头/断开等事件由 esp_http_client 自行处理。 */
      break;
  }
  return ESP_OK;
}

/* 下载 url 指向的 bin 固件并写入 W25Q64 指定槽位。
 * 阻塞直到下载完成或失败，应在独立任务中调用，勿在 LVGL 线程执行。 */
esp_err_t http_downloadBin(const char* url,
                           W25Q64_Slot_t slot,
                           http_download_progress_cb_t on_progress,
                           void* arg,
                           uint32_t* downloaded_size) {
  if (downloaded_size != NULL) {
    *downloaded_size = 0;
  }

  W25Q64_SlotInfo_t slot_info;
  esp_err_t err = W25Q64_GetSlotInfo(slot, &slot_info);
  if (err != ESP_OK) {
    return err;
  }

  /* 新下载开始前使旧记录失效，避免下载失败时 IAP 误用被覆盖的数据。 */
  err = W25Q64_ClearImageInfo(slot);
  if (err != ESP_OK) {
    return err;
  }

  /* Flash 必须先擦后写，整槽擦除。 */
  err = W25Q64_EraseSlot(slot);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "erase slot %d failed: %s", (int)slot, esp_err_to_name(err));
    return err;
  }

  http_download_ctx_t ctx = {
      .slot = slot,
      .offset = 0,
      .slot_size = slot_info.size,
      .failed = false,
      .err = ESP_OK,
      .on_progress = on_progress,
      .arg = arg,
  };

  esp_http_client_config_t config = {
      .url = url,
      .event_handler = download_event_handler,
      .user_data = &ctx,
      .timeout_ms = HTTP_REQUEST_TIMEOUT_MS,
      /* 校验 HTTPS 服务器证书；并允许 CDN/OSS 的 302 跳转。 */
      .crt_bundle_attach = esp_crt_bundle_attach,
      .max_redirection_count = 5,
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    return ESP_ERR_NO_MEM;
  }

  esp_http_client_set_method(client, HTTP_METHOD_GET);
  err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "download failed: %s", esp_err_to_name(err));
  } else if (ctx.failed) {
    err = ctx.err;
  } else {
    const int status = esp_http_client_get_status_code(client);
    if (status != 200) {
      ESP_LOGE(TAG, "unexpected HTTP status: %d", status);
      err = ESP_FAIL;
    } else if (ctx.offset == 0U) {
      ESP_LOGE(TAG, "empty response");
      err = ESP_FAIL;
    } else {
      ESP_LOGI(TAG, "downloaded %lu bytes to slot %d",
               (unsigned long)ctx.offset, (int)slot);
      err = W25Q64_SaveUsetageInfo(slot, ctx.offset);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "save slot usage failed: %s", esp_err_to_name(err));
      }
      if (downloaded_size != NULL) {
        *downloaded_size = ctx.offset;
      }
    }
  }

  esp_http_client_cleanup(client);
  return err;
}

esp_err_t http_calculate_bin_crc32(W25Q64_Slot_t slot,
                                   uint32_t length,
                                   uint32_t* crc32) {
  if (crc32 == NULL || length == 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  W25Q64_SlotInfo_t slot_info;
  esp_err_t err = W25Q64_GetSlotInfo(slot, &slot_info);
  if (err != ESP_OK || length > slot_info.size) {
    return err == ESP_OK ? ESP_ERR_INVALID_SIZE : err;
  }

  uint8_t buffer[HTTP_CRC32_READ_SIZE];
  uint32_t offset = 0;
  uint32_t value = 0xFFFFFFFFU;
  while (offset < length) {
    const size_t chunk = (length - offset) > sizeof(buffer)
                             ? sizeof(buffer)
                             : (size_t)(length - offset);
    err = W25Q64_ReadSlot(slot, offset, buffer, chunk);
    if (err != ESP_OK) {
      return err;
    }
    value = crc32_ieee_update(value, buffer, chunk);
    offset += (uint32_t)chunk;
  }

  *crc32 = value ^ 0xFFFFFFFFU;
  return ESP_OK;
}
