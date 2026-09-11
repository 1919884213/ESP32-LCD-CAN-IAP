#include "onenet_token.h"

#include <stdio.h>
#include <string.h>
#include "mbedtls/base64.h"
#include "mbedtls/md.h"

static const char* sign_method_name(onenet_sign_method_t method) {
  switch (method) {
    case ONENET_SIGN_SHA1:
      return "sha1";
    case ONENET_SIGN_MD5:
      return "md5";
    case ONENET_SIGN_SHA256:
      return "sha256";
    default:
      return "sha256";
  }
}

static mbedtls_md_type_t sign_method_type(onenet_sign_method_t method) {
  switch (method) {
    case ONENET_SIGN_SHA1:
      return MBEDTLS_MD_SHA1;
    case ONENET_SIGN_MD5:
      return MBEDTLS_MD_MD5;
    case ONENET_SIGN_SHA256:
      return MBEDTLS_MD_SHA256;
    default:
      return MBEDTLS_MD_SHA256;
  }
}

/* 按 Java URLEncoder 规则转义：字母数字与 . - * _ 不转义，空格 -> '+'，其余 %XX
 */
static int url_encode(const char* in, char* out, size_t out_size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  for (size_t i = 0; in[i] != '\0'; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '*' ||
        c == '_') {
      if (o + 1U >= out_size)
        return -1;
      out[o++] = (char)c;
    } else if (c == ' ') {
      if (o + 1U >= out_size)
        return -1;
      out[o++] = '+';
    } else {
      if (o + 3U >= out_size)
        return -1;
      out[o++] = '%';
      out[o++] = hex[c >> 4U];
      out[o++] = hex[c & 0x0FU];
    }
  }
  out[o] = '\0';
  return (int)o;
}

/* encryptText = expirationTime + "\n" + method + "\n" + resourceName + "\n" +
 * version */
static esp_err_t hmac_sign(const char* encrypt_text,
                           const char* access_key,
                           onenet_sign_method_t method,
                           char* sign_out,
                           size_t sign_size) {
  unsigned char key[64];
  size_t key_len = 0;
  if (mbedtls_base64_decode(key, sizeof(key), &key_len,
                            (const unsigned char*)access_key,
                            strlen(access_key)) != 0 ||
      key_len == 0) {
    return ESP_FAIL;
  }

  const mbedtls_md_info_t* info =
      mbedtls_md_info_from_type(sign_method_type(method));
  if (info == NULL)
    return ESP_FAIL;

  unsigned char digest[MBEDTLS_MD_MAX_SIZE];
  if (mbedtls_md_hmac(info, key, key_len, (const unsigned char*)encrypt_text,
                      strlen(encrypt_text), digest) != 0) {
    return ESP_FAIL;
  }

  size_t out_len = 0;
  if (mbedtls_base64_encode((unsigned char*)sign_out, sign_size, &out_len,
                            digest, mbedtls_md_get_size(info)) != 0) {
    return ESP_FAIL;
  }
  sign_out[out_len] = '\0';
  return ESP_OK;
}

esp_err_t onenet_token_generate(const char* version,
                                const char* resource_name,
                                const char* expiration_time,
                                const char* access_key,
                                onenet_sign_method_t method,
                                char* token,
                                size_t token_size) {
  if (version == NULL || resource_name == NULL || expiration_time == NULL ||
      access_key == NULL || token == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  char encrypt_text[512];
  int n = snprintf(encrypt_text, sizeof(encrypt_text), "%s\n%s\n%s\n%s",
                   expiration_time, sign_method_name(method), resource_name,
                   version);
  if (n < 0 || (size_t)n >= sizeof(encrypt_text))
    return ESP_ERR_INVALID_ARG;

  char sign[64];
  if (hmac_sign(encrypt_text, access_key, method, sign, sizeof(sign)) != ESP_OK)
    return ESP_FAIL;

  char res[256];
  char sig[192];
  if (url_encode(resource_name, res, sizeof(res)) < 0 ||
      url_encode(sign, sig, sizeof(sig)) < 0) {
    return ESP_ERR_INVALID_ARG;
  }

  n = snprintf(token, token_size, "version=%s&res=%s&et=%s&method=%s&sign=%s",
               version, res, expiration_time, sign_method_name(method), sig);
  if (n < 0 || (size_t)n >= token_size)
    return ESP_ERR_INVALID_ARG;

  return ESP_OK;
}