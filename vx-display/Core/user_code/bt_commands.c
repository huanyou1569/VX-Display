/**
 * bt_commands.c  —— 蓝牙指令处理实现
 *
 * 命令入口: BT_ProcessCommand() 由 UART 空闲中断回调调用
 * 轮询:     BT_PollVoxelReady() 在主循环中调用
 */

#include "bt_commands.h"
#include "sd_animation.h"
#include "slice_player.h"
#include "anim_player.h"
#include "anim_player_v2.h"
#include "anim_player_v3.h"
#include "volume_hall.h"
#include "motor_control.h"
#include "fatfs.h"
#include <string.h>
#include <stdio.h>

/* ==========================================================================
 * 缓冲区 (AXI SRAM, DMA 可访问)
 * ========================================================================== */

/* 回复缓冲区 */
static uint8_t  bt_resp[256]   __attribute__((section(".RAM_AXI"), used));
static uint16_t bt_resp_size   = 0;
static uint8_t  bt_resp_ready  = 0;

/* 体素数据接收缓冲区 (文件头 + 一帧) */
static uint8_t bt_voxel_buf[VXAN_HEADER_SIZE + VXAN_FRAME_SIZE]
    __attribute__((section(".RAM_AXI"), used));

#define BT_SAVE_CHUNK  8192U   /* 每次 f_write 写入 8KB，与扇区对齐 */

/*
 * 文件接收缓冲 —— 由 main.c 分配在 RAM_D2。
 * 与 anim_player 后台缓冲共享同一块内存 (互斥: :SAVE 和动画不同时发生)。
 */
extern uint8_t g_ramd2_buf[];
#define bt_file_buf g_ramd2_buf

/*
 * f_write 中转缓冲 —— 必须在 AXI SRAM。
 * SDMMC IDMA 不能从 RAM_D2 读数据（BSP_SD_WriteBlocks_DMA 超时 30s），
 * 但可以从 AXI SRAM 读（所有已验证的 f_read 均使用 AXI SRAM 缓冲）。
 * 每块 8KB，刚好对齐 16 个 SD 扇区。
 */
static uint8_t bt_write_buf[BT_SAVE_CHUNK]
    __attribute__((section(".RAM_AXI"), used, aligned(32)));

/* 文件保存用 FIL —— 必须在 AXI SRAM (非缓存)。
 * 栈上 FIL (DTCMRAM, 可缓存) 会与 SDMMC IDMA 的 FAT/目录扇区读写产生
 * D-Cache 行共享，导致 FIL 状态损坏 → 写入错误簇 → 文件 0KB。 */
static FIL  bt_save_file __attribute__((section(".RAM_AXI"), used));
static FIL *bt_save_fp = &bt_save_file;

/* ==========================================================================
 * 状态
 * ========================================================================== */

static volatile uint8_t bt_receiving   = 0;   /* 正在二进制接收 */
static volatile uint8_t bt_data_ready  = 0;   /* 体素数据接收完毕待处理 */
static uint32_t         bt_data_size   = 0;   /* 期望的体素数据长度 */

/* 文件保存状态 */
static volatile uint8_t bt_save_receiving  = 0;  /* 正在接收文件数据 */
static volatile uint8_t bt_save_data_ready = 0;  /* 文件数据收完，待写入TF卡 */
static uint32_t         bt_save_size       = 0;  /* 期望的文件数据长度 */
static uint32_t         bt_save_offset     = 0;  /* 已接收的字节数 */
static char             bt_save_filename[64];     /* 保存目标文件名 */

/* 延迟命令队列 —— 避免在 ISR 中执行阻塞的 SD 卡操作 */
typedef enum {
    CMD_NONE = 0,
    CMD_LOAD,
    CMD_LIST,
    CMD_SAVE,
} bt_cmd_type_t;

static volatile bt_cmd_type_t bt_cmd_pending = CMD_NONE;
static char    bt_cmd_arg[64];                      /* :LOAD 的文件名 */
static uint8_t bt_cmd_arg_len;

/* ==========================================================================
 * 小辅助
 * ========================================================================== */

static uint32_t parse_u32(const uint8_t *s, uint16_t max_len)
{
    uint32_t val = 0;
    uint16_t i;
    for (i = 0; i < max_len && s[i] >= '0' && s[i] <= '9'; i++) {
        val = val * 10U + (uint32_t)(s[i] - '0');
    }
    return val;
}

/* ==========================================================================
 * 回复管理
 * ========================================================================== */

static void set_response(const char *str)
{
    uint16_t len = (uint16_t)strlen(str);
    if (len > sizeof(bt_resp) - 1) len = sizeof(bt_resp) - 1;
    memcpy(bt_resp, str, len);
    bt_resp_size  = len;
    bt_resp_ready = 1;
}

static void set_response_ok(void)    { set_response("OK\n"); }
static void set_response_err(void)   { set_response("ERR\n"); }

uint8_t BT_HasResponse(void)         { return bt_resp_ready; }
const uint8_t *BT_GetResponse(void)  { return bt_resp; }
uint16_t BT_GetResponseSize(void)    { return bt_resp_size; }
void BT_ClearResponse(void)          { bt_resp_ready = 0; bt_resp_size = 0; }
uint8_t BT_IsReceiving(void)         { return bt_receiving; }
uint8_t BT_IsSaveReceiving(void)     { return bt_save_receiving; }

int32_t  BT_GetSaveProgress(void)    { return bt_save_receiving ? (int32_t)bt_save_offset : -1; }
uint32_t BT_GetSaveTotal(void)       { return bt_save_size; }

/**
 * 由 UART 空闲中断回调调用 —— 将收到的片段追加到文件接收缓冲。
 * 累积满 bt_save_size 字节后自动置 bt_save_data_ready。
 */
void BT_FeedFileData(const uint8_t *data, uint16_t size)
{
    if (size == 0) return;
    if (bt_save_offset + size > bt_save_size) {
        /* 超过预期长度：截断 */
        size = (uint16_t)(bt_save_size - bt_save_offset);
    }
    if (size == 0) return;

    memcpy(&bt_file_buf[bt_save_offset], data, size);
    bt_save_offset += size;

    if (bt_save_offset >= bt_save_size) {
        bt_save_receiving  = 0;
        bt_save_data_ready = 1;
    }
}

/* ==========================================================================
 * 二进制接收 —— :DATA <size>
 * ========================================================================== */

static void bt_start_data_receive(uint8_t *buf, uint32_t buf_cap, uint32_t size)
{
    HAL_StatusTypeDef st;

    if (size > buf_cap || size == 0) {
        set_response("ERR:bad size\n");
        return;
    }

    bt_data_size = size;
    bt_receiving = 1;

    /* 停止旧的空闲 DMA 接收 */
    (void)HAL_UART_DMAStop(&huart4);

    /*
     * 清除所有挂起的 UART 状态标志，防止旧中断在新 DMA
     * 传输期间误触发（尤其是 IDLE / ORE / FE / NE）。
     */
    __HAL_UART_CLEAR_IDLEFLAG(&huart4);
    __HAL_UART_CLEAR_OREFLAG(&huart4);
    __HAL_UART_CLEAR_FEFLAG(&huart4);
    __HAL_UART_CLEAR_NEFLAG(&huart4);
    __HAL_UART_CLEAR_PEFLAG(&huart4);

    /*
     * 清空接收缓冲的 D-Cache，保证 DMA 写入的
     * 数据不会被脏缓存回写覆盖。
     */
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)buf, (int32_t)buf_cap);
    __DSB(); __ISB();

    st = HAL_UART_Receive_DMA(&huart4, buf, (uint16_t)size);
    if (st != HAL_OK) {
        bt_receiving = 0;
        set_response("ERR:DMA start failed\n");
        return;
    }

    /*
     * 将 ReceptionType 从 TOIDLE 改为 STANDARD，
     * 阻止 HAL_UART_IRQHandler 在 RxEventCallback 返回后
     * 自动重启空闲 DMA（覆盖我们刚设好的固定长度 DMA）。
     */
    huart4.ReceptionType = HAL_UART_RECEPTION_STANDARD;
}

/**
 * 由 HAL_UART_RxCpltCallback 调用 —— 固定长度DMA接收完成。
 */
void BT_OnRxComplete(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART4) return;
    if (!bt_receiving)            return;

    bt_receiving  = 0;

    /* 体素数据接收完成 (:DATA 固定长度 DMA) */
    bt_data_ready = 1;

    SCB_InvalidateDCache_by_Addr((uint32_t *)bt_voxel_buf,
                                 sizeof(bt_voxel_buf));
    __DSB(); __ISB();

    set_response_ok();
}

/* ==========================================================================
 * 轮询 —— 由主循环调用
 * ========================================================================== */

void BT_PollVoxelReady(void)
{
    int ret;

    if (!bt_data_ready) return;
    bt_data_ready = 0;

    ret = SD_ApplyVoxelData(bt_voxel_buf, bt_data_size);
    if (ret == 0) {
        set_response("VOX:OK\n");
    } else {
        char buf[16];
        int n = snprintf(buf, sizeof(buf), "VOX:ERR=%d\n", ret);
        if (n > 0 && n < (int)sizeof(buf))
            set_response(buf);
        else
            set_response("VOX:ERR\n");
    }
}

/**
 * 主循环轮询 —— 文件数据DMA收完后写入TF卡。
 */
void BT_PollSaveFile(void)
{
    FRESULT fr;
    UINT    bw;
    char    path[80];
    uint32_t remaining;
    uint32_t offset;

    if (!bt_save_data_ready) return;
    bt_save_data_ready = 0;

    if (bt_save_size == 0 || bt_save_size > BT_FILE_BUF_SIZE) {
        set_response("ERR:bad save size\n");
        return;
    }

    /* 构建完整路径 */
    {
        int n = snprintf(path, sizeof(path), "0:/%s", bt_save_filename);
        if (n < 0 || n >= (int)sizeof(path)) {
            set_response("ERR:path too long\n");
            return;
        }
    }

    /* 挂载 SD */
    fr = f_mount(&SDFatFS, SDPath, 1);
    if (fr != FR_OK) {
        set_response("ERR:SD mount failed\n");
        return;
    }

    /*
     * FIL 必须使用 bt_save_fp (AXI SRAM, 非缓存)，禁止栈上 FIL。
     * 理由同 SD_LoadStaticModel / SlicePlayer_Load:
     * f_write 内部通过 SDMMC IDMA 读写 FAT/目录扇区，
     * 若 FIL 在可缓存的 DTCMRAM 栈上，D-Cache 与 IDMA 之间的
     * 缓存行共享会导致 FIL 状态损坏 → 写入错误簇 → f_write 失败。
     */
    memset(bt_save_fp, 0, sizeof(FIL));
    fr = f_open(bt_save_fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        set_response("ERR:file open failed\n");
        return;
    }

    /*
     * 分块中转写入：bt_file_buf(RAM_D2) → bt_write_buf(AXI SRAM) → f_write。
     * SDMMC IDMA 无法从 RAM_D2 读取（超时 30s），只能从 AXI SRAM 读取。
     * 每次 memcpy 8KB 到 bt_write_buf，再 f_write 写入 TF 卡。
     */
    remaining = bt_save_size;
    offset    = 0;

    while (remaining > 0) {
        UINT chunk = (remaining > BT_SAVE_CHUNK) ? BT_SAVE_CHUNK : (UINT)remaining;

        memcpy(bt_write_buf, &bt_file_buf[offset], chunk);
        __DSB();

        fr = f_write(bt_save_fp, bt_write_buf, chunk, &bw);
        if (fr != FR_OK || bw != chunk) {
            f_close(bt_save_fp);
            set_response("ERR:write failed\n");
            return;
        }
        offset    += chunk;
        remaining -= chunk;
    }

    /*
     * f_sync 确保所有 FAT / 目录 / 数据扇区均已写入 SD 卡。
     * 然后 f_close 释放文件对象。
     */
    fr = f_sync(bt_save_fp);
    if (fr != FR_OK) {
        f_close(bt_save_fp);
        set_response("ERR:sync failed\n");
        return;
    }

    fr = f_close(bt_save_fp);
    if (fr != FR_OK) {
        set_response("ERR:close failed\n");
        return;
    }

    {
        char buf[40];
        int n = snprintf(buf, sizeof(buf), "SAVED %lu OK\n",
                         (unsigned long)bt_save_size);
        if (n > 0 && n < (int)sizeof(buf))
            set_response(buf);
        else
            set_response("SAVED OK\n");
    }
}

/**
 * 主循环轮询 —— 执行延迟的 SD 卡指令。
 * 这些操作不可在 UART 中断回调中执行（会阻塞等 SDMMC DMA 完成，
 * 而 SDMMC 中断优先级不高于 UART，导致死锁）。
 */
void BT_PollCommands(void)
{
    switch (bt_cmd_pending) {
    case CMD_NONE:
        return;

    case CMD_LIST: {
        DIR     dir;
        FILINFO fno;
        FRESULT fr;
        char    buf[256];
        int     pos = 0;
        int     cnt = 0;

        bt_cmd_pending = CMD_NONE;

        fr = f_mount(&SDFatFS, SDPath, 1);
        if (fr != FR_OK) {
            set_response("ERR:SD mount failed\n");
            return;
        }
        fr = f_opendir(&dir, "/");
        if (fr != FR_OK) {
            set_response("ERR:open dir failed\n");
            return;
        }
        buf[0] = '\0';
        while (1) {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == '\0') break;
            const char *name = fno.fname;
            size_t len = strlen(name);

            /* 匹配 .bin 或 .slices */
            int match_bin    = (len >= 4 && name[len-4]=='.' && name[len-3]=='b'
                                && name[len-2]=='i' && name[len-1]=='n');
            int match_slices = (len >= 7 && name[len-7]=='.' && name[len-6]=='s'
                                && name[len-5]=='l' && name[len-4]=='i'
                                && name[len-3]=='c' && name[len-2]=='e'
                                && name[len-1]=='s');
            if (!match_bin && !match_slices) continue;

            int remain = (int)sizeof(buf) - pos - 2;
            if (remain < 20) break;
            int n = snprintf(buf + pos, remain, "%s\n", name);
            if (n > 0) { pos += n; cnt++; }
        }
        f_closedir(&dir);
        if (cnt == 0)
            set_response("(no files)\n");
        else
            set_response(buf);
        return;
    }

    case CMD_LOAD: {
        int ret;
        bt_cmd_pending = CMD_NONE;

        /* 换文件前先关掉所有旧播放器 */
        AnimV3_Deactivate();
        AnimV2_Deactivate();
        AnimPlayer_Deactivate();
        SlicePlayer_SetActive(0);

        int arglen = (int)strlen(bt_cmd_arg);
        if (arglen >= 7 && strcmp(bt_cmd_arg + arglen - 7, ".slices") == 0) {
            /*
             * .slices 文件: 优先尝试 V2 1-bit动画, 再试 V1 动画, 最后回退静态。
             */
            ret = AnimV3_Load(bt_cmd_arg);
            if (ret == 0) {
                SlicePlayer_SetActive(0);
                set_response_ok();
            } else {
                ret = AnimV2_Load(bt_cmd_arg);
            if (ret == 0) {
                /* V2 1-bit动画加载成功 */
                SlicePlayer_SetActive(0);
                set_response_ok();
            } else {
                /* V2 失败, 尝试 V1 (format=0x02) */
                ret = AnimPlayer_Load(bt_cmd_arg);
                if (ret == 0) {
                    /* V1 多帧动画加载成功 */
                    SlicePlayer_SetActive(0);
                    set_response_ok();
                } else if (ret == -3) {
                    /* 单帧 .slices, 回退到静态切片 */
                    ret = SlicePlayer_Load(bt_cmd_arg);
                    if (ret == 0) {
                        SlicePlayer_SetActive(1);
                        set_response_ok();
                    } else {
                        set_response_err();
                    }
                } else {
                    set_response_err();
                }
            }
            }
        } else {
            ret = SD_LoadStaticModel(bt_cmd_arg);
            if (ret == 0) {
                SlicePlayer_SetActive(0);
                set_response_ok();
            } else {
                set_response_err();
            }
        }
        return;
    }

    case CMD_SAVE:
        bt_cmd_pending   = CMD_NONE;
        bt_save_offset    = 0;
        bt_save_receiving = 1;
        return;
    }
}

/* ==========================================================================
 * 指令解析
 * ========================================================================== */

uint8_t BT_ProcessCommand(const uint8_t *data, uint16_t size)
{
    if (size < 2 || data[0] != ':')
        return 0;   /* 不是指令 */

    /* ---- :STATUS ---- */
    if (size >= 7 && memcmp(data, ":STATUS", 7) == 0) {
        uint32_t period   = VolumeHall_GetPeriodUs();
        uint16_t rpm      = VolumeHall_GetRPM();
        uint32_t dly_raw  = (period > 0) ? period / 50U : 0U;
        uint8_t  full     = MotorControl_IsWindowFull();
        uint8_t  stable   = MotorControl_IsStable();
        char buf[64];
        int n = snprintf(buf, sizeof(buf),
                         "RPM:%u DLY:%lu F:%u S:%u\n",
                         rpm, dly_raw, full, stable);
        if (n > 0 && n < (int)sizeof(buf)) {
            memcpy(bt_resp, buf, (uint16_t)n);
            bt_resp_size  = (uint16_t)n;
        } else {
            memcpy(bt_resp, "ERR\n", 4);
            bt_resp_size  = 4;
        }
        bt_resp_ready = 1;
        return 1;
    }

    /* ---- :LIST ---- */
    if (size >= 5 && memcmp(data, ":LIST", 5) == 0) {
        bt_cmd_pending  = CMD_LIST;
        bt_cmd_arg_len  = 0;
        return 1;  /* 延迟到主循环执行，避免 ISR 中阻塞等 SD 卡 */
    }

    /* ---- :BRIGHT <pct> ---- */
    if (size >= 8 && memcmp(data, ":BRIGHT ", 8) == 0) {
        uint32_t val = parse_u32(data + 8, size - 8);
        if (val >= 1 && val <= 100) {
            AnimV2_SetBrightness((uint8_t)val);
            set_response_ok();
        } else {
            set_response("ERR:1-100\n");
        }
        return 1;
    }

    /* ---- :COLOR <r>,<g>,<b> ---- */
    if (size >= 7 && memcmp(data, ":COLOR ", 7) == 0) {
        uint16_t i = 7;
        int vals[3], n = 0;
        while (i < size && n < 3) {
            uint32_t v = parse_u32(data + i, size - i);
            if (v > 255) v = 255;
            vals[n++] = (int)v;
            while (i < size && data[i] >= '0' && data[i] <= '9') i++;
            if (i < size && (data[i] == ',' || data[i] == ' ')) i++;
        }
        if (n == 3) {
            AnimV2_SetColor((uint8_t)vals[0], (uint8_t)vals[1], (uint8_t)vals[2]);
            set_response_ok();
        } else {
            set_response("ERR:r,g,b\n");
        }
        return 1;
    }

    /* ---- :LOAD <filename> ---- */
    if (size >= 6 && memcmp(data, ":LOAD ", 6) == 0) {
        uint16_t i = 6;
        uint16_t j = 0;
        while (i < size && data[i] != '\n' && data[i] != '\r'
               && j < sizeof(bt_cmd_arg) - 1) {
            bt_cmd_arg[j++] = (char)data[i++];
        }
        bt_cmd_arg[j] = '\0';
        if (j == 0) {
            set_response("ERR:no filename\n");
            return 1;
        }
        bt_cmd_pending  = CMD_LOAD;
        bt_cmd_arg_len  = (uint8_t)j;
        return 1;  /* 延迟到主循环执行 */
    }

    /* ---- :DATA <size> ---- */
    if (size >= 6 && memcmp(data, ":DATA ", 6) == 0) {
        uint32_t val = parse_u32(data + 6, size - 6);
        bt_start_data_receive(bt_voxel_buf, sizeof(bt_voxel_buf), val);
        /*
         * 不 set_response —— :DATA 进入二进制接收模式后，
         * 需要先收完数据才回复 OK。
         */
        return 1;
    }

    /* ---- :SAVE <filename> <size> ---- */
    if (size >= 6 && memcmp(data, ":SAVE ", 6) == 0) {
        uint16_t i = 6;
        uint16_t j = 0;

        /* 解析文件名 (到空格或换行) */
        while (i < size && data[i] != ' ' && data[i] != '\n'
               && data[i] != '\r' && j < sizeof(bt_save_filename) - 1) {
            bt_save_filename[j++] = (char)data[i++];
        }
        bt_save_filename[j] = '\0';

        if (j == 0 || i >= size || data[i] != ' ') {
            set_response("ERR:bad save format\n");
            return 1;
        }

        /* 跳过空格 */
        i++;

        /* 解析文件大小 */
        uint32_t fsize = parse_u32(data + i, size - i);
        if (fsize == 0 || fsize > BT_FILE_BUF_SIZE) {
            set_response("ERR:bad save size\n");
            return 1;
        }

        bt_save_size = fsize;
        bt_cmd_pending = CMD_SAVE;
        return 1;  /* 延迟到主循环：f_open + 启动DMA */
    }

    return 0;   /* 以 ':' 开头但未识别的指令，不回传 */
}
