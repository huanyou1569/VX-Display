/**
 * @file ui_input.c
 * @brief 输入处理 - 摇杆ADC、方向检测、菜单导航、按键处理
 * @note  所有逻辑从原始main.c主循环中提取，保持完全一致
 */
#include "ui_input.h"
#include "app_state.h"
#include "ui_menu.h"
#include "ble_handler.h"
#include "motor_control.h"
#include "audio_player.h"
#include "usart.h"
#include "stdio.h"
#include "string.h"

// KEY2读取（对应原 main.c while 循环第1行 key2_now）
uint8_t Input_ReadKey2(void) {
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_7) == GPIO_PIN_RESET) ? 0 : 1;
}

// 死区检测，跳过画笔模式（对应原 main.c 行421-426）
void Input_ProcessDeadZone(void) {
    if (menu_state != 7 && menu_state != 8 && menu_state != 9) {
        if ((joy_up == 0) && (joy_down == 0) && (joy_left == 0) && (joy_right == 0)) {
            if (center_start_tick == 0) center_start_tick = HAL_GetTick();
            else if ((HAL_GetTick() - center_start_tick) > CENTER_DELAY_MS) joy_need_center = 0;
        } else { center_start_tick = 0; }
    }
}

// 画笔模式方向发送，20ms间隔（对应原 main.c 行429-463）
void Input_ProcessBrushDirection(void) {
    if (menu_state == 7 || menu_state == 8) {
        uint8_t cur_dir = 0;
        if (joy_up) cur_dir = 1;
        else if (joy_down) cur_dir = 2;
        else if (joy_left) cur_dir = 3;
        else if (joy_right) cur_dir = 4;

        if (cur_dir != 0) {
            if (HAL_GetTick() - brush_move_tick >= 20) {
                brush_move_tick = HAL_GetTick();
                if (menu_state == 7) {
                    if (cur_dir != brush_last_dir) {
                        brush_last_dir = cur_dir;
                        Bluetooth_SendCmd("JK\n");
                    }
                }
                else { // menu_state == 8
                    if (cur_dir == brush_last_dir) {
                        if (cur_dir == 3) Bluetooth_SendCmd("JU\n");
                        else if (cur_dir == 4) Bluetooth_SendCmd("JD\n");
                        else if (cur_dir == 1) Bluetooth_SendCmd("JL\n");
                        else if (cur_dir == 2) Bluetooth_SendCmd("JR\n");
                    } else if (brush_last_dir == 0) {
                        brush_last_dir = cur_dir;
                        if (cur_dir == 3) Bluetooth_SendCmd("JU\n");
                        else if (cur_dir == 4) Bluetooth_SendCmd("JD\n");
                        else if (cur_dir == 1) Bluetooth_SendCmd("JL\n");
                        else if (cur_dir == 2) Bluetooth_SendCmd("JR\n");
                    }
                }
            }
        } else {
            brush_last_dir = 0;
            brush_move_tick = 0;
        }
    }
}

// 贪吃蛇方向发送，100ms间隔（menu_state == 9）
void Input_ProcessSnakeDirection(void) {
    if (menu_state != 9) return;
    #define SNAKE_MOVE_MS 100
    uint8_t cur_dir = 0;
    if (joy_up)    cur_dir = 1;
    else if (joy_down)  cur_dir = 2;
    else if (joy_left)  cur_dir = 3;
    else if (joy_right) cur_dir = 4;

    if (cur_dir != 0) {
        if (HAL_GetTick() - snake_move_tick >= SNAKE_MOVE_MS) {
            snake_move_tick = HAL_GetTick();
            if (cur_dir != snake_last_dir) {
                snake_last_dir = cur_dir;
                if      (cur_dir == 1) Bluetooth_SendCmd("JL\n");
                else if (cur_dir == 2) Bluetooth_SendCmd("JR\n");
                else if (cur_dir == 3) Bluetooth_SendCmd("JU\n");
                else if (cur_dir == 4) Bluetooth_SendCmd("JD\n");
            }
        }
    } else {
        snake_last_dir = 0;
        snake_move_tick = 0;
    }
}

// 非画笔模式的方向导航（对应原 main.c 行467-524）
void Input_ProcessNavigation(void) {
    if (!duty_ramp_active && !joy_need_center && (HAL_GetTick() - last_joy_move_tick) > JOY_COOLDOWN_MS) {
        uint8_t action = 0;
        if (joy_up && !last_joy_up)      action = 1;
        if (joy_down && !last_joy_down)  action = 2;
        if (joy_left && !last_joy_left)  action = 3;
        if (joy_right && !last_joy_right) action = 4;
        if (action) {
            last_joy_move_tick = HAL_GetTick();
            joy_need_center = 1;
            center_start_tick = 0;
            if (menu_state == 0) {
                if (action == 3) { if (main_selected == 0) main_selected = 3; else main_selected--; DrawMainMenu(main_selected); }
                else if (action == 4) { main_selected++; if (main_selected >= 4) main_selected = 0; DrawMainMenu(main_selected); }
            } else if (menu_state == 1) {
                if (!motor_in_control) {
                    if (action == 3) { if (motor_sub_selected == 0) motor_sub_selected = 1; else motor_sub_selected--; DrawMotorSubMenu(motor_sub_selected); }
                    else if (action == 4) { motor_sub_selected++; if (motor_sub_selected >= 2) motor_sub_selected = 0; DrawMotorSubMenu(motor_sub_selected); }
                } else {
                    if (action == 3) { if (duty_permil <= DUTY_PERMIL_MAX - 10) duty_permil += 10; else duty_permil = DUTY_PERMIL_MAX; UpdatePWM(); DrawSubMotor(); }
                    else if (action == 4) { if (duty_permil >= 10) duty_permil -= 10; else duty_permil = 0; UpdatePWM(); DrawSubMotor(); }
                }
            } else if (menu_state == 2) {
                if (action == 3) {
                    if (audio_selected == 0) { audio_selected = audio_count - 1; audio_top = (audio_count > VISIBLE_ITEMS) ? (audio_count - VISIBLE_ITEMS) : 0; }
                    else { audio_selected--; if (audio_selected < audio_top) audio_top = audio_selected; }
                    DrawSubAudio(audio_selected, audio_top);
                } else if (action == 4) {
                    audio_selected++; if (audio_selected >= audio_count) { audio_selected = 0; audio_top = 0; }
                    if (audio_selected >= audio_top + VISIBLE_ITEMS) audio_top = audio_selected - VISIBLE_ITEMS + 1;
                    DrawSubAudio(audio_selected, audio_top);
                }
            } else if (menu_state == 3) {
                if (!bt_load_sent) {
                    if (bt_load_mode == 1) {
                        if (action == 3) { if (bt_selected == 0) bt_selected = bt_load_count - 1; else bt_selected--; }
                        else if (action == 4) { bt_selected++; if (bt_selected >= bt_load_count) bt_selected = 0; }
                        DrawSubBluetooth();
                    } else if (bt_status_mode == 0) {
                        if (action == 3) { if (bt_selected == 0) bt_selected = bt_cmd_count - 1; else bt_selected--; }
                        else if (action == 4) { bt_selected++; if (bt_selected >= bt_cmd_count) bt_selected = 0; }
                        DrawSubBluetooth();
                    }
                }
            } else if (menu_state == 4) {
                if (action == 3) { if (interact_selected == 0) interact_selected = interact_count - 1; else interact_selected--; }
                else if (action == 4) { interact_selected++; if (interact_selected >= interact_count) interact_selected = 0; }
                DrawInteractMenu(interact_selected);
            } else if (menu_state == 5) {
                char cmd[20];
                if (action == 3) { Bluetooth_SendCmd("J+\n"); donut_last_j = 0; }
                else if (action == 4) { Bluetooth_SendCmd("J-\n"); donut_last_j = 1; }
                else if (action == 1) { if (donut_brightness < 100) donut_brightness++; sprintf(cmd, "BRIGHT %d\n", donut_brightness); Bluetooth_SendCmd(cmd); }
                else if (action == 2) { if (donut_brightness > 0) donut_brightness--; sprintf(cmd, "BRIGHT %d\n", donut_brightness); Bluetooth_SendCmd(cmd); }
                DrawDonutControl();
            } else if (menu_state == 6) {
                if (action == 3) { if (brush_selected == 0) brush_selected = brush_count - 1; else brush_selected--; }
                else if (action == 4) { brush_selected++; if (brush_selected >= brush_count) brush_selected = 0; }
                DrawBrushMenu(brush_selected);
            }
        }
    }
    last_joy_up = joy_up; last_joy_down = joy_down;
    last_joy_left = joy_left; last_joy_right = joy_right;
}

// 摇杆按钮处理（对应原 main.c 行527-597）
// 用50ms消抖 + 原始短按/长按逻辑
void Input_ProcessJoyButton(void) {
    #define JOY_BTN_DEBOUNCE_MS 50
    static uint32_t btn_stable_tick = 0;
    static uint8_t  btn_stable = 1;       // 消抖后的稳定值

    // --- 消抖：joy_btn 连续稳定 50ms 才更新 btn_stable ---
    if (joy_btn != btn_stable) {
        if (btn_stable_tick == 0) {
            btn_stable_tick = HAL_GetTick();
        } else if ((HAL_GetTick() - btn_stable_tick) >= JOY_BTN_DEBOUNCE_MS) {
            btn_stable = joy_btn;
            btn_stable_tick = 0;
        }
        // 未到消抖时间 → 沿用旧 btn_stable, 不触发任何动作
    } else {
        btn_stable_tick = 0;
    }

    if (menu_state == 8) {
        if (btn_stable == 0 && last_joy_btn == 1) {
            joy_btn_press_time = HAL_GetTick(); joy_btn_pressed = 1;
            draw_btn_toggle = !draw_btn_toggle;
            Bluetooth_SendCmd(draw_btn_toggle ? "JB1\n" : "JB0\n");
        } else if (btn_stable == 0 && joy_btn_pressed) {
            if ((HAL_GetTick() - joy_btn_press_time) > 800) {
                Bluetooth_SendCmd("JE\n"); HAL_Delay(200); Bluetooth_SendCmd("JE\n");
                menu_state = 4; brush_selected = 0; draw_btn_toggle = 0;
                DrawInteractMenu(interact_selected);
                joy_btn_pressed = 0; HAL_Delay(200);
            }
        } else if (btn_stable == 1 && last_joy_btn == 0) {
            if (joy_btn_pressed) { joy_btn_pressed = 0; }
        }
    } else if (menu_state == 7) {
        if (btn_stable == 0 && last_joy_btn == 1) {
            joy_btn_press_time = HAL_GetTick(); joy_btn_pressed = 1;
        } else if (btn_stable == 0 && joy_btn_pressed) {
            if ((HAL_GetTick() - joy_btn_press_time) > 800) {
                Bluetooth_SendCmd("JE\n"); HAL_Delay(200); Bluetooth_SendCmd("JE\n");
                menu_state = 4; brush_selected = 0;
                DrawInteractMenu(interact_selected);
                joy_btn_pressed = 0; HAL_Delay(200);
            }
        } else if (btn_stable == 1 && last_joy_btn == 0) {
            if (joy_btn_pressed) { joy_btn_pressed = 0; }
        }
    } else if (menu_state == 9) {
        // 贪吃蛇：短按=JG开局/重开，长按=JE退出
        if (btn_stable == 0 && last_joy_btn == 1) {
            joy_btn_press_time = HAL_GetTick(); joy_btn_pressed = 1;
        } else if (btn_stable == 0 && joy_btn_pressed) {
            if ((HAL_GetTick() - joy_btn_press_time) > 800) {
                Bluetooth_SendCmd("JE\n");
                menu_state = 4;
                DrawInteractMenu(interact_selected);
                joy_btn_pressed = 0; HAL_Delay(200);
            }
        } else if (btn_stable == 1 && last_joy_btn == 0) {
            if (joy_btn_pressed && (HAL_GetTick() - joy_btn_press_time) < 800) {
                Bluetooth_SendCmd("JG\n");
                joy_btn_pressed = 0;
            }
        }
    } else {
        if (btn_stable == 0 && last_joy_btn == 1) {
            joy_btn_press_time = HAL_GetTick(); joy_btn_pressed = 1;
        } else if (btn_stable == 0 && joy_btn_pressed) {
            if ((HAL_GetTick() - joy_btn_press_time) > 800) {
                if (menu_state == 5) { Bluetooth_SendCmd("JE\n"); HAL_Delay(200); Bluetooth_SendCmd("JE\n"); }
                if (menu_state == 6) { Bluetooth_SendCmd("JE\n"); HAL_Delay(200); Bluetooth_SendCmd("JE\n"); }
                if (menu_state == 1 || menu_state == 2 || menu_state == 3 || menu_state == 4 || menu_state == 5) {
                    menu_state = 0; bt_status_mode = 0; bt_load_mode = 0;
                    bt_load_sent = 0; bt_load_result = 0; bt_selected = 0;
                    interact_selected = 0; donut_brightness = 30; brush_selected = 0;
                    motor_in_control = 0;
                    DrawMainMenu(main_selected);
                }
                if (menu_state == 6) {
                    menu_state = 4; brush_selected = 0;
                    DrawInteractMenu(interact_selected);
                }
                joy_btn_pressed = 0; HAL_Delay(200);
            }
        } else if (btn_stable == 1 && last_joy_btn == 0) {
            if (joy_btn_pressed && (HAL_GetTick() - joy_btn_press_time) < 800) {
                if (menu_state == 0) {
                    switch (main_selected) {
                        case 0: menu_state = 1; motor_in_control = 0; motor_sub_selected = 0; DrawMotorSubMenu(motor_sub_selected); break;
                        case 1: menu_state = 3; bluetooth_state = 4; bt_selected = 0; bt_status_mode = 0; bt_load_mode = 0; bt_load_sent = 0; bt_load_result = 0;
                                memset(bt_last_response, 0, sizeof(bt_last_response));
                                Bluetooth_SendCmd("AT+TRANMD=1"); HAL_Delay(200);
                                Bluetooth_SendCmd("AT+LOGMSG=1"); HAL_Delay(200);
                                DrawSubBluetooth(); break;
                        case 2: menu_state = 2; audio_selected = 0; audio_top = 0; DrawSubAudio(audio_selected, audio_top); submenu_enter_tick = HAL_GetTick(); joy_btn_pressed = 0; break;
                        case 3: menu_state = 4; interact_selected = 0; DrawInteractMenu(interact_selected); break;
                        default: break;
                    }
                } else if (menu_state == 1) {
                    if (!motor_in_control) {
                        if (motor_sub_selected == 0) {
                            motor_in_control = 1; duty_ramp_target = DUTY_RAMP_TARGET; DrawSubMotor();
                        } else {
                            duty_ramp_target = DUTY_PERMIL_MAX;
                            duty_ramp_active = 1; duty_ramp_start_tick = HAL_GetTick(); duty_ramp_start_value = duty_permil;
                            motor_in_control = 1; DrawSubMotor();
                        }
                    } else {
                        duty_ramp_target = DUTY_RAMP_TARGET;
                        duty_ramp_active = 1; duty_ramp_start_tick = HAL_GetTick(); duty_ramp_start_value = duty_permil; DrawSubMotor();
                    }
                } else if (menu_state == 2) {
                    if ((HAL_GetTick() - submenu_enter_tick) > 500) AudioPlayer_Play(audio_files[audio_selected]);
                } else if (menu_state == 3) {
                    if (!bt_load_sent) {
                        if (bt_status_mode == 0 && bt_load_mode == 0) {
                            if (bt_selected == 0) { Bluetooth_SendCmd(":STATUS"); bt_status_mode = 1; status_rpm = 0; status_f = 0; status_s = 0; }
                            else if (bt_selected == 1) { bt_load_mode = 1; bt_selected = 0; }
                            else if (bt_selected == 2) {
                                // 退出透传 → 切为从机模式 → 复位，释放BLE连接
                                HAL_Delay(1000);
                                HAL_UART_Transmit(&huart1, (uint8_t *)"+++", 3, 100);
                                HAL_Delay(1000);
                                HAL_UART_Transmit(&huart1, (uint8_t *)"AT+ROLE=0", 9, 500);
                                HAL_Delay(200);
                                HAL_UART_Transmit(&huart1, (uint8_t *)"AT+RESET", 8, 500);
                                HAL_Delay(500);
                                bluetooth_state = 0; bt_selected = 0; bt_status_mode = 0; bt_load_mode = 0;
                                bt_load_sent = 0; bt_load_result = 0;
                                memset(bt_last_response, 0, sizeof(bt_last_response));
                                DrawSubBluetooth();
                            }
                        } else if (bt_load_mode == 1) {
                            char cmd[64]; sprintf(cmd, ":LOAD %s\r\n", bt_load_files[bt_selected]); Bluetooth_SendCmd(cmd);
                            bt_load_sent = 1; bt_load_sent_tick = HAL_GetTick(); bt_load_result = 0; memset(bt_last_response, 0, sizeof(bt_last_response));
                        } else if (bt_status_mode == 1) { bt_status_mode = 0; bt_selected = 0; }
                        DrawSubBluetooth();
                    }
                } else if (menu_state == 4) {
                    if (interact_selected == 0) { menu_state = 5; donut_brightness = 30; donut_last_j = 0; DrawDonutControl(); }
                    else if (interact_selected == 1) {
                        menu_state = 6; brush_selected = 0;
                        Bluetooth_SendCmd("BRUSH\n");
                        DrawBrushMenu(brush_selected);
                    }
                    else if (interact_selected == 2) {
                        menu_state = 9; snake_last_dir = 0; snake_move_tick = 0;
                        snake_score = 0; snake_high = 0; snake_state = 0; snake_len = 0;
                        Bluetooth_SendCmd(":SNAKE\n");
                        DrawSnakeGame();
                    }
                } else if (menu_state == 6) {
                    if (brush_selected == 0) { menu_state = 7; brush_last_dir = 0; brush_move_tick = 0; DrawColorMenu(); }
                    else if (brush_selected == 1) { menu_state = 8; draw_btn_toggle = 0; brush_last_dir = 0; brush_move_tick = 0; DrawDrawMenu(); }
                }
                joy_btn_pressed = 0;
            }
        }
    }
    last_joy_btn = btn_stable;
}

// KEY2处理（对应原 main.c 行600-627）
void Input_ProcessKey2(uint8_t key2_now) {
    if (!duty_ramp_active) {
        if (menu_state == 1 && motor_in_control) {
            if (key2_now == 0 && last_key2_state == 1 && (HAL_GetTick() - last_key2_tick) > KEY_COOLDOWN_MS) {
                last_key2_tick = HAL_GetTick();
                if (duty_permil >= 1) duty_permil -= 1; else duty_permil = 0;
                UpdatePWM(); DrawSubMotor();
            }
        } else if (menu_state == 5) {
            if (key2_now == 0 && last_key2_state == 1 && (HAL_GetTick() - last_key2_tick) > KEY_COOLDOWN_MS) {
                last_key2_tick = HAL_GetTick();
                Bluetooth_SendCmd("JK\n"); DrawDonutControl();
            }
        } else if (menu_state == 7 || menu_state == 8) {
            if (key2_now == 0 && last_key2_state == 1) {
                key2_press_start = HAL_GetTick(); key2_pressed = 1;
            } else if (key2_now == 1 && key2_pressed) {
                key2_pressed = 0;
                if ((HAL_GetTick() - key2_press_start) > 800) {
                    menu_state = 6;
                    brush_selected = (menu_state == 7) ? 0 : 1;
                    DrawBrushMenu(brush_selected);
                } else {
                    Bluetooth_SendCmd("JP\n");
                }
            }
        }
    }
    last_key2_state = key2_now;
}
