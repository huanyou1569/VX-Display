#ifndef WS2812_PORT_H
#define WS2812_PORT_H

#include <stdint.h>
#include "main.h"
#include "ws2812_enable.h"
//这是一个ws2812的bsrr结构体，用于控制两组引脚的bsrr
extern uint32_t g_ws2812PortBBuffer[WS2812_WAVE_BUFFER_SIZE];//B组引脚ws2812波形数据缓冲bsrr数组
extern uint32_t g_ws2812PortDBuffer[WS2812_WAVE_BUFFER_SIZE];//D组引脚ws2812波形数据缓冲bsrr数组

void ws2812_port_encode(void);//将ws2812波形数据缓冲时间片数组中的每个时间片的引脚状态转换成bsrr格式并存储到PortBuffer中

#endif /* WS2812_PORT_H */
