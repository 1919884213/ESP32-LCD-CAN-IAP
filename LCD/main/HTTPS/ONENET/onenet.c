#include "onenet.h"

#include <stdio.h>
#include <time.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define TAG "mqtt"
#define REPORT_PERIOD_US (5 * 1000 * 1000) /* 上报周期 5s */

static char token[256];
static esp_mqtt_client_handle_t client;
static bool s_mqtt_started;
static volatile bool s_connected;
static esp_timer_handle_t s_report_timer;
static portMUX_TYPE s_data_mux = portMUX_INITIALIZER_UNLOCKED;
static onenet_can_data_t s_can;

static void mqtt_report_can_data(void) {
  if (!s_connected || client == NULL)
    return;

  onenet_can_data_t d;
  portENTER_CRITICAL(&s_data_mux);
  d = s_can;
  portEXIT_CRITICAL(&s_data_mux);

  cJSON* root = cJSON_CreateObject();
  if (root == NULL)
    return;
  cJSON* params = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "id", cJSON_CreateString("123"));
  cJSON_AddItemToObject(root, "version", cJSON_CreateString("1.0"));
  cJSON_AddItemToObject(root, "params", params);

#define ADD_PROP(key, val) \
  do { \
    cJSON* obj = cJSON_CreateObject(); \
    cJSON_AddItemToObject(obj, "value", cJSON_CreateNumber(val)); \
    cJSON_AddItemToObject(params, key, obj); \
  } while (0)

  ADD_PROP("speed", d.speed);
  ADD_PROP("rpm", d.rpm);
  ADD_PROP("roll", d.roll);
  ADD_PROP("pitch", d.pitch);
  ADD_PROP("distance", d.distance);
  ADD_PROP("temperature", d.temperature);
  ADD_PROP("humidity", d.humidity);
  ADD_PROP("light", d.light);
  ADD_PROP("uptime", d.uptime);

#undef ADD_PROP

  char* str = cJSON_PrintUnformatted(root);
  if (str != NULL) {
    ESP_LOGI(TAG, "post: %s", str);
    esp_mqtt_client_publish(client, ONENET_PROP_POST, str, 0, 1, 0);
    free(str);
  }
  cJSON_Delete(root);
}

static void report_timer_cb(void* arg) {
  (void)arg;
  mqtt_report_can_data();
}

static void mqtt_event_handler(void* handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void* event_data) {
  esp_mqtt_event_handle_t event = event_data;
  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "Mqtt Connect!\r\n");
      s_connected = true;
      if (s_report_timer != NULL) {
        esp_timer_start_periodic(s_report_timer, REPORT_PERIOD_US);
      }
      break;
    case MQTT_EVENT_DISCONNECTED:
      s_connected = false;
      if (s_report_timer != NULL) {
        esp_timer_stop(s_report_timer);
      }
      break;
    default:
      ESP_LOGI(TAG, "Other event id:%d", event->event_id);
      break;
  }
}

void mqtt_update_can_data(const onenet_can_data_t* data) {
  if (data == NULL)
    return;
  portENTER_CRITICAL(&s_data_mux);
  s_can = *data;
  portEXIT_CRITICAL(&s_data_mux);
}

bool mqtt_is_connected(void) {
  return s_connected;
}

esp_err_t mqtt_client_init(void) {
  if (s_mqtt_started) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "internal heap free=%d largest=%d",
           (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (int)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  esp_timer_create_args_t timer_args = {
      .callback = report_timer_cb,
      .name = "mqtt_report",
  };
  esp_timer_create(&timer_args, &s_report_timer);

  esp_mqtt_client_config_t cfg = {0};
  cfg.broker.address.uri = ONENET;
  cfg.broker.address.port = PORT;
  cfg.credentials.client_id = DEVICEID;
  cfg.credentials.username = PRODUCTID;
  cfg.task.stack_size = 8192;

  onenet_token_generate("2018-10-31", "products/1SPYOdVhCv/devices/ESP32S3",
                        SECRET_ONENET_ACCESSKeyId, PRODUCTKEY, ONENET_SIGN_SHA256,
                        token, sizeof(token));
  cfg.credentials.authentication.password = token;

  client = esp_mqtt_client_init(&cfg);
  if (client == NULL) {
    return ESP_ERR_NO_MEM;
  }
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                 NULL);
  if (esp_mqtt_client_start(client) != ESP_OK) {
    esp_mqtt_client_destroy(client);
    client = NULL;
    return ESP_FAIL;
  }
  s_mqtt_started = true;
  return ESP_OK;
}
