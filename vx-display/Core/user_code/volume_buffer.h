#ifndef VOLUME_BUFFER_H
#define VOLUME_BUFFER_H

#include <stdint.h>
#include "volume_config.h"
//体素rgb颜色结构体
typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} VolumeColor_t;

void VolumeBuffer_Clear(void);
void VolumeBuffer_SetVoxel(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b);
VolumeColor_t VolumeBuffer_GetVoxel(int x, int y, int z);
void LED_SetGlobalBrightness(uint8_t brightness);

/*
 * 快速列读取: 将 (x,y) 处全部 Z 方向 24 个体素一次性复制到 out。
 * 无边界检查 (调用方保证 x,y 已 clamp 到 0-31)。
 * out 需有 24 × 3 = 72 字节空间。
 */
void VolumeBuffer_ReadColumn(int x, int y, void *out);


#endif /* VOLUME_BUFFER_H */
