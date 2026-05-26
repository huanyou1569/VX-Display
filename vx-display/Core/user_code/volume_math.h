#ifndef VOLUME_MATH_H
#define VOLUME_MATH_H

#include <stdint.h>
#include"volume_config.h"
#define VOLUME_TRIG_SCALE 32767//q15格式的正弦表的最大值
int16_t VolumeMath_Sin(uint16_t angle_index);
int16_t VolumeMath_Cos(uint16_t angle_index);

#endif /* VOLUME_MATH_H */
