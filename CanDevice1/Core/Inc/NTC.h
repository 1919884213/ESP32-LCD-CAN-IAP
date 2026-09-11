#ifndef NTC_H
#define NTC_H

#include <stdint.h>

#define NTC_ADC_CHANNEL     ADC_CHANNEL_2
#define NTC_AVG_SAMPLES     8U
#define NTC_VREF_MV         3300U
#define NTC_ADC_FULL_SCALE  4095U

typedef enum {
  NTC_OK = 0,
  NTC_ERR,
} NTC_Status_t;

void NTC_Init(void);
NTC_Status_t NTC_ReadRaw(uint16_t *value);
NTC_Status_t NTC_ReadAvg(uint16_t *value);
uint16_t NTC_RawToMv(uint16_t raw);

#endif /* NTC_H */
