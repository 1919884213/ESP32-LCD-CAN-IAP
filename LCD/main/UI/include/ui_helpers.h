/* UI 辅助工具头文件：提供统一的样式与常用控件创建函数 */
#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "lvgl.h"

/* 中文补充子集字体（内置思源黑体 CJK 缺失字符，lv_font_conv 生成） */
LV_FONT_DECLARE(font_cn_supplement_14);
LV_FONT_DECLARE(font_cn_supplement_16);
/* 天气状况大字（24px，含 ASCII 与和风天气中文状况全集） */
LV_FONT_DECLARE(font_cn_weather_24);
/* 主中文字体（PinF，含 ASCII+常用中文，位图运行时拷贝到 PSRAM） */
LV_FONT_DECLARE(PinF);

/* 中文 14px 字体链：内置思源黑体 CJK 为基，缺失字回退到补充子集 */
const lv_font_t *ui_font_zh_14(void);
/* 中文 16px 字体链：内置思源黑体 CJK 为基，缺失字回退到补充子集 */
const lv_font_t *ui_font_zh_16(void);

/* 应用页面背景样式 */
void ui_apply_page_style(lv_obj_t *obj);
/* 应用卡片（带边框圆角）样式 */
void ui_apply_card_style(lv_obj_t *obj);
/* Apply the driving-console visual system to interactive controls. */
void ui_apply_primary_button_style(lv_obj_t *obj);
void ui_apply_secondary_button_style(lv_obj_t *obj);
void ui_apply_input_style(lv_obj_t *obj);
void ui_apply_progress_style(lv_obj_t *obj, lv_color_t indicator_color);
/* 创建标题（强调色） */
lv_obj_t *ui_create_title(lv_obj_t *parent, const char *text);
/* 创建说明文字（灰色） */
lv_obj_t *ui_create_caption(lv_obj_t *parent, const char *text);
/* 创建数值文字（白色，可指定字体） */
lv_obj_t *ui_create_value(lv_obj_t *parent, const char *text,
                          const lv_font_t *font);

/* 四个 STM32 逻辑槽固定对应的 bin 文件名（fw: 0=STM32_1 … 3=STM32_4）。 */
const char *ui_firmware_name(uint32_t fw);

#endif /* UI_HELPERS_H */
