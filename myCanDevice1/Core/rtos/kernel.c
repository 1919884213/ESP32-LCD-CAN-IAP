#include "kernel.h"
#include "port.h"
#include <stdint.h>

/* =====================================================================
 * TinyRTOS 内核核心 —— 就绪队列 / 调度 / 任务管理 / 时基
 * ---------------------------------------------------------------------
 * 就绪队列：位图法 O(1)
 *   ready_bitmap 第 i 位置 1 = 优先级 i 有就绪任务（31 最高，0 最低）
 *   每优先级一条链表 ready_head[i]，同优先级多任务头插串起来，
 *   时间片轮转 = 把队头挪到队尾
 *   找最高就绪优先级 = 最高置位位下标 = 31 - __builtin_clz(bitmap)  ← O(1)
 *   约定：数值越大优先级越高（FreeRTOS 风格），idle 用 0 垫底
 * ===================================================================== */

volatile uint32_t ready_bitmap; /* 就绪位图：bit i=1 → 优先级 i 有就绪任务 */
static tcb_t *ready_head[32];   /* 每优先级一条就绪链表 */

tcb_t tcb_pool[MAX_TASKS];    /* 任务池（静态数组，不 malloc） */
tcb_t *current_tcb = NULL;    /* 当前运行任务 */
volatile uint32_t g_tick = 0; /* 时基计数（os_tick 里 g_tick++）*/

int highest_prio(void) {
  if (ready_bitmap == 0u)
    return 0;
  return 31 - __builtin_clz(ready_bitmap);
}

/* 任务入就绪队列：头插 + 置位。
 * 注意用 cur_prio 而非 prio：互斥量优先级继承时会临时提升优先级，
 * 入队必须按"当前优先级"排队 */
void ready_insert(tcb_t *t) {
  uint8_t p = t->cur_prio;
  t->next = ready_head[p]; /* 头插：新任务放链表头 */
  ready_head[p] = t;
  ready_bitmap |= (1u << p); /* 置位：该优先级有任务了 */
}

/* 任务出就绪队列：链表摘除 + 空则清位。*/
void ready_remove(tcb_t *t) {
  uint8_t p = t->cur_prio;
  tcb_t **pp = &ready_head[p];
  while (*pp && *pp != t) /* 找前驱节点 */
    pp = &(*pp)->next;
  if (*pp) {
    *pp = t->next; /* 前驱的 next 跳过 t，完成摘除 */
    t->next = NULL;
    if (ready_head[p] == NULL)
      ready_bitmap &= ~(1u << p); /* 该优先级空了，清位 */
  }
}

/* 任务当前优先级变更（互斥量优先级继承用）。
 * 若任务在就绪队列（本内核当前运行任务也在就绪链表里），
 * 必须先按【旧 cur_prio】摘除，再改值、按新优先级重插，
 * 否则就绪链表/位图错乱 → 调度器以为它还在老优先级上。
 * 阻塞中的任务不在就绪队列，直接改值即可（唤醒时按新值入队） */
void prio_change(tcb_t *t, uint8_t new_prio) {
  if (t->state == TASK_READY) {
    ready_remove(t);        /* 旧优先级链表摘除（此刻 cur_prio 还是旧值） */
    t->cur_prio = new_prio; /* 更新当前优先级 */
    ready_insert(t);        /* 新优先级链表重插（含置位就绪位图） */
  } else {
    t->cur_prio = new_prio;
  }
}

/* 调度：选最高就绪优先级链表的队头作为 current_tcb。
 * 调用者：PendSV 里（真正的切换点）以及同步原语 os_yield 之后 */
void os_schedule(void) {
  if (ready_bitmap == 0u)
    return; /* 全阻塞：保持当前（等 idle 兜底） */
  current_tcb = ready_head[highest_prio()];
}

/* 静态任务栈：每任务 2KB（STACK_SIZE），编译期分配，不用 malloc（无碎片、确定性） */
static uint32_t task_stacks[MAX_TASKS][STACK_SIZE / sizeof(uint32_t)];
/*伪造现场*/
/* 栈初始化：构造 16 字首帧，伪装成"刚被 PendSV 中断过"。
 * 硬件自动弹 8 字：xPSR / PC / LR / R12 / R3-R0
 * 软件手动弹 8 字：R11-R4（PendSV 里 LDMIA 恢复） */
static void stack_init(tcb_t *t, task_fn entry, void *arg) {
  uint32_t *sp = (uint32_t *)(t->stack_base + STACK_SIZE); /* 从栈顶往下写 */

  /* --- 硬件自动弹出的 8 字（异常返回时） --- */
  *--sp = 0x01000000u; /* xPSR：bit24=T 位，必须 1（Thumb）漏了 HardFault */
  *--sp = ((uint32_t)entry) |
          1u; /* PC：任务入口地址，OR 1 强制 Thumb 位（bit0=1） */
  *--sp = 0u; /* LR：任务不返回 */
  *--sp = 0u; /* R12 */
  *--sp = 0u;
  *--sp = 0u;
  *--sp = 0u;            /* R3 R2 R1 */
  *--sp = (uint32_t)arg; /* R0 = 任务参数 */
  /* --- PendSV 里 LDMIA 恢复的 8 字 --- */
  *--sp = 0u;
  *--sp = 0u;
  *--sp = 0u;
  *--sp = 0u; /* R11 R10 R9 R8 */
  *--sp = 0u;
  *--sp = 0u;
  *--sp = 0u;
  *--sp = 0u; /* R7  R6  R5  R4 */
  t->sp = sp; /* 保存 sp（指向 R4 保存区） */
}

/* 创建任务：找空槽（sp==NULL）→ 填 TCB → 造首帧 → 入就绪队列。
 * 返回值：槽位号 0~MAX_TASKS-1，失败 -1 */
int task_create(task_fn entry, void *arg, uint8_t prio) {
  if (prio > 31)
    prio = 31;
  for (int i = 0; i < MAX_TASKS; i++) {
    tcb_t *t = &tcb_pool[i];
    if (t->sp == NULL) { /* 空闲槽位（stack_init 会设 sp，NULL=没用过） */
      t->prio = t->cur_prio = prio;
      t->state = TASK_READY;
      t->slice = 10; /* 默认 10 tick 时间片 */
      t->delay_ticks = 0;
      t->next = NULL;
      t->stack_base = (uint32_t)task_stacks[i];
      stack_init(t, entry, arg);
      uint32_t b = enter_critical(); /* 入队要进临界区（防被 SysTick 打断） */
      ready_insert(t);
      exit_critical(b);
      return i;
    }
  }
  return -1; /* 任务池满 */
}

/* 主动让出 CPU 一次：悬起 PendSV 请求调度（当前任务仍在就绪队列） */
void task_delete(tcb_t *task) {
  uint32_t key = enter_critical();

  if (task == NULL)
    task = current_tcb;

  if (task == NULL || task->state == TASK_FREE || task->state == TASK_DYING) {
    exit_critical(key);
    return;
  }

  if (task != current_tcb && task->state != TASK_READY) {
    exit_critical(key);
    return;
  }

  /* Only READY tasks can be deleted externally; wait queues need extra unlinking. */
  if (task->state == TASK_READY)
    ready_remove(task);

  task->state = TASK_DYING;
  task->delay_ticks = 0;

  if (task == current_tcb) {
    exit_critical(key);
    os_yield();
    while (1) {
    }
  }

  exit_critical(key);
}

void task_exit(void) {
  task_delete(NULL);
  while (1) {
  }
}

void os_yield(void) {
  SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; /*挂机pendSV,请求CPU执行任务切换*/
}

/* 阻塞延时：改状态 + 摘除就绪 + 让出 CPU。
 * 倒计时由 SysTick 的 os_tick 完成，归零后被重新唤醒 */
void task_delay(uint32_t ticks) {
  if (ticks == 0) { /* 0 = 纯让出，不阻塞 */
    os_yield();
    return;
  }
  uint32_t b = enter_critical();
  current_tcb->state = TASK_BLOCKED;
  current_tcb->delay_ticks = ticks;
  ready_remove(
      current_tcb); /* 从就绪摘除，别忘了！漏了会被调度两次 → 栈帧撕裂 */
  exit_critical(b);
  os_yield(); /* 悬起 PendSV 切走 */
}

/**
  每次SysTick_handler都会调用os_tick (1ms一次)
  SysTick 时基（1ms）：抢占引擎，每毫秒干三件事
*/
void os_tick(void) {
  g_tick++; /* 时基计数器 +1（os_get_tick 读的就是它） */

  /* ① 唤醒延时到期的任务：倒计时归零 → 重新入就绪队列 */
  for (int i = 0; i < MAX_TASKS; i++) {
    tcb_t *t = &tcb_pool[i];
    if (t->state == TASK_BLOCKED && t->delay_ticks) {
      if (--t->delay_ticks == 0) {
        t->state = TASK_READY;
        ready_insert(t); /* 唤醒 = 入队（不是 remove！） */
      }
    }
  }

  /* ② 时间片轮转：时间片耗尽 → 队头挪到队尾（同优先级公平） */
  if (current_tcb && current_tcb->slice) {
    if (--current_tcb->slice == 0) {
      current_tcb->slice = 10;
      uint8_t p = current_tcb->cur_prio;
      tcb_t *h = ready_head[p];
      if (h && h->next) {
        ready_head[p] = h->next; /*  先把队头摘下来  */
        h->next = NULL;
        tcb_t **pp = &ready_head[p];
        while (*pp) /* 找到队尾 */
          pp = &(*pp)->next;
        *pp = h; /* 原队头接到队尾 */
      }
    }
  }

  /* ③ 抢占判断：有更高优先级就绪？只悬起 PendSV，不直接切换（防重入） */
  if (ready_bitmap != 0u && ready_head[highest_prio()] != current_tcb)
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

/* SysTick 中断服务：强符号，覆盖 startup 里的 weak 默认处理 */
void SysTick_Handler(void) { os_tick(); }

/* 空闲任务（最低优先级 0）：所有任务阻塞时兜底运行，避免调度器
 * 保持一个已阻塞任务导致 task_delay 失效。WFI 休眠，等 SysTick 唤醒 */
static void idle_task(void *arg) {
  (void)arg;
  for (;;) {
    __WFI();
  }
}

/* 启动内核（永不返回）：配优先级 + 启动 SysTick + 预选首任务 + SVC 启动 */
void os_start(void) {
  __disable_irq(); /* 配置期间关中断 */

  task_create(idle_task, NULL, 0); /* 兜底空闲任务（优先级 0） */

  NVIC_SetPriority(PendSV_IRQn, 15); /* PendSV 必须最低优先级 */
  NVIC_SetPriority(SVCall_IRQn, 15); /* SVC 也最低（仅启动用一次） */

  /* SysTick 1ms 时基。设为最低优先级 15，与 PendSV 同级：
   * 二者都会被临界区 BASEPRI(0x50) 一起屏蔽，避免 os_tick 与就绪链表竞态 */
  SysTick_Config(SystemCoreClock / 1000u);
  NVIC_SetPriority(SysTick_IRQn, 15);

  /* ★ 预选第一个任务，并显式设好 PSP/CONTROL */
  os_schedule();                        /* current_tcb = 最高就绪任务 */
  __set_PSP((uint32_t)current_tcb->sp); /* PSP = 首任务栈 */
  __set_CONTROL(0x02);                  /* 线程模式用 PSP（SPSEL=1） */
  __ISB();

  __enable_irq();
  __asm volatile("svc 0"); /* 触发 SVC_Handler 恢复首帧 → 首个任务开跑 */

  while (1)
    ;
}
