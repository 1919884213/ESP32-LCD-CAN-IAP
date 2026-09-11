#ifndef CMSIS_OS2_H_
#define CMSIS_OS2_H_

/*
 * Minimal CMSIS-RTOS v2 subset implemented on top of TinyRTOS (os_compat.c).
 * Only the API actually used by this project is declared here, so the existing
 * task sources (written against CMSIS-RTOS2 / FreeRTOS) compile unchanged.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t osStatus_t;
#define osOK             (0)
#define osError          (-1)
#define osErrorResource  (-2)
#define osErrorParameter (-3)

typedef void *osThreadId_t;
typedef void (*osThreadFunc_t)(void *argument);

typedef enum {
  osPriorityNone = 0,
  osPriorityIdle = 1,
  osPriorityLow = 8,
  osPriorityBelowNormal = 16,
  osPriorityNormal = 24,
  osPriorityAboveNormal = 32,
  osPriorityHigh = 40,
  osPriorityRealtime = 48
} osPriority_t;

typedef struct {
  const char *name;
  uint32_t attr_bits;
  void *cb_mem;
  uint32_t cb_size;
  void *stack_mem;
  uint32_t stack_size;
  osPriority_t priority;
  uint32_t tz_module;
  uint32_t reserved;
} osThreadAttr_t;

typedef void *osMessageQueueId_t;

typedef struct {
  const char *name;
  uint32_t attr_bits;
  void *cb_mem;
  uint32_t cb_size;
  void *mq_mem;
  uint32_t mq_size;
} osMessageQueueAttr_t;

#define osWaitForever 0xFFFFFFFFU

osStatus_t osKernelInitialize(void);
osStatus_t osKernelStart(void);
uint32_t osKernelGetTickCount(void);

osStatus_t osDelay(uint32_t ticks);
osStatus_t osDelayUntil(uint32_t ticks);

osThreadId_t osThreadNew(osThreadFunc_t func, void *argument,
                         const osThreadAttr_t *attr);

osMessageQueueId_t osMessageQueueNew(uint32_t msg_count, uint32_t msg_size,
                                     const osMessageQueueAttr_t *attr);
osStatus_t osMessageQueuePut(osMessageQueueId_t mq_id, const void *msg_ptr,
                             uint8_t msg_prio, uint32_t timeout);
osStatus_t osMessageQueueGet(osMessageQueueId_t mq_id, void *msg_ptr,
                             uint8_t *msg_prio, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* CMSIS_OS2_H_ */
