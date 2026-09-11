#include <stdbool.h>
#include "driver/gptimer_etm.h"
#include "driver/gptimer_types.h"
#include "driver/ledc.h"

/* PWM 最大占空比（5bit 分辨率满量程）与开机默认亮度，供 UI 共用 */
#define LED_PWM_MAX_DUTY 31U
#define LED_PWM_DEFAULT_DUTY 15U

esp_err_t setLED(uint32_t duty);
uint32_t getLED(void);
esp_err_t setTurnSignals(bool left_on, bool right_on);
