#include "ws2812_gpio.h"
#include "stm32h7xx_hal_gpio.h"
#define LINE_COUNT 32

//用g_ws2812Lines数组存储每条LED线对应的GPIO端口和引脚信息，方便后续操作
static const WS2812_Line_t g_ws2812Lines[LINE_COUNT] =
{
    {LED_LINE1_GPIO_Port,  LED_LINE1_Pin},
    {LED_LINE2_GPIO_Port,  LED_LINE2_Pin},
    {LED_LINE3_GPIO_Port,  LED_LINE3_Pin},
    {LED_LINE4_GPIO_Port,  LED_LINE4_Pin},
    {LED_LINE5_GPIO_Port,  LED_LINE5_Pin},
    {LED_LINE6_GPIO_Port,  LED_LINE6_Pin},
    {LED_LINE7_GPIO_Port,  LED_LINE7_Pin},
    {LED_LINE8_GPIO_Port,  LED_LINE8_Pin},

    {LED_LINE9_GPIO_Port,  LED_LINE9_Pin},
    {LED_LINE10_GPIO_Port, LED_LINE10_Pin},
    {LED_LINE11_GPIO_Port, LED_LINE11_Pin},
    {LED_LINE12_GPIO_Port, LED_LINE12_Pin},
    {LED_LINE13_GPIO_Port, LED_LINE13_Pin},
    {LED_LINE14_GPIO_Port, LED_LINE14_Pin},
    {LED_LINE15_GPIO_Port, LED_LINE15_Pin},
    {LED_LINE16_GPIO_Port, LED_LINE16_Pin},

    {LED_LINE17_GPIO_Port, LED_LINE17_Pin},
    {LED_LINE18_GPIO_Port, LED_LINE18_Pin},
    {LED_LINE19_GPIO_Port, LED_LINE19_Pin},
    {LED_LINE20_GPIO_Port, LED_LINE20_Pin},
    {LED_LINE21_GPIO_Port, LED_LINE21_Pin},
    {LED_LINE22_GPIO_Port, LED_LINE22_Pin},
    {LED_LINE23_GPIO_Port, LED_LINE23_Pin},
    {LED_LINE24_GPIO_Port, LED_LINE24_Pin},

    {LED_LINE25_GPIO_Port, LED_LINE25_Pin},
    {LED_LINE26_GPIO_Port, LED_LINE26_Pin},
    {LED_LINE27_GPIO_Port, LED_LINE27_Pin},
    {LED_LINE28_GPIO_Port, LED_LINE28_Pin},
    {LED_LINE29_GPIO_Port, LED_LINE29_Pin},
    {LED_LINE30_GPIO_Port, LED_LINE30_Pin},
    {LED_LINE31_GPIO_Port, LED_LINE31_Pin},
    {LED_LINE32_GPIO_Port, LED_LINE32_Pin},
};
//设置引脚的高低电平
void ws2812_gpio_set(uint32_t line_state){
    GPIO_PinState pin_state;
    for(int i=0;i<LINE_COUNT;i++){
        if(line_state&(1UL<<i)){
            pin_state=GPIO_PIN_SET;
        }
        else{
            pin_state=GPIO_PIN_RESET;
        }
        HAL_GPIO_WritePin(g_ws2812Lines[i].port,g_ws2812Lines[i].pin,pin_state);
    }
}
//清空电平输出
void ws2812_gpio_clear(){
    ws2812_gpio_set(0);
}
