"""
MCU 调色板复刻 —— 与 sd_animation.c 中的 g_palette[256][3] 保持一致。

颜色渐变路径:
  0:    黑(灭)
  1-31: 纯绿 0→255
  32-63: 绿→青
  64-95: 青→蓝
  96-127: 蓝→紫→粉
  128-159: 粉→红
  160-191: 红→橙→黄
  192-224: 黄→白
  225-255: 白

亮度上限: PALETTE_MAX_BRIGHTNESS = 35 (与 MCU 端一致)
"""

from typing import Optional

PALETTE_MAX_BRIGHTNESS = 35

# 锚点 —— 每隔约 8 个调色板索引取一个关键色
_ANCHORS: list[tuple[int, int, int, int]] = [
    # (索引, R, G, B)
    (0,   0,   0,   0),
    (1,   0,   8,   0),
    (2,   0,  16,   0),
    (3,   0,  24,   0),
    (4,   0,  32,   0),
    (5,   0,  40,   0),
    (6,   0,  48,   0),
    (7,   0,  56,   0),
    (8,   0,  64,   0),
    (9,   0,  72,   0),
    (10,  0,  80,   0),
    (11,  0,  88,   0),
    (12,  0,  96,   0),
    (13,  0, 104,   0),
    (14,  0, 112,   0),
    (15,  0, 120,   0),
    (16,  0, 128,   0),
    (17,  0, 136,   0),
    (18,  0, 144,   0),
    (19,  0, 152,   0),
    (20,  0, 160,   0),
    (21,  0, 168,   0),
    (22,  0, 176,   0),
    (23,  0, 184,   0),
    (24,  0, 192,   0),
    (25,  0, 200,   0),
    (26,  0, 208,   0),
    (27,  0, 216,   0),
    (28,  0, 224,   0),
    (29,  0, 232,   0),
    (30,  0, 240,   0),
    (31,  0, 255,   0),
    (32,  0, 255,   8),
    (40,  0, 255,  72),
    (48,  0, 255, 136),
    (56,  0, 255, 200),
    (64,  0, 200, 255),
    (72,  0, 136, 255),
    (80,  0,  72, 255),
    (88,  0,   8, 255),
    (96, 32,   0, 255),
    (104,96,   0, 255),
    (112,160,  0, 255),
    (120,224,  0, 255),
    (128,255,  0, 200),
    (136,255,  0, 136),
    (144,255,  0,  72),
    (152,255,  0,   8),
    (160,255, 32,   0),
    (168,255, 96,   0),
    (176,255,160,   0),
    (184,255,224,   0),
    (192,255,255,   0),
    (200,255,255,  64),
    (208,255,255, 128),
    (216,255,255, 192),
    (224,255,255, 255),
]


def _lerp(a: int, b: int, t: float) -> int:
    return int(a + (b - a) * t + 0.5)


def _build_full_palette() -> list[tuple[int, int, int]]:
    """在锚点之间做线性插值，生成完整的 256 色调色板。"""
    # 每个索引的 RGB
    palette: list[Optional[tuple[int, int, int]]] = [None] * 256

    # 填入锚点
    for idx, r, g, b in _ANCHORS:
        palette[idx] = (r, g, b)

    # 在相邻锚点之间插值
    anchor_indices = sorted(a[0] for a in _ANCHORS)
    for i in range(len(anchor_indices) - 1):
        i0 = anchor_indices[i]
        i1 = anchor_indices[i + 1]
        r0, g0, b0 = palette[i0]  # type: ignore
        r1, g1, b1 = palette[i1]  # type: ignore
        for idx in range(i0 + 1, i1):
            t = (idx - i0) / (i1 - i0)
            palette[idx] = (
                _lerp(r0, r1, t),
                _lerp(g0, g1, t),
                _lerp(b0, b1, t),
            )

    # 224 之后全部白色
    for idx in range(225, 256):
        palette[idx] = (255, 255, 255)

    return palette  # type: ignore


# 全局调色板（模块加载时构建一次）
_BUILTIN_PALETTE: list[tuple[int, int, int]] = _build_full_palette()


def cap_brightness(v: int) -> int:
    """亮度上限映射 0-255 → 0-PALETTE_MAX_BRIGHTNESS。"""
    return (v * PALETTE_MAX_BRIGHTNESS + 127) // 255


def resolve_color(
    idx: int,
    file_r: int = 0,
    file_g: int = 0,
    file_b: int = 0,
    file_count: int = 0,
) -> tuple[int, int, int]:
    """将调色板索引解析为 RGB (0-35 亮度)。

    Args:
        idx: 调色板索引 (0-255)
        file_r/g/b: 文件调色板颜色 (对应索引 1)
        file_count: 文件调色板中的非零色数量

    Returns:
        (R, G, B) 每个通道 0-35
    """
    if idx == 1 and file_count >= 1:
        return (
            cap_brightness(file_r),
            cap_brightness(file_g),
            cap_brightness(file_b),
        )
    r, g, b = _BUILTIN_PALETTE[idx]
    return cap_brightness(r), cap_brightness(g), cap_brightness(b)


def resolve_color_preview(
    idx: int,
    file_r: int = 0,
    file_g: int = 0,
    file_b: int = 0,
    file_count: int = 0,
) -> tuple[int, int, int]:
    """同 resolve_color，但不去亮度上限，用于预览（0-255 全范围）。"""
    if idx == 1 and file_count >= 1:
        return (file_r, file_g, file_b)
    return _BUILTIN_PALETTE[idx]


def get_builtin(index: int) -> tuple[int, int, int]:
    """获取内置调色板第 index 项的设计值 (0-255, 未限幅)。主要用于调试。"""
    return _BUILTIN_PALETTE[index]
