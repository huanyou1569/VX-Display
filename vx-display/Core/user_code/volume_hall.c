#include "volume_hall.h"
#include "volume_config.h"
#include "stm32h7xx_hal.h"

/* TIM5 句柄由 CubeMX 生成 */
extern TIM_HandleTypeDef htim5;

/* -------------------------------------------------------------------------- */
/* 可调参数                                                                  */
/* -------------------------------------------------------------------------- */
#define TIM5_TICKS_PER_US           240U    // TIM5 计数频率 240MHz = 240 ticks/us
#define STABLE_THRESHOLD_PERCENT    2U      // 相邻周期变化 < 2% 认为趋于稳定
#define STABLE_CONFIRM_COUNT        5U      // 连续满足 5 次才标记为稳速
#define UNSTABLE_THRESHOLD_PERCENT  10U     // 变化 > 10% 认为失速，清零稳定标志

/* -------------------------------------------------------------------------- */
/* 内部状态                                                                  */
/* -------------------------------------------------------------------------- */
static volatile uint32_t s_last_cnt = 0;
static volatile uint32_t s_prev_period_us = 0;
static volatile uint32_t s_period_us = 0;
static volatile uint32_t s_slice_us = 1000; // 默认 1ms
static volatile uint8_t  s_stable = 0;
static volatile uint8_t  s_stable_cnt = 0;
static volatile uint8_t  s_first_trigger = 1;

/* -------------------------------------------------------------------------- */
/* 内部辅助：计算两个无符号数的百分比差异 (以平均值为基准)                   */
/* -------------------------------------------------------------------------- */
static uint32_t percent_diff(uint32_t a, uint32_t b)
{
    uint32_t diff = (a > b) ? (a - b) : (b - a);
    uint32_t avg  = (a + b) >> 1;
    if (avg == 0)
    {
        return 0;
    }
    return (uint32_t)((((uint64_t)diff * 100ULL) + (avg >> 1)) / avg);
}

/* -------------------------------------------------------------------------- */
/* 初始化 DWT 周期计数器，并确保 TIM5 处于 32 位自由运行状态                 */
/* -------------------------------------------------------------------------- */
void VolumeHall_Init(void)
{
    // 使能 DWT 跟踪和 CYCCNT
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // 强制 TIM5 为 32 位自由运行模式（ARR=0xFFFFFFFF, PSC=0）
    // 若 CubeMX 已正确配置则无副作用；若未配，此处兜底
    __HAL_TIM_SET_AUTORELOAD(&htim5, 0xFFFFFFFFU);
    __HAL_TIM_SET_PRESCALER(&htim5, 0);
    __HAL_TIM_SET_COUNTER(&htim5, 0);

    HAL_TIM_Base_Start(&htim5);

    s_last_cnt = __HAL_TIM_GET_COUNTER(&htim5);
    s_prev_period_us = 0;
    s_period_us = 0;
    s_slice_us = 1000;
    s_stable = 0;
    s_stable_cnt = 0;
    s_first_trigger = 1;
}

/* -------------------------------------------------------------------------- */
/* 霍尔下降沿中断回调 —— 在 EXTI9_5_IRQHandler 中调用                        */
/* -------------------------------------------------------------------------- */
void VolumeHall_OnTrigger(void)
{
    uint32_t now = __HAL_TIM_GET_COUNTER(&htim5);

    // 第一次触发仅校准时间戳，不计算周期（避免包含电机启动前的无效时间）
    if (s_first_trigger)
    {
        s_last_cnt = now;
        s_first_trigger = 0;
        return;
    }

    // 计算周期（无符号减法自动处理 32 位溢出）
    uint32_t diff_ticks = now - s_last_cnt;
    s_last_cnt = now;

    // 转换为微秒
    s_period_us = diff_ticks / TIM5_TICKS_PER_US;

    // 稳速判断：与上一圈周期比较
    if (s_prev_period_us == 0)
    {
        s_prev_period_us = s_period_us;
    }

    uint32_t pct = percent_diff(s_period_us, s_prev_period_us);
    s_prev_period_us = s_period_us;

    if (pct <= STABLE_THRESHOLD_PERCENT)
    {
        if (s_stable_cnt < STABLE_CONFIRM_COUNT)
        {
            s_stable_cnt++;
        }
        if (s_stable_cnt >= STABLE_CONFIRM_COUNT)
        {
            s_stable = 1;
        }
    }
    else if (pct > UNSTABLE_THRESHOLD_PERCENT)
    {
        // 突变：电机被干扰或启停中，清零稳定标志
        s_stable = 0;
        s_stable_cnt = 0;
    }
    // 若变化在 2%~10% 之间：保持当前状态，不增不减

    // 稳速后才更新切片间隔
    if (s_stable && s_period_us > 0)
    {
        s_slice_us = s_period_us / VOLUME_ANGLE_SLICES;
        if (s_slice_us == 0)
        {
            s_slice_us = 1;
        }
    }
}

/* -------------------------------------------------------------------------- */
uint8_t VolumeHall_IsStable(void)
{
    return s_stable;
}

/* -------------------------------------------------------------------------- */
uint32_t VolumeHall_GetSliceIntervalUs(void)
{
    return s_slice_us;
}

/* -------------------------------------------------------------------------- */
uint32_t VolumeHall_GetPeriodUs(void)
{
    return s_period_us;
}

/* -------------------------------------------------------------------------- */
uint16_t VolumeHall_GetRPM(void)
{
    if (s_period_us == 0)
    {
        return 0;
    }
    // RPM = 60 * 1,000,000 / period_us
    return (uint16_t)((60ULL * 1000000ULL) / s_period_us);
}

/* -------------------------------------------------------------------------- */
/* DWT 微秒级忙等                                                            */
/* 注意：H7 @ 480MHz 时 32 位 CYCCNT 约 8.9 秒溢出，单次等待应远小于此值      */
/* -------------------------------------------------------------------------- */
void VolumeHall_DelayUs(uint32_t us)
{
    uint32_t ticks = us * 480U;  // 480MHz = 480 ticks/us
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < ticks)
    {
        // 空转等待，POV 场景下可接受
    }
}
