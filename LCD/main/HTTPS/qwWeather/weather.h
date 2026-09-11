#include <stddef.h>
#include "cJson.h"
#include "esp_err.h"
#include "esp_http_client.h"

#define HOST "nf6fr9rxj9.re.qweatherapi.com"
#include "secrets.h"
#define API_KEY SECRET_QWEATHER_API_KEY
#define WEATHER_URL                                              \
  "https://nf6fr9rxj9.re.qweatherapi.com/weather/v1/current/30/" \
  "104?localTime=false&lang=zh"

#define AIR_URL                                                    \
  "https://nf6fr9rxj9.re.qweatherapi.com/airquality/v1/current/"   \
  "39.92/116.41?lang=zh"

#define WEATHER_RESPONSE_MAX 2048U
#define AIR_RESPONSE_MAX 8192U
typedef struct weatherData {
  char text[32];
  double t_value;
  double f_value;
  double h_value;
} weatherData_t;

typedef struct weather {
  weatherData_t weatherData;
  cJSON* condition;
  cJSON* temperature;
  cJSON* feelsLike;
  cJSON* humidity;
} weather_t;

typedef struct Air_data {
  int aqi;            /* AQI 指数 */
  char category[24];  /* 空气质量等级（优/良/…） */
  char primaryPollutant[32]; /* 首要污染物名称 */
  int red;            /* AQI 指示色 */
  int green;
  int blue;
  double pm2p5;       /* PM2.5 浓度 */
  double pm10;        /* PM10 浓度 */
  double no2;         /* NO2 浓度 */
  double o3;          /* O3 浓度 */
  double co;          /* CO 浓度 */
} AirData_t;

typedef struct Air {
  AirData_t AirData;
} Air_t;
// 获取天气信息
esp_err_t get_weather(const char* url, weather_t* weather, char* buf);
esp_err_t get_air(const char* url, Air_t* air, char* buf);
