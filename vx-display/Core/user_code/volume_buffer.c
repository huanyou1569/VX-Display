#include "volume_buffer.h"
#include <string.h>

static uint8_t g_globalBrightness = 48;//全局亮度

//体素缓冲区，用于存放3d的体素文件
static VolumeColor_t g_volumeBuffer[VOLUME_DIAMETER][VOLUME_DIAMETER][VOLUME_HEIGHT];//创建一个3c的体素缓冲区

//设置全局亮度函数
void LED_SetGlobalBrightness(uint8_t brightness)
{
    g_globalBrightness = brightness;
}
//缩放亮度函数
static uint8_t scale8(uint8_t value, uint8_t brightness)
{
    return (uint16_t)value * brightness / 255;
}
//缓冲区清理函数
void VolumeBuffer_Clear(void)
{
    memset(g_volumeBuffer, 0, sizeof(g_volumeBuffer));
}
//设置体素颜色函数
void VolumeBuffer_SetVoxel(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b)
{
    if(x < 0 || x >= VOLUME_DIAMETER) return;//越界时直接返回
    if(y < 0 || y >= VOLUME_DIAMETER) return;
    if(z < 0 || z >= VOLUME_HEIGHT) return;

    g_volumeBuffer[x][y][z].r = scale8(r, g_globalBrightness);//将缩放后的亮度存入为体素颜色
    g_volumeBuffer[x][y][z].g = scale8(g, g_globalBrightness);
    g_volumeBuffer[x][y][z].b = scale8(b, g_globalBrightness);
}
// 快速列读取: 一次 memcpy 代替 24 次函数调用
void VolumeBuffer_ReadColumn(int x, int y, void *out)
{
    memcpy(out, &g_volumeBuffer[x][y][0], VOLUME_HEIGHT * 3);
}

//获取体素颜色函数
VolumeColor_t VolumeBuffer_GetVoxel(int x, int y, int z)
{
    static const VolumeColor_t black = {0, 0, 0};//定义一个黑色常量用于越界时返回

    if(x < 0 || x >= VOLUME_DIAMETER) return black;//越界时返回黑色
    if(y < 0 || y >= VOLUME_DIAMETER) return black;
    if(z < 0 || z >= VOLUME_HEIGHT) return black;

    return g_volumeBuffer[x][y][z];//返回体素颜色
}
