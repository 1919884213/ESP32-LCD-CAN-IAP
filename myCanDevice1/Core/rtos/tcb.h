#ifndef __TCB_H__
#define __TCB_H__

#include <stdint.h>

#define MAX_TASKS 5
#define STACK_SIZE 1024 /* 每任务 1KB 栈（F103 共 20KB RAM，5 任务=5KB） */

#define TASK_FREE 0
#define TASK_READY 1
#define TASK_BLOCKED 2
#define TASK_DYING 3

typedef void (*task_fn)(void *args);

typedef struct tcb {
  uint32_t *sp;         /*保存的栈指针*/
  uint8_t prio;         /*基础优先级*/
  uint8_t cur_prio;     /*当前优先级*/
  uint8_t state;        /*任务状态*/
  uint32_t delay_ticks; /*剩余阻塞tick*/
  uint32_t slice;       /*时间片剩余*/
  struct tcb *next;     /*链表节点*/
  uint32_t stack_base;  /*栈底*/
} tcb_t;

extern tcb_t tcb_pool[MAX_TASKS];
extern tcb_t *current_tcb;

#endif /* __TCB_H__ */
