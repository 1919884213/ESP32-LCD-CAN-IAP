/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "MPU6050.h"
#include "HCSR04.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for MPU6050Task */
osThreadId_t MPU6050TaskHandle;
const osThreadAttr_t MPU6050Task_attributes = {
  .name = "MPU6050Task",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for CANTask */
osThreadId_t CANTaskHandle;
const osThreadAttr_t CANTask_attributes = {
  .name = "CANTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for UltrasonicTask */
osThreadId_t UltrasonicTaskHandle;
const osThreadAttr_t UltrasonicTask_attributes = {
  .name = "UltrasonicTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for MPU6050Queue */
osMessageQueueId_t MPU6050QueueHandle;
const osMessageQueueAttr_t MPU6050Queue_attributes = {
  .name = "MPU6050Queue"
};
/* Definitions for DistanceQueue */
osMessageQueueId_t DistanceQueueHandle;
const osMessageQueueAttr_t DistanceQueue_attributes = {
  .name = "DistanceQueue"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartMPU6050Task(void *argument);
void StartCANTask(void *argument);
void StartUltrasonicTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of MPU6050Queue */
  MPU6050QueueHandle = osMessageQueueNew (1, sizeof(MPU6050_Data_t), &MPU6050Queue_attributes);

  /* creation of DistanceQueue */
  DistanceQueueHandle = osMessageQueueNew (2, sizeof(HCSR04_Handle_t), &DistanceQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of MPU6050Task */
  MPU6050TaskHandle = osThreadNew(StartMPU6050Task, NULL, &MPU6050Task_attributes);

  /* creation of CANTask */
  CANTaskHandle = osThreadNew(StartCANTask, NULL, &CANTask_attributes);

  /* creation of UltrasonicTask */
  UltrasonicTaskHandle = osThreadNew(StartUltrasonicTask, NULL, &UltrasonicTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartMPU6050Task */
/**
  * @brief  Function implementing the MPU6050Task thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartMPU6050Task */
__weak void StartMPU6050Task(void *argument)
{
  /* USER CODE BEGIN StartMPU6050Task */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartMPU6050Task */
}

/* USER CODE BEGIN Header_StartCANTask */
/**
* @brief Function implementing the CANTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCANTask */
__weak void StartCANTask(void *argument)
{
  /* USER CODE BEGIN StartCANTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartCANTask */
}

/* USER CODE BEGIN Header_StartUltrasonicTask */
/**
* @brief Function implementing the UltrasonicTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUltrasonicTask */
__weak void StartUltrasonicTask(void *argument)
{
  /* USER CODE BEGIN StartUltrasonicTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartUltrasonicTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

