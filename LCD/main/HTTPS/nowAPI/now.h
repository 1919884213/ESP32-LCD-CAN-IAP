#ifndef NOW_H
#define NOW_H

#include <stdbool.h>
#include <stdint.h>

#define name1 "pool.ntp.org"
#define name2 "cn.pool.ntp.org"
#define name3 "ntp1.aliyun.com"

/* 启动 SNTP 同步（内部防重复初始化，仅在 WiFi 连接后调用有效） */
void now_sync_start(void);

/* 读取本地时间（东八区）。未同步（年份 < 2020）时返回 false */
bool now_get_datetime(uint16_t *year, uint8_t *month, uint8_t *day,
                      uint8_t *hour, uint8_t *minute, uint8_t *second);

#endif /* NOW_H */
