#include "WiFi.h"
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

typedef struct {
  char ssid[33];
  char password[65];
} wifi_credentials_t;

static QueueHandle_t s_connect_queue;
static volatile wifi_status_t s_status = WIFI_STATUS_IDLE;
static volatile bool s_ignore_disconnect;
static volatile bool s_enabled = true;

/* 最近一次成功入队的凭据：控制中心 WiFi 开关用它重连 */
static char s_last_ssid[33];
static char s_last_password[65];
static bool s_has_last;

static void wifi_connect_task(void *arg) {
  wifi_credentials_t credentials;
  while (true) {
    if (xQueueReceive(s_connect_queue, &credentials, portMAX_DELAY) != pdTRUE) continue;

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, credentials.ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, credentials.password,
            sizeof(config.sta.password));
    s_status = WIFI_STATUS_CONNECTING;
    /* 射频可能被控制中心关闭，先确保已启动（幂等） */
    s_enabled = true;
    esp_wifi_start();
    s_ignore_disconnect = esp_wifi_disconnect() == ESP_OK;
    esp_wifi_set_config(WIFI_IF_STA, &config);
    esp_wifi_connect();

    memcpy(s_last_ssid, credentials.ssid, sizeof(s_last_ssid));
    memcpy(s_last_password, credentials.password, sizeof(s_last_password));
    s_has_last = true;
  }
}

void event_callback(void* event_handler_arg,
                    esp_event_base_t event_base,
                    int32_t event_id,
                    void* event_data) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    s_status = WIFI_STATUS_CONNECTED;
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_ignore_disconnect) {
      s_ignore_disconnect = false;
    } else if (s_status == WIFI_STATUS_CONNECTING) {
      s_status = WIFI_STATUS_FAILED;
    }
  }
}

void wifi_init(void) {
  if (s_connect_queue != NULL) return;

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&wifi_init_config);

  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_callback,
                             NULL);
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, event_callback, NULL);

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_start();
  s_connect_queue = xQueueCreate(1, sizeof(wifi_credentials_t));
  xTaskCreate(wifi_connect_task, "wifi_connect", 4096, NULL, 5, NULL);
}

bool wifi_connect_request(const char *ssid, const char *password) {
  wifi_credentials_t credentials = {0};
  if (s_connect_queue == NULL || ssid == NULL || password == NULL ||
      ssid[0] == '\0' || strlen(ssid) > 32U || strlen(password) > 64U) {
    return false;
  }
  strlcpy(credentials.ssid, ssid, sizeof(credentials.ssid));
  strlcpy(credentials.password, password, sizeof(credentials.password));
  return xQueueOverwrite(s_connect_queue, &credentials) == pdPASS;
}

bool wifi_get_last_credentials(const char **ssid, const char **password) {
  if (!s_has_last) return false;
  *ssid = s_last_ssid;
  *password = s_last_password;
  return true;
}

void wifi_set_enabled(bool enable) {
  if (enable == s_enabled) return;
  s_enabled = enable;
  if (!enable) {
    /* 主动关闭：忽略随后上报的断连事件，避免误判为连接失败 */
    s_ignore_disconnect = esp_wifi_disconnect() == ESP_OK;
    esp_wifi_stop();
    s_status = WIFI_STATUS_IDLE;
  } else {
    esp_wifi_start();
  }
}

bool wifi_is_enabled(void) { return s_enabled; }

wifi_status_t wifi_get_status (void) {
  return s_status;
}
