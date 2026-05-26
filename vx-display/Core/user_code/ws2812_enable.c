#include "ws2812_enable.h"

uint32_t g_ws2812WaveBuffer[WS2812_WAVE_BUFFER_SIZE];//ws2812波形数据缓冲时间片数组
/*
ws2812灯带接收的逻辑0和1的时间片表示如下：
逻辑 0：短高电平 + 长低电平
逻辑 1：长高电平 + 短低电平
这里使用三个时间片来表示一个bit，其中：
对于逻辑0：第一个时间片为高电平，后两个时间片为低电平
对于逻辑1：前两个时间片为高电平，第三个时间片为低电平
逻辑 0 = 100
逻辑 1 = 110
*/



//时间片清理函数
void WS2812_Wave_Clear(void)
{
    for(uint16_t i = 0; i < WS2812_WAVE_BUFFER_SIZE; i++)
    {
        g_ws2812WaveBuffer[i] = 0;
    }
}

void WS2812_Wave_Enable(void)//时间片输出函数
{
    WS2812_Wave_Clear();
    for(uint16_t bit_pos=0;bit_pos<WS2812_TOTAL_BITS;bit_pos++)//遍历每个bit
    {
        uint32_t bit_data=g_ws2812_TOTAL_BITS[bit_pos];//获取当前bit的数据
        uint16_t wave_index=bit_pos*WS2812_SLOTS_PER_BIT;//计算当前bit在波形缓冲区中的起始位置
        //对于第一个时间片，直接将所有的的32路gpio拉高
        g_ws2812WaveBuffer[wave_index]=0xFFFFFFFFUL;
        //对于第二个时间片，根据bit_data的值来设置高电平的持续时间
        g_ws2812WaveBuffer[wave_index+1]=bit_data ;
        //对于第三个时间片，所有的32路gpio都拉低
        g_ws2812WaveBuffer[wave_index+2]=0x00000000UL;
        // //如果bit_data为0，则第三个时间片也应该是全0，保持低电平
        // g_ws2812WaveBuffer[wave_index+3]=0x00000000UL;
    }
    
}

