# VX-Display — 3D 体素 POV 旋转显示系统

基于 STM32 的 **双 MCU 蓝牙无线 3D 体积显示系统**：STM32H743 旋转 LED 主显示器 + STM32F407 摇杆蓝牙遥控底板。

[![MCU](https://img.shields.io/badge/MCU-STM32H743-orange.svg)](https://www.st.com)
[![MCU](https://img.shields.io/badge/Remote-STM32F407-blue.svg)](https://www.st.com)
[![Language](https://img.shields.io/badge/Language-C11-brightgreen.svg)]()

---

## 系统总览

```
┌─ 上位机 (Python tkinter) ──────────────────────┐
│  BLE 连接 • 文件传输 • 3D 预览 • 键盘映射      │
└────────────── BLE ─────────────────────────────┘
         ↕                              ↕
┌─ F407 底板 (vx-diplay 遥控器) ──┐   ┌─ H743 主板 (vx-display 主显示器) ──┐
│  OLED 菜单 · 摇杆 · 音频 · 电机  │   │  32×32×24 体素 · 768 LED · POV    │
│  蓝牙主控 · 本地调速 · SD 卡播放 │   │  蓝牙从机 · 交互渲染 · TF 卡存储  │
└──────────────────────────────────┘   └─────────────────────────────────────┘
```

| 角色 | MCU | 主要功能 |
|------|-----|----------|
| **主显示器** | STM32H743 (Cortex-M7, 480MHz) | POV 旋转 LED 3D 渲染，体素空间 32×32×24 |
| **遥控底板** | STM32F407 (Cortex-M4, 168MHz) | 摇杆/按键蓝牙遥控，OLED 显示，音频播放，电机控制 |
| **上位机** | Python (PC) | 蓝牙文件传输，3D 预览，键盘模拟，C# 模型切片 |

---

# 一、主显示器 — vx-display (STM32H743)

## 硬件规格

| 参数 | 值 |
|------|-----|
| MCU | STM32H743 (Cortex-M7, 480MHz) |
| LED | 768 颗 WS2812B (32 路 × 24 颗/路) |
| 体素空间 | 32 × 32 × 24 (24576 体素) |
| 旋转相位 | 100 面/圈 (50 正向 + 50 __RBIT 反向) |
| 分辨率 | 3.6°/面 |
| 蓝牙 | E104-BT52 (BLE 5.0, UART4, 115200bps) |
| 存储 | TF 卡 (FatFS, SDMMC 1-bit) |
| 传感器 | 霍尔传感器 (旋转同步) |
| 驱动 | DMA (TIM1+TIM2 → GPIOB+GPIOE BSRR) |

## 交互模式

| 模式 | 说明 | 控制 |
|------|------|------|
| **甜甜圈** | 3D 环面，体素扫描填充，6色切换 | D/F 缩放，K 切色 |
| **粒子球** | Fibonacci 球面 200 粒子，呼吸动画 | D/F 缩放，K 切色 |
| **3D 画笔** | 霍尔相对坐标系，6 色颜料，画布持久化 | WASD 移动，P 落笔 |
| **贪吃蛇** | 3D 贪吃蛇，穿墙环绕，2×2×2 单元 | WASD 转向，N 开局 |
| **V3 动画** | 64 色调色板，多帧彩色动画，SD 卡异步加载 | :LOAD 切换文件 |

## 渲染管线

```
TIM3 ISR (每 ~1ms):
  Snake → Brush → Sphere → V3Animation → SlicePlayer
    → WS2812_ShowFromSlice() → BSRR编码 → DMA → GPIO

霍尔 EXTI (每圈):
  所有 OnHallEdge() → 相位归零 → 触发换帧

主循环:
  Update() → 生成下一帧 (后缓冲) → 霍尔换帧 (指针交换)
```

## 性能

| 指标 | 值 |
|------|-----|
| 最大转速 | ~700 RPM (Release) |
| 帧率 | ~11.7 fps (体素模式) |
| WS2812 DMA | ~741 µs/次 |
| 50 面展开 | ~16-20 ms |
| 双缓冲 | 225KB AXI SRAM |

## 项目结构

```
vx-display/
├── Core/
│   ├── Src/main.c                  # 主程序入口
│   ├── Src/stm32h7xx_it.c          # 中断处理
│   └── user_code/                  # 用户逻辑模块
│       ├── interaction_sphere.c    # 甜甜圈 + 粒子球
│       ├── brush_mode.c            # 3D 画笔模式
│       ├── snake_game.c            # 3D 贪吃蛇
│       ├── anim_player_v3.c        # V3 彩色动画播放
│       ├── bt_commands.c           # 蓝牙指令处理
│       ├── volume_buffer.c         # 体素缓冲区
│       ├── volume_to_ws2812.c      # 体素→LED 投影
│       ├── ws2812_driver.c         # WS2812 DMA 驱动
│       └── motor_control.c         # 电机稳速
├── upper_computer/                 # PC 上位机 (Python)
├── fbx_pipeline/                   # 动画制作工具
├── Drivers/                        # CMSIS + HAL 驱动库
├── FATFS/                          # FatFS 文件系统
└── CMakeLists.txt
```

---

# 二、遥控底板 — DAC+OLED+PWM(SDIO和TIM) (STM32F407)

## 硬件说明

### MCU

- **型号**：STM32F407VGT6 (Cortex-M4)
- **主频**：168MHz (HSE 8MHz / PLLM=6 / PLLN=168 / PLLP=2)

### 三大核心功能

| 功能 | 说明 |
|------|------|
| **蓝牙遥控 H743** | 通过 E104-BT52 BLE 模块发送指令控制体积显示器 |
| **电机转速控制** | TIM2 PWM 调速 + 霍尔测速，OLED 实时显示 Duty% 和 RPM |
| **音频播放** | 从 SD 卡读取 .raw 文件，DAC + TIM6 + DMA 播放 |

### 引脚分配

| 外设 | 引脚 | 功能 | 配置参数 |
|------|------|------|----------|
| OLED (I2C1) | PB8=SCL, PB9=SDA | SSD1306 128x64 显示 | 100kHz |
| BLE (USART1) | PA9=TX, PA10=RX | E104-BT52 蓝牙模块 | 115200bps |
| 摇杆 X/Y (ADC1) | PA0/PA1 | 摇杆双轴输入 | 12bit |
| 电机 PWM (TIM2_CH3) | PA2 | 电机转速控制 | 4kHz |
| 电机测速 | PD6 | 霍尔传感器脉冲 | 5脉冲/转 |
| 音频 DAC (DAC1) | PA4 | 音频模拟输出 | TIM6 触发, 8kHz |
| SD 卡 (SDIO) | — | FatFS 文件系统 | 1-bit, 24MHz |

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

### 代码架构

```
Core/
├── Src/
│   ├── main.c                    # 主程序入口, 外设初始化, 主循环调度
│   ├── audio_player.c            # DAC+DMA 音频播放引擎
│   └── oled.c / font.c           # OLED 驱动 + 字库
├── user_code/                    # 用户业务逻辑模块
│   ├── app_state.c/h             # 全局状态变量 + H743状态解析
│   ├── ui_menu.c/h               # OLED 所有界面绘制函数
│   ├── ui_input.c/h              # 摇杆/按键输入 + 菜单导航状态机
│   ├── motor_control.c/h         # PWM 调速 + 霍尔 RPM 测量
│   ├── ble_handler.c/h           # BLE 收发 + AT指令 + 文件传输
│   ├── audio_control.c/h         # 音频播放封装
│   └── sd_file.c/h               # SD 卡操作
└── Inc/
    └── main.h                    # 引脚宏定义
```

---

# 三、BLE 通信协议

UART, 115200 8N1, ASCII 文本, 换行结尾。

## 系统指令

| 指令 | 说明 | 响应 |
|------|------|------|
| `:STATUS` | 查询转速 | `RPM:600 DLY:3834 F:1 S:1` |
| `:LIST` | 列出TF卡文件 | 文件名列表 + `END` |
| `:LOAD <文件>` | 加载文件 | `OK` 或 `ERR` |
| `:BRIGHT <n>` | 设置亮度 (1-100) | — |

## 交互指令

| 指令 | 甜甜圈 | 画笔 | 贪吃蛇 |
|------|--------|------|--------|
| `J+`/`J-` | 缩放 | — | — |
| `JL`/`JR`/`JU`/`JD` | — | 移动 | 转向 |
| `JK` | 切颜色 | 切颜色 | — |
| `JG` | — | — | 开局 |
| `JP` | — | 落笔 | — |
| `JB0`/`JB1` | — | Z轴切换 | — |
| `JE` | 退出 | 退出 | 退出 |

完整指令参考见 `ble.txt`。H743 每秒自动上报状态行: `RPM:xxx PPS:xxx HPS:xxx SKP:xxx BUS:xxx FILL:xxx F:x S:x ...`

---

# 四、构建

### MCU 固件 (需 ARM GCC + CMake + Ninja)

```bash
# H743 主显示器
cd vx-display && cmake --preset Debug && cmake --build build/Debug

# F407 遥控底板
cd DAC+OLED+PWM(SDIO和TIM) && cmake --build build/Debug
```

### 上位机

```bash
cd vx-display/upper_computer
pip install -r requirements.txt
python main.py
```

### 动画制作 (FBX → V3 .slices)

```bash
cd vx-display/fbx_pipeline
blender model.fbx --background --python export_frames.py -- --fps 10
python batch_convert.py frames_stl/*.stl --format v3 --shell -r X,0 -c 255,100,100
python merge_slices.py slices_out/*.slices -o anim_v3.slices
```

---

# 五、模型切片工具

- **RuiJi.Slice** — C# 三维模型切片软件，切片结果通过蓝牙传输给 RuiJiHG 全息投影设备
- 演示: <https://www.bilibili.com/video/av50186992/>

---

## 开源许可

MIT License

---

*作者: huanyou1569*
