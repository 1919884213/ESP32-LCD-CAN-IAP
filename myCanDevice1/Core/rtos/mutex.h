#ifndef __MUTEX_H__
#define __MUTEX_H__

#include "tcb.h" /* tcb_t / uint8_t 从这里来 */
#include <stdint.h>

typedef struct mutex {
  tcb_t *owner;     // 谁持有这把锁？NULL = 没人持有
  uint8_t nest;     // 递归计数（同一任务重复拿锁次数）
  uint8_t prio;     // 本锁给持有者继承到的最高优先级（0 = 没发生过继承）
  tcb_t *wait_head; // 等待队列 —— 复用信号量那套 t->next 链表
} mutex_t;

void mutex_init(mutex_t *mutex);
void mutex_take(mutex_t *mutex);
void mutex_give(mutex_t *mutex);

#endif /* __MUTEX_H__ */