#include "now.h"
#include "esp_sntp.h"
#include "time.h"

static bool s_sntp_started;

void now_sync_start(void) {
  if (s_sntp_started) {
    return;
  }
  s_sntp_started = true;

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  /*按顺序访问服务器直至成功*/
  esp_sntp_setservername(0, name1);
  esp_sntp_setservername(1, name2);
  esp_sntp_setservername(2, name3);
  esp_sntp_init();

  setenv("TZ", "CST-8", 1);  // 1 :覆盖旧设置
  tzset();                   // 2:激活配置
}

bool now_get_datetime(uint16_t *year, uint8_t *month, uint8_t *day,
                      uint8_t *hour, uint8_t *minute, uint8_t *second) {
  time_t now = time(NULL);
  struct tm timeinfo;
  localtime_r(&now, &timeinfo);
  /* 1970 起算的系统时间，说明 SNTP 尚未同步成功 */
  if (timeinfo.tm_year < (2020 - 1900)) {
    return false;
  }
  if (year != NULL) *year = (uint16_t)(timeinfo.tm_year + 1900);
  if (month != NULL) *month = (uint8_t)(timeinfo.tm_mon + 1);
  if (day != NULL) *day = (uint8_t)timeinfo.tm_mday;
  if (hour != NULL) *hour = (uint8_t)timeinfo.tm_hour;
  if (minute != NULL) *minute = (uint8_t)timeinfo.tm_min;
  if (second != NULL) *second = (uint8_t)timeinfo.tm_sec;
  return true;
}
