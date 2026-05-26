"""
VXAN .bin 文件读取与校验。

文件格式:
  偏移  大小  字段
  ──────────────────────────────
   0     4    magic    "VXAN"
   4     2    width    32
   6     2    height   32
   8     2    depth    24
  10     2    frames   总帧数
  12     1    pal_cnt  非零调色板数量
  13     1    pal_r    调色板[1] R
  14     1    pal_g    调色板[1] G
  15     1    pal_b    调色板[1] B
  16    N*24576  帧数据  每帧 32*32*24 字节（调色板索引）
"""

import os
import struct
from dataclasses import dataclass

VXAN_HEADER_SIZE = 16
VXAN_FRAME_SIZE = 24576  # 32 × 32 × 24
VXAN_MAGIC = b"VXAN"


@dataclass
class VXANInfo:
    """VXAN 文件信息，不含帧数据本身。"""
    file_path: str
    file_size: int
    width: int
    height: int
    depth: int
    total_frames: int
    palette_count: int
    palette_r: int
    palette_g: int
    palette_b: int

    @property
    def dimensions(self) -> str:
        return f"{self.width}×{self.height}×{self.depth}"

    @property
    def data_size(self) -> int:
        """帧数据总字节数。"""
        return self.total_frames * VXAN_FRAME_SIZE


def parse(path: str) -> VXANInfo:
    """读取并校验 VXAN 文件头。

    Raises:
        FileNotFoundError: 文件不存在
        ValueError: 格式不匹配或数据损坏
    """
    file_size = os.path.getsize(path)

    if file_size < VXAN_HEADER_SIZE:
        raise ValueError(f"文件太小: {file_size} 字节 (最小 {VXAN_HEADER_SIZE})")

    with open(path, "rb") as f:
        raw = f.read(VXAN_HEADER_SIZE)

    magic, w, h, d, frames, pal_cnt, pal_r, pal_g, pal_b = struct.unpack(
        "<4sHHHHBBBB", raw
    )

    if magic != VXAN_MAGIC:
        raise ValueError(f"不是VXAN格式文件 (magic={magic!r})")

    if w != 32 or h != 32 or d != 24:
        raise ValueError(f"尺寸不匹配: 期望32×32×24, 实际{w}×{h}×{d}")

    expected = VXAN_HEADER_SIZE + frames * VXAN_FRAME_SIZE
    if file_size < expected:
        raise ValueError(
            f"文件大小不匹配: 期望≥{expected}, 实际{file_size}"
        )

    return VXANInfo(
        file_path=path,
        file_size=file_size,
        width=w,
        height=h,
        depth=d,
        total_frames=frames,
        palette_count=pal_cnt,
        palette_r=pal_r,
        palette_g=pal_g,
        palette_b=pal_b,
    )


def read_full(path: str) -> bytes:
    """读取整个 VXAN 文件内容（含文件头和所有帧数据）。"""
    with open(path, "rb") as f:
        return f.read()
