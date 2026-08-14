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
#include <math.h>
#include <stdint.h>
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
uint16_t pdm_rx_buffer[1024];
StreamBufferHandle_t AudioStreamBuf = NULL;

/* ---- STAI Network Buffers ---- */
static int8_t ai_input_buf[STAI_NETWORK_IN_1_HEIGHT * STAI_NETWORK_IN_1_WIDTH];
static int8_t ai_output_buf[STAI_NETWORK_OUT_1_CHANNEL];
static uint8_t ai_activations[STAI_NETWORK_ACTIVATIONS_SIZE_BYTES]
    __attribute__((aligned(4)));
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
    .stack_size = 512 * 4,
    .priority = (osPriority_t)osPriorityAboveNormal,
};
/* Definitions for myTaskLogger */
osThreadId_t myTaskLoggerHandle;
const osThreadAttr_t myTaskLogger_attributes = {
    .name = "myTaskLogger",
    .stack_size = 128 * 4,
    .priority = (osPriority_t)osPriorityLow,
};
/* Definitions for myTaskAI */
osThreadId_t myTaskAIHandle;
const osThreadAttr_t myTaskAI_attributes = {
    .name = "myTaskAI",
    .stack_size = 512 * 4,
    .priority = (osPriority_t)osPriorityBelowNormal,
};
/* Definitions for LogQueue */
osMessageQueueId_t LogQueueHandle;
const osMessageQueueAttr_t LogQueue_attributes = {.name = "LogQueue"};
/* Definitions for MfccQueue */
osMessageQueueId_t MfccQueueHandle;
const osMessageQueueAttr_t MfccQueue_attributes = {.name = "MfccQueue"};
/* Definitions for VolQueue */
osMessageQueueId_t VolQueueHandle;
const osMessageQueueAttr_t VolQueue_attributes = {.name = "VolQueue"};
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
void StartTaskAI(void *argument);

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

  /* creation of MfccQueue */
  MfccQueueHandle = osMessageQueueNew(16, 40, &MfccQueue_attributes);

  /* creation of VolQueue */
  VolQueueHandle =
      osMessageQueueNew(16, sizeof(uint16_t), &VolQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  AudioStreamBuf = xStreamBufferCreate(2048, 1024);
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

  /* creation of myTaskAI */
  myTaskAIHandle = osThreadNew(StartTaskAI, NULL, &myTaskAI_attributes);

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

  if (HAL_I2S_Receive_DMA(&hi2s2, pdm_rx_buffer, 1024) == HAL_OK) {
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
  /* Wait for 512 words (1024 bytes) from DMA half buffer */
  uint16_t pdm_working_buffer[512];
  uint16_t pcm_output_buffer[16]; // 64 raw PDM words / decimation 64 = 16 PCM
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

  /* Fix 1: Delay Audio Start so LED animation can finish cleanly */
  osDelay(1000);

  int16_t print_peak = 0;
  int16_t frame_peak = 0;
  int chunks_since_last_print = 0;

  static int16_t rolling_buffer[256] = {0};
  int samples_collected = 0;

  for (;;) {
    xStreamBufferReceive(AudioStreamBuf, pdm_working_buffer, 1024,
                         osWaitForever);

    /* Process the 512 words as 8 chunks of 64 words */
    for (int chunk = 0; chunk < 8; chunk++) {
      uint16_t pdm_swapped[64];
      for (int i = 0; i < 64; i++) {
        pdm_swapped[i] =
            __builtin_bswap16(pdm_working_buffer[(chunk * 64) + i]);
      }
      PDM_Filter_64_LSB((uint8_t *)pdm_swapped, pcm_output_buffer, 1, &Filter);

      /* Software Decimation: Secure 2:1 sampling isolates pristine 16kHz audio!
       */
      for (int i = 0; i < 8; i++) {
        int16_t s = (int16_t)pcm_output_buffer[i * 2];

        /* Track absolute peaks separately for logger and AI */
        int16_t abs_raw = (s < 0) ? -s : s;
        if (abs_raw > print_peak)
          print_peak = abs_raw;
        if (abs_raw > frame_peak)
          frame_peak = abs_raw;

        /* Directly assign unfiltered pristine raw audio back */
        pcm_output_buffer[i * 2] = s;
      }

      chunks_since_last_print++;
      if (chunks_since_last_print >= 8) {
        chunks_since_last_print = 0;
        if (print_peak > 30) {
          printl("volume:%d\r\n", (int)print_peak);
        }
        print_peak = 0; // Only resets the logger!
      }

      // Feed decimated samples into rolling buffer for MFCC
      for (int i = 0; i < 256 - 8; i++) {
        rolling_buffer[i] = rolling_buffer[i + 8];
      }
      for (int i = 0; i < 8; i++) {
        rolling_buffer[256 - 8 + i] = (int16_t)pcm_output_buffer[i * 2];
      }
      samples_collected += 8;

      if (samples_collected >= 160) {
        float mfcc_output[MFCC_NUM_BANDS];
        MFCC_Compute(rolling_buffer, mfcc_output);

        /* Accurately stream BOTH the ML feature and the isolated VAD peak
         * concurrently */
        osMessageQueuePut(MfccQueueHandle, mfcc_output, 0, 0);
        osMessageQueuePut(VolQueueHandle, &frame_peak, 0, 0);

        frame_peak = 0;
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

/* USER CODE BEGIN Header_StartTaskAI */
/**
 * @brief Function implementing the myTaskAI thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartTaskAI */
void StartTaskAI(void *argument) {
  /* USER CODE BEGIN StartTaskAI */
  printl("AI Task Booting...\r\n");

  /* Initialize Neural Network */
  stai_network *network = (stai_network *)ai_network_ctx;
  stai_network_init(network);
  stai_ptr act_ptr = (stai_ptr)ai_activations;
  stai_ptr in_ptr = (stai_ptr)ai_input_buf;
  stai_ptr out_ptr = (stai_ptr)ai_output_buf;
  stai_network_set_activations(network, &act_ptr, 1);
  stai_network_set_inputs(network, &in_ptr, 1);
  stai_network_set_outputs(network, &out_ptr, 1);

  /* Initialize history to -80.0f (log-mel silence floor, matching Python
   * baseline) */
  static float
      raw_mel_history[STAI_NETWORK_IN_1_HEIGHT * STAI_NETWORK_IN_1_WIDTH];
  for (int i = 0; i < STAI_NETWORK_IN_1_HEIGHT * STAI_NETWORK_IN_1_WIDTH; i++) {
    raw_mel_history[i] = -80.0f;
  }
  float mfcc_frame[MFCC_NUM_BANDS];

  /* Boot delay: mic hardware generates noise on powerup. The Audio Task keeps
   * filling the queues during this sleep, so we flush both queues afterwards
   * to discard all stale boot-noise frames before the VAD starts. */
  osDelay(2000);
  osMessageQueueReset(MfccQueueHandle);
  osMessageQueueReset(VolQueueHandle);
  printl("AI Task Ready. Listening...\r\n");

/* --- LED State Machine ---
 * LED mapping on STM32F407 Discovery:
 *   PD12 = Green  (word: "up")
 *   PD13 = Orange (word: "go")
 *   PD15 = Blue   (listening / idle)
 */
#define LED_GREEN GPIO_PIN_12
#define LED_ORANGE GPIO_PIN_13
#define LED_BLUE GPIO_PIN_15
#define LED_PORT GPIOD

  /* Turn off all LEDs, then light blue to signal system is listening */
  HAL_GPIO_WritePin(LED_PORT, LED_GREEN | LED_ORANGE | LED_BLUE,
                    GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_PORT, LED_BLUE, GPIO_PIN_SET);

  /* LED timer: counts 20ms frames remaining on the current word LED */
  static int led_word_frames_remaining = 0;
#define LED_WORD_FRAMES 250 // 250 * 20ms = 5 seconds

  for (;;) {
    /* Block here — truly 0% CPU during silence */
    if (osMessageQueueGet(MfccQueueHandle, mfcc_frame, NULL, osWaitForever) ==
        osOK) {

      /* Shift history left, insert new frame on the right */
      for (int i = 0; i < (STAI_NETWORK_IN_1_HEIGHT - 1) * MFCC_NUM_BANDS;
           i++) {
        raw_mel_history[i] = raw_mel_history[i + MFCC_NUM_BANDS];
      }
      float *dest =
          raw_mel_history + (STAI_NETWORK_IN_1_HEIGHT - 1) * MFCC_NUM_BANDS;
      for (int b = 0; b < MFCC_NUM_BANDS; b++) {
        dest[b] = mfcc_frame[b];
      }

/* --- VAD Security Guard --- */
#define VAD_IDLE 0
#define VAD_CAPTURING 1
      static int vad_state = VAD_IDLE;
      static int loud_count = 0;
      static int quiet_count = 0;
      static int capture_frame_count = 0;

      static float noise_floor = 0.0f;
      const uint16_t vad_margin = 10;
      const uint16_t vad_min_floor = 20;

      int16_t current_vol = 0;
      /* Synchronously pull the exact volume belonging to THIS mfcc frame */
      osMessageQueueGet(VolQueueHandle, &current_vol, NULL, 0);

      // --- Adaptive Threshold Calc ---
      uint16_t dynamic_threshold = (uint16_t)noise_floor + vad_margin;
      if (dynamic_threshold < vad_min_floor) {
        dynamic_threshold = vad_min_floor; // Hard minimum sensitivity
      }

      static int debug_log_counter = 0;
      debug_log_counter++;
      if (debug_log_counter >= 50) { // 50 frames * 20ms = 1.0 second
        printl("[VAD] Dynamic Threshold: %u (Floor: %d, Vol: %d)\r\n",
               dynamic_threshold, (int)noise_floor, current_vol);
        debug_log_counter = 0;
      }

      if (vad_state == VAD_IDLE) {
        // Track the noise ONLY if we are below the tripwire
        if (current_vol < dynamic_threshold) {
          noise_floor = (noise_floor * 0.95f) + ((float)current_vol * 0.05f);
        }

        if (current_vol >= dynamic_threshold) {
          loud_count++;
          if (loud_count >= 3) {
            vad_state = VAD_CAPTURING;
            printl("\r\n[VAD] Start of word detected...\r\n");
            loud_count = 0;
            quiet_count = 0;
            capture_frame_count = 0;
          }
        } else {
          loud_count = 0;
        }
      } else if (vad_state == VAD_CAPTURING) {
        capture_frame_count++;

        /* End-of-word conditions: 3 quiet frames OR we hit the 900ms overflow
         * limit */
        int timeout_triggered = (capture_frame_count > 50);
        int silence_triggered = 0;

        if (current_vol < dynamic_threshold) {
          quiet_count++;
          if (quiet_count >= 10)
            silence_triggered = 1;
        } else {
          quiet_count = 0;
        }

        if (silence_triggered || timeout_triggered) {
          if (timeout_triggered) {
            printl("[VAD] Max word length hit. Running AI... \r\n");
            // The instant-snap recalibration
            noise_floor = (float)current_vol;
          } else {
            printl("[VAD] End of word detected. Running AI...\r\n");
          }

          /* Z-Score Normalisation across time for each band */
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
            if (std < 1e-8f)
              std = 1e-8f;
            for (int t = 0; t < STAI_NETWORK_IN_1_HEIGHT; t++) {
              float val_f32 =
                  (raw_mel_history[t * MFCC_NUM_BANDS + b] - mean) / std;

              /* Quantize float -> INT8 to match STEDGEAI's input tensor format
               */
              int32_t val_s32 =
                  (int32_t)roundf(val_f32 / STAI_NETWORK_IN_1_SCALE) +
                  STAI_NETWORK_IN_1_ZERO_POINT;
              if (val_s32 > 127)
                val_s32 = 127;
              if (val_s32 < -128)
                val_s32 = -128;

              ai_input_buf[t * MFCC_NUM_BANDS + b] = (int8_t)val_s32;
            }
          }

          stai_network_run(network, STAI_MODE_SYNC);

          /* STAI Int8 De-quantization: real = (q - zero_point) * scale */
          float prob_noise =
              (ai_output_buf[0] - STAI_NETWORK_OUT_1_ZERO_POINT) *
              STAI_NETWORK_OUT_1_SCALE;
          float prob_turn_on =
              (ai_output_buf[1] - STAI_NETWORK_OUT_1_ZERO_POINT) *
              STAI_NETWORK_OUT_1_SCALE;
          float prob_unknown =
              (ai_output_buf[2] - STAI_NETWORK_OUT_1_ZERO_POINT) *
              STAI_NETWORK_OUT_1_SCALE;
          float prob_wake_up =
              (ai_output_buf[3] - STAI_NETWORK_OUT_1_ZERO_POINT) *
              STAI_NETWORK_OUT_1_SCALE;

          int pct_noise = (int)(prob_noise * 100);
          int pct_turn_on = (int)(prob_turn_on * 100);
          int pct_unknown = (int)(prob_unknown * 100);
          int pct_wake_up = (int)(prob_wake_up * 100);

          printl(
              "noise:%02d%% turn_on:%02d%% unknown:%02d%% wake_up:%02d%%\r\n",
              pct_noise, pct_turn_on, pct_unknown, pct_wake_up);

          /* --- LED State Machine: drive LEDs based on best prediction --- */
          /* Note: unknown and noise do not trigger LEDs (just like idle) */
          if (pct_wake_up > pct_turn_on && pct_wake_up > pct_noise &&
              pct_wake_up > pct_unknown) {
            /* "wake_up" detected: green LED on, restart 5s timer */
            HAL_GPIO_WritePin(LED_PORT, LED_GREEN | LED_ORANGE | LED_BLUE,
                              GPIO_PIN_RESET);
            HAL_GPIO_WritePin(LED_PORT, LED_GREEN, GPIO_PIN_SET);
            led_word_frames_remaining = LED_WORD_FRAMES;
          } else if (pct_turn_on > pct_wake_up && pct_turn_on > pct_noise &&
                     pct_turn_on > pct_unknown) {
            /* "turn_on" detected: orange LED on, restart 5s timer */
            HAL_GPIO_WritePin(LED_PORT, LED_GREEN | LED_ORANGE | LED_BLUE,
                              GPIO_PIN_RESET);
            HAL_GPIO_WritePin(LED_PORT, LED_ORANGE, GPIO_PIN_SET);
            led_word_frames_remaining = LED_WORD_FRAMES;
          }

          /* Clean slate: Wipe history with perfectly mathematical Silence (-80)
           * for next word */
          for (int i = 0;
               i < STAI_NETWORK_IN_1_HEIGHT * STAI_NETWORK_IN_1_WIDTH; i++) {
            raw_mel_history[i] = -80.0f;
          }

          vad_state = VAD_IDLE;
          quiet_count = 0;
          loud_count = 0;
          capture_frame_count = 0;
        }
      }

      /* --- LED Countdown Tick (runs every 20ms frame) ---
       * Decrements the word-LED timer and restores blue when it expires. */
      if (led_word_frames_remaining > 0) {
        led_word_frames_remaining--;
        if (led_word_frames_remaining == 0) {
          /* Timer expired: turn off word LEDs and restore blue (listening) */
          HAL_GPIO_WritePin(LED_PORT, LED_GREEN | LED_ORANGE, GPIO_PIN_RESET);
          HAL_GPIO_WritePin(LED_PORT, LED_BLUE, GPIO_PIN_SET);
        }
      }
    }
  }
  /* USER CODE END StartTaskAI */
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
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xStreamBufferSendFromISR(AudioStreamBuf, &pdm_rx_buffer[0], 1024,
                             &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI2) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xStreamBufferSendFromISR(AudioStreamBuf, &pdm_rx_buffer[512], 1024,
                             &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}
/* USER CODE END Application */
