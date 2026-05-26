/**
 * @file ui_menu.c
 * @brief OLED菜单显示 - 所有界面的绘制函数
 * @note  所有函数直接从原始main.c提取，逻辑不变
 */
#include "ui_menu.h"
#include "app_state.h"
#include "oled.h"
#include "font.h"
#include "stdio.h"
#include "string.h"

void DrawMainMenu(uint8_t selected) {
    const char *items[] = {"1.Motor Drv", "2.Bluetooth", "3.Audio Play", "4.Interact"};
    OLED_NewFrame();
    for (uint8_t i = 0; i < 4; i++) {
        if (i == selected) OLED_PrintString(2, i * 16, (char *)items[i], &font16x16, OLED_COLOR_REVERSED);
        else OLED_PrintString(2, i * 16, (char *)items[i], &font16x16, OLED_COLOR_NORMAL);
    }
    OLED_ShowFrame();
}

void DrawSubMotor(void) {
    char buf1[20], buf2[20];
    uint16_t duty_int = duty_permil / 10;
    uint8_t duty_frac = duty_permil % 10;
    OLED_NewFrame();
    OLED_PrintString(2, 0, "Motor Ctrl", &font16x16, OLED_COLOR_NORMAL);
    sprintf(buf1, "Duty: %d.%d%%", duty_int, duty_frac);
    OLED_PrintString(2, 16, buf1, &font16x16, OLED_COLOR_REVERSED);
    sprintf(buf2, "RPM: %d", actual_speed_rpm);
    OLED_PrintString(2, 32, buf2, &font16x16, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
}

void DrawMotorSubMenu(uint8_t selected) {
    const char *items[] = {"1.Speed Ctrl", "2.Stop Motor"};
    OLED_NewFrame();
    OLED_PrintString(2, 0, "Motor Menu", &font16x16, OLED_COLOR_NORMAL);
    for (uint8_t i = 0; i < 2; i++) {
        if (i == selected) OLED_PrintString(2, 16 + i * 16, (char *)items[i], &font16x16, OLED_COLOR_REVERSED);
        else OLED_PrintString(2, 16 + i * 16, (char *)items[i], &font16x16, OLED_COLOR_NORMAL);
    }
    OLED_ShowFrame();
}

void DrawSubAudio(uint8_t selected, uint8_t top) {
    OLED_NewFrame();
    OLED_PrintString(2, 0, "Audio List", &font16x16, OLED_COLOR_NORMAL);
    for (uint8_t i = 0; i < VISIBLE_ITEMS; i++) {
        uint8_t idx = top + i;
        if (idx >= audio_count) break;
        uint8_t y = 16 + i * 16;
        if (idx == selected) OLED_PrintString(2, y, (char *)audio_files[idx], &font16x16, OLED_COLOR_REVERSED);
        else OLED_PrintString(2, y, (char *)audio_files[idx], &font16x16, OLED_COLOR_NORMAL);
    }
    OLED_ShowFrame();
}

void DrawSubBluetooth(void) {
    OLED_NewFrame();
    OLED_PrintString(2, 0, "BT Cmd Ctrl", &font16x16, OLED_COLOR_NORMAL);

    if (bt_load_sent) {
        if (bt_load_result == 1) OLED_PrintString(2, 16, "LOAD: YES", &font16x16, OLED_COLOR_REVERSED);
        else if (bt_load_result == 2) OLED_PrintString(2, 16, "LOAD: NO", &font16x16, OLED_COLOR_REVERSED);
        else OLED_PrintString(2, 16, "Waiting...", &font16x16, OLED_COLOR_NORMAL);
        if (strlen(bt_last_response) > 0) OLED_PrintASCIIString(0, 48, bt_last_response, &afont12x6, OLED_COLOR_NORMAL);
    }
    else if (bt_load_mode == 1) {
        OLED_PrintString(2, 0, "Select File", &font16x16, OLED_COLOR_NORMAL);

        uint8_t top = (bt_selected / BT_ITEMS_PER_PAGE) * BT_ITEMS_PER_PAGE;
        for (uint8_t i = 0; i < BT_ITEMS_PER_PAGE; i++) {
            uint8_t idx = top + i;
            if (idx >= bt_load_count) break;
            uint8_t y = 16 + i * 16;
            if (idx == bt_selected)
                OLED_PrintString(2, y, (char *)bt_load_files[idx], &font16x16, OLED_COLOR_REVERSED);
            else
                OLED_PrintString(2, y, (char *)bt_load_files[idx], &font16x16, OLED_COLOR_NORMAL);
        }
        OLED_PrintASCIIString(0, 56, "Press to send", &afont12x6, OLED_COLOR_NORMAL);
    }
    else if (bt_status_mode == 1) {
        char buf[32];
        sprintf(buf, "RPM:%d", status_rpm);
        OLED_PrintString(2, 16, buf, &font16x16, OLED_COLOR_NORMAL);
        sprintf(buf, "F:%d  S:%d", status_f, status_s);
        OLED_PrintString(2, 32, buf, &font16x16, OLED_COLOR_NORMAL);
        OLED_PrintASCIIString(0, 56, "Press to back", &afont12x6, OLED_COLOR_NORMAL);
    }
    else {
        uint8_t top = (bt_selected / BT_ITEMS_PER_PAGE) * BT_ITEMS_PER_PAGE;
        for (uint8_t i = 0; i < BT_ITEMS_PER_PAGE; i++) {
            uint8_t idx = top + i;
            if (idx >= bt_cmd_count) break;
            uint8_t y = 16 + i * 16;
            if (idx == bt_selected)
                OLED_PrintString(2, y, (char *)bt_cmd_items[idx], &font16x16, OLED_COLOR_REVERSED);
            else
                OLED_PrintString(2, y, (char *)bt_cmd_items[idx], &font16x16, OLED_COLOR_NORMAL);
        }
        char diag[16];
        sprintf(diag, "RX:%d", bt_rx_index);
        OLED_PrintASCIIString(0, 48, diag, &afont12x6, OLED_COLOR_NORMAL);
        if (strlen(bt_last_response) > 0) OLED_PrintASCIIString(40, 48, bt_last_response, &afont12x6, OLED_COLOR_NORMAL);
    }
    OLED_ShowFrame();
}

void DrawInteractMenu(uint8_t selected) {
    OLED_NewFrame();
    OLED_PrintString(2, 0, "Interact", &font16x16, OLED_COLOR_NORMAL);
    for (uint8_t i = 0; i < interact_count; i++) {
        uint8_t y = 16 + i * 16;
        if (i == selected) OLED_PrintString(2, y, (char *)interact_items[i], &font16x16, OLED_COLOR_REVERSED);
        else OLED_PrintString(2, y, (char *)interact_items[i], &font16x16, OLED_COLOR_NORMAL);
    }
    OLED_ShowFrame();
}

void DrawDonutControl(void) {
    char buf[20];
    OLED_NewFrame();
    OLED_PrintString(2, 0, "Donut Ctrl", &font16x16, OLED_COLOR_NORMAL);
    sprintf(buf, "B:%d", donut_brightness);
    OLED_PrintString(2, 16, buf, &font16x16, OLED_COLOR_NORMAL);
    OLED_PrintString(2, 32, donut_last_j ? "J:-" : "J:+", &font16x16, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 56, "X/Y:J/B KEY2:JK", &afont12x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
}

void DrawBrushMenu(uint8_t selected) {
    OLED_NewFrame();
    OLED_PrintString(2, 0, "Brush Menu", &font16x16, OLED_COLOR_NORMAL);
    for (uint8_t i = 0; i < brush_count; i++) {
        uint8_t y = 16 + i * 16;
        if (i == selected) OLED_PrintString(2, y, (char *)brush_items[i], &font16x16, OLED_COLOR_REVERSED);
        else OLED_PrintString(2, y, (char *)brush_items[i], &font16x16, OLED_COLOR_NORMAL);
    }
    OLED_ShowFrame();
}

void DrawColorMenu(void) {
    OLED_NewFrame();
    OLED_PrintString(2, 0, "COLOR Mode", &font16x16, OLED_COLOR_NORMAL);
    OLED_PrintString(2, 16, "Move: JK", &font16x16, OLED_COLOR_NORMAL);
    OLED_PrintString(2, 32, "KEY2: JP", &font16x16, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 56, "Long KEY2: back", &afont12x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
}

void DrawDrawMenu(void) {
    OLED_NewFrame();
    OLED_PrintString(2, 0, "DRAW Mode", &font16x16, OLED_COLOR_NORMAL);
    OLED_PrintString(2, 16, "X:JU/JD Y:JR/JL", &font16x16, OLED_COLOR_NORMAL);
    OLED_PrintString(2, 32, "Btn:JB1/JB0", &font16x16, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 56, "KEY2:JP Long:back", &afont12x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
}

void DrawSnakeGame(void) {
    char buf[32];
    OLED_NewFrame();
    OLED_PrintString(2, 0, "Snake Game", &font16x16, OLED_COLOR_NORMAL);
    sprintf(buf, "Score:%-3u High:%-3u", snake_score, snake_high);
    OLED_PrintASCIIString(0, 16, buf, &afont12x6, OLED_COLOR_NORMAL);
    sprintf(buf, "Len:%-2u %s", snake_len, snake_state ? "DEAD" : "PLAY");
    OLED_PrintASCIIString(0, 28, buf, &afont12x6, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 40, "Btn:JG Dir:JL/JR/JU/JD", &afont12x6, OLED_COLOR_NORMAL);
    OLED_PrintASCIIString(0, 56, "Long press: back", &afont12x6, OLED_COLOR_NORMAL);
    OLED_ShowFrame();
}

// 根据menu_state定时刷新显示（100ms间隔）
void DisplayRefresh(void) {
    if ((HAL_GetTick() - last_display_update) <= DISPLAY_REFRESH_MS) return;
    last_display_update = HAL_GetTick();
    if (menu_state == 1 && motor_in_control) { DrawSubMotor(); }
    if (menu_state == 3)      { DrawSubBluetooth(); }
    if (menu_state == 4)      { DrawInteractMenu(interact_selected); }
    if (menu_state == 5)      { DrawDonutControl(); }
    if (menu_state == 6)      { DrawBrushMenu(brush_selected); }
    if (menu_state == 7)      { DrawColorMenu(); }
    if (menu_state == 8)      { DrawDrawMenu(); }
    if (menu_state == 9)      { DrawSnakeGame(); }
}
