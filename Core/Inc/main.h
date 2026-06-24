/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32u5xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
void SystemClock_Config(void);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PWR_EN_Pin GPIO_PIN_0
#define PWR_EN_GPIO_Port GPIOA
#define BOT_RED_Pin GPIO_PIN_3
#define BOT_RED_GPIO_Port GPIOA
#define BOT_BLUE_Pin GPIO_PIN_4
#define BOT_BLUE_GPIO_Port GPIOA
#define BOT_GREEN_Pin GPIO_PIN_5
#define BOT_GREEN_GPIO_Port GPIOA
#define TOP_RED_Pin GPIO_PIN_7
#define TOP_RED_GPIO_Port GPIOA
#define TOP_BLUE_Pin GPIO_PIN_0
#define TOP_BLUE_GPIO_Port GPIOB
#define TOP_GREEN_Pin GPIO_PIN_1
#define TOP_GREEN_GPIO_Port GPIOB
#define TOP_RST_SHUT_Pin GPIO_PIN_15
#define TOP_RST_SHUT_GPIO_Port GPIOB
#define TOP_WAKE_Pin GPIO_PIN_8
#define TOP_WAKE_GPIO_Port GPIOA
#define TOP_ALERT_Pin GPIO_PIN_9
#define TOP_ALERT_GPIO_Port GPIOA
#define TOP_ALERT_EXTI_IRQn EXTI9_IRQn
#define BOT_DFETOFF_Pin GPIO_PIN_15
#define BOT_DFETOFF_GPIO_Port GPIOA
#define BOT_RST_SHUT_Pin GPIO_PIN_3
#define BOT_RST_SHUT_GPIO_Port GPIOB
#define BOT_WAKE_Pin GPIO_PIN_4
#define BOT_WAKE_GPIO_Port GPIOB
#define BOT_ALERT_Pin GPIO_PIN_5
#define BOT_ALERT_GPIO_Port GPIOB
#define BOT_ALERT_EXTI_IRQn EXTI5_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
