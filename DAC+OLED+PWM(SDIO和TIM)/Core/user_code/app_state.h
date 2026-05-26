/**
 * @file app_state.h
 * @brief 全局应用状态 - 所有跨模块共享的变量和常量
 */
#ifndef __APP_STATE_H__
#define __APP_STATE_H__

#include "main.h"
#include <stdint.h>

// ===== 通用常量 =====
#define VISIBLE_ITEMS     3
#define BT_ITEMS_PER_PAGE 3
#define JOY_COOLDOWN_MS   250
#define CENTER_DELAY_MS   150
#define DISPLAY_REFRESH_MS 100
#define KEY_COOLDOWN_MS   80

// ===== 电机常量 =====
#define DUTY_PERMIL_MAX    1000
#define DUTY_RAMP_TARGET   840
#define DUTY_RAMP_DURATION 3000
#define PULSES_PER_REV     5
#define RPM_FILTER_WINDOW  8

// ===== 蓝牙常量 =====
#define BT_RX_BUFFER_SIZE  128
#define BT_MAX_DEVICES     8

// ===== 菜单状态 =====
extern uint8_t menu_state;
extern uint8_t main_selected;
extern uint8_t interact_selected;
extern uint8_t brush_selected;

// ===== 音频 =====
extern uint8_t audio_selected;
extern const char *audio_files[];
extern const uint8_t audio_count;
extern uint8_t audio_top;

// ===== 摇杆 =====
extern uint32_t adc_x_raw, adc_y_raw;
extern uint8_t joy_up, joy_down, joy_left, joy_right;
extern uint8_t last_joy_up, last_joy_down, last_joy_left, last_joy_right;
extern uint8_t joy_btn, last_joy_btn;
extern uint32_t joy_btn_press_time;
extern uint8_t joy_btn_pressed;
extern uint32_t last_joy_move_tick;
extern uint8_t joy_need_center;
extern uint32_t center_start_tick;
extern uint32_t submenu_enter_tick;

// ===== 电机控制 =====
extern uint16_t actual_speed_rpm;
extern uint16_t duty_permil;
extern uint8_t duty_ramp_active;
extern uint32_t duty_ramp_start_tick;
extern uint16_t duty_ramp_start_value;
extern uint16_t duty_ramp_target;
extern volatile uint32_t last_pulse_tick;
extern volatile uint16_t current_rpm;
extern uint16_t rpm_buffer[RPM_FILTER_WINDOW];
extern uint8_t rpm_buf_index;
extern uint8_t motor_in_control;
extern uint8_t motor_sub_selected;

// ===== 按键 =====
extern uint8_t last_key2_state;
extern uint32_t last_key2_tick;
extern uint32_t key2_press_start;
extern uint8_t key2_pressed;
extern uint32_t last_display_update;

// ===== 蓝牙 =====
extern uint8_t bluetooth_state;
extern uint8_t bt_rx_buf[BT_RX_BUFFER_SIZE];
extern char bt_device_list[BT_MAX_DEVICES][18];
extern uint8_t bt_device_count, bt_selected;
extern char bt_last_response[BT_RX_BUFFER_SIZE];
extern const char *bt_cmd_items[];
extern const uint8_t bt_cmd_count;
extern uint8_t bt_status_mode, bt_load_mode;
extern int16_t status_rpm;
extern uint8_t status_f, status_s;
extern const char *bt_load_files[];
extern const uint8_t bt_load_count;
extern uint32_t bt_rx_last_tick;
extern uint8_t bt_rx_byte;
extern uint16_t bt_rx_index;
extern uint8_t bt_rx_line_complete;
extern uint8_t bt_load_sent, bt_load_result;
extern uint32_t bt_load_sent_tick, bt_load_result_tick;

// ===== 甜甜圈 =====
extern int16_t donut_brightness;
extern uint8_t donut_last_j;

// ===== 交互菜单 =====
extern const char *interact_items[];
extern const uint8_t interact_count;

// ===== 画笔 =====
extern const char *brush_items[];
extern const uint8_t brush_count;
extern uint8_t draw_btn_toggle;
extern uint8_t brush_last_dir;
extern uint32_t brush_move_tick;

// ===== 贪吃蛇 =====
extern uint8_t  snake_last_dir;
extern uint32_t snake_move_tick;
extern uint16_t snake_score;
extern uint16_t snake_high;
extern uint8_t  snake_state;     // 0=PLAY 1=DEAD
extern uint8_t  snake_len;

#endif
