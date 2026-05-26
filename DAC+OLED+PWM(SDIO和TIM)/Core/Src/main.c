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
#include "adc.h"
#include "dac.h"
#include "dma.h"
#include "fatfs.h"
#include "i2c.h"
#include "sdio.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_state.h"
#include "ui_menu.h"
#include "ui_input.h"
#include "motor_control.h"
#include "audio_control.h"
#include "ble_handler.h"
#include "oled.h"
#include "font.h"
#include "audio_player.h"
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

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_DAC_Init();
  MX_SDIO_SD_Init();
  MX_TIM6_Init();
  MX_FATFS_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  MX_ADC1_Init();
  MX_TIM7_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  // 音频系统初始化
  AudioControl_Init();
  HAL_Delay(100);
  // OLED初始化并显示主菜单
  OLED_Init();
  DrawMainMenu(main_selected);
  // 电机初始化（PWM启动 + TIM7测速中断）
  MotorControl_Init();
  // 蓝牙中断接收启动
  BLE_Init();
  // 进入AT指令模式并配置为主机模式
  HAL_Delay(500);
  HAL_UART_Transmit(&huart1, (uint8_t *)"+++", 3, 100);
  HAL_Delay(500);
  HAL_UART_Transmit(&huart1, (uint8_t *)"AT+ROLE=1", 9, 500);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    // 音频缓冲填充
    AudioControl_Poll();

    // KEY2状态读取
    uint8_t key2_now = Input_ReadKey2();

    // OLED定时刷新（100ms间隔，根据menu_state绘制对应界面）
    DisplayRefresh();

    // 蓝牙接收处理（行解析、STATUS解析、LOAD超时）
    BLE_ProcessReceive();

    // 电机占空比渐变
    MotorControl_ProcessRamp();

    // 摇杆ADC读取 + 方向检测
    {
      ADC_ChannelConfTypeDef sConfig = {0};
      sConfig.Rank = 1; sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
      sConfig.Channel = ADC_CHANNEL_0;
      HAL_ADC_ConfigChannel(&hadc1, &sConfig);
      HAL_ADC_Start(&hadc1); HAL_ADC_PollForConversion(&hadc1, 10); adc_x_raw = HAL_ADC_GetValue(&hadc1);
      sConfig.Channel = ADC_CHANNEL_1;
      HAL_ADC_ConfigChannel(&hadc1, &sConfig);
      HAL_ADC_Start(&hadc1); HAL_ADC_PollForConversion(&hadc1, 10); adc_y_raw = HAL_ADC_GetValue(&hadc1);

      joy_up   = (adc_y_raw < 500)  ? 1 : 0;
      joy_down = (adc_y_raw > 3500) ? 1 : 0;
      joy_left  = (adc_x_raw < 500)  ? 1 : 0;
      joy_right = (adc_x_raw > 3500) ? 1 : 0;
      joy_btn   = (HAL_GPIO_ReadPin(KEY3_GPIO_Port, KEY3_Pin) == GPIO_PIN_RESET) ? 0 : 1;
    }

    // 死区检测（摇杆归中）
    Input_ProcessDeadZone();

    // 画笔模式方向发送（COLOR=JK, DRAW=JL/JR/JU/JD, 20ms间隔）
    Input_ProcessBrushDirection();

    // 贪吃蛇方向发送（JL/JR/JU/JD, 100ms间隔）
    Input_ProcessSnakeDirection();

    // 非画笔模式方向导航（菜单切换、参数调节）
    Input_ProcessNavigation();

    // 摇杆按钮处理（短按确认/进入，长按800ms返回）
    Input_ProcessJoyButton();

    // KEY2处理（电机微调、甜甜圈切色、画笔落笔/返回）
    Input_ProcessKey2(key2_now);

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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 6;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
// 霍尔传感器PD6 EXTI回调 → 电机测速
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    MotorControl_HallCallback(GPIO_Pin);
}

// 蓝牙USART1接收回调 → 环形缓冲
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BLE_UartRxCallback(huart);
}
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

#ifdef  USE_FULL_ASSERT
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
