/**
 * anim_player_v3.c -- 6-bit真彩 + 50片双缓冲 + f_read
 *
 * 开机: f_read 同步加载帧0+1
 * 播放: f_read 分块加载 (在 PollAsyncLoad, 相位之后调用)
 * 换帧: 指针交换 (双缓冲)
 * 反向: 实时 __RBIT (rev_slice 在 AXI SRAM)
 */

#include "anim_player_v3.h"
#include "ws2812_driver.h"
#include "fatfs.h"
#include "main.h"
#include "tim.h"
#include <string.h>

extern uint8_t g_display_buf[];

static uint8_t *front_buf, *back_buf;

static uint8_t rev_slice[2304]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

static uint8_t v3_sd_buf[29184]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

static FIL  v3_file __attribute__((section(".RAM_AXI"), used));
static FIL *v3_fp = &v3_file;

static uint16_t v3_total_frames, v3_cur_frame;
static uint8_t  v3_loaded, v3_brightness = 100;
static uint32_t v3_last_expand_us;
static uint8_t  palette[192], palette_work[192];

/* 分块 f_read */
#define SD_CHUNK  28800U  /* 整帧/块, 1次读完, 彻底消除多段f_read */
static uint32_t v3_sd_off, v3_sd_remaining;
static uint8_t  v3_sd_pending, v3_back_ready;

static volatile uint8_t s_phase, s_hall, s_swap_pending;

/* 诊断计数器 */
static uint32_t v3_diag_lseek_fail;   /* f_lseek 失败次数 */
static uint32_t v3_diag_fread_fail;   /* f_read 失败次数 */
static uint32_t v3_diag_swap_missed;  /* 换帧请求时 back_ready=0 次数 */
static uint32_t v3_diag_swap_ok;      /* 成功换帧次数 */
static uint32_t v3_diag_load_ok;      /* 完整加载帧次数 */
static uint8_t  v3_diag_fr_err;       /* 最后一次 f_read 的 FRESULT */
static uint32_t v3_diag_fr_off;       /* 最后一次 f_read 失败时的 v3_sd_off */

/* f_read 故障恢复 */
static char     v3_file_path[64];             /* 保存路径用于恢复时重新打开 */
static uint32_t v3_consec_fail;               /* 连续 f_read 失败计数 */
#define V3_MAX_CONSEC_FAIL  8                 /* 触发重开文件的连续失败阈值 (调低以更快恢复) */

#define V3_MAX_CHANNEL 25U
#define V3_DATA_START  208U

/* ========================================================================== */

static void palette_apply_brightness(void) {
    uint32_t i;
    for (i = 0; i < 192; i++) {
        uint32_t v = (uint32_t)palette[i] * v3_brightness / 100U;
        if (v > V3_MAX_CHANNEL) v = V3_MAX_CHANNEL;
        palette_work[i] = (uint8_t)v;
    }
}

/* ==========================================================================
 * 6-bit packed -> 50片正向 state 展开
 * ========================================================================== */

static void v3_expand_frame(const uint8_t *packed, const uint8_t *pal, uint8_t *state)
{
    int phase, voxel;
    memset(state, 0, (uint32_t)ANIM_V3_SLICE_COUNT * 2304U);
    for (phase = 0; phase < ANIM_V3_SLICE_COUNT; phase++) {
        const uint8_t *src = packed + (uint32_t)phase * ANIM_V3_SLICE_PACKED;
        uint32_t *dst = (uint32_t *)(state + (uint32_t)phase * 2304U);
        for (voxel = 0; voxel < ANIM_V3_VOXELS; voxel++) {
            int group = voxel >> 2, pos = voxel & 3;
            const uint8_t *b = src + (uint32_t)group * 3U;
            uint32_t val = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
            uint8_t ci = (uint8_t)((val >> (pos * 6U)) & 0x3FU);
            if (ci == 0) continue;
            int strip = voxel % ANIM_V3_STRIPS, led = voxel / ANIM_V3_STRIPS;
            const uint8_t *pc = pal + (uint32_t)ci * 3U;
            uint32_t grb = ((uint32_t)pc[1] << 16) | ((uint32_t)pc[0] << 8) | pc[2];
            int bit;
            for (bit = 0; bit < 24; bit++) {
                if (grb & (1UL << bit)) {
                    uint32_t *wp = dst + (uint32_t)led * 24U + (23U - (uint32_t)bit);
                    *wp |= (1UL << strip);
                }
            }
        }
    }
}

/* ==========================================================================
 * 整帧 f_read + 展开 (开机用)
 * ========================================================================== */

static int load_frame_sync(uint32_t frame_idx, uint8_t *dst_state)
{
    uint32_t foff = V3_DATA_START + frame_idx * (uint32_t)ANIM_V3_FRAME_SIZE;
    uint32_t rem = ANIM_V3_FRAME_SIZE, off = 0;
    FRESULT fr; UINT br;
    if (f_lseek(v3_fp, foff) != FR_OK) return -1;
    while (rem) {
        UINT c = (rem > 8192U) ? 8192U : (UINT)rem;
        fr = f_read(v3_fp, v3_sd_buf + off, c, &br);
        if (fr != FR_OK || br != c) return -1;
        off += c; rem -= c;
    }
    v3_expand_frame(v3_sd_buf, palette_work, dst_state);
    return 0;
}

/* ==========================================================================
 * AnimV3_Load
 * ========================================================================== */

int AnimV3_Load(const char *path)
{
    uint8_t hdr[16]; UINT br;
    memset(v3_fp, 0, sizeof(FIL));
    if (f_open(v3_fp, path, FA_READ) != FR_OK) return -1;
    if (f_read(v3_fp, hdr, 16, &br) != FR_OK || br != 16) { f_close(v3_fp); return -3; }
    if (memcmp(hdr, "VXSL", 4) || hdr[12] != 0x04)         { f_close(v3_fp); return -3; }
    v3_total_frames = (uint16_t)hdr[10] | ((uint16_t)hdr[11] << 8);
    if (v3_total_frames < 2) { f_close(v3_fp); return -3; }
    if (f_read(v3_fp, palette, 192, &br) != FR_OK || br != 192) { f_close(v3_fp); return -4; }
    palette_apply_brightness();

    front_buf = g_display_buf;
    back_buf  = g_display_buf + (uint32_t)ANIM_V3_HALF_BUF;

    if (load_frame_sync(0, front_buf)) { f_close(v3_fp); return -4; }
    if (load_frame_sync(1, back_buf))  { f_close(v3_fp); return -4; }

    v3_cur_frame  = 0;
    v3_loaded     = 1;
    s_phase       = 0;
    v3_sd_pending = 0;
    v3_back_ready = 1;
    v3_consec_fail = 0;
    strncpy(v3_file_path, path, sizeof(v3_file_path) - 1);
    v3_file_path[sizeof(v3_file_path) - 1] = '\0';
    return 0;
}

/* ==========================================================================
 * 渲染
 * ========================================================================== */

void AnimV3_RenderPhase(uint8_t phase)
{
    uint32_t i;
    if (!v3_loaded) return;
    if (phase < 50) {
        WS2812_ShowFromSlice(&front_buf[(uint32_t)phase * 2304U]);
    } else {
        uint8_t slot = phase - 50U;
        const uint32_t *fwd = (const uint32_t *)(front_buf + (uint32_t)slot * 2304U);
        uint32_t *rp = (uint32_t *)rev_slice;
        for (i = 0; i < 576U; i++) rp[i] = __RBIT(fwd[i]);
        WS2812_ShowFromSlice(rev_slice);
    }
}

void AnimV3_OnHallEdge(void) { s_hall = 1; }

uint8_t AnimV3_RenderNext(void)
{
    if (s_hall) { s_hall = 0; s_phase = 0; s_swap_pending = 1; }
    AnimV3_RenderPhase(s_phase);
    s_phase++; if (s_phase >= 100) s_phase = 0;
    return s_phase;
}

/* ==========================================================================
 * 换帧
 * ========================================================================== */

void AnimV3_AdvanceFrame(void)
{
    uint8_t *tmp; uint16_t next; uint32_t foff;
    if (!v3_loaded || !v3_back_ready) return;

    s_swap_pending = 0;
    __HAL_TIM_DISABLE_IT(&htim3, TIM_IT_UPDATE);
    tmp = front_buf; front_buf = back_buf; back_buf = tmp;
    v3_back_ready = 0;
    v3_cur_frame++; if (v3_cur_frame >= v3_total_frames) v3_cur_frame = 0;
    /* s_phase 由霍尔自动复位, 不在此处清零以避免与 ISR 竞争 */
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);

    v3_diag_swap_ok++;

    next = v3_cur_frame + 1;
    if (next >= v3_total_frames) next = 0;
    foff = V3_DATA_START + (uint32_t)next * ANIM_V3_FRAME_SIZE;

    if (f_lseek(v3_fp, foff) == FR_OK) {
        v3_sd_off       = 0;
        v3_sd_remaining = ANIM_V3_FRAME_SIZE;
        v3_sd_pending   = 1;
    } else {
        v3_diag_lseek_fail++;
    }
}

void AnimV3_ProcessSwap(void)
{
    if (s_swap_pending) {
        if (!v3_back_ready) {
            v3_diag_swap_missed++;
            s_swap_pending = 0;  /* 消耗请求，不等：等下一圈霍尔再试 */
            return;
        }
        AnimV3_AdvanceFrame();
    }
}

/* ==========================================================================
 * 主循环: 分块 f_read (相位之后, 不挡相位)
 * ========================================================================== */

void AnimV3_PollAsyncLoad(void)
{
    FRESULT fr; UINT br;
    if (!v3_sd_pending) return;

    {
        UINT c = (v3_sd_remaining > SD_CHUNK) ? SD_CHUNK : (UINT)v3_sd_remaining;
        fr = f_read(v3_fp, v3_sd_buf + v3_sd_off, c, &br);
        if (fr != FR_OK || br != c) {
            v3_diag_fread_fail++;
            v3_diag_fr_err = (uint8_t)fr;      /* 始终更新，便于追踪变化 */
            v3_diag_fr_off = v3_sd_off;
            v3_consec_fail++;

            /* 连续失败超阈值: 关闭重开文件恢复 FIL 损坏 */
            if (v3_consec_fail >= V3_MAX_CONSEC_FAIL) {
                v3_consec_fail = 0;
                if (v3_file_path[0]) {
                    f_close(v3_fp);
                    memset(v3_fp, 0, sizeof(FIL));
                    if (f_open(v3_fp, v3_file_path, FA_READ) == FR_OK) {
                        uint32_t foff = V3_DATA_START
                                      + ((v3_cur_frame + 1) % v3_total_frames)
                                      * (uint32_t)ANIM_V3_FRAME_SIZE;
                        if (f_lseek(v3_fp, foff) == FR_OK) {
                            v3_sd_off       = 0;
                            v3_sd_remaining = ANIM_V3_FRAME_SIZE;
                        }
                    }
                }
                return;
            }

            /* f_read 失败: 回退到帧开头重试 */
            {
                uint32_t foff = V3_DATA_START
                              + ((v3_cur_frame + 1) % v3_total_frames)
                              * (uint32_t)ANIM_V3_FRAME_SIZE;
                if (f_lseek(v3_fp, foff) == FR_OK) {
                    v3_sd_off       = 0;
                    v3_sd_remaining = ANIM_V3_FRAME_SIZE;
                }
            }
            return;
        }
        v3_consec_fail = 0;  /* 成功则清零连续失败计数 */
        v3_sd_off       += c;
        v3_sd_remaining -= c;
    }

    if (v3_sd_remaining == 0) {
        v3_sd_pending = 0;
        uint32_t t0 = DWT->CYCCNT;
        v3_expand_frame(v3_sd_buf, palette_work, back_buf);
        v3_last_expand_us = (DWT->CYCCNT - t0) / 480U;
        v3_back_ready = 1;
        v3_diag_load_ok++;
    }
}

/* ========================================================================== */

void     AnimV3_Deactivate(void)       { v3_loaded = 0; v3_sd_pending = 0; }
uint8_t  AnimV3_IsActive(void)         { return v3_loaded; }
uint16_t AnimV3_GetCurrentFrame(void)   { return v3_cur_frame; }
uint16_t AnimV3_GetTotalFrames(void)    { return v3_total_frames; }
uint32_t AnimV3_GetExpandUs(void)       { return v3_last_expand_us; }

void AnimV3_SetBrightness(uint8_t pct) {
    if (pct == 0) pct = 1; if (pct > 100) pct = 100;
    v3_brightness = pct; palette_apply_brightness();
}
uint8_t AnimV3_GetBrightness(void) { return v3_brightness; }

uint8_t  AnimV3_GetDiagFlags(void)       {
    uint8_t f = 0;
    if (v3_back_ready)  f |= 0x01;
    if (v3_sd_pending)  f |= 0x02;
    if (s_swap_pending) f |= 0x04;
    if (s_hall)         f |= 0x08;
    if (!v3_loaded)     f |= 0x10;
    return f;
}
uint32_t AnimV3_GetDiagLseekFail(void)    { return v3_diag_lseek_fail; }
uint32_t AnimV3_GetDiagFreadFail(void)    { return v3_diag_fread_fail; }
uint32_t AnimV3_GetDiagSwapMissed(void)   { return v3_diag_swap_missed; }
uint32_t AnimV3_GetDiagSwapOk(void)       { return v3_diag_swap_ok; }
uint32_t AnimV3_GetDiagLoadOk(void)       { return v3_diag_load_ok; }
uint8_t  AnimV3_GetDiagFrErr(void)         { return v3_diag_fr_err; }
uint32_t AnimV3_GetDiagFrOff(void)         { return v3_diag_fr_off; }
