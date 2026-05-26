"""
colorize_slices.py  —— 静态.slices文件逐层炫彩着色

对 format=0x02 的 state-packed .slices 文件，保持亮灭不变，
将 24 层 LED 分别赋予不同颜色。

用法:
    python colorize_slices.py chiken.slices -o chiken_rainbow.slices
    python colorize_slices.py chiken.slices -o chiken_dna.slices --profile dna
    python colorize_slices.py chiken.slices -o chiken_custom.slices --colors colors.txt

颜色文件格式 (每行一个 LED, LED0→LED23):
    R,G,B
    ...
    24行, 值域 0-255, 自动等比压缩到 max-brightness (默认35)
"""

import struct
import argparse
import colorsys
import sys
import os

SLICE_COUNT = 100
LEDS = 24
GRB_BITS = 24
STATE_WORDS = 576       # 24 LEDs × 24 GRB bits
SLICE_SIZE = STATE_WORDS * 4   # 2304
HEADER_SIZE = 16
HEADER_FMT = "<4sHHHHBBBB"


def hsv_to_rgb(h, s, v):
    """h: 0-360, s: 0-1, v: 0-1 → (r, g, b) 0-255"""
    r, g, b = colorsys.hsv_to_rgb(h / 360.0, s, v)
    return int(r * 255), int(g * 255), int(b * 255)


def clamp_colors(colors, max_val):
    """等比压缩: 每通道最大不超过 max_val"""
    result = []
    for r, g, b in colors:
        peak = max(r, g, b, 1)
        if peak > max_val:
            r = r * max_val // peak
            g = g * max_val // peak
            b = b * max_val // peak
        result.append((r, g, b))
    return result


def generate_rainbow(leds=24):
    """全色环 HSV 彩虹, 0→360°"""
    colors = []
    for i in range(leds):
        h = i * 360 // leds
        colors.append(hsv_to_rgb(h, 1.0, 1.0))
    return colors


def generate_dna(leds=24):
    """DNA 科学渐变: 蓝(240°)→青→绿→黄→红(0°)"""
    colors = []
    for i in range(leds):
        h = 240 - i * 240 // (leds - 1)  # 240° → 0°
        colors.append(hsv_to_rgb(h, 1.0, 1.0))
    return colors


def generate_ocean(leds=24):
    """海洋渐变: 深蓝(220°)→青(180°)"""
    colors = []
    for i in range(leds):
        h = 220 - i * 40 // (leds - 1)
        colors.append(hsv_to_rgb(h, 0.9, 0.9))
    return colors


def generate_fire(leds=24):
    """火焰渐变: 黄(60°)→橙→红(0°)"""
    colors = []
    for i in range(leds):
        h = 60 - i * 60 // (leds - 1)
        colors.append(hsv_to_rgb(h, 1.0, 1.0))
    return colors


def generate_neon(leds=24):
    """霓虹: 品红↔青↔黄交替高饱和"""
    colors = []
    base_hues = [300, 180, 60, 120, 330, 200]  # 品红 青 黄 绿 粉 蓝
    for i in range(leds):
        h = base_hues[i % len(base_hues)]
        # 微调色相避免完全重复
        h = (h + (i // len(base_hues)) * 7) % 360
        colors.append(hsv_to_rgb(h, 1.0, 1.0))
    return colors


PROFILES = {
    "dna":     ("DNA科学渐变 (蓝→红)", generate_dna),
    "rainbow": ("全色环彩虹", generate_rainbow),
    "ocean":   ("海洋渐变 (深蓝→青)", generate_ocean),
    "fire":    ("火焰渐变 (黄→红)", generate_fire),
    "neon":    ("霓虹跳跃色", generate_neon),
}


def load_colors_from_file(path):
    """从文本文件读取24行 R,G,B"""
    colors = []
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split(',')
            if len(parts) >= 3:
                colors.append((int(parts[0]), int(parts[1]), int(parts[2])))
    if len(colors) != 24:
        print(f"错误: 颜色文件需要 24 行 (找到 {len(colors)} 行)")
        sys.exit(1)
    return colors


def process_slices(input_path, output_path, colors_24):
    """
    改写 state 数据: 每 LED 保持原有 strip mask, 替换为目标颜色。
    """
    with open(input_path, 'rb') as f:
        header = f.read(HEADER_SIZE)
        raw = bytearray(f.read())

    magic, strips, leds, depth, frames, fmt = struct.unpack(HEADER_FMT, header)[:6]

    if magic != b"VXSL":
        print(f"错误: 非 VXSL 文件")
        sys.exit(1)

    # V3 格式: 调色板 + 帧数据, 尺寸不同
    if fmt == 0x04:
        process_slices_v3(input_path, output_path, colors_24, header, raw)
        return

    # V1/V2 格式: 100片 × 2304B
    if len(raw) != SLICE_COUNT * SLICE_SIZE:
        print(f"错误: 数据大小不匹配 (期望 {SLICE_COUNT * SLICE_SIZE}, 实际 {len(raw)})")
        sys.exit(1)

    if fmt != 0x02:
        print(f"错误: 不支持 format=0x{fmt:02x}, 仅支持 0x02 和 0x04")
        sys.exit(1)

    total_changed = 0  # 统计实际着色的 (slice, LED) 数

    for s in range(SLICE_COUNT):
        slice_off = s * SLICE_SIZE

        for led in range(LEDS):
            led_off = slice_off + led * GRB_BITS * 4

            # 取 OR 得到该 LED 的 strip mask
            mask = 0
            for bit in range(GRB_BITS):
                mask |= struct.unpack_from('<I', raw, led_off + bit * 4)[0]

            if mask == 0:
                continue  # 该 LED 在此片无亮起体素, 跳过

            total_changed += 1

            # 清零 24 个 word
            for bit in range(GRB_BITS):
                struct.pack_into('<I', raw, led_off + bit * 4, 0)

            # 新颜色 → GRB
            r, g, b = colors_24[led]
            grb = (g << 16) | (r << 8) | b

            # 为每个活跃 GRB bit 写入 mask
            for bit in range(GRB_BITS):
                if grb & (1 << bit):
                    struct.pack_into('<I', raw, led_off + bit * 4, mask)

    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(raw)

    print(f"输出: {output_path}")
    print(f"  切片数: {SLICE_COUNT}, LED 层数: {LEDS}")
    print(f"  着色点: {total_changed} (每片平均 {total_changed // SLICE_COUNT} 层)")
    print(f"  着色方案: 见下方色表")
    print()
    print("  LED   R   G   B  预览")
    for i, (r, g, b) in enumerate(colors_24):
        bar = color_bar(r, g, b)
        print(f"  {i:2d}   {r:3d} {g:3d} {b:3d}  {bar}")


def process_slices_v3(input_path, output_path, colors_24, header, raw):
    """
    V3 (format=0x04) 调色板着色: 只需改写 64 色调色板, 体素数据不动。
    header: 16 字节
    raw: 调色板(192B) + 帧数据
    """
    palette_count = struct.unpack_from('<H', header, 14)[0]
    PALETTE_SIZE = palette_count * 3

    if len(raw) < PALETTE_SIZE:
        print(f"错误: V3 数据太小 ({len(raw)} 字节)")
        sys.exit(1)

    palette_data = bytearray(raw[:PALETTE_SIZE])
    frame_data = raw[PALETTE_SIZE:]

    # 从 24 层颜色生成 64 色调色板
    # 索引 0 = 灭(黑), 索引 1-63 = 24层颜色循环分布
    palette_data[0] = 0
    palette_data[1] = 0
    palette_data[2] = 0  # index 0 = off

    for i in range(1, palette_count):
        # 每个 pal 索引对应一个 LED 层 (循环)
        led = (i - 1) * 24 // (palette_count - 1)
        if led >= 24:
            led = 23
        r, g, b = colors_24[led]
        off = i * 3
        palette_data[off] = r
        palette_data[off + 1] = g
        palette_data[off + 2] = b

    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(palette_data)
        f.write(frame_data)

    total_size = HEADER_SIZE + PALETTE_SIZE + len(frame_data)
    print(f"输出(V3): {output_path}")
    print(f"  调色板: {palette_count} 色, 帧数据: {len(frame_data)} 字节")
    print(f"  总大小: {total_size / 1024:.1f} KB")
    print()
    print("  LED   R   G   B  预览")
    for i, (r, g, b) in enumerate(colors_24):
        bar = color_bar(r, g, b)
        print(f"  {i:2d}   {r:3d} {g:3d} {b:3d}  {bar}")


def color_bar(r, g, b):
    """终端色块: 用 ANSI true color 画小方块"""
    if r == 0 and g == 0 and b == 0:
        return "\033[48;2;0;0;0m  \033[0m"
    return f"\033[48;2;{r};{g};{b}m  \033[0m"


def main():
    parser = argparse.ArgumentParser(
        description="静态.slices文件逐层炫彩着色")
    parser.add_argument("input", nargs="?", help="输入 .slices 文件 (format=0x02)")
    parser.add_argument("-o", "--output", default=None, help="输出 .slices 文件")
    parser.add_argument("--profile", default="dna",
                        choices=list(PROFILES.keys()),
                        help="预设配色方案 (默认: dna)")
    parser.add_argument("--max-brightness", type=int, default=35,
                        help="单通道最大亮度 (默认: 35)")
    parser.add_argument("--colors", default=None,
                        help="自定义颜色文件 (24行 R,G,B, 覆盖 --profile)")
    parser.add_argument("--list-profiles", action="store_true",
                        help="列出所有预设配色")
    args = parser.parse_args()

    if args.list_profiles:
        print("预设配色方案:")
        for name, (desc, _) in PROFILES.items():
            print(f"  {name:10s}  {desc}")
        return

    if not args.input or not args.output:
        print("用法: colorize_slices.py <输入.slices> -o <输出.slices> [选项]")
        print("      colorize_slices.py --list-profiles")
        sys.exit(1)

    if not os.path.isfile(args.input):
        print(f"错误: 输入文件不存在: {args.input}")
        sys.exit(1)

    # 生成颜色
    if args.colors:
        colors_24 = load_colors_from_file(args.colors)
    else:
        _, gen_fn = PROFILES[args.profile]
        colors_24 = gen_fn(24)

    # 等比压缩
    colors_24 = clamp_colors(colors_24, args.max_brightness)

    process_slices(args.input, args.output, colors_24)


if __name__ == "__main__":
    main()
