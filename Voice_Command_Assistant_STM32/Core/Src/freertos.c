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
#include "cmsis_os.h"
#include "main.h"
#include "task.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "arm_math.h"
#include "logger.h"
#include "mfcc.h"
#include "network.h"
#include "network_data.h"
#include "pdm_filter.h"
#include "stream_buffer.h"
#include "usart.h"
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ARM_MATH_CM4
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern I2S_HandleTypeDef hi2s2;
uint16_t pdm_rx_buffer[128];
StreamBufferHandle_t AudioStreamBuf = NULL;

/* ---- STAI Network Buffers ---- */
/* Input:  49 frames x 10 mel bands x 1 channel (float32) = 490 floats */
static float ai_input_buf[STAI_NETWORK_IN_1_HEIGHT * STAI_NETWORK_IN_1_WIDTH];
/* Output: 3 classes [up, go, noise] */
static float ai_output_buf[STAI_NETWORK_OUT_1_CHANNEL];
/* Activations scratch RAM (~64 KB) */
static uint8_t ai_activations[STAI_NETWORK_ACTIVATIONS_SIZE_BYTES]
    __attribute__((aligned(4)));
/* Opaque network context handle */
static uint8_t ai_network_ctx[STAI_NETWORK_CONTEXT_SIZE]
    __attribute__((aligned(8)));

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
    .name = "defaultTask",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityNormal,
};
/* Definitions for myTaskLed */
osThreadId_t myTaskLedHandle;
const osThreadAttr_t myTaskLed_attributes = {
    .name = "myTaskLed",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityLow,
};
/* Definitions for myTaskAudio */
osThreadId_t myTaskAudioHandle;
const osThreadAttr_t myTaskAudio_attributes = {
    .name = "myTaskAudio",
    .stack_size = 1024 * 4, /* Doubled from 512 to give the Neural Network
                               guaranteed breathing room! */
    .priority = (osPriority_t)osPriorityAboveNormal,
};
/* Definitions for myTaskLogger */
osThreadId_t myTaskLoggerHandle;
const osThreadAttr_t myTaskLogger_attributes = {
    .name = "myTaskLogger",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityLow,
};
/* Definitions for LogQueue */
osMessageQueueId_t LogQueueHandle;
const osMessageQueueAttr_t LogQueue_attributes = {.name = "LogQueue"};
/* Definitions for DmaTxSem */
osSemaphoreId_t DmaTxSemHandle;
const osSemaphoreAttr_t DmaTxSem_attributes = {.name = "DmaTxSem"};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTaskLed(void *argument);
void StartTaskAudio(void *argument);
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
  LogQueueHandle = osMessageQueueNew(10, 128, &LogQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  AudioStreamBuf = xStreamBufferCreate(256, 128);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle =
      osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of myTaskLed */
  myTaskLedHandle = osThreadNew(StartTaskLed, NULL, &myTaskLed_attributes);

  /* creation of myTaskAudio */
  myTaskAudioHandle =
      osThreadNew(StartTaskAudio, NULL, &myTaskAudio_attributes);

  /* creation of myTaskLogger */
  myTaskLoggerHandle =
      osThreadNew(StartTaskLogger, NULL, &myTaskLogger_attributes);

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
void StartDefaultTask(void *argument) {
  /* USER CODE BEGIN StartDefaultTask */

  /* Give the ST-LINK USB-to-Serial bridge 200ms to open its COM port properly
     before we barrage it with text, or else characters get dropped/corrupted.
   */
  osDelay(200);

  if (HAL_I2S_Receive_DMA(&hi2s2, pdm_rx_buffer, 128) == HAL_OK) {
    printl("I2S DMA started successfully!\r\n");
  }
  /* Infinite loop */
  for (;;) {
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
void StartTaskLed(void *argument) {
  /* USER CODE BEGIN StartTaskLed */
  for (int i = 0; i < 2; i++) {
    HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_12);
    osDelay(100);
    HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_13);
    osDelay(100);
    HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_14);
    osDelay(100);
    HAL_GPIO_TogglePin(GPIOD, GPIO_PIN_15);
    osDelay(100);
  }
  /* Infinite loop */
  for (;;) {
    osDelay(1);
  }
  /* USER CODE END StartTaskLed */
}

/* USER CODE BEGIN Header_StartTaskAudio */
/**
 * @brief Function implementing the myTaskAudio thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTaskAudio */
void StartTaskAudio(void *argument) {
  /* USER CODE BEGIN StartTaskAudio */
  PDMFilter_InitStruct Filter = {0};
  uint16_t
      pdm_working_buffer[64]; // Holds the raw 128 bytes we grab from the pipe
  uint16_t pcm_output_buffer[16]; // 64 raw PDM words / decimation 64 = 16 PCM
                                  // samples output
  // samples output

  /* Verified working settings from mic_test calibration */
  Filter.Fs = 16000;
  Filter.LP_HZ = 4000.0f; /* 4000/16000 fraction perfectly maps to a realistic
                             8000Hz Nyquist cutoff on the 32kHz hardware! */
  Filter.HP_HZ = 10.0f;
  Filter.In_MicChannels = 1;
  Filter.Out_MicChannels = 1;
  PDM_Filter_Init(&Filter);
  MFCC_Init();

  /* ---- Boot-time STAI Network Initialisation ---- */
  stai_network *network = (stai_network *)ai_network_ctx;
  stai_network_init(network);
  stai_ptr act_ptr = (stai_ptr)ai_activations;
  stai_ptr in_ptr =
      (stai_ptr)ai_input_buf; /* This is now our NORMALIZED tensor */
  stai_ptr out_ptr = (stai_ptr)ai_output_buf;
  stai_network_set_activations(network, &act_ptr, 1);
  stai_network_set_inputs(network, &in_ptr, 1);
  stai_network_set_outputs(network, &out_ptr, 1);
  printl("STAI network ready.\r\n");

  /* Fix 1: Delay Audio Start so LED animation can finish cleanly */
  osDelay(1000);

/* State Machine defines */
#define STATE_WAIT_WAKE 0
#define STATE_WAIT_ACTION 1
#define STATE_ACTION_EXECUTED 2

  static int current_state = STATE_WAIT_WAKE;
  static uint32_t state_timer = 0;

  /* Infinite loop */
  static int16_t rolling_buffer[512] = {0};
  int samples_collected = 0;
  int16_t gated_peak = 0;
  int16_t raw_peak = 0;

  /* Secondary buffer for raw MFCCs (so we don't corrupt history during
   * standardisation) */
  static float
      raw_mel_history[STAI_NETWORK_IN_1_HEIGHT * STAI_NETWORK_IN_1_WIDTH] = {0};

  for (;;) {
    // Sleep until 128 bytes arrive
    xStreamBufferReceive(AudioStreamBuf, pdm_working_buffer, 128,
                         osWaitForever);

    /* Time-Correct Decode: STM32 DMA Little-Endian requires unswapping byte
       pairs. This perfectly pairs with LSB to extract a mathematically pure
       centered signal! */
    uint16_t pdm_swapped[64];
    for (int i = 0; i < 64; i++) {
      pdm_swapped[i] = __builtin_bswap16(pdm_working_buffer[i]);
    }
    PDM_Filter_64_LSB((uint8_t *)pdm_swapped, pcm_output_buffer, 1, &Filter);

    /* Software Decimation: Secure 2:1 sampling isolates pristine 16kHz audio!
     */
    for (int i = 0; i < 8; i++) {
      int16_t s = (int16_t)pcm_output_buffer[i * 2];

      /* Track Absolute Raw Peak */
      int16_t abs_raw = (s < 0) ? -s : s;
      if (abs_raw > raw_peak)
        raw_peak = abs_raw;

      /* STATIC DIGITAL NOISE GATE (Ceiling: 30)
       * Wipe all electrical silicone DC hum directly into pure silence before
       * it enters the MFCC neural network structure! */
      if (s > -30 && s < 30) {
        s = 0;
      }

      /* Feed gated audio cleanly into the network processing buffer */
      pcm_output_buffer[i * 2] = s;

      /* Track Absolute Gated Peak for UI */
      int16_t abs_gated = (s < 0) ? -s : s;
      if (abs_gated > gated_peak)
        gated_peak = abs_gated;
    }

    // Stride Math (Rolling Buffer for 512-window MFCC)
    for (int i = 0; i < 512 - 8; i++) {
      rolling_buffer[i] = rolling_buffer[i + 8];
    }
    for (int i = 0; i < 8; i++) {
      rolling_buffer[512 - 8 + i] = (int16_t)pcm_output_buffer[i * 2];
    }
    samples_collected += 8;

    // Handle Timeouts
    if (current_state == STATE_WAIT_ACTION &&
        (HAL_GetTick() - state_timer) >= 10000) {
      printl("Timeout waiting for action. Returning to WAKE state...\r\n");
      current_state = STATE_WAIT_WAKE;
    }
    if (current_state == STATE_ACTION_EXECUTED &&
        (HAL_GetTick() - state_timer) >= 5000) {
      current_state = STATE_WAIT_WAKE;
    }

    // Update LEDs for current state
    if (current_state == STATE_WAIT_WAKE) {
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14,
                        GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_15, GPIO_PIN_SET); // Blue only
    } else if (current_state == STATE_WAIT_ACTION) {
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15,
                        GPIO_PIN_RESET);
      HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET); // Green only
    } else if (current_state == STATE_ACTION_EXECUTED) {
      HAL_GPIO_WritePin(GPIOD,
                        GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15,
                        GPIO_PIN_SET); // All 4 LEDs ON!
    }

    if (samples_collected >= 320) {
      /* Accumulate one Mel-Band frame (10 floats) into input buffer */
      float mfcc_output[MFCC_NUM_BANDS]; /* 10 raw Mel bands */
      MFCC_Compute(rolling_buffer, mfcc_output);

      /* Fix 4a: Shift raw mel history strictly chronologically */
      for (int i = 0; i < (STAI_NETWORK_IN_1_HEIGHT - 1) * MFCC_NUM_BANDS;
           i++) {
        raw_mel_history[i] = raw_mel_history[i + MFCC_NUM_BANDS];
      }
      /* Insert new frame at the END */
      float *dest =
          raw_mel_history + (STAI_NETWORK_IN_1_HEIGHT - 1) * MFCC_NUM_BANDS;
      for (int b = 0; b < MFCC_NUM_BANDS; b++) {
        dest[b] = mfcc_output[b];
      }

      static int frames_since_run = 0;
      frames_since_run++;
      /* Slide inference window every 12 frames (~240ms) for magical snap
       * responsiveness! */
      if (frames_since_run >= 12) {
        frames_since_run = 0;

        if (current_state == STATE_ACTION_EXECUTED) {
          uint32_t passed = HAL_GetTick() - state_timer;
          uint32_t ms_left = (passed < 5000) ? (5000 - passed) : 0;
          printl("   --- EXECUTING ---   | raw:%d out:%d bar:30 [COOLDOWN: "
                 "%lu.%lus]\r\n",
                 (int)raw_peak, (int)gated_peak, ms_left / 1000,
                 (ms_left % 1000) / 100);
        } else {
          /* Z-Score Normalisation across history columns */
          for (int b = 0; b < MFCC_NUM_BANDS; b++) {
            float sum = 0.0f, sq_sum = 0.0f;
            for (int t = 0; t < STAI_NETWORK_IN_1_HEIGHT; t++) {
              float val = raw_mel_history[t * MFCC_NUM_BANDS + b];
              sum += val;
              sq_sum += val * val;
            }
            float mean = sum / (float)STAI_NETWORK_IN_1_HEIGHT;
            float variance =
                (sq_sum / (float)STAI_NETWORK_IN_1_HEIGHT) - (mean * mean);
            if (variance < 0.0f)
              variance = 0.0f;

            float std = sqrtf(variance);
            if (std < 1e-8f) {
              std = 1e-8f;
            }

            for (int t = 0; t < STAI_NETWORK_IN_1_HEIGHT; t++) {
              ai_input_buf[t * MFCC_NUM_BANDS + b] =
                  (raw_mel_history[t * MFCC_NUM_BANDS + b] - mean) / std;
            }
          }

          stai_network_run(network, STAI_MODE_SYNC);

          int pct_up = (int)(ai_output_buf[0] * 100);
          int pct_go = (int)(ai_output_buf[1] * 100);
          int pct_noise = (int)(ai_output_buf[2] * 100);

          if (current_state == STATE_WAIT_ACTION) {
            uint32_t passed = HAL_GetTick() - state_timer;
            uint32_t ms_left = (passed < 10000) ? (10000 - passed) : 0;
            printl("up:%02d%% go:%02d%% noise:%02d%% | raw:%d out:%d bar:30 "
                   "[WAIT GO: "
                   "%lu.%lus]\r\n",
                   pct_up, pct_go, pct_noise, (int)raw_peak, (int)gated_peak,
                   ms_left / 1000, (ms_left % 1000) / 100);
          } else {
            printl(
                "up:%02d%% go:%02d%% noise:%02d%% | raw:%d out:%d bar:30\r\n",
                pct_up, pct_go, pct_noise, (int)raw_peak, (int)gated_peak);
          }

          if (pct_up >= 100 && current_state == STATE_WAIT_WAKE) {
            /* WIPE the acoustic history securely to completely prevent GHOST
             * looping! */
            memset(raw_mel_history, 0, sizeof(raw_mel_history));
            current_state = STATE_WAIT_ACTION;
            state_timer = HAL_GetTick();
            printl("  >>> 'UP' CONFIRMED! (%02d%% | peak:%d) Say 'GO' within "
                   "10s <<<\r\n\n",
                   pct_up, (int)gated_peak);
          } else if (pct_go >= 75 && current_state == STATE_WAIT_ACTION) {
            /* WIPE the acoustic history securely to completely prevent GHOST
             * looping! */
            memset(raw_mel_history, 0, sizeof(raw_mel_history));
            current_state = STATE_ACTION_EXECUTED;
            state_timer = HAL_GetTick();
            printl("  >>> 'GO' CONFIRMED! (%02d%% | peak:%d) Executing... "
                   "<<<\r\n\n",
                   pct_go, (int)gated_peak);
          }
        }

        gated_peak = 0;
        raw_peak = 0;
        samples_collected = 0;
      }
    }
  }
  /* USER CODE END StartTaskAudio */
}

/* USER CODE BEGIN Header_StartTaskLogger */
/**
 * @brief Function implementing the myTaskLogger thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTaskLogger */
void StartTaskLogger(void *argument) {
  /* USER CODE BEGIN StartTaskLogger */
  /* Infinite loop */
  char msgBuffer[128];
  for (;;) {
    if (osMessageQueueGet(LogQueueHandle, msgBuffer, NULL, osWaitForever) ==
        osOK) {
      HAL_UART_Transmit_DMA(&huart2, (uint8_t *)msgBuffer, strlen(msgBuffer));
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

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI2) {
    BaseType_t xHigherPriorityTaskWoken =
        pdFALSE; // xStreamBufferSendFromISR requires a BaseType_t variable to
                 // check if a higher-priority task needs to wake up after the
                 // push.
    xStreamBufferSendFromISR(AudioStreamBuf, &pdm_rx_buffer[0], 128,
                             &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI2) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xStreamBufferSendFromISR(AudioStreamBuf, &pdm_rx_buffer[64], 128,
                             &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}
/* USER CODE END Application */
