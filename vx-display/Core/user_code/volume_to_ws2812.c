#include "volume_to_ws2812.h"
#include "volume_config.h"
#include "volume_math.h"
#include "Zelda_Z_no_base.h"

/* 当前旋转相位 */
static volatile uint8_t s_current_phase = 0;
/* 霍尔触发标志（ISR 设置，主循环清零） */
static volatile uint8_t s_hall_edge = 0;
/* 霍尔是否已触发过 */
static volatile uint8_t s_hall_synced = 0;

/* -------------------------------------------------------------------------- */
/* 内部辅助：坐标裁剪                                                        */
/* -------------------------------------------------------------------------- */
static inline int vtw_clamp(int val, int min, int max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* -------------------------------------------------------------------------- */
/* Q15 定点乘法：a * b / 65536，四舍五入                                     */
/*   dx_i 放大 2 倍补偿 Q15→Q16 的分母差异                                    */
/* -------------------------------------------------------------------------- */
static inline int32_t vtw_q15_mul_round(int32_t a, int16_t b)
{
    int32_t prod = a * (int32_t)b;
    if (prod >= 0)
        return (prod + 32768) >> 16;
    else
        return -((-prod + 32768) >> 16);
}

/* -------------------------------------------------------------------------- */
void VolumeToWS2812_Init(void)
{
    LED_Buffer_Init();
    VolumeBuffer_Clear();
    s_current_phase = 0;
    s_hall_edge     = 0;
    s_hall_synced   = 0;
}

/* -------------------------------------------------------------------------- */
/* 核心映射：将体素空间按指定相位投影到 LED 缓冲区                             */
/*                                                                           */
/* 旋转轴位于 strip 15.5（dx_i = strip*2 - 31，零点在 strip=15.5）。           */
/* dx_i * 2 搭配 /65536 的分母，使 cos=32767 时 strip 0→x=0, strip31→x=31。  */
/*                                                                           */
/* 参照 RuiJi.Slice CircleSlicer 的 Rotate180FlipX，Y 方向取反。               */
/* -------------------------------------------------------------------------- */
void VolumeToWS2812_RenderPhase(uint8_t phase)
{
    if (phase >= VOLUME_ANGLE_SLICES)
        phase = 0;

    int16_t cos_val = VolumeMath_Cos(phase);
    int16_t sin_val = VolumeMath_Sin(phase);

    for (uint8_t strip = 0; strip < STRIP_COUNT; strip++)
    {
        int32_t dx_i = (int32_t)strip * 2 - 31;

        int x = VOLUME_CENTER_X + vtw_q15_mul_round(dx_i, cos_val);
        int y = VOLUME_CENTER_Y - vtw_q15_mul_round(dx_i, sin_val);

        x = vtw_clamp(x, 0, VOLUME_DIAMETER - 1);
        y = vtw_clamp(y, 0, VOLUME_DIAMETER - 1);

        /* 一次 memcpy(72B) 代替 24 次 GetVoxel+SetColor */
        VolumeBuffer_ReadColumn(x, y, &g_ledBuffer[strip][0]);
    }

    WS2812_Show();
}

/* -------------------------------------------------------------------------- */
/* 自动相位渲染：霍尔同步后再递增，避免 ISR 与主循环的竞态条件                 */
/* -------------------------------------------------------------------------- */
void VolumeToWS2812_RenderNext(void)
{
    /* 霍尔同步：ISR 只设标志，主循环在这里安全复位相位 */
    if (s_hall_edge)
    {
        s_hall_edge = 0;
        s_current_phase = 0;
        s_hall_synced = 1;
    }

    uint8_t phase = s_current_phase;
    VolumeToWS2812_RenderPhase(phase);

    phase++;
    if (phase >= VOLUME_ANGLE_SLICES)
        phase = 0;
    s_current_phase = phase;
}

/* -------------------------------------------------------------------------- */
uint8_t VolumeToWS2812_GetCurrentPhase(void)
{
    return s_current_phase;
}

/* -------------------------------------------------------------------------- */
/* 霍尔中断回调：设置标志，由主循环安全处理相位复位                            */
/* -------------------------------------------------------------------------- */
void VolumeToWS2812_OnHallEdge(void)
{
    s_hall_edge = 1;
}

/* -------------------------------------------------------------------------- */
void VolumeToWS2812_DrawTestScene(void)
{
    VolumeBuffer_Clear();

    volume_draw_sphere_shell(
        VOLUME_CENTER_X,
        VOLUME_CENTER_Y,
        VOLUME_CENTER_Z,
        8,
        200, 0, 0
    );

    volume_draw_line(
        VOLUME_CENTER_X - 8, VOLUME_CENTER_Y,     VOLUME_CENTER_Z,
        VOLUME_CENTER_X + 8, VOLUME_CENTER_Y,     VOLUME_CENTER_Z,
        200, 200, 200
    );
    volume_draw_line(
        VOLUME_CENTER_X,     VOLUME_CENTER_Y - 8, VOLUME_CENTER_Z,
        VOLUME_CENTER_X,     VOLUME_CENTER_Y + 8, VOLUME_CENTER_Z,
        200, 200, 200
    );
    volume_draw_line(
        VOLUME_CENTER_X,     VOLUME_CENTER_Y,     VOLUME_CENTER_Z - 8,
        VOLUME_CENTER_X,     VOLUME_CENTER_Y,     VOLUME_CENTER_Z + 8,
        200, 200, 200
    );
}

void volume_load_model(void)
{
    VolumeBuffer_Clear();

    for (int z = 0; z < VOLUME_HEIGHT; z++)
    {
        for (int y = 0; y < VOLUME_DIAMETER; y++)
        {
            for (int x = 0; x < VOLUME_DIAMETER; x++)
            {
                if (g_volumeData[z][y][x] != 0)
                    VolumeBuffer_SetVoxel(x, y, z, 40, 0, 0);
            }
        }
    }
}
