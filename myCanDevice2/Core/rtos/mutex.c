#include "mutex.h"
#include "kernel.h"
#include "port.h" /* enter_critical / exit_critical / SCB */
#include <stdint.h>

void mutex_init(mutex_t *m) {
  m->owner = NULL;
  m->nest = 0;
  m->prio = 0;
  m->wait_head = NULL;
}

/* P 操作：获取互斥量。
 * 拿不到就阻塞，并触发【优先级继承】：
 *   把我的（更高）优先级临时借给持有者，让持有者赶紧跑完释放锁，
 *   从而消灭"高优先级任务被低优先级任务间接拖住"的优先级反转。
 * 是否启用继承由编译宏 MUTEX_INHERIT 控制（CMakeLists.txt 里开关）。 */
void mutex_take(mutex_t *m) {
  uint32_t b = enter_critical();

  /* ① 没人持有 → 直接拿走，登记持有者 */
  if (m->owner == NULL) {
    m->owner = current_tcb;
    m->nest = 1;
    exit_critical(b);
    return;
  }

  /* ② 自己已经持有（递归拿同一把锁）→ 计数 +1，直接放行 */
  if (m->owner == current_tcb) {
    m->nest++;
    exit_critical(b);
    return;
  }

  /* ③ 被别的任务持有 → 阻塞等待 */
  tcb_t *owner = m->owner; /* 记住持有者，一会儿可能要把优先级借给它 */
  current_tcb->state = TASK_BLOCKED;
  /* ★ 顺序关键（同信号量）：先摘就绪（用旧 next 修补就绪链表），
   *   再覆写 next 去串等待队列。反了会把等待队列写进就绪链表 → 假唤醒 */
  ready_remove(current_tcb);
  current_tcb->next = m->wait_head;
  m->wait_head = current_tcb;

#ifdef MUTEX_INHERIT
  /* ★ 优先级继承：我的基础优先级 > 持有者当前优先级 → 提升持有者。
   *   持有者被提上来后变成"最高就绪"，调度器马上选中它，让它释放锁 */
  if (current_tcb->prio > owner->cur_prio) {
    if (current_tcb->prio > m->prio)
      m->prio = current_tcb->prio; /* 记录本锁继承到的最高优先级 */
    prio_change(owner, current_tcb->prio); /* 提升（就绪链表位置跟着挪） */
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;   /* 悬起 PendSV：抢占让位给提升后的持有者 */
  }
#endif

  exit_critical(b);
  os_yield(); /* 切走，等持有者释放锁 */
}

/* V 操作：释放互斥量。
 * 有等待者 → 把锁【直接交接】给队首等待者；
 * 没等待者 → 锁置为空闲。
 * 无论哪种都要【恢复持有者的基础优先级】（继承到此结束）并触发调度。 */
void mutex_give(mutex_t *m) {
  uint32_t b = enter_critical();

  /* 没持有 / 不是持有者释放 → 错误用法，忽略 */
  if (m->owner == NULL || m->owner != current_tcb) {
    exit_critical(b);
    return;
  }

  /* 递归锁：计数没减到 0 还不算真释放 */
  if (--m->nest > 0) {
    exit_critical(b);
    return;
  }

#ifdef MUTEX_INHERIT
  /* ① 恢复持有者优先级（继承结束）。注意就绪队列位置要同步挪回去 */
  prio_change(m->owner, m->owner->prio);
#endif

  /* ② 有等待者 → 锁直接交接给队首等待者（它醒来即为新持有者） */
  if (m->wait_head != NULL) {
    tcb_t *w = m->wait_head;
    m->wait_head = w->next;
    w->next = NULL;
    w->state = TASK_READY;
    ready_insert(w);
    m->owner = w; /* 锁交接给 w */
    m->nest = 1;
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; /* 叫醒 w（若它优先级更高会来抢占） */
  } else {
    /* ③ 没人等 → 锁空闲 */
    m->owner = NULL;
  }
  m->prio = 0; /* 继承记录清零 */

  exit_critical(b);
}