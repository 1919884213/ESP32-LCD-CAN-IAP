#include "NTC.h"
#include "adc.h"

extern ADC_HandleTypeDef hadc2;

void NTC_Init(void) {
  /* ADC2 (PA2 -> ADC2_IN2) is configured by CubeMX in MX_ADC2_Init().
   * Nothing extra required here; each read reconfigures for single-shot. */
}

NTC_Status_t NTC_ReadRaw(uint16_t *value) {
  uint16_t v = 0U;
  ADC_ChannelConfTypeDef sConfig = {0};

  sConfig.Channel = NTC_ADC_CHANNEL;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK) {
    return NTC_ERR;
  }

  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.ContinuousConvMode = DISABLE;
  HAL_ADC_Init(&hadc2);

  if (HAL_ADC_Start(&hadc2) != HAL_OK) {
    return NTC_ERR;
  }
  if (HAL_ADC_PollForConversion(&hadc2, 100U) != HAL_OK) {
    HAL_ADC_Stop(&hadc2);
    return NTC_ERR;
  }
  v = (uint16_t)HAL_ADC_GetValue(&hadc2);
  HAL_ADC_Stop(&hadc2);

  *value = v;
  return NTC_OK;
}

NTC_Status_t NTC_ReadAvg(uint16_t *value) {
  uint32_t sum = 0U;
  uint8_t i;
  uint16_t v;

  for (i = 0U; i < NTC_AVG_SAMPLES; i++) {
    if (NTC_ReadRaw(&v) != NTC_OK) {
      return NTC_ERR;
    }
    sum += v;
  }
  *value = (uint16_t)(sum / NTC_AVG_SAMPLES);
  return NTC_OK;
}

uint16_t NTC_RawToMv(uint16_t raw) {
  return (uint16_t)(((uint32_t)raw * NTC_VREF_MV) / NTC_ADC_FULL_SCALE);
}
