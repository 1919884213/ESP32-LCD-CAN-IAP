#include "ui_helpers.h"

#include <string.h>

#include "esp_heap_caps.h"

/* UI 统一配色：背景、卡片、边框、说明、数值、强调色 */
#define UI_BG_COLOR       0xF4F7F8
#define UI_CARD_COLOR     0xFFFFFF
#define UI_BORDER_COLOR   0xD7E0E4
#define UI_CAPTION_COLOR  0x657784
#define UI_VALUE_COLOR    0x17212B
#define UI_ACCENT_COLOR   0x1677C8
#define UI_HEALTH_COLOR   0x0F9B8E
#define UI_INPUT_COLOR    0xFFFFFF
#define UI_TRACK_COLOR    0xE1E9EC

/* 主中文字体（PingFang）：包装文件 PinF_font.c 导出的位图指针与大小 */
extern const uint8_t *const PinF_glyph_bitmap;
extern const uint32_t PinF_bitmap_size;

/* 中文字体链：PingFang 为基底（拉丁+常用中文），缺失字回退到补充子集字体。
 * LVGL v9 通过 lv_font_t 的 fallback 字段接链，因此先结构体拷贝为可变字体
 * 再设置 fallback；位图按项目约定运行时拷贝到 PSRAM。 */
static lv_font_t s_font_zh_14;
static lv_font_fmt_txt_dsc_t s_pf_dsc_14;
static lv_font_t s_font_zh_16;
static bool s_fonts_ready;

static void ui_fonts_init(void)
{
    if (s_fonts_ready) {
        return;
    }
    s_font_zh_16 = lv_font_source_han_sans_sc_16_cjk;
    s_font_zh_16.fallback = &font_cn_supplement_16;

    /* 拷贝 PingFang 位图到 PSRAM（不实时，按项目约定放外部 RAM）；
     * PSRAM 不可用时回退到 flash 中的原位置。 */
    s_pf_dsc_14 = *(const lv_font_fmt_txt_dsc_t *)PinF.dsc;
    uint8_t *psram = heap_caps_malloc(PinF_bitmap_size, MALLOC_CAP_SPIRAM);
    if (psram != NULL) {
        memcpy(psram, PinF_glyph_bitmap, PinF_bitmap_size);
        s_pf_dsc_14.glyph_bitmap = psram;
    }
    s_font_zh_14 = PinF;
    s_font_zh_14.dsc = &s_pf_dsc_14;
    s_font_zh_14.fallback = &font_cn_supplement_14;
    s_fonts_ready = true;
}

const lv_font_t *ui_font_zh_14(void)
{
    ui_fonts_init();
    return &s_font_zh_14;
}

const lv_font_t *ui_font_zh_16(void)
{
    ui_fonts_init();
    return &s_font_zh_16;
}

/* 应用页面背景样式 */
void ui_apply_page_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

/* 应用浅色卡片样式：细边框、较大圆角与紧凑内边距。 */
void ui_apply_card_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_CARD_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(UI_BORDER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 7, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

void ui_apply_primary_button_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_ACCENT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 9, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x0F639F),
                              LV_PART_MAIN | LV_STATE_PRESSED);
}

void ui_apply_secondary_button_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xE5F0F7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 9, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(UI_ACCENT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xD4E6F2),
                              LV_PART_MAIN | LV_STATE_PRESSED);
}

void ui_apply_input_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_INPUT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(UI_BORDER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 9, LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(UI_VALUE_COLOR), LV_PART_MAIN);
}

void ui_apply_progress_style(lv_obj_t *obj, lv_color_t indicator_color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_TRACK_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, indicator_color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
}

/* 创建标题标签（强调色、16px） */
lv_obj_t *ui_create_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_ACCENT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ui_font_zh_16(), LV_PART_MAIN);
    return label;
}

/* 创建说明标签（灰色、14px） */
lv_obj_t *ui_create_caption(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_CAPTION_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ui_font_zh_14(), LV_PART_MAIN);
    return label;
}

/* 创建数值标签（白色、可指定字体） */
lv_obj_t *ui_create_value(lv_obj_t *parent, const char *text,
                          const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_VALUE_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    return label;
}

/* 四个 STM32 逻辑槽固定对应的 bin 文件名。 */
const char *ui_firmware_name(uint32_t fw)
{
    switch (fw) {
        case 0U:
            return "CanDevice1.bin";
        case 1U:
            return "CanDevice2.bin";
        case 2U:
            return "myCanDevice1.bin";
        default:
            return "myCanDevice2.bin";
    }
}
