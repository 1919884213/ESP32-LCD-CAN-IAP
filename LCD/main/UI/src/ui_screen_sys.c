#include "ui_screen_sys.h"
#include "ui_helpers.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>

/* 每行资源的显示控件：名称、数值文本、占用进度条。 */
typedef struct {
  lv_obj_t *name;
  lv_obj_t *value;
  lv_obj_t *bar;
} sys_row_t;

typedef enum {
  SYS_ROW_CPU0 = 0,
  SYS_ROW_CPU1,
  SYS_ROW_HEAP,
  SYS_ROW_PSRAM,
  SYS_ROW_LVGL,
  SYS_ROW_COUNT,
} sys_row_id_t;

static sys_row_t s_rows[SYS_ROW_COUNT];
static lv_obj_t *s_fps_label;

static const char *s_row_names[SYS_ROW_COUNT] = {
    [SYS_ROW_CPU0] = "CPU0",
    [SYS_ROW_CPU1] = "CPU1",
    [SYS_ROW_HEAP] = "内部堆",
    [SYS_ROW_PSRAM] = "PSRAM",
    [SYS_ROW_LVGL] = "LVGL",
};

#if configGENERATE_RUN_TIME_STATS
/* 上次 CPU 采样：空闲计数与采样时刻，用于计算 1s 内的占用率。 */
static uint32_t s_last_idle[portNUM_PROCESSORS];
static TickType_t s_last_tick;
#else
static TickType_t s_last_tick;
#endif
/* 渲染完成帧计数：用于计算真实 FPS。 */
static volatile uint32_t s_render_frames;

static void render_frame_event_cb(lv_event_t *event) {
  (void)event;
  ++s_render_frames;
}

#if configGENERATE_RUN_TIME_STATS
/* 读取指定核心空闲任务的运行时计数器。 */
static uint32_t sys_get_idle_counter(int core) {
  return core == 0 ? ulTaskGetIdleRunTimeCounter()
                   : ulTaskGetIdleRunTimeCounterForCore(core);
}
#endif

/* 创建一行资源：名称 + 数值 + 进度条。 */
static void create_row(lv_obj_t *parent, sys_row_id_t id, lv_coord_t y) {
  sys_row_t *row = &s_rows[id];
  row->name = ui_create_caption(parent, s_row_names[id]);
  lv_obj_set_pos(row->name, 0, y);
  row->value = ui_create_value(parent, "--", ui_font_zh_14());
  lv_obj_align(row->value, LV_ALIGN_TOP_RIGHT, 0, y);
  row->bar = lv_bar_create(parent);
  lv_obj_set_size(row->bar, 150, 4);
  lv_obj_set_pos(row->bar, 72, y + 6);
  lv_bar_set_range(row->bar, 0, 100);
  lv_bar_set_value(row->bar, 0, LV_ANIM_OFF);
  ui_apply_progress_style(row->bar, lv_color_hex(0x1677C8));
}

/* 更新一行的百分比数值与进度条。 */
static void set_row_pct(sys_row_id_t id, unsigned pct,
                        const char *extra_text) {
  if (pct > 100U) {
    pct = 100U;
  }
  char value_str[40];
  snprintf(value_str, sizeof(value_str), "%u%%%s", pct,
           extra_text != NULL ? extra_text : "");
  lv_label_set_text(s_rows[id].value, value_str);
  lv_bar_set_value(s_rows[id].bar, (int32_t)pct, LV_ANIM_OFF);
}

/* 创建 MCU 资源占用页面：标题、FPS 与五行资源。 */
void ui_sys_screen_init(lv_obj_t *parent) {
  ui_apply_page_style(parent);
  lv_obj_set_style_pad_all(parent, 8, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = ui_create_title(parent, "系统资源");
  lv_obj_set_pos(title, 0, 0);

  s_fps_label = ui_create_caption(parent, "FPS --");
  lv_obj_align(s_fps_label, LV_ALIGN_TOP_RIGHT, 0, 0);

  /* 挂接显示刷新完成事件，统计真实渲染帧率 */
  lv_display_add_event_cb(lv_display_get_default(), render_frame_event_cb,
                          LV_EVENT_FLUSH_FINISH, NULL);

  for (int i = 0; i < SYS_ROW_COUNT; ++i) {
    create_row(parent, (sys_row_id_t)i, 32 + (lv_coord_t)i * 40);
  }

#if !configGENERATE_RUN_TIME_STATS
  /* 未开启运行时统计时无法计算 CPU 占用，直接显示提示。 */
  lv_label_set_text(s_rows[SYS_ROW_CPU0].value, "未启用");
  lv_label_set_text(s_rows[SYS_ROW_CPU1].value, "未启用");
#endif

  s_last_tick = xTaskGetTickCount();
}

/* 刷新资源占用：每 1s 采样一次空闲计数与内存。 */
void ui_sys_screen_update(void) {
  if (s_rows[0].name == NULL) {
    return;
  }

  static uint32_t s_last_frames = 0;

  TickType_t now = xTaskGetTickCount();
  if ((now - s_last_tick) < pdMS_TO_TICKS(1000)) {
    return;
  }

  /* FPS：按真实渲染帧数与 elapsed 时间计算。 */
  const uint32_t frames = s_render_frames;
  const uint32_t elapsed_ms =
      (uint32_t)((now - s_last_tick) * portTICK_PERIOD_MS);
  if (elapsed_ms > 0U) {
    const uint32_t fps = (frames - s_last_frames) * 1000U / elapsed_ms;
    char fps_str[24];
    snprintf(fps_str, sizeof(fps_str), "FPS %" PRIu32, fps);
    lv_label_set_text(s_fps_label, fps_str);
  }
  s_last_frames = frames;
  s_last_tick = now;

#if configGENERATE_RUN_TIME_STATS
  const uint32_t idle_now[portNUM_PROCESSORS] = {
      sys_get_idle_counter(0), sys_get_idle_counter(1)};
  const uint32_t idle_delta[portNUM_PROCESSORS] = {
      idle_now[0] - s_last_idle[0], idle_now[1] - s_last_idle[1]};
  s_last_idle[0] = idle_now[0];
  s_last_idle[1] = idle_now[1];

  /* 运行时计数在双核间共享计数基准，取两核空闲量中较小者作为
   * 1s 时间片的基准，保证占用率不超过 100%。 */
  const uint32_t total =
      idle_delta[0] > idle_delta[1] ? idle_delta[0] : idle_delta[1];
  if (total > 0U) {
    for (int core = 0; core < portNUM_PROCESSORS; ++core) {
      const uint32_t busy = total - idle_delta[core];
      const unsigned pct =
          (unsigned)((uint64_t)busy * 100ULL / total);
      set_row_pct((sys_row_id_t)(SYS_ROW_CPU0 + core), pct, "");
    }
  }
#endif

  /* 内部堆：已用 = 总量 - 剩余。 */
  const uint32_t heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const uint32_t heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  if (heap_total > heap_free) {
    const unsigned pct = (unsigned)(((uint64_t)(heap_total - heap_free) *
                                     100ULL) / heap_total);
    char extra[24];
    snprintf(extra, sizeof(extra), " %" PRIu32 "K空", heap_free / 1024U);
    set_row_pct(SYS_ROW_HEAP, pct, extra);
  }

  /* PSRAM：已用 = 总量 - 剩余。 */
  const uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  const uint32_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (psram_total > 0U) {
    const unsigned pct = (unsigned)(((uint64_t)(psram_total - psram_free) *
                                     100ULL) / psram_total);
    char extra[24];
    snprintf(extra, sizeof(extra), " %" PRIu32 "K空", psram_free / 1024U);
    set_row_pct(SYS_ROW_PSRAM, pct, extra);
  }

  /* LVGL 内部堆：lv_mem 使用率。 */
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  set_row_pct(SYS_ROW_LVGL, mon.used_pct, "");
}
