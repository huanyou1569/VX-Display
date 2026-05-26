#ifndef WS2812_GPIO_H
#define WS2812_GPIO_H

#include <stdint.h>
#include "main.h"
//映射gpio口和引脚
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} WS2812_Line_t;
void ws2812_gpio_set(uint32_t line_state);
void ws2812_gpio_clear();

#endif /* WS2812_GPIO_H */
