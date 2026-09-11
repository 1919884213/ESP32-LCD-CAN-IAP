#ifndef __SEM_H__
#define __SEM_H__

#include "tcb.h"
#include <stdint.h>

typedef struct sem{
  uint32_t count;
  tcb_t *wait_head; /*等待队列头：等这个信号量的任务，用 t->next 串成链表*/
}sem_t;

void sem_init(sem_t *s,uint32_t init_count);
void sem_take(sem_t *s);/*P操作*/
void sem_give(sem_t *s);/*V操作*/

#endif /* __SEM_H__ */