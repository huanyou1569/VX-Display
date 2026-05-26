/**
 * @file ui_input.h
 * @brief 输入处理 - 摇杆ADC、方向检测、菜单导航、按键处理
 */
#ifndef __UI_INPUT_H__
#define __UI_INPUT_H__

#include "main.h"
#include <stdint.h>

uint8_t Input_ReadKey2(void);
void Input_ProcessDeadZone(void);
void Input_ProcessBrushDirection(void);
void Input_ProcessSnakeDirection(void);
void Input_ProcessNavigation(void);
void Input_ProcessJoyButton(void);
void Input_ProcessKey2(uint8_t key2_now);

#endif
