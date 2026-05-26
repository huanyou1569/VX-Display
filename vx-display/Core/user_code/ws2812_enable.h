#ifndef WS2812_ENABLE_H
#define WS2812_ENABLE_H

#include <stdint.h>
#include "ws2812_code.h"

#define WS2812_SLOTS_PER_BIT      3//每个bit展开为3个时间片
#define WS2812_WAVE_SLOTS         (WS2812_TOTAL_BITS * WS2812_SLOTS_PER_BIT)//ws2812总的时间片个数
#define WS2812_RESET_SLOTS        250//预留的全为0的时间片个数
#define WS2812_WAVE_BUFFER_SIZE   (WS2812_WAVE_SLOTS + WS2812_RESET_SLOTS)//总的时间片个数
extern uint32_t g_ws2812WaveBuffer[WS2812_WAVE_BUFFER_SIZE];//ws2812波形数据缓冲时间片数组
void WS2812_Wave_Enable(void);//设置ws2812的时间片
#endif /* WS2812_ENABLE_H */
