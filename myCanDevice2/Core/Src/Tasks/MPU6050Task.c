#include "FreeRTOS.h"
#include "MPU6050.h"
#include "cmsis_os.h"
#include "i2c.h"
#include "main.h"
#include "task.h"
#include "usart.h"
#include <stdio.h>

extern osMessageQueueId_t MPU6050QueueHandle;

MPU6050_Data_t mpu_data;
volatile uint8_t mpu6050_fault = 0;

int __io_putchar(int ch) {
  uint8_t byte = (uint8_t)ch;
  HAL_UART_Transmit(&huart1, &byte, 1, HAL_MAX_DELAY);
  return ch;
}

void StartMPU6050Task(void *argument) {
  (void)argument;

  if (MPU6050_Init() != 0) {
    mpu6050_fault = 1;
  }

  for (;;) {
    if (!mpu6050_fault) {
      MPU6050_ReadAccel(&mpu_data.ax, &mpu_data.ay, &mpu_data.az);
      MPU6050_ReadGyro(&mpu_data.gx, &mpu_data.gy, &mpu_data.gz);
      MPU6050_ReadTemp(&mpu_data.temp);
      printf("ax=%d ay=%d az=%d gx=%d gy=%d gz=%d temp=%d\r\n",
             mpu_data.ax, mpu_data.ay, mpu_data.az,
             mpu_data.gx, mpu_data.gy, mpu_data.gz, mpu_data.temp);
      osDelay(1000); /*串口发送后延时1s*/
      osMessageQueuePut(MPU6050QueueHandle, &mpu_data, 0, 0);
    } else {
      printf("MPU6050 fault: %d I2C err=0x%08lX\r\n",
             (int)mpu6050_fault, (unsigned long)hi2c2.ErrorCode);
      osDelay(1000); /*串口发送后延时1s*/
    }
    osDelay(10);
  }
}