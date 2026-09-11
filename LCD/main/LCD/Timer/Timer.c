#include "Timer.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

static gptimer_handle_t s_timer = NULL;

void Timer_Init(void) {
    gptimer_config_t cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,   /* 1MHz -> 1 tick = 1us */
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &s_timer));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));
    ESP_ERROR_CHECK(gptimer_start(s_timer));
}

uint64_t Timer_GetUs(void) {
    uint64_t cnt = 0;
    gptimer_get_raw_count(s_timer, &cnt);
    return cnt;
}

uint32_t Timer_GetMs(void) {
    return (uint32_t)(Timer_GetUs() / 1000ULL);
}

void Timer_DelayUs(uint32_t us) {
    uint64_t start = Timer_GetUs();
    while ((Timer_GetUs() - start) < us) {
        /* busy wait, 适用于 us 级短延时 */
    }
}

void Timer_DelayMs(uint32_t ms) {
    /* ms 级延时交给 RTOS，避免占用 CPU / 触发任务看门狗 */
    vTaskDelay(pdMS_TO_TICKS(ms));
}
