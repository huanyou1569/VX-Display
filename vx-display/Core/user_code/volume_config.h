#ifndef VOLUME_CONFIG_H
#define VOLUME_CONFIG_H
//这是基础参数定义文件
#define VOLUME_DIAMETER        32//定义体积的直径为灯珠
#define VOLUME_RADIUS          (VOLUME_DIAMETER / 2)//半径为直径的一半
#define VOLUME_HEIGHT          24//高度为24灯珠
#define VOLUME_ANGLE_SLICES    100//切片数量为100片

#define VOLUME_CENTER_X        (VOLUME_DIAMETER / 2)//中心点的X坐标为直径的一半
#define VOLUME_CENTER_Y        (VOLUME_DIAMETER / 2)//中心点的Y坐标为直径的一半
#define VOLUME_CENTER_Z        (VOLUME_HEIGHT / 2)//中心点的Z坐标为高度的一半

#define VOLUME_PI              3.1415926f//定义圆周率为3.1415926

#endif