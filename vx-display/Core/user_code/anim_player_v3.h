/**
 * anim_player_v3.h -- 6-bit真彩 + 50片双缓冲 + 异步SD加载
 *
 * 双缓冲零停顿动画:
 *   - 6-bit 体素 (64色调色板), 每帧 28.1KB
 *   - 双缓冲: front/back 各 112.5KB (50片正向state)
 *   - 反向 50 片实时 __RBIT 计算
 *   - SD卡异步分块加载, CPU不阻塞
 *   - 换帧 = 指针交换 (0µs)
 */

#ifndef ANIM_PLAYER_V3_H
#define ANIM_PLAYER_V3_H

#include <stdint.h>

#define ANIM_V3_SLICE_COUNT    50
#define ANIM_V3_STRIPS         32
#define ANIM_V3_LEDS           24
#define ANIM_V3_VOXELS         (ANIM_V3_STRIPS * ANIM_V3_LEDS)          /* 768 */
#define ANIM_V3_SLICE_PACKED   ((ANIM_V3_VOXELS * 6 + 7) / 8)         /* 576 */
#define ANIM_V3_FRAME_SIZE     (ANIM_V3_SLICE_COUNT * ANIM_V3_SLICE_PACKED) /* 28800 */
#define ANIM_V3_PALETTE_COUNT  64
#define ANIM_V3_PALETTE_SIZE   (ANIM_V3_PALETTE_COUNT * 3)             /* 192 */
#define ANIM_V3_HALF_BUF       (ANIM_V3_SLICE_COUNT * ANIM_V3_STRIPS * ANIM_V3_LEDS * 3) /* 112.5KB */
#define ANIM_V3_HEADER_SIZE    16

/* 加载多帧 .slices 文件 (format=0x04), 返回 0=成功, <0=错误 */
int  AnimV3_Load(const char *path);

/* 渲染指定相位 (0-99), phase 50-99 由 __RBIT 实时计算 */
void AnimV3_RenderPhase(uint8_t phase);

/* 霍尔同步 */
void AnimV3_OnHallEdge(void);

/* 获取下一个相位并渲染, 返回当前相位号 */
uint8_t AnimV3_RenderNext(void);

/* 换帧: 指针交换 + 启动异步SD加载下一帧 */
void AnimV3_AdvanceFrame(void);

/* 主循环处理换帧请求 */
void AnimV3_ProcessSwap(void);

/* 主循环轮询异步SD加载进度 */
void AnimV3_PollAsyncLoad(void);

/* 状态查询 */
void     AnimV3_Deactivate(void);
uint8_t  AnimV3_IsActive(void);
uint16_t AnimV3_GetCurrentFrame(void);
uint16_t AnimV3_GetTotalFrames(void);
uint32_t AnimV3_GetExpandUs(void);

/* 亮度控制: 1-100%, 缩放调色板 */
void    AnimV3_SetBrightness(uint8_t pct);
uint8_t AnimV3_GetBrightness(void);

/* 诊断接口 */
uint8_t  AnimV3_GetDiagFlags(void);       /* bit0=back_rdy bit1=sd_pend bit2=swap_pend bit3=hall bit4=!loaded */
uint32_t AnimV3_GetDiagLseekFail(void);   /* f_lseek 失败计数 */
uint32_t AnimV3_GetDiagFreadFail(void);   /* f_read 失败计数 */
uint32_t AnimV3_GetDiagSwapMissed(void);  /* back_ready=0 时尝试换帧计数 */
uint32_t AnimV3_GetDiagSwapOk(void);      /* 成功换帧计数 */
uint32_t AnimV3_GetDiagLoadOk(void);      /* 完整帧加载计数 */
uint8_t  AnimV3_GetDiagFrErr(void);       /* 最后一次 f_read 的 FRESULT */
uint32_t AnimV3_GetDiagFrOff(void);       /* 失败时 v3_sd_off */

#endif
