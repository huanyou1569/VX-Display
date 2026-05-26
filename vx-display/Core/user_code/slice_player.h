/**
 * slice_player.h  —— 预计算切片播放器
 *
 * 每圈 100 片，每片 32 strip × 24 LED × 3 字节 RGB = 2304 字节。
 * 启动时从 TF 卡 .slices 文件全量加载到 RAM。
 * 渲染时直接 memcpy 到 LED 缓冲区 + WS2812_ShowFast，CPU ~10µs。
 */

#ifndef SLICE_PLAYER_H
#define SLICE_PLAYER_H

#include <stdint.h>

#define SLICE_COUNT      100
#define SLICE_STRIPS     32
#define SLICE_LEDS       24
#define SLICE_SIZE       (SLICE_STRIPS * SLICE_LEDS * 3)   /* 2304 */

/* 加载 .slices 文件，返回 0=成功, <0=错误 */
int SlicePlayer_Load(const char *path);

/* 渲染指定相位 (0-99) */
void SlicePlayer_RenderPhase(uint8_t phase);

/* 霍尔同步: 复位相位 */
void SlicePlayer_OnHallEdge(void);

/* 获取下一个相位并渲染，返回当前相位号 */
uint8_t SlicePlayer_RenderNext(void);

/* 当前相位 */
uint8_t SlicePlayer_GetPhase(void);

/* 已加载的切片数 */
uint8_t SlicePlayer_GetSliceCount(void);

/* 激活/查询切片模式 (由 bt_commands 或 main 调用) */
void    SlicePlayer_SetActive(uint8_t active);
uint8_t SlicePlayer_IsActive(void);

#endif
