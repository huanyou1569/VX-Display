#!/usr/bin/env python3
"""
gen_dna.py — 生成 DNA 双螺旋 V3 动画 .slices (v2: 细线版)

DNA 数学模型:
  链1: x = cx + R·cos(α), y = cy + R·sin(α), z = t
  链2: x = cx + R·cos(α+π), y = cy + R·sin(α+π), z = t
  其中 α = t·2π·圈数/高度 + 帧旋转角

骨架=1体素细曲线, 横档=细直线, 无填充球
输出 = V3 格式多帧 .slices (64色调色板, 6-bit per voxel)

用法:
  python gen_dna.py
  python gen_dna.py --radius 9 --turns 2.5 --frames 40 --rungs-per-turn 6 -o dna_v3.slices
"""

import struct
import math
import argparse
import sys

# =============================================================================
# 常量
# =============================================================================

VOL_X = 32
VOL_Y = 32
VOL_Z = 24
STRIPS = 32
LEDS   = 24
SLICES_PER_FRAME = 50
SLICE_BYTES = 576          # 768 voxels × 6 bits / 8
PALETTE_SIZE = 64
PALETTE_BYTES = PALETTE_SIZE * 3
FRAME_BYTES = SLICES_PER_FRAME * SLICE_BYTES

# =============================================================================
# 64 色调色板
# =============================================================================

def make_palette():
    palette = [(0, 0, 0)]  # idx 0 = 黑
    max_v = 35
    for i in range(1, 64):
        hue = (i - 1) / 63.0
        h = hue * 6.0
        c = 1.0
        x = c * (1.0 - abs(h % 2.0 - 1.0))
        if h < 1:   r, g, b = c, x, 0
        elif h < 2: r, g, b = x, c, 0
        elif h < 3: r, g, b = 0, c, x
        elif h < 4: r, g, b = 0, x, c
        elif h < 5: r, g, b = x, 0, c
        else:       r, g, b = c, 0, x
        palette.append((int(r * max_v), int(g * max_v), int(b * max_v)))
    return palette


# =============================================================================
# DNA 体素生成 (细线版)
# =============================================================================

def generate_dna_volume(frame, total_frames, params):
    """
    返回 3D 列表 volume[x][y][z] = 颜色索引 (0-63), 0=空
    骨架和横档均为单个体素细线, 不作球填充
    """
    R = params['radius']
    turns = params['turns']
    rungs_per_turn = params['rungs_per_turn']
    ci1 = params['backbone1_color']
    ci2 = params['backbone2_color']
    ci_rung = params['rung_color']
    pad = params.get('height_padding', 1)
    curve_steps = params.get('curve_steps', 300)  # 骨架采样点数

    z_min = pad
    z_max = VOL_Z - 1 - pad
    z_range = float(z_max - z_min)
    if z_range <= 0:
        z_min, z_max = 1, VOL_Z - 2
        z_range = float(z_max - z_min)

    rot = frame * 2.0 * math.pi / total_frames
    volume = [[[0] * VOL_Z for _ in range(VOL_Y)] for _ in range(VOL_X)]

    def set_voxel(x, y, z, ci):
        ix = int(round(x))
        iy = int(round(y))
        iz = int(round(z))
        if 0 <= ix < VOL_X and 0 <= iy < VOL_Y and 0 <= iz < VOL_Z:
            if volume[ix][iy][iz] == 0:
                volume[ix][iy][iz] = ci

    # --- 画细线 (3D Bresenham, 单个体素) ---
    def draw_line(x0, y0, z0, x1, y1, z1, ci):
        ix0, iy0, iz0 = int(round(x0)), int(round(y0)), int(round(z0))
        ix1, iy1, iz1 = int(round(x1)), int(round(y1)), int(round(z1))

        dx = abs(ix1 - ix0)
        dy = abs(iy1 - iy0)
        dz = abs(iz1 - iz0)
        sx = 1 if ix1 > ix0 else -1
        sy = 1 if iy1 > iy0 else -1
        sz = 1 if iz1 > iz0 else -1

        if dx >= dy and dx >= dz:
            err1 = 2 * dy - dx
            err2 = 2 * dz - dx
            cx, cy, cz = ix0, iy0, iz0
            while cx != ix1:
                set_voxel(cx, cy, cz, ci)
                if err1 > 0: cy += sy; err1 -= 2 * dx
                if err2 > 0: cz += sz; err2 -= 2 * dx
                err1 += 2 * dy; err2 += 2 * dz
                cx += sx
        elif dy >= dx and dy >= dz:
            err1 = 2 * dx - dy
            err2 = 2 * dz - dy
            cx, cy, cz = ix0, iy0, iz0
            while cy != iy1:
                set_voxel(cx, cy, cz, ci)
                if err1 > 0: cx += sx; err1 -= 2 * dy
                if err2 > 0: cz += sz; err2 -= 2 * dy
                err1 += 2 * dx; err2 += 2 * dz
                cy += sy
        else:
            err1 = 2 * dy - dz
            err2 = 2 * dx - dz
            cx, cy, cz = ix0, iy0, iz0
            while cz != iz1:
                set_voxel(cx, cy, cz, ci)
                if err1 > 0: cy += sy; err1 -= 2 * dz
                if err2 > 0: cx += sx; err2 -= 2 * dz
                err1 += 2 * dy; err2 += 2 * dx
                cz += sz
        set_voxel(ix1, iy1, iz1, ci)

    # --- 骨架: 数学曲线上采样点, 去重后设体素 ---
    backbone1_pts = []
    backbone2_pts = []

    for si in range(curve_steps + 1):
        t_frac = si / curve_steps
        z_float = z_min + t_frac * z_range
        alpha = t_frac * turns * 2.0 * math.pi + rot

        # 链 1
        x1 = 16.0 + R * math.cos(alpha)
        y1 = 16.0 + R * math.sin(alpha)
        backbone1_pts.append((x1, y1, round(z_float)))

        # 链 2
        x2 = 16.0 + R * math.cos(alpha + math.pi)
        y2 = 16.0 + R * math.sin(alpha + math.pi)
        backbone2_pts.append((x2, y2, round(z_float)))

    # 骨架去重后写体素
    seen1 = set()
    for x, y, z in backbone1_pts:
        ix, iy, iz = int(round(x)), int(round(y)), int(z)
        key = (ix, iy, iz)
        if key not in seen1:
            seen1.add(key)
            set_voxel(ix, iy, iz, ci1)

    seen2 = set()
    for x, y, z in backbone2_pts:
        ix, iy, iz = int(round(x)), int(round(y)), int(z)
        key = (ix, iy, iz)
        if key not in seen2:
            seen2.add(key)
            set_voxel(ix, iy, iz, ci2)

    # --- 横档: 每隔一段画细线连接两链 ---
    total_rungs = max(1, int(turns * rungs_per_turn))
    for ri in range(total_rungs + 1):
        t_frac = ri / max(total_rungs, 1)
        z_float = z_min + t_frac * z_range
        alpha = t_frac * turns * 2.0 * math.pi + rot

        x1 = 16.0 + R * math.cos(alpha)
        y1 = 16.0 + R * math.sin(alpha)
        x2 = 16.0 + R * math.cos(alpha + math.pi)
        y2 = 16.0 + R * math.sin(alpha + math.pi)

        draw_line(x1, y1, z_float, x2, y2, z_float, ci_rung)

    # 统计
    count = 0
    for x in range(VOL_X):
        for y in range(VOL_Y):
            for z in range(VOL_Z):
                if volume[x][y][z]:
                    count += 1
    return volume, count


# =============================================================================
# 体素 → V3 切片采样
# =============================================================================

def sample_v3_slice(volume, phase):
    angle = 2.0 * math.pi * phase / 100.0
    cos_a = math.cos(angle)
    sin_a = math.sin(angle)

    voxels = [0] * (STRIPS * LEDS)
    vi = 0
    for led in range(LEDS):
        for strip in range(STRIPS):
            dx_i = strip * 2.0 - 31.0
            sx = int(round(16.0 + dx_i * cos_a * 0.5))
            sy = int(round(16.0 - dx_i * sin_a * 0.5))
            if sx < 0: sx = 0
            elif sx >= VOL_X: sx = VOL_X - 1
            if sy < 0: sy = 0
            elif sy >= VOL_Y: sy = VOL_Y - 1
            voxels[vi] = volume[sx][sy][led]
            vi += 1

    packed = bytearray(SLICE_BYTES)
    pi = 0
    for i in range(0, 768, 4):
        v0 = voxels[i] & 0x3F
        v1 = voxels[i + 1] & 0x3F
        v2 = voxels[i + 2] & 0x3F
        v3 = voxels[i + 3] & 0x3F
        packed[pi]     = v0 | ((v1 & 0x03) << 6)
        packed[pi + 1] = ((v1 >> 2) & 0x0F) | ((v2 & 0x0F) << 4)
        packed[pi + 2] = ((v2 >> 4) & 0x03) | (v3 << 2)
        pi += 3
    return bytes(packed)


# =============================================================================
# V3 .slices 写出
# =============================================================================

def write_v3_slices(filename, frames_data, palette):
    n_frames = len(frames_data)
    header = struct.pack('<4sHHHHBBH',
        b'VXSL', STRIPS, LEDS, 0, n_frames, 0x04, 0, PALETTE_SIZE)

    pal_bytes = bytearray()
    for r, g, b in palette:
        pal_bytes.extend([r, g, b])

    frame_bytes = bytearray()
    for fi, (vol, _) in enumerate(frames_data):
        if fi % 10 == 0:
            print(f"  采样帧 {fi + 1}/{n_frames} ...")
        for phase in range(SLICES_PER_FRAME):
            frame_bytes.extend(sample_v3_slice(vol, phase))

    with open(filename, 'wb') as f:
        f.write(header)
        f.write(pal_bytes)
        f.write(frame_bytes)

    size_kb = (16 + PALETTE_BYTES + n_frames * FRAME_BYTES) / 1024
    print(f"\n  输出: {filename}  |  帧数: {n_frames}  |  大小: {size_kb:.1f} KB")


# =============================================================================
# 主入口
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='生成 DNA 双螺旋 V3 动画 (细线版)')
    parser.add_argument('--radius', type=float, default=9.0,
                        help='螺旋半径 (体素), 默认 9')
    parser.add_argument('--turns', type=float, default=2.5,
                        help='螺旋圈数, 默认 2.5')
    parser.add_argument('--rungs-per-turn', type=int, default=6,
                        dest='rungs_per_turn', help='每圈横档数, 默认 6')
    parser.add_argument('--frames', type=int, default=40,
                        help='动画帧数, 默认 40')
    parser.add_argument('--curve-steps', type=int, default=300,
                        dest='curve_steps', help='骨架采样密度, 默认 300')
    parser.add_argument('--height-padding', type=int, default=1,
                        dest='height_padding')
    parser.add_argument('--backbone1-color', type=int, default=16,
                        dest='backbone1_color', help='链1颜色索引, 默认 16(蓝)')
    parser.add_argument('--backbone2-color', type=int, default=48,
                        dest='backbone2_color', help='链2颜色索引, 默认 48(红)')
    parser.add_argument('--rung-color', type=int, default=32,
                        dest='rung_color', help='横档颜色索引, 默认 32(黄)')
    parser.add_argument('-o', '--output', default='dna_v3.slices')
    args = parser.parse_args()

    palette = make_palette()
    params = vars(args)

    print(f"DNA 双螺旋 (细线)  |  半径={args.radius}  圈数={args.turns}  帧={args.frames}")
    print(f"  横档/圈={args.rungs_per_turn}  骨架采样={args.curve_steps}")

    frames_data = []
    total_vox = 0
    for f in range(args.frames):
        if f % 10 == 0:
            print(f"  生成帧 {f + 1}/{args.frames} ...")
        vol, cnt = generate_dna_volume(f, args.frames, params)
        frames_data.append((vol, cnt))
        total_vox += cnt

    avg = total_vox / args.frames
    print(f"  每帧平均: {avg:.0f} 体素  (总 {total_vox})")
    print(f"  填充率: {avg / (32*32*24) * 100:.2f}%")
    write_v3_slices(args.output, frames_data, palette)
    print("  完成. 发送到 TF 卡后 :LOAD " + args.output)


if __name__ == '__main__':
    main()
