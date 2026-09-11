/* OTA 服务器响应显示页面头文件。 */
#ifndef UI_SCREEN_OTA_H
#define UI_SCREEN_OTA_H

#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* 创建 OTA 查询结果页面。 */
void ui_ota_screen_init(lv_obj_t *parent);
/* 刷新 OTA 查询结果显示。 */
void ui_ota_screen_update(void);

#endif /* UI_SCREEN_OTA_H */
