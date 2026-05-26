"""
3D 体素预览 —— 独立窗口，鼠标旋转/缩放，纯 tkinter 无额外依赖。
"""

import math
import tkinter as tk
from typing import Optional

import palette as pal


class PreviewWindow(tk.Toplevel):
    """体素预览独立窗口 —— 可缩放、可拖拽旋转。"""

    DEFAULT_SIZE = 580
    MIN_SIZE = 350
    DOT_PX = 3                     # 每个体素的最小像素直径

    def __init__(self, parent):
        super().__init__(parent)
        self.title("3D 体素预览")
        self.geometry(f"{self.DEFAULT_SIZE}x{self.DEFAULT_SIZE}")
        self.minsize(self.MIN_SIZE, self.MIN_SIZE)
        self.configure(bg="#f0f0f0")

        self.canvas = tk.Canvas(
            self, bg="#ffffff", highlightthickness=0,
        )
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # 旋转角度 (弧度)
        self._azimuth = math.radians(30)
        self._elevation = math.radians(25)

        # 缩放因子 (值越大、模型占画布比例越大)
        self._zoom = 8.0
        self._zoom_min = 2.0
        self._zoom_max = 60.0

        # 体素数据
        self._voxels: list[tuple[int, int, int, int, int, int]] = []
        self._photo: Optional[tk.PhotoImage] = None

        # 鼠标状态
        self._drag_prev: Optional[tuple[int, int]] = None

        # 事件绑定
        self.canvas.bind("<Button-1>", self._on_press)
        self.canvas.bind("<B1-Motion>", self._on_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_release)
        self.canvas.bind("<MouseWheel>", self._on_wheel)
        self.bind("<Configure>", self._on_resize)

        # 初始提示
        self._draw_placeholder()

    # =========================================================================
    # 数据加载
    # =========================================================================

    def load(
        self,
        raw_data: bytes,
        file_palette_r: int = 0,
        file_palette_g: int = 0,
        file_palette_b: int = 0,
        file_palette_count: int = 0,
    ):
        """加载 VXAN 帧数据 (24576 字节) 并显示。

        raw_data:  32×32×24 调色板索引
        file_palette_*:  文件头中的调色板信息
        """
        voxels: list[tuple[int, int, int, int, int, int]] = []
        idx_offset = 0
        for z in range(24):
            for y in range(32):
                for x in range(32):
                    pi = raw_data[idx_offset]
                    idx_offset += 1
                    if pi == 0:
                        continue
                    r, g, b = pal.resolve_color_preview(
                        pi,
                        file_palette_r, file_palette_g, file_palette_b,
                        file_palette_count,
                    )
                    voxels.append((x, y, z, r, g, b))

        self._voxels = voxels
        self._redraw()

    # =========================================================================
    # 渲染
    # =========================================================================

    def _redraw(self, *_):
        """重新渲染。"""
        w = self.canvas.winfo_width()
        h = self.canvas.winfo_height()
        if w < 10 or h < 10:
            return

        pixels = bytearray(w * h * 3)
        depth = [float("-inf")] * (w * h)

        ca = math.cos(self._azimuth)
        sa = math.sin(self._azimuth)
        ce = math.cos(self._elevation)
        se = math.sin(self._elevation)
        scale = self._zoom * min(w, h) / 32.0  # 自适应窗口大小
        half_x = w / 2
        half_y = h / 2
        dot = max(1, self.DOT_PX * min(w, h) // self.DEFAULT_SIZE)

        ox, oy, oz = 15.5, 15.5, 11.5

        for x, y, z, r, g, b in self._voxels:
            nx = (x - ox) / 16.0
            ny = (y - oy) / 16.0
            nz = (z - oz) / 12.0

            x1 = nx * ca - ny * sa
            y1 = nx * sa + ny * ca
            y2 = y1 * ce - nz * se
            z2 = y1 * se + nz * ce

            sx = int(half_x + x1 * scale)
            sy = int(half_y - z2 * scale)

            if sx < 0 or sx >= w or sy < 0 or sy >= h:
                continue

            for dy in range(-dot, dot + 1):
                py = sy + dy
                if py < 0 or py >= h:
                    continue
                row_base = py * w
                for dx in range(-dot, dot + 1):
                    px = sx + dx
                    if px < 0 or px >= w:
                        continue
                    if z2 > depth[row_base + px]:
                        depth[row_base + px] = z2
                        pi = (row_base + px) * 3
                        pixels[pi] = r
                        pixels[pi + 1] = g
                        pixels[pi + 2] = b

        ppm = self._make_ppm(w, h, pixels)
        self._photo = tk.PhotoImage(data=ppm)
        self.canvas.delete("all")
        self.canvas.create_image(w // 2, h // 2, image=self._photo)

    @staticmethod
    def _make_ppm(w: int, h: int, pixels: bytearray) -> bytes:
        return f"P6\n{w} {h}\n255\n".encode("ascii") + bytes(pixels)

    def _draw_placeholder(self):
        self.canvas.delete("all")
        self.canvas.create_text(
            250, 250,
            text="加载 .bin 文件后\n在此显示 3D 预览",
            fill="#aaaaaa", font=("", 14),
            justify=tk.CENTER,
        )

    # =========================================================================
    # 交互
    # =========================================================================

    def _on_press(self, event):
        self._drag_prev = (event.x, event.y)

    def _on_drag(self, event):
        if self._drag_prev is None:
            return
        dx = event.x - self._drag_prev[0]
        dy = event.y - self._drag_prev[1]
        self._drag_prev = (event.x, event.y)

        self._azimuth += dx * 0.008
        self._elevation += dy * 0.008
        self._elevation = max(-math.pi / 2 + 0.01,
                              min(math.pi / 2 - 0.01, self._elevation))
        self._redraw()

    def _on_release(self, event):
        self._drag_prev = None

    def _on_wheel(self, event):
        factor = 1.12 if event.delta > 0 else 0.88
        self._zoom *= factor
        self._zoom = max(self._zoom_min, min(self._zoom_max, self._zoom))
        self._redraw()

    def _on_resize(self, event):
        if event.widget == self and self._voxels:
            self._redraw()
