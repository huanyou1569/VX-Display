# VX-Display — 3D 体素 POV 旋转显示器

基于 STM32H743 的 **POV (视觉暂留) 旋转 LED 体积显示器**，可在 32×32×24 的体素空间中实时渲染 3D 图形。支持蓝牙无线传输模型、动画播放、3D 画笔、甜甜圈和贪吃蛇交互模式。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![MCU](https://img.shields.io/badge/MCU-STM32H743-orange.svg)](https://www.st.com)
[![Language](https://img.shields.io/badge/Language-C11-brightgreen.svg)]()

---

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

## 系统架构

```
┌─ PC 上位机 (Python tkinter) ─────────────────┐
│  BLE 连接 • 文件传输 • 3D 预览 • 键盘映射    │
└────────────── BLE/UART ───────────────────────┘
                       ↓
┌─ MCU 固件 (STM32H743) ───────────────────────┐
│                                                │
│  ┌─ 交互模式 ─────────────────────────────┐   │
│  │  画笔 · 甜甜圈 · 粒子球 · 贪吃蛇       │   │
│  └────────────── ↓ ───────────────────────┘   │
│  ┌─ 体素层 ───────────────────────────────┐   │
│  │  volume_buffer · volume_draw · volume_math  │
│  └────────────── ↓ ───────────────────────┘   │
│  ┌─ 渲染管线 ─────────────────────────────┐   │
│  │  50面采样 → state编码 → BSRR → DMA     │   │
│  └────────────── ↓ ───────────────────────┘   │
│  ┌─ WS2812 驱动 ──────────────────────────┐   │
│  │  TIM1/TIM2 DMA → 32路并行 → 768颗LED   │   │
│  └─────────────────────────────────────────┘   │
└────────────────────────────────────────────────┘
```

## 功能特性

### 交互模式

| 模式 | 说明 | 控制 |
|------|------|------|
| **甜甜圈** | 3D 环面，体素扫描填充，6色切换 | D/F 缩放，K 切色 |
| **粒子球** | Fibonacci 球面 200 粒子，呼吸动画 | D/F 缩放，K 切色 |
| **3D 画笔** | 霍尔相对坐标系，6 色颜料，画布持久化 | WASD 移动，P 落笔 |
| **贪吃蛇** | 3D 贪吃蛇，穿墙环绕，2×2×2 单元 | WASD 转向，N 开局 |
| **V3 动画** | 64 色调色板，多帧彩色动画，SD 卡异步加载 | :LOAD 切换文件 |

### 上位机 (Python)

- BLE 设备扫描与连接
- 3D 体素模型预览 (PPM 渲染)
- STL → .bin 模型转换 (调用 C# 导出器)
- TF 卡文件传输与管理
- 键盘模拟摇杆 (所有交互模式)
- 实时状态监控 (RPM/PPS/帧率)

### 动画生成工具

- `gen_dna.py` — DNA 双螺旋 V3 动画生成
- `colorize_slices.py` — 切片着色 (5 种预设)
- `batch_convert.py` — STL 批量转 .slices
- `merge_slices.py` — 多帧合并为动画
- `export_frames.py` — Blender FBX → 逐帧 STL

## 构建

### 前置条件

- **ARM GCC** 13.3+ (`arm-none-eabi-gcc`)
- **CMake** 4.2+ + **Ninja**
- **OpenOCD** 0.12.0+ (调试/烧录)
- **Python** 3.10+ (上位机 + 工具脚本)
- **STM32CubeMX** (外设配置，可选)

### MCU 固件

```bash
cd vx-display
cmake --preset Debug
cmake --build build/Debug
# 烧录
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "program build/Debug/vx-display.elf verify reset exit"
```

### 上位机

```bash
cd upper_computer
pip install -r requirements.txt
python main.py
```

### Blender → V3 动画

```bash
cd fbx_pipeline
# 1. FBX → 逐帧 STL
blender model.fbx --background --python export_frames.py -- --fps 10
# 2. STL → V3 .slices
python batch_convert.py frames_stl/*.stl --format v3 --shell -r X,0 -c 255,100,100
# 3. 合并动画
python merge_slices.py slices_out/*.slices -o anim_v3.slices
# 4. 传到 TF 卡 → :LOAD anim_v3.slices
```

## BLE 通信协议

UART4, 115200 8N1, ASCII 文本, 换行结尾。

### 系统指令

| 指令 | 说明 | 响应 |
|------|------|------|
| `:STATUS` | 查询转速 | `RPM:600 DLY:3834 F:1 S:1` |
| `:LIST` | 列出TF卡文件 | 文件名列表 + `END` |
| `:LOAD <文件>` | 加载文件 | `OK` 或 `ERR` |
| `:BRIGHT <n>` | 设置亮度 (1-100) | — |

### 交互指令

| 指令 | 甜甜圈 | 画笔 | 贪吃蛇 |
|------|--------|------|--------|
| `J+`/`J-` | 缩放 | — | — |
| `JL`/`JR`/`JU`/`JD` | — | 移动 | 转向 |
| `JK` | 切颜色 | 切颜色 | — |
| `JG` | — | — | 开局 |
| `JP` | — | 落笔 | — |
| `JB0`/`JB1` | — | Z轴切换 | — |
| `JH<n>` | — | 视角偏移 | — |
| `JE` | 退出 | 退出 | 退出 |
| `:BRUSH` | — | 进入画笔 | — |
| `:CLEAR` | — | 清画布 | — |
| `:SNAKE` | — | — | 进入蛇模式 |

完整指令参考见 [ble.txt](ble.txt)。

## 项目结构

```
vx-display/
├── Core/
│   ├── Inc/                     # HAL 外设头文件 (CubeMX)
│   ├── Src/
│   │   ├── main.c               # 主程序入口
│   │   ├── stm32h7xx_it.c       # 中断处理
│   │   └── ...                  # 外设初始化
│   └── user_code/               # 用户逻辑模块
│       ├── interaction_sphere.c # 甜甜圈 + 粒子球
│       ├── brush_mode.c         # 3D 画笔模式
│       ├── snake_game.c         # 3D 贪吃蛇
│       ├── anim_player_v3.c     # V3 彩色动画播放
│       ├── slice_player.c       # 静态切片播放
│       ├── bt_commands.c        # 蓝牙指令处理
│       ├── volume_buffer.c      # 体素缓冲区
│       ├── volume_draw.c        # 体素绘制 (点/线/球)
│       ├── volume_to_ws2812.c   # 体素→LED 投影
│       ├── ws2812_driver.c      # WS2812 DMA 驱动
│       └── motor_control.c      # 电机稳速
├── Drivers/                     # CMSIS + HAL 驱动库
├── FATFS/                       # FatFS 文件系统
├── upper_computer/              # PC 上位机 (Python)
├── fbx_pipeline/                # 动画制作工具
├── ble.txt                      # BLE 指令完整参考
├── work.txt                     # 开发日志
└── CMakeLists.txt
```

## 渲染管线

```
TIM3 ISR (每 ~1ms):
  Snake_RenderNext → Brush_RenderNext → IS_RenderNext
    → AnimV3_RenderNext → ... → WS2812_ShowFromSlice()
      → fill_wave_from_state() → BSRR编码 → DMA → GPIO

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

## 开源许可

MIT License

---

*作者: huanyou1569*
