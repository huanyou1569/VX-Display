#include "ws2812_port.h"
#include <stdint.h>

__attribute__((section(".RAM_D1")))
__attribute__((aligned(32)))
uint32_t g_ws2812PortBBuffer[WS2812_WAVE_BUFFER_SIZE];

__attribute__((section(".RAM_D1")))
__attribute__((aligned(32)))
uint32_t g_ws2812PortDBuffer[WS2812_WAVE_BUFFER_SIZE];//d组引脚的dma数据缓冲区
//把一个 16 位的 GPIO 输出状态 state，转换成可以直接写入 GPIOx->BSRR 的 32 位数据.state是一组引脚16个的输出电平状态
static uint32_t ws2812_port_convertto_bsrr(uint16_t state){
    uint32_t set_bits = state;//将一组16个引脚中为高电平的引脚放到低的16位
    uint32_t reset_bits = (((uint32_t)(~state)&0xFFFFUL)<<16);//将一组16个引脚中为低电平的引脚放到高的16位
    return set_bits | reset_bits;//将两部分合并成一个32位的值，低16位表示要设置为高电平的引脚，高16位表示要设置为低电平的引脚
}

void ws2812_port_encode(void){
    for(uint16_t i=0;i<WS2812_WAVE_BUFFER_SIZE;i++){
        uint32_t line_state = g_ws2812WaveBuffer[i];//获取当前时间片的引脚状态前16位位A组引脚后16位为B组引脚
        uint16_t state_b = line_state & 0xFFFF;//提取B组引脚的状态
        uint16_t state_d = (line_state >> 16) & 0xFFFF;//提取D组引脚的状态
        g_ws2812PortBBuffer[i] = ws2812_port_convertto_bsrr(state_b);//将B组引脚状态转换成BSRR格式并存储到PortBuffer中
        g_ws2812PortDBuffer[i] = ws2812_port_convertto_bsrr(state_d);//将D组引脚状态转换成BSRR格式并存储到PortBuffer中
    }
}
