/* UI 共享数据模块头文件：定义各界面数据结构和线程安全的读写接口 */
#ifndef UI_DATA_H
#define UI_DATA_H

#include <stdbool.h>
#include <stdint.h>

/* 驾驶数据：速度（km/h）、日期与时钟、就绪状态 */
typedef struct {
    float speed_kph;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool ready;
} ui_drive_data_t;

/* 灯光数据：转向灯与远近光开关状态 */
typedef struct {
    bool left_turn;
    bool right_turn;
    bool high_beam;
    bool low_beam;
} ui_lighting_data_t;

/* 环境数据：温度（°C）、湿度（%）、光照（%）、距离（cm） */
typedef struct {
    float temperature_c;
    float humidity_percent;
    uint8_t light_percent;
    uint16_t distance_cm;
} ui_env_data_t;

/* IMU 数据：三轴加速度与角速度 */
typedef struct {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
} ui_imu_data_t;

/* 初始化共享数据互斥量 */
void ui_data_init(void);

/* 写入接口（互斥保护） */
void ui_set_drive_data(const ui_drive_data_t *data);
void ui_set_lighting_data(const ui_lighting_data_t *data);
void ui_set_environment_data(float temperature_c, float humidity_percent,
                             uint8_t light_percent);
void ui_set_distance_cm(uint16_t distance_cm);
void ui_set_imu_data(const ui_imu_data_t *data);

/* 局部更新：仅写速度，不影响日期/时钟/就绪状态 */
void ui_set_speed_kph(float speed_kph);

/* 局部更新：仅写日期与时钟（NTP 同步结果） */
void ui_set_datetime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                     uint8_t minute, uint8_t second);

/* 读取接口（互斥保护） */
void ui_get_drive_data(ui_drive_data_t *data);
void ui_get_lighting_data(ui_lighting_data_t *data);
void ui_get_environment_data(ui_env_data_t *data);
void ui_get_imu_data(ui_imu_data_t *data);

#endif /* UI_DATA_H */
