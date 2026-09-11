#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>

typedef struct {
  uint8_t humidity_int;
  uint8_t humidity_dec;
  uint8_t temp_int;
  uint8_t temp_dec;
  uint8_t checksum;
} DHT11_Data_t;

typedef enum {
  DHT11_OK = 0,
  DHT11_ERR_TIMEOUT,
  DHT11_ERR_CHECKSUM,
} DHT11_Status_t;

void DHT11_Init(void);
DHT11_Status_t DHT11_Read(DHT11_Data_t *data);

#endif /* DHT11_H */
