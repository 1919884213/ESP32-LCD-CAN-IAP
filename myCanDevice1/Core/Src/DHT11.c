#include "DHT11.h"
#include "main.h"
#include "port.h"

#define DHT11_PIN             GPIO_PIN_11
#define DHT11_PORT            GPIOB

#define DHT11_START_LOW_US    20000U   /* >= 18ms */
#define DHT11_START_HIGH_US   30U      /* 20 ~ 40us */
#define DHT11_PULSE_TIMEOUT_US 300U    /* single pulse timeout */
#define DHT11_BIT1_THRESH_US  50U      /* high>50us => '1', else '0' */

static uint32_t DHT11_Micros(void);
static void DHT11_DelayUs(uint32_t us);
static uint32_t DHT11_WaitLevel(GPIO_PinState level, uint32_t timeoutUs);
static void DHT11_SetOutput(void);
static void DHT11_SetInput(void);

void DHT11_Init(void) {
  static uint8_t initialized = 0U;
  if (initialized != 0U) {
    return;
  }
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  initialized = 1U;
}

static uint32_t DHT11_Micros(void) {
  return DWT->CYCCNT / (SystemCoreClock / 1000000U);
}

static void DHT11_DelayUs(uint32_t us) {
  uint32_t start = DHT11_Micros();
  while ((DHT11_Micros() - start) < us) {
  }
}

static uint32_t DHT11_WaitLevel(GPIO_PinState level, uint32_t timeoutUs) {
  uint32_t start = DHT11_Micros();
  while ((DHT11_Micros() - start) < timeoutUs) {
    if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == level) {
      return DHT11_Micros() - start;
    }
  }
  return 0U;
}

static void DHT11_SetOutput(void) {
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = DHT11_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DHT11_PORT, &gpio);
}

static void DHT11_SetInput(void) {
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = DHT11_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(DHT11_PORT, &gpio);
}

DHT11_Status_t DHT11_Read(DHT11_Data_t *data) {
  uint8_t buf[5] = {0};
  uint8_t i;
  uint8_t bit;
  uint32_t highUs;
  uint32_t crit;
  DHT11_Status_t status = DHT11_OK;

  DHT11_Init();

  /* 1. Start signal: pull low >= 18ms（这段可被抢占，DHT11 容忍更长低电平） */
  DHT11_SetOutput();
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
  DHT11_DelayUs(DHT11_START_LOW_US);

  /* 2. 时序敏感段：释放高电平到读完 40 bit 必须不可抢占（内核临界区，屏蔽 SysTick/PendSV） */
  crit = enter_critical();
  HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
  DHT11_DelayUs(DHT11_START_HIGH_US);

  /* Release bus, switch to input with pull-up */
  DHT11_SetInput();

  /* 3. DHT11 response: low ~80us then high ~80us */
  if (DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_PULSE_TIMEOUT_US) == 0U) {
    status = DHT11_ERR_TIMEOUT;
  } else if (DHT11_WaitLevel(GPIO_PIN_SET, DHT11_PULSE_TIMEOUT_US) == 0U) {
    status = DHT11_ERR_TIMEOUT;
  } else {
    /* 4. Read 40 bits: each bit = 50us low + data high pulse */
    for (i = 0U; i < 40U; i++) {
      if (DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_PULSE_TIMEOUT_US) == 0U) {
        status = DHT11_ERR_TIMEOUT;
        break;
      }
      if (DHT11_WaitLevel(GPIO_PIN_SET, DHT11_PULSE_TIMEOUT_US) == 0U) {
        status = DHT11_ERR_TIMEOUT;
        break;
      }
      highUs = DHT11_WaitLevel(GPIO_PIN_RESET, DHT11_PULSE_TIMEOUT_US);
      if (highUs == 0U) {
        status = DHT11_ERR_TIMEOUT;
        break;
      }
      bit = (highUs > DHT11_BIT1_THRESH_US) ? 1U : 0U;
      buf[i >> 3] = (uint8_t)((buf[i >> 3] << 1) | bit);
    }
  }

  /* 5. Release bus, back to idle (input pull-up) */
  DHT11_SetInput();
  exit_critical(crit);

  if (status != DHT11_OK) {
    return status;
  }

  /* 6. Checksum: (hum_int + hum_dec + temp_int + temp_dec) & 0xFF */
  if ((uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]) != buf[4]) {
    return DHT11_ERR_CHECKSUM;
  }

  data->humidity_int = buf[0];
  data->humidity_dec = buf[1];
  data->temp_int = buf[2];
  data->temp_dec = buf[3];
  data->checksum = buf[4];

  return DHT11_OK;
}
