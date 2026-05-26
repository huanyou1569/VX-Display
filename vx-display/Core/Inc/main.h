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
#include "stm32h7xx_hal.h"

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

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_LINE19_Pin GPIO_PIN_2
#define LED_LINE19_GPIO_Port GPIOE
#define LED_LINE20_Pin GPIO_PIN_3
#define LED_LINE20_GPIO_Port GPIOE
#define LED_LINE21_Pin GPIO_PIN_4
#define LED_LINE21_GPIO_Port GPIOE
#define LED_LINE22_Pin GPIO_PIN_5
#define LED_LINE22_GPIO_Port GPIOE
#define LED_LINE23_Pin GPIO_PIN_6
#define LED_LINE23_GPIO_Port GPIOE
#define HAL_Pin GPIO_PIN_5
#define HAL_GPIO_Port GPIOC
#define HAL_EXTI_IRQn EXTI9_5_IRQn
#define LED_LINE1_Pin GPIO_PIN_0
#define LED_LINE1_GPIO_Port GPIOB
#define LED_LINE2_Pin GPIO_PIN_1
#define LED_LINE2_GPIO_Port GPIOB
#define LED_LINE3_Pin GPIO_PIN_2
#define LED_LINE3_GPIO_Port GPIOB
#define LED_LINE24_Pin GPIO_PIN_7
#define LED_LINE24_GPIO_Port GPIOE
#define LED_LINE25_Pin GPIO_PIN_8
#define LED_LINE25_GPIO_Port GPIOE
#define LED_LINE26_Pin GPIO_PIN_9
#define LED_LINE26_GPIO_Port GPIOE
#define LED_LINE27_Pin GPIO_PIN_10
#define LED_LINE27_GPIO_Port GPIOE
#define LED_LINE28_Pin GPIO_PIN_11
#define LED_LINE28_GPIO_Port GPIOE
#define LED_LINE29_Pin GPIO_PIN_12
#define LED_LINE29_GPIO_Port GPIOE
#define LED_LINE30_Pin GPIO_PIN_13
#define LED_LINE30_GPIO_Port GPIOE
#define LED_LINE31_Pin GPIO_PIN_14
#define LED_LINE31_GPIO_Port GPIOE
#define LED_LINE32_Pin GPIO_PIN_15
#define LED_LINE32_GPIO_Port GPIOE
#define LED_LINE11_Pin GPIO_PIN_10
#define LED_LINE11_GPIO_Port GPIOB
#define LED_LINE12_Pin GPIO_PIN_11
#define LED_LINE12_GPIO_Port GPIOB
#define LED_LINE13_Pin GPIO_PIN_12
#define LED_LINE13_GPIO_Port GPIOB
#define LED_LINE14_Pin GPIO_PIN_13
#define LED_LINE14_GPIO_Port GPIOB
#define LED_LINE15_Pin GPIO_PIN_14
#define LED_LINE15_GPIO_Port GPIOB
#define LED_LINE16_Pin GPIO_PIN_15
#define LED_LINE16_GPIO_Port GPIOB
#define LED_LINE4_Pin GPIO_PIN_3
#define LED_LINE4_GPIO_Port GPIOB
#define LED_LINE5_Pin GPIO_PIN_4
#define LED_LINE5_GPIO_Port GPIOB
#define LED_LINE6_Pin GPIO_PIN_5
#define LED_LINE6_GPIO_Port GPIOB
#define LED_LINE7_Pin GPIO_PIN_6
#define LED_LINE7_GPIO_Port GPIOB
#define LED_LINE8_Pin GPIO_PIN_7
#define LED_LINE8_GPIO_Port GPIOB
#define LED_LINE9_Pin GPIO_PIN_8
#define LED_LINE9_GPIO_Port GPIOB
#define LED_LINE10_Pin GPIO_PIN_9
#define LED_LINE10_GPIO_Port GPIOB
#define LED_LINE17_Pin GPIO_PIN_0
#define LED_LINE17_GPIO_Port GPIOE
#define LED_LINE18_Pin GPIO_PIN_1
#define LED_LINE18_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
