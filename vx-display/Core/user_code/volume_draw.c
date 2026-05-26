#include "volume_draw.h"
#include <stdlib.h>

//画点函数
void volume_draw_point(int x, int y, int z, uint8_t r, uint8_t g, uint8_t b) {
   
    VolumeBuffer_SetVoxel(x, y, z, r, g, b); // 设置体素颜色
}
//获取最大值函数
int max_i(int a,int b,int c){
    int max_i_val=a;
    if(b>max_i_val) max_i_val=b;
    if(c>max_i_val) max_i_val=c;
    return max_i_val;
}
//取绝对值函数
int abs_i(int x){
    if(x<0) return -x;
    return x;

}
//画线函数,使用3d Bresenham算法
void volume_draw_line(int x0, int y0, int z0, int x1, int y1, int z1, uint8_t r, uint8_t g, uint8_t b) {
    int dx=abs_i(x1-x0);//计算x方向上的长度
    int dy=abs_i(y1-y0);
    int dz=abs_i(z1-z0);
    //判断每一步是正着走还是反着走，当为1时正走x每一步都增加，当为-1时减少
    int sx = (x1 >= x0) ? 1 : -1;
    int sy = (y1 >= y0) ? 1 : -1;
    int sz = (z1 >= z0) ? 1 : -1;
    //当前坐标
    int x = x0;
    int y = y0;
    int z = z0;
    //先绘制起点
    volume_draw_point(x, y, z, r, g, b);
    //计算主方向
    int max_i_d=max_i(dx, dy, dz);
    if(max_i_d==dx){
        int p1 = 2 * dy - dx;//两个误差变量，用于判断什么时候跟着x走
        int p2 = 2 * dz - dx;
        while(x != x1){
            x += sx;
            if(p1 >= 0)
            {
                    y += sy;
                    p1 -= 2 * dx;
            }
            if(p2 >= 0)
            {
                    z += sz;
                    p2 -= 2 * dx;
            }
            p1 += 2 * dy;
            p2 += 2 * dz;
            volume_draw_point(x, y, z, r, g, b);
        }
        
    }else if(max_i_d==dy){
        int p1 = 2 * dx - dy;//两个误差变量，用于判断什么时候跟着y走
        int p2 = 2 * dz - dy;
        while(y != y1){
            y += sy;
            if(p1 >= 0)
            {
                    x += sx;
                    p1 -= 2 * dy;
            }
            if(p2 >= 0)
            {
                    z += sz;
                    p2 -= 2 * dy;
            }
            p1 += 2 * dx;
            p2 += 2 * dz;
            volume_draw_point(x, y, z, r, g, b);
        }
    }else{
        int p1 = 2 * dy - dz;//两个误差变量，用于判断什么时候跟着z走
        int p2 = 2 * dx - dz;
        while(z != z1){
            z += sz;
            if(p1 >= 0){
                    y += sy;
                    p1 -= 2 * dz;
            }
            if(p2 >= 0){
                    x += sx;
                    p2 -= 2 * dz;
            }
            p1 += 2 * dy;
            p2 += 2 * dx;
            volume_draw_point(x, y, z, r, g, b);
        }
    }

}
//画球壳函数
void volume_draw_sphere_shell(int cx, int cy, int cz, int radius,
                            uint8_t r, uint8_t g, uint8_t b)
{
    int r2 = radius * radius;
    int threshold = radius;

    for(int x = cx - radius; x <= cx + radius; x++)
    {
        for(int y = cy - radius; y <= cy + radius; y++)
        {
            for(int z = cz - radius; z <= cz + radius; z++)
            {
                int dx = x - cx;
                int dy = y - cy;
                int dz = z - cz;

                int d2 = dx * dx + dy * dy + dz * dz;

                if(abs(d2 - r2) <= threshold)
                {
                    volume_draw_point(x, y, z, r, g, b);
                }
            }
        }
    }
}

