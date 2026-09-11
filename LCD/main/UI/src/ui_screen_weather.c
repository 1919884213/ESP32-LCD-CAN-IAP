#include "ui_screen_weather.h"
#include "ui.h"
#include "ui_helpers.h"
#include "weather.h"
#include "../asset/weather_icons.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static lv_obj_t *s_condition;
static lv_obj_t *s_weather_icon;
static lv_obj_t *s_temperature;
static lv_obj_t *s_humidity;
static lv_obj_t *s_status;
static lv_obj_t *s_aqi_value;
static lv_obj_t *s_aqi_category;
static lv_obj_t *s_primary;
static lv_obj_t *s_pm25;
static lv_obj_t *s_pm10;
static lv_obj_t *s_no2;
static lv_obj_t *s_o3;
static lv_obj_t *s_co;
static SemaphoreHandle_t s_weather_mutex;
static weather_t s_weather;
static Air_t s_air;
static esp_err_t s_air_request_result;
static char s_raw_json[WEATHER_RESPONSE_MAX];
static bool s_request_running;
static bool s_result_ready;
static bool s_raw_json_displayed;
static esp_err_t s_request_result = ESP_FAIL;
static esp_err_t s_air_request_result = ESP_FAIL;

#define TAG "WEATHER"
#define WEATHER_TASK_STACK_WORDS 16384U

static void log_weather_heap(void)
{
    ESP_LOGI(TAG, "heap before request: internal=%u largest=%u psram=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(
                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static lv_obj_t *create_weather_card(lv_obj_t *parent, lv_coord_t x,
                                     lv_coord_t y, lv_coord_t width,
                                     lv_coord_t height)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_pos(card, x, y);
    ui_apply_card_style(card);
    return card;
}

static void create_temperature_icon(lv_obj_t *parent)
{
    lv_obj_t *stem = lv_obj_create(parent);
    lv_obj_set_size(stem, 5, 16);
    lv_obj_set_pos(stem, 9, 4);
    lv_obj_set_style_bg_color(stem, lv_color_hex(0xE05A47), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(stem, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(stem, LV_RADIUS_CIRCLE, LV_PART_MAIN);

    lv_obj_t *bulb = lv_obj_create(parent);
    lv_obj_set_size(bulb, 11, 11);
    lv_obj_set_pos(bulb, 6, 17);
    lv_obj_set_style_bg_color(bulb, lv_color_hex(0xE05A47), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bulb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bulb, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bulb, LV_RADIUS_CIRCLE, LV_PART_MAIN);
}

static void create_humidity_icon(lv_obj_t *parent)
{
    lv_obj_t *icon = lv_label_create(parent);
    lv_label_set_text(icon, LV_SYMBOL_TINT);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x1677C8), LV_PART_MAIN);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_pos(icon, 5, 6);
}

/* 返回主页 */
static void weather_home_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_tabview_set_active(ui_TabView, UI_PAGE_DRIVE, LV_ANIM_OFF);
}

static const lv_image_dsc_t *weather_icon_for_condition(const char *condition)
{
    if (condition == NULL) {
        return &weather_sunny;
    }
    if (strstr(condition, "雷") != NULL) {
        return &weather_thunder;
    }
    if (strstr(condition, "雪") != NULL ||
        strstr(condition, "冰") != NULL ||
        strstr(condition, "霰") != NULL) {
        return &weather_snow;
    }
    if (strstr(condition, "暴") != NULL ||
        strstr(condition, "大") != NULL) {
        return &weather_heavy_rain;
    }
    if (strstr(condition, "雨") != NULL ||
        strstr(condition, "阵") != NULL ||
        strstr(condition, "流") != NULL ||
        strstr(condition, "淋") != NULL) {
        return &weather_rain;
    }
    if (strstr(condition, "雾") != NULL ||
        strstr(condition, "霾") != NULL ||
        strstr(condition, "凇") != NULL ||
        strstr(condition, "浮") != NULL ||
        strstr(condition, "尘") != NULL ||
        strstr(condition, "沙") != NULL) {
        return &weather_fog;
    }
    if (strstr(condition, "阴") != NULL) {
        return &weather_overcast;
    }
    if (strstr(condition, "云") != NULL) {
        return &weather_cloudy;
    }
    return &weather_sunny;
}

static void weather_request_task(void *arg)
{
    (void)arg;
    weather_t weather = {0};
    Air_t air = {0};
    char response[WEATHER_RESPONSE_MAX] = {0};
    char air_response[AIR_RESPONSE_MAX] = {0};
    esp_err_t result = get_weather(WEATHER_URL, &weather, response);
    if (result == ESP_OK && weather.weatherData.text[0] == '\0') {
        result = ESP_FAIL;
    }
    const esp_err_t air_result = get_air(AIR_URL, &air, air_response);

    if (xSemaphoreTake(s_weather_mutex, portMAX_DELAY) == pdPASS) {
        memcpy(s_raw_json, response, sizeof(s_raw_json));
        if (result == ESP_OK) {
            s_weather = weather;
        }
        s_air = air;
        s_air_request_result = air_result;
        s_request_result = result;
        s_result_ready = true;
        s_request_running = false;
        xSemaphoreGive(s_weather_mutex);
    }
    vTaskDelete(NULL);
}

void ui_weather_screen_init(lv_obj_t *parent)
{
    ui_apply_page_style(parent);
    lv_obj_set_style_pad_all(parent, 8, LV_PART_MAIN);

    lv_obj_t *title = ui_create_title(parent, "天气");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    s_status = ui_create_caption(parent, "左滑返回");
    lv_obj_align(s_status, LV_ALIGN_TOP_RIGHT, -56, 3);

    /* 右上角返回主页按钮 */
    lv_obj_t *home = lv_button_create(parent);
    lv_obj_set_size(home, 52, 24);
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, 0, 0);
    ui_apply_secondary_button_style(home);
    lv_obj_add_event_cb(home, weather_home_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *home_label = ui_create_value(home, "首页", ui_font_zh_14());
    lv_obj_set_style_text_color(home_label, lv_color_hex(0x1677C8),
                                LV_PART_MAIN);
    lv_obj_center(home_label);

    /* 三张卡片集中在屏幕上半区，下半区留空 */
    lv_obj_t *condition_card = create_weather_card(parent, 0, 30, 304, 48);
    s_weather_icon = lv_image_create(condition_card);
    lv_image_set_src(s_weather_icon, &weather_sunny);
    lv_image_set_scale(s_weather_icon, 160); /* 64px 图标缩到 40px */
    lv_obj_set_pos(s_weather_icon, 4, 4);
    lv_obj_t *condition_caption = ui_create_caption(condition_card, "天气状况");
    lv_obj_set_pos(condition_caption, 52, 1);
    s_condition = ui_create_value(condition_card, "--", &font_cn_weather_24);
    lv_obj_set_width(s_condition, 240);
    lv_label_set_long_mode(s_condition, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(s_condition, 52, 20);

    lv_obj_t *temperature_card = create_weather_card(parent, 0, 86, 146, 42);
    lv_obj_t *temperature_caption = ui_create_caption(temperature_card, "温度");
    lv_obj_set_pos(temperature_caption, 22, 3);
    create_temperature_icon(temperature_card);
    s_temperature = ui_create_value(temperature_card, "--.- C",
                                    &lv_font_montserrat_16);
    lv_obj_align(s_temperature, LV_ALIGN_BOTTOM_RIGHT, 0, -3);

    lv_obj_t *humidity_card = create_weather_card(parent, 158, 86, 146, 42);
    lv_obj_t *humidity_caption = ui_create_caption(humidity_card, "湿度");
    lv_obj_set_pos(humidity_caption, 22, 3);
    create_humidity_icon(humidity_card);
    s_humidity = ui_create_value(humidity_card, "--.- %RH",
                                 &lv_font_montserrat_16);
    lv_obj_align(s_humidity, LV_ALIGN_BOTTOM_RIGHT, 0, -3);

    /* 空气质量卡片 */
    lv_obj_t *air_card = create_weather_card(parent, 0, 136, 304, 50);
    lv_obj_t *air_caption = ui_create_caption(air_card, "空气质量");
    lv_obj_set_pos(air_caption, 4, 0);
    s_primary = ui_create_caption(air_card, "--");
    lv_obj_set_width(s_primary, 196);
    lv_label_set_long_mode(s_primary, LV_LABEL_LONG_DOT);
    lv_obj_align(s_primary, LV_ALIGN_TOP_RIGHT, -4, 0);
    s_aqi_value = ui_create_value(air_card, "--", &lv_font_montserrat_20);
    lv_obj_align(s_aqi_value, LV_ALIGN_BOTTOM_LEFT, 4, 0);
    s_aqi_category = ui_create_value(air_card, "--", ui_font_zh_16());
    lv_obj_align(s_aqi_category, LV_ALIGN_BOTTOM_LEFT, 78, 2);

    /* 污染物浓度卡片 */
    lv_obj_t *pollutants_card = create_weather_card(parent, 0, 190, 304, 42);
    s_pm25 = ui_create_caption(pollutants_card, "--");
    lv_obj_set_pos(s_pm25, 4, 0);
    s_pm10 = ui_create_caption(pollutants_card, "--");
    lv_obj_set_pos(s_pm10, 104, 0);
    s_no2 = ui_create_caption(pollutants_card, "--");
    lv_obj_set_pos(s_no2, 204, 0);
    s_o3 = ui_create_caption(pollutants_card, "--");
    lv_obj_set_pos(s_o3, 4, 18);
    s_co = ui_create_caption(pollutants_card, "--");
    lv_obj_set_pos(s_co, 104, 18);

    s_weather_mutex = xSemaphoreCreateMutex();
    if (s_weather_mutex == NULL) {
        lv_label_set_text(s_status, "天气初始化失败");
    }
}

void ui_weather_screen_request(void)
{
    if (s_weather_mutex == NULL ||
        xSemaphoreTake(s_weather_mutex, 0) != pdPASS) {
        return;
    }
    if (!s_request_running) {
        log_weather_heap();
        s_request_running = true;
        s_result_ready = false;
        s_raw_json_displayed = false;
        s_request_result = ESP_FAIL;
        s_air_request_result = ESP_FAIL;
        if (xTaskCreateWithCaps(weather_request_task, "weather",
                                WEATHER_TASK_STACK_WORDS, NULL, 5, NULL,
                                MALLOC_CAP_SPIRAM) != pdPASS) {
            s_request_running = false;
            s_result_ready = true;
        }
    }
    xSemaphoreGive(s_weather_mutex);
}

void ui_weather_screen_update(void)
{
    if (s_status == NULL || s_weather_mutex == NULL ||
        xSemaphoreTake(s_weather_mutex, 0) != pdPASS) {
        return;
    }

    const bool request_running = s_request_running;
    const bool result_ready = s_result_ready;
    const esp_err_t request_result = s_request_result;
    const esp_err_t air_request_result = s_air_request_result;
    const weather_t weather = s_weather;
    const Air_t air = s_air;
    if (result_ready && !s_raw_json_displayed) {
        ESP_LOGI(TAG, "RAW JSON: %s",
                 s_raw_json[0] != '\0' ? s_raw_json : "--");
        s_raw_json_displayed = true;
    }
    xSemaphoreGive(s_weather_mutex);

    if (request_running) {
        lv_label_set_text(s_status, "更新中…");
        return;
    }
    if (!result_ready) {
        lv_label_set_text(s_status, "左滑返回");
        return;
    }
    if (request_result != ESP_OK) {
        lv_label_set_text(s_status, "天气请求失败");
        return;
    }

    lv_label_set_text(s_status, "已更新");
    lv_label_set_text(s_condition, weather.weatherData.text);
    lv_image_set_src(s_weather_icon,
                     weather_icon_for_condition(weather.weatherData.text));
    const int temperature_x10 = (int)(weather.weatherData.t_value * 10.0 +
                                      (weather.weatherData.t_value >= 0.0 ? 0.5 : -0.5));
    const int humidity_x10 = (int)(weather.weatherData.h_value * 10.0 + 0.5);
    lv_label_set_text_fmt(s_temperature, "%d.%d C", temperature_x10 / 10,
                          temperature_x10 >= 0 ? temperature_x10 % 10
                                              : -temperature_x10 % 10);
    lv_label_set_text_fmt(s_humidity, "%d.%d %%RH", humidity_x10 / 10,
                          humidity_x10 % 10);

    /* 空气质量：AQI 指数 + 等级 + 首要污染物 + 各污染物浓度 */
    if (air_request_result != ESP_OK) {
        lv_label_set_text(s_aqi_value, "--");
        lv_label_set_text(s_aqi_category, "--");
        lv_label_set_text(s_primary, "--");
        lv_label_set_text(s_pm25, "--");
        lv_label_set_text(s_pm10, "--");
        lv_label_set_text(s_no2, "--");
        lv_label_set_text(s_o3, "--");
        lv_label_set_text(s_co, "--");
        return;
    }

    const lv_color_t air_color = lv_color_make(air.AirData.red, air.AirData.green,
                                               air.AirData.blue);
    lv_obj_set_style_text_color(s_aqi_value, air_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_aqi_category, air_color, LV_PART_MAIN);
    lv_label_set_text_fmt(s_aqi_value, "%d", air.AirData.aqi);
    lv_label_set_text(s_aqi_category, air.AirData.category);
    lv_label_set_text_fmt(s_primary, "首要污染物: %s",
                          air.AirData.primaryPollutant);

    const int pm25_x10 = (int)(air.AirData.pm2p5 * 10.0 + 0.5);
    const int pm10_x10 = (int)(air.AirData.pm10 * 10.0 + 0.5);
    const int no2_x100 = (int)(air.AirData.no2 * 100.0 + 0.5);
    const int o3_x100 = (int)(air.AirData.o3 * 100.0 + 0.5);
    const int co_x100 = (int)(air.AirData.co * 100.0 + 0.5);
    lv_label_set_text_fmt(s_pm25, "PM2.5 %d.%d", pm25_x10 / 10, pm25_x10 % 10);
    lv_label_set_text_fmt(s_pm10, "PM10 %d.%d", pm10_x10 / 10, pm10_x10 % 10);
    lv_label_set_text_fmt(s_no2, "NO2 %d.%02d", no2_x100 / 100, no2_x100 % 100);
    lv_label_set_text_fmt(s_o3, "O3 %d.%02d", o3_x100 / 100, o3_x100 % 100);
    lv_label_set_text_fmt(s_co, "CO %d.%02d", co_x100 / 100, co_x100 % 100);
}
