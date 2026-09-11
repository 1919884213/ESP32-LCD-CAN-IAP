#ifndef GL5528_H
#define GL5528_H

#include <stdint.h>

#define GL5528_ADC_CHANNEL     ADC_CHANNEL_3
#define GL5528_AVG_SAMPLES      8U
#define GL5528_VREF_MV          3300U
#define GL5528_ADC_FULL_SCALE   4095U

typedef enum {
  GL5528_OK = 0,
  GL5528_ERR,
} GL5528_Status_t;

void GL5528_Init(void);
GL5528_Status_t GL5528_ReadRaw(uint16_t *value);
GL5528_Status_t GL5528_ReadAvg(uint16_t *value);
uint16_t GL5528_RawToMv(uint16_t raw);

#endif /* GL5528_H */
