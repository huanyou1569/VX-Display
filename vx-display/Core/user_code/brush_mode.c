/**
 * brush_mode.c -- 3D画笔交互模式
 *
 * 架构: 画布bitmap→体素→50面采样→state (后50面 __RBIT)
 *
 * 操作:
 *   摇杆         = JL/JR/JU/JD (步进1体素)
 *   摇杆按钮     = JB0/Y轴, JB1/Z轴
 *   画笔按钮     = JP 切换画/移模式
 *   激活/退出    = :BRUSH / JE
 *   清屏         = :CLEAR
 */

#include "brush_mode.h"
#include "ws2812_driver.h"
#include "volume_buffer.h"
#include "volume_math.h"
#include "led_buffer.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/* 常量 */
/* ========================================================================== */

#define BRUSH_SLICE_BYTES   2304
#define BRUSH_HALF_SLICES   50
#define BRUSH_BUF_BYTES     (BRUSH_HALF_SLICES * BRUSH_SLICE_BYTES)  /* 115200 */

#define BRUSH_VOLUME_X      32
#define BRUSH_VOLUME_Y      32
#define BRUSH_VOLUME_Z      24
#define BRUSH_SIZE          2       /* 笔刷 2×2×2 */

#define BRUSH_DEFAULT_X     16
#define BRUSH_DEFAULT_Y     16
#define BRUSH_DEFAULT_Z     12

#define BRUSH_BLINK_FRAMES  4       /* 光标闪烁周期 (帧) */
#define BRUSH_COLOR_COUNT   6       /* 6种颜料颜色 */

/* 6种颜料 (R+G+B≤30) */
static const uint8_t paint_palette[BRUSH_COLOR_COUNT][3] = {
    {30,  0,  0},  /* 0=红 */
    { 0, 30,  0},  /* 1=绿 */
    { 0,  0, 30},  /* 2=蓝 */
    {15, 15,  0},  /* 3=黄 */
    { 0, 15, 15},  /* 4=青 */
    {15,  0, 15},  /* 5=紫 */
};

/* 移模式光标: 白色 R+G+B=30 */
#define CURSOR_W_R  12
#define CURSOR_W_G  12
#define CURSOR_W_B   6

/* ========================================================================== */
/* 画布 (颜色索引 0=空 1-6=颜料, AXI SRAM) */
/* ========================================================================== */

static uint8_t brush_canvas[BRUSH_VOLUME_X][BRUSH_VOLUME_Y][BRUSH_VOLUME_Z]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

/* ========================================================================== */
/* 渲染缓冲 */
/* ========================================================================== */

extern uint8_t g_display_buf[];

static uint8_t *brush_front_buf;    /* 50 面 */
static uint8_t *brush_back_buf;     /* 50 面 */

static uint8_t brush_rev[BRUSH_SLICE_BYTES]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

/* ========================================================================== */
/* 画笔状态 */
/* ========================================================================== */

static volatile uint8_t brush_active;
static uint8_t  brush_x       = BRUSH_DEFAULT_X;
static uint8_t  brush_y       = BRUSH_DEFAULT_Y;
static uint8_t  brush_z       = BRUSH_DEFAULT_Z;
static uint8_t  brush_drawing;           /* 1=画模式, 0=移模式 */
static uint8_t  brush_btn_held;          /* 1=按下(Z轴), 0=松开(Y轴) */
static uint16_t brush_angle_offset;      /* 霍尔视角偏移 (0-99相位) */
static uint8_t  brush_color_index;       /* 当前颜料 (0-5) */
static uint8_t  brush_brightness = 30;   /* 1-100 */
static int8_t   brush_color_temp;        /* -128..127 */

static volatile uint8_t  brush_phase;
static volatile uint8_t  brush_hall;
static volatile uint8_t  brush_swap_pending;
static volatile uint8_t  brush_back_ready;
static uint32_t brush_frame_count;
static uint32_t brush_swap_count;
static uint32_t brush_expand_us;

/* 诊断 */
static volatile uint32_t brush_diag_render_calls;
static volatile uint32_t brush_diag_hall_edges;
static volatile uint32_t brush_diag_swaps;
static volatile uint32_t brush_diag_generations;
static volatile uint32_t brush_diag_voxels;  /* 画布上已画体素数 */

/* ========================================================================== */
/* 颜色切换 */
/* ========================================================================== */

void Brush_CycleColor(void)
{
    brush_color_index++;
    if (brush_color_index >= BRUSH_COLOR_COUNT)
        brush_color_index = 0;
}

/* ========================================================================== */
/* 画布操作 */
/* ========================================================================== */

static void canvas_clear(void)
{
    memset(brush_canvas, 0, sizeof(brush_canvas));
    brush_diag_voxels = 0;
}

static void canvas_set_brush(void)
{
    int dx, dy, dz;
    uint8_t ci = (uint8_t)(brush_color_index + 1);  /* 1-6 */
    for (dx = 0; dx < BRUSH_SIZE; dx++) {
        for (dy = 0; dy < BRUSH_SIZE; dy++) {
            for (dz = 0; dz < BRUSH_SIZE; dz++) {
                uint8_t *p = &brush_canvas
                    [brush_x + dx][brush_y + dy][brush_z + dz];
                if (*p == 0) {
                    *p = ci;
                    brush_diag_voxels++;
                }
            }
        }
    }
}

/* ========================================================================== */
/* 光标绘制 (移模式=白, 画模式=当前颜料色) */
/* ========================================================================== */

static void cursor_draw(uint8_t dim)
{
    uint8_t cr, cg, cb;
    if (brush_drawing) {
        uint8_t b = dim ? (uint8_t)(BRUSH_BLINK_FRAMES / 2) : BRUSH_BLINK_FRAMES;
        cr = (uint8_t)((uint16_t)paint_palette[brush_color_index][0] * b / BRUSH_BLINK_FRAMES);
        cg = (uint8_t)((uint16_t)paint_palette[brush_color_index][1] * b / BRUSH_BLINK_FRAMES);
        cb = (uint8_t)((uint16_t)paint_palette[brush_color_index][2] * b / BRUSH_BLINK_FRAMES);
    } else {
        cr = CURSOR_W_R; cg = CURSOR_W_G; cb = CURSOR_W_B;
    }
    int dx, dy, dz;
    for (dx = 0; dx < BRUSH_SIZE; dx++)
        for (dy = 0; dy < BRUSH_SIZE; dy++)
            for (dz = 0; dz < BRUSH_SIZE; dz++)
                VolumeBuffer_SetVoxel(
                    brush_x + dx, brush_y + dy, brush_z + dz, cr, cg, cb);
}

/* ========================================================================== */
/* 画布 → 体素 (按各体素的颜色索引着色) */
/* ========================================================================== */

static void canvas_to_volume(void)
{
    int x, y, z;
    for (x = 0; x < BRUSH_VOLUME_X; x++)
        for (y = 0; y < BRUSH_VOLUME_Y; y++)
            for (z = 0; z < BRUSH_VOLUME_Z; z++) {
                uint8_t ci = brush_canvas[x][y][z];
                if (ci) {
                    ci--;
                    VolumeBuffer_SetVoxel(x, y, z,
                        paint_palette[ci][0],
                        paint_palette[ci][1],
                        paint_palette[ci][2]);
                }
            }
}

/* ========================================================================== */
/* LED 缓冲 → state 数组 (GRB MSB-first, 展开循环) */
/* ========================================================================== */

static void led_to_state(uint8_t *state_out)
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
/* Q15 定点乘 */
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
/* 生成 50 面 */
/* ========================================================================== */

static void brush_expand(uint8_t *state)
{
    uint32_t t0 = DWT->CYCCNT;
    uint16_t phase;
    uint8_t strip;

    /* ---- 1. 清空体素 ---- */
    VolumeBuffer_Clear();
    LED_SetGlobalBrightness(255);

    /* ---- 2. 画布 → 体素 ---- */
    canvas_to_volume();

    /* ---- 3. 光标 (闪烁: 画模式亮暗交替, 移模式亮灭交替) ---- */
    {
        uint8_t phase = brush_swap_count & (BRUSH_BLINK_FRAMES - 1);
        if (brush_drawing) {
            /* 画模式: 亮→暗→亮→暗 交替, 始终有光标 */
            cursor_draw(phase >= (BRUSH_BLINK_FRAMES / 2));
        } else {
            /* 移模式: 亮→灭→亮→灭 交替 */
            if (phase < (BRUSH_BLINK_FRAMES / 2))
                cursor_draw(0);
        }
    }

    /* ---- 4. 50 面采样 ---- */
    memset(state, 0, BRUSH_BUF_BYTES);

    for (phase = 0; phase < BRUSH_HALF_SLICES; phase++) {
        int16_t cos_val = VolumeMath_Cos(phase);
        int16_t sin_val = VolumeMath_Sin(phase);

        for (strip = 0; strip < 32; strip++) {
            int32_t dx_i = (int32_t)strip * 2 - 31;
            int x = 16 + (int)q15_mul_round(dx_i, cos_val);
            int y = 16 - (int)q15_mul_round(dx_i, sin_val);

            if (x < 0) x = 0; if (x > 31) x = 31;
            if (y < 0) y = 0; if (y > 31) y = 31;

            VolumeBuffer_ReadColumn(x, y, &g_ledBuffer[strip][0]);
        }

        led_to_state(state + (uint32_t)phase * BRUSH_SLICE_BYTES);
    }

    brush_diag_generations++;
    brush_expand_us = (DWT->CYCCNT - t0) / 480U;
}

/* ========================================================================== */
/* 公开 API */
/* ========================================================================== */

void Brush_Init(void)
{
    brush_front_buf = g_display_buf;
    brush_back_buf  = g_display_buf + BRUSH_BUF_BYTES;

    brush_x = BRUSH_DEFAULT_X;
    brush_y = BRUSH_DEFAULT_Y;
    brush_z = BRUSH_DEFAULT_Z;
    brush_drawing      = 0;
    brush_btn_held     = 0;
    brush_angle_offset = 51;   /* 默认: 霍尔位置=前方 */
    brush_color_index  = 0;
    brush_brightness   = 30;
    brush_color_temp   = 0;
    brush_phase        = 0;
    brush_hall         = 0;
    brush_swap_pending = 0;
    brush_back_ready   = 0;
    brush_frame_count  = 0;
    brush_swap_count   = 0;
    brush_active       = 0;

    canvas_clear();
}

void Brush_Activate(void)
{
    if (brush_active) return;

    __HAL_TIM_DISABLE_IT(&htim3, TIM_IT_UPDATE);

    brush_expand(brush_front_buf);
    memcpy(brush_back_buf, brush_front_buf, BRUSH_BUF_BYTES);
    brush_back_ready = 1;
    brush_active = 1;

    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);
}

void Brush_Deactivate(void) { brush_active = 0; }
uint8_t Brush_IsActive(void) { return brush_active; }

void Brush_ClearCanvas(void)
{
    canvas_clear();
    /* 非激活状态只清画布; 激活状态需保留下次刷新生效 */
}

/* ========================================================================== */
/* 主循环更新 (同 IS 模式: 换帧 + 生成下一帧) */
/* ========================================================================== */

void Brush_Update(void)
{
    if (!brush_active) return;

    /* 换帧 */
    if (brush_swap_pending && brush_back_ready) {
        uint8_t *tmp;
        brush_swap_pending = 0;
        tmp = brush_front_buf;
        brush_front_buf = brush_back_buf;
        brush_back_buf = tmp;
        brush_back_ready = 0;
        brush_frame_count++;
        brush_swap_count++;
        brush_diag_swaps++;
    }

    /* 画模式: 落笔留痕 */
    if (brush_drawing)
        canvas_set_brush();

    /* 生成下一帧 */
    if (!brush_back_ready) {
        brush_expand(brush_back_buf);
        brush_back_ready = 1;
    }
}

/* ========================================================================== */
/* 渲染 (TIM3 ISR) */
/* ========================================================================== */

static void Brush_RenderPhase(uint8_t phase)
{
    brush_diag_render_calls++;
    if (phase < BRUSH_HALF_SLICES) {
        WS2812_ShowFromSlice(
            &brush_front_buf[(uint32_t)phase * BRUSH_SLICE_BYTES]);
    } else {
        uint8_t slot = phase - BRUSH_HALF_SLICES;
        const uint32_t *fwd =
            (const uint32_t *)(brush_front_buf
                + (uint32_t)slot * BRUSH_SLICE_BYTES);
        uint32_t *rp = (uint32_t *)brush_rev;
        uint16_t i;
        for (i = 0; i < 576U; i++)
            rp[i] = __RBIT(fwd[i]);
        WS2812_ShowFromSlice(brush_rev);
    }
}

void Brush_OnHallEdge(void) { brush_hall = 1; brush_diag_hall_edges++; }

uint8_t Brush_RenderNext(void)
{
    if (brush_hall) {
        brush_hall = 0;
        brush_phase = 0;
        brush_swap_pending = 1;
    }
    Brush_RenderPhase(brush_phase);
    brush_phase++;
    if (brush_phase >= 100) brush_phase = 0;
    return brush_phase;
}

/* ========================================================================== */
/* BLE 指令处理 */
/* ========================================================================== */

uint8_t Brush_HandleJoy(const uint8_t *data, uint16_t size)
{
    if (!brush_active) return 0;
    if (size < 2 || data[0] != 'J') return 0;

    switch (data[1]) {

    /* ---- 移动: JL/JR/JU/JD (霍尔相对) ---- */
    case 'L':
    case 'R':
    case 'U':
    case 'D':
        {
            /* 霍尔视角坐标系: 前=(cosθ, -sinθ), 左=(sinθ, cosθ) */
            int16_t cv = VolumeMath_Cos(brush_angle_offset);
            int16_t sv = VolumeMath_Sin(brush_angle_offset);
            int fdx = (cv >  16384) ? 1 : ((cv < -16384) ? -1 : 0);
            int fdy = (-sv >  16384) ? 1 : ((-sv < -16384) ? -1 : 0);
            int ldx = (sv >  16384) ? 1 : ((sv < -16384) ? -1 : 0);
            int ldy = (cv >  16384) ? 1 : ((cv < -16384) ? -1 : 0);

            if (brush_btn_held) {
                /* Z 轴模式: U=上(z+), D=下(z-), L/R=XY旋转 */
                if (data[1] == 'U' && brush_z + BRUSH_SIZE < BRUSH_VOLUME_Z)
                    brush_z++;
                else if (data[1] == 'D' && brush_z > 0)
                    brush_z--;
                else if (data[1] == 'L') {
                    if (ldx > 0 && brush_x + BRUSH_SIZE < BRUSH_VOLUME_X) brush_x++;
                    if (ldx < 0 && brush_x > 0) brush_x--;
                    if (ldy > 0 && brush_y + BRUSH_SIZE < BRUSH_VOLUME_Y) brush_y++;
                    if (ldy < 0 && brush_y > 0) brush_y--;
                } else if (data[1] == 'R') {
                    if (ldx > 0 && brush_x > 0) brush_x--;
                    if (ldx < 0 && brush_x + BRUSH_SIZE < BRUSH_VOLUME_X) brush_x++;
                    if (ldy > 0 && brush_y > 0) brush_y--;
                    if (ldy < 0 && brush_y + BRUSH_SIZE < BRUSH_VOLUME_Y) brush_y++;
                }
            } else {
                /* XY 模式: U=前(远离轴), D=后(靠近轴), L=左, R=右 */
                if (data[1] == 'U') {
                    if (fdx > 0 && brush_x + BRUSH_SIZE < BRUSH_VOLUME_X) brush_x++;
                    if (fdx < 0 && brush_x > 0) brush_x--;
                    if (fdy > 0 && brush_y + BRUSH_SIZE < BRUSH_VOLUME_Y) brush_y++;
                    if (fdy < 0 && brush_y > 0) brush_y--;
                } else if (data[1] == 'D') {
                    if (fdx > 0 && brush_x > 0) brush_x--;
                    if (fdx < 0 && brush_x + BRUSH_SIZE < BRUSH_VOLUME_X) brush_x++;
                    if (fdy > 0 && brush_y > 0) brush_y--;
                    if (fdy < 0 && brush_y + BRUSH_SIZE < BRUSH_VOLUME_Y) brush_y++;
                } else if (data[1] == 'L') {
                    if (ldx > 0 && brush_x + BRUSH_SIZE < BRUSH_VOLUME_X) brush_x++;
                    if (ldx < 0 && brush_x > 0) brush_x--;
                    if (ldy > 0 && brush_y + BRUSH_SIZE < BRUSH_VOLUME_Y) brush_y++;
                    if (ldy < 0 && brush_y > 0) brush_y--;
                } else if (data[1] == 'R') {
                    if (ldx > 0 && brush_x > 0) brush_x--;
                    if (ldx < 0 && brush_x + BRUSH_SIZE < BRUSH_VOLUME_X) brush_x++;
                    if (ldy > 0 && brush_y > 0) brush_y--;
                    if (ldy < 0 && brush_y + BRUSH_SIZE < BRUSH_VOLUME_Y) brush_y++;
                }
            }
        }
        return 1;

    /* ---- 摇杆按钮: JB0(松开) / JB1(按下) ---- */
    case 'B':
        if (size >= 3) {
            if (data[2] == '0') { brush_btn_held = 0; return 1; }
            if (data[2] == '1') { brush_btn_held = 1; return 1; }
        }
        return 0;

    /* ---- 视角偏移: JH<n> (0-99相位) ---- */
    case 'H':
        if (size >= 4 && data[2] >= '0' && data[2] <= '9') {
            uint16_t val = 0;
            uint16_t i = 2;
            while (i < size && data[i] >= '0' && data[i] <= '9')
                val = (uint16_t)(val * 10 + (data[i++] - '0'));
            brush_angle_offset = val % 100;
        }
        return 1;

    /* ---- 颜色切换: JK ---- */
    case 'K':
        Brush_CycleColor();
        return 1;

    /* ---- 画笔切换: JP ---- */
    case 'P':
        brush_drawing = !brush_drawing;
        return 1;

    /* ---- 退出: JE (同 IS) ---- */
    case 'E':
        Brush_Deactivate();
        return 1;

    default:
        return 0;
    }
}

/* ========================================================================== */
/* 诊断 */
/* ========================================================================== */

uint16_t Brush_GetDiag(char *buf, uint16_t max_len)
{
    int len;
    if (max_len < 128) return 0;

    len = snprintf(buf, max_len,
        "BX:%u BY:%u BZ:%u A:%u CL:%u BTN:%u DR:%u US:%lu SWP:%lu VOX:%lu",
        (unsigned)brush_x, (unsigned)brush_y, (unsigned)brush_z,
        (unsigned)brush_angle_offset,
        (unsigned)brush_color_index,
        (unsigned)brush_btn_held, (unsigned)brush_drawing,
        (unsigned long)brush_expand_us,
        (unsigned long)brush_diag_swaps,
        (unsigned long)brush_diag_voxels);

    return (uint16_t)len;
}
