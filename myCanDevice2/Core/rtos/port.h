#ifndef TINYRTOS_PORT_H
#define TINYRTOS_PORT_H

#include "stm32f1xx.h"
#include <stdint.h>


#define KERNEL_BASEPRI 0x50u
/**
  进入临界区
*/
static inline uint32_t enter_critical(void) {
  uint32_t b = __get_BASEPRI();
  __set_BASEPRI(KERNEL_BASEPRI);
  return b;
}
/**
  退出临界区
*/
static inline void exit_critical(uint32_t b) { __set_BASEPRI(b); }
extern volatile uint32_t g_tick;

static inline uint32_t os_get_tick(void) { return g_tick; }

#endif /* TINYRTOS_PORT_H */