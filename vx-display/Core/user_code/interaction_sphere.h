/**
 * interaction_sphere.h -- 粒子球交互系统 v5 (体素层渲染)
 *
 * 粒子→体素→50面采样→state (后50面__RBIT), 200粒子全覆盖
 */

#ifndef INTERACTION_SPHERE_H
#define INTERACTION_SPHERE_H

#include <stdint.h>

void IS_Init(void);

/* ---- 蓝牙输入 ---- */
void IS_SetRotation(int8_t dir);    /* 1=CW/放大, -1=CCW/缩小 */
void IS_SetColorTemp(int8_t val);   /* -128(冷) ~ +127(暖) */
void IS_SetBrightness(uint8_t pct); /* 1-100 */
uint8_t IS_GetBrightness(void);

/* ---- 主循环 ---- */
void IS_Update(void);

/* ---- 相位渲染 (TIM3 ISR 调用) ---- */
uint8_t IS_RenderNext(void);

/* ---- 霍尔同步 ---- */
void IS_OnHallEdge(void);

/* ---- 状态查询 ---- */
uint8_t  IS_IsActive(void);
void     IS_Activate(void);
void     IS_Deactivate(void);
void     IS_CycleColor(void);   /* JK: 切颜色 */
uint16_t IS_GetRadius(void);
int8_t   IS_GetColorTemp(void);
uint8_t  IS_GetFlags(void);
uint32_t IS_GetExpandUs(void);

/* ---- 诊断 ---- */
uint32_t IS_GetDiagRenderCalls(void);
uint32_t IS_GetDiagHallEdges(void);
uint32_t IS_GetDiagSwaps(void);
uint32_t IS_GetDiagGenerations(void);
uint32_t IS_GetDiagParticlesSet(void);
uint16_t IS_GetParticleCount(void);

#endif
