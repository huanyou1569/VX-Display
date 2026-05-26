#ifndef LED_BUFFER_H
#define LED_BUFFER_H

#include <stdint.h>
#define STRIP_COUNT     32// 32条灯带
#define LEDS_PER_STRIP  24// 每条灯带24颗灯
/// 这是一个LED缓冲区的头文件，定义了LED颜色结构体和相关函数
typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} LED_Color_t;

extern LED_Color_t g_ledBuffer[STRIP_COUNT][LEDS_PER_STRIP];


void LED_Buffer_Init(void);
void LED_Buffer_ClearAll(void);
void LED_Buffer_SetColor(uint8_t strip, uint8_t ledIndex, uint8_t r, uint8_t g, uint8_t b);
void LED_Buffer_Test(uint8_t strip,  uint8_t r, uint8_t g, uint8_t b);
void LED_Buffer_TestAll(uint8_t r, uint8_t g, uint8_t b);
void LED_Buffer_ClearStrip(uint8_t strip);

#endif /* LED_BUFFER_H */
