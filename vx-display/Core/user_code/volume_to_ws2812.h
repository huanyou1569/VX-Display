#ifndef VOLUME_TO_WS2812_H
#define VOLUME_TO_WS2812_H

#include <stdint.h>
#include "volume_buffer.h"
#include "volume_draw.h"
#include "led_buffer.h"
#include "ws2812_driver.h"

/*
 * volume_to_ws2812
 * 作用：将 volume_buffer 中的 3D 体素数据映射到 ws2812 驱动层的 LED 缓冲区，
 *       并通过 DMA+TIM 发送出去，使旋转 LED 阵列利用视觉残留显示出 3D 图像。
 *
 * 显示逻辑：
 *   - 体素空间 g_volumeBuffer[32][32][24] 是静止的世界坐标系。
 *   - 物理 LED 阵列为 32(strip) x 24(led) 的竖直平面，绕竖直轴旋转。
 *   - 转轴位于 strip = 15.5（VOLUME_DIAMETER/2 - 0.5）。
 *   - 一圈分为 VOLUME_ANGLE_SLICES(50) 个相位切片。
 *   - 在每个相位，LED 平面作为“扫描平面”，其上每个 LED 映射到体素空间中
 *     该角度直径方向上的对应坐标，采样体素颜色后输出。
 *
 * 时序控制：
 *   - 由调用方按约 1000 Hz（每 1 ms）的节奏调用 RenderPhase() 或 RenderNext()。
 *   - 函数内部先填充 g_ledBuffer，再调用 WS2812_Show() 启动 DMA 发送。
 *   - 霍尔同步：在霍尔中断中调用 OnHallEdge()，将相位归零，避免长期漂移。
 */

/* 初始化：清零 LED 缓冲区和体素缓冲区 */
void VolumeToWS2812_Init(void);

/* 渲染指定相位（0 .. VOLUME_ANGLE_SLICES-1），并启动 DMA 发送 */
void VolumeToWS2812_RenderPhase(uint8_t phase);

/* 使用当前内部相位渲染一帧，然后自动递增相位；适合开环或霍尔补偿后的主循环 */
void VolumeToWS2812_RenderNext(void);

/* 获取当前内部相位（供调试或上位机显示用） */
uint8_t VolumeToWS2812_GetCurrentPhase(void);

/* 在霍尔中断中调用：标记一圈开始，复位相位到 0 */
void VolumeToWS2812_OnHallEdge(void);

/* 在体素空间中绘制一个测试场景（红色球壳 + 白色十字），用于验证映射 */
void VolumeToWS2812_DrawTestScene(void);
//模型加载函数
void volume_load_model(void);

#endif /* VOLUME_TO_WS2812_H */
