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
#include "fdcan.h"
#include "i2c.h"
#include "icache.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "B_BMS.h"
#include "B_BMS_fan.h"
#include "B_BMS_init.h"
#include "B_TEST_BMS.h"
#include "B_BMS_power_mode.h"
#include "B_BMS_soc.h"
#include "B_BMS_protect.h"
#include "B_BMS_rtc.h"
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

/* USER CODE BEGIN PV */
extern BMS_Unit BMS[STACK];
extern uint8_t LV_BMS_initOK;
extern uint16_t LV_BMS_running;
extern unsigned int RX_CRC_Fail;
extern unsigned int I2C_HAL_Fail;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void SystemPower_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void delayUS(uint32_t us)
{
    // DWT cycle counter based microsecond delay.
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    uint32_t cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * cycles_per_us;
    // TIM2 is reserved for fan PWM.
    while ((DWT->CYCCNT - start) < cycles)
    {
    }
}

void LV_STAT()
{
    static unsigned int prev_RX_CRC_Fail = 0;
    static unsigned int prev_I2C_HAL_Fail = 0;

    // 이번 주기에 새로 발생한 통신 오류(CRC 불일치 / I2C HAL 오류)가 있는지 확인
    uint8_t comm_error = ((RX_CRC_Fail != prev_RX_CRC_Fail) || (I2C_HAL_Fail != prev_I2C_HAL_Fail)) ? 1U : 0U;
    prev_RX_CRC_Fail = RX_CRC_Fail;
    prev_I2C_HAL_Fail = I2C_HAL_Fail;

    uint8_t fault = (BMS[TOP].ProtectionsTriggered || BMS[BOT].ProtectionsTriggered ||
                      BMS[TOP].PF_ProtectionsTriggered || BMS[BOT].PF_ProtectionsTriggered ||
                      comm_error) ? 1U : 0U;

    // 1. BMS 보호 로직(일반/영구 고장) 또는 통신 오류 발생 시
    if (fault)
    {
        // 에러 상태: RED LED 점등, GREEN LED 소등
        HAL_GPIO_WritePin(BOT_RED_GPIO_Port, BOT_RED_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(BOT_GREEN_GPIO_Port, BOT_GREEN_Pin, GPIO_PIN_RESET);
    }
    // 2. 정상 동작 시
    else
    {
        // 정상 상태: GREEN LED 점등, RED LED 소등
        HAL_GPIO_WritePin(BOT_GREEN_GPIO_Port, BOT_GREEN_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(BOT_RED_GPIO_Port, BOT_RED_Pin, GPIO_PIN_RESET);
    }

    // 3. MCU 보호 감시자 상태를 별도 LED로 구분한다.
    //    위의 RED/GREEN은 "BQ가 보호를 걸었다"를 나타내고, 아래는 "MCU가 개입했다"를 나타낸다.
    //    둘은 원인이 완전히 다르므로 현장에서 구분되어야 한다.
    //    BLUE  = MCU가 BOTHOFF를 걸고 있음
    //    TOP_RED = latch 상태(자동 복구 안 됨 — 사람이 확인해야 함)
    HAL_GPIO_WritePin(BOT_BLUE_GPIO_Port, BOT_BLUE_Pin,
                      BMS_Protect.bothoff_asserted ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TOP_RED_GPIO_Port, TOP_RED_Pin,
                      BMS_Protect.latched ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the System Power */
  SystemPower_Config();

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_FDCAN1_Init();
  MX_I2C2_Init();
  MX_I2C4_Init();
  MX_TIM2_Init();
  MX_ICACHE_Init();
  /* USER CODE BEGIN 2 */
  // 저전력 구간의 경과 시간 측정과 주기적 웨이크업에 쓴다. BQ 초기화보다 먼저 세워야
  // 첫 슬립부터 실측 시간이 적용된다. LSE/LSI 둘 다 실패하면 조용히 폴백 동작한다.
  BMS_RTC_Init();
  BMS_FanControl_Init();
  LV_BMS_MAIN_RUN();
  BMS_SOC_Init();
  BMS_Protect_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    bool already_ran_while_run = Handle_Wakeup_Event();
    if (!already_ran_while_run) {
      LV_BMS_WHILE_RUN();
    }
    // 데이터를 읽은 직후, 팬/CAN/LED보다 먼저 판정한다 (차단 결정이 가장 급하므로).
    BMS_Protect_Update();
    BMS_FanControl_Update();
    BMS_CAN_SendRunData(TOP);
    BMS_CAN_SendRunData(BOT);
    BMS_CAN_SendProtectDiag();
    LV_STAT();
    Enter_Sleep_Sequence();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_0;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV4;
  RCC_OscInitStruct.PLL.PLLM = 3;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 1;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_1;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Power Configuration
  * @retval None
  */
static void SystemPower_Config(void)
{

  /*
   * Switch to SMPS regulator instead of LDO
   */
  if (HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY) != HAL_OK)
  {
    Error_Handler();
  }
/* USER CODE BEGIN PWR */
/* USER CODE END PWR */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
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
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
