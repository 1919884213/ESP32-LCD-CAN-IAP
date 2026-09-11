#include "LLM.h"

#include <stdbool.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "LLM"

/* 后台任务栈尽量给大:cJSON + TLS 都吃栈 */
#define LLM_TASK_STACK_SIZE 16384

/* 请求体缓冲（JSON 序列化结果） */
#define REQ_BUF_SIZE 1024
static char req_buf[REQ_BUF_SIZE];

/* 回答缓冲:流式解析出的 assistant 内容增量追加,供 UI 边收边显示。
 * 非实时大数据缓冲,按项目约定放 PSRAM。 */
#define ANS_BUF_SIZE 4096
EXT_RAM_BSS_ATTR static char ans_buf[ANS_BUF_SIZE];
static volatile size_t ans_len;
static volatile bool ans_ready;

/* 回答缓冲互斥:后台任务追加,UI 读取,避免跨线程撕裂 */
static SemaphoreHandle_t ans_mutex;

/* 请求信号量:LLM()（UI 事件）释放,后台常驻任务等待 */
static SemaphoreHandle_t s_req_sem;
static volatile bool s_task_alive;

/* 把完整文本写入 ans_buf（覆盖）并置就绪标志 */
static void set_answer(const char* text) {
  if (text == NULL) {
    text = "";
  }
  xSemaphoreTake(ans_mutex, portMAX_DELAY);
  size_t n = strlen(text);
  if (n >= ANS_BUF_SIZE) {
    n = ANS_BUF_SIZE - 1U;
  }
  memcpy(ans_buf, text, n);
  ans_buf[n] = '\0';
  ans_len = n;
  ans_ready = true;
  xSemaphoreGive(ans_mutex);
}

/* 追加一段流式内容到回答缓冲（截断保护） */
static void ans_append(const char* text) {
  if (text == NULL || text[0] == '\0') {
    return;
  }
  xSemaphoreTake(ans_mutex, portMAX_DELAY);
  size_t n = strlen(text);
  if (ans_len + n >= ANS_BUF_SIZE) {
    n = ANS_BUF_SIZE - 1U - ans_len;
  }
  if (n > 0U) {
    memcpy(ans_buf + ans_len, text, n);
    ans_len += n;
    ans_buf[ans_len] = '\0';
  }
  xSemaphoreGive(ans_mutex);
}

/* 提示词中转缓冲:LLM() 立即返回,由后台任务消费 */
static char prompt_buf[256];
static volatile bool llm_busy;

bool LLM_IsBusy(void) {
  return llm_busy;
}

/* 当前已收到多少字节回答（供 UI 增量显示） */
size_t LLM_StreamLen(void) {
  if (ans_mutex == NULL) {
    return 0;
  }
  xSemaphoreTake(ans_mutex, portMAX_DELAY);
  const size_t len = ans_len;
  xSemaphoreGive(ans_mutex);
  return len;
}

/* 拷贝 ans_buf[offset, offset+out_size) 到 out,返回实际拷贝字节数（不消费） */
size_t LLM_StreamRead(char* out, size_t out_size, size_t offset) {
  if (out == NULL || out_size == 0U || ans_mutex == NULL) {
    return 0;
  }
  xSemaphoreTake(ans_mutex, portMAX_DELAY);
  size_t n = (offset < ans_len) ? (ans_len - offset) : 0U;
  if (n >= out_size) {
    n = out_size - 1U;
  }
  if (n > 0U) {
    memcpy(out, ans_buf + offset, n);
  }
  out[n] = '\0';
  xSemaphoreGive(ans_mutex);
  return n;
}

/* 返回 needle 在回答缓冲中首次出现的偏移；未找到返回当前回答长度 */
size_t LLM_StreamFind(const char* needle) {
  if (needle == NULL || needle[0] == '\0' || ans_mutex == NULL) {
    return 0;
  }
  const size_t nlen = strlen(needle);
  xSemaphoreTake(ans_mutex, portMAX_DELAY);
  size_t result = ans_len;
  if (ans_len >= nlen) {
    for (size_t i = 0; i + nlen <= ans_len; i++) {
      if (memcmp(ans_buf + i, needle, nlen) == 0) {
        result = i;
        break;
      }
    }
  }
  xSemaphoreGive(ans_mutex);
  return result;
}

bool LLM_TakeAnswer(char* out, size_t out_size) {
  if (!ans_ready || out == NULL || out_size == 0U || ans_mutex == NULL) {
    return false;
  }
  xSemaphoreTake(ans_mutex, portMAX_DELAY);
  strncpy(out, ans_buf, out_size - 1U);
  out[out_size - 1U] = '\0';
  ans_ready = false;
  ans_len = 0;
  ans_buf[0] = '\0';
  xSemaphoreGive(ans_mutex);
  return true;
}

/* 解析一行 SSE 数据:追加 content,返回 true 表示流已结束 */
static bool handle_sse_line(const char* line) {
  if (strncmp(line, "data:", 5) != 0) {
    return false;
  }
  const char* payload = line + 5;
  while (*payload == ' ') {
    payload++;
  }
  if (strcmp(payload, "[DONE]") == 0) {
    return true;
  }

  cJSON* root = cJSON_Parse(payload);
  if (root == NULL) {
    ESP_LOGW(TAG, "skip unparsable chunk: %s", payload);
    return false;
  }
  cJSON* choices = cJSON_GetObjectItem(root, "choices");
  cJSON* item = (choices != NULL) ? cJSON_GetArrayItem(choices, 0) : NULL;
  cJSON* delta = (item != NULL) ? cJSON_GetObjectItem(item, "delta") : NULL;
  cJSON* content =
      (delta != NULL) ? cJSON_GetObjectItem(delta, "content") : NULL;
  if (cJSON_IsString(content) && content->valuestring != NULL) {
    ans_append(content->valuestring);
  }
  cJSON* finish =
      (item != NULL) ? cJSON_GetObjectItem(item, "finish_reason") : NULL;
  const bool done = cJSON_IsString(finish) && finish->valuestring != NULL &&
                    strcmp(finish->valuestring, "stop") == 0;
  cJSON_Delete(root);
  return done;
}

/* 常驻工作任务:创建一次,循环等待请求信号量 */
static void llm_task(void* arg);

/* 序列化请求 JSON 并写入 buf（保证 \0 结尾）,返回值交给调用方释放 */
static cJSON* generate_json(const char* prompt, char* buf, size_t buf_size) {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "model", "glm-4-flash");
  cJSON* messages = cJSON_CreateArray();
  cJSON* role1 = cJSON_CreateObject();
  cJSON_AddStringToObject(role1, "role", "system");
  cJSON_AddStringToObject(
      role1, "content",
      "你是一个智能座舱聊天助手。每次都必须先用不超过20个字简短回答用户，"
      "再另起一行，严格按\"选项N:内容\"格式给出4个聊天选项，每个选项单独一行，"
      "不要用其他编号或符号，也绝不能只给选项而省略回答。选项只能是与助手继续"
      "聊天的话题或追问，不要涉及车控、天气、导航等功能。示例:\n"
      "好的呀。\n选项1:再多讲一点\n选项2:换个话题\n选项3:给我讲个笑话\n"
      "选项4:你怎么看");

  cJSON* role2 = cJSON_CreateObject();
  cJSON_AddStringToObject(role2, "role", "user");
  cJSON_AddStringToObject(role2, "content", prompt);

  cJSON_AddItemToArray(messages, role1);
  cJSON_AddItemToArray(messages, role2);
  cJSON_AddItemToObject(root, "messages", messages);
  cJSON_AddBoolToObject(root, "stream", true);
  cJSON_AddNumberToObject(root, "temperature", 1);

  /* 对象节点的 valuestring 是 NULL,必须用 Print 系列函数序列化 */
  char* printed = cJSON_PrintUnformatted(root);
  if (printed != NULL) {
    strncpy(buf, printed, buf_size - 1U);
    buf[buf_size - 1U] = '\0';
    cJSON_free(printed);
  } else {
    buf[0] = '\0';
  }
  return root;
}

/* 设置请求方法/头/体（每次请求都要做,句柄跨请求复用） */
static void llm_setup_request(esp_http_client_handle_t client) {
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "Accept", "text/event-stream");
  esp_http_client_set_header(client, "Authorization", "Bearer " APIKEY);
}

void LLM(char* promart) {
  if (llm_busy) {
    ESP_LOGW(TAG, "request in progress, dropped");
    return;
  }

  size_t len = strlen(promart);
  if (len == 0 || len >= sizeof(prompt_buf)) {
    ESP_LOGE(TAG, "invalid prompt length %d", (int)len);
    return;
  }
  if (ans_mutex == NULL) {
    ans_mutex = xSemaphoreCreateMutex();
  }
  if (s_req_sem == NULL) {
    s_req_sem = xSemaphoreCreateBinary();
  }
  xSemaphoreTake(ans_mutex, portMAX_DELAY);
  ans_len = 0;
  ans_buf[0] = '\0';
  ans_ready = false;
  xSemaphoreGive(ans_mutex);
  memcpy(prompt_buf, promart, len + 1U);
  llm_busy = true;

  if (!s_task_alive) {
    s_task_alive = true;
    if (xTaskCreate(llm_task, "llm_task", LLM_TASK_STACK_SIZE, NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS) {
      ESP_LOGE(TAG, "create llm_task failed");
      s_task_alive = false;
      llm_busy = false;
      return;
    }
  }
  /* 释放信号量通知常驻任务处理；任务未就绪时令牌会被留存 */
  xSemaphoreGive(s_req_sem);
}

static void llm_task(void* arg) {
  (void)arg;
  esp_http_client_config_t cfg = {.url = URL,
                                  .crt_bundle_attach = esp_crt_bundle_attach,
                                  .buffer_size = 2048,
                                  .buffer_size_tx = 2048,
                                  .timeout_ms = 60000,
                                  .keep_alive_enable = true,
                                  .keep_alive_idle = 30,
                                  .keep_alive_interval = 5,
                                  .keep_alive_count = 3};
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == NULL) {
    ESP_LOGE(TAG, "client init failed");
    set_answer("[连接失败]");
    s_task_alive = false;
    llm_busy = false;
    vTaskDelete(NULL);
  }

  for (;;) {
    xSemaphoreTake(s_req_sem, portMAX_DELAY);

    cJSON* root = generate_json(prompt_buf, req_buf, sizeof(req_buf));
    size_t post_len = strlen(req_buf);
    ESP_LOGI(TAG, "request: %s", req_buf);
    if (post_len == 0) {
      cJSON_Delete(root);
      set_answer("[请求体为空]");
      llm_busy = false;
      continue;
    }

    llm_setup_request(client);
    esp_http_client_set_post_field(client, req_buf, (int)post_len);

    /* 打开连接（复用失败的旧连接时重建一次） */
    esp_err_t err = esp_http_client_open(client, (int)post_len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "open failed: %s, re-init", esp_err_to_name(err));
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      client = esp_http_client_init(&cfg);
      if (client == NULL) {
        cJSON_Delete(root);
        set_answer("[连接失败]");
        llm_busy = false;
        continue;
      }
      llm_setup_request(client);
      esp_http_client_set_post_field(client, req_buf, (int)post_len);
      err = esp_http_client_open(client, (int)post_len);
      if (err != ESP_OK) {
        cJSON_Delete(root);
        set_answer("[请求失败]");
        llm_busy = false;
        continue;
      }
    }

    /* 发送请求体（open 只发头,正文需手动写） */
    int wret = esp_http_client_write(client, req_buf, (int)post_len);
    if (wret != (int)post_len) {
      ESP_LOGE(TAG, "write body failed");
      esp_http_client_close(client);
      cJSON_Delete(root);
      set_answer("[请求失败]");
      llm_busy = false;
      continue;
    }
    esp_http_client_fetch_headers(client);

    /* 流式读取:按行解析 SSE,取到完整回答停止 */
    char line[2048];
    size_t line_len = 0;
    bool stream_done = false;
    char chunk[256];
    while (!stream_done) {
      int n = esp_http_client_read(client, chunk, sizeof(chunk));
      if (n <= 0) {
        break;
      }
      for (int i = 0; i < n; i++) {
        if (chunk[i] == '\n') {
          line[line_len] = '\0';
          line_len = 0;
          if (handle_sse_line(line)) {
            stream_done = true;
            break;
          }
        } else if (line_len < sizeof(line) - 1U) {
          line[line_len++] = chunk[i];
        }
      }
    }
    cJSON_Delete(root);

    /* 响应完整且服务端允许 keep-alive 时保留连接复用,否则关闭 */
    if (!esp_http_client_is_complete_data_received(client) ||
        !esp_http_client_is_persistent_connection(client)) {
      esp_http_client_close(client);
    }

    if (ans_len > 0U) {
      ans_ready = true;
    } else {
      set_answer("[无回答内容]");
    }
    ESP_LOGI(TAG, "assistant(%u): %s", (unsigned)ans_len, ans_buf);
    llm_busy = false;
  }
}