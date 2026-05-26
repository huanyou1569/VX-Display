/**
 * @file ui_menu.h
 * @brief OLED菜单显示 - 所有界面的绘制函数和刷新逻辑
 */
#ifndef __UI_MENU_H__
#define __UI_MENU_H__

#include "main.h"
#include <stdint.h>

void DrawMainMenu(uint8_t selected);
void DrawSubMotor(void);
void DrawMotorSubMenu(uint8_t selected);
void DrawSubAudio(uint8_t selected, uint8_t top);
void DrawSubBluetooth(void);
void DrawInteractMenu(uint8_t selected);
void DrawDonutControl(void);
void DrawBrushMenu(uint8_t selected);
void DrawColorMenu(void);
void DrawDrawMenu(void);
void DrawSnakeGame(void);

// 根据menu_state定时刷新显示
void DisplayRefresh(void);

#endif
