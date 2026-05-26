#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdint.h>

/*
 * motor_control — 电机稳速检测 + 相位间隔计算
 *
 * 依赖 volume_hall 提供原始霍尔周期数据。
 * 在 EXTI9_5_IRQHandler 中，先调 VolumeHall_OnTrigger()，再调
 * MotorControl_ProcessHallTick() 来更新采样窗。
 *
 * 使用方法：
 *   1. main() 中调用 MotorControl_Init()
 *   2. 主循环中调用 MotorControl_IsStable() 等待稳速
 *   3. 稳速后调用 MotorControl_GetPhaseIntervalUs() 获取每相位间隔
 *   4. 用该值替代原来固定 2ms 的延时
 */

#define MOTOR_SAMPLE_COUNT          8U    // 稳速判断窗口（圈数）
#define MOTOR_STABLE_THRESHOLD_PCT  15U   // 多数样本在 15% 以内即稳速
#define MOTOR_SLICES_PER_ROTATION   100U  // 每圈 100 个相位面

/* 初始化滑动窗口和内部状态 */
void MotorControl_Init(void);

/*
 * 霍尔下降沿处理 —— 在 EXTI9_5_IRQHandler 中、VolumeHall_OnTrigger() 之后调用。
 * 每次圈完成（霍尔触发）时更新采样窗口并重新评估稳速状态。
 */
void MotorControl_ProcessHallTick(void);

/* 返回当前瞬时转速（转/分），未触发时返回 0 */
uint16_t MotorControl_GetRPM(void);

/* 返回 1 表示采样窗口已满（已采集足够圈数） */
uint8_t MotorControl_IsWindowFull(void);

/* 返回 1 表示最近 20 圈周期抖动在阈值以内，电机已稳速 */
uint8_t MotorControl_IsStable(void);

/*
 * 返回稳速后的平均转速（转/分），不稳速返回 0。
 * 和 MotorControl_GetRPM 的区别：本函数返回 20 圈滑动平均值，更稳定。
 */
uint16_t MotorControl_GetStableRPM(void);

/*
 * 返回每相位切片间隔（微秒）。
 * 计算方式：20 圈平均周期 / 50 片。
 * 未稳速时返回 0。
 */
uint32_t MotorControl_GetPhaseIntervalUs(void);

/*
 * 与上面相同，但向上取整到毫秒，方便 HAL_GetTick 计时。
 * 未稳速时返回默认值 2ms。
 */
uint32_t MotorControl_GetPhaseIntervalMs(void);

#endif /* MOTOR_CONTROL_H */
