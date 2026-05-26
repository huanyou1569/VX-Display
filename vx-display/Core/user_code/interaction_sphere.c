/**
 * interaction_sphere.c -- 粒子交互系统 v6 (环形/甜甜圈)
 *
 * 架构: 体素扫描/粒子→体素→50面采样→state (后50面 __RBIT 生成)
 *
 * 环形 (Torus): XY平面环面, 绕Z轴, 体素扫描填充
 *   - 大半径 R=8, 小半径 r=3
 *   - 遍历包围盒 + 环面方程检测 → 完美填充表面
 *
 * 球体 (Sphere): Fibonacci 粒子 → 体素
 *
 * 切换: 改 IS_SHAPE 宏
 */

#include "interaction_sphere.h"
#include "ws2812_driver.h"
#include "volume_buffer.h"
#include "volume_draw.h"
#include "volume_math.h"
#include "led_buffer.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ========================================================================== */
/* 常量 */
/* ========================================================================== */

#define IS_PARTICLE_COUNT   200
#define IS_RADIUS_MIN       3
#define IS_RADIUS_MAX       22
#define IS_RADIUS_DEFAULT   10
#define IS_RADIUS_STEP      1
#define IS_BREATH_AMP       1
#define IS_BREATH_PERIOD    30
#define IS_SLICE_BYTES      2304
#define IS_HALF_SLICES      50
#define IS_BUF_BYTES        (IS_HALF_SLICES * IS_SLICE_BYTES)  /* 115200 */

/* ---- 形状选择 ---- */
#define IS_SHAPE_TORUS  1
#define IS_SHAPE_SPHERE 0
#define IS_SHAPE  IS_SHAPE_TORUS

/* 环形参数: XY平面 (绕Z轴), 参考比例 */
#define IS_TORUS_R_MAJOR  8
#define IS_TORUS_R_MINOR  3
#define IS_COLOR_COUNT    6

/* 6种颜色 (R+G+B≤30, 用于甜甜圈和球体) */
static const uint8_t is_color_palette[IS_COLOR_COUNT][3] = {
    {20,  2,  2},  /* 0=红 */
    { 2, 20,  2},  /* 1=绿 */
    { 2,  2, 20},  /* 2=蓝 */
    {12, 12,  2},  /* 3=黄 */
    { 2, 12, 12},  /* 4=青 */
    {12,  2, 12},  /* 5=紫 */
};

/* 球体中心 (x10), IS_SHAPE=0 时使用 */
#define IS_SPHERE_CX10  160
#define IS_SPHERE_CY10  160
#define IS_SPHERE_CZ10  120

/* 呼吸 sin 查表 */
static const int16_t is_sin_lut[256] = {
       0,   804,  1608,  2411,  3212,  4011,  4808,  5602,
    6393,  7180,  7962,  8739,  9512, 10278, 11039, 11793,
   12540, 13279, 14010, 14733, 15447, 16151, 16846, 17531,
   18205, 18868, 19520, 20160, 20788, 21403, 22006, 22595,
   23170, 23732, 24279, 24812, 25330, 25833, 26320, 26791,
   27246, 27684, 28106, 28511, 28899, 29269, 29622, 29957,
   30274, 30572, 30853, 31114, 31357, 31581, 31786, 31972,
   32138, 32286, 32413, 32522, 32610, 32679, 32728, 32757,
   32767, 32757, 32728, 32679, 32610, 32522, 32413, 32286,
   32138, 31972, 31786, 31581, 31357, 31114, 30853, 30572,
   30274, 29957, 29622, 29269, 28899, 28511, 28106, 27684,
   27246, 26791, 26320, 25833, 25330, 24812, 24279, 23732,
   23170, 22595, 22006, 21403, 20788, 20160, 19520, 18868,
   18205, 17531, 16846, 16151, 15447, 14733, 14010, 13279,
   12540, 11793, 11039, 10278,  9512,  8739,  7962,  7180,
    6393,  5602,  4808,  4011,  3212,  2411,  1608,   804,
       0,  -804, -1608, -2411, -3212, -4011, -4808, -5602,
   -6393, -7180, -7962, -8739, -9512,-10278,-11039,-11793,
  -12540,-13279,-14010,-14733,-15447,-16151,-16846,-17531,
  -18205,-18868,-19520,-20160,-20788,-21403,-22006,-22595,
  -23170,-23732,-24279,-24812,-25330,-25833,-26320,-26791,
  -27246,-27684,-28106,-28511,-28899,-29269,-29622,-29957,
  -30274,-30572,-30853,-31114,-31357,-31581,-31786,-31972,
  -32138,-32286,-32413,-32522,-32610,-32679,-32728,-32757,
  -32767,-32757,-32728,-32679,-32610,-32522,-32413,-32286,
  -32138,-31972,-31786,-31581,-31357,-31114,-30853,-30572,
  -30274,-29957,-29622,-29269,-28899,-28511,-28106,-27684,
  -27246,-26791,-26320,-25833,-25330,-24812,-24279,-23732,
  -23170,-22595,-22006,-21403,-20788,-20160,-19520,-18868,
  -18205,-17531,-16846,-16151,-15447,-14733,-14010,-13279,
  -12540,-11793,-11039,-10278, -9512, -8739, -7962, -7180,
   -6393, -5602, -4808, -4011, -3212, -2411, -1608,  -804
};

/* ========================================================================== */
/* 缓冲 & 预计算 */
/* ========================================================================== */

extern uint8_t g_display_buf[];

static uint8_t *is_front_buf;
static uint8_t *is_back_buf;

static uint8_t is_rev_slice[IS_SLICE_BYTES]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

/* 粒子: 3D坐标 (×10) + 亮度 (仅球体使用) */
static int16_t  is_part_x[IS_PARTICLE_COUNT];
static int16_t  is_part_y[IS_PARTICLE_COUNT];
static int16_t  is_part_z[IS_PARTICLE_COUNT];
static uint8_t  is_part_br[IS_PARTICLE_COUNT];
static uint16_t is_part_cnt;

/* ========================================================================== */
/* 状态 */
/* ========================================================================== */

static volatile uint8_t is_active;
static uint8_t  is_brightness = 30;
static uint16_t is_radius_base = IS_RADIUS_DEFAULT;
static uint16_t is_radius_cur;
static int8_t   is_color_temp;
static uint8_t  is_color_index;  /* 颜色索引 0-5 */
static uint16_t is_breath_counter;
static volatile uint8_t is_phase;
static volatile uint8_t is_hall;
static volatile uint8_t is_swap_pending;
static volatile uint8_t is_back_ready;
static volatile int16_t is_rot_accum;
static volatile int8_t  is_temp_target;
static uint32_t is_frame_count;
static uint32_t is_expand_us;

/* 诊断 */
static volatile uint32_t is_diag_render_calls;
static volatile uint32_t is_diag_hall_edges;
static volatile uint32_t is_diag_swaps;
static volatile uint32_t is_diag_generations;
static volatile uint32_t is_diag_particles_set;

/* ========================================================================== */
/* 预计算: 环形/球面 (仅球体使用, 环形直接在expand中体素扫描) */
/* ========================================================================== */

static void is_precompute(void)
{
    const float golden = 0.6180339887498949f;
    const float twopi  = 6.283185307179586f;
    const float cx     = 16.0f;
    const float cy     = 16.0f;
    const float cz     = 12.0f;
    uint16_t n = 0;
    int i;

#if IS_SHAPE == IS_SHAPE_SPHERE
    /* 球体: 200点 Fibonacci 分布 */
    const float ref_r = 16.0f;
    const float scx   = (float)IS_SPHERE_CX10 / 10.0f;
    const float scy   = (float)IS_SPHERE_CY10 / 10.0f;
    const float scz   = (float)IS_SPHERE_CZ10 / 10.0f;

    for (i = 0; i < IS_PARTICLE_COUNT; i++) {
        float y_sphere = 1.0f - (2.0f * (float)i / (float)(IS_PARTICLE_COUNT - 1));
        float r_sphere = sqrtf(1.0f - y_sphere * y_sphere);
        float phi      = (float)i * golden * twopi;

        float px = scx + ref_r * r_sphere * cosf(phi);
        float py = scy + ref_r * r_sphere * sinf(phi);
        float pz = scz + ref_r * y_sphere;

        float sin_b = sinf(phi);
        float br = 0.15f + 0.85f * fabsf(sin_b);

        is_part_x[n]  = (int16_t)(px * 10.0f);
        is_part_y[n]  = (int16_t)(py * 10.0f);
        is_part_z[n]  = (int16_t)(pz * 10.0f);
        is_part_br[n] = (uint8_t)(br * 255.0f);
        n++;
    }
#else
    /* 环形: 体素扫描, 不需要粒子预计算, 但保留占位 */
    (void)cx; (void)cy; (void)cz;
#endif

    is_part_cnt = n;
}

/* ========================================================================== */
/* Q15 定点乘法 */
/* ========================================================================== */

static inline int32_t q15_mul_round(int32_t a, int16_t b)
{
    int32_t prod = a * (int32_t)b;
    if (prod >= 0)
        return (prod + 32768) >> 16;
    else
        return -((-prod + 32768) >> 16);
}

/* ========================================================================== */
/* LED 缓冲区 -> state 数组 (GRB MSB-first, 展开循环优化) */
/* ========================================================================== */

static void led_buffer_to_state(uint8_t *state_out)
{
    uint32_t *wp = (uint32_t *)state_out;
    uint8_t led, strip;

    for (led = 0; led < 24; led++) {
        uint32_t *base = wp + (uint32_t)led * 24;
        for (strip = 0; strip < 32; strip++) {
            LED_Color_t *c = &g_ledBuffer[strip][led];
            uint32_t grb = ((uint32_t)c->g << 16)
                         | ((uint32_t)c->r << 8)
                         |  (uint32_t)c->b;
            uint32_t mask = 1UL << strip;

            if (grb & 0x00800000U) base[0]  |= mask;
            if (grb & 0x00400000U) base[1]  |= mask;
            if (grb & 0x00200000U) base[2]  |= mask;
            if (grb & 0x00100000U) base[3]  |= mask;
            if (grb & 0x00080000U) base[4]  |= mask;
            if (grb & 0x00040000U) base[5]  |= mask;
            if (grb & 0x00020000U) base[6]  |= mask;
            if (grb & 0x00010000U) base[7]  |= mask;
            if (grb & 0x00008000U) base[8]  |= mask;
            if (grb & 0x00004000U) base[9]  |= mask;
            if (grb & 0x00002000U) base[10] |= mask;
            if (grb & 0x00001000U) base[11] |= mask;
            if (grb & 0x00000800U) base[12] |= mask;
            if (grb & 0x00000400U) base[13] |= mask;
            if (grb & 0x00000200U) base[14] |= mask;
            if (grb & 0x00000100U) base[15] |= mask;
            if (grb & 0x00000080U) base[16] |= mask;
            if (grb & 0x00000040U) base[17] |= mask;
            if (grb & 0x00000020U) base[18] |= mask;
            if (grb & 0x00000010U) base[19] |= mask;
            if (grb & 0x00000008U) base[20] |= mask;
            if (grb & 0x00000004U) base[21] |= mask;
            if (grb & 0x00000002U) base[22] |= mask;
            if (grb & 0x00000001U) base[23] |= mask;
        }
    }
}

/* ========================================================================== */
/* 生成 50 面到 state 缓冲区 */
/* ========================================================================== */

static void is_expand_sphere(uint8_t *state)
{
    uint32_t t0 = DWT->CYCCNT;
    int16_t R10, scale_num, scale_den;
    int8_t temp;
    uint16_t phase, p;

#if IS_SHAPE == IS_SHAPE_TORUS
    scale_den = 100;  /* 默认R=10->缩放1.0 */
#else
    scale_den = 160;  /* 球体参考半径16x10 */
#endif

    /* ---- 呼吸 ---- */
    {
        uint8_t idx = (uint8_t)(is_breath_counter * 256U / IS_BREATH_PERIOD);
        int16_t sv  = is_sin_lut[idx];
        int16_t ba  = (int16_t)(((int32_t)IS_BREATH_AMP * 10 * sv + 16384) / 32768);
        R10 = (int16_t)is_radius_base * 10 + ba;
        if (R10 < IS_RADIUS_MIN * 10) R10 = IS_RADIUS_MIN * 10;
        if (R10 > IS_RADIUS_MAX * 10) R10 = IS_RADIUS_MAX * 10;
    }
    is_radius_cur = (uint16_t)((R10 + 5) / 10);
    temp      = is_color_temp;
    scale_num = R10;

    /* ---- 1. 清空体素 ---- */
    VolumeBuffer_Clear();
    LED_SetGlobalBrightness(255);

    /* ---- 2. 形状 -> 体素 ---- */
    {
        int tr = (int)temp;
        int max_tot = ((int)is_brightness * 30 + 50) / 100;
        uint16_t drawn = 0;

#if IS_SHAPE == IS_SHAPE_TORUS
        /* ---- 环形: 体素扫描填充 ---- */
        {
            const int tcx = 18, tcy = 16, tcz = 12;  /* cx=18 偏离轴心2单位 */
            int R_cur = (IS_TORUS_R_MAJOR * (int)scale_num + scale_den / 2)
                        / (int)scale_den;
            int r_cur = (IS_TORUS_R_MINOR * (int)scale_num + scale_den / 2)
                        / (int)scale_den;
            if (R_cur < 2)  R_cur = 2;
            if (r_cur < 1)  r_cur = 1;

            int R2 = R_cur * R_cur;
            int r2 = r_cur * r_cur;
            int threshold = r_cur;
            int bx_min = tcx - (R_cur + r_cur);
            int bx_max = tcx + (R_cur + r_cur);
            int by_min = tcy - (R_cur + r_cur);
            int by_max = tcy + (R_cur + r_cur);
            int bz_min = tcz - r_cur;
            int bz_max = tcz + r_cur;
            int tx, ty, tz;

            for (tx = bx_min; tx <= bx_max; tx++) {
                if (tx < 0) continue; if (tx >= 32) break;
                for (ty = by_min; ty <= by_max; ty++) {
                    if (ty < 0) continue; if (ty >= 32) break;
                    int dx = tx - tcx, dy = ty - tcy;
                    int D2 = dx * dx + dy * dy;

                    for (tz = bz_min; tz <= bz_max; tz++) {
                        if (tz < 0) continue; if (tz >= 24) break;
                        int dz = tz - tcz;

                        /* 环面方程: (sqrt(D2)-R)^2 + (z-cz)^2 ~= r^2 */
                        int D_int = (int)sqrtf((float)D2);
                        int delta = D_int - R_cur;
                        int d2_check = delta * delta + dz * dz;

                        if (abs(d2_check - r2) <= threshold) {
                            int outer = delta > 0 ? delta + r_cur : r_cur + delta;
                            if (outer < 0) outer = 0;
                            int top = dz > 0 ? dz : 0;
                            int depth_num = (max_tot * (30 + outer * 12 + top * 8) + 64) / 128;

                            uint8_t ci = is_color_index;
                            int rv = ((int)is_color_palette[ci][0] * depth_num + 15) / 30;
                            int gv = ((int)is_color_palette[ci][1] * depth_num + 15) / 30;
                            int bv = ((int)is_color_palette[ci][2] * depth_num + 15) / 30;

                            if (rv < 0) rv = 0; if (rv > 255) rv = 255;
                            if (gv < 0) gv = 0; if (gv > 255) gv = 255;
                            if (bv < 0) bv = 0; if (bv > 255) bv = 255;

                            VolumeBuffer_SetVoxel(tx, ty, tz,
                                (uint8_t)rv, (uint8_t)gv, (uint8_t)bv);
                            drawn++;
                        }
                    }
                }
            }
        }
#else
        /* ---- 球体: 粒子 -> 体素 ---- */
        {
            const int16_t Cx10 = IS_SPHERE_CX10;
            const int16_t Cy10 = IS_SPHERE_CY10;
            const int16_t Cz10 = IS_SPHERE_CZ10;

            for (p = 0; p < is_part_cnt; p++) {
                int16_t dx10 = is_part_x[p] - Cx10;
                int16_t dy10 = is_part_y[p] - Cy10;
                int16_t dz10 = is_part_z[p] - Cz10;

                int x = (Cx10 + (int16_t)((int32_t)dx10 * scale_num / scale_den) + 5) / 10;
                int y = (Cy10 + (int16_t)((int32_t)dy10 * scale_num / scale_den) + 5) / 10;
                int z = (Cz10 + (int16_t)((int32_t)dz10 * scale_num / scale_den) + 5) / 10;

                if (x < 0 || x >= 32 || y < 0 || y >= 32 || z < 0 || z >= 24)
                    continue;
                drawn++;

                {
                    uint8_t br = is_part_br[p];
                    int depth = (max_tot * (int)br + 128) / 255;

                    uint8_t ci = is_color_index;
                    int r = ((int)is_color_palette[ci][0] * depth + 15) / 30;
                    int g = ((int)is_color_palette[ci][1] * depth + 15) / 30;
                    int b = ((int)is_color_palette[ci][2] * depth + 15) / 30;

                    if (r < 0) r = 0; if (r > 255) r = 255;
                    if (g < 0) g = 0; if (g > 255) g = 255;
                    if (b < 0) b = 0; if (b > 255) b = 255;

                    VolumeBuffer_SetVoxel(x, y, z, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                }
            }
        }
#endif
        is_diag_particles_set = drawn;
    }

    /* ---- 3. 50面采样 + 编码 ---- */
    memset(state, 0, IS_BUF_BYTES);

    for (phase = 0; phase < IS_HALF_SLICES; phase++) {
        int16_t cos_val = VolumeMath_Cos(phase);
        int16_t sin_val = VolumeMath_Sin(phase);
        uint8_t strip;

        for (strip = 0; strip < 32; strip++) {
            int32_t dx_i = (int32_t)strip * 2 - 31;
            int x = 16 + (int)q15_mul_round(dx_i, cos_val);
            int y = 16 - (int)q15_mul_round(dx_i, sin_val);

            if (x < 0) x = 0; if (x > 31) x = 31;
            if (y < 0) y = 0; if (y > 31) y = 31;

            VolumeBuffer_ReadColumn(x, y, &g_ledBuffer[strip][0]);
        }

        led_buffer_to_state(state + (uint32_t)phase * IS_SLICE_BYTES);
    }

    is_diag_generations++;
    is_expand_us = (DWT->CYCCNT - t0) / 480U;
}

/* ========================================================================== */
/* 公开 API */
/* ========================================================================== */

void IS_Init(void)
{
    is_precompute();

    is_front_buf = g_display_buf;
    is_back_buf  = g_display_buf + IS_BUF_BYTES;

    is_radius_base    = IS_RADIUS_DEFAULT;
    is_color_temp     = 0;
    is_temp_target    = 0;
    is_color_index    = 0;
    is_breath_counter = 0;
    is_phase          = 0;
    is_hall           = 0;
    is_swap_pending   = 0;
    is_back_ready     = 0;
    is_rot_accum      = 0;
    is_frame_count    = 0;
    is_active         = 0;
}

void IS_SetRotation(int8_t dir)   { is_rot_accum += (int16_t)dir; }
void IS_SetColorTemp(int8_t val)  { is_temp_target = val; }

void IS_SetBrightness(uint8_t pct)
{
    if (pct == 0) pct = 1;
    if (pct > 100) pct = 100;
    is_brightness = pct;
}

uint8_t IS_GetBrightness(void) { return is_brightness; }

void IS_Update(void)
{
    if (!is_active) return;

    if (is_swap_pending && is_back_ready) {
        uint8_t *tmp;
        is_swap_pending = 0;
        tmp = is_front_buf; is_front_buf = is_back_buf; is_back_buf = tmp;
        is_back_ready = 0;
        is_frame_count++;
        is_diag_swaps++;

        if (++is_breath_counter >= IS_BREATH_PERIOD * 100U)
            is_breath_counter = 0;

        if (is_rot_accum) {
            int16_t nr = (int16_t)is_radius_base
                       + is_rot_accum * (int16_t)IS_RADIUS_STEP;
            if (nr < IS_RADIUS_MIN) nr = IS_RADIUS_MIN;
            if (nr > IS_RADIUS_MAX) nr = IS_RADIUS_MAX;
            is_radius_base = (uint16_t)nr;
            is_rot_accum = 0;
        }
        is_color_temp = is_temp_target;
    }

    if (!is_back_ready) {
        is_expand_sphere(is_back_buf);
        is_back_ready = 1;
    }
}

/* ---- 渲染: 前50面直接查表, 后50面 __RBIT 反转 ---- */

static void IS_RenderPhase(uint8_t phase)
{
    is_diag_render_calls++;
    if (phase < IS_HALF_SLICES) {
        WS2812_ShowFromSlice(
            &is_front_buf[(uint32_t)phase * IS_SLICE_BYTES]);
    } else {
        uint8_t slot = phase - IS_HALF_SLICES;
        const uint32_t *fwd =
            (const uint32_t *)(is_front_buf + (uint32_t)slot * IS_SLICE_BYTES);
        uint32_t *rp = (uint32_t *)is_rev_slice;
        uint16_t i;
        for (i = 0; i < 576U; i++)
            rp[i] = __RBIT(fwd[i]);
        WS2812_ShowFromSlice(is_rev_slice);
    }
}

void IS_OnHallEdge(void) { is_hall = 1; is_diag_hall_edges++; }

uint8_t IS_RenderNext(void)
{
    if (is_hall) {
        is_hall = 0;
        is_phase = 0;
        is_swap_pending = 1;
    }
    IS_RenderPhase(is_phase);
    is_phase++;
    if (is_phase >= 100) is_phase = 0;
    return is_phase;
}

/* ---- 状态查询 ---- */

uint8_t IS_IsActive(void) { return is_active; }

void IS_Activate(void)
{
    if (is_active) return;
    __HAL_TIM_DISABLE_IT(&htim3, TIM_IT_UPDATE);
    is_expand_sphere(is_front_buf);
    memcpy(is_back_buf, is_front_buf, IS_BUF_BYTES);
    is_back_ready = 1;
    is_active = 1;
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);
}

void IS_Deactivate(void) { is_active = 0; }

void IS_CycleColor(void)
{
    is_color_index++;
    if (is_color_index >= IS_COLOR_COUNT)
        is_color_index = 0;
}

uint16_t IS_GetRadius(void)    { return is_radius_cur; }
int8_t   IS_GetColorTemp(void) { return is_color_temp; }
uint32_t IS_GetExpandUs(void)  { return is_expand_us; }

uint8_t IS_GetFlags(void)
{
    uint8_t f = 0;
    if (is_back_ready)   f |= 0x01;
    if (is_swap_pending) f |= 0x02;
    if (is_hall)         f |= 0x04;
    if (!is_active)      f |= 0x10;
    return f;
}

uint32_t IS_GetDiagRenderCalls(void)  { return is_diag_render_calls; }
uint32_t IS_GetDiagHallEdges(void)    { return is_diag_hall_edges; }
uint32_t IS_GetDiagSwaps(void)        { return is_diag_swaps; }
uint32_t IS_GetDiagGenerations(void)  { return is_diag_generations; }
uint32_t IS_GetDiagParticlesSet(void) { return is_diag_particles_set; }
uint16_t IS_GetParticleCount(void)    { return is_part_cnt; }
