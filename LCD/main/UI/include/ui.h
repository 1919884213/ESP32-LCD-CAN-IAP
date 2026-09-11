/* UI 主模块头文件：声明全局页面对象与 UI 初始化/周期刷新接口 */
#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_data.h"

/* 全局页面对象：由隐藏 TabView 管理，DRIVE 为默认主页。 */
extern lv_obj_t *ui_TabView;
extern lv_obj_t *ui_ScreenDrive;
extern lv_obj_t *ui_ScreenWeather;
extern lv_obj_t *ui_ScreenEnv;
extern lv_obj_t *ui_ScreenImu;
extern lv_obj_t *ui_ScreenIap;
extern lv_obj_t *ui_ScreenWifi;
extern lv_obj_t *ui_ScreenOta;
extern lv_obj_t *ui_ScreenW25;
extern lv_obj_t *ui_ScreenChat;
extern lv_obj_t *ui_ScreenMenu;

/* TabView 页序（ui.c 定义各页时按此顺序添加） */
typedef enum {
    UI_PAGE_DRIVE = 0,
    UI_PAGE_WEATHER,
    UI_PAGE_ENV,
    UI_PAGE_IMU,
    UI_PAGE_IAP,
    UI_PAGE_WIFI,
    UI_PAGE_OTA,
    UI_PAGE_W25,
    UI_PAGE_CHAT,
    UI_PAGE_MENU,
} ui_page_id_t;

/* 初始化整个 UI：创建 DRIVE 主页面、控制中心与功能菜单页。 */
void ui_init(void);
/* 周期刷新所有页面（由 LVGL 任务调用） */
void ui_tick(void);

/* 页面跳转：IAP 作为 OTA 子页面，由 OTA 页按钮进入/返回 */
void ui_goto_page_iap(void);
void ui_goto_page_ota(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
