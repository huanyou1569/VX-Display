#ifndef __BEEP_CON_H
#define __BEEP_CON_H
#include "main.h"

#define AUDIO_RING_BUFFER_SIZE (64 * 1024)//音频缓冲区大小

typedef enum {//播放状态
    STOP = 0,
    RUN = 1
} Beep_sta;

void Beep_Init(void);//初始化
void Beep_Con(Beep_sta sta);//播放开始/停止
uint32_t Audio_Get_Free_Space(void);//检测缓冲区
void Audio_Write_To_Buffer(uint8_t* data, uint32_t len);//写入缓冲区

#endif
