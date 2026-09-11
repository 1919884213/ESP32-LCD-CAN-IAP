#include "ui_data.h"
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* 全局互斥量，用于保护各共享数据结构的并发访问 */
static SemaphoreHandle_t s_data_mutex;
/* 各类共享数据（驾驶、灯光、环境、IMU） */
static ui_drive_data_t s_drive_data;
static ui_lighting_data_t s_lighting_data;
static ui_env_data_t s_env_data;
static ui_imu_data_t s_imu_data;


/* 初始化共享数据互斥量（线程安全，重复调用安全） */
void ui_data_init(void) {
  if (s_data_mutex == NULL)
    s_data_mutex = xSemaphoreCreateMutex();
}

/* 写入驾驶数据（互斥保护，data 为空时直接返回） */
void ui_set_drive_data(const ui_drive_data_t* data) {
  if (data == NULL)
    return;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_drive_data = *data;
  xSemaphoreGive(s_data_mutex);
}

/* 写入灯光数据（互斥保护，data 为空时直接返回） */
void ui_set_lighting_data(const ui_lighting_data_t* data) {
  if (data == NULL)
    return;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_lighting_data = *data;
  xSemaphoreGive(s_data_mutex);
}

/* 写入环境数据（温度/湿度/光照，互斥保护，不影响距离字段） */
void ui_set_environment_data(float temperature_c,
                             float humidity_percent,
                             uint8_t light_percent) {
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_env_data.temperature_c = temperature_c;
  s_env_data.humidity_percent = humidity_percent;
  s_env_data.light_percent = light_percent;
  xSemaphoreGive(s_data_mutex);
}

/* 写入距离（cm，互斥保护，不影响温湿度/光照字段） */
void ui_set_distance_cm(uint16_t distance_cm) {
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_env_data.distance_cm = distance_cm;
  xSemaphoreGive(s_data_mutex);
}

/* 写入 IMU 数据（互斥保护，data 为空时直接返回） */
void ui_set_imu_data(const ui_imu_data_t* data) {
  if (data == NULL)
    return;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_imu_data = *data;
  xSemaphoreGive(s_data_mutex);
}

/* 仅更新速度字段（互斥保护，不影响日期/时钟/就绪状态） */
void ui_set_speed_kph(float speed_kph) {
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_drive_data.speed_kph = speed_kph;
  xSemaphoreGive(s_data_mutex);
}

/* 仅更新日期与时钟字段（互斥保护，NTP 同步结果） */
void ui_set_datetime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                     uint8_t minute, uint8_t second) {
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_drive_data.year = year;
  s_drive_data.month = month;
  s_drive_data.day = day;
  s_drive_data.hour = hour;
  s_drive_data.minute = minute;
  s_drive_data.second = second;
  xSemaphoreGive(s_data_mutex);
}

/* 读取驾驶数据（互斥保护，data 为空时直接返回） */
void ui_get_drive_data(ui_drive_data_t* data) {
  if (data == NULL)
    return;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  *data = s_drive_data;
  xSemaphoreGive(s_data_mutex);
}

/* 读取灯光数据（互斥保护，data 为空时直接返回） */
void ui_get_lighting_data(ui_lighting_data_t* data) {
  if (data == NULL)
    return;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  *data = s_lighting_data;
  xSemaphoreGive(s_data_mutex);
}

/* 读取环境数据（互斥保护，data 为空时直接返回） */
void ui_get_environment_data(ui_env_data_t* data) {
  if (data == NULL)
    return;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  *data = s_env_data;
  xSemaphoreGive(s_data_mutex);
}

/* 读取 IMU 数据（互斥保护，data 为空时直接返回） */
void ui_get_imu_data(ui_imu_data_t* data) {
  if (data == NULL)
    return;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  *data = s_imu_data;
  xSemaphoreGive(s_data_mutex);
}
