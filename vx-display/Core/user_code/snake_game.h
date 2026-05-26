/**
 * snake_game.h -- 3D 贪吃蛇游戏
 *
 * 空间: 32x32x24 体素, 穿墙环绕, 撞自己死亡
 * 方向: 6向 + 查表转向 (JL/JR/JU/JD)
 * 食物: 初始10个, 保持≥5个, 至少3个在XY面
 */

#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H

#include <stdint.h>

void Snake_Init(void);
void Snake_Update(void);

/* TIM3 ISR */
uint8_t Snake_RenderNext(void);
void Snake_OnHallEdge(void);

/* 状态 */
uint8_t Snake_IsActive(void);
void Snake_Activate(void);
void Snake_Deactivate(void);

/* BLE 指令: JG=开局, JL/JR/JU/JD=转向, JE=退出 */
uint8_t Snake_HandleJoy(const uint8_t *data, uint16_t size);

/* 诊断 + 分数回传, 返回写入字节数 */
uint16_t Snake_GetDiag(char *buf, uint16_t max_len);

#endif
