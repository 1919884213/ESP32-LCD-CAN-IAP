#include "ui_screen_wifi.h"
#include "WiFi.h"
#include "ui_helpers.h"
#include "ONENET/onenet.h"

/* WiFi 页控件指针：SSID/密码输入框、软键盘、状态 */
static lv_obj_t* s_ssid;
static lv_obj_t* s_password;
static lv_obj_t* s_keyboard;
static lv_obj_t* s_status;
static bool s_validation_error;

const char* SSID = WIFI_DEFAULT_SSID;
const char* pwd = WIFI_DEFAULT_PASSWORD;

/* 软键盘事件回调：确定/取消时收起键盘 */
static void keyboard_event_cb(lv_event_t* event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_keyboard_set_textarea(s_keyboard, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
  }
}

/* 输入框聚焦回调：弹出软键盘并关联到当前输入框 */
static void textarea_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_FOCUSED)
    return;
  lv_keyboard_set_textarea(s_keyboard, lv_event_get_target(event));
  lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

/* 连接按钮回调：读取 SSID/密码并请求 WiFi 连接 */
static void connect_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    return;
  const char* ssid = lv_textarea_get_text(s_ssid);
  const char* password = lv_textarea_get_text(s_password);
  if (!wifi_connect_request(ssid, password)) {
    s_validation_error = true;
    lv_label_set_text(s_status, "请输入有效 SSID/密码");
    return;
  }
  s_validation_error = false;
  lv_keyboard_set_textarea(s_keyboard, NULL);
  lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_status, "连接中…");
}

/* 默认 WiFi 按钮回调：使用代码中预设的 SSID 和密码连接。 */
static void default_wifi_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    return;
  if (!wifi_connect_request(SSID, pwd)) {
    s_validation_error = true;
    lv_label_set_text(s_status, "默认 WiFi 无效");
    return;
  }
  s_validation_error = false;
  lv_keyboard_set_textarea(s_keyboard, NULL);
  lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_status, "正在连接默认…");
}

/* 创建一个单行输入框（带占位符与聚焦事件） */
static lv_obj_t* create_textarea(lv_obj_t* parent,
                                 const char* placeholder,
                                 lv_coord_t y) {
  lv_obj_t* textarea = lv_textarea_create(parent);
  lv_obj_set_size(textarea, 280, 31);
  lv_obj_set_pos(textarea, 12, y);
  lv_textarea_set_one_line(textarea, true);
  lv_textarea_set_placeholder_text(textarea, placeholder);
  lv_obj_set_style_text_font(textarea, ui_font_zh_14(), LV_PART_MAIN);
  ui_apply_input_style(textarea);
  lv_obj_add_event_cb(textarea, textarea_event_cb, LV_EVENT_FOCUSED, NULL);
  return textarea;
}

/* 创建 WiFi 页面控件：标题、SSID/密码输入、连接按钮、软键盘 */
void ui_wifi_screen_init(lv_obj_t* parent) {
  ui_apply_page_style(parent);
  lv_obj_set_style_pad_all(parent, 8, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = ui_create_title(parent, "网络");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
  s_status = ui_create_caption(parent, "未连接");
  lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, 0, 1);

  /* SSID 与密码输入框 */
  s_ssid = create_textarea(parent, "WiFi 名称 (SSID)", 42);
  s_password = create_textarea(parent, "密码", 82);
  lv_textarea_set_password_mode(s_password, true);

  /* 手动输入 WiFi 的连接按钮 */
  lv_obj_t* connect = lv_button_create(parent);
  lv_obj_set_size(connect, 128, 32);
  lv_obj_set_pos(connect, 12, 127);
  ui_apply_primary_button_style(connect);
  lv_obj_add_event_cb(connect, connect_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* label = ui_create_value(connect, "连接", ui_font_zh_14());
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_center(label);

  /* 使用文件顶部预设 SSID/密码的快捷连接按钮 */
  lv_obj_t* default_wifi = lv_button_create(parent);
  lv_obj_set_size(default_wifi, 160, 32);
  lv_obj_set_pos(default_wifi, 148, 127);
  ui_apply_secondary_button_style(default_wifi);
  lv_obj_add_event_cb(default_wifi, default_wifi_event_cb, LV_EVENT_CLICKED,
                      NULL);
  lv_obj_t* default_label =
      ui_create_value(default_wifi, "默认 WiFi", ui_font_zh_14());
  lv_obj_set_style_text_color(default_label, lv_color_hex(0x1677C8), LV_PART_MAIN);
  lv_obj_center(default_label);

  /* 底部软键盘，初始隐藏 */
  s_keyboard = lv_keyboard_create(parent);
  lv_obj_set_size(s_keyboard, 304, 94);
  lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_CANCEL, NULL);
}

/* 刷新 WiFi 页面：根据连接状态更新状态标签 */
void ui_wifi_screen_update(void) {
  static bool s_mqtt_init_done;
  if (s_status == NULL)
    return;
  if (s_validation_error)
    return;
  switch (wifi_get_status()) {
    case WIFI_STATUS_IDLE:
      lv_label_set_text(s_status, "未连接");
      break;
    case WIFI_STATUS_CONNECTING:
      lv_label_set_text(s_status, "连接中…");
      break;
    case WIFI_STATUS_CONNECTED:
      lv_label_set_text(s_status, "已连接");
      if (!s_mqtt_init_done) {
        mqtt_client_init();
        s_mqtt_init_done = true;
      }
      break;
    case WIFI_STATUS_FAILED:
      lv_label_set_text(s_status, "连接失败");
      break;
  }
}
