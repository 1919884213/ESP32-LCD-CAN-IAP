#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>

typedef enum {
    WIFI_STATUS_IDLE,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED,
} wifi_status_t;

/* WiFi 默认热点（首次开机自动连接），密钥在 main/secrets.h 中配置 */
#include "secrets.h"
#define WIFI_DEFAULT_SSID     SECRET_WIFI_SSID
#define WIFI_DEFAULT_PASSWORD SECRET_WIFI_PASSWORD

/* Initialize the station once before accepting connection requests. */
void wifi_init(void);

/* Queue a station connection request. SSID and password are copied by this call.
 * The accepted credentials are remembered as the "last" pair for reconnection. */
bool wifi_connect_request(const char *ssid, const char *password);

/* Enable/disable the WiFi radio. Disabling disconnects and stops esp_wifi. */
void wifi_set_enabled(bool enable);
bool wifi_is_enabled(void);

/* Return the last accepted credentials (true if available). Pointers stay
 * valid until the next successful wifi_connect_request(). */
bool wifi_get_last_credentials(const char **ssid, const char **password);

wifi_status_t wifi_get_status(void);

#endif /* WIFI_H */
