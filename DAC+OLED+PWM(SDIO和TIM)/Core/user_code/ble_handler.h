/**
 * @file ble_handler.h
 * @brief 蓝牙处理 - USART1 指令发送、响应解析、DMA接收
 */
#ifndef __BLE_HANDLER_H__
#define __BLE_HANDLER_H__

#include "main.h"
#include <stdint.h>

void Bluetooth_SendCmd(const char *cmd);
void ParseStatusResponse(char *resp);

// BLE接收处理（在主循环中调用）
void BLE_ProcessReceive(void);

// BLE初始化（启动UART中断接收）
void BLE_Init(void);

// UART接收回调（在HAL_UART_RxCpltCallback中调用）
void BLE_UartRxCallback(UART_HandleTypeDef *huart);

#endif
