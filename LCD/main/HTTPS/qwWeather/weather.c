/**
 * @file weather.c
 * @brief 和风天气（QWeather）当前天气数据获取模块。
 *
 * 模块职责：
 *   1. 通过 HTTPS 请求和风天气 API（GET），携带 X-QW-Api-Key 鉴权；
 *   2. 接收响应字节流（服务端可能返回 gzip 压缩），用 zlib 解压；
 *   3. 用 cJSON 解析天气 JSON，抽出天气状况文字、体感温度、温度、湿度；
 *   4. 将解析结果写入 weather_t，供 UI 天气页面展示。
 *
 * 说明：HTTP 请求为同步阻塞执行（esp_http_client_perform），
 * 调用方应放在独立任务中（UI 层用 PSRAM 栈任务调用本模块）。
 */

#include "weather.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "zlib.h"

#define TAG "WEATHER"
/* 响应缓冲：保存原始 HTTP 响应体。
 * 服务器偶尔会忽略 Accept-Encoding 仍返回 gzip，因此需要支持解压。
 * 空气数据响应较大（含污染物/站点），放到 PSRAM 中（实时性要求不高）。 */
EXT_RAM_BSS_ATTR static uint8_t buff[AIR_RESPONSE_MAX];
/* buff 中已写入的字节数（由事件回调累加） */
static uint32_t offset;

/* 用 zlib 解压 gzip 数据：inflateInit2(MAX_WBITS+16) 自动识别并处理
 * gzip 头、尾部 8 字节（CRC32/ISIZE）与校验，解压成功后补 '\0' 作字符串。
 *
 * @param output       解压输出缓冲区（调用方提供，至少 output_size 字节）
 * @param output_size  输出缓冲区容量（含结尾 '\0'）
 * @return ESP_OK 解压成功；ESP_FAIL 初始化失败、数据损坏或输出空间不足
 */
static esp_err_t decompress_gzip(char* output, size_t output_size) {
  /* z_stream 是 zlib 的解压上下文，必须清零后使用 */
  z_stream strm = {0};
  /* windowBits = MAX_WBITS + 16：开启 gzip 自动识别（RFC1952） */
  if (inflateInit2(&strm, MAX_WBITS + 16) != Z_OK) {
    return ESP_FAIL;
  }
  strm.next_in = (Bytef*)buff;
  strm.avail_in = offset;
  strm.next_out = (Bytef*)output;
  /* 预留 1 字节给末尾 '\0' */
  strm.avail_out = (uInt)(output_size - 1U);

  /* Z_FINISH 表示一次性解压到输出耗尽或流结束 */
  const int ret = inflate(&strm, Z_FINISH);
  inflateEnd(&strm); /* 释放 zlib 内部状态（含 32KB 窗口，malloc 分配） */
  if (ret != Z_STREAM_END) {
    return ESP_FAIL;
  }
  /* total_out 为实际解压出的字节数，补字符串结束符 */
  output[strm.total_out] = '\0';
  return ESP_OK;
}

/* esp_http_client 事件回调：把响应数据逐块收集进 buff。
 *
 * HTTP_EVENT_ON_HEADER  打印服务端声明的 Content-Encoding（仅观察用）
 * HTTP_EVENT_ON_DATA    追加数据到 buff，超限则返回 ESP_FAIL 中止请求
 */
static esp_err_t event_handler(esp_http_client_event_t* evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
      if (evt->header_key != NULL && evt->header_value != NULL &&
          strcasecmp(evt->header_key, "Content-Encoding") == 0) {
        ESP_LOGI(TAG, "Content-Encoding: %s", evt->header_value);
      }
      break;
    case HTTP_EVENT_ON_DATA: {
      uint8_t* data = (uint8_t*)evt->data;
      /* 剩余空间不足（还要留 1 字节给 '\0'）则中止请求 */
      if ((size_t)evt->data_len > sizeof(buff) - offset - 1U)
        return ESP_FAIL;
      memcpy(buff + offset, data, evt->data_len);
      offset += evt->data_len;
      break;
    }
    default:
      break;
  }
  return ESP_OK;
}
/* 创建并配置 HTTP 客户端：GET、TLS 证书 bundle、请求头。
 * 请求头固定携带 API Key；Accept-Encoding 设为 identity 期望明文，
 * 但服务端仍可能返回 gzip，由调用方按魔数判断解压。
 */
static esp_http_client_handle_t client_init(
    const char* url,
    http_event_handle_cb event_handler) {
  esp_http_client_config_t config = {
      .url = url,
      .method = HTTP_METHOD_GET,
      .event_handler = event_handler,
      .crt_bundle_attach = esp_crt_bundle_attach, /* 内置根证书，校验 HTTPS */
  };
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client != NULL) {
    esp_http_client_set_header(client, "X-QW-Api-Key", API_KEY);
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
  }
  return client;
}

/* 执行一次 GET：清空缓冲 → 请求 → 按 gzip 魔数决定解压或直拷。
 *
 * @param url      API 地址（weather.h 中的 WEATHER_URL/AIR_URL 等）
 * @param str      输出字符串缓冲（至少 max_size 字节）
 * @param max_size 输出缓冲容量（含结尾 '\0'）
 * @return esp_http_client_perform 的返回值；解压失败时改为 ESP_FAIL
 */
static esp_err_t perform_get(const char* url, char* str, size_t max_size) {
  offset = 0;
  memset(buff, 0, sizeof(buff));
  esp_http_client_handle_t client = client_init(url, event_handler);
  if (client == NULL)
    return ESP_ERR_NO_MEM;

  esp_err_t err = esp_http_client_perform(client);
  buff[offset] = '\0';
  ESP_LOGI(TAG, "GET %s => err=%d, body=%u bytes", url, (int)err,
           (unsigned)offset);
  /* gzip 魔数 0x1F 0x8B：响应为 gzip，需解压；否则直接拷贝明文 */
  if (offset >= 2U && buff[0] == 0x1FU && buff[1] == 0x8BU) {
    ESP_LOGI(TAG, "body is gzip, decompressing...");
    if (decompress_gzip(str, max_size) != ESP_OK) {
      err = ESP_FAIL;
      str[0] = '\0';
      ESP_LOGE(TAG, "gzip decompress failed");
    }
  } else {
    if (offset + 1U > max_size) {
      ESP_LOGE(TAG, "body too large: %u > %u", (unsigned)offset,
               (unsigned)max_size);
      err = ESP_FAIL;
      str[0] = '\0';
    } else {
      memcpy(str, buff, offset + 1U);
    }
  }
  esp_http_client_cleanup(client);
  return err;
}

/* 解析 cJSON 数值：优先取数值本身，其次取对象里的 "value" 字段。
 * 部分 API 返回 { "value": 26.4 } 嵌套结构，统一兼容两种情况。
 */
static double json_number_or_value(const cJSON* item) {
  if (cJSON_IsNumber(item)) {
    return item->valuedouble;
  }

  const cJSON* value = cJSON_GetObjectItemCaseSensitive(item, "value");
  return cJSON_IsNumber(value) ? value->valuedouble : 0.0;
}

/* 兼容字符串/数值两种形式的取值（v1 接口字段均为字符串，如 "11.0"）。
 * 数字直接取 valuedouble；字符串用 strtod 转换；对象取 "value" 字段。 */
static double json_double_or_value(const cJSON* item) {
  if (cJSON_IsNumber(item)) {
    return item->valuedouble;
  }
  if (cJSON_IsString(item) && item->valuestring != NULL) {
    return strtod(item->valuestring, NULL);
  }
  return json_number_or_value(item);
}

/* 解析天气 JSON，把关心的字段写入 weather_t。
 * 注意：返回的 root 由调用方负责 cJSON_Delete，字段指针随之失效，
 * 调用方需在删除前拷贝所需值或置 NULL。
 *
 * @return 解析成功返回 root（非 NULL）；JSON 非法返回 NULL
 */
static cJSON* praseWeatherJson(char* str, weather_t* weather) {
  cJSON* root = cJSON_Parse(str);
  if (root == NULL) {
    return NULL;
  }
  /* 直接引用 cJSON 树内节点，延迟解析以节省内存 */
  weather->condition = cJSON_GetObjectItem(root, "condition");
  weather->feelsLike = cJSON_GetObjectItem(root, "feelsLike");
  weather->humidity = cJSON_GetObjectItem(root, "humidity");
  weather->temperature = cJSON_GetObjectItem(root, "temperature");

  const cJSON* text = cJSON_GetObjectItem(weather->condition, "text");

  /* 天气状况文字（如：晴、多云、小雨）拷贝到定长数组 */
  if (cJSON_IsString(text) && text->valuestring != NULL) {
    snprintf(weather->weatherData.text, sizeof(weather->weatherData.text), "%s",
             text->valuestring);
  }
  weather->weatherData.t_value = json_number_or_value(weather->temperature);
  weather->weatherData.f_value = json_number_or_value(weather->feelsLike);
  weather->weatherData.h_value = json_number_or_value(weather->humidity);

  return root;
}
/* 解析空气质量 JSON，把关心的字段写入 Air_t。
 * 兼容两种接口返回：
 *  A) 新版 indexes[]/pollutants[]（air.json 示例）：
 *       indexes[] 取 us-epa 项 aqi/category/primaryPollutant/color
 *       pollutants[] 各污染物浓度（concentration.value）
 *  B) 经典 v1 now 对象（字段为字符串，如 "11.0"）：
 *       now.aqi / now.category / now.primary / now.pm2p5 ...
 *
 * @return 解析成功返回 root（非 NULL）；JSON 非法返回 NULL
 */
static cJSON* praseAirJson(char* str, Air_t* air) {
  cJSON* root = cJSON_Parse(str);
  if (root == NULL) {
    return NULL;
  }

  const cJSON* now = cJSON_GetObjectItem(root, "now");
  if (cJSON_IsObject(now)) {
    /* 经典 v1 格式：now 对象，字段为字符串 */
    const cJSON* aqi_now = cJSON_GetObjectItem(now, "aqi");
    if (cJSON_IsNumber(aqi_now) || cJSON_IsString(aqi_now)) {
      air->AirData.aqi = (int)json_double_or_value(aqi_now);
    }
    const cJSON* category = cJSON_GetObjectItem(now, "category");
    if (cJSON_IsString(category) && category->valuestring != NULL) {
      snprintf(air->AirData.category, sizeof(air->AirData.category), "%s",
               category->valuestring);
    }
    const cJSON* primary = cJSON_GetObjectItem(now, "primary");
    if (cJSON_IsString(primary) && primary->valuestring != NULL &&
        strcasecmp(primary->valuestring, "NA") != 0) {
      snprintf(air->AirData.primaryPollutant,
               sizeof(air->AirData.primaryPollutant), "%s",
               primary->valuestring);
    }
    air->AirData.pm2p5 = json_double_or_value(cJSON_GetObjectItem(now, "pm2p5"));
    air->AirData.pm10 = json_double_or_value(cJSON_GetObjectItem(now, "pm10"));
    air->AirData.no2 = json_double_or_value(cJSON_GetObjectItem(now, "no2"));
    air->AirData.o3 = json_double_or_value(cJSON_GetObjectItem(now, "o3"));
    air->AirData.co = json_double_or_value(cJSON_GetObjectItem(now, "co"));
    /* v1 无颜色字段，按 AQI 分级着色 */
    if (air->AirData.aqi <= 50) {
      air->AirData.red = 0; air->AirData.green = 228; air->AirData.blue = 0;
    } else if (air->AirData.aqi <= 100) {
      air->AirData.red = 255; air->AirData.green = 255; air->AirData.blue = 0;
    } else if (air->AirData.aqi <= 150) {
      air->AirData.red = 255; air->AirData.green = 126; air->AirData.blue = 0;
    } else if (air->AirData.aqi <= 200) {
      air->AirData.red = 255; air->AirData.green = 0; air->AirData.blue = 0;
    } else if (air->AirData.aqi <= 300) {
      air->AirData.red = 153; air->AirData.green = 0; air->AirData.blue = 76;
    } else {
      air->AirData.red = 126; air->AirData.green = 0; air->AirData.blue = 35;
    }
    return root;
  }

  /* 新版 indexes[]/pollutants[] 格式 */
  const cJSON* indexes = cJSON_GetObjectItem(root, "indexes");
  const cJSON* us_epa = NULL;
  if (cJSON_IsArray(indexes)) {
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, indexes) {
      const cJSON* code = cJSON_GetObjectItem(item, "code");
      if (cJSON_IsString(code) && code->valuestring != NULL &&
          strcmp(code->valuestring, "us-epa") == 0) {
        us_epa = item;
        break;
      }
    }
    if (us_epa == NULL) {
      us_epa = indexes->child;
    }
  }
  if (us_epa != NULL) {
    const cJSON* aqi = cJSON_GetObjectItem(us_epa, "aqi");
    if (cJSON_IsNumber(aqi)) {
      air->AirData.aqi = (int)aqi->valuedouble;
    }
    const cJSON* category = cJSON_GetObjectItem(us_epa, "category");
    if (cJSON_IsString(category) && category->valuestring != NULL) {
      snprintf(air->AirData.category, sizeof(air->AirData.category), "%s",
               category->valuestring);
    }
    const cJSON* primary = cJSON_GetObjectItem(us_epa, "primaryPollutant");
    if (cJSON_IsObject(primary)) {
      const cJSON* name = cJSON_GetObjectItem(primary, "name");
      if (cJSON_IsString(name) && name->valuestring != NULL) {
        snprintf(air->AirData.primaryPollutant,
                 sizeof(air->AirData.primaryPollutant), "%s",
                 name->valuestring);
      }
    }
    const cJSON* color = cJSON_GetObjectItem(us_epa, "color");
    if (cJSON_IsObject(color)) {
      const cJSON* r = cJSON_GetObjectItem(color, "red");
      const cJSON* g = cJSON_GetObjectItem(color, "green");
      const cJSON* b = cJSON_GetObjectItem(color, "blue");
      if (cJSON_IsNumber(r))
        air->AirData.red = r->valueint;
      if (cJSON_IsNumber(g))
        air->AirData.green = g->valueint;
      if (cJSON_IsNumber(b))
        air->AirData.blue = b->valueint;
    }
  }

  const cJSON* pollutants = cJSON_GetObjectItem(root, "pollutants");
  if (cJSON_IsArray(pollutants)) {
    cJSON* item = NULL;
    cJSON_ArrayForEach(item, pollutants) {
      const cJSON* code = cJSON_GetObjectItem(item, "code");
      if (!cJSON_IsString(code) || code->valuestring == NULL) {
        continue;
      }
      const cJSON* concentration = cJSON_GetObjectItem(item, "concentration");
      const double value = json_number_or_value(concentration);
      if (strcmp(code->valuestring, "pm2p5") == 0) {
        air->AirData.pm2p5 = value;
      } else if (strcmp(code->valuestring, "pm10") == 0) {
        air->AirData.pm10 = value;
      } else if (strcmp(code->valuestring, "no2") == 0) {
        air->AirData.no2 = value;
      } else if (strcmp(code->valuestring, "o3") == 0) {
        air->AirData.o3 = value;
      } else if (strcmp(code->valuestring, "co") == 0) {
        air->AirData.co = value;
      }
    }
  }

  return root;
}

/* 对外接口：获取并解析天气，结果写入 weather。
 * 流程：参数校验 → 发起 GET → 解析 JSON → 校验有效性 → 清理临时节点。
 *
 * @param url     API 地址
 * @param weather 输出结构体
 * @param buf     中间字符串缓冲（转发给 perform_get）
 * @return ESP_OK 成功；无效参数/请求失败/解析失败/无天气文字均返回错误
 */
esp_err_t get_weather(const char* url, weather_t* weather, char* buf) {
  if (url == NULL || weather == NULL || buf == NULL)
    return ESP_ERR_INVALID_ARG;

  memset(weather, 0, sizeof(*weather));
  esp_err_t err = perform_get(url, buf, WEATHER_RESPONSE_MAX);
  if (err != ESP_OK)
    return err;
  cJSON* root = praseWeatherJson(buf, weather);
  /* root 为 NULL（JSON 非法）或天气文字为空都视为失败 */
  if (root == NULL || weather->weatherData.text[0] == '\0') {
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  cJSON_Delete(root);
  /* 防止调用方持有已释放的 cJSON 指针 */
  weather->condition = NULL;
  weather->temperature = NULL;
  weather->feelsLike = NULL;
  weather->humidity = NULL;
  return ESP_OK;
}

esp_err_t get_air(const char* url, Air_t* air, char* buf) {
  if (url == NULL || air == NULL || buf == NULL)
    return ESP_ERR_INVALID_ARG;

  memset(air, 0, sizeof(*air));
  esp_err_t err = perform_get(url, buf, AIR_RESPONSE_MAX);
  if (err != ESP_OK)
    return err;
  cJSON* root = praseAirJson(buf, air);
  if (root == NULL || air->AirData.category[0] == '\0') {
    if (root != NULL) {
      const cJSON* code = cJSON_GetObjectItem(root, "code");
      if (cJSON_IsString(code) && code->valuestring != NULL) {
        ESP_LOGE(TAG, "air API error code=%s", code->valuestring);
      }
    }
    cJSON_Delete(root);
    ESP_LOGE(TAG, "air JSON parse failed: category empty, body=%.200s", buf);
    return ESP_FAIL;
  }

  cJSON_Delete(root);
  ESP_LOGI(TAG,
           "air parsed: aqi=%d category=%s primary=%s pm25=%.1f pm10=%.1f "
           "no2=%.2f o3=%.2f co=%.2f",
           air->AirData.aqi, air->AirData.category, air->AirData.primaryPollutant,
           air->AirData.pm2p5, air->AirData.pm10, air->AirData.no2,
           air->AirData.o3, air->AirData.co);
  return ESP_OK;
}