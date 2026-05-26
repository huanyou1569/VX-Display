#include "led_buffer.h"


LED_Color_t g_ledBuffer[STRIP_COUNT][LEDS_PER_STRIP];// 全局LED缓冲区，存储每条灯带的颜色数据


void LED_Buffer_Init(void) {
    // 初始化所有led
    //调用led清理颜色函数
    LED_Buffer_ClearAll();
}
// 清除所有LED颜色，将它们设置为0
void LED_Buffer_ClearAll(void)
{   
    //将所有led颜色设置为0
    for(uint8_t strip = 0; strip < STRIP_COUNT; strip++)
    {
        for(uint8_t i = 0; i < LEDS_PER_STRIP; i++)
        {
            g_ledBuffer[strip][i].r = 0;// 红色分量设置为0
            g_ledBuffer[strip][i].g = 0;// 绿色分量设置为0
            g_ledBuffer[strip][i].b = 0;// 蓝色分量设置为0
        }
    }
}

//具体灯珠颜色设置函数
void LED_Buffer_SetColor(uint8_t strip, uint8_t ledIndex, uint8_t r, uint8_t g, uint8_t b)
{
        //当函数列和行参数越界时return
    if(strip >= STRIP_COUNT || ledIndex >= LEDS_PER_STRIP)
    {
        return;
    }
    if(strip < STRIP_COUNT && ledIndex < LEDS_PER_STRIP)
    {
        g_ledBuffer[strip][ledIndex].r = r;// 设置红色分量，strip表示灯带索引，ledIndex表示灯珠索引
        g_ledBuffer[strip][ledIndex].g = g;// 设置绿色分量
        g_ledBuffer[strip][ledIndex].b = b;// 设置蓝色分量
    }

}
//单路灯珠测试函数
void LED_Buffer_Test(uint8_t strip,  uint8_t r, uint8_t g, uint8_t b)
{
    //测试函数，设置指定灯带的所有灯珠为相同颜色
    if(strip >= STRIP_COUNT) return;
    for(uint8_t i = 0; i < LEDS_PER_STRIP; i++)
    {
           g_ledBuffer[strip][i].r = r;// 设置红色分量
           g_ledBuffer[strip][i].g = g;// 设置绿色分量
           g_ledBuffer[strip][i].b = b;// 设置蓝色分量 
    }
}
//所有路灯珠测试
void LED_Buffer_TestAll(uint8_t r, uint8_t g, uint8_t b)
{
    //测试函数，设置所有灯带的所有灯珠为相同颜色
    for(uint8_t strip = 0; strip < STRIP_COUNT; strip++)
    {
        for(uint8_t i = 0; i < LEDS_PER_STRIP; i++)
        {
            g_ledBuffer[strip][i].r = r;// 设置红色分量
            g_ledBuffer[strip][i].g = g;// 设置绿色分量
            g_ledBuffer[strip][i].b = b;// 设置蓝色分量 
        }
    }
}
//清空某一路的led
void LED_Buffer_ClearStrip(uint8_t strip)
{
    //清空指定灯带的颜色，将其设置为0
    if(strip >= STRIP_COUNT) return;
    for(uint8_t i = 0; i < LEDS_PER_STRIP; i++)
    {
        g_ledBuffer[strip][i].r = 0;// 红色分量设置为0
        g_ledBuffer[strip][i].g = 0;// 绿色分量设置为0
        g_ledBuffer[strip][i].b = 0;// 蓝色分量设置为0
    }
}


