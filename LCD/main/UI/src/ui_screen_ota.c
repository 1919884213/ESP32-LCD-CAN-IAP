#include "ui_screen_ota.h"

#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http.h"
#include "ui.h"
#include "ui_helpers.h"

/* OTA 页控件指针：查询状态、版本、大小、下载地址、服务器消息 */
static lv_obj_t* s_status;
static lv_obj_t* s_version;
static lv_obj_t* s_size;
static lv_obj_t* s_url;
static lv_obj_t* s_tag;
static lv_obj_t* s_unix_timestamp;
/* 固件请求选择、目标分区和下载相关控件 */
static lv_obj_t* s_request_select;
static lv_obj_t* s_target;
static lv_obj_t* s_download_status;
static lv_obj_t* s_progress_bar;
static lv_obj_t* s_progress_label;

/* 下载状态（由下载任务更新，UI 轮询读取） */
static volatile bool s_download_running;
static volatile uint8_t s_download_progress;
static char s_download_status_buf[64] = "空闲";
/* 信号量保护 GET 状态，避免 LVGL 在 HTTP 任务写结果期间读取。 */
static SemaphoreHandle_t s_http_state_sem;
static bool s_http_request_running;
static bool s_http_result_ready;
static uint32_t s_request_target;
/* 下载任务使用的固件 URL 快照，避免查询与下载并发竞争。 */
static char s_download_url[HTTP_OTA_URL_MAX];
static char s_download_tag[HTTP_OTA_TAG_MAX];
/* 下载固件的显示名（来源 + .bin），随下载任务写入槽位记录。 */
static char s_download_name[W25Q64_BIN_NAME_MAX];

static bool http_request_begin(void) {
  if (s_http_state_sem == NULL ||
      xSemaphoreTake(s_http_state_sem, portMAX_DELAY) != pdPASS) {
    return false;
  }

  if (s_http_request_running) {
    xSemaphoreGive(s_http_state_sem);
    return false;
  }
  s_http_request_running = true;
  s_http_result_ready = false;
  xSemaphoreGive(s_http_state_sem);
  return true;
}

static void http_request_finish(void) {
  if (s_http_state_sem == NULL ||
      xSemaphoreTake(s_http_state_sem, portMAX_DELAY) != pdPASS) {
    return;
  }
  s_http_request_running = false;
  s_http_result_ready = true;
  xSemaphoreGive(s_http_state_sem);
}

static void http_request_cancel(void) {
  if (s_http_state_sem == NULL ||
      xSemaphoreTake(s_http_state_sem, portMAX_DELAY) != pdPASS) {
    return;
  }
  s_http_request_running = false;
  s_http_result_ready = false;
  xSemaphoreGive(s_http_state_sem);
}

static void http_request_get_state(bool* running, bool* result_ready) {
  *running = false;
  *result_ready = false;
  if (s_http_state_sem == NULL ||
      xSemaphoreTake(s_http_state_sem, portMAX_DELAY) != pdPASS) {
    return;
  }
  *running = s_http_request_running;
  *result_ready = s_http_result_ready;
  xSemaphoreGive(s_http_state_sem);
}

static void http_get_task(void* args) {
  const char* request_url = (const char*)args;
  esp_err_t err = http_get(request_url);

  if (err != ESP_OK) {
    ESP_LOGE("OTA", "OTA metadata request failed: %s", esp_err_to_name(err));
  }
  http_request_finish();
  vTaskDelete(NULL);
}

/* 读取当前固件按钮对应的云平台请求地址。 */
static const char* selected_firmware_url(uint32_t target) {
  switch (target) {
    case 0U:
      return CanDevice1;
    case 1U:
      return CanDevice2;
    case 2U:
      return myCanDevice1;
    default:
      return myCanDevice2;
  }
}

/* 请求按钮回调：请求下拉框选中主题的服务器信息。 */
static void request_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  if (!http_request_begin()) {
    lv_label_set_text(s_status, "请求进行中");
    return;
  }

  lv_label_set_text(s_status, "查询中…");
  s_request_target = lv_dropdown_get_selected(s_request_select);
  const char* request_url = selected_firmware_url(s_request_target);
  /* xTaskCreate stack depth is in words. Keep TLS's 32 KiB stack in PSRAM. */
  if (xTaskCreateWithCaps(http_get_task, "httpTask", 8192,
                          (void*)request_url, 5, NULL,
                          MALLOC_CAP_SPIRAM) != pdPASS) {
    http_request_cancel();
    ESP_LOGE("OTA", "Failed to create HTTP task: free internal=%u, PSRAM=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    lv_label_set_text(s_status, "任务创建失败");
  }
}

/* 读取下载按钮当前选中的逻辑固件槽（下载自动写到非活动半区）。 */
static W25Q64_FwSlot_t selected_fw_slot(void) {
  switch (lv_dropdown_get_selected(s_target)) {
    case 0:
      return W25Q64_FW_STM32_1;
    case 1:
      return W25Q64_FW_STM32_2;
    case 2:
      return W25Q64_FW_STM32_3;
    default:
      return W25Q64_FW_STM32_4;
  }
}

/* 下载进度回调：由下载任务在每收到一块数据时调用。 */
static void download_progress_cb(uint32_t written, uint32_t total, void* arg) {
  (void)arg;
  s_download_progress = (uint8_t)((uint64_t)written * 100U / total);
}

static int hex_digit_value(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

/* 从云平台 TAG 中提取独立的 8 位十六进制 CRC32。 */
static bool parse_crc32_tag(const char* tag, uint32_t* crc32) {
  if (tag == NULL || crc32 == NULL) {
    return false;
  }

  for (const char* candidate = tag; *candidate != '\0'; ++candidate) {
    if (hex_digit_value(candidate[0]) < 0 ||
        (candidate != tag && hex_digit_value(candidate[-1]) >= 0)) {
      continue;
    }

    uint32_t value = 0;
    size_t digits = 0;
    while (digits < 8U && hex_digit_value(candidate[digits]) >= 0) {
      value = (value << 4U) | (uint32_t)hex_digit_value(candidate[digits]);
      ++digits;
    }
    if (digits == 8U && hex_digit_value(candidate[digits]) < 0) {
      *crc32 = value;
      return true;
    }
  }
  return false;
}

/* 下载任务：在独立任务中下载固件到选中槽位，避免阻塞 LVGL 线程。 */
static void download_task(void* arg) {
  const W25Q64_FwSlot_t fw = (W25Q64_FwSlot_t)(uintptr_t)arg;
  /* 写当前非活动半区，成功后切换，保留另一份旧固件。 */
  const W25Q64_Slot_t slot = W25Q64_GetDownloadSlot(fw);
  uint32_t downloaded_size = 0;
  esp_err_t err = http_downloadBin(s_download_url, slot, download_progress_cb,
                                   NULL, &downloaded_size);
  if (err == ESP_OK) {
    s_download_progress = 100;
    uint32_t expected_crc32 = 0;
    uint32_t actual_crc32 = 0;
    snprintf(s_download_status_buf, sizeof(s_download_status_buf),
             "校验 CRC32…");
    err = http_calculate_bin_crc32(slot, downloaded_size, &actual_crc32);
    if (err != ESP_OK) {
      snprintf(s_download_status_buf, sizeof(s_download_status_buf),
               "CRC32 读取失败: %s", esp_err_to_name(err));
    } else if (!parse_crc32_tag(s_download_tag, &expected_crc32)) {
      snprintf(s_download_status_buf, sizeof(s_download_status_buf),
               "CRC32: %08lX 标签无效", (unsigned long)actual_crc32);
    } else if (actual_crc32 != expected_crc32) {
      snprintf(s_download_status_buf, sizeof(s_download_status_buf),
               "CRC32 校验失败 E:%08lX A:%08lX", (unsigned long)expected_crc32,
               (unsigned long)actual_crc32);
      err = ESP_ERR_INVALID_CRC;
    } else {
      W25Q64_ImageInfo_t image_info = {
          .size = downloaded_size,
          .crc32 = actual_crc32,
      };
      strlcpy(image_info.name, s_download_name, sizeof(image_info.name));
      err = W25Q64_SaveImageInfo(slot, &image_info);
      if (err == ESP_OK) {
        err = W25Q64_SetActiveSlot(fw, slot);
      }
      if (err == ESP_OK) {
        snprintf(s_download_status_buf, sizeof(s_download_status_buf),
                 "CRC32 校验通过: 0x%08lX", (unsigned long)actual_crc32);
      } else {
        snprintf(s_download_status_buf, sizeof(s_download_status_buf),
                 "元数据保存失败: %s", esp_err_to_name(err));
      }
    }
  }
  s_download_running = false;
  if (err != ESP_OK && strncmp(s_download_status_buf, "CRC32 ", 6U) != 0) {
    snprintf(s_download_status_buf, sizeof(s_download_status_buf),
             "下载失败: %s", esp_err_to_name(err));
  }
  vTaskDelete(NULL);
}

/* 下载按钮回调：校验已查询到固件后，启动下载任务。 */
static void download_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  if (s_download_running) {
    return;
  }

  http_ota_info_t info;
  http_get_ota_info(&info);
  if (!info.valid) {
    snprintf(s_download_status_buf, sizeof(s_download_status_buf),
             "无固件信息");
    return;
  }
  /* 快照 URL 与显示名，防止下载期间被新的查询覆盖。 */
  const W25Q64_FwSlot_t fw = selected_fw_slot();
  strlcpy(s_download_url, info.url, sizeof(s_download_url));
  strlcpy(s_download_tag, info.tag, sizeof(s_download_tag));
  strlcpy(s_download_name, ui_firmware_name((uint32_t)fw),
          sizeof(s_download_name));
  s_download_running = true;
  s_download_progress = 0;
  snprintf(s_download_status_buf, sizeof(s_download_status_buf),
           "下载中…");
  /* 任务栈显式分配到 PSRAM（4096 字 = 16KB），省内部 RAM。
   * 下载过程只做 HTTP/TLS 与外挂 SPI Flash，不触发内部 Flash 写，PSRAM 栈安全。
   */
  if (xTaskCreateWithCaps(download_task, "ota_dl", 4096,
                          (void*)(uintptr_t)fw, 10, NULL,
                          MALLOC_CAP_SPIRAM) != pdPASS) {
    s_download_running = false;
    snprintf(s_download_status_buf, sizeof(s_download_status_buf),
             "任务创建失败");
  }
}

/* 进入 IAP 子页面（CAN 升级） */
static void iap_event_cb(lv_event_t* event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }
  ui_goto_page_iap();
}

/* 目标下拉框使用四个 STM32 逻辑槽固定的 bin 名，只需构建一次。 */
static void refresh_target_options(void) {
  static bool built = false;
  if (built) {
    return;
  }
  built = true;

  char options[W25Q64_FW_COUNT * (W25Q64_BIN_NAME_MAX + 8U)];
  size_t used = 0;
  options[0] = '\0';
  for (uint32_t fw = 0; fw < W25Q64_FW_COUNT; ++fw) {
    if (fw != 0U) {
      options[used++] = '\n';
    }
    const char* name = ui_firmware_name(fw);
    const size_t len = strlen(name);
    memcpy(&options[used], name, len);
    used += len;
    options[used] = '\0';
  }

  const uint32_t selected = lv_dropdown_get_selected(s_target);
  lv_dropdown_set_options(s_target, options);
  if (selected < W25Q64_FW_COUNT) {
    lv_dropdown_set_selected(s_target, selected);
  }
}

/* 创建 OTA 页面：状态、版本、大小、下载地址和服务器消息。 */
void ui_ota_screen_init(lv_obj_t* parent) {
  s_http_state_sem = xSemaphoreCreateMutex();
  if (s_http_state_sem == NULL) {
    ESP_LOGE("OTA", "Failed to create HTTP state mutex");
  }
  ui_apply_page_style(parent);
  lv_obj_set_style_pad_all(parent, 8, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  /* 内容超过可视高度时向下滚动的容器 */
  lv_obj_t* container = lv_obj_create(parent);
  lv_obj_set_size(container, 304, 200);
  lv_obj_set_pos(container, 0, 0);
  lv_obj_set_scroll_dir(container, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);

  lv_obj_t* title = ui_create_title(container, "固件中心");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t* request_caption = ui_create_caption(container, "01 / 请求来源");
  lv_obj_set_pos(request_caption, 0, 22);

  s_request_select = lv_dropdown_create(container);
  lv_obj_set_size(s_request_select, 160, 30);
  lv_obj_set_pos(s_request_select, 0, 38);
  lv_dropdown_set_options(s_request_select,
                          "CanDevice1\nCanDevice2\nmyCanDevice1\nmyCanDevice2");
  lv_dropdown_set_selected(s_request_select, 0);
  lv_dropdown_set_dir(s_request_select, LV_DIR_BOTTOM);
  lv_obj_set_style_text_font(s_request_select, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  ui_apply_input_style(s_request_select);
  lv_obj_t* request_list = lv_dropdown_get_list(s_request_select);
  lv_obj_set_style_bg_color(request_list, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_color(request_list, lv_color_hex(0x17212B),
                              LV_PART_MAIN);
  lv_obj_set_style_border_color(request_list, lv_color_hex(0xC9D7DC),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(request_list, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_color(request_list, lv_color_hex(0xE5F0F7),
                            LV_PART_SELECTED);

  lv_obj_t* request = lv_button_create(container);
  lv_obj_set_size(request, 134, 30);
  lv_obj_set_pos(request, 170, 38);
  ui_apply_primary_button_style(request);
  lv_obj_add_event_cb(request, request_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* request_label =
      ui_create_value(request, "查询", ui_font_zh_14());
  lv_obj_set_style_text_color(request_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_center(request_label);

  s_status = ui_create_caption(container, "未查询");
  lv_obj_set_pos(s_status, 0, 72);

  lv_obj_t* version_card = lv_obj_create(container);
  lv_obj_set_size(version_card, 128, 48);
  lv_obj_set_pos(version_card, 0, 92);
  lv_obj_clear_flag(version_card, LV_OBJ_FLAG_SCROLLABLE);
  ui_apply_card_style(version_card);
  lv_obj_t* version_caption = ui_create_caption(version_card, "版本");
  lv_obj_align(version_caption, LV_ALIGN_TOP_LEFT, 0, 0);
  s_version = ui_create_value(version_card, "--", &lv_font_montserrat_16);
  lv_obj_align(s_version, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t* size_card = lv_obj_create(container);
  lv_obj_set_size(size_card, 128, 48);
  lv_obj_set_pos(size_card, 144, 92);
  lv_obj_clear_flag(size_card, LV_OBJ_FLAG_SCROLLABLE);
  ui_apply_card_style(size_card);
  lv_obj_t* size_caption = ui_create_caption(size_card, "大小");
  lv_obj_align(size_caption, LV_ALIGN_TOP_LEFT, 0, 0);
  s_size = ui_create_value(size_card, "--", &lv_font_montserrat_16);
  lv_obj_align(s_size, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t* url_caption = ui_create_caption(container, "下载地址");
  lv_obj_set_pos(url_caption, 0, 148);
  lv_obj_t* url_box = lv_obj_create(container);
  lv_obj_set_size(url_box, 272, 38);
  lv_obj_set_pos(url_box, 0, 166);
  lv_obj_clear_flag(url_box, LV_OBJ_FLAG_SCROLLABLE);
  ui_apply_card_style(url_box);
  s_url = ui_create_value(url_box, "--", &lv_font_montserrat_14);
  lv_obj_set_width(s_url, 254);
  lv_label_set_long_mode(s_url, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_center(s_url);

  s_tag = ui_create_caption(container, "TAG: --");
  lv_obj_set_pos(s_tag, 0, 212);
  s_unix_timestamp = ui_create_caption(container, "UNIX: --");
  lv_obj_set_pos(s_unix_timestamp, 0, 232);

  /* 下载分区独立于固件请求按钮，可选择三个 W25Q64 槽位。 */
  lv_obj_t* target_caption = ui_create_caption(container, "02 / W25 目标");
  lv_obj_set_pos(target_caption, 0, 258);
  s_target = lv_dropdown_create(container);
  lv_obj_set_size(s_target, 160, 32);
  lv_obj_set_pos(s_target, 0, 278);
  lv_dropdown_set_options(
      s_target,
      "CanDevice1.bin\nCanDevice2.bin\nmyCanDevice1.bin\nmyCanDevice2.bin");
  lv_dropdown_set_selected(s_target, 0);
  lv_dropdown_set_dir(s_target, LV_DIR_TOP);
  lv_obj_set_style_text_font(s_target, &lv_font_montserrat_14, LV_PART_MAIN);
  ui_apply_input_style(s_target);
  lv_obj_t* target_list = lv_dropdown_get_list(s_target);
  lv_obj_set_style_bg_color(target_list, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
  lv_obj_set_style_text_color(target_list, lv_color_hex(0x17212B), LV_PART_MAIN);
  lv_obj_set_style_border_color(target_list, lv_color_hex(0xC9D7DC), LV_PART_MAIN);
  lv_obj_set_style_border_width(target_list, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_color(target_list, lv_color_hex(0xE5F0F7), LV_PART_SELECTED);

  /* 下载按钮：使用当前选中的 W25Q64 分区。 */
  lv_obj_t* download = lv_button_create(container);
  lv_obj_set_size(download, 134, 32);
  lv_obj_set_pos(download, 170, 278);
  ui_apply_secondary_button_style(download);
  lv_obj_add_event_cb(download, download_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* download_label =
      ui_create_value(download, "下载", ui_font_zh_14());
  lv_obj_set_style_text_color(download_label, lv_color_hex(0x1677C8), LV_PART_MAIN);
  lv_obj_center(download_label);

  /* 下载进度条与百分比 */
  s_progress_bar = lv_bar_create(container);
  lv_obj_set_size(s_progress_bar, 260, 10);
  lv_obj_set_pos(s_progress_bar, 0, 320);
  lv_bar_set_range(s_progress_bar, 0, 100);
  lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
  ui_apply_progress_style(s_progress_bar, lv_color_hex(0xE98B28));
  s_progress_label = ui_create_value(container, "0%", &lv_font_montserrat_14);
  lv_obj_align(s_progress_label, LV_ALIGN_TOP_RIGHT, 0, 320);

  s_download_status = ui_create_caption(container, "空闲");
  lv_obj_set_pos(s_download_status, 0, 338);

  /* 固定顶部：进入 IAP 子页面（CAN 升级），置于最上层 */
  lv_obj_t* iap = lv_button_create(parent);
  lv_obj_set_size(iap, 66, 26);
  lv_obj_align(iap, LV_ALIGN_TOP_RIGHT, 0, 0);
  ui_apply_secondary_button_style(iap);
  lv_obj_add_event_cb(iap, iap_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* iap_label = ui_create_value(iap, "IAP 升级", ui_font_zh_14());
  lv_obj_set_style_text_color(iap_label, lv_color_hex(0x1677C8), LV_PART_MAIN);
  lv_obj_center(iap_label);

  refresh_target_options();
}

/* 将 HTTP 模块缓存的最近一次查询结果显示到页面，并刷新下载进度。 */
void ui_ota_screen_update(void) {
  if (s_status == NULL) {
    return;
  }

  refresh_target_options();

  bool http_request_running;
  bool http_result_ready;
  http_request_get_state(&http_request_running, &http_result_ready);
  if (http_request_running) {
    lv_label_set_text(s_status, "查询中…");
  } else if (http_result_ready) {
    /* HTTP 任务已结束，读取完整结果，不会与 http_get() 并发访问。 */
    http_ota_info_t info;
    http_get_ota_info(&info);
    if (info.valid) {
      lv_label_set_text_fmt(s_status, "消息: %s", info.message);
      lv_label_set_text_fmt(s_version, "V%d", info.version);
      lv_label_set_text_fmt(s_size, "%d B", info.size);
      lv_label_set_text(s_url, info.url);
      lv_label_set_text_fmt(s_tag, "TAG: %s", info.tag);
      lv_label_set_text_fmt(s_unix_timestamp, "UNIX: %d", info.unix_timestamp);
    } else if (info.http_status != 0) {
      lv_label_set_text_fmt(s_status, "HTTP %d 错误", info.http_status);
    } else {
      lv_label_set_text(s_status, "查询失败");
    }
  }

  /* 始终刷新下载进度与状态 */
  lv_bar_set_value(s_progress_bar, s_download_progress, LV_ANIM_OFF);
  lv_label_set_text_fmt(s_progress_label, "%u%%", s_download_progress);
  lv_label_set_text(s_download_status, s_download_status_buf);
}
