/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "pdm_filter.h"
#include <math.h> /* log10f for dBFS */
#include <stdio.h>
#include <string.h>

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
CRC_HandleTypeDef hcrc;

I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_rx;

UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_tx;

/* USER CODE BEGIN PV */
#include <stdio.h>
#include <string.h>

/* PDM raw data from the mic */
#define PDM_BUF_HALF 512 /* 8192 bits = 16ms of audio at 512kHz */
#define PDM_BUF_SIZE (PDM_BUF_HALF * 2)
uint16_t pdm_buf[PDM_BUF_SIZE];
int16_t pcm_buf[128]; /* 8 x 16 PCM samples per half-buffer */

/* UART TX buffer */
char uart_buf[128];
volatile uint8_t print_flag = 0;

PDMFilter_InitStruct Filter;     /* PDM decimation filter state */
static uint16_t swapped_pdm[64]; /* PDM byte-swap scratch buffer — kept global
                                    to avoid stack overflow */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2S2_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_CRC_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI2) {
    print_flag = 1; /* First half (0..511) is ready */
  }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s) {
  if (hi2s->Instance == SPI2) {
    print_flag = 2; /* Second half (512..1023) is ready */
  }
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2S2_Init();
  MX_USART2_UART_Init();
  MX_CRC_Init();
  /* USER CODE BEGIN 2 */
  HAL_Delay(500); /* Long delay — ensure USB serial bridge has opened */

  /* Checkpoint 1 — UART works */
  HAL_UART_Transmit(&huart2, (uint8_t *)"[1] Boot OK\r\n", 13, 100);

  /* Init PDM filter */
  Filter.Fs =
      16000; /* Real target PCM frequency (e.g. 1.024MHz / 64 dec = 16kHz) */
  Filter.LP_HZ = 8000.0f;
  Filter.HP_HZ = 10.0f;
  Filter.In_MicChannels = 1;
  Filter.Out_MicChannels = 1;

  PDM_Filter_Init(&Filter);

  HAL_UART_Transmit(&huart2, (uint8_t *)"[2] PDM filter init OK\r\n", 24, 100);

  /* Start I2S DMA */
  if (HAL_I2S_Receive_DMA(&hi2s2, pdm_buf, PDM_BUF_SIZE) != HAL_OK) {
    HAL_UART_Transmit(&huart2, (uint8_t *)"[ERR] I2S DMA failed!\r\n", 23, 100);
    Error_Handler();
  }

  HAL_UART_Transmit(&huart2,
                    (uint8_t *)"[3] I2S DMA running — VU meter starting...\r\n",
                    44, 100);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  /* ═════════════════════
   * DYNAMIC SILENCE GATE
   * ═════════════════════ */
#define SILENCE_BAR_INIT 30
#define OBSERVE_CYCLE_BLOCKS 50 /* 50 blocks * 8ms = 400ms target */
#define OBSERVE_TOLERANCE 5     /* ±5 variance between 8ms block envelopes */

  static int16_t silence_bar = SILENCE_BAR_INIT;
  static int16_t prev_silence_bar = SILENCE_BAR_INIT;
  static int16_t prev_block_peak = 0;
  static int16_t observe_peak = 0;
  static int32_t stable_run = 0;
  static int32_t below_bar_run = 0;
  static int32_t over_bar_run = 0; /* Add track for over-bar jumps */
  static uint8_t gate_active = (SILENCE_BAR_INIT > 0) ? 1 : 0;

  while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (print_flag == 1 || print_flag == 2) {
      int half = print_flag;
      print_flag = 0;

      int base = (half == 1) ? 0 : 512;

      for (int chunk = 0; chunk < 8; chunk++) {
        for (int i = 0; i < 64; i++) {
          swapped_pdm[i] = HTONS(pdm_buf[base + (chunk * 64) + i]);
        }
        PDM_Filter_64_LSB((uint8_t *)swapped_pdm,
                          (uint16_t *)&pcm_buf[chunk * 16], 1, &Filter);
      }

      /* 1. Extract the Envelope (Absolute Peak of the 8ms Block) */
      int16_t block_peak = 0;
      for (int i = 0; i < 128; i++) {
        int16_t raw_v = pcm_buf[i];
        int16_t abs_v = (raw_v < 0) ? -raw_v : raw_v;
        if (abs_v > block_peak)
          block_peak = abs_v;
      }

      /* ── DYNAMIC SILENCE GATE STATE MACHINE ── */

      /* Rule 1: The Stability Tracker (Finding the Noise Floor)
       * Instead of looking at zero crossings, we look at the absolute volume
       * envelope of the room spanning 8ms window blocks. If the volume envelope
       * barely shifts (diff <= OBSERVE_TOLERANCE) between consecutive blocks,
       * we consider the room "acoustically stable".
       */
      int16_t diff = block_peak - prev_block_peak;
      if (diff < 0)
        diff = -diff;

      if (diff <= OBSERVE_TOLERANCE) {
        /* The room is quiet/flat. Increment our stable observation counter. */
        stable_run++;

        /* Continuously track the absolute maximum peak while in this stable
         * phase */
        if (block_peak > observe_peak)
          observe_peak = block_peak;

        /* If the room remains perfectly stable for a full Observation Cycle
         * (e.g. 50 blocks = 400ms), we confidently lock the Silence Bar
         * precisely at the highest peak observed! */
        if (stable_run >= OBSERVE_CYCLE_BLOCKS) {
          silence_bar = observe_peak;
          prev_silence_bar = silence_bar;
          gate_active = 1;

          /* Reset observer counters to begin searching for the next valid lock
           */
          stable_run = 0;
          observe_peak = block_peak;
          over_bar_run = 0;
        }
      } else {
        /* The envelope jumped! This could be a voice or a micro-spike of
         * static. Shatter the stable chain, preventing it from incorrectly
         * locking a new floor here. */
        stable_run = 0;
        observe_peak = block_peak;
      }

      prev_block_peak = block_peak;

      /* Rule 2: Acoustic Hysteresis (Opening the Gate)
       * We never open the gate on a single jump. DC electrical hum often
       * micro-spikes. To prove it's a real sound (like a human voice), the
       * amplitude must breach ABOVE the established silence bar AND sustain
       * that breach for 3 consecutive blocks (24ms). This mathematically
       * eliminates pop-noise while keeping speech attack times flawlessly
       * sharp.
       */
      if (gate_active) {
        if (block_peak > silence_bar) {
          over_bar_run++;
          if (over_bar_run >= 3) { /* Sustained breach authorized */
            gate_active = 0; /* Disable gate: let the full raw voice waveform through! */
            over_bar_run = 0;
          }
        } else {
          over_bar_run = 0; /* Failed to sustain breach, ignore the pop */
        }
      }

      /* Rule 3: The Fallback Timeout (Closing the Gate)
       * When human speech finishes, the room drops back to silence. However,
       * micro-jittering DC noise might prevent Rule 1 from finding perfect
       * stability to immediately re-lock the gate. Fallback: If we are
       * currently "open", but the volume envelope falls entirely beneath the
       * last-known silence bar for 400ms seamlessly, we assume speech is over
       * and manually snap the gate back shut using the old threshold.
       */
      if (!gate_active && prev_silence_bar > 0) {
        if (block_peak <= prev_silence_bar) {
          below_bar_run++;
          if (below_bar_run >= OBSERVE_CYCLE_BLOCKS) {
            gate_active = 1;
            silence_bar = prev_silence_bar;
            below_bar_run = 0;
            over_bar_run = 0;
          }
        } else {
          below_bar_run = 0;
        }
      }

      /* 2. Apply Gating (If Active and below the bar envelope limit) */
      int32_t final_peak = 0;
      for (int i = 0; i < 128; i++) {
        int16_t raw_v = pcm_buf[i];
        int16_t abs_v = (raw_v < 0) ? -raw_v : raw_v;

        if (gate_active && abs_v <= silence_bar) {
          pcm_buf[i] = 0;
          abs_v = 0;
        }

        if (abs_v > final_peak)
          final_peak = abs_v;
      }

      /* Visual VU Meter */
      int len = 0;
      int bars = (int)(final_peak /
                       20); /* Scale sensitivity for voice values (400-800) */
      if (bars > 50)
        bars = 50;

      for (int i = 0; i < bars; i++)
        uart_buf[i] = '|';
      len = bars;

      len += snprintf(uart_buf + len, sizeof(uart_buf) - len,
                      " raw:%d out:%ld | bar:%d %s\r\n", block_peak, final_peak,
                      silence_bar, gate_active ? "GATE" : "PASS");
      HAL_UART_Transmit_DMA(&huart2, (uint8_t *)uart_buf, len);
    }
  }
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
    Error_Handler();
  }
}

/**
 * @brief CRC Initialization Function
 * @param None
 * @retval None
 */
static void MX_CRC_Init(void) {

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */
}

/**
 * @brief I2S2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2S2_Init(void) {

  /* USER CODE BEGIN I2S2_Init 0 */

  /* USER CODE END I2S2_Init 0 */

  /* USER CODE BEGIN I2S2_Init 1 */

  /* USER CODE END I2S2_Init 1 */
  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_RX;
  hi2s2.Init.Standard = I2S_STANDARD_LSB;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = 64000;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  if (HAL_I2S_Init(&hi2s2) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN I2S2_Init 2 */

  /* USER CODE END I2S2_Init 2 */
}

/**
 * @brief USART2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART2_UART_Init(void) {

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */
}

/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void) {

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
  /* DMA1_Stream6_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
