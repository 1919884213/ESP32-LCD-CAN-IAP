#include "ui.h"
#include "ui_control_center.h"
#include "ui_helpers.h"
#include "ui_screen_drive.h"
#include "ui_screen_env.h"
#include "ui_screen_weather.h"
#include "ui_screen_imu.h"
#include "ui_screen_iap.h"
#include "ui_screen_ota.h"
#include "ui_screen_w25.h"
#include "ui_screen_chat.h"
#include "ui_standby.h"
#include "ui_screen_wifi.h"
#include "../asset/icon_download.h"
#include "../asset/icon_environment.h"
#include "../asset/icon_imu.h"
#include "../asset/icon_wifi.h"
#include "../asset/weather_icons.h"
#include <stdint.h>

/* 页面对象仍由同一个 TabView 管理，标签栏隐藏，只暴露 DRIVE 主页面。 */
lv_obj_t *ui_TabView;
lv_obj_t *ui_ScreenDrive;
lv_obj_t *ui_ScreenWeather;
lv_obj_t *ui_ScreenEnv;
lv_obj_t *ui_ScreenImu;
lv_obj_t *ui_ScreenIap;
lv_obj_t *ui_ScreenWifi;
lv_obj_t *ui_ScreenOta;
lv_obj_t *ui_ScreenW25;
lv_obj_t *ui_ScreenChat;
lv_obj_t *ui_ScreenMenu;

typedef struct {
    ui_page_id_t page;
    const lv_image_dsc_t *icon;
    const char *title;
    const char *subtitle;
} ui_menu_item_t;

static const ui_menu_item_t s_menu_items[] = {
    { UI_PAGE_ENV, &icon_environment, "环境", "温度·光照" },
    { UI_PAGE_IMU, &icon_imu, "姿态", "加速度·陀螺" },
    { UI_PAGE_WEATHER, &weather_sunny, "天气", "实时天气" },
    { UI_PAGE_OTA, &icon_download, "OTA", "查询·下载" },
    { UI_PAGE_WIFI, &icon_wifi, "网络", "WiFi 配置" },
    { UI_PAGE_W25, NULL, "存储", "镜像槽位" },
    { UI_PAGE_CHAT, NULL, "对话", "AI 助手" },
};

/* 功能菜单（整页）布局尺寸 */
#define MENU_TILE_HEIGHT 64
#define MENU_TILE_GAP    18

static void return_to_drive_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    lv_tabview_set_active(ui_TabView, UI_PAGE_DRIVE, LV_ANIM_OFF);
}

static void menu_item_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const ui_page_id_t page = (ui_page_id_t)(intptr_t)lv_event_get_user_data(event);
    lv_tabview_set_active(ui_TabView, (uint32_t)page, LV_ANIM_OFF);
    if (page == UI_PAGE_WEATHER) {
        ui_weather_screen_request();
    }
}

/* 页面跳转：IAP 子页面（由 OTA 页按钮进入） */
void ui_goto_page_iap(void)
{
    lv_tabview_set_active(ui_TabView, UI_PAGE_IAP, LV_ANIM_OFF);
}

void ui_goto_page_ota(void)
{
    lv_tabview_set_active(ui_TabView, UI_PAGE_OTA, LV_ANIM_OFF);
}

/* 主屏下滑呼出控制中心；右划进入功能页；左划任意页返回 DRIVE。 */
static void swipe_menu_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_GESTURE) {
        return;
    }
    /* 控制中心覆盖层展开时，手势由面板自行处理，这里不响应 */
    if (ui_control_center_is_open()) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(lv_indev_active());
    const uint32_t active = lv_tabview_get_tab_active(ui_TabView);
    if (direction == LV_DIR_BOTTOM && active == UI_PAGE_DRIVE) {
        ui_control_center_open();
    } else if (direction == LV_DIR_RIGHT && active == UI_PAGE_DRIVE) {
        lv_tabview_set_active(ui_TabView, UI_PAGE_MENU, LV_ANIM_OFF);
    } else if (direction == LV_DIR_LEFT && active != UI_PAGE_DRIVE) {
        lv_tabview_set_active(ui_TabView, UI_PAGE_DRIVE, LV_ANIM_OFF);
    }
}

static void enable_menu_swipe(lv_obj_t *screen)
{
    /* 页面固定布局无需滚动：去掉 SCROLLABLE，避免内容溢出（如背景图）时
     * 页面成为滚动对象而吞掉单指滑动手势 */
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(screen, swipe_menu_event_cb, LV_EVENT_GESTURE, NULL);
}

static void create_menu_tile(lv_obj_t *parent, const ui_menu_item_t *item,
                             lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_set_size(tile, 138, MENU_TILE_HEIGHT);
    lv_obj_set_pos(tile, x, y);
    ui_apply_card_style(tile);
    lv_obj_add_event_cb(tile, menu_item_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)item->page);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0xE5F0F7),
                              LV_PART_MAIN | LV_STATE_PRESSED);

    if (item->icon != NULL) {
        lv_obj_t *image = lv_image_create(tile);
        lv_image_set_src(image, item->icon);
        lv_obj_set_size(image, 30, 30);
        lv_obj_set_pos(image, 10, (MENU_TILE_HEIGHT - 30) / 2);
        /* 64px 天气图标单独缩放，其余图标用统一倍率 */
        lv_image_set_scale(image,
                           item->page == UI_PAGE_WEATHER ? 120 : 178);
    } else {
        const char *symbol =
            item->page == UI_PAGE_CHAT ? LV_SYMBOL_EDIT : LV_SYMBOL_SAVE;
        lv_obj_t *icon_label = ui_create_title(tile, symbol);
        lv_obj_set_pos(icon_label, 16, 12);
    }

    lv_obj_t *title = ui_create_value(tile, item->title, ui_font_zh_16());
    lv_obj_set_pos(title, 50, 10);
    lv_obj_t *subtitle = ui_create_caption(tile, item->subtitle);
    lv_obj_set_width(subtitle, 76);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(subtitle, 50, 38);
}

/* 功能页：整页，选项卡放在可滚动容器中，增大间距、隐藏滚动条 */
static void create_function_menu(lv_obj_t *parent)
{
    ui_apply_page_style(parent);
    lv_obj_set_style_pad_all(parent, 10, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = ui_create_title(parent, "功能");
    lv_obj_set_pos(title, 0, 0);
    lv_obj_t *home = lv_button_create(parent);
    lv_obj_set_size(home, 52, 24);
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, 0, 0);
    ui_apply_secondary_button_style(home);
    lv_obj_add_event_cb(home, return_to_drive_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *menu_home_label = ui_create_value(home, "首页", ui_font_zh_14());
    lv_obj_set_style_text_color(menu_home_label, lv_color_hex(0x1677C8),
                                LV_PART_MAIN);
    lv_obj_center(menu_home_label);

    /* 可滚动容器：两列选项卡、增大间距、隐藏滚动条 */
    lv_obj_t *scroll = lv_obj_create(parent);
    lv_obj_set_size(scroll, 300, 196);
    lv_obj_set_pos(scroll, 0, 34);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scroll, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scroll, 0, LV_PART_MAIN);

    const uint32_t count = sizeof(s_menu_items) / sizeof(s_menu_items[0]);
    for (uint32_t index = 0; index < count; ++index) {
        const lv_coord_t x = (index % 2U) == 0U ? 0 : 150;
        const lv_coord_t y =
            (lv_coord_t)(index / 2U) * (MENU_TILE_HEIGHT + MENU_TILE_GAP);
        create_menu_tile(scroll, &s_menu_items[index], x, y);
    }
}

void ui_init(void)
{
    ui_TabView = lv_tabview_create(lv_screen_active());
    lv_obj_set_size(ui_TabView, 320, 240);
    lv_obj_center(ui_TabView);
    ui_apply_page_style(ui_TabView);
    lv_tabview_set_tab_bar_size(ui_TabView, 0);

    lv_obj_t *content = lv_tabview_get_content(ui_TabView);
    lv_obj_set_scroll_dir(content, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    ui_ScreenDrive = lv_tabview_add_tab(ui_TabView, "驾驶");
    ui_ScreenWeather = lv_tabview_add_tab(ui_TabView, "天气");
    ui_ScreenEnv = lv_tabview_add_tab(ui_TabView, "环境");
    ui_ScreenImu = lv_tabview_add_tab(ui_TabView, "姿态");
    ui_ScreenIap = lv_tabview_add_tab(ui_TabView, "IAP");
    ui_ScreenWifi = lv_tabview_add_tab(ui_TabView, "网络");
    ui_ScreenOta = lv_tabview_add_tab(ui_TabView, "OTA");
    ui_ScreenW25 = lv_tabview_add_tab(ui_TabView, "存储");
    ui_ScreenChat = lv_tabview_add_tab(ui_TabView, "对话");
    ui_ScreenMenu = lv_tabview_add_tab(ui_TabView, "功能");

    lv_obj_add_flag(lv_tabview_get_tab_bar(ui_TabView), LV_OBJ_FLAG_HIDDEN);

    ui_drive_screen_init(ui_ScreenDrive);
    ui_weather_screen_init(ui_ScreenWeather);
    ui_env_screen_init(ui_ScreenEnv);
    ui_imu_screen_init(ui_ScreenImu);
    ui_iap_screen_init(ui_ScreenIap);
    ui_wifi_screen_init(ui_ScreenWifi);
    ui_ota_screen_init(ui_ScreenOta);
    ui_w25_screen_init(ui_ScreenW25);
    ui_chat_screen_init(ui_ScreenChat);
    create_function_menu(ui_ScreenMenu);

    enable_menu_swipe(ui_ScreenDrive);
    enable_menu_swipe(ui_ScreenWeather);
    enable_menu_swipe(ui_ScreenEnv);
    enable_menu_swipe(ui_ScreenImu);
    enable_menu_swipe(ui_ScreenIap);
    enable_menu_swipe(ui_ScreenWifi);
    enable_menu_swipe(ui_ScreenOta);
    enable_menu_swipe(ui_ScreenW25);
    enable_menu_swipe(ui_ScreenChat);
    enable_menu_swipe(ui_ScreenMenu);

    lv_tabview_set_active(ui_TabView, UI_PAGE_DRIVE, LV_ANIM_OFF);

    /* 控制中心覆盖层：任意页面下滑呼出（挂在 lv_layer_top，不随页切换销毁） */
    ui_control_center_init();

    /* 无触摸 5s 进入待机：降亮度、只显示 SNTP 时间，触摸唤醒 */
    ui_standby_init();
}

void ui_tick(void)
{
    /* 常驻刷新：驾驶页含转向灯/自动亮度/状态灯等副作用，WiFi 页含
     * MQTT/LLM 触发，二者与当前页无关，必须每 tick 调用。 */
    ui_drive_screen_update();
    ui_wifi_screen_update();

    /* 其余页面只更新当前激活页，避免隐藏页每 100ms 也刷新 label
     * （大量 set_text_fmt/display event 会放大 LVGL 内部压力）。 */
    switch (lv_tabview_get_tab_active(ui_TabView)) {
        case UI_PAGE_WEATHER: ui_weather_screen_update(); break;
        case UI_PAGE_ENV:     ui_env_screen_update();     break;
        case UI_PAGE_IMU:     ui_imu_screen_update();     break;
        case UI_PAGE_IAP:     ui_iap_screen_update();     break;
        case UI_PAGE_OTA:     ui_ota_screen_update();     break;
        case UI_PAGE_W25:     ui_w25_screen_update();     break;
        case UI_PAGE_CHAT:    ui_chat_screen_update();    break;
        default:              break;
    }
}
