/**
 * bt_commands.h  —— 蓝牙指令处理
 *
 * 协议: 文本指令以 ':' 开头，换行结尾
 *   :LOAD <文件名>      加载TF卡文件 (.bin或.slices)
 *   :STATUS             查询设备状态
 *   :LIST               列出TF卡 .bin / .slices 文件
 *   :DATA <字节数>      进入二进制传输模式 (体素数据)
 *   :SAVE <文件名> <字节数>  接收文件并写入TF卡 (不加载)
 *
 * 二进制传输: PC发送 :DATA N\n 后，MCU停止空闲DMA，
 * 启动固定长度DMA接收N字节，收完后写体素缓冲并回复OK。
 */

#ifndef BT_COMMANDS_H
#define BT_COMMANDS_H

#include <stdint.h>
#include "usart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 最大文件接收缓冲 (16字节头 + 100片 * 2304字节 = 230416) */
#define BT_FILE_BUF_SIZE  230416U

/* ---- 由 main.c 调用 ---- */

/**
 * 处理收到的数据。若识别为指令则执行并返回 1，
 * 调用方通过 BT_GetResponse 获取回复内容。
 * 返回 0 表示非指令数据，调用方按原逻辑回传。
 */
uint8_t BT_ProcessCommand(const uint8_t *data, uint16_t size);

/**
 * 主循环轮询 —— 检查体素数据是否接收完毕并写入体素缓冲。
 */
void BT_PollVoxelReady(void);

/**
 * 主循环轮询 —— 执行延迟的阻塞指令 (:LOAD, :LIST, :SAVE文件打开)。
 * 这些涉及 SD 卡操作，不能在中 ISR 执行。
 */
void BT_PollCommands(void);

/**
 * 主循环轮询 —— 文件数据DMA接收完成后，写入TF卡。
 */
void BT_PollSaveFile(void);

/**
 * UART固定长度DMA接收完成回调。
 * 在 HAL_UART_RxCpltCallback 中调用。
 */
void BT_OnRxComplete(UART_HandleTypeDef *huart);

/* ---- 回复数据获取 ---- */

/** 是否有待发送的回复数据 */
uint8_t BT_HasResponse(void);

/** 获取回复数据指针和长度，发完后调用 BT_ClearResponse */
const uint8_t *BT_GetResponse(void);
uint16_t      BT_GetResponseSize(void);
void           BT_ClearResponse(void);

/** 是否正在二进制接收模式 */
uint8_t BT_IsReceiving(void);

/** 是否正在接收文件数据 (:SAVE) */
uint8_t BT_IsSaveReceiving(void);

/**
 * 向文件接收缓冲追加数据 (在 UART 空闲中断中调用)。
 * 每次空闲DMA触发时，将收到的片段追加到 bt_file_buf。
 * 当累计长度达到 bt_save_size 时自动设 bt_save_data_ready。
 */
void BT_FeedFileData(const uint8_t *data, uint16_t size);

/** 获取文件接收进度: 已收/预期, -1=未在接收模式 */
int32_t BT_GetSaveProgress(void);
uint32_t BT_GetSaveTotal(void);

#ifdef __cplusplus
}
#endif

#endif /* BT_COMMANDS_H */
