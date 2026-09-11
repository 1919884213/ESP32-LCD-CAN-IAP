#include "sem.h"
#include "kernel.h"
#include <stdint.h>
#include <stdio.h>
#include "port.h"
void sem_init(sem_t *s, uint32_t init_count) {
  s->count = init_count;
  s->wait_head = NULL;
}
void sem_take(sem_t *s) {
  uint32_t b = enter_critical();
  /*有资源拿,拿走资源直接运行*/
  if (s->count > 0) {
    s->count--;
    exit_critical(b);
    return;
  }
  /*无资源拿就阻塞等待*/
  else {
    current_tcb->state = TASK_BLOCKED; /*任务切换到阻塞态*/
    /* ★ 顺序关键：必须先摘就绪，再挂等待队列！
     *   ready_remove 要用 t->next 修补就绪链表，若先改 next 去串等待队列，
     *   会把等待队列头写进就绪链表 → 位图不清 → 任务被假唤醒（count 乱变 1）*/
    ready_remove(current_tcb);         /*① 先摘就绪（此刻 next 还是就绪链表的正确 next）*/
    current_tcb->next = s->wait_head;  /*② 再头插到等待链表（之后才覆写 next）*/
    s->wait_head = current_tcb;        /*③ 更新等待队列头*/
    exit_critical(b);                  /*退出临界区*/
    os_yield();                        /*触发一次调度*/
    return;
  }
  exit_critical(b);
}

void sem_give(sem_t *s) {
  uint32_t b = enter_critical();
  if (s->wait_head != NULL) {          /* 有等着的人 */
    tcb_t *temp = s->wait_head;        /* ① 捞出等待者（队头） */
    s->wait_head = temp->next;         /* ② 摘除队头 */
    temp->next = NULL;                 /* ③ 清 next */
    temp->state = TASK_READY;          /* ④ 叫醒的是 temp */
    ready_insert(temp);                /* ⑤ temp 回就绪队列 */
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; /* ⑥ 申请调度（temp 优先级高会来抢占） */
  } else {
    s->count++;                        /* 没人等，资源存起来 */
  }
  exit_critical(b);
}