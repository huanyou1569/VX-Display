"""
VX-Display 蓝牙上位机

通过 BLE 连接 STM32H743 LED POV 旋转显示设备，实现:
  - 体素模型 3D 预览
  - 体素模型无线传输 (.bin → MCU)
  - STL → .bin 转换（调用 C# 导出器）
  - TF 卡文件加载控制
  - 设备状态实时监控
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from datetime import datetime
import os

import protocol
import vxan
import exporter as exp
import preview as pv
from ble_client import BLEClient
#，单片机蓝牙地址
DEFAULT_DEVICE = "D5:6D:01:74:5B:21"


# =============================================================================
# 主窗口
# =============================================================================

class App:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("VX-Display 蓝牙上位机")
        self.root.geometry("680x680")
        self.root.minsize(520, 560)

        # 数据
        self._devices: list[tuple[str, str, int]] = []  # (名称, 地址, RSSI)
        self._vxan_info: vxan.VXANInfo | None = None
        self._vxan_data: bytes | None = None
        self._exporter_exe: str | None = exp.find_exe()

        self._ft_data: bytes | None = None       # 待发送到TF卡的文件数据
        self._ft_filename: str = ""              # 目标文件名
        self._ft_sending: bool = False           # 正在传输标志

        # 键盘模拟状态
        self._js_temp: int = 0                   # 色温 -128..127
        self._js_brightness: int = 50            # 亮度 1..100
        self._brush_shift: bool = False          # 画笔 Shift 状态
        self._brush_drawing: bool = False        # 画笔 画/移 模式
        self._brush_mode_active: bool = False    # 键盘映射: True=画笔
        self._snake_mode_active: bool = False    # 键盘映射: True=贪吃蛇

        # 预览窗口（单例）
        self._preview_win: pv.PreviewWindow | None = None

        # BLE 客户端
        self.ble = BLEClient(self.root)
        self.ble.on_notify = self._on_device_message
        self.ble.on_status = self._log_status
        self.ble.start()

        # 构建界面
        self._build_ui()

        # 定期轮询 BLE 队列
        self._poll()

        # 窗口关闭
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # =========================================================================
    # 界面构建
    # =========================================================================

    def _build_ui(self):
        pf = {"padx": 10, "pady": 4}

        # ===== 连接区 =====
        conn = ttk.LabelFrame(self.root, text="设备连接", padding=(8, 5))
        conn.pack(fill=tk.X, **pf)

        r1 = ttk.Frame(conn)
        r1.pack(fill=tk.X, pady=2)
        self._scan_btn = ttk.Button(r1, text="扫描设备", command=self._on_scan, width=10)
        self._scan_btn.pack(side=tk.LEFT, padx=2)
        self._device_var = tk.StringVar()
        self._device_combo = ttk.Combobox(
            r1, textvariable=self._device_var, state="readonly", width=36
        )
        self._device_combo.pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._connect_btn = ttk.Button(r1, text="连接", command=self._on_connect, width=7)
        self._connect_btn.pack(side=tk.LEFT, padx=1)
        self._disconnect_btn = ttk.Button(r1, text="断开", command=self._on_disconnect, width=7)
        self._disconnect_btn.pack(side=tk.LEFT, padx=1)
        self._disconnect_btn.configure(state=tk.DISABLED)

        self._conn_status = tk.StringVar(value="● 未连接")
        ttk.Label(conn, textvariable=self._conn_status).pack(anchor=tk.W, padx=6)

        # ===== 模型转换区 =====
        conv = ttk.LabelFrame(self.root, text="模型转换  (STL → bin)", padding=(8, 5))
        conv.pack(fill=tk.X, **pf)

        r2 = ttk.Frame(conv)
        r2.pack(fill=tk.X, pady=2)
        self._stl_path_var = tk.StringVar()
        ttk.Entry(r2, textvariable=self._stl_path_var, width=44).pack(
            side=tk.LEFT, padx=2, fill=tk.X, expand=True
        )
        ttk.Button(r2, text="选择STL", command=self._on_choose_stl, width=9).pack(side=tk.RIGHT, padx=2)

        r3 = ttk.Frame(conv)
        r3.pack(fill=tk.X, pady=2)
        ttk.Label(r3, text="旋转:").pack(side=tk.LEFT)
        self._rot_axis_var = tk.StringVar(value="")
        ttk.Combobox(
            r3, textvariable=self._rot_axis_var,
            values=["", "X", "Y", "Z"], state="readonly", width=3,
        ).pack(side=tk.LEFT, padx=2)
        self._rot_angle_var = tk.StringVar(value="0")
        ttk.Entry(r3, textvariable=self._rot_angle_var, width=6).pack(side=tk.LEFT, padx=2)

        ttk.Label(r3, text="  上轴:").pack(side=tk.LEFT, padx=(12, 0))
        self._up_axis_var = tk.StringVar(value="Y")
        ttk.Combobox(
            r3, textvariable=self._up_axis_var,
            values=["X", "Y", "Z"], state="readonly", width=3,
        ).pack(side=tk.LEFT, padx=2)

        ttk.Label(r3, text="  颜色:").pack(side=tk.LEFT, padx=(12, 0))
        self._color_var = tk.StringVar(value="255,255,255")
        ttk.Entry(r3, textvariable=self._color_var, width=14).pack(side=tk.LEFT, padx=2)

        self._shell_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            r3, text="仅外壳", variable=self._shell_var,
        ).pack(side=tk.LEFT, padx=(8, 0))
        self._slices_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            r3, text="切片(.slices)", variable=self._slices_var,
        ).pack(side=tk.LEFT, padx=(4, 0))

        self._convert_btn = ttk.Button(r3, text="开始转换", command=self._on_convert, width=10)
        self._convert_btn.pack(side=tk.RIGHT, padx=2)

        self._conv_status = tk.StringVar()
        ttk.Label(conv, textvariable=self._conv_status, foreground="#888").pack(anchor=tk.W, padx=6)

        # ===== 体素传输区 =====
        xfer = ttk.LabelFrame(self.root, text="体素模型传输", padding=(8, 5))
        xfer.pack(fill=tk.X, **pf)

        r4 = ttk.Frame(xfer)
        r4.pack(fill=tk.X, pady=2)
        self._file_path_var = tk.StringVar()
        ttk.Entry(r4, textvariable=self._file_path_var, width=38).pack(
            side=tk.LEFT, padx=2, fill=tk.X, expand=True
        )
        ttk.Button(r4, text="选择bin", command=self._on_choose_file, width=9).pack(side=tk.RIGHT, padx=2)

        # 文件信息 —— 用多行 Label 显示，避免被截断
        self._file_info_var = tk.StringVar(value="未选择文件")
        ttk.Label(xfer, textvariable=self._file_info_var).pack(anchor=tk.W, padx=6, pady=(2, 4))

        # 按钮行
        r5 = ttk.Frame(xfer)
        r5.pack(fill=tk.X, pady=2)
        self._send_btn = ttk.Button(r5, text="发送到设备", command=self._on_send, width=12)
        self._send_btn.pack(side=tk.LEFT, padx=2)
        self._send_btn.configure(state=tk.DISABLED)
        self._preview_btn = ttk.Button(r5, text="预览模型", command=self._on_preview, width=10)
        self._preview_btn.pack(side=tk.LEFT, padx=4)
        self._preview_btn.configure(state=tk.DISABLED)
        self._progress_var = tk.IntVar()
        self._progress_bar = ttk.Progressbar(r5, variable=self._progress_var, maximum=100)
        self._progress_bar.pack(side=tk.LEFT, padx=8, fill=tk.X, expand=True)
        self._progress_label = ttk.Label(r5, text="")
        self._progress_label.pack(side=tk.RIGHT, padx=2)

        # ===== 文件传输到TF卡 =====
        ft = ttk.LabelFrame(self.root, text="文件传输到TF卡  (.slices / .bin)", padding=(8, 5))
        ft.pack(fill=tk.X, **pf)

        r_ft = ttk.Frame(ft)
        r_ft.pack(fill=tk.X, pady=2)
        self._ft_path_var = tk.StringVar()
        ttk.Entry(r_ft, textvariable=self._ft_path_var, width=34).pack(
            side=tk.LEFT, padx=2, fill=tk.X, expand=True
        )
        ttk.Button(r_ft, text="选择文件", command=self._on_choose_ft_file, width=9).pack(side=tk.RIGHT, padx=2)

        self._ft_info_var = tk.StringVar(value="选择 .slices 或 .bin 文件，发送到设备TF卡")
        ttk.Label(ft, textvariable=self._ft_info_var).pack(anchor=tk.W, padx=6, pady=(2, 4))

        r_ft2 = ttk.Frame(ft)
        r_ft2.pack(fill=tk.X, pady=2)
        self._ft_send_btn = ttk.Button(r_ft2, text="发送到TF卡", command=self._on_send_file, width=12)
        self._ft_send_btn.pack(side=tk.LEFT, padx=2)
        self._ft_send_btn.configure(state=tk.DISABLED)
        self._ft_progress_var = tk.IntVar()
        self._ft_progress_bar = ttk.Progressbar(r_ft2, variable=self._ft_progress_var, maximum=100)
        self._ft_progress_bar.pack(side=tk.LEFT, padx=8, fill=tk.X, expand=True)
        self._ft_progress_label = ttk.Label(r_ft2, text="")
        self._ft_progress_label.pack(side=tk.RIGHT, padx=2)

        # ===== TF 卡控制区 =====
        tf = ttk.LabelFrame(self.root, text="TF 卡控制", padding=(8, 5))
        tf.pack(fill=tk.X, **pf)

        r6 = ttk.Frame(tf)
        r6.pack(fill=tk.X, pady=2)
        self._tf_file_var = tk.StringVar()
        self._tf_entry = ttk.Entry(r6, textvariable=self._tf_file_var, width=34)
        self._tf_entry.pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._tf_load_btn = ttk.Button(r6, text="加载", command=self._on_load_tf, width=8)
        self._tf_load_btn.pack(side=tk.LEFT, padx=1)
        self._tf_list_btn = ttk.Button(r6, text="列出文件", command=self._on_list_tf, width=9)
        self._tf_list_btn.pack(side=tk.LEFT, padx=1)
        self._tf_load_btn.configure(state=tk.DISABLED)
        self._tf_list_btn.configure(state=tk.DISABLED)

        # ===== 快捷指令 =====
        cmd = ttk.LabelFrame(self.root, text="快捷指令  (:BRIGHT 50  /  :COLOR 255,0,0)", padding=(8, 5))
        cmd.pack(fill=tk.X, **pf)

        r_cmd = ttk.Frame(cmd)
        r_cmd.pack(fill=tk.X, pady=2)
        self._cmd_var = tk.StringVar()
        self._cmd_entry = ttk.Entry(r_cmd, textvariable=self._cmd_var, width=34)
        self._cmd_entry.pack(side=tk.LEFT, padx=2, fill=tk.X, expand=True)
        self._cmd_entry.bind("<Return>", lambda e: self._on_send_cmd())
        self._cmd_btn = ttk.Button(r_cmd, text="发送", command=self._on_send_cmd, width=8)
        self._cmd_btn.pack(side=tk.LEFT, padx=1)
        self._cmd_btn.configure(state=tk.DISABLED)

        # ===== 键盘摇杆 (调试用) =====
        js = ttk.LabelFrame(self.root, text="键盘摇杆  (调试: 代替底板)", padding=(8, 5))
        js.pack(fill=tk.X, **pf)

        js_info = (
            "  贪吃蛇: G=进入  WASD=转向  N=开局  E=退出\n"
            "  画笔: B=进入  W/A/S/D=移动  Shift=Z轴  P=落笔  C=清屏  []=视角  K=切色  E=退出\n"
            "  粒子球/甜甜圈: D=放大  F=缩小  K=切色  ←→=色温  ↑↓=亮度  E=退出"
        )
        ttk.Label(js, text=js_info, foreground="#666").pack(anchor=tk.W, padx=6)
        self._js_status_var = tk.StringVar(value="  画笔: 未激活  |  粒子球: 色温 0  亮度 50%")
        ttk.Label(js, textvariable=self._js_status_var, foreground="#06C").pack(anchor=tk.W, padx=6)

        # -- 键盘绑定: 画笔模式 --
        self._brush_shift = False
        self.root.bind('<KeyPress-w>',     lambda e: self._on_brush_move('U'))
        self.root.bind('<KeyPress-W>',     lambda e: self._on_brush_move('U'))
        self.root.bind('<KeyPress-s>',     lambda e: self._on_brush_move('D'))
        self.root.bind('<KeyPress-S>',     lambda e: self._on_brush_move('D'))
        self.root.bind('<KeyPress-a>',     lambda e: self._on_brush_move('L'))
        self.root.bind('<KeyPress-A>',     lambda e: self._on_brush_move('L'))
        self.root.bind('<KeyPress-d>',     lambda e: self._on_d_key())
        self.root.bind('<KeyPress-D>',     lambda e: self._on_d_key())
        self.root.bind('<KeyPress-p>',     lambda e: self._on_brush_pen())
        self.root.bind('<KeyPress-P>',     lambda e: self._on_brush_pen())
        self.root.bind('<KeyPress-c>',     lambda e: self._on_brush_clear())
        self.root.bind('<KeyPress-C>',     lambda e: self._on_brush_clear())
        self.root.bind('<KeyPress-b>',     lambda e: self._on_brush_enter())
        self.root.bind('<KeyPress-B>',     lambda e: self._on_brush_enter())
        self.root.bind('<KeyPress-Shift_L>',  lambda e: self._on_brush_shift(True))
        self.root.bind('<KeyPress-Shift_R>',  lambda e: self._on_brush_shift(True))
        self.root.bind('<KeyRelease-Shift_L>', lambda e: self._on_brush_shift(False))
        self.root.bind('<KeyRelease-Shift_R>', lambda e: self._on_brush_shift(False))
        self.root.bind('<bracketleft>',  lambda e: self._on_brush_angle(-25))
        self.root.bind('<bracketright>', lambda e: self._on_brush_angle(25))
        self.root.bind('<KeyPress-k>',   lambda e: self._on_color_cycle())
        self.root.bind('<KeyPress-K>',   lambda e: self._on_color_cycle())
        self.root.bind('<KeyPress-g>',   lambda e: self._on_snake_enter())
        self.root.bind('<KeyPress-G>',   lambda e: self._on_snake_enter())
        self.root.bind('<KeyPress-n>',   lambda e: self._on_snake_new())
        self.root.bind('<KeyPress-N>',   lambda e: self._on_snake_new())

        # -- 键盘绑定: 粒子球模式 (D/F/←→↑↓ 保留, D键与画笔共用需注意) --
        self.root.bind('<KeyPress-f>',     lambda e: self._on_js_ccw())
        self.root.bind('<KeyPress-F>',     lambda e: self._on_js_ccw())
        self.root.bind('<Left>',           lambda e: self._on_js_temp_step(-16))
        self.root.bind('<Right>',          lambda e: self._on_js_temp_step(16))
        self.root.bind('<Up>',             lambda e: self._on_js_brightness_step(10))
        self.root.bind('<Down>',           lambda e: self._on_js_brightness_step(-10))
        self.root.bind('<KeyPress-e>',     lambda e: self._on_js_exit())
        self.root.bind('<KeyPress-E>',     lambda e: self._on_js_exit())

        # ===== 日志区 =====
        log = ttk.LabelFrame(self.root, text="日志", padding=(8, 5))
        log.pack(fill=tk.BOTH, expand=True, padx=10, pady=(4, 6))

        self._log_text = tk.Text(
            log, height=8, state=tk.DISABLED,
            font=("Consolas", 10), wrap=tk.WORD,
        )
        sb = ttk.Scrollbar(log, command=self._log_text.yview)
        self._log_text.configure(yscrollcommand=sb.set)

        for tag, fg in [
            ("info", "#333"), ("status", "#888"), ("device", "#06C"),
            ("success", "#282"), ("error", "#C00"), ("header", "#333"),
        ]:
            self._log_text.tag_config(tag, foreground=fg)
        self._log_text.tag_config("header", font=("Consolas", 10, "bold"))

        self._log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb.pack(side=tk.RIGHT, fill=tk.Y)

        # 初始状态
        self._set_buttons_state("disconnected")

    # =========================================================================
    # 日志
    # =========================================================================

    def _log(self, text: str, tag: str = "info"):
        ts = datetime.now().strftime("%H:%M:%S")
        self._log_text.configure(state=tk.NORMAL)
        self._log_text.insert(tk.END, f"[{ts}] {text}\n", tag)
        self._log_text.configure(state=tk.DISABLED)
        self._log_text.see(tk.END)

    def _log_status(self, msg: str):
        self._log(msg, "status")

    def _on_device_message(self, text: str):
        for line in text.strip().splitlines():
            if line.strip():
                self._log(line.strip(), "device")

    # =========================================================================
    # 轮询
    # =========================================================================

    def _poll(self):
        self.ble.poll()
        self.root.after(100, self._poll)

    # =========================================================================
    # 按钮状态机
    # =========================================================================

    def _set_buttons_state(self, state: str):
        """disconnected | connected | transferring"""
        if state == "disconnected":
            self._scan_btn.configure(state=tk.NORMAL)
            self._connect_btn.configure(state=tk.NORMAL if self._devices else tk.DISABLED)
            self._disconnect_btn.configure(state=tk.DISABLED)
            self._convert_btn.configure(state=tk.NORMAL)
            self._send_btn.configure(state=tk.DISABLED)
            self._preview_btn.configure(state=tk.NORMAL if self._vxan_data else tk.DISABLED)
            self._ft_send_btn.configure(state=tk.DISABLED)
            self._tf_load_btn.configure(state=tk.DISABLED)
            self._tf_list_btn.configure(state=tk.DISABLED)
            self._cmd_btn.configure(state=tk.DISABLED)
        elif state == "connected":
            self._scan_btn.configure(state=tk.NORMAL)
            self._connect_btn.configure(state=tk.DISABLED)
            self._disconnect_btn.configure(state=tk.NORMAL)
            self._convert_btn.configure(state=tk.NORMAL)
            self._send_btn.configure(state=tk.NORMAL if self._vxan_data else tk.DISABLED)
            self._preview_btn.configure(state=tk.NORMAL if self._vxan_data else tk.DISABLED)
            self._ft_send_btn.configure(state=tk.NORMAL if self._ft_data else tk.DISABLED)
            self._tf_load_btn.configure(state=tk.NORMAL)
            self._tf_list_btn.configure(state=tk.NORMAL)
            self._cmd_btn.configure(state=tk.NORMAL)
        elif state == "transferring":
            self._scan_btn.configure(state=tk.DISABLED)
            self._connect_btn.configure(state=tk.DISABLED)
            self._disconnect_btn.configure(state=tk.NORMAL)
            self._convert_btn.configure(state=tk.DISABLED)
            self._send_btn.configure(state=tk.DISABLED)
            self._preview_btn.configure(state=tk.NORMAL if self._vxan_data else tk.DISABLED)
            self._ft_send_btn.configure(state=tk.DISABLED)
            self._tf_load_btn.configure(state=tk.DISABLED)
            self._tf_list_btn.configure(state=tk.DISABLED)
            self._cmd_btn.configure(state=tk.DISABLED)

    # =========================================================================
    # 扫描 / 连接 / 断开
    # =========================================================================

    def _on_scan(self):
        def _done(devices):
            if isinstance(devices, Exception):
                self._log(f"扫描失败: {devices}", "error")
                messagebox.showerror("扫描失败", str(devices))
                return
            self._devices = devices
            names = []
            default_idx = -1
            for i, (name, addr, rssi) in enumerate(devices):
                if rssi >= -40:      bar = "████"
                elif rssi >= -55:    bar = "███▌"
                elif rssi >= -70:    bar = "██▌ "
                elif rssi >= -85:    bar = "█▌  "
                else:                bar = "▌   "
                is_default = addr.upper() == DEFAULT_DEVICE
                if is_default:
                    label = f"★★ 单片机设备  {addr}"
                    if name:
                        label += f"  [{name}]"
                else:
                    label = f"  {bar} {addr}  ({rssi}dBm)"
                    if name:
                        label += f"  [{name}]"
                names.append(label)
                if is_default:
                    default_idx = i
            self._device_combo["values"] = names
            if names:
                if default_idx >= 0:
                    self._device_combo.current(default_idx)
                    self._log(f"发现 {len(devices)} 台设备, 默认设备已选中 (★★)", "success")
                else:
                    self._device_combo.current(0)
                    self._log(f"发现 {len(devices)} 台设备 (按信号排序)", "success")
                self._connect_btn.configure(state=tk.NORMAL)
            else:
                self._log("未发现蓝牙设备 — 请确认模块已上电", "status")
        self.ble.scan(_done)

    def _on_connect(self):
        sel = self._device_combo.current()
        if sel < 0 or sel >= len(self._devices):
            messagebox.showwarning("提示", "请先选择一个设备")
            return
        _name, address, _rssi = self._devices[sel]

        def _done(result):
            if isinstance(result, Exception):
                self._log(f"连接失败: {result}", "error")
                self._conn_status.set("● 连接失败")
                messagebox.showerror("连接失败", str(result))
                return
            self._conn_status.set("● 已连接")
            self._log("连接成功", "success")
            self._set_buttons_state("connected")

        self._conn_status.set("● 正在连接...")
        self.ble.connect(address, _done)

    def _on_disconnect(self):
        self.ble.disconnect()
        self._conn_status.set("● 未连接")
        self._log("已断开连接", "status")
        self._set_buttons_state("disconnected")

    # =========================================================================
    # 文件选择 & 预览
    # =========================================================================

    def _on_choose_file(self):
        path = filedialog.askopenfilename(
            title="选择 VXAN 体素模型文件",
            filetypes=[("VXAN 体素模型", "*.bin"), ("所有文件", "*.*")],
        )
        if not path:
            return
        self._load_bin(path)

    def _load_bin(self, path: str):
        try:
            info = vxan.parse(path)
        except (ValueError, OSError) as e:
            messagebox.showerror("文件错误", str(e))
            self._log(f"打开文件失败: {e}", "error")
            return

        self._vxan_info = info
        self._vxan_data = vxan.read_full(path)
        self._file_path_var.set(path)

        kb = info.file_size / 1024
        self._file_info_var.set(
            f"  {info.dimensions}  |  {info.total_frames} 帧  |  {kb:.1f} KB  |  "
            f"调色板 #{info.palette_count}  RGB({info.palette_r},{info.palette_g},{info.palette_b})"
        )
        basename = os.path.basename(path)
        self._log(f"已加载: {basename} ({kb:.1f} KB)", "info")

        self._update_preview()
        self._preview_btn.configure(state=tk.NORMAL)
        if self.ble.is_connected:
            self._send_btn.configure(state=tk.NORMAL)

    def _update_preview(self):
        """刷新预览窗口（如果打开了）。"""
        if self._preview_win is None or self._vxan_data is None or self._vxan_info is None:
            return
        frame = self._vxan_data[
            vxan.VXAN_HEADER_SIZE : vxan.VXAN_HEADER_SIZE + vxan.VXAN_FRAME_SIZE
        ]
        info = self._vxan_info
        self._preview_win.load(
            frame,
            file_palette_r=info.palette_r,
            file_palette_g=info.palette_g,
            file_palette_b=info.palette_b,
            file_palette_count=info.palette_count,
        )

    def _on_preview(self):
        """打开 / 刷新预览窗口。"""
        if self._vxan_data is None:
            return

        if self._preview_win is None or not self._preview_win.winfo_exists():
            self._preview_win = pv.PreviewWindow(self.root)
            self._preview_win.protocol(
                "WM_DELETE_WINDOW", self._on_preview_close
            )

        self._update_preview()
        self._preview_win.deiconify()
        self._preview_win.lift()

    def _on_preview_close(self):
        """预览窗口关闭回调。"""
        if self._preview_win:
            self._preview_win.destroy()
            self._preview_win = None

    # =========================================================================
    # 文件传输到TF卡
    # =========================================================================

    def _on_choose_ft_file(self):
        path = filedialog.askopenfilename(
            title="选择文件发送到设备TF卡",
            filetypes=[
                ("切片/体素文件", "*.slices *.bin"),
                ("切片文件", "*.slices"),
                ("VXAN 体素模型", "*.bin"),
                ("所有文件", "*.*"),
            ],
        )
        if not path:
            return
        try:
            with open(path, "rb") as f:
                self._ft_data = f.read()
        except OSError as e:
            messagebox.showerror("文件错误", str(e))
            self._log(f"读取文件失败: {e}", "error")
            return

        basename = os.path.basename(path)
        self._ft_filename = basename
        self._ft_path_var.set(path)
        kb = len(self._ft_data) / 1024
        self._ft_info_var.set(
            f"  {basename}  |  {kb:.1f} KB  |  就绪，点击'发送到TF卡'开始传输"
        )
        self._log(f"已选择: {basename} ({kb:.1f} KB)", "info")
        if self.ble.is_connected:
            self._ft_send_btn.configure(state=tk.NORMAL)

    def _on_send_file(self):
        if not self._ft_data or not self.ble.is_connected:
            return
        if self._ft_sending:
            return

        self._ft_sending = True
        self._set_buttons_state("transferring")
        self._ft_progress_var.set(0)
        self._ft_progress_label.configure(text="准备中...")
        self._log(f"开始发送文件: {self._ft_filename}", "header")

        def _progress(sent, total):
            self._ft_progress_var.set(int(sent * 100 / total))
            self._ft_progress_label.configure(text=f"{sent}/{total}")

        def _done(result):
            self._ft_sending = False
            self._set_buttons_state("connected")
            if isinstance(result, Exception):
                self._log(f"文件传输失败: {result}", "error")
                self._ft_progress_label.configure(text="失败")
                messagebox.showerror("传输失败", str(result))
                return
            self._ft_progress_var.set(100)
            self._ft_progress_label.configure(text="完成")
            self._log(
                f"文件已保存到TF卡: {self._ft_filename}  "
                f"(发送 :LOAD {self._ft_filename} 即可加载)", "success"
            )

        self.ble.send_file_to_device(
            self._ft_data, self._ft_filename, _progress, _done
        )

    # =========================================================================
    # 发送体素数据
    # =========================================================================

    def _on_send(self):
        if not self._vxan_data or not self.ble.is_connected:
            return

        self._set_buttons_state("transferring")
        self._progress_var.set(0)
        self._progress_label.configure(text="准备中...")
        self._log("开始发送体素数据...", "header")

        def _progress(sent, total):
            self._progress_var.set(int(sent * 100 / total))
            self._progress_label.configure(text=f"{sent}/{total}")

        def _done(result):
            self._set_buttons_state("connected")
            if isinstance(result, Exception):
                self._log(f"传输失败: {result}", "error")
                self._progress_label.configure(text="失败")
                messagebox.showerror("传输失败", str(result))
                return
            self._progress_var.set(100)
            self._progress_label.configure(text="完成")
            self._log("体素数据传输完成", "success")

        self.ble.send_voxel_data(self._vxan_data, _progress, _done)

    # =========================================================================
    # TF 卡指令
    # =========================================================================

    def _on_load_tf(self):
        filename = self._tf_file_var.get().strip()
        if not filename:
            messagebox.showwarning("提示", "请输入文件名，如 chiken.bin")
            return
        if not self.ble.is_connected:
            messagebox.showwarning("提示", "请先连接设备")
            return

        def _done(result):
            if isinstance(result, Exception):
                self._log(f"发送失败: {result}", "error")
                return
            self._log(f"已发送: :LOAD {filename}", "info")

        self.ble.send_command(protocol.cmd_load(filename), _done)

    def _on_list_tf(self):
        if not self.ble.is_connected:
            messagebox.showwarning("提示", "请先连接设备")
            return

        def _done(result):
            if isinstance(result, Exception):
                self._log(f"发送失败: {result}", "error")
                return
            self._log("已发送: :LIST", "info")

        self.ble.send_command(protocol.cmd_list(), _done)

    def _on_send_cmd(self):
        cmd = self._cmd_var.get().strip()
        if not cmd:
            return
        if not self.ble.is_connected:
            messagebox.showwarning("提示", "请先连接设备")
            return

        def _done(result):
            if isinstance(result, Exception):
                self._log(f"发送失败: {result}", "error")
                return
            self._log(f"已发送: {cmd}", "info")

        self.ble.send_command((cmd + "\n").encode("ascii"), _done)
        self._cmd_var.set("")

    # =========================================================================
    # 键盘模拟摇杆
    # =========================================================================

    def _on_js_cw(self):
        """D键: 顺时针旋转 → 粒子球放大"""
        if not self.ble.is_connected:
            return
        self.ble.send_command(b'J+\n')
        self._log("键盘: J+ (放大)", "status")

    def _on_js_ccw(self):
        """F键: 逆时针旋转 → 粒子球缩小"""
        if not self.ble.is_connected:
            return
        self.ble.send_command(b'J-\n')
        self._log("键盘: J- (缩小)", "status")

    def _on_js_temp_step(self, delta: int):
        """← → 键: 色温步进 (±16)"""
        if not self.ble.is_connected:
            return
        self._js_temp = max(-128, min(127, self._js_temp + delta))
        cmd = f"JT{self._js_temp}\n".encode("ascii")
        self.ble.send_command(cmd)
        self._js_status_var.set(
            f"  色温: {self._js_temp:+d}  |  亮度: {self._js_brightness}%"
        )
        self._log(f"键盘: JT{self._js_temp} (色温)", "status")

    def _on_js_brightness_step(self, delta: int):
        """↑ ↓ 键: 亮度步进 (±10)"""
        if not self.ble.is_connected:
            return
        self._js_brightness = max(1, min(100, self._js_brightness + delta))
        cmd = f":BRIGHT {self._js_brightness}\n".encode("ascii")
        self.ble.send_command(cmd)
        self._js_status_var.set(
            f"  色温: {self._js_temp:+d}  |  亮度: {self._js_brightness}%"
        )
        self._log(f"键盘: :BRIGHT {self._js_brightness} (亮度)", "status")

    def _on_js_exit(self):
        """E键: 退出互动模式 (画笔或粒子球)"""
        if not self.ble.is_connected:
            return
        self._brush_mode_active = False
        self._snake_mode_active = False
        self.ble.send_command(b'JE\n')
        self._log("键盘: JE (退出)", "status")

    # ---- 画笔模式键盘 ----

    def _on_d_key(self):
        """D键: 贪吃蛇/画笔=右移, 粒子球=放大"""
        if not self.ble.is_connected:
            return
        if self._snake_mode_active or self._brush_mode_active:
            self._on_brush_move('R')
        else:
            self._on_js_cw()

    def _on_brush_move(self, direction: str):
        """WASD: 画笔移动"""
        if not self.ble.is_connected:
            return
        cmd = f"J{direction}\n".encode("ascii")
        self.ble.send_command(cmd)
        plane = "Z轴" if self._brush_shift else "Y轴"
        self._js_status_var.set(
            f"  画笔: {plane}  {'画' if self._brush_drawing else '移'}模式  |  粒子球: 色温 {self._js_temp:+d}  亮度 {self._js_brightness}%"
        )

    def _on_brush_pen(self):
        """P键: 切换画/移模式"""
        if not self.ble.is_connected:
            return
        self._brush_drawing = not self._brush_drawing
        self.ble.send_command(b'JP\n')
        self._log(f"键盘: JP ({'画' if self._brush_drawing else '移'}模式)", "status")

    def _on_brush_clear(self):
        """C键: 清空画布"""
        if not self.ble.is_connected:
            return
        self.ble.send_command(b'CLEAR\n')
        self._log("键盘: CLEAR (清屏)", "status")

    def _on_brush_enter(self):
        """B键: 进入画笔模式"""
        if not self.ble.is_connected:
            return
        self.ble.send_command(b'BRUSH\n')
        self._brush_mode_active = True
        self._brush_drawing = False
        self._js_status_var.set(f"  画笔: 已激活  |  粒子球: 色温 {self._js_temp:+d}  亮度 {self._js_brightness}%")
        self._log("键盘: BRUSH (进入画笔)", "status")

    def _on_brush_shift(self, pressed: bool):
        """Shift键: 切换 Z/Y 轴"""
        if not self.ble.is_connected:
            return
        self._brush_shift = pressed
        cmd = b'JB1\n' if pressed else b'JB0\n'
        self.ble.send_command(cmd)
        self._log(f"键盘: {'JB1 (Z轴)' if pressed else 'JB0 (Y轴)'}", "status")

    def _on_brush_angle(self, delta: int):
        """[ ] 键: 旋转霍尔视角偏移 (±25相位=90°)"""
        if not self.ble.is_connected:
            return
        self._brush_angle = (getattr(self, '_brush_angle', 0) + delta) % 100
        cmd = f"JH{self._brush_angle}\n".encode("ascii")
        self.ble.send_command(cmd)
        self._log(f"键盘: JH{self._brush_angle} (视角偏移 {self._brush_angle*3.6:.0f}°)", "status")

    def _on_color_cycle(self):
        """K键: 切换颜色 (IS或Brush模式)"""
        if not self.ble.is_connected:
            return
        self.ble.send_command(b'JK\n')
        self._log("键盘: JK (切颜色)", "status")

    def _on_snake_enter(self):
        """G键: 进入贪吃蛇模式"""
        if not self.ble.is_connected:
            return
        self._brush_mode_active = False
        self._snake_mode_active = True
        self.ble.send_command(b'SNAKE\n')
        self._js_status_var.set("  贪吃蛇: 已激活  WASD=转向  G=开局  E=退出")
        self._log("键盘: SNAKE (进入贪吃蛇)", "status")

    def _on_snake_new(self):
        """N键: 贪吃蛇新游戏"""
        if not self.ble.is_connected:
            return
        self.ble.send_command(b'JG\n')
        self._log("键盘: JG (新游戏)", "status")

    # =========================================================================
    # STL 转换
    # =========================================================================

    def _on_choose_stl(self):
        path = filedialog.askopenfilename(
            title="选择 3D 模型文件",
            filetypes=[
                ("3D 模型", "*.stl *.obj *.fbx"),
                ("STL 文件", "*.stl"),
                ("所有文件", "*.*"),
            ],
        )
        if not path:
            return
        self._stl_path_var.set(path)
        basename = os.path.splitext(os.path.basename(path))[0]
        self._conv_status.set(f"就绪: {basename}")
        self._log(f"已选择: {os.path.basename(path)}", "info")

    def _on_convert(self):
        stl_path = self._stl_path_var.get().strip()
        if not stl_path:
            messagebox.showwarning("提示", "请先选择 STL 文件")
            return
        if self._exporter_exe is None:
            messagebox.showerror(
                "错误",
                "未找到 VxDisplay.Exporter.exe\n"
                "请确认路径: RuiJi.Slice-master/VxDisplay.Exporter/bin/Release/",
            )
            return

        self._convert_btn.configure(state=tk.DISABLED)
        self._conv_status.set("转换中...")

        rot = self._rot_axis_var.get().strip()
        try:
            ang = float(self._rot_angle_var.get().strip())
        except ValueError:
            ang = 0.0
            rot = ""
        up = self._up_axis_var.get().strip()
        color = self._color_var.get().strip()
        shell = self._shell_var.get()
        slices = self._slices_var.get()

        self._log(f"开始转换: {os.path.basename(stl_path)}", "header")

        def _done(success, message, bin_path):
            self._convert_btn.configure(state=tk.NORMAL)
            self._conv_status.set(message)
            tag = "success" if success else "error"
            self._log(message, tag)
            if success and bin_path:
                self._load_bin(bin_path)
            if not success:
                messagebox.showerror("转换失败", message)

        exp.run(stl_path, rotate_axis=rot, rotate_angle=ang, up_axis=up, color=color, shell=shell, slices=slices, on_done=_done)

    # =========================================================================
    # 生命周期
    # =========================================================================

    def _on_close(self):
        if self._preview_win and self._preview_win.winfo_exists():
            self._preview_win.destroy()
        self.ble.stop()
        self.root.destroy()

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    App().run()
