#ifndef WS2812_CODE_H
#define WS2812_CODE_H

#include <stdint.h>
#include"led_buffer.h"
#define WS2812_BITS_PER_LED      24//每个LED占用24位（8位红色，8位绿色，8位蓝色）
#define WS2812_TOTAL_BITS           (WS2812_BITS_PER_LED * LEDS_PER_STRIP) //总的位数
extern uint32_t g_ws2812_TOTAL_BITS[WS2812_TOTAL_BITS];
void WS2812_ClearBits(void);//用于清除WS2812灯带的总数据，将所有位设置为0
uint32_t WS2812_ColorToGRB(LED_Color_t color);//将RGB颜色转换为GRB格式，因为WS2812灯带使用GRB顺序,然后存入uint32_t类型的数组中
void WS2812_FillBits(void);//将LED缓冲区的数据转换为WS2812灯带所需的位数据，并存储在g_ws2812_TOTAL_BITS数组中


#endif /* WS2812_CODE_H */
