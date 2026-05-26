/**
 * @file audio_control.c
 * @brief 音频播放控制 - 封装audio_player
 */
#include "audio_control.h"
#include "audio_player.h"

void AudioControl_Init(void) {
    AudioPlayer_Init();
}

void AudioControl_Poll(void) {
    AudioPlayer_Process();
}
