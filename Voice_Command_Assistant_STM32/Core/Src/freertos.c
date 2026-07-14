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
#include <string.h>
#include "usart.h"
#include "logger.h"
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
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for myTaskLed */
osThreadId_t myTaskLedHandle;
const osThreadAttr_t myTaskLed_attributes = {
  .name = "myTaskLed",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for myTaskLed2 */
osThreadId_t myTaskLed2Handle;
const osThreadAttr_t myTaskLed2_attributes = {
  .name = "myTaskLed2",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for myTaskLogger */
osThreadId_t myTaskLoggerHandle;
const osThreadAttr_t myTaskLogger_attributes = {
  .name = "myTaskLogger",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for LogQueue */
osMessageQueueId_t LogQueueHandle;
const osMessageQueueAttr_t LogQueue_attributes = {
  .name = "LogQueue"
};
/* Definitions for DmaTxSem */
osSemaphoreId_t DmaTxSemHandle;
const osSemaphoreAttr_t DmaTxSem_attributes = {
  .name = "DmaTxSem"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTaskLed(void *argument);
void StartTaskLed2(void *argument);
void StartTaskLogger(void *argument);

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

  /* Create the semaphores(s) */
  /* creation of DmaTxSem */
  DmaTxSemHandle = osSemaphoreNew(1, 1, &DmaTxSem_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of LogQueue */
  LogQueueHandle = osMessageQueueNew (10, 128, &LogQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of myTaskLed */
  myTaskLedHandle = osThreadNew(StartTaskLed, NULL, &myTaskLed_attributes);

  /* creation of myTaskLed2 */
  myTaskLed2Handle = osThreadNew(StartTaskLed2, NULL, &myTaskLed2_attributes);

  /* creation of myTaskLogger */
  myTaskLoggerHandle = osThreadNew(StartTaskLogger, NULL, &myTaskLogger_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTaskLed */
/**
* @brief Function implementing the myTaskLed thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskLed */
void StartTaskLed(void *argument)
{
  /* USER CODE BEGIN StartTaskLed */
  /* Infinite loop */
  for(;;)
  {
	HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_12|GPIO_PIN_14);
	printl("Blinking leds 12 and 14\n");
	osDelay(1000);
  }
  /* USER CODE END StartTaskLed */
}

/* USER CODE BEGIN Header_StartTaskLed2 */
/**
* @brief Function implementing the myTaskLed2 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskLed2 */
void StartTaskLed2(void *argument)
{
  /* USER CODE BEGIN StartTaskLed2 */
  /* Infinite loop */
  for(;;)
  {
	HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13|GPIO_PIN_15);
	printl("Blinking leds 13 and 15\n");
	osDelay(500);
  }
  /* USER CODE END StartTaskLed2 */
}

/* USER CODE BEGIN Header_StartTaskLogger */
/**
* @brief Function implementing the myTaskLogger thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTaskLogger */
void StartTaskLogger(void *argument)
{
  /* USER CODE BEGIN StartTaskLogger */
  /* Infinite loop */
	char msgBuffer[128];
	for(;;) {
	   if (osMessageQueueGet(LogQueueHandle, msgBuffer, NULL, osWaitForever) == osOK) {
	      HAL_UART_Transmit_DMA(&huart2, (uint8_t*)msgBuffer, strlen(msgBuffer));
	      osSemaphoreAcquire(DmaTxSemHandle, osWaitForever);
	   }
	}
  /* USER CODE END StartTaskLogger */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        osSemaphoreRelease(DmaTxSemHandle);
    }
}
/* USER CODE END Application */

