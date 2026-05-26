/**
 * brush_mode.h -- 3D画笔交互模式
 *
 * 画笔在体素空间中移动, 2×2×2 笔刷, 红色闪烁光标
 * 画布持久化: 72KB AXI SRAM 存储已画体素
 */

#ifndef BRUSH_MODE_H
#define BRUSH_MODE_H

#include <stdint.h>

void Brush_Init(void);
void Brush_Update(void);

/* TIM3 ISR 渲染 */
uint8_t Brush_RenderNext(void);
void Brush_OnHallEdge(void);

/* 状态 */
uint8_t Brush_IsActive(void);
void Brush_Activate(void);
void Brush_Deactivate(void);
void Brush_ClearCanvas(void);
void Brush_CycleColor(void);   /* JK: 切画笔颜色 */

/*
 * BLE 指令处理
 *   移动: JL / JR / JU / JD (左右/上下, 当前平面)
 *   按钮: JB0 (松开, Y轴) / JB1 (按下, Z轴)
 *   画笔: JP (切换画/移模式)
 *   清屏: :CLEAR
 *   激活: :BRUSH
 *   退出: JE
 * 返回 1=已处理, 0=未处理
 */
uint8_t Brush_HandleJoy(const uint8_t *data, uint16_t size);

/* BLE 诊断输出, 返回写入字节数 */
uint16_t Brush_GetDiag(char *buf, uint16_t max_len);

#endif
