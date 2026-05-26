/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins
     PH0-OSC_IN (PH0)   ------> RCC_OSC_IN
     PH1-OSC_OUT (PH1)   ------> RCC_OSC_OUT
     PA13 (JTMS/SWDIO)   ------> DEBUG_JTMS-SWDIO
     PA14 (JTCK/SWCLK)   ------> DEBUG_JTCK-SWCLK
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, LED_LINE19_Pin|LED_LINE20_Pin|LED_LINE21_Pin|LED_LINE22_Pin
                          |LED_LINE23_Pin|LED_LINE24_Pin|LED_LINE25_Pin|LED_LINE26_Pin
                          |LED_LINE27_Pin|LED_LINE28_Pin|LED_LINE29_Pin|LED_LINE30_Pin
                          |LED_LINE31_Pin|LED_LINE32_Pin|LED_LINE17_Pin|LED_LINE18_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LED_LINE1_Pin|LED_LINE2_Pin|LED_LINE3_Pin|LED_LINE11_Pin
                          |LED_LINE12_Pin|LED_LINE13_Pin|LED_LINE14_Pin|LED_LINE15_Pin
                          |LED_LINE16_Pin|LED_LINE4_Pin|LED_LINE5_Pin|LED_LINE6_Pin
                          |LED_LINE7_Pin|LED_LINE8_Pin|LED_LINE9_Pin|LED_LINE10_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LED_LINE19_Pin LED_LINE20_Pin LED_LINE21_Pin LED_LINE22_Pin
                           LED_LINE23_Pin LED_LINE24_Pin LED_LINE25_Pin LED_LINE26_Pin
                           LED_LINE27_Pin LED_LINE28_Pin LED_LINE29_Pin LED_LINE30_Pin
                           LED_LINE31_Pin LED_LINE32_Pin LED_LINE17_Pin LED_LINE18_Pin */
  GPIO_InitStruct.Pin = LED_LINE19_Pin|LED_LINE20_Pin|LED_LINE21_Pin|LED_LINE22_Pin
                          |LED_LINE23_Pin|LED_LINE24_Pin|LED_LINE25_Pin|LED_LINE26_Pin
                          |LED_LINE27_Pin|LED_LINE28_Pin|LED_LINE29_Pin|LED_LINE30_Pin
                          |LED_LINE31_Pin|LED_LINE32_Pin|LED_LINE17_Pin|LED_LINE18_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : HAL_Pin */
  GPIO_InitStruct.Pin = HAL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(HAL_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_LINE1_Pin LED_LINE2_Pin LED_LINE3_Pin LED_LINE11_Pin
                           LED_LINE12_Pin LED_LINE13_Pin LED_LINE14_Pin LED_LINE15_Pin
                           LED_LINE16_Pin LED_LINE4_Pin LED_LINE5_Pin LED_LINE6_Pin
                           LED_LINE7_Pin LED_LINE8_Pin LED_LINE9_Pin LED_LINE10_Pin */
  GPIO_InitStruct.Pin = LED_LINE1_Pin|LED_LINE2_Pin|LED_LINE3_Pin|LED_LINE11_Pin
                          |LED_LINE12_Pin|LED_LINE13_Pin|LED_LINE14_Pin|LED_LINE15_Pin
                          |LED_LINE16_Pin|LED_LINE4_Pin|LED_LINE5_Pin|LED_LINE6_Pin
                          |LED_LINE7_Pin|LED_LINE8_Pin|LED_LINE9_Pin|LED_LINE10_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PD5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
