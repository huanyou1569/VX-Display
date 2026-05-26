/**
 * @file ble_handler.c
 * @brief 蓝牙处理 - 指令发送、响应解析、中断接收
 * @note  所有逻辑从原始main.c提取，保持不变
 */
#include "ble_handler.h"
#include "app_state.h"
#include "ui_menu.h"
#include "usart.h"
#include "stdio.h"
#include "string.h"

// 发送蓝牙指令（对应原 main.c 的 Bluetooth_SendCmd）
void Bluetooth_SendCmd(const char *cmd) {
    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 100);
}

// 解析STATUS响应（对应原 main.c 的 ParseStatusResponse）
void ParseStatusResponse(char *resp) {
    char *p;
    p = strstr(resp, "RPM:"); if (p) sscanf(p, "RPM:%hd", &status_rpm);
    p = strstr(resp, "F:"); if (p) sscanf(p, "F:%hhu", &status_f);
    p = strstr(resp, "S:"); if (p) sscanf(p, "S:%hhu", &status_s);
    // 贪吃蛇字段
    p = strstr(resp, "SCORE:"); if (p) sscanf(p, "SCORE:%hu", &snake_score);
    p = strstr(resp, "HIGH:");  if (p) sscanf(p, "HIGH:%hu", &snake_high);
    p = strstr(resp, "LEN:");   if (p) sscanf(p, "LEN:%hhu", &snake_len);
    p = strstr(resp, "STATE:"); if (p) { if (strstr(p, "DEAD")) snake_state = 1; else snake_state = 0; }
}

// BLE接收处理（对应原 main.c 行371-389）
void BLE_ProcessReceive(void) {
    // 空闲超时 → 行完成
    if (bt_rx_index > 0 && !bt_rx_line_complete && (HAL_GetTick() - bt_rx_last_tick) > 50) {
        bt_rx_line_complete = 1;
    }
    // 行完成处理
    if (bt_rx_line_complete) {
        bt_rx_line_complete = 0;
        strcpy(bt_last_response, (char *)bt_rx_buf);
        if ((menu_state == 3 && bt_status_mode == 1) || menu_state == 9)
            ParseStatusResponse(bt_last_response);
        if (bt_load_sent == 1 && bt_load_result == 0 && strlen(bt_last_response) > 0) {
            bt_load_result = strstr(bt_last_response, "OK") ? 1 : 2;
            bt_load_result_tick = HAL_GetTick();
        }
        bt_rx_index = 0; memset(bt_rx_buf, 0, BT_RX_BUFFER_SIZE); bt_rx_last_tick = 0;
    }
    // LOAD超时（3秒无响应）
    if (bt_load_sent == 1 && bt_load_result == 0 && (HAL_GetTick() - bt_load_sent_tick) > 3000) {
        bt_load_result = 2; bt_load_result_tick = HAL_GetTick();
    }
    // LOAD结果显示超时（3秒后清除）
    if (bt_load_sent == 1 && bt_load_result != 0 && (HAL_GetTick() - bt_load_result_tick) > 3000) {
        bt_load_sent = 0; bt_load_result = 0; bt_load_mode = 0; bt_selected = 1; DrawSubBluetooth();
    }
}

// BLE初始化（对应原 main.c 行341）
void BLE_Init(void) {
    HAL_UART_Receive_IT(&huart1, &bt_rx_byte, 1);
}

// UART接收回调（对应原 main.c 行675-686 HAL_UART_RxCpltCallback）
void BLE_UartRxCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        uint8_t ch = bt_rx_byte;
        if (bt_rx_index < BT_RX_BUFFER_SIZE - 1) {
            bt_rx_buf[bt_rx_index++] = ch;
            bt_rx_buf[bt_rx_index] = '\0';
        }
        bt_rx_last_tick = HAL_GetTick();
        if (ch == '\n') bt_rx_line_complete = 1;
        HAL_UART_Receive_IT(&huart1, &bt_rx_byte, 1);
    }
}
