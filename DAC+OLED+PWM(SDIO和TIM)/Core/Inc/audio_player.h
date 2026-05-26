#ifndef __AUDIO_PLAYER_H
#define __AUDIO_PLAYER_H
#include "main.h"

void AudioPlayer_Init(void);
void AudioPlayer_Start(void);
void AudioPlayer_Process(void);
void AudioPlayer_Stop(void);
void AudioPlayer_Play(const char *filename);

#endif
