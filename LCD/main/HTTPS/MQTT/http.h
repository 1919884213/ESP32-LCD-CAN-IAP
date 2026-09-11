#include <stdbool.h>
#include <stdint.h>
#include "W25Q64.h"
#include "cJSON.h"
#include "esp_http_client.h"

#define CanDevice1                                                            \
  "https://apis.bemfa.com/vb/api/v1/"                                         \
  "firmwareVersion?openID=4f56a3cfdba743a3a72f71450556145d&topic=CanDevice1&" \
  "deviceType=1"
#define CanDevice2                                                            \
  "https://apis.bemfa.com/vb/api/v1/"                                         \
  "firmwareVersion?openID=4f56a3cfdba743a3a72f71450556145d&topic=CanDevice2&" \
  "deviceType=1"

#define myCanDevice1                                                            \
  "https://apis.bemfa.com/vb/api/v1/"                                         \
  "firmwareVersion?openID=4f56a3cfdba743a3a72f71450556145d&topic=myCanDevice1&" \
  "deviceType=1"

#define myCanDevice2                                                            \
  "https://apis.bemfa.com/vb/api/v1/"                                         \
  "firmwareVersion?openID=4f56a3cfdba743a3a72f71450556145d&topic=myCanDevice2&" \
  "deviceType=1"

/* 单次 OTA 查询结果，字符串会在下一次 http_get() 前保持有效。 */
#define HTTP_OTA_MESSAGE_MAX 32U
#define HTTP_OTA_TAG_MAX 64U
#define HTTP_OTA_URL_MAX 512U

/* 响应缓冲区最大容量（字节）。 */
#define HTTP_RESP_BUF_MAX 4096
/* 单次 HTTP 请求的超时时间（毫秒），覆盖 DNS/TCP/TLS 建连及收发。 */
#define HTTP_REQUEST_TIMEOUT_MS 10000

typedef struct {
  char message[HTTP_OTA_MESSAGE_MAX];
  char tag[HTTP_OTA_TAG_MAX];
  char url[HTTP_OTA_URL_MAX]; 
  int version;
  int size;
  int unix_timestamp;
  int http_status;
  esp_err_t last_err;
  bool valid;
} http_ota_info_t;

/* 请求 OTA 信息并缓存成功解析的服务器响应。 */
esp_err_t http_get(const char* url);
/* 读取最近一次请求的 OTA 查询结果。 */
void http_get_ota_info(http_ota_info_t* info);

/* 下载进度回调：written 为已写入字节数，total 为槽位容量。 */
typedef void (*http_download_progress_cb_t)(uint32_t written,
                                            uint32_t total,
                                            void* arg);
/* 下载 url 指向的 bin 固件并写入 W25Q64 指定槽位（阻塞，需在独立任务中调用）。
 * on_progress 可为 NULL，arg 透传给回调；downloaded_size 返回实际写入字节数。
 */
esp_err_t http_downloadBin(const char* url,
                           W25Q64_Slot_t slot,
                           http_download_progress_cb_t on_progress,
                           void* arg,
                           uint32_t* downloaded_size);

/* 读取槽位中指定长度的数据，按 tools/calc_bin_crc.py 的 CRC-32/IEEE 规则计算。
 */
esp_err_t http_calculate_bin_crc32(W25Q64_Slot_t slot,
                                   uint32_t length,
                                   uint32_t* crc32);
