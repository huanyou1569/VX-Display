#ifndef WS2812_DRIVER_H
#define WS2812_DRIVER_H

#include <stdint.h>
#include"led_buffer.h"

#include "ws2812_code.h"
#include "ws2812_enable.h"
#include "ws2812_gpio.h"
#include "ws2812_port.h"
#include"main.h"
#include"tim.h"
#include"dma.h"


void WS2812_Send(void);
uint8_t WS2812_IsBusy(void);
void WS2812_DMA_Complete(DMA_HandleTypeDef *hdma);
void WS2812_DMA_Error(DMA_HandleTypeDef *hdma);
void WS2812_Show(void);
void WS2812_ShowFast(void);   /* FillWave from g_ledBuffer */
void WS2812_ShowFromSlice(const uint8_t *states); /* pre-computed state → BSRR */
uint32_t WS2812_GetRefreshCount(void);
uint32_t WS2812_GetSkipCount(void);
uint32_t WS2812_GetBusyMaxUs(void);
uint32_t WS2812_GetFillMaxUs(void);


#endif /* WS2812_DRIVER_H */
