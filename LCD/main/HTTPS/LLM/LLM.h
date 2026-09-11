#ifndef LLM_H
#define LLM_H

#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stddef.h>

#define URL "https://open.bigmodel.cn/api/paas/v4/chat/completions"
#include "secrets.h"
#define APIKEY SECRET_LLM…KEY

/* 异步发起一次 LLM 请求（流式），立即返回 */
void LLM(char* promart);
/* 是否有请求正在进行 */
bool LLM_IsBusy(void);
/* 取走最近一次的回答；无新回答返回 false，有则拷贝到 out 并清除标志 */
bool LLM_TakeAnswer(char* out, size_t out_size);
/* 流式：当前已收到的回答字节数 */
size_t LLM_StreamLen(void);
/* 流式：拷贝 ans_buf[offset, offset+out_size) 到 out，返回实际拷贝字节数（不消费） */
size_t LLM_StreamRead(char* out, size_t out_size, size_t offset);
/* 流式：返回 needle 在回答缓冲中首次出现的偏移；未找到返回当前回答长度 */
size_t LLM_StreamFind(const char* needle);

#endif /* LLM_H */
