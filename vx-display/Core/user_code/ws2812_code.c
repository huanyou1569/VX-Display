#include "ws2812_code.h"
uint32_t g_ws2812_TOTAL_BITS[WS2812_TOTAL_BITS];//用于存储WS2812灯带的总数据，每个LED占用24位（8位红色，8位绿色，8位蓝色）uint32_t 类型来存储32路的数据
void WS2812_ClearBits(){
    for (int i = 0; i < WS2812_TOTAL_BITS; i++) {
        g_ws2812_TOTAL_BITS[i] = 0; // 将所有位清零
    }
 }
 uint32_t WS2812_ColorToGRB(LED_Color_t color){//将RGB颜色转换为GRB格式，因为WS2812灯带使用GRB顺序,然后存入uint32_t类型的数组中
    return ((color.g << 16) | (color.r << 8) | color.b); // 将RGB颜色转换为GRB格式
 }
 //将LED缓冲区的数据转换为WS2812灯带所需的位数据，并存储在g_ws2812_TOTAL_BITS数组中
 void WS2812_FillBits(void)
{   //先将总位数清零
    WS2812_ClearBits();
     for(uint8_t led_index = 0; led_index < LEDS_PER_STRIP; led_index++){//对每一路上的同样序号位的灯珠历遍
        for(uint8_t strip = 0; strip < STRIP_COUNT; strip++){//遍历32路灯带
            uint32_t rgb_color=WS2812_ColorToGRB(g_ledBuffer[strip][led_index]);
            for(uint8_t bit = 0; bit < WS2812_BITS_PER_LED; bit++){//遍历一个灯的24位数据
                 uint16_t bit_pos = led_index * WS2812_BITS_PER_LED + bit;//计算当前位在总位数组中的位置
                  if(rgb_color & (1UL << (23 - bit))){//检查当前位是否为1，如果是，则在对应的总位数组中设置相应的位（<<）表示将1左移到对应的位置，|=表示将该位设置为1，用于设置对应路数的
                    g_ws2812_TOTAL_BITS[bit_pos] |= (1UL << strip);
                  }
                }
        }

     }
}


