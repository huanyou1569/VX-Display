"""
STL → VXAN .bin 转换 —— 调用 RuiJi.Slice-master 的 C# 导出器。
"""

import os
import sys
import subprocess
import threading
from typing import Optional

_EXE_NAME = "VxDisplay.Exporter.exe"


def find_exe() -> Optional[str]:
    """自动查找 VxDisplay.Exporter.exe 路径。

    查找顺序:
      1. PyInstaller 打包后的目录 (sys._MEIPASS/exporter/)
      2. 开发环境中的项目相对路径
    """
    # PyInstaller 打包路径
    if getattr(sys, 'frozen', False):
        base = getattr(sys, '_MEIPASS', None)
        if base:
            path = os.path.join(base, "exporter", _EXE_NAME)
            if os.path.isfile(path):
                return path

    # 开发环境路径
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    for config in ("Release", "Debug"):
        path = os.path.join(
            project_root, "RuiJi.Slice-master",
            "VxDisplay.Exporter", "bin", config, _EXE_NAME,
        )
        if os.path.isfile(path):
            return path
    return None


def run(
    stl_path: str,
    output_path: str = "",
    rotate_axis: str = "",
    rotate_angle: float = 0.0,
    up_axis: str = "",
    color: str = "",
    shell: bool = False,
    slices: bool = False,
    on_done=None,
) -> None:
    """在后台线程中运行导出器。

    on_done(success: bool, message: str, bin_path: str | None)
    在主线程中调用。
    """

    # 在主线程中尽早校验，避免进子线程后才发现问题
    if on_done is None:
        return

    exe = find_exe()
    if exe is None:
        on_done(False, "未找到 VxDisplay.Exporter.exe，请先编译 C# 项目", None)
        return

    exe_dir = os.path.dirname(exe)

    if not output_path:
        output_path = os.path.splitext(stl_path)[0] + ".bin"

    cmd = [exe, stl_path]
    if output_path:
        cmd.extend(["-o", output_path])
    if rotate_axis and rotate_angle != 0.0:
        cmd.extend(["-r", f"{rotate_axis},{rotate_angle}"])
    if up_axis:
        cmd.extend(["-u", up_axis])
    if color:
        cmd.extend(["-c", color])
    if shell:
        cmd.append("--shell")
    if slices:
        cmd.append("--slices")

    def _worker():
        try:
            # 必须 cd 到 exe 所在目录，否则找不到 AssimpNet.dll 等依赖
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=120,
                cwd=exe_dir,
                # encoding: 使用系统默认编码处理中文输出
            )
        except subprocess.TimeoutExpired:
            on_done(False, "转换超时 (120秒)", None)
            return
        except FileNotFoundError:
            on_done(False, f"找不到导出器程序: {exe}", None)
            return
        except OSError as e:
            on_done(False, f"启动导出器失败: {e}", None)
            return

        if result.returncode != 0:
            err = result.stderr.strip() or result.stdout.strip() or "未知错误"
            on_done(
                False,
                f"转换失败 (exit={result.returncode})\n{err[:500]}",
                None,
            )
            return

        if not os.path.isfile(output_path):
            on_done(False, f"输出文件未生成: {os.path.basename(output_path)}", None)
            return

        size_kb = os.path.getsize(output_path) / 1024
        on_done(
            True,
            f"转换成功 — {os.path.basename(output_path)} ({size_kb:.1f} KB)",
            output_path,
        )

    t = threading.Thread(target=_worker, daemon=True, name="exporter")
    t.start()
