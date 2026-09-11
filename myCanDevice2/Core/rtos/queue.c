#include "queue.h"
#include "kernel.h"
#include "port.h"
#include <string.h>

void queue_init(queue_t *q, void *buf, uint32_t msg_size, uint32_t size) {
  q->buf = buf; q->size = size; q->msg_size = msg_size;
  q->head = 0; q->tail = 0; q->count = 0;
  q->r_wait = NULL; q->w_wait = NULL;
}

/* send：满则一直等，腾出空位才写 */
void queue_send(queue_t *q, const void *msg) {
  uint32_t b = enter_critical();
  while (q->count == q->size) {           /* 满 → 循环等空位 */
    current_tcb->state = TASK_BLOCKED;
    ready_remove(current_tcb);
    current_tcb->next = q->w_wait;
    q->w_wait = current_tcb;
    exit_critical(b);
    os_yield();                           /* 被唤醒后回到 while 重新检查 */
    b = enter_critical();                 /* ★ 重新进临界区 */
  }
  memcpy((uint8_t*)q->buf + q->tail * q->msg_size, msg, q->msg_size);
  q->tail = (q->tail + 1) % q->size;
  q->count++;
  if (q->r_wait != NULL) { /* 唤醒读等待者（等读的人现在有数据了） */
    tcb_t *t = q->r_wait;
    q->r_wait = t->next;
    t->next = NULL;
    t->state = TASK_READY;
    ready_insert(t);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
  }
  exit_critical(b);
}

/* recv：空则一直等，有数据才读 */
void queue_recv(queue_t *q, void *msg) {
  uint32_t b = enter_critical();
  while (q->count == 0) {                 /* 空 → 循环等数据 */
    current_tcb->state = TASK_BLOCKED;
    ready_remove(current_tcb);
    current_tcb->next = q->r_wait;
    q->r_wait = current_tcb;
    exit_critical(b);
    os_yield();
    b = enter_critical();                 /* ★ 重新进临界区 */
  }
  memcpy(msg, (uint8_t*)q->buf + q->head * q->msg_size, q->msg_size);
  q->head = (q->head + 1) % q->size;
  q->count--;
  if (q->w_wait != NULL) { /* 唤醒写等待者（等写的人现在有空位了） */
    tcb_t *t = q->w_wait;
    q->w_wait = t->next;
    t->next = NULL;
    t->state = TASK_READY;
    ready_insert(t);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
  }
  exit_critical(b);
}

/* try_recv：空则立即返回 0（不阻塞），有数据则读走并返回 1 */
int queue_try_recv(queue_t *q, void *msg) {
  uint32_t b = enter_critical();
  if (q->count == 0) {
    exit_critical(b);
    return 0;
  }
  memcpy(msg, (uint8_t*)q->buf + q->head * q->msg_size, q->msg_size);
  q->head = (q->head + 1) % q->size;
  q->count--;
  if (q->w_wait != NULL) { /* 唤醒写等待者（等写的人现在有空位了） */
    tcb_t *t = q->w_wait;
    q->w_wait = t->next;
    t->next = NULL;
    t->state = TASK_READY;
    ready_insert(t);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
  }
  exit_critical(b);
  return 1;
}

/* try_send：非阻塞发送。满时丢弃最旧一条（邮箱保留最新值），恒返回 1 */
int queue_try_send(queue_t *q, const void *msg) {
  uint32_t b = enter_critical();
  if (q->count == q->size) { /* 满：丢最旧，腾出尾位 */
    q->head = (q->head + 1) % q->size;
    q->count--;
  }
  memcpy((uint8_t*)q->buf + q->tail * q->msg_size, msg, q->msg_size);
  q->tail = (q->tail + 1) % q->size;
  q->count++;
  if (q->r_wait != NULL) { /* 唤醒读等待者 */
    tcb_t *t = q->r_wait;
    q->r_wait = t->next;
    t->next = NULL;
    t->state = TASK_READY;
    ready_insert(t);
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
  }
  exit_critical(b);
  return 1;
}