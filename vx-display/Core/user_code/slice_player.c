/**
 * slice_player.c  —— 预计算切片播放器实现
 *
 * 文件读写流程完全照抄 SD_LoadStaticModel (已验证稳定)。
 * 缓冲 + FIL 均在 AXI SRAM，与 sd_animation 保持一致。
 */

#include "slice_player.h"
#include "led_buffer.h"
#include "ws2812_driver.h"
#include "sd_animation.h"    /* VXAN_Header_t 复用其结构 */
#include "fatfs.h"
#include <string.h>

/* ==========================================================================
 * 前端缓冲 —— 由 main.c 统一分配 (AXI SRAM)
 * 与 anim_player 共享同一块内存 (互斥使用)。
 * ========================================================================== */

extern uint8_t g_display_buf[];
#define slice_buf g_display_buf

static FIL  slice_file __attribute__((section(".RAM_AXI"), used));
static FIL *slice_fp = &slice_file;

static uint8_t slice_loaded = 0;
static uint8_t slice_active = 0;  /* 由 SlicePlayer_SetActive 控制 */

/* ==========================================================================
 * 相位管理
 * ========================================================================== */

static volatile uint8_t s_phase = 0;
static volatile uint8_t s_hall  = 0;

/* ==========================================================================
 * 加载 —— 照抄 SD_LoadStaticModel 文件读写流程
 * ========================================================================== */

int SlicePlayer_Load(const char *path)
{
    VXAN_Header_t hdr;   /* 切片头复用 VXAN 结构 (magic + width/height/depth/frames) */
    UINT    br;
    FRESULT fr;
    FIL    *fp = slice_fp;
    uint32_t total;

    /* 1. 挂载 SD */
    fr = f_mount(&SDFatFS, SDPath, 1);
    if (fr != FR_OK) return -1;

    /* 2. 打开文件 */
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
    if (hdr.width  != SLICE_STRIPS ||
        hdr.height != SLICE_LEDS   ||
        hdr.depth  != 0) {
        f_close(fp);
        return -3;
    }
    if (hdr.total_frames > SLICE_COUNT) {
        f_close(fp);
        return -3;
    }

    /* 5. 读所有切片数据 (一次 f_read，与 SD_LoadStaticModel 一致) */
    total = (uint32_t)hdr.total_frames * SLICE_SIZE;
    if (f_read(fp, slice_buf, total, &br) != FR_OK || br != total) {
        f_close(fp);
        return -4;
    }

    f_close(fp);
    slice_loaded = (uint8_t)hdr.total_frames;
    slice_active = 1;
    s_phase = 0;
    return 0;
}

/* ==========================================================================
 * 渲染
 * ========================================================================== */

void SlicePlayer_RenderPhase(uint8_t phase)
{
    if (!slice_loaded) return;
    if (phase >= slice_loaded) phase = 0;

    WS2812_ShowFromSlice(&slice_buf[(uint32_t)phase * SLICE_SIZE]);
}

void SlicePlayer_OnHallEdge(void)
{
    s_hall = 1;
}

uint8_t SlicePlayer_RenderNext(void)
{
    if (s_hall) {
        s_hall  = 0;
        s_phase = 0;
    }
    SlicePlayer_RenderPhase(s_phase);
    s_phase++;
    if (s_phase >= slice_loaded) s_phase = 0;
    return s_phase;
}

uint8_t SlicePlayer_GetPhase(void)
{
    return s_phase;
}

uint8_t SlicePlayer_GetSliceCount(void)
{
    return slice_loaded;
}

void SlicePlayer_SetActive(uint8_t active)
{
    slice_active = active;
}

uint8_t SlicePlayer_IsActive(void)
{
    return slice_active;
}
