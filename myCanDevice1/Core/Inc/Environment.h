#ifndef __ENVIRONMENT_H__
#define __ENVIRONMENT_H__

#include "main.h"

typedef struct {
  int16_t temperature_x100;
  uint16_t humidity_x100;
  uint16_t light_mv;
  uint16_t ntc_mv;
  uint8_t status;
  uint8_t dht_error_count;
  uint8_t light_error_count;
  uint8_t ntc_error_count;
} Environment_Data_t;

uint8_t Environment_Init(void);
void Environment_Read(Environment_Data_t *data, uint8_t read_dht11);

#endif
