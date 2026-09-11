/* PinF_font.c —— LVGL 生成的 PinF.c 的包装编译单元。
 *
 * 为什么需要它：
 *  1) lv_font_conv 生成的位图/描述符都是 static，外部（ui_helpers.c）拿不到，
 *     无法把字形位图拷贝到 PSRAM；
 *  2) 生成文件在 PinF.fallback 里引用了本工程不存在的 lv_font_PinF。
 * 这里把生成文件纳入同一编译单元导出位图，并把遗留的 lv_font_PinF 指到补充字体，
 * 以后重新生成 PinF.c 都不用再手工改生成文件。
 *
 * 注意：CMake 只编译本文件，不要再单独编译 PinF.c（否则符号重复）。 */
#include "lvgl.h"

extern const lv_font_t font_cn_supplement_14;

/* 生成器把 fallback 写成 &lv_font_PinF，用宏指到真实字体让链接通过 */
#define lv_font_PinF font_cn_supplement_14
#include "PinF.c"
#undef lv_font_PinF

/* 供 ui_helpers.c 把字形位图拷贝到 PSRAM */
const uint8_t *const PinF_glyph_bitmap = glyph_bitmap;
const uint32_t PinF_bitmap_size = sizeof(glyph_bitmap);
