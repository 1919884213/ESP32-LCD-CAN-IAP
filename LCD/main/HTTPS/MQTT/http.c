#include "http.h"
#include <stdbool.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_log.h"


/* HTTP 分片响应在回调中依次拼接到该缓冲区。 */
static char resp_buf[HTTP_RESP_BUF_MAX];
/* 已写入 resp_buf 的有效字节数。 */
static size_t resp_len;
/* 响应超过缓冲区容量时置位，禁止解析截断后的 JSON。 */
static bool resp_overflow;
/* 保存最近一次查询的结果，供 LVGL 页面读取。 */
static http_ota_info_t ota_info;

esp_err_t http_handler(esp_http_client_event_t* evt) {
  switch (evt->event_id) {
    /* 每次收到服务器分片数据时触发，将数据追加到响应缓冲区。 */
    case HTTP_EVENT_ON_DATA:
      /*检查缓冲区大小够不够*/
      if ((size_t)evt->data_len <= sizeof(resp_buf) - resp_len - 1U) {
        /**/
        memcpy(resp_buf + resp_len, evt->data, evt->data_len);
        resp_len += evt->data_len;
      } else {
        /* 缓冲区剩余空间不足，标记溢出以便后续丢弃本次响应。 */
        resp_overflow = true;
      }
      break;
    /* 请求完成（连接关闭）时触发，为缓冲区补上字符串结束符。 */
    case HTTP_EVENT_ON_FINISH:
      resp_buf[resp_len] = '\0';
      /* 仅当响应完整未溢出时才打印，避免输出被截断的乱码 JSON。 */
      if (!resp_overflow) {
        ESP_LOGI("HTTP", "Response: %s", resp_buf);
      }
      break;
    default:
      /* 其它事件（连接、头、错误等）在本模块中不处理。 */
      break;
  }
  return ESP_OK;
}

/* 将最近一次缓存的 OTA 查询结果拷贝给调用方。 */
void http_get_ota_info(http_ota_info_t* info) {
  if (info != NULL) {
    *info = ota_info;
  }
}

/* 对 url 发起一次 GET 请求，并保存解析成功的 OTA 信息。 */
esp_err_t http_get(const char* url) {
  /* 每次请求前清空上一次的结果，防止残留数据被误读。 */
  memset(&ota_info, 0, sizeof(ota_info));
  ota_info.last_err = ESP_FAIL;

  esp_http_client_config_t client_config = {
      .url = url,
      .event_handler = http_handler,
      /* WiFi 下的 DNS、TCP 和 TLS 建连通常远超过 200 ms。 */
      .timeout_ms = HTTP_REQUEST_TIMEOUT_MS,
      /* 使用 ESP-IDF 内置 CA 证书包验证 bemfa 的 HTTPS 服务器证书。 */
      .crt_bundle_attach = esp_crt_bundle_attach,
  };
  /* 根据配置创建 HTTP 客户端句柄。 */
  esp_http_client_handle_t client = esp_http_client_init(&client_config);

  if (client == NULL) {
    /* 创建失败通常意味着内存不足。 */
    ESP_LOGE("HTTP", "Failed to initialize HTTP client");
    ota_info.last_err = ESP_ERR_NO_MEM;
    return ota_info.last_err;
  }
  /* 每次请求独立接收响应，不能复用上一次的长度和内容。 */
  resp_len = 0U;
  resp_overflow = false;
  memset(resp_buf, 0, sizeof(resp_buf));

  /* 指定请求方法为 GET 并同步执行，阻塞直到完成或超时。 */
  esp_http_client_set_method(client, HTTP_METHOD_GET);
  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    /* 网络错误、超时或 TLS 校验失败都会走到这里。 */
    ESP_LOGE("HTTP", "GET failed: %s", esp_err_to_name(err));
    ota_info.last_err = err;
    goto cleanup;
  }

  /* 截断的 JSON 不能保证格式完整，因此不继续解析。 */
  if (resp_overflow) {
    ESP_LOGE("HTTP", "Response exceeds %u bytes", HTTP_RESP_BUF_MAX - 1U);
    ota_info.last_err = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  /* 校验 HTTP 状态码，非 200 表示服务器返回错误。 */
  ota_info.http_status = esp_http_client_get_status_code(client);
  if (ota_info.http_status != 200) {
    ESP_LOGE("HTTP", "Unexpected HTTP status: %d",
             ota_info.http_status);
    ota_info.last_err = ESP_FAIL;
    goto cleanup;
  }

  /* 将服务器响应字符串转换为 cJSON 对象。 */
  cJSON* json = cJSON_Parse(resp_buf);
  if (json == NULL) {
    /* 响应不是合法的 JSON 文本时解析失败。 */
    ESP_LOGE("HTTP", "Failed to parse JSON response");
    ota_info.last_err = ESP_FAIL;
    goto cleanup;
  }

  /* 接口格式：根节点包含 msg，OTA 信息位于 data 子对象。 */
  cJSON* msg = cJSON_GetObjectItemCaseSensitive(json, "msg");
  cJSON* data = cJSON_GetObjectItemCaseSensitive(json, "data");

  if (!cJSON_IsString(msg) || !cJSON_IsObject(data)) {
    /* msg 或 data 的类型与预期不符，说明接口结构已变化。 */
    ESP_LOGE("HTTP", "Invalid OTA JSON response format");
    ota_info.last_err = ESP_FAIL;
    cJSON_Delete(json);
    goto cleanup;
  }

  /* 从 data 中取得 OTA 下载地址、版本号和固件大小。 */
  cJSON* ota_url = cJSON_GetObjectItemCaseSensitive(data, "url");
  cJSON* version = cJSON_GetObjectItemCaseSensitive(data, "version");
  cJSON* tag = cJSON_GetObjectItemCaseSensitive(data, "tag");
  cJSON* size = cJSON_GetObjectItemCaseSensitive(data, "size");
  cJSON* unix_timestamp = cJSON_GetObjectItemCaseSensitive(data, "unix");

  /* 逐个校验字段的类型：url/tag 为字符串，其余为数值。 */
  if (!cJSON_IsString(ota_url) || !cJSON_IsNumber(version) ||
      !cJSON_IsString(tag) || !cJSON_IsNumber(size) ||
      !cJSON_IsNumber(unix_timestamp)) {
    ESP_LOGE("HTTP", "Invalid OTA data format");
    ota_info.last_err = ESP_FAIL;
    cJSON_Delete(json);
    goto cleanup;
  }

  ESP_LOGI("HTTP", "msg=%s", msg->valuestring);
  ESP_LOGI("HTTP", "url=%s", ota_url->valuestring);
  ESP_LOGI("HTTP", "version=%d, tag=%s, size=%d, unix=%d",
           version->valueint, tag->valuestring, size->valueint,
           unix_timestamp->valueint);

  /* cJSON 释放前复制字符串，避免页面保存已失效的 valuestring 指针。 */
  strlcpy(ota_info.message, msg->valuestring, sizeof(ota_info.message));
  strlcpy(ota_info.tag, tag->valuestring, sizeof(ota_info.tag));
  strlcpy(ota_info.url, ota_url->valuestring, sizeof(ota_info.url));
  ota_info.version = version->valueint;
  ota_info.size = size->valueint;
  ota_info.unix_timestamp = unix_timestamp->valueint;
  /* 全部字段解析成功，标记本次查询结果有效。 */
  ota_info.valid = true;
  ota_info.last_err = ESP_OK;

  cJSON_Delete(json);

cleanup:
  /* 释放 HTTP 客户端资源（若已创建成功）。 */
  esp_http_client_cleanup(client);
  return ota_info.last_err;
}
