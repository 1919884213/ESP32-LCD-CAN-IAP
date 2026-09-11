/**
 * @file    main.c
 * @brief   ESP32 智能仪表主程序入口
 *
 * 系统功能概述：
 *  1. 外设初始化：W25Q64 SPI Flash、LCD 显示屏（旋转 90°）、LVGL 图形库、
 *     触摸、WiFi、LED（PWM）；
 *  2. 创建三个 FreeRTOS 任务（绑定不同核心以分散负载）：
 *     - Can_task  （核心 0，优先级 20）：接收并解析 CAN/TWAI 报文，
 *       将 IMU、车速、距离、环境等数据更新到 UI，并同步给 MQTT 上报；
 *     - lvgl_task （核心 1，优先级 5，栈 16KB）：周期驱动 LVGL 界面刷新；
 *     - Time_task （核心 0，优先级 5）：WiFi 连接后启动 SNTP 对时，
 *       并周期性把系统时间同步到 UI 显示。
 */

#include <inttypes.h>
#include <stdlib.h>
#include "Can.h"
#include "Can/Can.h"
#include "LCD/Display/Display.h"
#include "LCD/Timer/Timer.h"
#include "LCD/Touch/Touch.h"
#include "LED.h"
#include "LVGL/lv_port.h"
#include "UI/include/ui.h"
#include "W25Q64.h"
#include "WiFi.h"
#include "now.h"
#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ONENET/onenet.h"


/* 前向声明：三个 FreeRTOS 任务的入口函数 */
static void lvgl_task(void* arg);   /* LVGL 界面刷新任务   */
static void Can_task(void* arg);    /* CAN 报文接收解析任务 */
static void Time_task(void* arg);   /* SNTP 对时任务       */

/**
 * @brief  应用程序入口（ESP-IDF）
 *
 * 初始化顺序：W25Q64 Flash -> 显示屏 -> LVGL -> WiFi/UI -> LED -> 任务创建
 */
void app_main(void) {
  uint32_t w25q64_id = 0;
  /* 初始化 W25Q64 SPI Flash 并读取 JEDEC ID，用于确认 Flash 芯片通信正常 */
  esp_err_t w25q64_err = W25Q64_Init();
  if (w25q64_err == ESP_OK) {
    w25q64_err = W25Q64_ReadJedecId(&w25q64_id);
  }
  if (w25q64_err == ESP_OK) {
    ESP_LOGI("W25Q64", "JEDEC ID: 0x%06" PRIX32, w25q64_id);
  } else {
    ESP_LOGE("W25Q64", "JEDEC ID read failed: %s", esp_err_to_name(w25q64_err));
  }

  /* 初始化并配置显示屏（旋转 90°） */
  Display_Init();
  Display_SetDirection(DISP_DIR_90);
  /* 初始化 LVGL 及其底层显示/触摸驱动，并绑定系统时基 */
  lv_init();
  lv_port_init();
  lv_tick_set_cb(Timer_GetMs);

  /* 初始化 WiFi、UI 共享数据（含互斥量）与界面 */
  wifi_init();
  ui_data_init();
  ui_init();



  /* 使用中间占空比启动，便于确认 GPIO4 的 PWM 输出和 LED 接线。 */
  esp_err_t led_err = setLED(15);
  if (led_err != ESP_OK) {
    ESP_LOGE("LED", "setLED(15) failed: %s", esp_err_to_name(led_err));
  }

  /* 创建任务：CAN 处理固定在核心 0，LVGL 刷新固定在核心 1，网络对时固定在核心 0 */
  xTaskCreatePinnedToCore(Can_task, "Can", 4096, NULL, 20, NULL, 0);
  xTaskCreatePinnedToCore(lvgl_task, "lvgl", 16 * 1024, NULL, 5, NULL, 1);
  xTaskCreatePinnedToCore(Time_task, "time", 4096, NULL, 5, NULL, 0);
}

/* LVGL 任务：周期性刷新 UI，并限制延迟以让出 CPU 给 IDLE/WDT */
static void lvgl_task(void* arg) {
  TickType_t last_ui_update = xTaskGetTickCount();
  while (1) {
    TickType_t now = xTaskGetTickCount();
    if ((now - last_ui_update) >= pdMS_TO_TICKS(100)) {
      ui_tick();  // 核心语句
      last_ui_update = now;
    }
    uint32_t ms = lv_timer_handler();
    uint32_t delay_ms = ms < 5 ? 5 : ms;
    if (delay_ms > 20)
      delay_ms = 20;
    /* FREERTOS_HZ=100 时 pdMS_TO_TICKS(<10ms) 会取整成 0：vTaskDelay(0) 不让出
     * CPU，LVGL 任务会空转、饿死 IDLE 并触发任务看门狗。至少让出 1 个 tick。 */
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    if (ticks == 0)
      ticks = 1;
    vTaskDelay(ticks);
  }
}

/* 对时任务：等待 WiFi 连接后启动 SNTP，之后周期性把系统时间同步到 UI */
static void Time_task(void* arg) {
  bool sntp_started = false;
  while (1) {
    if (!sntp_started) {
      if (wifi_get_status() == WIFI_STATUS_CONNECTED) {
        now_sync_start();
        sntp_started = true;
        ESP_LOGI("TIME", "WiFi connected, SNTP sync started");
      }
    } else {
      uint16_t year = 0;
      uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
      if (now_get_datetime(&year, &month, &day, &hour, &minute, &second)) {
        ui_set_datetime(year, month, day, hour, minute, second);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
/* CAN 任务：初始化总线后循环接收报文，解析并更新对应 UI 数据 */
static void Can_task(void* arg) {
  if (!Can_init()) {
    ESP_LOGE("CAN", "CAN task stopped because TWAI initialization failed");
    vTaskDelete(NULL);
    return;
  }
  onenet_can_data_t report = {0};
  while (1) {
    /* IAP 期间由 IAP task 独占接收，避免抢走 0x610/0x611 ACK。 */
    if (Can_iap_running()) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    twai_message_t message = {0};
    /* 阻塞等待接收一帧 CAN 报文，超时 100ms 后继续循环 */
    if (!Can_receive(&message, pdMS_TO_TICKS(100)))
      continue;
    /* 过滤长度不足 8 字节的短帧（协议约定数据帧固定 8 字节） */
    if (message.data_length_code < 8U) {
      ESP_LOGI("CAN", "RX id=0x%03" PRIX32 " dlc=%u (short frame)",
               message.identifier, message.data_length_code);
      continue;
    }
    ESP_LOGI("CAN",
             "RX id=0x%03" PRIX32
             " dlc=%u data=%02X %02X %02X %02X %02X %02X %02X %02X",
             message.identifier, message.data_length_code, message.data[0],
             message.data[1], message.data[2], message.data[3], message.data[4],
             message.data[5], message.data[6], message.data[7]);
    /* 只处理标准数据帧，忽略扩展帧和远程帧 */
    if (message.extd || message.rtr)
      continue;
    /* ---- 0x101：IMU 加速度报文 ----
     * data[0..1]=X 轴加速度(mg, int16 小端)  data[2..3]=Y 轴
     * data[4..5]=Z 轴                        data[6]=状态位
     * data[7]=计数器
     * bit2 (0x04) 置位时表示加速度数据有效，更新到 IMU 界面 */
    if (message.identifier == 0x101U) {
      const int16_t ax = (int16_t)((uint16_t)message.data[0] |
                                   ((uint16_t)message.data[1] << 8));
      const int16_t ay = (int16_t)((uint16_t)message.data[2] |
                                   ((uint16_t)message.data[3] << 8));
      const int16_t az = (int16_t)((uint16_t)message.data[4] |
                                   ((uint16_t)message.data[5] << 8));
      const uint8_t state = message.data[6];
      ESP_LOGI("CAN", "MOTION ACCEL x=%d y=%d z=%d mg state=0x%02X cnt=%u", ax,
               ay, az, state, message.data[7]);
      if ((state & 0x04U) != 0U) {
        ui_imu_data_t imu = {ax, ay, az, 0, 0, 0};
        ui_set_imu_data(&imu);
      }
    /* ---- 0x100：运动状态报文 ----
     * data[0..1]=车速(0.01km/h, uint16 小端) data[2..3]=转速(rpm)
     * data[4..5]=横滚角 roll(0.01°, int16)   data[6..7]=俯仰角 pitch(0.01°)
     * 更新 UI 速度显示，并写入 MQTT 上报缓存 */
    } else if (message.identifier == 0x100U) {
      const uint16_t speed_raw =
          (uint16_t)message.data[0] | ((uint16_t)message.data[1] << 8);
      const uint16_t rpm =
          (uint16_t)message.data[2] | ((uint16_t)message.data[3] << 8);
      const int16_t roll = (int16_t)((uint16_t)message.data[4] |
                                     ((uint16_t)message.data[5] << 8));
      const int16_t pitch = (int16_t)((uint16_t)message.data[6] |
                                      ((uint16_t)message.data[7] << 8));
      ESP_LOGI(
          "CAN",
          "MOTION STATE speed=%u.%02u km/h rpm=%u roll=%d.%02d pitch=%d.%02d",
          speed_raw / 100U, speed_raw % 100U, rpm, roll / 100,
          abs((int)roll % 100), pitch / 100, abs((int)pitch % 100));
      ui_set_speed_kph(speed_raw / 100.0f);
      report.speed = speed_raw / 100.0f;
      report.rpm = (float)rpm;
      report.roll = roll / 100.0f;
      report.pitch = pitch / 100.0f;
      mqtt_update_can_data(&report);
    /* ---- 0x102：超声波测距报文 ----
     * data[0..3]=距离(0.1mm, int32 小端)  data[4]=有效标志(0 无效)
     * data[5]=传感器忙标志
     * 有效时换算为 cm 更新 UI，并写入 MQTT 上报缓存 */
    } else if (message.identifier == 0x102U) {
      const uint8_t valid = message.data[4];
      if (valid == 0)
        continue;
      int32_t dist = 0;
      for (int i = 0; i < 4; i++) {
        dist |= (int32_t)((uint32_t)message.data[i] << (i * 8));
      }
      ESP_LOGI("CAN", "DIST distance=%d.%d cm busy=%u", dist / 10,
               abs(dist % 10), message.data[5]);
      ui_set_distance_cm((uint16_t)(dist / 10));
      report.distance = dist / 10.0f;
      mqtt_update_can_data(&report);
    /* ---- 0x110：轮速/里程/霍尔报文（当前仅记录日志） ----
     * data[0..1]=轮速  data[2..5]=里程(小端)  data[6..7]=霍尔值 */
    } else if (message.identifier == 0x110U) {
      const uint16_t wheel_speed =
          (uint16_t)message.data[0] | ((uint16_t)message.data[1] << 8);
      const uint32_t odometer =
          (uint32_t)message.data[2] | ((uint32_t)message.data[3] << 8) |
          ((uint32_t)message.data[4] << 16) | ((uint32_t)message.data[5] << 24);
      const uint16_t hall =
          (uint16_t)message.data[6] | ((uint16_t)message.data[7] << 8);
      ESP_LOGI("CAN", "WHEEL speed=%u odometer=%" PRIu32 " hall=%u",
               wheel_speed, odometer, hall);
    /* ---- 0x200：环境传感器报文 ----
     * data[0..1]=温度(0.01°C, int16)  data[2..3]=湿度(0.01%RH)
     * data[4..5]=光照 ADC 电压(mV)    data[6]=状态位
     * 状态低两位全 1 且各字段非错误值(0x8000/0xFFFF)时数据才有效；
     * 光照电压按 3300mV 满量程换算为百分比后取反（越亮百分比越小，反转为越亮越大） */
    } else if (message.identifier == 0x200U) {
      const int16_t temp_raw = (int16_t)((uint16_t)message.data[0] |
                                         ((uint16_t)message.data[1] << 8));
      const uint16_t humidity_raw =
          (uint16_t)message.data[2] | ((uint16_t)message.data[3] << 8);
      const uint16_t light_mv =
          (uint16_t)message.data[4] | ((uint16_t)message.data[5] << 8);
      const uint8_t status = message.data[6];
      if ((status & 0x03U) == 0x03U && temp_raw != (int16_t)0x8000 &&
          humidity_raw != 0xFFFFU && light_mv != 0xFFFFU) {
        uint8_t light_percent = (uint32_t)light_mv * 100U / 3300U;
        if (light_percent > 100U)
          light_percent = 100U;
        light_percent = (uint8_t)(100U - light_percent);
        ui_set_environment_data((float)temp_raw / 100.0f,
                                (float)humidity_raw / 100.0f, light_percent);
        report.temperature = (int)(temp_raw / 100.0f);
        report.humidity = humidity_raw / 100.0f;
        report.light = light_percent;
        mqtt_update_can_data(&report);
      }
    /* ---- 0x201：环境节点诊断报文 ----
     * data[0]=DHT 错误计数  data[1]=ADC 错误计数  data[2]=接收错误计数
     * data[3]=发送错误计数  data[4]=节点状态      data[5]=版本
     * data[6..7]=运行时长 uptime（uint16 小端，写入 MQTT 上报缓存） */
    } else if (message.identifier == 0x201U) {
      ESP_LOGI("CAN",
               "ENV DIAG dht_err=%u adc_err=%u rx_err=%u tx_err=%u state=%u "
               "version=%u uptime=%u",
               message.data[0], message.data[1], message.data[2],
               message.data[3], message.data[4], message.data[5],
               (unsigned)((uint16_t)message.data[6] |
                          ((uint16_t)message.data[7] << 8)));
      report.uptime = (int)((uint16_t)message.data[6] |
                            ((uint16_t)message.data[7] << 8));
      mqtt_update_can_data(&report);
    /* ---- 0x202：保留/调试报文，仅打印原始数据 ---- */
    }else if(message.identifier == 0x202U){
          ESP_LOGI("CAN",
             "RX id=0x%03" PRIX32
             " dlc=%u data=%02X %02X %02X %02X %02X %02X %02X %02X",
             message.identifier, message.data_length_code, message.data[0],
             message.data[1], message.data[2], message.data[3], message.data[4],
             message.data[5], message.data[6], message.data[7]);
    } 
    /* ---- 0x610 / 0x611：IAP 固件升级应答帧（仅打印，正常由 IAP 任务处理） ----
     * data[0]=命令字  data[1]=状态  data[2]=会话
     * data[3..4]=块序号（uint16 小端） */
    else if (message.identifier == 0x610U || message.identifier == 0x611U) {
      ESP_LOGI("CAN",
               "IAP ACK id=0x%03" PRIX32
               " cmd=0x%02X status=0x%02X session=0x%02X block=%u",
               message.identifier, message.data[0], message.data[1],
               message.data[2],
               (unsigned)((uint16_t)message.data[3] |
                          ((uint16_t)message.data[4] << 8)));
    }
  }
}
