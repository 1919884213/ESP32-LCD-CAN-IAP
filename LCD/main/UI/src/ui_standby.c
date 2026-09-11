#include "ui_standby.h"
#include "LED.h"
#include "ui_data.h"
#include "../asset/lvgl_standby_320x240.h"
#include <stdint.h>

#define STANDBY_INACTIVE_MS 5000U /* 无触摸进入待机的时限 */
#define STANDBY_LED_DUTY 5U       /* 待机时面板灯降低到该占空比 */

static lv_obj_t *s_standby_layer;
static lv_obj_t *s_time_label;
static lv_obj_t *s_date_label;
static bool s_standby_on;
static uint32_t s_saved_duty;       /* 进入待机前的面板灯占空比 */
static uint32_t s_timeout_ms = STANDBY_INACTIVE_MS; /* 息屏时限，0=禁用自动息屏 */

void ui_standby_set_timeout_sec(uint32_t sec)
{
    s_timeout_ms = sec * 1000U;
}

/* 进入待机：记住当前占空比，降低亮度，显示只含 SNTP 时间的遮罩层 */
static void standby_enter(void)
{
    if (s_standby_on) return;
    s_standby_on = true;
    s_saved_duty = getLED();
    setLED(STANDBY_LED_DUTY);
    lv_obj_clear_flag(s_standby_layer, LV_OBJ_FLAG_HIDDEN);
}

/* 唤醒：恢复进入前的占空比并隐藏遮罩层 */
static void standby_exit(void)
{
    if (!s_standby_on) return;
    s_standby_on = false;
    lv_obj_add_flag(s_standby_layer, LV_OBJ_FLAG_HIDDEN);
    setLED(s_saved_duty);
}

/* 软件定时器回调：按 LVGL 输入不活动时间切换待机/唤醒，
 * 并刷新待机页的 SNTP 日期时间。 */
static void standby_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const uint32_t inactive_ms = lv_display_get_inactive_time(NULL);

    if (s_timeout_ms == 0U) {
        /* 息屏已禁用：只负责唤醒与刷新时间 */
        if (s_standby_on) {
            standby_exit();
        }
    } else if (s_standby_on) {
        if (inactive_ms < s_timeout_ms) {
            standby_exit();
        }
    } else if (inactive_ms >= s_timeout_ms) {
        standby_enter();
    }

    if (!s_standby_on) return;

    ui_drive_data_t drive;
    ui_get_drive_data(&drive);
    lv_label_set_text_fmt(s_time_label, "%02u:%02u:%02u", drive.hour,
                          drive.minute, drive.second);
    if (drive.year == 0U) {
        lv_label_set_text(s_date_label, "----/--/--");
    } else {
        lv_label_set_text_fmt(s_date_label, "%04u/%02u/%02u", drive.year,
                              drive.month, drive.day);
    }
}

void ui_standby_init(void)
{
    /* 顶层遮罩：全屏黑底，位于 TabView 之上，初始隐藏 */
    s_standby_layer = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_standby_layer, 320, 240);
    lv_obj_set_pos(s_standby_layer, 0, 0);
    lv_obj_set_style_bg_color(s_standby_layer, lv_color_black(),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_standby_layer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_standby_layer, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_standby_layer, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_standby_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_standby_layer, LV_OBJ_FLAG_HIDDEN);

    /* 待机背景图：铺满整屏并置于最底层 */
    lv_obj_t *bg = lv_image_create(s_standby_layer);
    lv_image_set_src(bg, &lvgl_standby_320x240);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_size(bg, 320, 240);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(bg);

    s_time_label = lv_label_create(s_standby_layer);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xFFFFFF),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_36,
                               LV_PART_MAIN);
    lv_obj_center(s_time_label);
    lv_label_set_text(s_time_label, "00:00:00");

    s_date_label = lv_label_create(s_standby_layer);
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(0x8A9AA3),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_14,
                               LV_PART_MAIN);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 30);
    lv_label_set_text(s_date_label, "----/--/--");

    /* LVGL 软件定时器：周期检测触摸不活动时间 */
    lv_timer_create(standby_timer_cb, 200, NULL);
}
