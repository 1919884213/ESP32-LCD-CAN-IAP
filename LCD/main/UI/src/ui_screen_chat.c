/* AI 对话页面：LLM 返回 4 个对话选项，显示在四个按钮上，点击即提问。
 * 屏幕太小不便键盘输入，改用"AI 给选项 -> 点击选项"的问答方式。
 *
 * 布局（320x240）：
 *   标题行(24) | 对话显示区(100) | 4 个选项按钮(2x2)
 * 首次连接时自动向 LLM 发起开场对话，把返回的 4 个示例选项填到按钮上。
 */
#include "ui_screen_chat.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "LLM.h"
#include "WiFi.h"
#include "esp_attr.h"
#include "ui.h"
#include "ui_helpers.h"

/* 对话历史最大字符数，超过后清屏重来，避免控件文本无限增长 */
#define CHAT_MAX_CHARS 4000
#define OPTION_COUNT 4
#define OPTION_MAX_LEN 64
/* 选项标记（"选项"）的 UTF-8 字节：选项从这里开始，只解析到按钮，不显示进文字框 */
#define OPTION_MARKER "\xE9\x80\x89\xE9\xA1\xB9"
#define OPTION_MARKER_LEN 6U
/* 回答全文缓冲（PSRAM）：流式完成后取全文解析选项 */
#define ANSWER_BUF_SIZE 4096

static lv_obj_t* s_chat_box;   /* 对话历史（只读） */
static lv_obj_t* s_thinking;   /* 等待回答提示（"AI 正在思考…"） */
static lv_obj_t* s_option_btn[OPTION_COUNT];
static lv_obj_t* s_option_label[OPTION_COUNT];
static char s_options[OPTION_COUNT][OPTION_MAX_LEN];
static int s_option_count;

/* 交互状态：等待回答中 / 本次连接是否已发过开场白 */
static bool s_awaiting;
static bool s_started;

/* 回答全文缓冲（PSRAM）：整段取回后解析正文与选项 */
EXT_RAM_BSS_ATTR static char s_answer_buf[ANSWER_BUF_SIZE];

/* 开场白（本次连接后第一次进对话页发送一次） */
#define CHAT_INTRO "你好，请介绍一下你能做什么"
/* AI 正文最大字数（UTF-8 字符），超出部分截断 */
#define CHAT_REPLY_MAX_CHARS 20

/* 追加一行对话到历史区，超长则清屏 */
static void chat_append(const char* prefix, const char* text) {
  if (s_chat_box == NULL) {
    return;
  }
  if (lv_textarea_get_text(s_chat_box)[0] != '\0' &&
      strlen(lv_textarea_get_text(s_chat_box)) > CHAT_MAX_CHARS) {
    lv_textarea_set_text(s_chat_box, "");
  }
  lv_textarea_add_text(s_chat_box, prefix);
  lv_textarea_add_text(s_chat_box, text);
  lv_textarea_add_text(s_chat_box, "\n");
  lv_textarea_set_cursor_pos(s_chat_box, LV_TEXTAREA_CURSOR_LAST);
}

/* 隐藏全部选项按钮（等待回答期间不显示上一轮选项） */
static void hide_option_buttons(void) {
  for (int i = 0; i < OPTION_COUNT; i++) {
    if (s_option_btn[i] != NULL) {
      lv_obj_add_flag(s_option_btn[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

/* 显示/隐藏"思考中"提示 */
static void set_thinking(bool on) {
  if (s_thinking == NULL) {
    return;
  }
  if (on) {
    lv_obj_clear_flag(s_thinking, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_thinking, LV_OBJ_FLAG_HIDDEN);
  }
}

/* 更新 4 个选项按钮：不足的隐藏 */
static void update_option_buttons(void) {
  for (int i = 0; i < OPTION_COUNT; i++) {
    if (i < s_option_count) {
      lv_label_set_text(s_option_label[i], s_options[i]);
      lv_obj_clear_flag(s_option_btn[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_option_btn[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

/* 选项按钮点击：把选项文本作为下一条提问发给 LLM */
static void option_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  const int index = (int)(intptr_t)lv_event_get_user_data(event);
  if (index >= s_option_count || s_awaiting || LLM_IsBusy()) {
    return;
  }
  const char* option = s_options[index];
  s_awaiting = true;
  hide_option_buttons();
  set_thinking(true);
  chat_append("你: ", option);
  LLM(option);
}

/* 跳过选项前缀：至多一个序号（ASCII 1-4 / 中文 一-四），再跳分隔符 */
static const char* skip_option_sep(const char* q, const char* end) {
  if (q < end && *q >= '1' && *q <= '4') {
    q++;
  } else if (q + 2 < end && (unsigned char)q[0] == 0xE4 &&
             (unsigned char)q[1] == 0xB8 &&
             ((unsigned char)q[2] == 0x80 || (unsigned char)q[2] == 0x89)) {
    q += 3; /* 一 / 三 */
  } else if (q + 2 < end && (unsigned char)q[0] == 0xE4 &&
             (unsigned char)q[1] == 0xBA && (unsigned char)q[2] == 0x8C) {
    q += 3; /* 二 */
  } else if (q + 2 < end && (unsigned char)q[0] == 0xE5 &&
             (unsigned char)q[1] == 0x9B && (unsigned char)q[2] == 0x9B) {
    q += 3; /* 四 */
  }
  while (q < end) {
    if (*q == ' ' || *q == '\t' || *q == ':' || *q == '.' || *q == ')' ||
        *q == '、' || *q == '*') {
      q++;
    } else if (q + 2 < end && (unsigned char)q[0] == 0xEF &&
               (unsigned char)q[1] == 0xBC && (unsigned char)q[2] == 0x9A) {
      q += 3; /* 全角冒号"：" */
    } else if (q + 2 < end && (unsigned char)q[0] == 0xE3 &&
               (unsigned char)q[1] == 0x80 && (unsigned char)q[2] == 0x80) {
      q += 3; /* 全角空格 */
    } else {
      break;
    }
  }
  return q;
}

/* 返回字符串前 max_chars 个 UTF-8 字符所占字节数（不切断多字节字符） */
static size_t utf8_limit(const char* text, size_t max_chars) {
  size_t chars = 0;
  size_t i = 0;
  while (text[i] != '\0') {
    if (((unsigned char)text[i] & 0xC0U) != 0x80U) {
      if (chars == max_chars) {
        break;
      }
      chars++;
    }
    i++;
  }
  return i;
}

/* 返回选项部分起点（正文结束偏移）：优先识别 "选项N：" 标记，
 * 否则识别行首编号 "1." / "1)" / "1、"；都没有返回整段长度。 */
static size_t find_body_end(const char* text) {
  const char* end_all = text + strlen(text);
  const char* p = text;
  const char* marker;
  while ((marker = strstr(p, OPTION_MARKER)) != NULL) {
    const char* after = marker + OPTION_MARKER_LEN;
    if (skip_option_sep(after, end_all) != after) {
      return (size_t)(marker - text);
    }
    p = after;
  }

  p = text;
  while (*p != '\0') {
    const char* nl = strchr(p, '\n');
    const char* line_end = (nl != NULL) ? nl : (p + strlen(p));
    const char* q0 = p;
    while (q0 < line_end && (*q0 == ' ' || *q0 == '\t' || *q0 == '*' ||
                             *q0 == '-' || *q0 == '#')) {
      q0++;
    }
    if ((size_t)(line_end - q0) >= 3 && *q0 >= '1' && *q0 <= '4' &&
        skip_option_sep(q0, line_end) > q0 + 1) {
      return (size_t)(p - text);
    }
    if (nl == NULL) {
      break;
    }
    p = nl + 1;
  }
  return (size_t)(end_all - text);
}

/* 把 [start,end) 去首尾空白/markdown 后存入 s_options */
static void add_option(const char* start, const char* end) {
  while (start < end && (*start == ' ' || *start == '\t' || *start == '*')) {
    start++;
  }
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' ||
                         end[-1] == '*' || end[-1] == '-')) {
    end--;
  }
  const size_t len = (size_t)(end - start);
  if (len > 0 && len < OPTION_MAX_LEN && s_option_count < OPTION_COUNT) {
    memcpy(s_options[s_option_count], start, len);
    s_options[s_option_count][len] = '\0';
    s_option_count++;
  }
}

/* 扫描 "选项N：xxx"：不依赖换行，单行内多个选项也能拆开；容忍 markdown/中文序号 */
static void scan_marked_options(const char* text) {
  const char* end_all = text + strlen(text);
  const char* p = text;
  const char* marker;
  while (s_option_count < OPTION_COUNT &&
         (marker = strstr(p, "\xE9\x80\x89\xE9\xA1\xB9")) != NULL) {
    const char* after = marker + 6;
    const char* q = skip_option_sep(after, end_all);
    if (q == after) { /* "选项" 后没有序号/分隔符，视为普通词语 */
      p = after;
      continue;
    }
    const char* next = strstr(q, "\xE9\x80\x89\xE9\xA1\xB9");
    const char* nl = strchr(q, '\n');
    const char* end = end_all;
    if (next != NULL && next < end) {
      end = next;
    }
    if (nl != NULL && nl < end) {
      end = nl;
    }
    add_option(q, end);
    p = (end > q) ? end : (q + 1);
  }
}

/* 兜底：无 "选项" 前缀时按行解析 "1. xxx" / "1) xxx" / "1、xxx" */
static void scan_numbered_lines(const char* text) {
  const char* p = text;
  while (*p != '\0' && s_option_count < OPTION_COUNT) {
    const char* nl = strchr(p, '\n');
    const char* line_end = (nl != NULL) ? nl : (p + strlen(p));
    const char* q0 = p;
    while (q0 < line_end && (*q0 == ' ' || *q0 == '\t' || *q0 == '*' ||
                             *q0 == '-' || *q0 == '#')) {
      q0++;
    }
    if ((size_t)(line_end - q0) >= 3 && *q0 >= '1' && *q0 <= '4') {
      const char* q = skip_option_sep(q0, line_end);
      if (q > q0 + 1) { /* 数字后必须跟分隔符，避免误吞普通句子 */
        add_option(q, line_end);
      }
    }
    if (nl == NULL) {
      break;
    }
    p = nl + 1;
  }
}

/* AI 没按格式给选项时的兜底，保证按钮区始终可用 */
static void set_default_options(void) {
  static const char* const defs[OPTION_COUNT] = {
      "再多讲一点", "换个话题", "给我讲个笑话", "你怎么看"};
  for (int i = 0; i < OPTION_COUNT; i++) {
    strncpy(s_options[i], defs[i], OPTION_MAX_LEN - 1U);
    s_options[i][OPTION_MAX_LEN - 1U] = '\0';
  }
  s_option_count = OPTION_COUNT;
}

/* 从 AI 回答中解析选项，取前 4 个 */
static void parse_options(const char* text) {
  s_option_count = 0;
  scan_marked_options(text);
  if (s_option_count == 0) {
    scan_numbered_lines(text);
  }
  if (s_option_count == 0) {
    set_default_options();
  }
  update_option_buttons();
}

/* 返回驾驶主页 */
static void home_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  lv_tabview_set_active(ui_TabView, 0, LV_ANIM_OFF);
}

void ui_chat_screen_init(lv_obj_t* parent) {
  ui_apply_page_style(parent);
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

  /* 标题行 */
  lv_obj_t* title = ui_create_title(parent, "AI 对话");
  lv_obj_set_pos(title, 0, 0);

  lv_obj_t* home = lv_button_create(parent);
  lv_obj_set_size(home, 44, 22);
  lv_obj_align(home, LV_ALIGN_TOP_RIGHT, 0, 0);
  ui_apply_secondary_button_style(home);
  lv_obj_add_event_cb(home, home_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* home_label = ui_create_value(home, "首页", ui_font_zh_14());
  lv_obj_set_style_text_color(home_label, lv_color_hex(0x1677C8),
                              LV_PART_MAIN);
  lv_obj_center(home_label);

  /* 对话历史（只读） */
  s_chat_box = lv_textarea_create(parent);
  lv_obj_set_size(s_chat_box, 304, 100);
  lv_obj_set_pos(s_chat_box, 8, 26);
  lv_textarea_set_text(s_chat_box, "");
  lv_textarea_set_cursor_click_pos(s_chat_box, false);
  lv_obj_set_style_text_font(s_chat_box, ui_font_zh_14(), LV_PART_MAIN);
  lv_obj_set_style_text_color(s_chat_box, lv_color_hex(0x333333),
                              LV_PART_MAIN);
  lv_obj_add_flag(s_chat_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(s_chat_box, LV_SCROLLBAR_MODE_AUTO);

  /* 等待回答提示：位于按钮区中央，仅在请求期间显示 */
  s_thinking = ui_create_caption(parent, "AI 正在思考…");
  lv_obj_align(s_thinking, LV_ALIGN_CENTER, 0, 55);
  lv_obj_add_flag(s_thinking, LV_OBJ_FLAG_HIDDEN);

  /* 4 个选项按钮：2x2 网格 */
  static const lv_coord_t btn_x[OPTION_COUNT] = {8, 164, 8, 164};
  static const lv_coord_t btn_y[OPTION_COUNT] = {132, 132, 188, 188};
  for (int i = 0; i < OPTION_COUNT; i++) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, 148, 46);
    lv_obj_set_pos(btn, btn_x[i], btn_y[i]);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xE5F0F7),
                              LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xD7E0E4), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, option_event_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)i);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, "");
    lv_obj_set_width(label, 136);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, ui_font_zh_14(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
    s_option_btn[i] = btn;
    s_option_label[i] = label;
  }
  s_option_count = 0;
}

void ui_chat_screen_update(void) {
  if (s_chat_box == NULL) {
    return;
  }

  /* 回答就绪就整段显示：直接轮询取用，不依赖 s_awaiting/忙闲，避免漏取 */
  if (LLM_TakeAnswer(s_answer_buf, sizeof(s_answer_buf))) {
    s_awaiting = false;
    set_thinking(false);
    size_t body_len = find_body_end(s_answer_buf);
    /* 兜底：正文最多 CHAT_REPLY_MAX_CHARS 个字符 */
    const size_t limit = utf8_limit(s_answer_buf, CHAT_REPLY_MAX_CHARS);
    if (body_len > limit) {
      body_len = limit;
    }
    while (body_len > 0 && (s_answer_buf[body_len - 1U] == '\n' ||
                            s_answer_buf[body_len - 1U] == '\r' ||
                            s_answer_buf[body_len - 1U] == ' ' ||
                            s_answer_buf[body_len - 1U] == '\t')) {
      body_len--;
    }
    /* 正文为空（模型只给了选项）时不写空的 "AI: " 行；选项进按钮 */
    if (body_len > 0U) {
      const char saved = s_answer_buf[body_len];
      s_answer_buf[body_len] = '\0';
      chat_append("AI: ", s_answer_buf);
      s_answer_buf[body_len] = saved;
    }
    parse_options(s_answer_buf);
  }

  /* 开场白：本次连接后第一次进对话页发送一次 */
  if (!s_started && wifi_get_status() == WIFI_STATUS_CONNECTED &&
      !s_awaiting && !LLM_IsBusy()) {
    s_started = true;
    s_awaiting = true;
    hide_option_buttons();
    set_thinking(true);
    chat_append("你: ", CHAT_INTRO);
    LLM(CHAT_INTRO);
  }
}