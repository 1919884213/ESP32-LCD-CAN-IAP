#ifndef DISPLAY_H
#define DISPLAY_H

#include "driver/gpio.h"
#include <stdint.h>

#define SCK GPIO_NUM_5
#define SDI GPIO_NUM_6
#define DC GPIO_NUM_7
#define RESET GPIO_NUM_15
#define CS GPIO_NUM_16
#define LED GPIO_NUM_4

/* 显示方向（写入 0x36 寄存器，BGR=1）
 * 位定义：bit7 MY(上下) bit6 MX(左右) bit5 MV(行列交换) bit3 BGR
 */
typedef enum {
  DISP_DIR_0   = 0x48,  /* MY+BGR                  竖屏 240x320 */
  DISP_DIR_90  = 0x68,  /* MX+MV+BGR   横屏 320x240，修正左右镜像 */
  DISP_DIR_180 = 0x88,  /* MX+BGR                  竖屏 240x320 上下翻转 */
  DISP_DIR_270 = 0xE8,  /* MY+MX+MV+BGR 横屏 320x240 */
} Display_Dir_t;

void Display_Init(void);
void Display_SetDirection(Display_Dir_t dir);
uint16_t Display_GetWidth(void);
uint16_t Display_GetHeight(void);

void Display_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

#endif
