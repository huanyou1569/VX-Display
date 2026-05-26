/**
 * anim_player_v2.c  —— 1-bit体素 + 50片180°对称 + RAM全帧预加载
 *
 * 缓冲布局:
 *   g_display_buf (AXI SRAM): 展开后100片state (225KB)
 *   g_ramd2_buf   (RAM_D2):   全部1-bit帧数据 (N × 4800)
 *
 * 换帧 = CPU展开(1-bit → state) → 写入g_display_buf, 零SD卡访问
 */

#include "anim_player_v2.h"
#include "ws2812_driver.h"
#include "sd_animation.h"    /* VXAN_Header_t */
#include "fatfs.h"
#include <string.h>

/* ---- 共享缓冲 ---- */
extern uint8_t g_display_buf[];       /* AXI SRAM: 展开后state */
extern uint8_t g_ramd2_buf[];         /* RAM_D2:  原始1-bit帧数据 */

/* SDMMC IDMA → AXI SRAM 中转缓冲 (8KB), 同 anim_player.c */
static uint8_t anim_v2_xfer[8192]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

/* 动画文件 FIL — 必须在 AXI SRAM (非缓存) */
static FIL  anim_v2_file __attribute__((section(".RAM_AXI"), used));
static FIL *anim_v2_fp = &anim_v2_file;

/* ---- 动画状态 ---- */
static uint16_t anim_v2_total_frames;   /* 总帧数 */
static uint16_t anim_v2_cur_frame;      /* 当前帧号 (0-based) */
static uint8_t  anim_v2_loaded;         /* 已加载标志 */
static uint32_t anim_v2_last_expand_us; /* 上次展开耗时 (us) */

/* 调色板颜色 */
static uint8_t palette_r, palette_g, palette_b;
static uint8_t anim_v2_brightness = 2;   /* 亮度百分比 1-100 */

/* ---- 相位管理 ---- */
static volatile uint8_t s_phase = 0;
static volatile uint8_t s_hall  = 0;
static volatile uint8_t s_swap_pending = 0;

/* ==========================================================================
 * 1-bit → state 展开 (核心)
 *
 * bit_data: 4800 字节 (50 phases × 96 bytes)
 * state_buf: 225KB (100 phases × 2304 bytes)
 *
 * state 格式: 576 uint32_t mask words per phase
 *   word[led*24 + grb_bit] = 32-bit strip mask
 *   与 ExportSlices() 输出格式相同, 由 fill_wave_from_state 消费
 * ========================================================================== */

static void anim_v2_expand_frame(const uint8_t *bit_data, uint8_t *state_buf,
                                  uint8_t r, uint8_t g, uint8_t b)
{
    /* 叠加亮度缩放 (1-100%) */
    if (anim_v2_brightness != 100) {
        r = (uint8_t)((uint32_t)r * anim_v2_brightness / 100U);
        g = (uint8_t)((uint32_t)g * anim_v2_brightness / 100U);
        b = (uint8_t)((uint32_t)b * anim_v2_brightness / 100U);
    }
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    uint8_t active_bits[24];
    int num_bits = 0;
    int i, phase, led, b_idx;

    /* 收集 palette 中为1的GRB bit位置 */
    for (i = 0; i < 24; i++) {
        if (grb & (1UL << i))
            active_bits[num_bits++] = (uint8_t)i;
    }

    /* 全部100片清零 */
    memset(state_buf, 0, 100U * 2304U);

    for (phase = 0; phase < 50; phase++) {
        const uint8_t *src = bit_data + (uint32_t)phase * 96U;
        uint32_t *fwd = (uint32_t *)(state_buf + (uint32_t)phase * 2304U);
        uint32_t *rev = (uint32_t *)(state_buf + (uint32_t)(phase + 50U) * 2304U);

        for (led = 0; led < 24; led++) {
            uint32_t word = (uint32_t)src[led*4]
                          | ((uint32_t)src[led*4 + 1] << 8)
                          | ((uint32_t)src[led*4 + 2] << 16)
                          | ((uint32_t)src[led*4 + 3] << 24);

            if (word == 0) continue;

            uint32_t word_rev = __RBIT(word);

            for (b_idx = 0; b_idx < num_bits; b_idx++) {
                int k = active_bits[b_idx];
                fwd[led * 24 + k] = word;
                rev[led * 24 + k] = word_rev;
            }
        }
    }
}

/* ==========================================================================
 * AnimV2_Load — 开机一次性加载全部帧
 * ========================================================================== */

int AnimV2_Load(const char *path)
{
    VXAN_Header_t hdr;
    UINT    br;
    FRESULT fr;
    FIL    *fp = anim_v2_fp;
    uint32_t total_bit_size;
    uint32_t remaining, off;

    /* 1. 打开文件 */
    memset(fp, 0, sizeof(FIL));
    fr = f_open(fp, path, FA_READ);
    if (fr != FR_OK) return -(int)fr;

    /* 2. 读16字节头 */
    if (f_read(fp, &hdr, 16, &br) != FR_OK || br != 16) {
        f_close(fp);
        return -3;
    }

    /* 3. 校验 */
    if (memcmp(hdr.magic, "VXSL", 4) != 0) {
        f_close(fp);
        return -3;
    }
    if (hdr.width  != ANIM_V2_STRIPS ||
        hdr.height != ANIM_V2_LEDS) {
        f_close(fp);
        return -3;
    }
    if (hdr.palette_count != 0x03) {
        f_close(fp);
        return -3;   /* 非1-bit格式 */
    }
    if (hdr.depth == 0 || hdr.depth > ANIM_V2_SLICE_COUNT) {
        f_close(fp);
        return -3;
    }
    if (hdr.total_frames < 2) {
        f_close(fp);
        return -3;   /* 单帧请用SlicePlayer */
    }

    anim_v2_total_frames = hdr.total_frames;

    /* 4. 保存调色板 */
    palette_r = hdr.palette_r;
    palette_g = hdr.palette_g;
    palette_b = hdr.palette_b;

    /* 5. 计算总量并检查缓冲区 */
    total_bit_size = (uint32_t)anim_v2_total_frames * ANIM_V2_FRAME_SIZE;

    /*
     * g_ramd2_buf 大小: 230416 字节 (main.c)
     * 最大可存帧数: 230416 / 4800 = 48 帧
     */
    if (total_bit_size > 230416UL) {
        f_close(fp);
        return -5;   /* 帧数过多, RAM_D2 放不下 */
    }

    /*
     * 6. 一次性f_read全部帧到RAM_D2。
     * SDMMC IDMA → AXI SRAM中转 (anim_v2_xfer) → memcpy → g_ramd2_buf。
     * 开机时WS2812 DMA未启动, 无需等待空闲。
     */
    remaining = total_bit_size;
    off       = 0;
    while (remaining > 0) {
        UINT chunk = (remaining > 8192U) ? 8192U : (UINT)remaining;
        fr = f_read(fp, anim_v2_xfer, chunk, &br);
        if (fr != FR_OK || br != chunk) {
            f_close(fp);
            anim_v2_loaded = 0;
            return -4;
        }
        memcpy(&g_ramd2_buf[off], anim_v2_xfer, chunk);
        off       += chunk;
        remaining -= chunk;
    }

    f_close(fp);

    /* 7. 展开帧0到g_display_buf */
    anim_v2_expand_frame(g_ramd2_buf, g_display_buf,
                          palette_r, palette_g, palette_b);

    anim_v2_cur_frame = 0;
    anim_v2_loaded    = 1;
    s_phase           = 0;

    return 0;
}

/* ==========================================================================
 * 渲染
 * ========================================================================== */

void AnimV2_RenderPhase(uint8_t phase)
{
    if (!anim_v2_loaded) return;
    if (phase >= 100) phase = 0;

    WS2812_ShowFromSlice(&g_display_buf[(uint32_t)phase * 2304U]);
}

void AnimV2_OnHallEdge(void)
{
    s_hall = 1;
}

uint8_t AnimV2_RenderNext(void)
{
    if (s_hall) {
        s_hall  = 0;
        s_phase = 0;
        s_swap_pending = 1;
    }
    AnimV2_RenderPhase(s_phase);
    s_phase++;
    if (s_phase >= 100) s_phase = 0;
    return s_phase;
}

/* ==========================================================================
 * 换帧 — CPU展开, 零SD卡访问
 * ========================================================================== */

void AnimV2_AdvanceFrame(void)
{
    uint16_t next;

    if (!anim_v2_loaded) return;

    next = anim_v2_cur_frame + 1;
    if (next >= anim_v2_total_frames)
        next = 0;

    {
        uint32_t t0 = DWT->CYCCNT;
        anim_v2_expand_frame(&g_ramd2_buf[(uint32_t)next * ANIM_V2_FRAME_SIZE],
                              g_display_buf,
                              palette_r, palette_g, palette_b);
        uint32_t t1 = DWT->CYCCNT;
        anim_v2_last_expand_us = (t1 - t0) / 480U;
    }

    anim_v2_cur_frame = next;
    /* s_phase 由霍尔自动复位, 不在此处清零以避免与 ISR 竞争 */
}

/* ==========================================================================
 * 主循环换帧处理
 * ========================================================================== */

void AnimV2_ProcessSwap(void)
{
    if (s_swap_pending) {
        s_swap_pending = 0;
        AnimV2_AdvanceFrame();
    }
}

/* ==========================================================================
 * 状态查询
 * ========================================================================== */

void     AnimV2_Deactivate(void)       { anim_v2_loaded = 0; }
uint8_t  AnimV2_IsActive(void)        { return anim_v2_loaded; }
uint16_t AnimV2_GetCurrentFrame(void)  { return anim_v2_cur_frame; }
uint16_t AnimV2_GetTotalFrames(void)   { return anim_v2_total_frames; }
uint32_t AnimV2_GetExpandUs(void)     { return anim_v2_last_expand_us; }

/* ==========================================================================
 * 亮度控制
 * ========================================================================== */

void AnimV2_SetBrightness(uint8_t pct)
{
    if (pct == 0) pct = 1;          /* 至少 1%, 避免全黑 */
    if (pct > 100) pct = 100;
    anim_v2_brightness = pct;

    /* 立即重新展开当前帧, 无需等下一圈 */
    if (anim_v2_loaded) {
        AnimV2_AdvanceFrame();
    }
}

uint8_t AnimV2_GetBrightness(void)
{
    return anim_v2_brightness;
}

/* ==========================================================================
 * 颜色控制 (运行时换色)
 * ========================================================================== */

void AnimV2_SetColor(uint8_t r, uint8_t g, uint8_t b)
{
    palette_r = r;
    palette_g = g;
    palette_b = b;

    /* 立即重新展开当前帧 */
    if (anim_v2_loaded) {
        AnimV2_AdvanceFrame();
    }
}

void AnimV2_GetColor(uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = palette_r;
    *g = palette_g;
    *b = palette_b;
}
