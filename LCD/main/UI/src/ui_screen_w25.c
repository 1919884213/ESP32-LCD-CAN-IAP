#include "ui_screen_w25.h"
#include "ui_helpers.h"
#include "W25Q64.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* W25 页显示行：STM32 逻辑槽合并 A/B，只显示当前活动半区。 */
typedef enum {
  W25_DISP_META = 0,
  W25_DISP_STM32_1,
  W25_DISP_STM32_2,
  W25_DISP_STM32_3,
  W25_DISP_STM32_4,
  W25_DISP_ESP32S3,
  W25_DISP_COMMON,
  W25_DISP_COUNT,
} w25_disp_t;

/* 无有效镜像时的回退名称。 */
static const char *s_disp_names[W25_DISP_COUNT] = {
    [W25_DISP_META] = "META",
    [W25_DISP_STM32_1] = "STM32_1",
    [W25_DISP_STM32_2] = "STM32_2",
    [W25_DISP_STM32_3] = "STM32_3",
    [W25_DISP_STM32_4] = "STM32_4",
    [W25_DISP_ESP32S3] = "ESP32S3",
    [W25_DISP_COMMON] = "COMMON",
};

/* 每行槽位的显示控件：名称+起始地址、容量+占比、占用进度条。 */
typedef struct {
  lv_obj_t *name;
  lv_obj_t *size;
  lv_obj_t *bar;
} w25_row_t;

static w25_row_t s_rows[W25_DISP_COUNT];
/* 上次刷新名称时的 revision，用于避免每 tick 重复 set_text。 */
static uint32_t s_name_rev = 0xFFFFFFFFU;

/* 显示行对应的物理槽；STM32 行返回当前活动半区。 */
static W25Q64_Slot_t disp_active_slot(uint32_t disp) {
  if (disp >= W25_DISP_STM32_1 && disp <= W25_DISP_STM32_4) {
    W25Q64_Slot_t slot = W25Q64_SLOT_STM32_1_A;
    W25Q64_GetActiveSlot((W25Q64_FwSlot_t)(disp - W25_DISP_STM32_1), &slot);
    return slot;
  }
  if (disp == W25_DISP_ESP32S3) {
    return W25Q64_SLOT_ESP32S3;
  }
  if (disp == W25_DISP_COMMON) {
    return W25Q64_SLOT_COMMON;
  }
  return W25Q64_SLOT_META;
}

/* 更新 STM32 行名称：使用固定映射的 bin 文件名。 */
static void update_stm32_name(uint32_t disp) {
  lv_label_set_text(s_rows[disp].name,
                    ui_firmware_name(disp - W25_DISP_STM32_1));
}

/* 创建 W25Q64 占用情况页面：标题、总容量摘要与七个逻辑槽位行。 */
void ui_w25_screen_init(lv_obj_t *parent) {
  ui_apply_page_style(parent);
  lv_obj_set_style_pad_all(parent, 8, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = ui_create_title(parent, "外部存储");
  lv_obj_set_pos(title, 0, 0);

  lv_obj_t *total = ui_create_caption(parent, "W25Q64 / 8 MiB");
  lv_obj_align(total, LV_ALIGN_TOP_RIGHT, 0, 0);

  for (uint32_t i = 0; i < W25_DISP_COUNT; ++i) {
    const lv_coord_t y = 26 + (lv_coord_t)i * 28;

    /* 槽名（A/B 分区合并后显示固定 bin 名，过长省略） */
    s_rows[i].name = ui_create_caption(parent, s_disp_names[i]);
    lv_obj_set_width(s_rows[i].name, 195);
    lv_label_set_long_mode(s_rows[i].name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_rows[i].name, 0, y);

    /* 容量 + 占总容量百分比（右对齐） */
    s_rows[i].size = ui_create_value(parent, "", ui_font_zh_14());
    lv_obj_align(s_rows[i].size, LV_ALIGN_TOP_RIGHT, 0, y);

    /* 占用进度条：已写入长度占分区容量的比例 */
    s_rows[i].bar = lv_bar_create(parent);
    lv_obj_set_size(s_rows[i].bar, 210, 4);
    lv_obj_set_pos(s_rows[i].bar, 0, y + 16);
    lv_bar_set_range(s_rows[i].bar, 0, 100);
    lv_bar_set_value(s_rows[i].bar, 0, LV_ANIM_OFF);
    ui_apply_progress_style(s_rows[i].bar, lv_color_hex(0x0F9B8E));
  }

  ui_w25_screen_update();
}

/* 刷新 W25Q64 占用情况：更新每个槽位的名称、容量占比文本与进度条。 */
void ui_w25_screen_update(void) {
  if (s_rows[0].name == NULL) {
    return;
  }

  const uint32_t rev = W25Q64_GetNameRevision();
  const bool refresh_names = rev != s_name_rev;
  s_name_rev = rev;

  for (uint32_t i = 0; i < W25_DISP_COUNT; ++i) {
    const W25Q64_Slot_t slot = disp_active_slot(i);
    W25Q64_SlotInfo_t info;
    if (W25Q64_GetSlotInfo(slot, &info) != ESP_OK) {
      continue;
    }

    if (i == W25_DISP_META) {
      lv_label_set_text(s_rows[i].size, "元数据");
      lv_bar_set_value(s_rows[i].bar, 0, LV_ANIM_OFF);
      continue;
    }
    if (i == W25_DISP_COMMON) {
      char text[24];
      snprintf(text, sizeof(text), "%" PRIu32 "K", info.size / 1024U);
      lv_label_set_text(s_rows[i].size, text);
      lv_bar_set_value(s_rows[i].bar, 0, LV_ANIM_OFF);
      continue;
    }

    if (refresh_names && i >= W25_DISP_STM32_1 && i <= W25_DISP_STM32_4) {
      update_stm32_name(i);
    }

    uint32_t used_size = 0U;
    const esp_err_t err = W25Q64_GetUsetageInfo(slot, &used_size);
    if (err == ESP_ERR_NOT_FOUND) {
      used_size = 0U;
    } else if (err != ESP_OK) {
      lv_label_set_text(s_rows[i].size, "读取错误");
      lv_bar_set_value(s_rows[i].bar, 0, LV_ANIM_OFF);
      continue;
    }

    const uint32_t pct = info.size == 0U
                             ? 0U
                             : (uint32_t)(((uint64_t)used_size * 100ULL) /
                                          info.size);
    char value_str[32];
    snprintf(value_str, sizeof(value_str), "%" PRIu32 "K/%" PRIu32 "K %u%%",
             used_size / 1024U, info.size / 1024U, (unsigned)pct);
    lv_label_set_text(s_rows[i].size, value_str);
    lv_bar_set_value(s_rows[i].bar, (int32_t)pct, LV_ANIM_OFF);
  }
}
