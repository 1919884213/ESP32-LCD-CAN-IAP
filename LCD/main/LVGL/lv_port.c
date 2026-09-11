#include "lv_port.h"
#include "LCD/Display/Display.h"
#include "LCD/Touch/Touch.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdlib.h>


#define TAG "LVGL"

extern spi_device_handle_t s_spi;

/* 显示缓冲区：双缓冲，每个 1/12 屏，RGB565
 * 双缓冲让 LVGL 渲染 buf1 时 SPI 同时发 buf2，CPU 与 DMA 并行 */
#define DISP_BUF_LINES 20
#define DISP_BUF_SIZE (320 * DISP_BUF_LINES * 4)
static uint8_t *s_disp_buf1 = NULL;
static uint8_t *s_disp_buf2 = NULL;
static bool s_disp_in_psram;

/* 显示刷新回调：把 LVGL 渲染好的 RGB565 数据发到 ILI9341
 * px_map 在 DMA 可访问内存，spi_device_polling_transmit 自动走 DMA */
static void lv_flush_cb(lv_display_t *disp, const lv_area_t *area,
                        uint8_t *px_map) {
  uint16_t x1 = area->x1, y1 = area->y1;
  uint16_t x2 = area->x2, y2 = area->y2;
  uint32_t pixels = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);
  uint32_t total_bytes = pixels * 2;

  Display_SetWindow(x1, y1, x2, y2);
  gpio_set_level(DC, 1);

  /* SPI_MAX_TRANS=32KB，一整块（320×20×2=12.8KB）一次就能传完
   * 超大区域才需分段，用 4KB 对齐分段保证 DMA 地址对齐 */
  const uint32_t MAX_BYTES_PER_TRANS = 32 * 1024;
  uint32_t offset = 0;
  while (offset < total_bytes) {
    uint32_t n = (total_bytes - offset > MAX_BYTES_PER_TRANS)
                     ? MAX_BYTES_PER_TRANS
                     : (total_bytes - offset);
    spi_transaction_t t = {
        .length = n * 8,
        .tx_buffer = px_map + offset,
        .flags = s_disp_in_psram ? SPI_TRANS_DMA_USE_PSRAM : 0,
    };
    spi_device_polling_transmit(s_spi, &t);
    offset += n;
  }
  lv_display_flush_ready(disp);
}
/* 触摸读取回调：使用映射后的屏幕坐标 */
static void lv_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t x, y;
  if (Touch_Read(&x, &y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  data->continue_reading = false;
}

void lv_port_init(void) {
  /* 显示初始化：PSRAM 优先（DMA 直读），失败回退内部 DMA */
  s_disp_buf1 = heap_caps_malloc(DISP_BUF_SIZE, MALLOC_CAP_SPIRAM);
  s_disp_buf2 = heap_caps_malloc(DISP_BUF_SIZE, MALLOC_CAP_SPIRAM);
  if (s_disp_buf1 == NULL || s_disp_buf2 == NULL) {
    heap_caps_free(s_disp_buf1);
    heap_caps_free(s_disp_buf2);
    s_disp_buf1 = heap_caps_malloc(DISP_BUF_SIZE, MALLOC_CAP_DMA);
    s_disp_buf2 = heap_caps_malloc(DISP_BUF_SIZE, MALLOC_CAP_DMA);
    s_disp_in_psram = false;
  } else {
    s_disp_in_psram = true;
  }
  if (s_disp_buf1 == NULL || s_disp_buf2 == NULL) {
    ESP_LOGE(TAG, "display buf malloc failed");
    return;
  }

  lv_display_t *disp = lv_display_create(320, 240);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
  lv_display_set_buffers(disp, s_disp_buf1, s_disp_buf2, DISP_BUF_SIZE,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, lv_flush_cb);

  /* 触摸初始化 */
  Touch_Init();
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lv_touch_read_cb);
  lv_indev_set_display(indev, disp);
  lv_indev_enable(indev, true);
  lv_indev_set_mode(indev, LV_INDEV_MODE_TIMER);

  ESP_LOGI(TAG, "lvgl port init done");
}
