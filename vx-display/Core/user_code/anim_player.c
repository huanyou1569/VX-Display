/**
 * anim_player.c  —— 切片动画播放器实现 (双缓冲, 指针互换)
 *
 * 缓冲布局:
 *   前端帧: g_display_buf[AXI SRAM]  或 g_ramd2_buf[RAM_D2]  (由 anim_active 指向)
 *   后台帧: 另一个 buf                                              (由 anim_back  指向)
 *
 * 换帧时 f_read 下一帧到 anim_back, 然后交换 anim_active / anim_back 指针,
 * 无需 memcpy。WS2812 DMA 可从 AXI SRAM 和 RAM_D2 读取。
 */

#include "anim_player.h"
#include "ws2812_driver.h"
#include "sd_animation.h"    /* VXAN_Header_t */
#include "fatfs.h"
#include <string.h>

/* SDMMC 中断优先级可能被 TIM DMA 中断抢占, f_read 前确保 WS2812 空闲 */
static void anim_wait_ws2812_idle(void)
{
    uint32_t timeout = 1000000;  /* ~2ms @480MHz */
    while (WS2812_IsBusy() && --timeout) { __NOP(); }
}

/* ---- 共享缓冲 ---- */
extern uint8_t g_display_buf[];       /* AXI SRAM: 当前活跃帧 */
extern uint8_t g_ramd2_buf[];         /* 后台预加载帧 (RAM_D2) */

/* SDMMC IDMA → AXI SRAM 中转缓冲 (8KB) */
static uint8_t anim_xfer[8192]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

static uint8_t *anim_active;          /* 当前播放帧指针 */
static uint8_t *anim_back;            /* 后台帧指针 */

/* ---- 动画文件 ---- */
static FIL  anim_file __attribute__((section(".RAM_AXI"), used));
static FIL *anim_fp = &anim_file;

static uint16_t anim_total_frames;   /* 动画文件总帧数 */
static uint16_t anim_cur_frame;      /* 当前播放帧号 (0-based) */
static uint8_t  anim_loaded;         /* 动画已加载标志 */
static uint16_t anim_slice_count;    /* 每帧片数 */

/* ---- 相位管理 ---- */
static volatile uint8_t s_phase = 0;
static volatile uint8_t s_hall  = 0;
static volatile uint8_t s_swap_pending = 0;  /* 霍尔触发后置1, 主循环处理 */

/* ==========================================================================
 * 加载
 * ========================================================================== */

int AnimPlayer_Load(const char *path)
{
    VXAN_Header_t hdr;
    UINT    br;
    FRESULT fr;
    FIL    *fp = anim_fp;
    uint32_t frame_size;

    /* 1. 打开文件 (FS 已在启动时挂载, 无需重复 f_mount) */
    memset(fp, 0, sizeof(FIL));
    fr = f_open(fp, path, FA_READ);
    if (fr != FR_OK) return -(int)fr;

    /* 3. 读 16 字节头 */
    if (f_read(fp, &hdr, 16, &br) != FR_OK || br != 16) {
        f_close(fp);
        return -3;
    }

    /* 4. 验证 */
    if (memcmp(hdr.magic, "VXSL", 4) != 0) {
        f_close(fp);
        return -3;
    }
    if (hdr.width  != ANIM_STRIPS ||
        hdr.height != ANIM_LEDS) {
        f_close(fp);
        return -3;
    }
    if (hdr.depth == 0 || hdr.depth > ANIM_SLICE_COUNT) {
        f_close(fp);
        return -3;
    }
    if (hdr.total_frames < 2) {
        f_close(fp);
        return -3;   /* 单帧请用 SlicePlayer_Load */
    }

    anim_slice_count = hdr.depth;
    anim_total_frames = hdr.total_frames;
    frame_size = (uint32_t)anim_slice_count * ANIM_SLICE_SIZE;

    /* 5. 读第一帧 — 照抄 SlicePlayer_Load 的一次性 f_read */
    if (f_read(fp, g_display_buf, frame_size, &br) != FR_OK
        || br != frame_size) {
        f_close(fp);
        return -4;
    }

    /* 6. 帧0在 g_display_buf (AXI SRAM), 后台指向 g_ramd2_buf */
    anim_active    = g_display_buf;
    anim_back      = g_ramd2_buf;
    anim_cur_frame = 0;
    anim_loaded        = 1;
    s_phase            = 0;
    return 0;
}

/* ==========================================================================
 * 渲染
 * ========================================================================== */

void AnimPlayer_RenderPhase(uint8_t phase)
{
    if (!anim_loaded) return;
    if (phase >= anim_slice_count) phase = 0;

    WS2812_ShowFromSlice(&anim_active[(uint32_t)phase * ANIM_SLICE_SIZE]);
}

void AnimPlayer_OnHallEdge(void)
{
    s_hall = 1;
}

uint8_t AnimPlayer_RenderNext(void)
{
    if (s_hall) {
        s_hall  = 0;
        s_phase = 0;
        s_swap_pending = 1;  /* 新一圈开始, 触发帧切换 */
    }
    AnimPlayer_RenderPhase(s_phase);
    s_phase++;
    if (s_phase >= anim_slice_count) s_phase = 0;
    return s_phase;
}

/* ==========================================================================
 * 帧切换
 * ========================================================================== */

/*
 * 从动画文件读取下一帧到 anim_back, 然后交换指针。
 * 调用此函数会阻塞 ~180ms (f_read 225KB)。
 * 返回:  1=切换成功, 0=最后一帧(循环到头), -1=错误
 */
int AnimPlayer_PollFrameSwap(void)
{
    FRESULT fr;
    UINT    br;
    uint32_t frame_size;

    if (!anim_loaded) return -1;

    /* 计算下一帧号 (循环) */
    uint16_t next_frame = anim_cur_frame + 1;
    if (next_frame >= anim_total_frames)
        next_frame = 0;

    /*
     * 如果下一帧就是当前帧 (单帧动画 / 只有一帧剩余),
     * 不需要加载。
     */
    if (next_frame == anim_cur_frame)
        return 0;

    frame_size = (uint32_t)anim_slice_count * ANIM_SLICE_SIZE;

    /*
     * f_read 从当前文件指针位置读取。首帧后指针在帧1开头,
     * 若循环回帧0需 f_lseek。
     */
    if (next_frame == 0) {
        /* 回到文件头 + 16 字节 header */
        fr = f_lseek(anim_fp, 16);
        if (fr != FR_OK) return -1;
    }

    /*
     * 加载下一帧: f_read → anim_xfer (AXI SRAM, 8KB) → memcpy → anim_back。
     * 绕开 SDMMC IDMA 直接写 RAM_D2 的限制: IDMA 只写 AXI SRAM, CPU 负责搬运。
     */
    anim_wait_ws2812_idle();
    {
        uint32_t remaining = frame_size;
        uint32_t off = 0;
        while (remaining > 0) {
            UINT chunk = (remaining > 8192U) ? 8192U : (UINT)remaining;
            fr = f_read(anim_fp, anim_xfer, chunk, &br);
            if (fr != FR_OK || br != chunk) {
                anim_loaded = 0;
                f_close(anim_fp);
                return -1;
            }
            memcpy(anim_back + off, anim_xfer, chunk);
            off       += chunk;
            remaining -= chunk;
        }
    }

    /* 交换指针: 刚加载的后台帧变为活跃帧 */
    {
        uint8_t *tmp = anim_active;
        anim_active  = anim_back;
        anim_back    = tmp;
    }

    anim_cur_frame = next_frame;
    /* s_phase 由霍尔自动复位 */
    return 1;
}

/* ==========================================================================
 * 状态查询
 * ========================================================================== */

uint8_t  AnimPlayer_GetPhase(void)         { return s_phase; }
uint8_t  AnimPlayer_GetSliceCount(void)    { return (uint8_t)anim_slice_count; }
uint16_t AnimPlayer_GetTotalFrames(void)   { return anim_total_frames; }
uint16_t AnimPlayer_GetCurrentFrame(void)  { return anim_cur_frame; }
/** 主循环调用 — 如果有待处理的换帧请求则执行 */
void AnimPlayer_ProcessSwap(void)
{
    if (s_swap_pending) {
        s_swap_pending = 0;
        AnimPlayer_PollFrameSwap();
    }
}

void     AnimPlayer_Deactivate(void)       { anim_loaded = 0; }
uint8_t  AnimPlayer_IsActive(void)         { return anim_loaded; }
