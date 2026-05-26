/**
 * snake_game.c -- 3D 贪吃蛇游戏
 *
 * 架构: 蛇身+食物→体素→50面采样→state (后50面 __RBIT)
 *
 * 操作:
 *   JG       = 开始新游戏
 *   JL/JR    = 左转/右转 (XY面内旋转)
 *   JU/JD    = 上(Z+)/下(Z-) 或 返回XY面
 *   JE       = 退出
 */

#include "snake_game.h"
#include "ws2812_driver.h"
#include "volume_buffer.h"
#include "volume_math.h"
#include "led_buffer.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================== */
/* 常量 */
/* ========================================================================== */

#define SNAKE_SLICE_BYTES   2304
#define SNAKE_HALF_SLICES   50
#define SNAKE_BUF_BYTES     (SNAKE_HALF_SLICES * SNAKE_SLICE_BYTES)

#define VOL_X  32
#define VOL_Y  32
#define VOL_Z  24

#define SNAKE_MAX_LEN   200     /* 环缓冲最大容量 */
#define SNAKE_INIT_LEN  5       /* 初始长度 */
#define FOOD_MAX        10      /* 最多食物数 */
#define FOOD_MIN        5       /* 最少保持食物数 */
#define FOOD_INIT_XY    3       /* 初始至少3个在XY面(z=12附近) */
#define STEP_DIVIDER    2       /* 每N个霍尔走一步 */

#define SNAKE_INIT_X  16
#define SNAKE_INIT_Y  16
#define SNAKE_INIT_Z  12
#define SNAKE_CELL     2   /* 单元大小: 2×2×2 */
#define SNAKE_STEP     2   /* 每步移动 2 格 */

/* 颜色 (R+G+B≤30) */
#define SNAKE_HEAD_R  30
#define SNAKE_HEAD_G  30
#define SNAKE_HEAD_B   0   /* 黄色蛇头 */
#define SNAKE_BODY_R   0
#define SNAKE_BODY_G  30
#define SNAKE_BODY_B   0   /* 绿色蛇身 */
#define FOOD_R        30
#define FOOD_G         0
#define FOOD_B         0   /* 红色食物 */

/* ========================================================================== */
/* 方向定义 */
/* ========================================================================== */

enum { DIR_PX, DIR_NX, DIR_PY, DIR_NY, DIR_PZ, DIR_NZ };

/* 方向向量: dx, dy, dz */
static const int8_t dir_vec[6][3] = {
    { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
};

/*
 * 转向查表: [当前方向][操作]
 *   操作索引: 0=JL(左), 1=JR(右), 2=JU(上), 3=JD(下)
 *
 *   XY方向: JL/JR=XY面90°旋转, JU=+Z, JD=-Z
 *   Z 方向: JL/JR=固定XY输出, JU/JD=返回XY面
 */
static const int8_t turn_table[6][4] = {
    /*         JL       JR       JU       JD    */
    /* +X */ { DIR_PY,  DIR_NY,  DIR_PZ,  DIR_NZ },
    /* -X */ { DIR_NY,  DIR_PY,  DIR_PZ,  DIR_NZ },
    /* +Y */ { DIR_NX,  DIR_PX,  DIR_PZ,  DIR_NZ },
    /* -Y */ { DIR_PX,  DIR_NX,  DIR_PZ,  DIR_NZ },
    /* +Z */ { DIR_PY,  DIR_NY,  DIR_PX,  DIR_NX },
    /* -Z */ { DIR_PY,  DIR_NY,  DIR_NX,  DIR_PX },
};

/* ========================================================================== */
/* 蛇身数组 (线性, 头在 [0], 尾在 [len-1]) */
/* ========================================================================== */

typedef struct { int16_t x, y, z; } SnakeSeg;

static SnakeSeg snake_body[SNAKE_MAX_LEN];
static uint16_t snake_len;        /* 当前长度 */
static uint8_t  snake_dir;        /* 当前方向 (0-5) */
static uint8_t  snake_last_xy;    /* 进入Z前最后的XY方向 */

/* ========================================================================== */
/* 食物 */
/* ========================================================================== */

static SnakeSeg foods[FOOD_MAX];
static uint8_t  food_count;
static uint16_t foods_eaten;

/* ========================================================================== */
/* 游戏状态 */
/* ========================================================================== */

static volatile uint8_t snake_active;
static volatile uint8_t snake_alive;         /* ISR+主循环访问 */
static volatile uint8_t snake_step_counter;  /* ISR+主循环访问 */
static uint16_t snake_high_score;

/* 渲染 */
extern uint8_t g_display_buf[];
static uint8_t *snake_front_buf;
static uint8_t *snake_back_buf;
static uint8_t snake_rev[SNAKE_SLICE_BYTES]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

static volatile uint8_t snake_phase;
static volatile uint8_t snake_hall;
static volatile uint8_t snake_swap_pending;
static volatile uint8_t snake_back_ready;
static uint32_t snake_expand_us;
static uint32_t snake_swap_count;
static int16_t snake_last_hx, snake_last_hy, snake_last_hz; /* 调试 */
static uint16_t snake_diag_hits;  /* 碰撞检测触发次数 */

static volatile uint32_t snake_diag_render;
static volatile uint32_t snake_diag_hall;
static volatile uint32_t snake_diag_steps;
static volatile uint32_t snake_diag_gen;

/* ========================================================================== */
/* 辅助 */
/* ========================================================================== */

static int rng_state;
static int simple_rand(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}

/* 2×2×2 盒重叠检测: body[1]..body[len-2], 不含头和尾 */
static int is_on_snake_body(int16_t x, int16_t y, int16_t z) {
    uint16_t i;
    for (i = 1; i + 1 < snake_len; i++) {
        if (abs((int)snake_body[i].x - (int)x) < SNAKE_CELL &&
            abs((int)snake_body[i].y - (int)y) < SNAKE_CELL &&
            abs((int)snake_body[i].z - (int)z) < SNAKE_CELL) return 1;
    }
    return 0;
}

/* 蛇移动: 头进尾出 */
static void snake_move(SnakeSeg new_head) {
    uint16_t i;
    /* 右移腾出 [0] 给新头 */
    for (i = snake_len; i > 0; i--)
        snake_body[i] = snake_body[i - 1];
    snake_body[0] = new_head;
    snake_len++;
}

static void snake_pop_tail(void) {
    if (snake_len > 0) snake_len--;
}

/* ========================================================================== */
/* 食物生成 */
/* ========================================================================== */

static void spawn_food(uint8_t idx, int prefer_xy) {
    int attempts = 0;
    while (attempts < 1000) {
        int16_t x, y, z;
        if (prefer_xy && attempts < 500) {
            x = (int16_t)(simple_rand() % VOL_X);
            y = (int16_t)(simple_rand() % VOL_Y);
            z = (int16_t)(SNAKE_INIT_Z + (simple_rand() % 5) - 2);
            if (z < 0) z = 0; if (z >= VOL_Z) z = (int16_t)(VOL_Z - 1);
        } else {
            x = (int16_t)(simple_rand() % VOL_X);
            y = (int16_t)(simple_rand() % VOL_Y);
            z = (int16_t)(simple_rand() % VOL_Z);
        }
        {
            int ov_hd = abs((int)snake_body[0].x - (int)x) < SNAKE_CELL
                     && abs((int)snake_body[0].y - (int)y) < SNAKE_CELL
                     && abs((int)snake_body[0].z - (int)z) < SNAKE_CELL;
            int ov_tl = abs((int)snake_body[snake_len-1].x - (int)x) < SNAKE_CELL
                     && abs((int)snake_body[snake_len-1].y - (int)y) < SNAKE_CELL
                     && abs((int)snake_body[snake_len-1].z - (int)z) < SNAKE_CELL;
            if (!is_on_snake_body(x, y, z) && !ov_hd && !ov_tl) {
            foods[idx].x = x;
            foods[idx].y = y;
            foods[idx].z = z;
            return;
            } /* !ov */
        }
        attempts++;
    }
    /* fallback: just place anywhere */
    foods[idx].x = (int16_t)(simple_rand() % VOL_X);
    foods[idx].y = (int16_t)(simple_rand() % VOL_Y);
    foods[idx].z = (int16_t)(simple_rand() % VOL_Z);
}

static void spawn_all_food(void) {
    uint8_t i;
    for (i = 0; i < FOOD_MAX; i++)
        spawn_food(i, (i < FOOD_INIT_XY));
    food_count = FOOD_MAX;
}

static void refill_food(void) {
    while (food_count < FOOD_MIN) {
        spawn_food(food_count, 0);
        food_count++;
    }
}

/* ========================================================================== */
/* 蛇初始化 */
/* ========================================================================== */

static void snake_reset(void) {
    int i;
    snake_len = SNAKE_INIT_LEN;
    snake_dir = DIR_PX;
    snake_last_xy = DIR_PX;
    snake_step_counter = 0;
    snake_alive = 1;

    /* 蛇身: body[0]=头(16,16,12), body[4]=尾(12,16,12), 方向+X */
    for (i = 0; i < SNAKE_INIT_LEN; i++) {
        snake_body[i].x = (int16_t)(SNAKE_INIT_X - i);
        snake_body[i].y = SNAKE_INIT_Y;
        snake_body[i].z = SNAKE_INIT_Z;
    }

    foods_eaten = 0;
    snake_diag_hits = 0;
    snake_alive = 1;
    spawn_all_food();
}

/* ========================================================================== */
/* 体素绘制 + 50面生成 */
/* ========================================================================== */

static void led_to_state(uint8_t *state_out) {
    uint32_t *wp = (uint32_t *)state_out;
    uint8_t led, strip;
    for (led = 0; led < 24; led++) {
        uint32_t *base = wp + (uint32_t)led * 24;
        for (strip = 0; strip < 32; strip++) {
            LED_Color_t *c = &g_ledBuffer[strip][led];
            uint32_t grb = ((uint32_t)c->g << 16) | ((uint32_t)c->r << 8) | (uint32_t)c->b;
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

static inline int32_t q15_mul_round(int32_t a, int16_t b) {
    int32_t prod = a * (int32_t)b;
    return (prod >= 0) ? ((prod + 32768) >> 16) : -((-prod + 32768) >> 16);
}

static void snake_expand(uint8_t *state) {
    uint32_t t0 = DWT->CYCCNT;
    uint8_t strip;
    uint16_t phase, i;
    int dx, dy, dz;

    /* ---- 1. 清体素 ---- */
    VolumeBuffer_Clear();
    LED_SetGlobalBrightness(255);

    /* ---- 2. 画食物 (2×2×2 红色) ---- */
    for (i = 0; i < food_count; i++)
        for (dz = 0; dz < SNAKE_CELL; dz++)
        for (dy = 0; dy < SNAKE_CELL; dy++)
        for (dx = 0; dx < SNAKE_CELL; dx++)
            VolumeBuffer_SetVoxel(
                foods[i].x + dx, foods[i].y + dy, foods[i].z + dz,
                FOOD_R, FOOD_G, FOOD_B);

    /* ---- 3. 画蛇身 (2×2×2 块: 绿身 + 黄头) ---- */
    for (i = 1; i < snake_len; i++)
        for (dz = 0; dz < SNAKE_CELL; dz++)
        for (dy = 0; dy < SNAKE_CELL; dy++)
        for (dx = 0; dx < SNAKE_CELL; dx++)
            VolumeBuffer_SetVoxel(
                snake_body[i].x + dx, snake_body[i].y + dy, snake_body[i].z + dz,
                SNAKE_BODY_R, SNAKE_BODY_G, SNAKE_BODY_B);
    /* 头 */
    for (dz = 0; dz < SNAKE_CELL; dz++)
    for (dy = 0; dy < SNAKE_CELL; dy++)
    for (dx = 0; dx < SNAKE_CELL; dx++)
        VolumeBuffer_SetVoxel(
            snake_body[0].x + dx, snake_body[0].y + dy, snake_body[0].z + dz,
            SNAKE_HEAD_R, SNAKE_HEAD_G, SNAKE_HEAD_B);

    /* ---- 4. 50面采样 ---- */
    memset(state, 0, SNAKE_BUF_BYTES);
    for (phase = 0; phase < SNAKE_HALF_SLICES; phase++) {
        int16_t cv = VolumeMath_Cos(phase);
        int16_t sv = VolumeMath_Sin(phase);
        for (strip = 0; strip < 32; strip++) {
            int32_t dx_i = (int32_t)strip * 2 - 31;
            int x = 16 + (int)q15_mul_round(dx_i, cv);
            int y = 16 - (int)q15_mul_round(dx_i, sv);
            if (x < 0) x = 0; if (x > 31) x = 31;
            if (y < 0) y = 0; if (y > 31) y = 31;
            VolumeBuffer_ReadColumn(x, y, &g_ledBuffer[strip][0]);
        }
        led_to_state(state + (uint32_t)phase * SNAKE_SLICE_BYTES);
    }
    snake_diag_gen++;
    snake_expand_us = (DWT->CYCCNT - t0) / 480U;
}

/* ========================================================================== */
/* 公开 API */
/* ========================================================================== */

void Snake_Init(void) {
    snake_front_buf = g_display_buf;
    snake_back_buf  = g_display_buf + SNAKE_BUF_BYTES;
    snake_phase = 0; snake_hall = 0;
    snake_swap_pending = 0; snake_back_ready = 0;
    snake_swap_count = 0;
    snake_active = 0; snake_alive = 0;
    snake_high_score = 0;
    rng_state = 12345;
    snake_reset();
    snake_active = 0;
}

void Snake_Activate(void) {
    if (snake_active) return;
    __HAL_TIM_DISABLE_IT(&htim3, TIM_IT_UPDATE);
    snake_reset();
    snake_expand(snake_front_buf);
    memcpy(snake_back_buf, snake_front_buf, SNAKE_BUF_BYTES);
    snake_back_ready = 1;
    snake_active = 1;
    __HAL_TIM_ENABLE_IT(&htim3, TIM_IT_UPDATE);
}

void Snake_Deactivate(void) { snake_active = 0; }
uint8_t Snake_IsActive(void) { return snake_active; }

/* ========================================================================== */
/* 主循环 */
/* ========================================================================== */

void Snake_Update(void) {
    if (!snake_active) return;

    /* 换帧 */
    if (snake_swap_pending && snake_back_ready) {
        uint8_t *tmp;
        snake_swap_pending = 0;
        tmp = snake_front_buf; snake_front_buf = snake_back_buf;
        snake_back_buf = tmp;
        snake_back_ready = 0;
        snake_swap_count++;

        /* 步进分频: 计数器无条件涨, 活蛇才移动 */
        if (++snake_step_counter >= STEP_DIVIDER) {
            snake_step_counter = 0;

            /* 计算新蛇头位置 (body[0]=当前头), 穿墙环绕 */
            SnakeSeg new_head = snake_body[0];
            new_head.x = (int16_t)(new_head.x + dir_vec[snake_dir][0] * SNAKE_STEP);
            new_head.y = (int16_t)(new_head.y + dir_vec[snake_dir][1] * SNAKE_STEP);
            new_head.z = (int16_t)(new_head.z + dir_vec[snake_dir][2] * SNAKE_STEP);
            new_head.x = (int16_t)((new_head.x + VOL_X) % VOL_X);
            new_head.y = (int16_t)((new_head.y + VOL_Y) % VOL_Y);
            new_head.z = (int16_t)((new_head.z + VOL_Z) % VOL_Z);
            snake_last_hx = new_head.x;
            snake_last_hy = new_head.y;
            snake_last_hz = new_head.z;

            /* 撞自己: body[1]..body[len-2] */
            if (is_on_snake_body(new_head.x, new_head.y, new_head.z)) {
                snake_diag_hits++;
                snake_alive = 0;
                if (foods_eaten > snake_high_score)
                    snake_high_score = foods_eaten;
                return;
            }

            snake_alive = 1;
            snake_move(new_head);
            {
                uint8_t fi; int ate = 0;
                for (fi = 0; fi < food_count; fi++) {
                    if (abs((int)foods[fi].x - (int)new_head.x) < SNAKE_CELL &&
                        abs((int)foods[fi].y - (int)new_head.y) < SNAKE_CELL &&
                        abs((int)foods[fi].z - (int)new_head.z) < SNAKE_CELL) {
                        foods[fi] = foods[food_count - 1];
                        food_count--; foods_eaten++; ate = 1; break;
                    }
                }
                if (!ate) snake_pop_tail();
                refill_food();
            }
            snake_diag_steps++;
        }
    }

    /* 生成下一帧 */
    if (!snake_back_ready) {
        snake_expand(snake_back_buf);
        snake_back_ready = 1;
    }
}

/* ========================================================================== */
/* 渲染 (TIM3 ISR) */
/* ========================================================================== */

static void Snake_RenderPhase(uint8_t phase) {
    snake_diag_render++;
    if (phase < SNAKE_HALF_SLICES) {
        WS2812_ShowFromSlice(&snake_front_buf[(uint32_t)phase * SNAKE_SLICE_BYTES]);
    } else {
        uint8_t slot = phase - SNAKE_HALF_SLICES;
        const uint32_t *fwd = (const uint32_t *)(snake_front_buf + (uint32_t)slot * SNAKE_SLICE_BYTES);
        uint32_t *rp = (uint32_t *)snake_rev;
        uint16_t i;
        for (i = 0; i < 576U; i++) rp[i] = __RBIT(fwd[i]);
        WS2812_ShowFromSlice(snake_rev);
    }
}

void Snake_OnHallEdge(void) { snake_hall = 1; snake_diag_hall++; }

uint8_t Snake_RenderNext(void) {
    if (snake_hall) {
        snake_hall = 0; snake_phase = 0; snake_swap_pending = 1;
    }
    Snake_RenderPhase(snake_phase);
    snake_phase++;
    if (snake_phase >= 100) snake_phase = 0;
    return snake_phase;
}

/* ========================================================================== */
/* BLE 指令 */
/* ========================================================================== */

uint8_t Snake_HandleJoy(const uint8_t *data, uint16_t size) {
    if (!snake_active) return 0;
    if (size < 2 || data[0] != 'J') return 0;

    switch (data[1]) {
    case 'G': /* 开始新游戏 */
        snake_reset();
        snake_step_counter = (uint8_t)(STEP_DIVIDER - 1); /* 下个霍尔立即走第一步 */
        snake_back_ready = 0;  /* 强制刷新显示 */
        return 1;

    case 'L': case 'R': case 'U': case 'D':
        if (snake_alive) {
            uint8_t op = (uint8_t)((data[1] == 'L') ? 0 :
                          (data[1] == 'R') ? 1 :
                          (data[1] == 'U') ? 2 : 3);
            uint8_t old_dir = snake_dir;
            uint8_t new_dir = (uint8_t)turn_table[old_dir][op];
            /* 不允许反向 (会立即撞自己) */
            if (dir_vec[new_dir][0] != -dir_vec[old_dir][0] ||
                dir_vec[new_dir][1] != -dir_vec[old_dir][1] ||
                dir_vec[new_dir][2] != -dir_vec[old_dir][2]) {
                /* 记住进入Z前的XY方向 */
                if (old_dir < DIR_PZ && (new_dir == DIR_PZ || new_dir == DIR_NZ))
                    snake_last_xy = old_dir;
                snake_dir = new_dir;
            }
        }
        return 1;

    case 'E':
        Snake_Deactivate();
        return 1;

    default: return 0;
    }
}

/* ========================================================================== */
/* 诊断 + 分数 */
/* ========================================================================== */

uint16_t Snake_GetDiag(char *buf, uint16_t max_len) {
    int len;
    /* NOTE: snake_len grows up to SNAKE_MAX_LEN via snake_move. */
    if (max_len < 128) return 0;

    len = snprintf(buf, max_len,
        "SCORE:%u HIGH:%u STATE:%s LEN:%u DIR:%u H:(%d,%d,%d) HIT:%u SP:%lu SC:%u US:%lu",
        (unsigned)foods_eaten,
        (unsigned)snake_high_score,
        snake_alive ? "PLAY" : "DEAD",
        (unsigned)snake_len,
        (unsigned)snake_dir,
        (int)snake_last_hx, (int)snake_last_hy, (int)snake_last_hz,
        (unsigned)snake_diag_hits,
        (unsigned long)snake_diag_steps,
        (unsigned)snake_step_counter,
        (unsigned long)snake_expand_us);

    return (uint16_t)len;
}
