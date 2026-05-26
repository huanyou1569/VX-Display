#ifndef SD_ANIMATION_H
#define SD_ANIMATION_H

#include <stdint.h>
#include "ff.h"

/* ====================================================================
 * 常量
 * ==================================================================== */

#define VXAN_FRAME_SIZE         24576   /* 32*32*24 bytes, palette-index per voxel */
#define VXAN_HEADER_SIZE        16          /* "VXAN"(4)+dim(8)+palette(4) */
#define VXAN_BATCH_SECTORS      8
#define VXAN_BATCH_SIZE         (512 * VXAN_BATCH_SECTORS)
#define VXAN_BATCHES_PER_FRAME  (VXAN_FRAME_SIZE / VXAN_BATCH_SIZE)

/* 调色板最大亮度: 每通道 0-255 → 0-35, 限制白光功耗 */
#define PALETTE_MAX_BRIGHTNESS  35

/* ====================================================================
 * 文件格式
 * ==================================================================== */

typedef struct {
    uint8_t  magic[4];          /* "VXAN" */
    uint16_t width;             /* 32 */
    uint16_t height;            /* 32 */
    uint16_t depth;             /* 24 */
    uint16_t total_frames;      /* N */
    uint8_t  palette_count;     /* 非零调色板数量 (索引0恒为黑) */
    uint8_t  palette_r;         /* 调色板[1] 的 RGB */
    uint8_t  palette_g;
    uint8_t  palette_b;
} __attribute__((packed)) VXAN_Header_t;

typedef struct {
    VXAN_Header_t header;
    uint16_t      current_frame;
    FIL           file;
    uint8_t       mounted;
    uint8_t       file_open;
} SD_Animation_Info_t;

/* ====================================================================
 * 接口一: 静态3D模型 — 一次性加载
 * ==================================================================== */

/**
 * 从SD卡加载静态3D模型, 直接写入体素缓冲区
 * 内部完成: 挂载SD → 打开文件 → 读取帧0 → 调色板→RGB → 写入g_volumeBuffer
 * @param path  文件路径, 如 "0:/model.vxan"
 * @return 0=成功, -1=SD挂载失败, -2=文件打开失败, -3=格式错误, -4=读取失败
 */
int SD_LoadStaticModel(const char *path);

/* ====================================================================
 * 接口二: 动画播放 — 逐帧异步加载
 * ==================================================================== */

/**
 * 开始播放动画: 挂载SD, 打开文件, 发起第一帧加载
 * @param path  文件路径, 如 "0:/anim.vxan"
 * @return 0=成功, 负值=错误 (同 SD_LoadStaticModel)
 */
int SD_Animation_Begin(const char *path);

/**
 * 每相位调用一次: 推进加载一个批次 (4KB, ~400us), 帧就绪时自动交换并写入体素缓冲区
 * @return 1=新帧已就绪并已写入g_volumeBuffer, 0=加载中, -1=错误/播放结束
 */
int SD_Animation_Update(void);

/**
 * 获取动画信息 (总帧数/当前帧等)
 */
const SD_Animation_Info_t *SD_Animation_GetInfo(void);

/**
 * 获取当前显示缓冲区 (调色板索引格式, 24576字节)
 */
const uint8_t *SD_Animation_GetDisplayBuffer(void);

/* ====================================================================
 * 底层API (如需手动控制)
 * ==================================================================== */

FRESULT SD_Animation_Init(void);
int     SD_Animation_Open(const char *path);
int     SD_Animation_LoadFrame(uint16_t frame_index);
int     SD_Animation_RequestFrame(uint16_t frame_index);
int     SD_Animation_PollLoad(void);
void    SD_Animation_ApplyToVolume(void);
void    SD_Animation_SwapBuffers(void);

/* ====================================================================
 * 接口三: 内存中的体素数据写入 (蓝牙传输等场景)
 * ==================================================================== */

/**
 * 将原始VXAN数据(文件头+帧数据)写入体素缓冲区。
 * 内部校验文件头, 解析调色板, 写入第一帧到g_volumeBuffer。
 *
 * @param data  完整的VXAN文件内容 (16字节头 + N*24576字节帧数据)
 * @param size  数据总长度
 * @return 0=成功, -1=格式错, -2=尺寸不匹配
 */
int SD_ApplyVoxelData(const uint8_t *data, uint32_t size);

#endif /* SD_ANIMATION_H */
