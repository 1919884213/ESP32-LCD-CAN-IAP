/**
 * @file    LED.c
 * @brief   LED 驱动：GPIO4 PWM 背光/呼吸灯 + GPIO17/18 转向灯
 *
 *  - GPIO4  通过 LEDC（LEDC_TIMER_1 / LEDC_CHANNEL_1，低速模式）
 *           输出 1kHz、5bit 分辨率（0~31 占空比）的 PWM 信号；
 *  - GPIO17/18 作为普通推挽输出，分别控制左/右转向灯（1=亮）。
 */

#include "LED.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TURN_LEFT_GPIO  GPIO_NUM_17       /* 左转向灯 GPIO */
#define TURN_RIGHT_GPIO GPIO_NUM_18       /* 右转向灯 GPIO */

static const char* TAG = "LED";

static bool     s_led_initialized;        /* LEDC PWM 是否已初始化（懒初始化标志） */
static bool     s_turn_initialized;       /* 转向灯 GPIO 是否已初始化 */
static uint32_t s_led_duty;               /* 当前 LED PWM 占空比（供 getLED 查询） */

/**
 * @brief  初始化转向灯 GPIO（仅初始化一次）
 * @return ESP_OK 成功；否则返回 GPIO 配置错误码
 */
static esp_err_t turn_signal_init(void) {
  if (s_turn_initialized) {               /* 已初始化则直接返回 */
    return ESP_OK;
  }
  esp_err_t err = gpio_set_direction(TURN_LEFT_GPIO, GPIO_MODE_OUTPUT);
  if (err != ESP_OK) {
    return err;
  }
  err = gpio_set_direction(TURN_RIGHT_GPIO, GPIO_MODE_OUTPUT);
  if (err != ESP_OK) {
    return err;
  }
  gpio_set_level(TURN_LEFT_GPIO, 0);      /* 默认两路转向灯均熄灭 */
  gpio_set_level(TURN_RIGHT_GPIO, 0);
  s_turn_initialized = true;
  ESP_LOGI(TAG, "turn signals ready: left=GPIO17 right=GPIO18");
  return ESP_OK;
}

/**
 * @brief  初始化 LEDC PWM 定时器与通道（GPIO4，1kHz，5bit 分辨率）
 * @return ESP_OK 成功；否则返回配置错误码
 */
static esp_err_t timer_init() {
  /* 先把 GPIO4 配置为输出，避免 PWM 启动前引脚悬空 */
  esp_err_t err = gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "GPIO4 output config failed: %s", esp_err_to_name(err));
    return err;
  }

  /* 配置 LEDC 定时器：低速模式、自动选择时钟源、5bit 分辨率、1kHz */
  ledc_timer_config_t timer_cfg = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                   .clk_cfg = LEDC_AUTO_CLK,
                                   .deconfigure = false,
                                   .duty_resolution = LEDC_TIMER_5_BIT,
                                   .freq_hz = 1000,
                                   .timer_num = LEDC_TIMER_1};
  err = ledc_timer_config(&timer_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
    return err;
  }
  /* 配置 LEDC 通道：通道 1 绑定 GPIO4，初始占空比为满量程（全亮） */
  ledc_channel_config_t channle_cfg = {.channel = LEDC_CHANNEL_1,
                                       .duty = LED_PWM_MAX_DUTY,
                                       .gpio_num = GPIO_NUM_4,
                                       .speed_mode = LEDC_LOW_SPEED_MODE,
                                       .timer_sel = LEDC_TIMER_1};
  err = ledc_channel_config(&channle_cfg);
  if (err == ESP_OK) {
    s_led_initialized = true;
    ESP_LOGI(TAG, "LEDC ready: GPIO4, 1kHz, duty resolution=5bit");
  } else {
    ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
  }

  return err;
}

/**
 * @brief  设置 LED PWM 占空比（未初始化时会自动懒初始化）
 * @param  duty 目标占空比 0~31，超出范围自动截断为 31
 * @return ESP_OK 成功；否则返回 LEDC 错误码
 */
esp_err_t setLED(uint32_t duty) {
  if (!s_led_initialized) {               /* 首次调用时自动初始化 PWM */
    esp_err_t err = timer_init();
    if (err != ESP_OK) {
      return err;
    }
  }
  if (duty > LED_PWM_MAX_DUTY) {          /* 占空比限幅 */
    duty = LED_PWM_MAX_DUTY;
  }
  /* 设置占空比后必须调用 update_duty 才会真正生效 */
  esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "set duty %lu failed: %s", (unsigned long)duty,
             esp_err_to_name(err));
    return err;
  }
  err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "update duty %lu failed: %s", (unsigned long)duty,
             esp_err_to_name(err));
  } else {
    s_led_duty = duty;                    /* 记录当前占空比供查询 */
    ESP_LOGI(TAG, "GPIO4 duty=%lu/31", (unsigned long)duty);
  }
  return err;
}

/**
 * @brief  获取当前 LED PWM 占空比（0~31）
 */
uint32_t getLED(void) {
  return s_led_duty;
}

/**
 * @brief  设置左右转向灯状态（内部自动完成 GPIO 初始化）
 * @param  left_on  true=左转灯亮
 * @param  right_on true=右转灯亮
 * @return ESP_OK 成功；否则返回初始化错误码
 */
esp_err_t setTurnSignals(bool left_on, bool right_on) {
  esp_err_t err = turn_signal_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "turn signal init failed: %s", esp_err_to_name(err));
    return err;
  }
  gpio_set_level(TURN_LEFT_GPIO, left_on ? 1 : 0);
  gpio_set_level(TURN_RIGHT_GPIO, right_on ? 1 : 0);
  return ESP_OK;
}
