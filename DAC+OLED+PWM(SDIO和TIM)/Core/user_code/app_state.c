/**
 * @file app_state.c
 * @brief 全局应用状态 - 所有跨模块共享变量的定义
 */
#include "app_state.h"

// ===== 菜单状态 =====
uint8_t menu_state = 0;
uint8_t main_selected = 0;
uint8_t interact_selected = 0;
uint8_t brush_selected = 0;

// ===== 音频 =====
uint8_t audio_selected = 0;
const char *audio_files[] = {"MUSIC1.raw", "MUSIC2.raw"};
const uint8_t audio_count = sizeof(audio_files) / sizeof(audio_files[0]);
uint8_t audio_top = 0;

// ===== 摇杆 =====
uint32_t adc_x_raw = 0, adc_y_raw = 0;
uint8_t joy_up = 0, joy_down = 0, joy_left = 0, joy_right = 0;
uint8_t last_joy_up = 0, last_joy_down = 0, last_joy_left = 0, last_joy_right = 0;
uint8_t joy_btn = 0, last_joy_btn = 1;
uint32_t joy_btn_press_time = 0;
uint8_t joy_btn_pressed = 0;
uint32_t last_joy_move_tick = 0;
uint8_t joy_need_center = 0;
uint32_t center_start_tick = 0;
uint32_t submenu_enter_tick = 0;

// ===== 电机控制 =====
uint16_t actual_speed_rpm = 0;
uint16_t duty_permil = 0;
uint8_t duty_ramp_active = 0;
uint32_t duty_ramp_start_tick = 0;
uint16_t duty_ramp_start_value = 0;
uint16_t duty_ramp_target = DUTY_RAMP_TARGET;
volatile uint32_t last_pulse_tick = 0;
volatile uint16_t current_rpm = 0;
uint16_t rpm_buffer[RPM_FILTER_WINDOW] = {0};
uint8_t rpm_buf_index = 0;
uint8_t motor_in_control = 0;
uint8_t motor_sub_selected = 0;

// ===== 按键 =====
uint8_t last_key2_state = 1;
uint32_t last_key2_tick = 0;
uint32_t key2_press_start = 0;
uint8_t key2_pressed = 0;
uint32_t last_display_update = 0;

// ===== 蓝牙 =====
uint8_t bluetooth_state = 0;
uint8_t bt_rx_buf[BT_RX_BUFFER_SIZE];
char bt_device_list[BT_MAX_DEVICES][18];
uint8_t bt_device_count = 0, bt_selected = 0;
char bt_last_response[BT_RX_BUFFER_SIZE] = {0};
const char *bt_cmd_items[] = {"STATUS", "LOAD file", "DISC"};
const uint8_t bt_cmd_count = sizeof(bt_cmd_items) / sizeof(bt_cmd_items[0]);
uint8_t bt_status_mode = 0, bt_load_mode = 0;
int16_t status_rpm = 0;
uint8_t status_f = 0, status_s = 0;
const char *bt_load_files[] = {
    "anim_rainbow.slices",
    "chiken_rainbow.slices",
    "dna_v3.slices",
    "dna_v3_3t.slices",
    "Zelda_Z_no_base.slices"
};
const uint8_t bt_load_count = sizeof(bt_load_files) / sizeof(bt_load_files[0]);
uint32_t bt_rx_last_tick = 0;
uint8_t bt_rx_byte;
uint16_t bt_rx_index = 0;
uint8_t bt_rx_line_complete = 0;
uint8_t bt_load_sent = 0, bt_load_result = 0;
uint32_t bt_load_sent_tick = 0, bt_load_result_tick = 0;

// ===== 甜甜圈 =====
int16_t donut_brightness = 30;
uint8_t donut_last_j = 0;

// ===== 交互菜单 =====
const char *interact_items[] = {"Donut", "Brush", "Snake"};
const uint8_t interact_count = sizeof(interact_items) / sizeof(interact_items[0]);

// ===== 画笔 =====
const char *brush_items[] = {"COLOR", "DRAW"};
const uint8_t brush_count = sizeof(brush_items) / sizeof(brush_items[0]);
uint8_t draw_btn_toggle = 0;
uint8_t brush_last_dir = 0;
uint32_t brush_move_tick = 0;

// ===== 贪吃蛇 =====
uint8_t  snake_last_dir = 0;
uint32_t snake_move_tick = 0;
uint16_t snake_score = 0;
uint16_t snake_high = 0;
uint8_t  snake_state = 0;
uint8_t  snake_len = 0;
