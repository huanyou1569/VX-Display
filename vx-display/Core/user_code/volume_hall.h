#ifndef VOLUME_HALL_H
#define VOLUME_HALL_H

#include <stdint.h>

/*
 * volume_hall
 * 作用：利用 PC5 霍尔下降沿中断 + TIM5 自由运行计数器测量电机周期，
 *       判断电机是否进入稳速状态，并动态计算 POV 每片刷新间隔。
 *
 * 使用前提：
 *   - CubeMX 已配置 PC5 为 GPIO_EXTI5，下降沿触发，NVIC 开启 EXTI9_5_IRQn
 *   - CubeMX 已配置 TIM5（32 位定时器），PSC=0，ARR=0xFFFFFFFF，时钟 240MHz
 *   - 在 stm32h7xx_it.c 的 EXTI9_5_IRQHandler 中调用 VolumeHall_OnTrigger()
 */

/* 初始化 DWT 周期计数器，并校准 TIM5 */
void VolumeHall_Init(void);

/* 霍尔下降沿中断回调 —— 在 EXTI9_5_IRQHandler 中调用 */
void VolumeHall_OnTrigger(void);

/* 返回 1 表示电机已稳速，0 表示未稳速或刚失去同步 */
uint8_t VolumeHall_IsStable(void);

/* 获取当前推荐的切片间隔（微秒），未稳速时返回默认值 1000 */
uint32_t VolumeHall_GetSliceIntervalUs(void);

/* 获取上一圈完整周期（微秒） */
uint32_t VolumeHall_GetPeriodUs(void);

/* 获取当前转速（转/分），未稳速时返回 0 */
uint16_t VolumeHall_GetRPM(void);

/* DWT 微秒级精确忙等，最大约 8.9 秒（480MHz 下 32 位计数器溢出时间） */
void VolumeHall_DelayUs(uint32_t us);

#endif /* VOLUME_HALL_H */
