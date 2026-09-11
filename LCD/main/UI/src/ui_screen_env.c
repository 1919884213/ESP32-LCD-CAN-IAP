#include "ui_screen_env.h"
#include "ui_data.h"
#include "ui_helpers.h"

/* 环境页各控件指针：数值标签、进度条、状态 */
static lv_obj_t *s_temp;
static lv_obj_t *s_humidity;
static lv_obj_t *s_light;
static lv_obj_t *s_distance;
static lv_obj_t *s_light_bar;
static lv_obj_t *s_distance_bar;
static lv_obj_t *s_status;

/* 创建一个带标题的卡片，返回卡片对象用于后续布局 */
static lv_obj_t *create_card(lv_obj_t *parent, const char *caption,
                             lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 146, 64);
    lv_obj_set_pos(card, x, y);
    ui_apply_card_style(card);
    lv_obj_t *label = ui_create_caption(card, caption);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

/* 创建一个进度条（量程 0~max，置于卡片右下角） */
static lv_obj_t *create_bar(lv_obj_t *parent, int32_t max)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 62, 8);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_RIGHT, 0, -5);
    lv_bar_set_range(bar, 0, max);
    ui_apply_progress_style(bar, lv_color_hex(0x0F9B8E));
    return bar;
}

/* 创建环境页面控件：温度/湿度/光照/距离卡片与进度条 */
void ui_env_screen_init(lv_obj_t *parent)
{
    ui_apply_page_style(parent);
    lv_obj_set_style_pad_all(parent, 8, LV_PART_MAIN);

    lv_obj_t *title = ui_create_title(parent, "环境");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    s_status = ui_create_caption(parent, "节点离线");
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, 0, 1);

    /* 温度卡片 */
    lv_obj_t *temp_card = create_card(parent, "温度", 0, 25);
    s_temp = ui_create_value(temp_card, "--.- C", &lv_font_montserrat_16);
    lv_obj_align(s_temp, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* 湿度卡片 */
    lv_obj_t *hum_card = create_card(parent, "湿度", 158, 25);
    s_humidity = ui_create_value(hum_card, "-- %RH", &lv_font_montserrat_16);
    lv_obj_align(s_humidity, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* 光照卡片（带进度条） */
    lv_obj_t *light_card = create_card(parent, "光照", 0, 101);
    s_light = ui_create_value(light_card, "-- %", &lv_font_montserrat_16);
    lv_obj_align(s_light, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    s_light_bar = create_bar(light_card, 100);

    /* 距离卡片（带进度条） */
    lv_obj_t *distance_card = create_card(parent, "距离", 158, 101);
    s_distance = ui_create_value(distance_card, "--- cm", &lv_font_montserrat_16);
    lv_obj_align(s_distance, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    s_distance_bar = create_bar(distance_card, 400);
}

/* 刷新环境页面：读取共享数据并更新各数值与进度条 */
void ui_env_screen_update(void)
{
    if (s_temp == NULL) return;

    ui_env_data_t data;
    ui_get_environment_data(&data);
    uint8_t light = data.light_percent > 100U ? 100U : data.light_percent;
    uint16_t distance = data.distance_cm > 400U ? 400U : data.distance_cm;

    /* 使用整数缩放显示，避免精简 printf 未启用浮点格式支持。 */
    int temp_x100 = (int)(data.temperature_c * 100.0f +
                          (data.temperature_c >= 0.0f ? 0.5f : -0.5f));
    int humidity_x100 = (int)(data.humidity_percent * 100.0f + 0.5f);
    lv_label_set_text_fmt(s_temp, "%d.%02d C", temp_x100 / 100,
                          temp_x100 >= 0 ? temp_x100 % 100 : -temp_x100 % 100);
    lv_label_set_text_fmt(s_humidity, "%d.%02d %%RH", humidity_x100 / 100,
                          humidity_x100 % 100);
    lv_label_set_text_fmt(s_light, "%u %%", light);
    lv_bar_set_value(s_light_bar, light, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_distance, "%u cm", data.distance_cm);
    lv_bar_set_value(s_distance_bar, distance, LV_ANIM_OFF);
    /* 超出量程（>400cm）时距离条指示色变红提示，量程内恢复橙色 */
    lv_obj_set_style_bg_color(s_distance_bar,
                              data.distance_cm > 400U ? lv_color_hex(0xCF4C4C)
                                                      : lv_color_hex(0x1677C8),
                              LV_PART_INDICATOR);
}
