#ifndef __SSD1306_H__
#define __SSD1306_H__

#include "main.h"

/*==========================================================================
 * SSD1306 OLED 128x64 驱动 (软件 I2C 位翻转)
 *--------------------------------------------------------------------------
 * 引脚 : SDA = PA7 (GPIO 开漏), SCK = PA8 (GPIO 开漏)
 * 地址 : 0x3C (SA0=0) 或 0x3D (SA0=1), 默认 0x3C
 * 说明 : 通过 GPIO 开漏 + 位翻转模拟 I2C, 不占用硬件 I2C。
 *========================================================================*/

#define SSD1306_I2C_ADDR  0x3C  /* 7 位地址 */
#define SSD1306_WIDTH     128
#define SSD1306_HEIGHT    64

/* 初始化并点亮 OLED, 返回 0=成功, 非0=总线 NACK(检查接线/地址) */
uint8_t SSD1306_Init(void);

/* 总线空闲电平自检: 1=高, 0=低(异常, 检查接线/供电) */
void SSD1306_BusIdleCheck(uint8_t *sda_high, uint8_t *scl_high);

/* 清屏 (全灭) */
void SSD1306_Clear(void);

/* 更新整屏缓冲到 OLED (SSD1306 需缓冲/寻址) */
void SSD1306_UpdateScreen(void);

/* 在 (x, y) 显示单个 ASCII 字符, 6x8 字体 */
void SSD1306_DrawChar(uint8_t x, uint8_t y, char ch);

/* 在 (x, y) 显示字符串, 6x8 字体 */
void SSD1306_DrawString(uint8_t x, uint8_t y, const char *str);

#endif
