#include <stddef.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "kernel.h"
#include "port.h"
#include "queue.h"

/* =====================================================================
 * CMSIS-RTOS v2 -> TinyRTOS 兼容层
 * ---------------------------------------------------------------------
 * 让用 CMSIS-OS2 API 编写的既有任务（CanTask/MPU6050Task/UltrasonicTask、
 * freertos.c、main.c）无需修改即可跑在 Core/rtos 的 TinyRTOS 上。
 * ===================================================================== */

#define OS_MAX_QUEUES 4
#define OS_QUEUE_ARENA_SIZE 512

static queue_t s_queues[OS_MAX_QUEUES];
static uint8_t s_queue_used[OS_MAX_QUEUES];
static uint8_t s_queue_arena[OS_QUEUE_ARENA_SIZE] __attribute__((aligned(8)));
static uint32_t s_arena_used;

osStatus_t osKernelInitialize(void) { return osOK; }

osStatus_t osKernelStart(void) {
  os_start(); /* 永不返回 */
  return osOK;
}

uint32_t osKernelGetTickCount(void) { return os_get_tick(); }

osStatus_t osDelay(uint32_t ticks) {
  task_delay(ticks);
  return osOK;
}

osStatus_t osDelayUntil(uint32_t ticks) {
  uint32_t now = os_get_tick();
  if ((int32_t)(ticks - now) > 0) {
    task_delay(ticks - now);
  } else {
    os_yield();
  }
  return osOK;
}

/* CMSIS 优先级 (Low=8/Normal=24/High=40...) clamp 到 TinyRTOS 的 0..31 */
osThreadId_t osThreadNew(osThreadFunc_t func, void *argument,
                         const osThreadAttr_t *attr) {
  uint8_t prio = 0;
  if (attr != NULL && (uint32_t)attr->priority > 0) {
    prio = (attr->priority > 31) ? 31 : (uint8_t)attr->priority;
  }
  int idx = task_create((task_fn)func, argument, prio);
  if (idx < 0) {
    return NULL;
  }
  return (osThreadId_t)(uintptr_t)(idx + 1); /* 句柄 = 槽位+1（非 0） */
}

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size,
                                     const osMessageQueueAttr_t *attr) {
  (void)attr;
  for (int i = 0; i < OS_MAX_QUEUES; i++) {
    if (!s_queue_used[i]) {
      uint32_t need = msg_count * msg_size;
      if (s_arena_used + need > OS_QUEUE_ARENA_SIZE) {
        return NULL; /* arena 满 */
      }
      void *buf = &s_queue_arena[s_arena_used];
      s_arena_used += need;
      s_queue_used[i] = 1;
      queue_init(&s_queues[i], buf, msg_size, msg_count);
      return (osMessageQueueId_t)&s_queues[i];
    }
  }
  return NULL; /* 队列池满 */
}

/* ponytail: timeout>0 走无限阻塞版，未实现超时；本项目全部用 timeout=0 */
osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr,
                             uint8_t msg_prio, uint32_t timeout) {
  (void)msg_prio;
  queue_t *q = (queue_t *)mq_id;
  if (q == NULL) {
    return osErrorParameter;
  }
  if (timeout == 0) {
    queue_try_send(q, msg_ptr); /* 非阻塞，满则丢最旧 */
  } else {
    queue_send(q, msg_ptr);
  }
  return osOK;
}

osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr,
                             uint8_t *msg_prio, uint32_t timeout) {
  (void)msg_prio;
  queue_t *q = (queue_t *)mq_id;
  if (q == NULL) {
    return osErrorParameter;
  }
  if (timeout == 0) {
    return queue_try_recv(q, msg_ptr) ? osOK : osErrorResource;
  }
  queue_recv(q, msg_ptr);
  return osOK;
}
