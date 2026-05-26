/**
 * @file motor_control.h
 * @brief 电机控制 - PWM调速 + 霍尔测速
 */
#ifndef __MOTOR_CONTROL_H__
#define __MOTOR_CONTROL_H__

#include "main.h"
#include <stdint.h>

extern uint16_t duty_ramp_target;

void UpdatePWM(void);
void MotorControl_Init(void);
void MotorControl_ProcessRamp(void);

// 霍尔传感器EXTI回调（在HAL_GPIO_EXTI_Callback中调用）
void MotorControl_HallCallback(uint16_t GPIO_Pin);

#endif
