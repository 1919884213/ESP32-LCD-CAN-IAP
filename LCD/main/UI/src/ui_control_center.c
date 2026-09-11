/* 控制中心：主页面下滑呼出的手机式快捷面板。
 * - WiFi 开关：短按开/关射频，长按进入网络(WiFi)页面配置
 * - 自动亮度开关（转发给驾驶页逻辑）+ 面板亮度滑块
 * - 息屏时间下拉
 * 覆盖层挂在全屏顶层对象上：任意页面下滑都能呼出。
 * 亮度/自动亮度的"状态"仍由驾驶页模块持有，本文件只做控件与转发。 */
#include "ui_control_center.h"
#include "LED.h"
#include "ui.h"
#include "ui_screen_drive.h"
#include "ui_standby.h"
#include "WiFi.h"
#include "ui_helpers.h"
#include <stdbool.h>
#include <stdint.h>

/* 面板尺寸：320x240 屏，从顶部滑入 */
#define CC_WIDTH 320
#define CC_HEIGHT 176

static lv_obj_t *s_panel;        /* 面板根对象（顶层覆盖层） */
static lv_obj_t *s_scrim;        /* 面板背后的半透明遮罩 */
static lv_obj_t *s_wifi_switch;  /* WiFi 开关 */
static lv_obj_t *s_wifi_status;  /* WiFi 状态文字 */
static lv_obj_t *s_auto_switch;  /* 自动亮度开关 */
static lv_obj_t *s_dimmer;       /* 亮度滑块 */
static lv_obj_t *s_dimmer_label; /* 亮度百分比 */
static bool s_open;
static bool s_wifi_hold; /* 长按开关跳页时，抵消 release 引起的误翻转 */

/* ---------------- WiFi 开关 ---------------- */

/* 按射频开关与实际连接状态刷新文字 */
static void refresh_wifi_widgets(void)
{
    if (!wifi_is_enabled()) {
        lv_label_set_text(s_wifi_status, "已关闭");
        return;
    }
    switch (wifi_get_status()) {
        case WIFI_STATUS_IDLE:
            lv_label_set_text(s_wifi_status, "等待连接");
            break;
        case WIFI_STATUS_CONNECTING:
            lv_label_set_text(s_wifi_status, "连接中…");
            break;
        case WIFI_STATUS_CONNECTED:
            lv_label_set_text(s_wifi_status, "已连接");
            break;
        case WIFI_STATUS_FAILED:
            lv_label_set_text(s_wifi_status, "连接失败");
            break;
    }
}

static void wifi_switch_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

    /* 长按已跳去网络页：release 顺带翻转了开关，复原回去，不执行开关动作 */
    if (s_wifi_hold) {
        s_wifi_hold = false;
        lv_obj_set_state(s_wifi_switch, LV_STATE_CHECKED, wifi_is_enabled());
        return;
    }

    if (lv_obj_has_state(s_wifi_switch, LV_STATE_CHECKED)) {
        wifi_set_enabled(true);
        /* 优先用上次连过的凭据，否则回落到固件内置默认 WiFi */
        const char *ssid = NULL;
        const char *password = NULL;
        if (!wifi_get_last_credentials(&ssid, &password)) {
            ssid = WIFI_DEFAULT_SSID;
            password = WIFI_DEFAULT_PASSWORD;
        }
        if (!wifi_connect_request(ssid, password)) {
            lv_label_set_text(s_wifi_status, "请先配置");
            return;
        }
    } else {
        wifi_set_enabled(false);
    }
    refresh_wifi_widgets();
}
/* 长按 WiFi 开关：进入网络页面（配置账号密码） */
static void wifi_switch_long_press_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_LONG_PRESSED) return;
    s_wifi_hold = true;
    ui_control_center_close();
    lv_tabview_set_active(ui_TabView, UI_PAGE_WIFI, LV_ANIM_OFF);
}

/* ---------------- 自动亮度 / 面板亮度 ---------------- */

static void auto_switch_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    ui_drive_set_auto_brightness(
        lv_obj_has_state(s_auto_switch, LV_STATE_CHECKED));
}

static void dimmer_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    const uint32_t duty = (uint32_t)lv_slider_get_value(s_dimmer);
    /* 手动调光视为接管：自动亮度开着则关掉，避免 500ms 后被覆盖 */
    if (ui_drive_is_auto_brightness()) {
        ui_drive_set_auto_brightness(false);
        lv_obj_remove_state(s_auto_switch, LV_STATE_CHECKED);
    }
    setLED(duty);
    lv_label_set_text_fmt(s_dimmer_label, "%u%%",
                          (unsigned)((duty * 100U) / LED_PWM_MAX_DUTY));
}

/* 驾驶页自动亮度调整时反向同步滑块与百分比（含待机恢复、周期跟随） */
void ui_control_center_sync_brightness(uint32_t duty)
{
    if (s_dimmer == NULL) return;
    lv_slider_set_value(s_dimmer, (int32_t)duty, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_dimmer_label, "%u%%",
                          (unsigned)((duty * 100U) / LED_PWM_MAX_DUTY));
}

/* ---------------- 息屏时间 ---------------- */

static void standby_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    /* 选项顺序：5s / 15s / 30s / 60s / 不 */
    static const uint32_t timeout_table[] = {5U, 15U, 30U, 60U, 0U};
    const uint16_t index =
        lv_dropdown_get_selected(lv_event_get_target(event));
    if (index < sizeof(timeout_table) / sizeof(timeout_table[0])) {
        ui_standby_set_timeout_sec(timeout_table[index]);
    }
}

/* ---------------- 展开 / 收起 ---------------- */

void ui_control_center_open(void)
{
    if (s_open) return;
    s_open = true;

    /* 展开瞬间同步一次所有控件状态 */
    lv_obj_set_state(s_wifi_switch, LV_STATE_CHECKED, wifi_is_enabled());
    refresh_wifi_widgets();
    lv_obj_set_state(s_auto_switch, LV_STATE_CHECKED,
                     ui_drive_is_auto_brightness());
    ui_control_center_sync_brightness(getLED());

    lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_scrim);
    lv_obj_move_foreground(s_panel);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_panel);
    lv_anim_set_values(&anim, -CC_HEIGHT, 6);
    lv_anim_set_duration(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_start(&anim);
}

void ui_control_center_close(void)
{
    if (!s_open) return;
    s_open = false;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_panel);
    lv_anim_set_values(&anim, lv_obj_get_y(s_panel), -CC_HEIGHT);
    lv_anim_set_duration(&anim, 200);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_completed_cb(&anim, (lv_anim_completed_cb_t)NULL);
    lv_anim_start(&anim);

    /* 收起后立即隐藏：动画期间面板仍在滑出轨道上，允许露出 */
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
}

bool ui_control_center_is_open(void)
{
    return s_open;
}

/* 面板内上滑收起 */
static void panel_gesture_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) return;
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) {
        ui_control_center_close();
    }
}

/* 遮罩点击：收起 */
static void dismiss_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    ui_control_center_close();
}

/* ---------------- 构建 ---------------- */

/* 白色行卡片 */
static lv_obj_t *create_row(lv_obj_t *parent, lv_coord_t y, lv_coord_t h)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, CC_WIDTH - 24, h);
    lv_obj_set_pos(row, 12, y);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(0xDCE4E8), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    return row;
}

/* 开关统一样式 */
static void style_switch(lv_obj_t *sw)
{
    lv_obj_set_size(sw, 44, 24);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xC6D2D9), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x1677C8),
                              LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -3, LV_PART_KNOB);
}

void ui_control_center_init(void)
{
    lv_obj_t *top = lv_layer_top();

    /* 半透明遮罩：铺满整屏，点击空白收起 */
    s_scrim = lv_obj_create(top);
    lv_obj_set_size(s_scrim, 320, 240);
    lv_obj_set_pos(s_scrim, 0, 0);
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(s_scrim, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scrim, 80, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_scrim, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_scrim, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_scrim, dismiss_cb, LV_EVENT_CLICKED, NULL);

    /* 面板根 */
    s_panel = lv_obj_create(top);
    lv_obj_set_size(s_panel, CC_WIDTH, CC_HEIGHT);
    lv_obj_set_pos(s_panel, 0, -CC_HEIGHT);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0xF4F7F8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_panel, 18, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_panel, 70, LV_PART_MAIN);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_panel, panel_gesture_cb, LV_EVENT_GESTURE, NULL);

    /* 标题与把手 */
    lv_obj_t *header = ui_create_title(s_panel, "控制中心");
    lv_obj_set_pos(header, 14, 8);
    lv_obj_t *grip = lv_obj_create(s_panel);
    lv_obj_set_size(grip, 44, 4);
    lv_obj_align(grip, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_remove_flag(grip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(grip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(grip, lv_color_hex(0xB9C6CD), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(grip, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(grip, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(grip, dismiss_cb, LV_EVENT_CLICKED, NULL);

    /* ---- 第 1 行：WiFi（短按开关，长按进入网络页） ---- */
    lv_obj_t *wifi_row = create_row(s_panel, 34, 40);
    s_wifi_status = ui_create_caption(wifi_row, "WiFi 网络");
    lv_obj_set_pos(s_wifi_status, 12, 12);
    s_wifi_switch = lv_switch_create(wifi_row);
    style_switch(s_wifi_switch);
    lv_obj_align(s_wifi_switch, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(s_wifi_switch, wifi_switch_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_wifi_switch, wifi_switch_long_press_cb,
                        LV_EVENT_LONG_PRESSED, NULL);

    /* ---- 第 2 行：自动亮度 ---- */
    lv_obj_t *auto_row = create_row(s_panel, 80, 40);
    lv_obj_t *auto_label = ui_create_caption(auto_row, "自动亮度");
    lv_obj_set_pos(auto_label, 12, 12);
    lv_obj_t *auto_hint = ui_create_caption(auto_row, "跟随光照");
    lv_obj_set_style_text_color(auto_hint, lv_color_hex(0x9AAAB4),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(auto_hint, &lv_font_montserrat_10,
                               LV_PART_MAIN);
    lv_obj_set_pos(auto_hint, 80, 14);
    s_auto_switch = lv_switch_create(auto_row);
    style_switch(s_auto_switch);
    lv_obj_align(s_auto_switch, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_event_cb(s_auto_switch, auto_switch_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* ---- 第 3 行：面板亮度 + 息屏 ---- */
    lv_obj_t *dim_row = create_row(s_panel, 126, 40);
    lv_obj_t *dim_label = ui_create_caption(dim_row, "亮度");
    lv_obj_set_pos(dim_label, 12, 12);
    s_dimmer = lv_slider_create(dim_row);
    lv_obj_set_size(s_dimmer, 88, 6);
    lv_obj_set_pos(s_dimmer, 48, 17);
    lv_slider_set_range(s_dimmer, 0, LED_PWM_MAX_DUTY);
    lv_slider_set_value(s_dimmer, LED_PWM_DEFAULT_DUTY, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_dimmer, lv_color_hex(0xDCE6E9), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dimmer, lv_color_hex(0x1677C8),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_dimmer, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_width(s_dimmer, 10, LV_PART_KNOB);
    lv_obj_set_style_height(s_dimmer, 10, LV_PART_KNOB);
    lv_obj_add_event_cb(s_dimmer, dimmer_event_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);
    s_dimmer_label = ui_create_caption(dim_row, "--%");
    lv_obj_set_width(s_dimmer_label, 32);
    lv_obj_set_pos(s_dimmer_label, 142, 12);
    lv_obj_set_style_text_align(s_dimmer_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);

    lv_obj_t *standby_dd = lv_dropdown_create(dim_row);
    lv_dropdown_set_options(standby_dd,
                            "息屏5s\n息屏15s\n息屏30s\n息屏60s\n不息屏");
    lv_obj_set_size(standby_dd, 92, 22);
    lv_obj_align(standby_dd, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_dropdown_set_selected(standby_dd, 0);
    lv_obj_set_style_text_font(standby_dd, ui_font_zh_14(), LV_PART_MAIN);
    lv_obj_t *dd_list = lv_dropdown_get_list(standby_dd);
    if (dd_list != NULL) {
        lv_obj_set_style_text_font(dd_list, ui_font_zh_14(), LV_PART_MAIN);
    }
    lv_dropdown_set_symbol(standby_dd, LV_SYMBOL_DOWN);
    lv_obj_add_event_cb(standby_dd, standby_event_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);
}
