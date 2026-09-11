#include "esp_log.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include <stdbool.h>
#include "secrets.h"
// 产品ID
#define PRODUCTID "1SPYOdVhCv"
//产品密钥
#define PRODUCTKEY SECRET_ONENET_PRODUCTKEY
// 设备ID
#define DEVICEID "ESP32S3"
// 设备密钥
#define DEVICEKEY SECRET_ONENET_DEVICEKEY
// URL(mqtt)
#define ONENET "mqtt://mqtts.heclouds.com"
// 端口
#define PORT 1883
// 物模型属性上报主题
#define ONENET_PROP_POST "$sys/1SPYOdVhCv/ESP32S3/thing/property/post"

/* CAN 总线最新数据（用于上报物模型，字段与平台属性标识符一一对应） */
typedef struct {
  float speed;      /* 车速 km/h   (0x100) */
  float rpm;        /* 电机转速 rpm  (0x100) */
  float roll;       /* 横滚角 °    (0x100) */
  float pitch;      /* 俯仰角 °    (0x100) */
  float distance;   /* 障碍距离 cm  (0x102) */
  int temperature;  /* 环境温度 °C  (0x200，int32) */
  float humidity;   /* 环境湿度 %RH (0x200) */
  int light;        /* 光照强度 %   (0x200，int32) */
  int uptime;       /* 运行时长 s   (0x201) */
} onenet_can_data_t;

esp_err_t mqtt_client_init(void);
/* 线程安全地更新待上报的 CAN 数据（Can_task 调用） */
void mqtt_update_can_data(const onenet_can_data_t* data);
/* MQTT 是否已连接（供 UI 状态灯使用） */
bool mqtt_is_connected(void);

