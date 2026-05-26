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
#include "dma.h"
#include "fatfs.h"
#include "rtc.h"
#include "sdmmc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "led_buffer.h"
#include "ws2812_driver.h"
#include "ws2812_code.h"
#include "ws2812_enable.h"
#include "ws2812_gpio.h"
#include "volume_buffer.h"
#include "volume_draw.h"
#include "volume_to_ws2812.h"
#include "volume_hall.h"
#include "motor_control.h"
#include <string.h>
#include <stdio.h>
#include "sd_animation.h"
#include "bt_commands.h"
#include "anim_player.h"
#include "anim_player_v2.h"
#include "anim_player_v3.h"
#include "slice_player.h"
#include "interaction_sphere.h"
#include "brush_mode.h"
#include "snake_game.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define WS2812_TEST_LED_COUNT      24

/* 你使用 GPIOB 的 16 路和 GPIOD 的 16 路 */


/*
 * 这个值要等于你测试用 TIM 的计数频率，单位 MHz。
 *
 * 如果 TIM 计数频率是 200MHz，填 200。
 * 如果 TIM 计数频率是 100MHz，填 100。
 *
 * 你前面 TIM ARR=82 用于 0.416us slot，推测 TIM 频率大概率接近 200MHz，
 * 所以先用 200 测试。
 */
#define WS2812_TIM_TICKS_PER_US   80U

// WS2812 时序参数 @ 80MHz
// T0H = 350ns (28 ticks), T1H = 700ns (56 ticks)
// bit 周期统一 1.25us (100 ticks)
#define WS2812_T0H_TICKS       84U
#define WS2812_T1H_TICKS       168U
#define WS2812_BIT_TICKS       300U

#define WS2812_RESET_US        500U
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
int flag_test=0;
extern TIM_HandleTypeDef htim1;
extern DMA_HandleTypeDef hdma_tim1_up;
extern TIM_HandleTypeDef htim2;
extern DMA_HandleTypeDef hdma_tim2_up;
#define WS2812_TEST_LED_COUNT      24
uint32_t s_phase_count  = 0;          // 累计渲染相位次数 (TIM3 ISR 引用)
static uint32_t s_last_count   = 0;          // 上一次上报时的计数
static uint32_t s_last_hw_ref   = 0;         // 上一次上报时的硬件刷新计数
static uint32_t s_last_hw_skp   = 0;         // 上一次上报时的跳过计数

// 蓝牙串口缓冲区 —— 必须放在 DMA 可访问的内存区域（DTCM 不行！）
static uint8_t rx_data[256] __attribute__((section(".RAM_D1")));
static uint8_t tx_buf[256] __attribute__((section(".RAM_D1")));

/*
 * 共享显示缓冲 (AXI SRAM, 225KB)
 * - 静态模式: slice_player 使用 (100片)
 * - 动画模式: anim_player 前端帧使用
 */
uint8_t g_display_buf[100 * 2304]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

/*
 * 共享 RAM_D2 缓冲 (225KB)
 * - :SAVE 模式: bt_commands 文件接收使用
 * - 动画模式: anim_player 后台帧使用
 */
uint8_t g_ramd2_buf[230416]
    __attribute__((section(".RAM_D1"), used, aligned(32)));
static uint32_t last_report_ticks = 0;   // 上一次蓝牙上报的 DWT 时间戳
static uint8_t  rx_needs_restart = 0;    // 标记：TX 完成后是否需要重启 RX
#define BT_REPORT_INTERVAL_US  1000000U  // 蓝牙状态上报间隔 1 秒


static volatile uint8_t is_need_reload_v3;  /* JE退出IS, 主循环重载V3 */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
    SCB_EnableICache();
    SCB_EnableDCache();

    /* MPU: AXI SRAM (0x24000000) 设不可缓存, SDMMC IDMA 区不走 cache */
    {
        MPU_Region_InitTypeDef MPU_InitStruct = {0};
        HAL_MPU_Disable();
        /* Region 0 保持 CubeMX 原样 */
        MPU_InitStruct.Enable = MPU_REGION_ENABLE;
        MPU_InitStruct.Number = MPU_REGION_NUMBER0;
        MPU_InitStruct.BaseAddress = 0x0;
        MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
        MPU_InitStruct.SubRegionDisable = 0x87;
        MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
        MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
        MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
        MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
        MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
        MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
        HAL_MPU_ConfigRegion(&MPU_InitStruct);
        /* Region 1: AXI SRAM 不可缓存 */
        MPU_InitStruct.Number = MPU_REGION_NUMBER1;
        MPU_InitStruct.BaseAddress = 0x24000000;
        MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
        MPU_InitStruct.SubRegionDisable = 0x00;
        MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
        MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
        MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
        MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
        MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
        HAL_MPU_ConfigRegion(&MPU_InitStruct);
        HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    }
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_UART4_Init();
  MX_SDMMC1_SD_Init();
  MX_TIM5_Init();
  MX_FATFS_Init();
  MX_RTC_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
    HAL_Delay(500); // 等待系统稳定
    VolumeHall_Init();                 // 启动 TIM5 + DWT + 霍尔计数
    MotorControl_Init();              // 启动电机转速计算
    VolumeToWS2812_Init();
    int ret = SD_LoadStaticModel("chiken.bin");
    if (ret != 0) {
        while (1) { __disable_irq(); }
    }

  /* 切片加载必须在 UART DMA 启动之前，避免 BLE 中断干扰 SD 卡读 */
  /*
   * SDMMC IDMA → RAM_D2 写入测试。
   * 验证 f_read 目标缓冲放在 RAM_D2 (0x30000000) 时 IDMA 是否能
   * 正确写入。失败时 MCU 停在此处。
   */
  {
      static uint8_t test_buf[16] __attribute__((section(".RAM_D1")));
      static FIL     test_fp __attribute__((section(".RAM_AXI")));
      UINT           br;
      if (f_open(&test_fp, "chiken.bin", FA_READ) != FR_OK
          || f_read(&test_fp, test_buf, 16, &br) != FR_OK
          || br != 16
          || memcmp(test_buf, "VXAN", 4) != 0) {
          while (1) { __disable_irq(); }
      }
      f_close(&test_fp);
  }

  SlicePlayer_Load("chiken.slices");  /* 确保 FS 已挂载 */

  if (AnimV3_Load("dna_v3_3t.slices") != 0) {
    if (AnimV2_Load("anim_rainbow.slices") != 0) {
      if (AnimPlayer_Load("anim_rainbow.slices") != 0) {
          /* V3/V2/V1动画均失败, 继续用静态 (SlicePlayer 已激活) */
      }
    }
  }

  IS_Init();  /* 粒子球交互系统: 预计算粒子 + 初始化双缓冲 */
  Brush_Init();  /* 3D画笔模式: 画布 + 光标 */
  Snake_Init();  /* 3D贪吃蛇游戏 */
  // TIM3 驱动相位刷新: 初始 1917us (313RPM 等效), 稳速后由霍尔自动更新
  htim3.Instance->ARR = 999;   // 1MHz tick, ARR+1=1000µs → 600RPM 默认
  __HAL_TIM_SET_COUNTER(&htim3, 0);
  HAL_TIM_Base_Start_IT(&htim3);
//   HAL_Delay(100);
//   HAL_UART_Transmit(&huart4, (uint8_t*)"AT+TRANMD=1", 11, 100);
//   HAL_Delay(100);
//   HAL_UART_Transmit(&huart4, (uint8_t*)"AT+RESET", 8, 100);
//   HAL_Delay(500);
  /* 蓝牙模块 AT 配置
  // HAL_UART_Transmit(&huart4, (uint8_t*)"AT", 2, 100);           // 测试通信（不需 +++）
  // HAL_Delay(100);
  // HAL_UART_Transmit(&huart4, (uint8_t*)"AT+ROLE=0", 9, 100);     // 从机模式
  // HAL_Delay(100);
  // HAL_UART_Transmit(&huart4, (uint8_t*)"AT+ADV=1", 8, 100);      // 开启广播
  // HAL_Delay(100);
  // HAL_UART_Transmit(&huart4, (uint8_t*)"AT+TRANMD=1", 11, 100);  // 透传模式（关键！)
  // HAL_Delay(100);
  // HAL_UART_Transmit(&huart4, (uint8_t*)"AT+RESET", 8, 100);      // 重启生效
  // HAL_Delay(500);*/
  // 使用Ex函数，接收不定长数据，开启蓝牙接收数据功能
  HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
  // 关闭DMA传输过半中断（HAL库默认开启，但我们只需要接收完成中断）
  __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);

  last_report_ticks = DWT->CYCCNT;  // 初始化蓝牙上报时间戳

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 相位由 TIM3 硬件触发, 主循环只做 IO */

    BT_PollVoxelReady();
    BT_PollCommands();
    BT_PollSaveFile();

    AnimV3_ProcessSwap();
    AnimV2_ProcessSwap();
    AnimPlayer_ProcessSwap();
    IS_Update();
    Brush_Update();
    Snake_Update();

    if (is_need_reload_v3) {
        is_need_reload_v3 = 0;
        AnimV3_Load("anim_rainbow.slices");
    }

    /* 稳速时更新 TIM3 相位间隔 */
    if (MotorControl_IsStable()) {
        uint32_t interval_us = MotorControl_GetPhaseIntervalUs();
        if (interval_us > 0 && interval_us <= 65535) {
            htim3.Instance->ARR = interval_us - 1;
        }
    }

    AnimV3_PollAsyncLoad();
    // 优先发送 BT 指令产生的待发送回复（如 VOX:OK）
    if (BT_HasResponse() && huart4.hdmatx->State == HAL_DMA_STATE_READY
        && !rx_needs_restart)
    {
        uint16_t n = BT_GetResponseSize();
        memcpy(tx_buf, BT_GetResponse(), n);
        BT_ClearResponse();
        SCB_CleanDCache_by_Addr((uint32_t*)tx_buf, sizeof(tx_buf));
        HAL_UART_Transmit_DMA(&huart4, tx_buf, n);
    }

    // 蓝牙周期性上报转速和延时（每秒一次，TX DMA 空闲才发）
    {
        uint32_t now = DWT->CYCCNT;
        if (now - last_report_ticks >= BT_REPORT_INTERVAL_US * 480U)
        {
            last_report_ticks = now;
        if (huart4.hdmatx->State == HAL_DMA_STATE_READY)
        {
            uint16_t rpm_inst = VolumeHall_GetRPM();               // 瞬时转速
            uint8_t  full     = MotorControl_IsWindowFull();        // 窗口是否满
            uint8_t  stable   = MotorControl_IsStable();            // 稳速标志
            uint32_t hw_now  = WS2812_GetRefreshCount();
            uint32_t sk_now  = WS2812_GetSkipCount();
            int32_t  svp      = BT_GetSaveProgress();
            int len = snprintf((char*)tx_buf, sizeof(tx_buf),
                               "RPM:%u PPS:%lu HPS:%lu SKP:%lu BUS:%lu FILL:%lu F:%u S:%u",
                               rpm_inst,
                               (s_phase_count - s_last_count),
                               (hw_now - s_last_hw_ref),
                               (sk_now - s_last_hw_skp),
                               WS2812_GetBusyMaxUs(),
                               WS2812_GetFillMaxUs(),
                               full, stable);
            if (svp >= 0) {
                len += snprintf((char*)tx_buf + len, sizeof(tx_buf) - len,
                                " SVP:%ld/%lu",
                                (long)svp, (unsigned long)BT_GetSaveTotal());
            }
            if (AnimV3_IsActive()) {
                len += snprintf((char*)tx_buf + len, sizeof(tx_buf) - len,
                                " EXP:%lu BR:%u F:0x%02X SW:%lu LD:%lu LK:%lu FR:%lu MS:%lu ER:%u FO:%lu",
                                (unsigned long)AnimV3_GetExpandUs(),
                                (unsigned int)AnimV3_GetBrightness(),
                                (unsigned int)AnimV3_GetDiagFlags(),
                                (unsigned long)AnimV3_GetDiagSwapOk(),
                                (unsigned long)AnimV3_GetDiagLoadOk(),
                                (unsigned long)AnimV3_GetDiagLseekFail(),
                                (unsigned long)AnimV3_GetDiagFreadFail(),
                                (unsigned long)AnimV3_GetDiagSwapMissed(),
                                (unsigned int)AnimV3_GetDiagFrErr(),
                                (unsigned long)AnimV3_GetDiagFrOff());
            } else if (AnimV2_IsActive()) {
                len += snprintf((char*)tx_buf + len, sizeof(tx_buf) - len,
                                " EXP:%lu BR:%u",
                                (unsigned long)AnimV2_GetExpandUs(),
                                (unsigned int)AnimV2_GetBrightness());
            } else if (Snake_IsActive()) {
                len += Snake_GetDiag((char*)tx_buf + len,
                                     (uint16_t)(sizeof(tx_buf) - len));
            } else if (Brush_IsActive()) {
                len += Brush_GetDiag((char*)tx_buf + len,
                                     (uint16_t)(sizeof(tx_buf) - len));
            } else if (IS_IsActive()) {
                len += snprintf((char*)tx_buf + len, sizeof(tx_buf) - len,
                                " IS_R:%u T:%d BR:%u F:0x%02X US:%lu"
                                " RND:%lu HAL:%lu SWP:%lu GEN:%lu PRT:%u",
                                (unsigned int)IS_GetRadius(),
                                (int)IS_GetColorTemp(),
                                (unsigned int)IS_GetBrightness(),
                                (unsigned int)IS_GetFlags(),
                                (unsigned long)IS_GetExpandUs(),
                                (unsigned long)IS_GetDiagRenderCalls(),
                                (unsigned long)IS_GetDiagHallEdges(),
                                (unsigned long)IS_GetDiagSwaps(),
                                (unsigned long)IS_GetDiagGenerations(),
                                (unsigned int)IS_GetDiagParticlesSet());
            }
            len += snprintf((char*)tx_buf + len, sizeof(tx_buf) - len, "\r\n");
            s_last_count   = s_phase_count;
            s_last_hw_ref  = hw_now;
            s_last_hw_skp  = sk_now;
            SCB_CleanDCache_by_Addr((uint32_t*)tx_buf, sizeof(tx_buf));
            HAL_UART_Transmit_DMA(&huart4, tx_buf, len);
        }

    }
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 80;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 3;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enables the Clock Security System
  */
  HAL_RCC_EnableCSS();
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == HAL_Pin)
    {
        VolumeHall_OnTrigger();          // 记录时间戳
        MotorControl_ProcessHallTick();   // 更新转速计算
        VolumeToWS2812_OnHallEdge();      // 复位相位到 0，同步电机位置
        SlicePlayer_OnHallEdge();         // 切片播放器同步
        AnimPlayer_OnHallEdge();          // 动画播放器同步
        AnimV3_OnHallEdge();             // V3动画播放器同步
        AnimV2_OnHallEdge();             // V2动画播放器同步
        IS_OnHallEdge();                 // 粒子球交互同步
        Brush_OnHallEdge();              // 画笔模式同步
        Snake_OnHallEdge();              // 贪吃蛇同步
    }
}
// 不定长数据接收完成回调函数（空闲线检测触发）
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == UART4)
    {
        // DMA 写了 rx_data，CPU 读之前必须先 invalidate D-Cache
        SCB_InvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));

        /* 文件接收模式: 追加数据到文件缓冲，不解析指令 */
        if (BT_IsSaveReceiving()) {
            BT_FeedFileData(rx_data, Size);
            /* 重启空闲DMA接收下一段 */
            SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
            HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
            __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
            return;
        }

        /* 摇杆指令 */
        if (Size >= 2 && rx_data[0] == 'J') {
            /* JK=切颜色 (IS和Brush共用) */
            if (rx_data[1] == 'K') {
                if (Brush_IsActive())
                    Brush_CycleColor();
                else if (IS_IsActive())
                    IS_CycleColor();
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
                return;
            }
            /* 贪吃蛇模式优先 */
            if (Snake_HandleJoy(rx_data, Size)) {
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
                return;
            }
            /* 画笔模式: 处理移动/按钮/画笔/退出 */
            if (Brush_HandleJoy(rx_data, Size)) {
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
                return;
            }
            /* JE = 退出当前交互模式 (画笔已在上方Brush_HandleJoy处理) */
            if (rx_data[1] == 'E') {
                if (IS_IsActive()) { IS_Deactivate(); is_need_reload_v3 = 1; }
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
                return;
            }
            /* 画笔/贪吃蛇激活时不触发IS */
            if (Brush_IsActive() || Snake_IsActive()) {
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
                return;
            }
            /* IS 粒子球: 首次摇杆自动激活 */
            if (!IS_IsActive()) {
                AnimV3_Deactivate();
                IS_Activate();
            }
            if (rx_data[1] == '+') {
                IS_SetRotation(1);
            } else if (rx_data[1] == '-') {
                IS_SetRotation(-1);
            } else if (rx_data[1] == 'T' && Size >= 4) {
                int sign = 1, idx = 2; int8_t val = 0;
                if (rx_data[idx] == '-') { sign = -1; idx++; }
                while (idx < (int)Size && rx_data[idx] >= '0' && rx_data[idx] <= '9')
                    val = (int8_t)(val * 10 + (rx_data[idx++] - '0'));
                IS_SetColorTemp((int8_t)(sign * val));
            }
            /* 消耗指令, 重启DMA接收 */
            SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
            HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
            __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
            return;
        }

        /* :SNAKE 或 SNAKE 激活贪吃蛇模式 */
        {
            const uint8_t *cmd = NULL;
            uint8_t cmd_len = 0;
            if (Size >= 6 && memcmp(rx_data, ":SNAKE", 6) == 0)
                { cmd = rx_data; cmd_len = 6; }
            else if (Size >= 5 && memcmp(rx_data, "SNAKE", 5) == 0)
                { cmd = rx_data; cmd_len = 5; }
            if (cmd && (Size == cmd_len || rx_data[cmd_len] == '\n' || rx_data[cmd_len] == '\r')) {
                if (!Snake_IsActive()) {
                    if (IS_IsActive()) IS_Deactivate();
                    Brush_Deactivate();
                    AnimV3_Deactivate();
                    Snake_Activate();
                }
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
                return;
            }
        }

        /* :BRUSH 或 BRUSH 激活画笔模式 (允许无冒号) */
        {
            const uint8_t *cmd = NULL;
            uint8_t cmd_len = 0;
            if (Size >= 6 && memcmp(rx_data, ":BRUSH", 6) == 0) {
                cmd = rx_data; cmd_len = 6;
            } else if (Size >= 5 && memcmp(rx_data, "BRUSH", 5) == 0) {
                cmd = rx_data; cmd_len = 5;
            }
            if (cmd && (Size == cmd_len || rx_data[cmd_len] == '\n' || rx_data[cmd_len] == '\r')) {
                if (!Brush_IsActive()) {
                    if (IS_IsActive()) IS_Deactivate();
                    AnimV3_Deactivate();
                    Brush_Activate();
                }
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
                return;
            }
        }

        /* :CLEAR 或 CLEAR 清空画布 */
        {
            const uint8_t *cmd = NULL;
            uint8_t cmd_len = 0;
            if (Size >= 6 && memcmp(rx_data, ":CLEAR", 6) == 0) {
                cmd = rx_data; cmd_len = 6;
            } else if (Size >= 5 && memcmp(rx_data, "CLEAR", 5) == 0) {
                cmd = rx_data; cmd_len = 5;
            }
            if (cmd && (Size == cmd_len || rx_data[cmd_len] == '\n' || rx_data[cmd_len] == '\r')) {
                Brush_ClearCanvas();
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
                return;
            }
        }

        /* :BRIGHT 指令: IS模式下直接拦截, 不经过BT_ProcessCommand */
        if (Size >= 8 && IS_IsActive() && memcmp(rx_data, ":BRIGHT ", 8) == 0) {
            uint32_t val = 0;
            int i = 8;
            while (i < (int)Size && rx_data[i] >= '0' && rx_data[i] <= '9')
                val = val * 10 + (rx_data[i++] - '0');
            if (val >= 1 && val <= 100)
                IS_SetBrightness((uint8_t)val);
            SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
            HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
            __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
            return;
        }

        /* 先尝试作为指令处理 */
        if (BT_ProcessCommand(rx_data, Size)) {
            /* 指令已处理，检查是否有回复需要发送 */
            if (BT_HasResponse()) {
                uint16_t n = BT_GetResponseSize();
                memcpy(tx_buf, BT_GetResponse(), n);
                BT_ClearResponse();
                SCB_CleanDCache_by_Addr((uint32_t*)tx_buf, sizeof(tx_buf));
                rx_needs_restart = 1;
                HAL_UART_Transmit_DMA(&huart4, tx_buf, n);
            } else if (!BT_IsReceiving()) {
                /* 无需回复且不在接收模式: 重启空闲DMA */
                SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
                HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
                __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
            }
            /* BT_IsReceiving() 为真时不重启 —— 固定长度DMA已接管 */
        } else {
            /* 非指令，按原逻辑回传 */
            memcpy(tx_buf, rx_data, Size);
            SCB_CleanDCache_by_Addr((uint32_t*)tx_buf, sizeof(tx_buf));
            rx_needs_restart = 1;
            HAL_UART_Transmit_DMA(&huart4, tx_buf, Size);
        }
    }
}

// DMA 发送完成回调 —— 收发完成后重新打开空闲DMA接收
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4)
    {
        if (rx_needs_restart)
        {
            rx_needs_restart = 0;
            SCB_CleanInvalidateDCache_by_Addr((uint32_t*)rx_data, sizeof(rx_data));
            HAL_UARTEx_ReceiveToIdle_DMA(&huart4, rx_data, sizeof(rx_data));
            __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
        }
    }
}

// 固定长度DMA接收完成回调 —— 体素数据收完
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) return;

    BT_OnRxComplete(huart);

    /* 发送回复并恢复空闲DMA */
    if (BT_HasResponse()) {
        /* TX DMA 可能正忙（周期上报），若忙则跳过回复 */
        if (huart4.hdmatx->State == HAL_DMA_STATE_READY) {
            uint16_t n = BT_GetResponseSize();
            memcpy(tx_buf, BT_GetResponse(), n);
            BT_ClearResponse();
            SCB_CleanDCache_by_Addr((uint32_t*)tx_buf, sizeof(tx_buf));
            rx_needs_restart = 1;
            HAL_UART_Transmit_DMA(&huart4, tx_buf, n);
        } else {
            BT_ClearResponse();
            rx_needs_restart = 1;
            /* TX 完成回调会重启空闲 DMA */
        }
    }
}
// SD 卡检测 —— 强制永远在线
uint8_t BSP_SD_IsDetected(void)
{
    return SD_PRESENT;
}

// SD 卡初始化 —— 卡已在 TRANSFER, 只变 CLKCR 时钟分频, 不调 ConfigWideBusOperation
uint8_t BSP_SD_Init(void)
{
    if (BSP_SD_IsDetected() != SD_PRESENT)
        return MSD_ERROR_SD_NOT_PRESENT;

    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    __HAL_SD_CLEAR_FLAG(&hsd1, SDMMC_STATIC_FLAGS);

    /* 只改 ClockDiv, 保持 InitCard 的 1-bit 模式, 不动 POWER */
    hsd1.Instance->CLKCR = (hsd1.Instance->CLKCR & ~0x3FFFU)
                         | (hsd1.Init.ClockDiv & 0x3FFFU);

    return MSD_OK;
}
/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
