#ifndef TOUCH_H
#define TOUCH_H

#include "driver/gpio.h"
#include <stdint.h>
#include <stdbool.h>

#define T_IRQ  GPIO_NUM_1
#define T_DO   GPIO_NUM_2
#define T_DIN  GPIO_NUM_42
#define T_CS   GPIO_NUM_41
#define T_CLK  GPIO_NUM_40

#define TOUCH_X_MAX  320
#define TOUCH_Y_MAX  240

void Touch_Init(void);

/* 读取原始 ADC 值（12bit, 0-4095），用于校准 */
bool Touch_ReadRaw(uint16_t *raw_x, uint16_t *raw_y);

/* 读取校准后的屏幕坐标 */
bool Touch_Read(uint16_t *x, uint16_t *y);

/* 设置校准参数：x = a*raw_x + b,  y = c*raw_y + d */
void Touch_SetCalibration(float a, float b, float c, float d);

#endif
