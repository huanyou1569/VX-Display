#include "beep_con.h"
#include "dac.h"
#include "tim.h"

extern TIM_HandleTypeDef htim6;

uint8_t audio_ring_buffer[AUDIO_RING_BUFFER_SIZE];
volatile uint32_t p_read  = 0;
volatile uint32_t p_write = 0;
Beep_sta con_sta = STOP;

void Beep_Init(void)
{
    con_sta  = STOP;
    p_read   = 0;
    p_write  = 0;

    for(int i = 0; i < AUDIO_RING_BUFFER_SIZE; i++)
    {
        audio_ring_buffer[i] = 0x80;
    }
}

void Beep_Con(Beep_sta sta)
{
    con_sta = sta;
}

uint32_t Audio_Get_Free_Space(void)
{
    uint32_t read_temp  = p_read;
    uint32_t write_temp = p_write;

    if (write_temp >= read_temp)
    {
        return AUDIO_RING_BUFFER_SIZE - (write_temp - read_temp) - 1;
    }
    else
    {
        return (read_temp - write_temp) - 1;
    }
}

void Audio_Write_To_Buffer(uint8_t* data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        audio_ring_buffer[p_write] = data[i];
        p_write++;
        if(p_write >= AUDIO_RING_BUFFER_SIZE)
        {
            p_write = 0;
        }
    }
}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance == TIM6)
    {
        if(con_sta == RUN)
        {
            uint8_t sample;
            if(p_read != p_write)
            {
                sample = audio_ring_buffer[p_read];
                p_read++;
                if(p_read >= AUDIO_RING_BUFFER_SIZE)
                    p_read = 0;
            }
            else
            {
                sample = 0x80;
            }

            // ???????????
            int16_t ac = (int16_t)sample - 128;    // -128 ~ +127
            const int16_t GAIN = 40;                // ???:4 ~ 8
            ac = ac * GAIN;

            // ??????
            if (ac > 127)  ac = 127;
            if (ac < -128) ac = -128;

            uint8_t amp_sample = (uint8_t)(ac + 128);
            uint16_t dac_val = (uint16_t)((uint32_t)amp_sample * 4095 / 255);
            HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_val);
        }
    }
}


