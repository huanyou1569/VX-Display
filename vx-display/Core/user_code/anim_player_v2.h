/**
 * anim_player_v2.h  —— 1-bit体素 + 50片180°对称 + RAM全帧预加载
 *
 * 零停顿动画播放器:
 *   - 仅存储50片 (phase 0-49), phase 50-99 通过__RBIT反转重建
 *   - 每体素1-bit (亮/灭), 颜色由共享调色板决定
 *   - 所有帧开机一次性从TF卡f_read到RAM_D2
 *   - 换帧 = CPU展开(1-bit → state) ≈ 300µs, 零SD卡访问
 *   - 播放时和旧版完全相同: WS2812_ShowFromSlice → 169µs
 */

#ifndef ANIM_PLAYER_V2_H
#define ANIM_PLAYER_V2_H

#include <stdint.h>

#define ANIM_V2_SLICE_COUNT  50
#define ANIM_V2_STRIPS       32
#define ANIM_V2_LEDS         24
#define ANIM_V2_BIT_SIZE     (ANIM_V2_STRIPS * ANIM_V2_LEDS / 8)           /* 96 */
#define ANIM_V2_FRAME_SIZE   (ANIM_V2_SLICE_COUNT * ANIM_V2_BIT_SIZE)      /* 4800 */
#define ANIM_V2_STATE_SIZE   (ANIM_V2_SLICE_COUNT * ANIM_V2_STRIPS * ANIM_V2_LEDS * 3)  /* 112.5KB */
#define ANIM_V2_STATE_FULL   (100 * ANIM_V2_STRIPS * ANIM_V2_LEDS * 3)     /* 225KB */

/* 加载多帧 .slices 文件 (format=0x03), 返回 0=成功, <0=错误 */
int  AnimV2_Load(const char *path);

/* 渲染指定相位 (0-99) */
void AnimV2_RenderPhase(uint8_t phase);

/* 霍尔同步: 复位相位 */
void AnimV2_OnHallEdge(void);

/* 获取下一个相位并渲染, 返回当前相位号 */
uint8_t AnimV2_RenderNext(void);

/* 换帧: CPU展开1-bit → state, 写入g_display_buf */
void AnimV2_AdvanceFrame(void);

/* 主循环调用 — 如果霍尔触发了新一圈, 执行帧切换 */
void AnimV2_ProcessSwap(void);

/* 状态查询 */
void     AnimV2_Deactivate(void);
uint8_t  AnimV2_IsActive(void);
uint16_t AnimV2_GetCurrentFrame(void);
uint16_t AnimV2_GetTotalFrames(void);
uint32_t AnimV2_GetExpandUs(void);

/* 亮度控制: 1-100%, 立即生效 */
void    AnimV2_SetBrightness(uint8_t pct);
uint8_t AnimV2_GetBrightness(void);

/* 颜色控制: 运行时换色, 立即生效 */
void AnimV2_SetColor(uint8_t r, uint8_t g, uint8_t b);
void AnimV2_GetColor(uint8_t *r, uint8_t *g, uint8_t *b);

#endif
