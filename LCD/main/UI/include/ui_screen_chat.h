/* AI 对话页面：LLM 返回 4 个选项显示在按钮上，点击即提问。 */
#ifndef UI_SCREEN_CHAT_H
#define UI_SCREEN_CHAT_H

#include "lvgl.h"

/* 创建 AI 对话页面。 */
void ui_chat_screen_init(lv_obj_t* parent);
/* 轮询 LLM 回答并追加到对话区。 */
void ui_chat_screen_update(void);

#endif /* UI_SCREEN_CHAT_H */
