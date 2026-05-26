/**
 * @file motor_control.c
 * @brief 电机控制 - PWM调速 + 霍尔测速
 * @note  TIM2 CH3(PA2) PWM输出，PD6 EXTI下降沿测速
 *        所有逻辑从原始main.c提取，保持不变
 */
#include "motor_control.h"
#include "app_state.h"
#include "ui_menu.h"
#include "tim.h"

// PWM更新（对应原 main.c 的 UpdatePWM）
void UpdatePWM(void) {
    uint32_t compare = (uint32_t)(2000UL * duty_permil / 1000);
    if (compare > 2000) compare = 2000;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, compare);
}

// 电机初始化：启动PWM和TIM7中断（对应原 main.c 行337-340）
void MotorControl_Init(void) {
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    duty_permil = 950;
    UpdatePWM();
    HAL_TIM_Base_Start_IT(&htim7);
}

// 占空比渐变处理（对应原 main.c 行392-402）
void MotorControl_ProcessRamp(void) {
    if (duty_ramp_active) {
        uint32_t elapsed = HAL_GetTick() - duty_ramp_start_tick;
        if (elapsed >= DUTY_RAMP_DURATION) {
            duty_permil = duty_ramp_target; duty_ramp_active = 0;
            UpdatePWM(); if (menu_state == 1 && motor_in_control) DrawSubMotor();
        } else {
            int32_t delta = (int32_t)duty_ramp_target - (int32_t)duty_ramp_start_value;
            duty_permil = (uint16_t)(duty_ramp_start_value + (delta * (int32_t)elapsed) / DUTY_RAMP_DURATION);
            UpdatePWM(); if (menu_state == 1 && motor_in_control) DrawSubMotor();
        }
    }
}

// 霍尔传感器中断回调（对应原 main.c 行652-673）
void MotorControl_HallCallback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_6) {
        static uint32_t last_valid_tick = 0;
        uint32_t now = HAL_GetTick();
        if (now - last_valid_tick < 2) return;
        last_valid_tick = now;
        if (last_pulse_tick != 0) {
            uint32_t diff = now - last_pulse_tick;
            if (diff >= 5 && diff <= 2000) {
                uint32_t rpm = (60000U / (uint32_t)PULSES_PER_REV) / diff;
                if (rpm <= 9999) {
                    rpm_buffer[rpm_buf_index % RPM_FILTER_WINDOW] = (uint16_t)rpm;
                    rpm_buf_index++;
                    uint32_t sum = 0;
                    for (uint8_t i = 0; i < RPM_FILTER_WINDOW; i++) sum += rpm_buffer[i];
                    current_rpm = (uint16_t)(sum / RPM_FILTER_WINDOW);
                }
            }
        }
        last_pulse_tick = now;
    }
}
