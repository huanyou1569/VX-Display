#include "ws2812_driver.h"
#include "stm32h743xx.h"
#define CACHE_LINE_SIZE 32U//定义要清理的缓存数据大小为32字节



extern TIM_HandleTypeDef htim1;
extern DMA_HandleTypeDef hdma_tim1_up;
extern TIM_HandleTypeDef htim2;
extern DMA_HandleTypeDef hdma_tim2_up;
static volatile uint8_t ws2812_busy_flag = 0;//ws2812的忙标志，0表示空闲，1表示忙碌，用于判断ws2812是否在发送信息
static volatile uint32_t ws2812_refresh_count = 0;  // 累计成功启动DMA的次数
static volatile uint32_t ws2812_skip_count    = 0;  // 累计因忙被跳过的次数
static volatile uint32_t ws2812_busy_start    = 0;  // 本次刷新开始时的 DWT
static volatile uint32_t ws2812_busy_max      = 0;  // 最近 1 秒内最长刷新周期
static volatile uint32_t ws2812_fillwave_max  = 0;  // FillWave+CleanCache 最长耗时
static volatile uint8_t dma_b_done = 0;//b引脚DMA传输完成标志
static volatile uint8_t dma_d_done = 0;//d引脚DMA传输完成标志
volatile uint32_t dma_remain_b_after_start = 0;
volatile uint32_t dma_remain_d_after_start = 0;
volatile uint32_t dma_remain_b_later = 0;
volatile uint32_t dma_remain_d_later = 0;
volatile uint32_t tim1_cnt_after_start = 0;
volatile uint32_t tim2_cnt_after_start = 0;
volatile uint32_t tim1_psc;
volatile uint32_t tim1_arr;
volatile uint32_t tim1_dier;
volatile uint32_t tim1_sr;

volatile uint32_t tim2_psc;
volatile uint32_t tim2_arr;
volatile uint32_t tim2_dier;
volatile uint32_t tim2_sr;

//判断是否在发送信息的函数
uint8_t WS2812_IsBusy(void)
{
    return ws2812_busy_flag;
}
//停止所有ws2812的DMA传输和定时器，关闭所有ws2812引脚输出，并清除忙标志
void WS2812_StopAll(void)
{
    HAL_TIM_Base_Stop(&htim1);
    HAL_TIM_Base_Stop(&htim2);

    __HAL_TIM_DISABLE_DMA(&htim1, TIM_DMA_UPDATE);
    __HAL_TIM_DISABLE_DMA(&htim2, TIM_DMA_UPDATE);

    GPIOB->BSRR = 0xFFFF0000UL;
    GPIOE->BSRR = 0xFFFF0000UL;

    /* 记录本次刷新总耗时 (CPU ticks) */
    {
        uint32_t elapsed = DWT->CYCCNT - ws2812_busy_start;
        if (elapsed > ws2812_busy_max) ws2812_busy_max = elapsed;
    }
    ws2812_busy_flag = 0;
}
//DMA传输完成后的回调函数，判断是哪个定时器的DMA传输完成了，并进行相应的处理
void WS2812_DMA_Complete(DMA_HandleTypeDef *hdma)
{
    if(hdma == &hdma_tim1_up)
    {
        HAL_TIM_Base_Stop(&htim1);
        __HAL_TIM_DISABLE_DMA(&htim1, TIM_DMA_UPDATE);

        GPIOB->BSRR = 0xFFFF0000UL;

        dma_b_done = 1;
    }
    if(hdma == &hdma_tim2_up)
    {
        HAL_TIM_Base_Stop(&htim2);
        __HAL_TIM_DISABLE_DMA(&htim2, TIM_DMA_UPDATE);

        GPIOE->BSRR = 0xFFFF0000UL;

        dma_d_done = 1;
    }
    if(dma_b_done && dma_d_done)
    {
        WS2812_StopAll();
    }
}
//dma传输错误后的回调函数
void WS2812_DMA_Error(DMA_HandleTypeDef *hdma)
{
    HAL_TIM_Base_Stop(&htim1);//先停止定时器tim1
    __HAL_TIM_DISABLE_DMA(&htim1,TIM_DMA_UPDATE);//再禁止tim1的dma请求
    HAL_TIM_Base_Stop(&htim2);//先停止定时器tim2
    __HAL_TIM_DISABLE_DMA(&htim2,TIM_DMA_UPDATE);//再禁止tim2的dma请求
    GPIOB->BSRR=0XFFFF0000UL;//最后将GPIOB的所有引脚拉低，结束ws2812的数据传输
    GPIOE->BSRR=0XFFFF0000UL;
    ws2812_busy_flag=0;//将忙标志清零，表示ws2812已经完成数据传输，可以继续发送下一条信息了
}
//ws2812数据发送函数
void WS2812_Send(void)
{
    if(ws2812_busy_flag)return;//当标志位位1时，直接返回
    dma_b_done = 0;//将b引脚的dma完成标志清零
    dma_d_done = 0;//将e引脚的dma完成标志清零
    ws2812_busy_flag=1;//将标志位置1，表示正在发送数据
    GPIOB->BSRR=0XFFFF0000UL;//先将GPIOB的所有引脚拉低，准备发送数据
    GPIOE->BSRR=0XFFFF0000UL;//先将GPIOE的所有引脚拉低，准备发送数据
    __HAL_TIM_SET_COUNTER(&htim1,0);//将定时器tim1的计数器清零
    __HAL_TIM_SET_COUNTER(&htim2,0);
    hdma_tim1_up.XferCpltCallback = WS2812_DMA_Complete;//设置dma完成传输后的回调函数
    hdma_tim1_up.XferErrorCallback = WS2812_DMA_Error;//设置dma传输错误后的回调函数
    hdma_tim2_up.XferCpltCallback = WS2812_DMA_Complete;//设置dma完成传输后的回调函数
    hdma_tim2_up.XferErrorCallback = WS2812_DMA_Error;
    volatile HAL_StatusTypeDef ret_b;//定义DMA传输状态变量
    volatile HAL_StatusTypeDef ret_e;
     ret_b = HAL_DMA_Start_IT(&hdma_tim1_up,(uint32_t)g_ws2812PortBBuffer,(uint32_t)&GPIOB->BSRR,WS2812_WAVE_BUFFER_SIZE);//启动dma传输，将ws2812数据从内存传输到GPIOB的BSRR寄存器
     ret_e = HAL_DMA_Start_IT(&hdma_tim2_up,(uint32_t)g_ws2812PortDBuffer,(uint32_t)&GPIOE->BSRR,WS2812_WAVE_BUFFER_SIZE);//启动dma传输，将ws2812数据从内存传输到GPIOE的BSRR寄存器
     dma_remain_b_after_start = __HAL_DMA_GET_COUNTER(&hdma_tim1_up);
     dma_remain_d_after_start = __HAL_DMA_GET_COUNTER(&hdma_tim2_up);
    if(ret_b != HAL_OK || ret_e != HAL_OK)//如果dma传输启动失败，直接调用错误处理函数
    {
        WS2812_DMA_Error(&hdma_tim1_up);
        return;
    }
    __HAL_TIM_ENABLE_DMA(&htim1,TIM_DMA_UPDATE);//使能tim1的dma请求
    __HAL_TIM_ENABLE_DMA(&htim2,TIM_DMA_UPDATE);//使能tim2的dma请求
     
    HAL_TIM_Base_Start(&htim1);//启动定时器tim1，开始发送ws2812数据
    HAL_TIM_Base_Start(&htim2);//启动定时器tim2，开始发送ws2812数据
    // HAL_Delay(1);//延时1毫秒，等待ws2812数据发送完成
    // for (int i = 0; i < 10; i++)
    // {
    // htim1.Instance->EGR = TIM_EGR_UG;
    // htim2.Instance->EGR = TIM_EGR_UG;
    // }
    dma_remain_b_later = __HAL_DMA_GET_COUNTER(&hdma_tim1_up);
    dma_remain_d_later = __HAL_DMA_GET_COUNTER(&hdma_tim2_up);
    tim1_cnt_after_start = __HAL_TIM_GET_COUNTER(&htim1);
    tim2_cnt_after_start = __HAL_TIM_GET_COUNTER(&htim2);
    tim1_psc  = htim1.Instance->PSC;
    tim1_arr  = htim1.Instance->ARR;
    tim1_dier = htim1.Instance->DIER;
    tim1_sr   = htim1.Instance->SR;

    tim2_psc  = htim2.Instance->PSC;
    tim2_arr  = htim2.Instance->ARR;
    tim2_dier = htim2.Instance->DIER;
    tim2_sr   = htim2.Instance->SR;
    __NOP();

    
}
//cpu缓存数据清理函数，确保ws2812数据能够正确地从内存传输到GPIO寄存器
static void WS2812_CleanDCache(void)
{
    uintptr_t addr_b = (uintptr_t)&g_ws2812PortBBuffer[0];//获取ws2812数据在内存中的地址
    uint32_t size_b = WS2812_WAVE_BUFFER_SIZE * sizeof(uint32_t);//计算ws2812数据的大小

    uintptr_t addr_d = (uintptr_t)&g_ws2812PortDBuffer[0];//获取ws2812数据在内存中的地址
    uint32_t size_d = WS2812_WAVE_BUFFER_SIZE * sizeof(uint32_t);//计算ws2812数据的大小

    uintptr_t start_b = addr_b & ~(uintptr_t)(CACHE_LINE_SIZE - 1U);//计算要清理的缓存数据的起始地址，向下对齐到cache line的边界
    uintptr_t end_b   = (addr_b + size_b + CACHE_LINE_SIZE - 1U)
                        & ~(uintptr_t)(CACHE_LINE_SIZE - 1U);//计算要清理的缓存数据的结束地址，向上对齐到cache line的边界

    uintptr_t start_d = addr_d & ~(uintptr_t)(CACHE_LINE_SIZE - 1U);
    uintptr_t end_d   = (addr_d + size_d + CACHE_LINE_SIZE - 1U)
                        & ~(uintptr_t)(CACHE_LINE_SIZE - 1U);

    SCB_CleanDCache_by_Addr((uint32_t *)start_b, (int32_t)(end_b - start_b));//调用CMSIS函数清理数据缓存，确保ws2812数据能够正确地从内存传输到GPIO寄存器
    SCB_CleanDCache_by_Addr((uint32_t *)start_d, (int32_t)(end_d - start_d));//调用CMSIS函数清理数据缓存，确保ws2812数据能够正确地从内存传输到GPIO寄存器
}

/*
 * 合并型填充: LED缓冲区 → PortB/PortD BSRR 波形 (单次遍历)
 *
 * 消除三个中间步骤:
 *   旧: FillBits → g_ws2812_TOTAL_BITS → Wave_Enable → g_ws2812WaveBuffer → port_encode → BSRR
 *   新: LED buffer → BSRR (直接)
 *
 * 每个 LED 的 24 位展开为 3×24=72 个 BSRR slot，LED 之间不跨位。
 * BSRR 编码: 低16位=置高, 高16位=置低
 */
static void WS2812_FillWave(void)
{
    uint32_t *pb = g_ws2812PortBBuffer;
    uint32_t *pd = g_ws2812PortDBuffer;

    const uint32_t ALLHI = 0x0000FFFFU;
    const uint32_t ALLLO = 0xFFFF0000U;

    for (uint8_t led = 0; led < LEDS_PER_STRIP; led++)
    {
        /* 收集该 LED 位置所有 32 条灯带的 GRB 值 */
        uint32_t grb[32];
        for (uint8_t s = 0; s < STRIP_COUNT; s++)
        {
            LED_Color_t *c = &g_ledBuffer[s][led];
            grb[s] = ((uint32_t)c->g << 16) | ((uint32_t)c->r << 8) | c->b;
        }

        /* 24 位展开 (GRB, MSB first) */
        for (uint8_t bit = 0; bit < 24; bit++)
        {
            uint32_t mask = 1UL << (23 - bit);
            uint32_t state = 0;

            /* 32 路 → 一个 32-bit 位片 */
            for (uint8_t s = 0; s < STRIP_COUNT; s++)
                if (grb[s] & mask)
                    state |= (1UL << s);

            uint16_t db = (uint16_t)state;
            uint16_t dd = (uint16_t)(state >> 16);

            *pb++ = ALLHI;  *pd++ = ALLHI;
            *pb++ = db | (uint32_t)((~db & 0xFFFFU) << 16);
            *pd++ = dd | (uint32_t)((~dd & 0xFFFFU) << 16);
            *pb++ = ALLLO;  *pd++ = ALLLO;
        }
    }

    /* Reset 时间片 */
    for (uint16_t i = 0; i < WS2812_RESET_SLOTS; i++) {
        *pb++ = ALLLO; *pd++ = ALLLO;
    }
}

//总的刷新函数，调用其他函数来发送数据
void WS2812_Show(void){
    if(WS2812_IsBusy()) { ws2812_skip_count++; return; }
    WS2812_FillBits();
    WS2812_Wave_Enable();
    ws2812_port_encode();
    WS2812_CleanDCache();
    WS2812_Send();
    ws2812_refresh_count++;
}

void WS2812_ShowFast(void){
    uint32_t t0, t1;
    if(WS2812_IsBusy()) { ws2812_skip_count++; return; }
    ws2812_busy_start = DWT->CYCCNT;
    WS2812_FillWave();
    WS2812_CleanDCache();
    t0 = DWT->CYCCNT;
    WS2812_Send();
    ws2812_refresh_count++;

    t1 = t0 - ws2812_busy_start;
    if (t1 > ws2812_fillwave_max) ws2812_fillwave_max = t1;
}

/*
 * 预计算 state → BSRR 快速展开 + Send
 * states: 576 个 uint32_t LE 值，每 bit 为 1 表示该 strip 需高电平
 * 总大小 576 × 4 = 2304 字节
 */
static void fill_wave_from_state(const uint8_t *states)
{
    uint32_t *pb = g_ws2812PortBBuffer;
    uint32_t *pd = g_ws2812PortDBuffer;

    for (uint16_t i = 0; i < 576; i++)
    {
        uint32_t s = (uint32_t)states[0]
                   | ((uint32_t)states[1] << 8)
                   | ((uint32_t)states[2] << 16)
                   | ((uint32_t)states[3] << 24);
        states += 4;

        uint16_t db = (uint16_t)s;
        uint16_t dd = (uint16_t)(s >> 16);

        *pb++ = 0x0000FFFFU;  *pd++ = 0x0000FFFFU;
        *pb++ = db | (uint32_t)((~db & 0xFFFFU) << 16);
        *pd++ = dd | (uint32_t)((~dd & 0xFFFFU) << 16);
        *pb++ = 0xFFFF0000U;  *pd++ = 0xFFFF0000U;
    }

    for (uint16_t i = 0; i < WS2812_RESET_SLOTS; i++) {
        *pb++ = 0xFFFF0000U; *pd++ = 0xFFFF0000U;
    }
}

void WS2812_ShowFromSlice(const uint8_t *states)
{
    uint32_t t0;
    if (WS2812_IsBusy()) { ws2812_skip_count++; return; }

    ws2812_busy_start = DWT->CYCCNT;
    fill_wave_from_state(states);
    WS2812_CleanDCache();
    t0 = DWT->CYCCNT;
    WS2812_Send();
    ws2812_refresh_count++;

    t0 -= ws2812_busy_start;
    if (t0 > ws2812_fillwave_max) ws2812_fillwave_max = t0;
}

uint32_t WS2812_GetBusyMaxUs(void)
{
    uint32_t val = ws2812_busy_max;
    ws2812_busy_max = 0;
    return val / 480U;
}

uint32_t WS2812_GetFillMaxUs(void)
{
    uint32_t val = ws2812_fillwave_max;
    ws2812_fillwave_max = 0;
    return val / 480U;
}

uint32_t WS2812_GetRefreshCount(void)
{
    return ws2812_refresh_count;
}

uint32_t WS2812_GetSkipCount(void)
{
    return ws2812_skip_count;
}




