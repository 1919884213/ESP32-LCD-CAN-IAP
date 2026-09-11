#include "ui_screen_drive.h"
#include "ui_control_center.h"
#include "ui_data.h"
#include "ui_helpers.h"
#include "LED.h"
#include "WiFi.h"
#include "ONENET/onenet.h"
#include "ui_standby.h"
#include "../asset/BK2.h"
#include <stdbool.h>
#include <stdint.h>

/* 灯光图标的标识，对应灯光数据中的各个开关 */
typedef enum {
    LIGHT_ICON_LEFT = 0,
    LIGHT_ICON_RIGHT,
    LIGHT_ICON_HIGH,
    LIGHT_ICON_LOW,
} light_icon_id_t;

/* 驾驶页的控件指针：中央时钟、日期、灯光图标 */
static lv_obj_t *s_clock;
static lv_obj_t *s_date;
static lv_obj_t *s_status_leds[3];
static lv_obj_t *s_light_icons[4];
static bool s_auto_brightness_on; /* 自动亮度开关状态（控件在控制中心） */
static uint32_t s_turn_blink_last_ms;
static bool s_turn_blink_phase = true;

/* 自动亮度刷新间隔：光敏数据来自 CAN 0x200，500ms 跟随足够平滑 */
#define AUTO_BRIGHTNESS_PERIOD_MS 500U
#define STATUS_LED_SIZE 6
/* 右上角状态灯呼吸周期：一轮 0→100%→0 约 5 秒 */
#define STATUS_LED_BREATH_PERIOD_MS 5000U

/* 前向声明：开关接口开启自动亮度时需要立即调整一次 */
static void apply_auto_brightness(void);

/* 计算状态灯呼吸亮度（0~255）：以 5s 为周期做三角波，
 * 前半程 0→100%，后半程 100%→0。 */
static uint8_t status_led_breath_brightness(uint32_t now_ms)
{
    const uint32_t half = STATUS_LED_BREATH_PERIOD_MS / 2U;
    const uint32_t phase = now_ms % STATUS_LED_BREATH_PERIOD_MS;
    const uint32_t progress = phase < half ? phase
                                           : STATUS_LED_BREATH_PERIOD_MS - phase;
    const uint32_t percent = (progress * 100U) / half;
    return (uint8_t)((percent * 255U) / 100U);
}

/* 创建右上角固定状态灯。 */
static lv_obj_t *create_status_led(lv_obj_t *parent, lv_coord_t x,
                                   lv_color_t color)
{
    lv_obj_t *led = lv_led_create(parent);
    lv_obj_set_size(led, STATUS_LED_SIZE, STATUS_LED_SIZE);
    lv_obj_set_pos(led, x, 2);
    lv_obj_remove_flag(led, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(led, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(led, 2, LV_PART_MAIN);
    lv_led_set_color(led, color);
    lv_led_on(led);
    return led;
}

/* 灯光图标状态改变回调：把勾选状态写回灯光共享数据 */
static void light_icon_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;

    ui_lighting_data_t data;
    ui_get_lighting_data(&data);
    const bool enabled = lv_obj_has_state(lv_event_get_target(event),
                                          LV_STATE_CHECKED);
    switch ((light_icon_id_t)(intptr_t)lv_event_get_user_data(event)) {
        case LIGHT_ICON_LEFT:  data.left_turn = enabled; break;
        case LIGHT_ICON_RIGHT: data.right_turn = enabled; break;
        case LIGHT_ICON_HIGH:  data.high_beam = enabled; break;
        case LIGHT_ICON_LOW:   data.low_beam = enabled; break;
        default: return;
    }
    ui_set_lighting_data(&data);
}

/* 创建一个可勾选的灯光图标（符号 + 可选框内底部文字 + 位置 + 标识） */
static lv_obj_t *create_light_icon(lv_obj_t *parent, const char *symbol,
                                   const char *hint, lv_coord_t x, lv_coord_t y,
                                   lv_coord_t w, lv_coord_t h,
                                   light_icon_id_t id)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_size(icon, w, h);
    lv_obj_set_pos(icon, x, y);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(icon, lv_color_hex(0xD7E0E4), LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(icon, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon, lv_color_hex(0xE1F2EF),
                              LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x0F796F), LV_PART_MAIN);
    lv_obj_add_event_cb(icon, light_icon_event_cb, LV_EVENT_VALUE_CHANGED,
                        (void *)(intptr_t)id);

    if (symbol != NULL) {
        lv_obj_t *label = lv_label_create(icon);
        lv_label_set_text(label, symbol);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
        if (hint != NULL) {
            lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 3);
        } else {
            lv_obj_center(label);
        }
    }
    if (hint != NULL) {
        lv_obj_t *hint_label = ui_create_caption(icon, hint);
        lv_obj_set_style_text_align(hint_label, LV_TEXT_ALIGN_CENTER,
                                    LV_PART_MAIN);
        lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    return icon;
}

/* 自动亮度：根据光敏电阻（CAN 0x200 上报的 light_percent，已反相，
 * 值越大越暗）映射 PWM 占空比——越暗越亮。亮度控件在控制中心，
 * 每次调整都通过 ui_control_center_sync_brightness 反向同步滑块。 */
static void apply_auto_brightness(void)
{
    ui_env_data_t env;
    ui_get_environment_data(&env);
    const uint32_t duty =
        ((uint32_t)env.light_percent * LED_PWM_MAX_DUTY) / 100U;
    setLED(duty);
    ui_control_center_sync_brightness(duty);
}

/* 自动亮度状态导出：供控制中心读取/切换 */
bool ui_drive_is_auto_brightness(void)
{
    return s_auto_brightness_on;
}

void ui_drive_set_auto_brightness(bool enabled)
{
    s_auto_brightness_on = enabled;
    if (enabled) {
        apply_auto_brightness();
    }
}

/* 设置图标勾选状态（选中/取消） */
static void set_icon_checked(lv_obj_t *icon, bool checked)
{
    if (checked) {
        lv_obj_add_state(icon, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(icon, LV_STATE_CHECKED);
    }
}

/* 创建驾驶页面控件：标题、时钟、环境卡片、灯光图标、状态灯 */
void ui_drive_screen_init(lv_obj_t *parent)
{
    ui_apply_page_style(parent);
    lv_obj_set_style_pad_all(parent, 10, LV_PART_MAIN);

    /* 主页面背景图：铺满整屏并置于最底层 */
    lv_obj_t *bg = lv_image_create(parent);
    lv_image_set_src(bg, &lvgl_bg_320x240);
    lv_obj_set_pos(bg, -10, -10);
    lv_obj_set_size(bg, 320, 240);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_to_index(bg, 0);

    lv_obj_t *title = ui_create_title(parent, "驾驶");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 右上角三颗状态灯：第一颗保留设备状态位，中间显示 MQTT，第三颗显示 WiFi。 */
    s_status_leds[0] = create_status_led(parent, 252, lv_color_hex(0x1677C8));
    s_status_leds[1] = create_status_led(parent, 272, lv_color_hex(0xE5A21A));
    s_status_leds[2] = create_status_led(parent, 292, lv_color_hex(0xD84545));

    /* 中央大号时钟（正中央） */
    s_clock = ui_create_value(parent, "00:00:00", &lv_font_montserrat_36);
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, 0);

    /* 时钟上方日期 */
    s_date = ui_create_caption(parent, "----/--/--");
    lv_obj_align(s_date, LV_ALIGN_TOP_MID, 0, 66);

    /* 底部一行四个灯光图标，居中、距底边留边距 */
    static const struct {
        const char *symbol;
        light_icon_id_t id;
    } lights[] = {
        {LV_SYMBOL_LEFT, LIGHT_ICON_LEFT},
        {LV_SYMBOL_RIGHT, LIGHT_ICON_RIGHT},
        {LV_SYMBOL_EYE_OPEN, LIGHT_ICON_HIGH},
        {LV_SYMBOL_EYE_CLOSE, LIGHT_ICON_LOW},
    };
    for (uint32_t i = 0; i < sizeof(lights) / sizeof(lights[0]); ++i) {
        s_light_icons[lights[i].id] =
            create_light_icon(parent, lights[i].symbol, NULL,
                              (lv_coord_t)(58 + (int32_t)i * 54), 194, 42, 42,
                              lights[i].id);
    }

    /* 亮度/自动亮度/息屏控件已迁往控制中心（主页面下滑呼出） */
}

/* 刷新驾驶页面：读取共享数据并更新时钟、环境、灯光、状态 */
void ui_drive_screen_update(void)
{
    if (s_clock == NULL) return;

    ui_drive_data_t drive;
    ui_lighting_data_t lighting;
    ui_get_drive_data(&drive);
    ui_get_lighting_data(&lighting);

    /* 转向灯由 GPIO17/GPIO18 输出，每秒翻转一次电平。 */
    const uint32_t now_ms = lv_tick_get();
    const bool turn_active = lighting.left_turn || lighting.right_turn;
    if (!turn_active) {
        s_turn_blink_phase = true;
        s_turn_blink_last_ms = now_ms;
    } else if ((uint32_t)(now_ms - s_turn_blink_last_ms) >= 1000U) {
        s_turn_blink_last_ms += 1000U;
        s_turn_blink_phase = !s_turn_blink_phase;
    }
    setTurnSignals(lighting.left_turn && s_turn_blink_phase,
                   lighting.right_turn && s_turn_blink_phase);

    if (drive.year == 0U) {
      /* SNTP 尚未同步成功，日期占位显示 */
      lv_label_set_text(s_date, "----/--/--");
    } else {
      lv_label_set_text_fmt(s_date, "%04u/%02u/%02u", drive.year, drive.month,
                            drive.day);
    }
    lv_label_set_text_fmt(s_clock, "%02u:%02u:%02u",
                          drive.hour, drive.minute, drive.second);
    const bool wifi_connected = wifi_get_status() == WIFI_STATUS_CONNECTED;
    const bool mqtt_connected = mqtt_is_connected();

    /* 右上角状态灯呼吸：仅绿色（已激活）的灯按 0-100-0 循环变化，约 5s 一轮。
     * 亮度变化通过把颜色向黑色混合实现（LED 自身保持满亮度），
     * 几何尺寸与阴影宽度不变，只有明暗变化；断开的灯保持原色常亮不呼吸。 */
    const uint8_t breath = status_led_breath_brightness(now_ms);
    /* 右上角第一颗状态灯：指示自动亮度开关状态（绿=开启，蓝=关闭） */
    lv_led_set_color(s_status_leds[0],
                     s_auto_brightness_on ? lv_color_hex(0x19A463)
                                          : lv_color_hex(0x1677C8));
    lv_led_set_color(s_status_leds[1],
                     mqtt_connected ? lv_color_mix(lv_color_hex(0x19A463),
                                                   lv_color_black(), breath)
                                    : lv_color_hex(0xE5A21A));
    lv_led_set_color(s_status_leds[2],
                     wifi_connected ? lv_color_mix(lv_color_hex(0x19A463),
                                                   lv_color_black(), breath)
                                    : lv_color_hex(0xD84545));
    set_icon_checked(s_light_icons[LIGHT_ICON_LEFT],
                     lighting.left_turn && s_turn_blink_phase);
    set_icon_checked(s_light_icons[LIGHT_ICON_RIGHT],
                     lighting.right_turn && s_turn_blink_phase);
    set_icon_checked(s_light_icons[LIGHT_ICON_HIGH], lighting.high_beam);
    set_icon_checked(s_light_icons[LIGHT_ICON_LOW], lighting.low_beam);

    /* 自动亮度：按固定周期跟随光敏电阻读数刷新占空比 */
    if (s_auto_brightness_on) {
        static uint32_t s_auto_last_ms;
        if ((uint32_t)(now_ms - s_auto_last_ms) >= AUTO_BRIGHTNESS_PERIOD_MS) {
            s_auto_last_ms = now_ms;
            apply_auto_brightness();
        }
    }
}
