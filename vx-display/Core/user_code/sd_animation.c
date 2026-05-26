#include "sd_animation.h"
#include "volume_buffer.h"
#include "fatfs.h"
#include "sdmmc.h"
#include <string.h>

/* ==========================================================================
 * 双缓冲: 全部放在 RAM_D2 (section .RAM_D1), SDMMC IDMA 可直接访问
 * 显示缓冲 A + 加载缓冲 B = 48KB
 * ========================================================================== */
static uint8_t anim_buf_a[VXAN_FRAME_SIZE] __attribute__((section(".RAM_AXI"), used));
static uint8_t anim_buf_b[VXAN_FRAME_SIZE] __attribute__((section(".RAM_AXI"), used));

static uint8_t *display_buf = anim_buf_a;
static uint8_t *load_buf    = anim_buf_b;

/* ==========================================================================
 * 异步加载状态机
 * ========================================================================== */
typedef enum {
    LOAD_IDLE,
    LOAD_READING,
    LOAD_DONE,
    LOAD_ERROR
} load_state_t;

static load_state_t g_load_state = LOAD_IDLE;
static uint8_t      g_load_batch;
static uint8_t     *g_load_ptr;

/* ==========================================================================
 * 调色板: 256 色 RGB (设计值 0-255, 实际输出由 PALETTE_MAX_BRIGHTNESS 限制)
 * 0=黑(灭), 1-255 渐变色 (绿→青→蓝→紫→红→橙→黄→白)
 * ========================================================================== */
static const uint8_t g_palette[256][3] = {
    [0]   = {  0,   0,   0},

    [1]   = {  0,   8,   0},   [2]   = {  0,  16,   0},
    [3]   = {  0,  24,   0},   [4]   = {  0,  32,   0},
    [5]   = {  0,  40,   0},   [6]   = {  0,  48,   0},
    [7]   = {  0,  56,   0},   [8]   = {  0,  64,   0},
    [9]   = {  0,  72,   0},   [10]  = {  0,  80,   0},
    [11]  = {  0,  88,   0},   [12]  = {  0,  96,   0},
    [13]  = {  0, 104,   0},   [14]  = {  0, 112,   0},
    [15]  = {  0, 120,   0},   [16]  = {  0, 128,   0},
    [17]  = {  0, 136,   0},   [18]  = {  0, 144,   0},
    [19]  = {  0, 152,   0},   [20]  = {  0, 160,   0},
    [21]  = {  0, 168,   0},   [22]  = {  0, 176,   0},
    [23]  = {  0, 184,   0},   [24]  = {  0, 192,   0},
    [25]  = {  0, 200,   0},   [26]  = {  0, 208,   0},
    [27]  = {  0, 216,   0},   [28]  = {  0, 224,   0},
    [29]  = {  0, 232,   0},   [30]  = {  0, 240,   0},
    [31]  = {  0, 255,   0},

    [32]  = {  0, 255,   8},   [40]  = {  0, 255,  72},
    [48]  = {  0, 255, 136},   [56]  = {  0, 255, 200},

    [64]  = {  0, 200, 255},   [72]  = {  0, 136, 255},
    [80]  = {  0,  72, 255},   [88]  = {  0,   8, 255},

    [96]  = { 32,   0, 255},   [104] = { 96,   0, 255},
    [112] = {160,   0, 255},   [120] = {224,   0, 255},

    [128] = {255,   0, 200},   [136] = {255,   0, 136},
    [144] = {255,   0,  72},   [152] = {255,   0,   8},

    [160] = {255,  32,   0},   [168] = {255,  96,   0},
    [176] = {255, 160,   0},   [184] = {255, 224,   0},

    [192] = {255, 255,   0},   [200] = {255, 255,  64},
    [208] = {255, 255, 128},   [216] = {255, 255, 192},

    [224] = {255, 255, 255},
};

/* ==========================================================================
 * 文件内调色板: 从文件头读取, 索引 1 对应此颜色 (索引 0 恒为黑)
 * 对于索引 >= 2 的多色模型, 回退到内置 g_palette
 * ========================================================================== */
static uint8_t g_file_palette_r;
static uint8_t g_file_palette_g;
static uint8_t g_file_palette_b;
static uint8_t g_file_palette_count;   /* 非零色数量 */

static SD_Animation_Info_t g_anim_info __attribute__((section(".RAM_AXI")));
static FIL g_static_file __attribute__((section(".RAM_AXI")));   /* 静态加载用 */

/* ==========================================================================
 * 亮度映射: 设计值(0-255) → 输出值(0-PALETTE_MAX_BRIGHTNESS)
 * ========================================================================== */
static inline uint8_t cap_brightness(uint8_t v)
{
    return ((uint16_t)v * PALETTE_MAX_BRIGHTNESS + 127) / 255;
}

/* ==========================================================================
 * 调色板索引 → RGB 解析 (索引 1 用文件调色板, 其余用内置 g_palette)
 * ========================================================================== */
static void resolve_palette_color(uint8_t idx, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (idx == 1 && g_file_palette_count >= 1) {
        *r = cap_brightness(g_file_palette_r);
        *g = cap_brightness(g_file_palette_g);
        *b = cap_brightness(g_file_palette_b);
    } else {
        *r = cap_brightness(g_palette[idx][0]);
        *g = cap_brightness(g_palette[idx][1]);
        *b = cap_brightness(g_palette[idx][2]);
    }
}

/* ==========================================================================
 * 接口一: 静态3D模型加载 (一次性)
 * ========================================================================== */

int SD_LoadStaticModel(const char *path)
{
    VXAN_Header_t hdr;
    UINT br;
    FRESULT fr;
    FIL *fp = &g_static_file;   /* 用 AXI SRAM 中的 FIL, 避免 DTCM */

    /* 1. 挂载SD */
    fr = f_mount(&SDFatFS, SDPath, 1);
    if (fr != FR_OK) return -1;

    /* 2. 打开文件 */
    memset(fp, 0, sizeof(FIL));  /* 清空 FIL, 排除残留值 */
    fr = f_open(fp, path, FA_READ);
    if (fr != FR_OK) return -(int)fr;

    /* 3. 读取并验证文件头 */
    if (f_read(fp, &hdr, VXAN_HEADER_SIZE, &br) != FR_OK
        || br != VXAN_HEADER_SIZE) {
        f_close(fp);
        return -3;
    }

    if (memcmp(hdr.magic, "VXAN", 4) != 0
        || hdr.width  != 32
        || hdr.height != 32
        || hdr.depth  != 24) {
        f_close(fp);
        return -3;
    }

    /* 提取文件调色板 */
    g_file_palette_count = hdr.palette_count;
    g_file_palette_r     = 30;
    g_file_palette_g     = 30;
    g_file_palette_b     = hdr.palette_b;

    /* 4. 读取帧0到加载缓冲 (AXI SRAM) */
    if (f_read(fp, load_buf, VXAN_FRAME_SIZE, &br) != FR_OK
        || br != VXAN_FRAME_SIZE) {
        f_close(fp);
        return -4;
    }

    f_close(fp);

    /* 5. 调色板索引 → RGB → 体素缓冲区 */
    {
        const uint8_t *buf = load_buf;
        uint8_t idx, r, g, b;
        int x, y, z;

        VolumeBuffer_Clear();
        for (z = 0; z < 24; z++) {
            for (y = 0; y < 32; y++) {
                for (x = 0; x < 32; x++) {
                    idx = *buf++;
                    if (idx == 0) continue;

                    resolve_palette_color(idx, &r, &g, &b);
                    VolumeBuffer_SetVoxel(x, y, z, r, g, b);
                }
            }
        }
    }

    return 0;
}

/* ==========================================================================
 * 接口二: 动画播放 (逐帧异步)
 * ========================================================================== */

static uint16_t g_anim_next_frame;

int SD_Animation_Begin(const char *path)
{
    FRESULT fr;
    int ret;

    /* 1. 挂载SD */
    fr = f_mount(&SDFatFS, SDPath, 1);
    if (fr != FR_OK) return -1;

    /* 2. 打开文件 */
    ret = SD_Animation_Open(path);
    if (ret < 0) return ret - 1;  /* -2→-3, -3→-4, 等等 */

    /* 3. 发起第一帧加载 */
    g_anim_next_frame = 0;
    ret = SD_Animation_RequestFrame(0);
    if (ret < 0) return -4;

    return 0;
}

int SD_Animation_Update(void)
{
    int ret;

    ret = SD_Animation_PollLoad();
    if (ret == 1) {
        /* 帧加载完成 */
        SD_Animation_SwapBuffers();
        SD_Animation_ApplyToVolume();

        /* 发起下一帧加载 */
        g_anim_next_frame++;
        if (g_anim_next_frame >= g_anim_info.header.total_frames) {
            g_anim_next_frame = 0;    /* 循环 */
        }
        SD_Animation_RequestFrame(g_anim_next_frame);

        return 1;   /* 新帧就绪 */
    }

    if (ret < 0) return -1;   /* 读取错误 */
    return 0;                  /* 加载中 */
}

/* ==========================================================================
 * 底层API
 * ========================================================================== */

FRESULT SD_Animation_Init(void)
{
    FRESULT res = f_mount(&SDFatFS, SDPath, 1);
    g_anim_info.mounted = (res == FR_OK) ? 1 : 0;
    return res;
}

int SD_Animation_Open(const char *path)
{
    VXAN_Header_t hdr;
    UINT br;

    if (!g_anim_info.mounted) return -1;

    memset(&g_anim_info.file, 0, sizeof(FIL));
    if (f_open(&g_anim_info.file, path, FA_READ) != FR_OK) return -2;

    if (f_read(&g_anim_info.file, &hdr, VXAN_HEADER_SIZE, &br) != FR_OK
        || br != VXAN_HEADER_SIZE) {
        f_close(&g_anim_info.file);
        return -3;
    }

    if (memcmp(hdr.magic, "VXAN", 4) != 0) {
        f_close(&g_anim_info.file);
        return -4;
    }

    if (hdr.width != 32 || hdr.height != 32 || hdr.depth != 24) {
        f_close(&g_anim_info.file);
        return -5;
    }

    memcpy(&g_anim_info.header, &hdr, sizeof(hdr));
    g_anim_info.current_frame = 0;
    g_anim_info.file_open = 1;

    /* 提取文件调色板 */
    g_file_palette_count = hdr.palette_count;
    g_file_palette_r     = hdr.palette_r;
    g_file_palette_g     = hdr.palette_g;
    g_file_palette_b     = hdr.palette_b;

    return 0;
}

int SD_Animation_LoadFrame(uint16_t frame_index)
{
    UINT br;
    FSIZE_t offset;

    if (!g_anim_info.file_open)  return -1;
    if (frame_index >= g_anim_info.header.total_frames) return -2;

    offset = (FSIZE_t)VXAN_HEADER_SIZE
           + (FSIZE_t)frame_index * VXAN_FRAME_SIZE;

    if (f_lseek(&g_anim_info.file, offset) != FR_OK) return -3;

    if (f_read(&g_anim_info.file, load_buf, VXAN_FRAME_SIZE, &br) != FR_OK
        || br != VXAN_FRAME_SIZE) {
        return -4;
    }

    g_anim_info.current_frame = frame_index;
    return 0;
}

int SD_Animation_RequestFrame(uint16_t frame_index)
{
    FSIZE_t offset;

    if (!g_anim_info.file_open)  return -1;
    if (frame_index >= g_anim_info.header.total_frames) return -2;

    offset = (FSIZE_t)VXAN_HEADER_SIZE
           + (FSIZE_t)frame_index * VXAN_FRAME_SIZE;

    if (f_lseek(&g_anim_info.file, offset) != FR_OK) {
        g_load_state = LOAD_ERROR;
        return -3;
    }

    g_load_batch  = 0;
    g_load_ptr    = load_buf;
    g_load_state  = LOAD_READING;
    g_anim_info.current_frame = frame_index;
    return 0;
}

int SD_Animation_PollLoad(void)
{
    UINT br;

    if (g_load_state != LOAD_READING) {
        return (g_load_state == LOAD_DONE) ? 1 : -1;
    }

    if (f_read(&g_anim_info.file, g_load_ptr, VXAN_BATCH_SIZE, &br) != FR_OK
        || br != VXAN_BATCH_SIZE) {
        g_load_state = LOAD_ERROR;
        return -2;
    }

    g_load_batch++;
    g_load_ptr += VXAN_BATCH_SIZE;

    if (g_load_batch >= VXAN_BATCHES_PER_FRAME) {
        g_load_state = LOAD_DONE;
        return 1;
    }

    return 0;
}

void SD_Animation_SwapBuffers(void)
{
    uint8_t *tmp;

    __disable_irq();
    tmp         = display_buf;
    display_buf = load_buf;
    load_buf    = tmp;
    __enable_irq();
}

void SD_Animation_ApplyToVolume(void)
{
    const uint8_t *buf = display_buf;
    uint8_t idx;
    int x, y, z;

    VolumeBuffer_Clear();
    for (z = 0; z < 24; z++) {
        for (y = 0; y < 32; y++) {
            for (x = 0; x < 32; x++) {
                idx = *buf++;
                if (idx == 0) continue;

                {
                    uint8_t r, g, b;
                    resolve_palette_color(idx, &r, &g, &b);
                    VolumeBuffer_SetVoxel(x, y, z, r, g, b);
                }
            }
        }
    }
}

const SD_Animation_Info_t *SD_Animation_GetInfo(void)
{
    return &g_anim_info;
}

const uint8_t *SD_Animation_GetDisplayBuffer(void)
{
    return display_buf;
}

/* ====================================================================
 * 接口三: 内存中的体素数据写入
 * ==================================================================== */

int SD_ApplyVoxelData(const uint8_t *data, uint32_t size)
{
    const VXAN_Header_t *hdr;

    if (size < VXAN_HEADER_SIZE + VXAN_FRAME_SIZE)
        return -2;

    hdr = (const VXAN_Header_t *)data;

    if (memcmp(hdr->magic, "VXAN", 4) != 0)
        return -1;
    if (hdr->width != 32 || hdr->height != 32 || hdr->depth != 24)
        return -1;

    /* 写入文件调色板，后续 resolve_palette_color 自动使用 */
    g_file_palette_count = hdr->palette_count;
    g_file_palette_r     = hdr->palette_r;
    g_file_palette_g     = hdr->palette_g;
    g_file_palette_b     = hdr->palette_b;

    /* 逐体素写入 */
    {
        const uint8_t *buf = data + VXAN_HEADER_SIZE;
        uint8_t idx, r, g, b;
        int x, y, z;

        VolumeBuffer_Clear();
        for (z = 0; z < 24; z++) {
            for (y = 0; y < 32; y++) {
                for (x = 0; x < 32; x++) {
                    idx = *buf++;
                    if (idx == 0) continue;
                    resolve_palette_color(idx, &r, &g, &b);
                    VolumeBuffer_SetVoxel(x, y, z, r, g, b);
                }
            }
        }
    }

    return 0;
}
