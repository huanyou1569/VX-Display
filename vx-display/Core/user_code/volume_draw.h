#ifndef VOLUME_DRAW_H
#define VOLUME_DRAW_H

#include <stdint.h>
#include "volume_buffer.h"
#include "volume_math.h"
void volume_draw_point(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b);
void volume_draw_line(int x0, int y0, int z0, int x1, int y1, int z1, uint8_t r, uint8_t g, uint8_t b);

void volume_draw_sphere_shell(int cx, int cy, int cz, int radius, uint8_t r, uint8_t g, uint8_t b);


#endif /* VOLUME_DRAW_H */
