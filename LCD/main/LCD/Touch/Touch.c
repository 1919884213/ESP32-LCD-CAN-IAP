#include "Touch.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#define TAG "TOUCH"

static bool s_inited = false;

/* Full safe XPT2046 ADC range used for the vertical screen mapping. */
#define TOUCH_X_RAW_LEFT   3732
#define TOUCH_X_RAW_RIGHT   588
#define TOUCH_Y_RAW_TOP    3654
#define TOUCH_Y_RAW_BOTTOM  424
#define TOUCH_PRESS_THRESHOLD    80
#define TOUCH_RELEASE_THRESHOLD  40
#define TOUCH_RAW_LOG_ENABLE      0
#define TOUCH_SAMPLE_COUNT        5

/* XPT2046 控制字节（差分模式，省电，PENIRQ 使能） */
#define XPT_CMD_Z1   0xB1
#define XPT_CMD_Z2   0xB2
#define XPT_CMD_X    0x90
#define XPT_CMD_Y    0xD0

/* 校准参数：屏幕坐标 = ADC * 系数 + 偏移 */
static float s_cal_a = 1.0f;  /* x = raw_x * a + b */
static float s_cal_b = 0.0f;
static float s_cal_c = 1.0f;  /* y = raw_y * c + d */
static float s_cal_d = 0.0f;
static bool  s_calibrated = false;

static inline void delay_cycles(volatile int n) {
    while (n--) { __asm__ volatile("nop"); }
}

/* 软件 SPI：8bit 命令 + 1bit 空周期 + 12bit 数据 */
static uint16_t sw_spi_xfer(uint8_t cmd) {
    gpio_set_level(T_CS, 0);
    delay_cycles(20);

    uint16_t value = 0;

    for (int i = 7; i >= 0; i--) {
        gpio_set_level(T_CLK, 0);
        gpio_set_level(T_DIN, (cmd >> i) & 1);
        delay_cycles(8);
        gpio_set_level(T_CLK, 1);
        delay_cycles(8);
    }

    gpio_set_level(T_CLK, 0);
    delay_cycles(8);
    gpio_set_level(T_CLK, 1);
    delay_cycles(8);

    for (int i = 11; i >= 0; i--) {
        gpio_set_level(T_CLK, 0);
        delay_cycles(8);
        value |= (gpio_get_level(T_DO) & 1) << i;
        gpio_set_level(T_CLK, 1);
        delay_cycles(8);
    }

    gpio_set_level(T_CS, 1);
    return value;
}

static uint16_t touch_read_axis(uint8_t cmd) {
    uint16_t samples[TOUCH_SAMPLE_COUNT];

    for (int i = 0; i < TOUCH_SAMPLE_COUNT; ++i) {
        sw_spi_xfer(cmd);
        samples[i] = sw_spi_xfer(cmd);
    }

    for (int i = 1; i < TOUCH_SAMPLE_COUNT; ++i) {
        uint16_t value = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > value) {
            samples[j + 1] = samples[j];
            --j;
        }
        samples[j + 1] = value;
    }

    return samples[TOUCH_SAMPLE_COUNT / 2];
}

void Touch_Init(void) {
    if (s_inited) return;

    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << T_CLK) | (1ULL << T_DIN) | (1ULL << T_CS),
        .mode         = GPIO_MODE_OUTPUT,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);

    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << T_DO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    gpio_set_level(T_CS, 1);
    gpio_set_level(T_CLK, 0);
    s_inited = true;
    ESP_LOGI(TAG, "XPT2046 touch init done (software SPI)");

    /* 应用实测校准系数（基于 320x240 横屏，3点实测数据） */
    Touch_SetCalibration(
        -(float)(TOUCH_X_MAX - 1) /
            (TOUCH_X_RAW_LEFT - TOUCH_X_RAW_RIGHT),
        (float)(TOUCH_X_MAX - 1) +
            (float)(TOUCH_X_MAX - 1) * TOUCH_X_RAW_RIGHT /
                (TOUCH_X_RAW_LEFT - TOUCH_X_RAW_RIGHT),
        -(float)(TOUCH_Y_MAX - 1) /
            (TOUCH_Y_RAW_TOP - TOUCH_Y_RAW_BOTTOM),
        (float)(TOUCH_Y_MAX - 1) +
            (float)(TOUCH_Y_MAX - 1) * TOUCH_Y_RAW_BOTTOM /
                (TOUCH_Y_RAW_TOP - TOUCH_Y_RAW_BOTTOM));
}

/* 读取原始 ADC 值（12bit, 0-4095）
 * 使用滞回防止抖动，只在按下瞬间打印一次 */
static bool s_pressed = false;
static bool s_logged = false;

bool Touch_ReadRaw(uint16_t *raw_x, uint16_t *raw_y) {
    if (!s_inited) return false;

    uint16_t z1 = sw_spi_xfer(XPT_CMD_Z1);

    /* 滞回：按下阈值高，松开阈值低（降低阈值使边角更灵敏） */
    if (!s_pressed && z1 > TOUCH_PRESS_THRESHOLD) {
        s_pressed = true;
        s_logged = false;
    } else if (s_pressed && z1 < TOUCH_RELEASE_THRESHOLD) {
        s_pressed = false;
    }

    if (!s_pressed) {
        if (z1 < 50) s_logged = false;  /* 重置日志标志 */
        return false;
    }

    /* 读取 X/Y */
    *raw_x = touch_read_axis(XPT_CMD_X);
    *raw_y = touch_read_axis(XPT_CMD_Y);

    /* 只在按下瞬间打印一次 */
#if TOUCH_RAW_LOG_ENABLE
    if (!s_logged) {
        ESP_LOGI(TAG, "touch raw_adc=(%u, %u), pressure=%u",
                 *raw_x, *raw_y, z1);
        s_logged = true;
    }
#endif

    return true;
}

/* 应用校准参数，输出屏幕坐标
 * 无状态：只报告当前帧是否按下，去抖交给 LVGL */
bool Touch_Read(uint16_t *x, uint16_t *y) {
    uint16_t rx, ry;
    if (!Touch_ReadRaw(&rx, &ry)) return false;

    int32_t sx, sy;
    if (s_calibrated) {
        sx = (int32_t)(rx * s_cal_a + s_cal_b + 0.5f);
        sy = (int32_t)(ry * s_cal_c + s_cal_d + 0.5f);
    } else {
        uint32_t x_adc = (rx > 3900) ? 3900 : rx;
        uint32_t y_adc = (ry > 3900) ? 3900 : ry;
        if (x_adc < 200) x_adc = 200;
        if (y_adc < 200) y_adc = 200;
        sx = TOUCH_X_MAX - 1 - (int32_t)((x_adc - 200) * TOUCH_X_MAX / (3900 - 200));
        sy = TOUCH_Y_MAX - 1 - (int32_t)((y_adc - 200) * TOUCH_Y_MAX / (3900 - 200));
    }

    /* 钳制到屏幕范围 */
    if (sx < 0) sx = 0;
    if (sx > TOUCH_X_MAX - 1) sx = TOUCH_X_MAX - 1;
    if (sy < 0) sy = 0;
    if (sy > TOUCH_Y_MAX - 1) sy = TOUCH_Y_MAX - 1;

    *x = (uint16_t)sx;
    *y = (uint16_t)sy;
    return true;
}

void Touch_SetCalibration(float a, float b, float c, float d) {
    s_cal_a = a; s_cal_b = b;
    s_cal_c = c; s_cal_d = d;
    s_calibrated = true;
    ESP_LOGI(TAG, "calibration applied: x=%.4f*rx%+.1f  y=%.4f*ry%+.1f", a, b, c, d);
}
