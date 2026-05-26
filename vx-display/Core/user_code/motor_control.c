#include "motor_control.h"
#include "volume_hall.h"

/* -------------------------------------------------------------------------- */
/* 滑动窗口                                                                  */
/* -------------------------------------------------------------------------- */
static uint32_t s_periods[MOTOR_SAMPLE_COUNT];  // 最近 N 圈的周期 (us)
static volatile uint8_t  s_idx;                  // 当前写入位置
static volatile uint8_t  s_full;                 // 窗口是否已满
static volatile uint8_t  s_stable;               // 当前稳速状态

/* -------------------------------------------------------------------------- */
void MotorControl_Init(void)
{
    s_idx = 0;
    s_full = 0;
    s_stable = 0;
    for (uint8_t i = 0; i < MOTOR_SAMPLE_COUNT; i++)
        s_periods[i] = 0;
}

static uint32_t s_prev_period = 0;   // 上一个有效周期，用于异常值过滤

/* -------------------------------------------------------------------------- */
void MotorControl_ProcessHallTick(void)
{
    uint32_t period = VolumeHall_GetPeriodUs();
    if (period == 0)
        return;  // 第一圈还没完成

    // ---- 异常值过滤 ----
    // 周期 > 6s → RPM < 10 → 霍尔噪声，直接丢弃
    if (period > 6000000UL)
        return;

    // 与前一个有效周期偏差超过 100% → 霍尔噪声，丢弃
    if (s_prev_period > 0)
    {
        uint32_t diff = (period > s_prev_period) ? (period - s_prev_period) : (s_prev_period - period);
        if (diff > s_prev_period)
            return;  // 噪声，不加窗口
    }
    s_prev_period = period;

    // 写入滑动窗口
    s_periods[s_idx] = period;
    s_idx++;
    if (s_idx >= MOTOR_SAMPLE_COUNT)
    {
        s_idx = 0;
        s_full = 1;
    }

    // 窗口未满，不判断稳速
    if (!s_full)
        return;

    // ---- 多数投票制稳速判断 ----
    // 容忍个别毛刺：75% 以上的样本落在 avg 的 ±threshold% 内即稳速
    uint64_t sum = 0;
    for (uint8_t i = 0; i < MOTOR_SAMPLE_COUNT; i++)
        sum += s_periods[i];
    uint32_t avg = (uint32_t)(sum / MOTOR_SAMPLE_COUNT);
    if (avg == 0) { s_stable = 0; return; }

    uint8_t in_range = 0;
    uint32_t margin = (uint32_t)(((uint64_t)avg * MOTOR_STABLE_THRESHOLD_PCT) / 100U);
    for (uint8_t i = 0; i < MOTOR_SAMPLE_COUNT; i++)
    {
        uint32_t v = s_periods[i];
        uint32_t diff = (v > avg) ? (v - avg) : (avg - v);
        if (diff <= margin) in_range++;
    }

    // 8 个样本中至少 6 个在范围内 → 稳速
    s_stable = (in_range >= (MOTOR_SAMPLE_COUNT * 3U / 4U)) ? 1 : 0;
}

/* -------------------------------------------------------------------------- */
uint16_t MotorControl_GetRPM(void)
{
    return VolumeHall_GetRPM();
}

/* -------------------------------------------------------------------------- */
uint8_t MotorControl_IsWindowFull(void)
{
    return s_full;
}

/* -------------------------------------------------------------------------- */
uint8_t MotorControl_IsStable(void)
{
    return s_stable;
}

/* -------------------------------------------------------------------------- */
uint16_t MotorControl_GetStableRPM(void)
{
    if (!s_full || !s_stable)
        return 0;

    uint64_t sum = 0;
    for (uint8_t i = 0; i < MOTOR_SAMPLE_COUNT; i++)
        sum += s_periods[i];

    uint32_t avg_us = (uint32_t)(sum / MOTOR_SAMPLE_COUNT);
    if (avg_us == 0)
        return 0;

    // RPM = 60,000,000 / avg_us
    return (uint16_t)((60ULL * 1000000ULL) / avg_us);
}

/* -------------------------------------------------------------------------- */
uint32_t MotorControl_GetPhaseIntervalUs(void)
{
    if (!s_full || !s_stable)
        return 0;

    uint64_t sum = 0;
    for (uint8_t i = 0; i < MOTOR_SAMPLE_COUNT; i++)
        sum += s_periods[i];

    uint32_t avg_us = (uint32_t)(sum / MOTOR_SAMPLE_COUNT);

    // 每圈 50 片 → 片间隔 = 平均周期 / 50
    return avg_us / MOTOR_SLICES_PER_ROTATION;
}

/* -------------------------------------------------------------------------- */
uint32_t MotorControl_GetPhaseIntervalMs(void)
{
    uint32_t us = MotorControl_GetPhaseIntervalUs();
    if (us == 0)
        return 2;  // 默认 2ms

    // 向上取整到毫秒
    return (us + 999U) / 1000U;
}
