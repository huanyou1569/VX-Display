# VX-Diban — STM32F407 蓝牙上位机底板

## 项目概述

这是一块基于 **STM32F407VGT6** 的底板，作为蓝牙上位机遥控 **STM32H743 体积显示器主板（VX-Display）**，同时具备本地电机转速控制和音乐播放功能。

### 三大核心功能

| 功能 | 说明 |
|------|------|
| **蓝牙遥控 H743** | 通过 E104-BT52 BLE 模块发送指令控制体积显示器的甜甜圈/画笔/文件加载等 |
| **电机转速控制** | TIM2 PWM 调速 + 霍尔测速，OLED 实时显示 Duty% 和 RPM |
| **音频播放** | 从 SD 卡读取 .raw 文件，DAC + TIM6 + DMA 播放 |

---

## 硬件说明

### MCU

- **型号**：STM32F407VGT6 (Cortex-M4)
- **主频**：168MHz (HSE 8MHz / PLLM=6 / PLLN=168 / PLLP=2)
- **Flash**：1MB / **RAM**：128KB

### 引脚分配与外设

| 外设 | 引脚 | 功能 | 配置参数 |
|------|------|------|----------|
| **OLED** (I2C1) | PB8=SCL, PB9=SDA | SSD1306 128x64 显示 | 100kHz, 7bit |
| **BLE** (USART1) | PA9=TX, PA10=RX | E104-BT52 蓝牙模块 | 115200bps, 8N1 |
| **摇杆 X** (ADC1) | PA0=VRX | 摇杆 X 轴 (左右) | 12bit, 采样3周期 |
| **摇杆 Y** (ADC1) | PA1=VRY | 摇杆 Y 轴 (上下) | 12bit, 采样3周期 |
| **摇杆按钮** | PD5=KEY3 | 短按确认, 长按返回 | EXTI 上升沿, 上拉 |
| **按键** | PC7=KEY2 | 辅助按键 | EXTI 下降沿, 上拉 |
| **电机 PWM** (TIM2_CH3) | PA2 | 电机转速控制 | 4kHz (84MHz/21/2000) |
| **电机测速** | PD6 | 霍尔传感器脉冲 | EXTI 下降沿, 5脉冲/转 |
| **电机超时** (TIM7) | — | 1秒无脉冲清零 | 10Hz (84MHz/8400/500) |
| **音频 DAC** (DAC1) | PA4 | 音频模拟输出 | TIM6 触发, 8kHz |
| **音频时钟** (TIM6) | — | DAC 触发源 | 84MHz/21/125 = 32kHz |
| **SD 卡** (SDIO) | — | FatFS 文件系统 | 1-bit 模式, 24MHz |
| **DMA1_Stream5** | — | DAC 音频数据 | 存储器→外设, 半字, 循环 |
| **DMA2_Stream2** | — | USART1 RX | BLE 接收 |
| **DMA2_Stream3** | — | SDIO RX | SD 卡读取 |
| **DMA2_Stream6** | — | SDIO TX | SD 卡写入 |
| **DMA2_Stream7** | — | USART1 TX | BLE 发送 |

### 系统时钟树

```
HSE 8MHz → PLL (M=6, N=168, P=2) → 168MHz SYSCLK
  ├── HCLK = 168MHz
  ├── APB1 = 42MHz (PCLK1 /4) → TIM2/3/6/7
  └── APB2 = 84MHz (PCLK2 /2) → USART1, ADC1
```

---

## 代码架构

### 文件结构

```
Core/
├── Src/
│   ├── main.c                    # 主程序入口, 外设初始化, 主循环调度
│   ├── stm32f4xx_it.c            # 中断服务函数 (TIM7, EXTI9_5, USART1, DMA)
│   ├── adc.c / dac.c / dma.c     # CubeMX 生成的外设驱动
│   ├── i2c.c / sdio.c / tim.c    # 同上
│   ├── usart.c / gpio.c           # 同上
│   ├── oled.c / font.c           # OLED 驱动 + 字库
│   └── audio_player.c            # DAC+DMA 音频播放引擎
├── user_code/                    # 用户业务逻辑模块
│   ├── app_state.c/h             # 全局状态变量 + H743状态解析
│   ├── ui_menu.c/h               # OLED 所有界面绘制函数
│   ├── ui_input.c/h              # 摇杆/按键输入 + 菜单导航状态机
│   ├── motor_control.c/h         # PWM 调速 + 霍尔 RPM 测量
│   ├── ble_handler.c/h           # BLE 收发 + AT指令 + 文件传输
│   ├── audio_control.c/h         # 音频播放封装
│   └── sd_file.c/h               # SD 卡操作占位
└── Inc/
    └── main.h                    # 引脚宏定义 (KEY2/KEY3/VRX/VRY)
```

### 主循环调度流程

```
while (1):
  1. AudioControl_Poll()          — 音频 DMA 缓冲填充
  2. Input_ReadKey2()             — 读取 KEY2 状态
  3. DisplayRefresh()             — OLED 100ms 定时刷新
  4. BLE_ProcessReceive()         — 蓝牙行解析 + STATUS/LOAD 响应处理
  5. MotorControl_ProcessRamp()   — 电机占空比 3 秒渐变
  6. [ADC 内联读取]               — 摇杆 VRX/VRY + 方向检测 + 按钮读取
  7. Input_ProcessDeadZone()      — 摇杆归中死区检测 (150ms)
  8. Input_ProcessBrushDirection()— 画笔模式方向发送 (20ms 间隔)
  9. Input_ProcessNavigation()    — 非画笔模式方向导航
  10. Input_ProcessJoyButton()    — 摇杆按钮 短按确认 / 长按返回
  11. Input_ProcessKey2()         — KEY2 辅助功能
```

### 菜单状态机

```
menu_state:
  0  主菜单          ←→  Motor / Bluetooth / Audio / Interact
  1  电机控制        调速 + RPM 显示
  2  音频播放        选曲 + 播放
  3  蓝牙控制        STATUS 查询 / LOAD 文件列表
  4  交互子菜单      Donut / Brush
  5  甜甜圈控制      缩放(J+/J-) / 亮度 / 切色(JK) / 退出(JE)
  6  画笔子菜单      COLOR / DRAW
  7  COLOR 模式      摇杆方向 = JK 切色
  8  DRAW 模式       摇杆方向 = JL/JR/JU/JD 移动, 按钮 = JB1/JB0 Z轴
```

### 摇杆输入规范

| 方向 | 条件 | ADC 值 |
|------|------|--------|
| 上 (joy_up) | Y < 500 | ~0V |
| 下 (joy_down) | Y > 3500 | ~3.3V |
| 左 (joy_left) | X < 500 | ~0V |
| 右 (joy_right) | X > 3500 | ~3.3V |
| 中位 | 500 ≤ X,Y ≤ 3500 | ~1.65V |

### BLE 通信协议

- **模块**: E104-BT52，主模式连接 H743 (MAC: D5:6D:01:74:5B:21)
- **进入蓝牙菜单时**: 发送 `AT+TRANMD=1` 退出 AT 模式, `AT+LOGMSG=1` 开启日志
- **透传指令集**: 见项目根目录 `ble.txt`
- **接收方式**: USART1 中断逐字节接收 (`\n` 为行结束), 50ms 空闲超时判行完整
- **STATUS 解析**: 从 `RPM:xxx F:x S:x` 格式提取转速和状态

### 电机控制逻辑

- **调速**: 摇杆左右 → duty_permil ±10 (范围 0–1000), UpdatePWM() 写入 TIM2 CCR3
- **渐变**: 摇杆按钮短按 → 3秒线性渐变到 DUTY_RAMP_TARGET=840 (84.0%)
- **微调**: KEY2 短按 → duty_permil -1
- **测速**: PD6 下降沿 → 脉冲间隔 → RPM = 60000 / 5 / Δt(ms), 8窗口滑动平均
- **超时**: TIM7 每 100ms 检查, 1秒无脉冲 RPM 清零

### 音频播放逻辑

- **播放链**: SD卡(.raw) → f_read → 8bit→12bit转换 → DMA 双缓冲 → DAC1 → PA4 模拟输出
- **触发**: TIM6 TRGO @ 32kHz (84MHz/21/125), DAC 硬件触发
- **缓冲**: DMA 循环模式, 双半区 (各1024半字), 半传输/全传输中断触发填充
- **音量**: 原始 8bit 偏移转有符号, 增益后限幅, 映射到 12bit DAC

---

## 构建与烧录

- **构建系统**: CMake + Ninja + ARM GCC 14.3
- **编译**: `cmake --build build/Debug`
- **烧录**: ST-Link (OpenOCD), `.vscode/launch.json` 配置 Cortex-Debug
- **RAM**: ~26KB / **FLASH**: ~63KB

---

## BLE 指令速查

详见 `ble.txt`。常用指令：

```
系统:  :STATUS (查询)  :LIST (文件列表)  :LOAD <file> (加载)  :BRIGHT <n> (亮度)
甜甜圈: J+ J- (缩放)  JK (切色)  JE (退出)
画笔:   JL JR JU JD (移动)  JP (落笔)  JB0 JB1 (Z轴)  JK (切色)  :CLEAR (清屏)  JE (退出)
```

H743 每秒自动上报状态行: `RPM:xxx PPS:xxx HPS:xxx SKP:xxx BUS:xxx FILL:xxx F:x S:x ...`
