#ifndef TINYRTOS_KERNEL_H
#define TINYRTOS_KERNEL_H

#include "tcb.h"

int highest_prio(void);
/**
  创建任务
  entry:入口函数
  arg:传递给函数的参数
  prio:任务优先级
*/
int task_create(task_fn entry, void *arg, uint8_t prio);
void task_delete(tcb_t *task);
void task_exit(void);
/**
  任务延时函数 (CPU去干其他任务)
*/
void task_delay(uint32_t ticks);
/**
  主动让出CPU,再主动触发调度
*/
void os_yield(void);
void os_start(void);
void os_schedule(void);
/**
  SysTick 时基中断的处理函数（每 1ms 被调一次）——它是整个**"抢占式"的引擎**
*/
void os_tick(void);
/**
  将任务加入就绪队列
*/
void ready_insert(tcb_t *t);
/**
  将任务移除就绪队列
*/
void ready_remove(tcb_t *t);
/** 调整任务当前优先级（互斥量优先级继承用），会同步就绪队列位置 */
void prio_change(tcb_t *t, uint8_t new_prio);

#endif /* TINYRTOS_KERNEL_H */
