#include "GL5528.h"
#include "adc.h"

extern ADC_HandleTypeDef hadc1;

void GL5528_Init(void) {
  /* ADC1 (PA3 -> ADC1_IN3) is configured by CubeMX in MX_ADC1_Init().
   * Nothing extra required here; each read reconfigures for single-shot. */
}

GL5528_Status_t GL5528_ReadRaw(uint16_t *value) {
  uint16_t v = 0U;
  ADC_ChannelConfTypeDef sConfig = {0};

  sConfig.Channel = GL5528_ADC_CHANNEL;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
    return GL5528_ERR;
  }

  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.ContinuousConvMode = DISABLE;
  HAL_ADC_Init(&hadc1);

  if (HAL_ADC_Start(&hadc1) != HAL_OK) {
    return GL5528_ERR;
  }
  if (HAL_ADC_PollForConversion(&hadc1, 100U) != HAL_OK) {
    HAL_ADC_Stop(&hadc1);
    return GL5528_ERR;
  }
  v = (uint16_t)HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);

  *value = v;
  return GL5528_OK;
}

GL5528_Status_t GL5528_ReadAvg(uint16_t *value) {
  uint32_t sum = 0U;
  uint8_t i;
  uint16_t v;

  for (i = 0U; i < GL5528_AVG_SAMPLES; i++) {
    if (GL5528_ReadRaw(&v) != GL5528_OK) {
      return GL5528_ERR;
    }
    sum += v;
  }
  *value = (uint16_t)(sum / GL5528_AVG_SAMPLES);
  return GL5528_OK;
}

uint16_t GL5528_RawToMv(uint16_t raw) {
  return (uint16_t)(((uint32_t)raw * GL5528_VREF_MV) / GL5528_ADC_FULL_SCALE);
}
