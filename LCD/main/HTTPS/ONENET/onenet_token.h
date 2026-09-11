#ifndef ONENET_TOKEN_H
#define ONENET_TOKEN_H

#include <stddef.h>
#include "esp_err.h"

#define ONENET_TOKEN_MAX 512U

/* 签名算法，对应 Java 版 SignatureMethod */
typedef enum {
  ONENET_SIGN_SHA1,
  ONENET_SIGN_MD5,
  ONENET_SIGN_SHA256,
} onenet_sign_method_t;

/* 生成 OneNET 鉴权 token，输出到 token。
 * 参数含义与 Java 版 assembleToken 一致：
 *   version        协议版本，如 "2018-10-31"
 *   resource_name  资源名，如 "products/123123/devices/device"
 *   expiration_time  过期时间戳（秒）
 *   access_key     产品/设备密钥（Base64）
 *   method         签名算法
 * token 缓冲区需至少 ONENET_TOKEN_MAX 字节 */
esp_err_t onenet_token_generate(const char* version, const char* resource_name,
                                const char* expiration_time,
                                const char* access_key,
                                onenet_sign_method_t method, char* token,
                                size_t token_size);

#endif /* ONENET_TOKEN_H */