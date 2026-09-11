#include "Environment.h"
#include "CANProtocol.h"
#include "DHT11.h"
#include "GL5528.h"
#include "NTC.h"

static void IncrementSaturating(uint8_t *value) {
  if (*value < UINT8_MAX) {
    (*value)++;
  }
}

uint8_t Environment_Init(void) {
  DHT11_Init();
  GL5528_Init();
  NTC_Init();
  return 0U;
}

void Environment_Read(Environment_Data_t *data, uint8_t read_dht11) {
  DHT11_Data_t dht;
  uint16_t light_raw;
  uint16_t ntc_raw;

  if (data == NULL) {
    return;
  }

  if (read_dht11 != 0U) {
    DHT11_Status_t dht_status = DHT11_Read(&dht);
    data->status &= (uint8_t)~(ENV_STATUS_DHT11_OK |
                              ENV_STATUS_DHT11_TIMEOUT |
                              ENV_STATUS_DHT11_CHECKSUM);
    if (dht_status == DHT11_OK) {
      data->temperature_x100 =
          (int16_t)((uint16_t)dht.temp_int * 100U +
                    (uint16_t)dht.temp_dec * 10U);
      data->humidity_x100 =
          (uint16_t)((uint16_t)dht.humidity_int * 100U +
                     (uint16_t)dht.humidity_dec * 10U);
      data->status |= ENV_STATUS_DHT11_OK;
      data->dht_error_count = 0U;
    } else {
      data->temperature_x100 = ENV_TEMP_INVALID;
      data->humidity_x100 = ENV_HUMI_INVALID;
      data->status |= dht_status == DHT11_ERR_TIMEOUT
                          ? ENV_STATUS_DHT11_TIMEOUT
                          : ENV_STATUS_DHT11_CHECKSUM;
      IncrementSaturating(&data->dht_error_count);
    }
  }

  data->status &= (uint8_t)~(ENV_STATUS_LDR_OK | ENV_STATUS_ADC_INVALID);
  if (GL5528_ReadAvg(&light_raw) == GL5528_OK) {
    data->light_mv = GL5528_RawToMv(light_raw);
    data->status |= ENV_STATUS_LDR_OK;
    data->light_error_count = 0U;
  } else {
    data->light_mv = ENV_LDR_INVALID;
    data->status |= ENV_STATUS_ADC_INVALID;
    IncrementSaturating(&data->light_error_count);
  }

  data->status &= (uint8_t)~ENV_STATUS_NTC_OK;
  if (NTC_ReadAvg(&ntc_raw) == NTC_OK) {
    data->ntc_mv = NTC_RawToMv(ntc_raw);
    data->status |= ENV_STATUS_NTC_OK;
    data->ntc_error_count = 0U;
  } else {
    data->ntc_mv = ENV_NTC_INVALID;
    data->status |= ENV_STATUS_ADC_INVALID;
    IncrementSaturating(&data->ntc_error_count);
  }
}
