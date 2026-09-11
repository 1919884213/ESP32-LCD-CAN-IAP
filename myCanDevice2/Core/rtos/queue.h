#ifndef __QUEUE_H__
#define __QUEUE_H__
#include "tcb.h"
#include <stdint.h>

typedef struct queue {
    void     *buf;
    uint32_t  size;
    uint32_t  msg_size;
    uint32_t  head;
    uint32_t  tail;
    uint32_t  count;
    tcb_t    *r_wait;
    tcb_t    *w_wait;
} queue_t;

void queue_init(queue_t *q, void *buf, uint32_t msg_size, uint32_t size);
void queue_send(queue_t *q, const void *msg);
void queue_recv(queue_t *q, void *msg);
/* 非阻塞接收：有数据返回 1 并拷贝，空返回 0（不阻塞） */
int queue_try_recv(queue_t *q, void *msg);
/* 非阻塞发送：满时丢弃最旧一条（保留最新值），返回 1 */
int queue_try_send(queue_t *q, const void *msg);
#endif