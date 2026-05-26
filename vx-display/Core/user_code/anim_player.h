/**
 * anim_player.h  —— 切片动画播放器 (双缓冲)
 *
 * 与 slice_player 互斥：动画模式下 slice_player 停用。
 * 使用两帧双缓冲实现零停顿帧切换：
 *   - 前端帧 (AXI SRAM)：当前播放帧
 *   - 后台帧 (RAM_D2)  ：异步预加载下一帧
 * 换帧时交换指针，无需 memcpy。
 */

#ifndef ANIM_PLAYER_H
#define ANIM_PLAYER_H

#include <stdint.h>

#define ANIM_SLICE_COUNT  100
#define ANIM_STRIPS       32
#define ANIM_LEDS         24
#define ANIM_SLICE_SIZE   (ANIM_STRIPS * ANIM_LEDS * 3)   /* 2304 */
#define ANIM_FRAME_SIZE   (ANIM_SLICE_COUNT * ANIM_SLICE_SIZE)  /* 225KB */

/* 加载多帧 .slices 文件 (total_frames > 1)，返回 0=成功, <0=错误 */
int AnimPlayer_Load(const char *path);

/* 渲染指定相位 */
void AnimPlayer_RenderPhase(uint8_t phase);

/* 霍尔同步: 复位相位 */
void AnimPlayer_OnHallEdge(void);

/* 获取下一个相位并渲染，返回当前相位号 */
uint8_t AnimPlayer_RenderNext(void);

/* 当前相位 */
uint8_t AnimPlayer_GetPhase(void);

/* 每帧片数 */
uint8_t AnimPlayer_GetSliceCount(void);

/* 动画总帧数 / 当前帧号 (0-based) */
uint16_t AnimPlayer_GetTotalFrames(void);
uint16_t AnimPlayer_GetCurrentFrame(void);

/*
 * 主循环轮询帧切换。
 * 切换到下一帧时从 TF 卡加载到后台缓冲，然后交换指针。
 * 调用方应在不需要实时渲染的时间窗口调用此函数。
 * 返回 1=发生了帧切换, 0=未切换, -1=动画播放完毕
 */
int AnimPlayer_PollFrameSwap(void);

/* 动画是否已加载 (由 main 判断用哪个渲染路径) */
void    AnimPlayer_Deactivate(void);
uint8_t AnimPlayer_IsActive(void);

/* 主循环调用 — 如果霍尔触发了新一圈, 执行帧切换 */
void AnimPlayer_ProcessSwap(void);

#endif
