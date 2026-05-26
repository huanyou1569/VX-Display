"""
batch_convert.py  —— 批量调用 VxDisplay.Exporter.exe 将 STL → .slices。

用法:
    python batch_convert.py frame_*.stl --shell -r X,90 -c 255,100,100

选项与 VxDisplay.Exporter.exe 的切片导出相同。
输出文件命名为 <输入名>.slices，放在 --out-dir 指定目录。
"""

import subprocess
import sys
import argparse
import glob
import os
import shutil


def find_exporter():
    """查找 VxDisplay.Exporter.exe"""
    candidates = [
        os.path.join(os.path.dirname(__file__), "..", "RuiJi.Slice-master",
                     "VxDisplay.Exporter", "bin", "Release",
                     "VxDisplay.Exporter.exe"),
        os.path.join(os.path.dirname(__file__), "..", "RuiJi.Slice-master",
                     "VxDisplay.Exporter", "bin", "Debug",
                     "VxDisplay.Exporter.exe"),
    ]
    for c in candidates:
        c = os.path.abspath(c)
        if os.path.isfile(c):
            return c
    return None


def main():
    parser = argparse.ArgumentParser(description="批量 STL → .slices 转换")
    parser.add_argument("inputs", nargs="+", help="输入 STL 文件 (支持通配符)")
    parser.add_argument("--out-dir", default="slices_out",
                        help="输出目录 (默认: slices_out)")
    parser.add_argument("--exporter", help="VxDisplay.Exporter.exe 路径 (自动检测)")
    parser.add_argument("-r", "--rotate", default="X,90",
                        help="预旋转 (默认: X,90)")
    parser.add_argument("--slice-count", type=int, default=None,
                        help="每帧片数 (默认: v1=100, v2=50)")
    parser.add_argument("-c", "--color", default="255,255,255",
                        help="实体颜色 R,G,B (默认: 255,255,255)")
    parser.add_argument("--shell", action="store_true",
                        help="仅保留外壳")
    parser.add_argument("--format", default="v1", choices=["v1", "v2", "v3"],
                        help="切片格式: v1=state-packed(100) v2=1-bit(50) v3=6-bit 64color(50)")
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    # 找 exporter
    exe = args.exporter or find_exporter()
    if not exe or not os.path.isfile(exe):
        print("错误: 找不到 VxDisplay.Exporter.exe")
        print("  请用 --exporter 指定路径")
        sys.exit(1)
    exe_dir = os.path.dirname(exe)
    print(f"Exporter: {exe}")

    # 展开输入文件, 排序
    files = []
    for pat in args.inputs:
        matches = glob.glob(pat) if "*" in pat or "?" in pat else [pat]
        files.extend(matches)
    files = sorted(set(f for f in files if os.path.isfile(f)))

    if not files:
        print("错误: 未找到输入文件")
        sys.exit(1)

    # 根据 format 自动选择默认 slice-count
    if args.slice_count is None:
        args.slice_count = 50 if args.format in ("v2", "v3") else 100

    print(f"输入: {len(files)} 个文件, 格式: {args.format}, 每帧: {args.slice_count} 片")

    # 创建输出目录
    os.makedirs(args.out_dir, exist_ok=True)

    import tempfile

    ok = 0
    fail = 0
    for i, fp in enumerate(files):
        fp = os.path.abspath(fp)  # 必须绝对路径, cwd 会切到导出器目录
        base = os.path.splitext(os.path.basename(fp))[0]
        out_path = os.path.join(os.path.abspath(args.out_dir), base + ".slices")

        if os.path.isfile(out_path):
            print(f"  [{i+1}/{len(files)}] 跳过 {base} (已存在)")
            ok += 1
            continue

        # 导出器: -o 控制 .bin 输出(扔 temp), .slices 生成在输入同目录
        slices_tmp = os.path.join(os.path.dirname(fp), base + ".slices")
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
            tmp_bin = tf.name

        cmd = [
            exe, fp,
            "-o", tmp_bin,
            "-r", args.rotate,
            "--slices",
            "--slice-count", str(args.slice_count),
            "-c", args.color,
            "--format", args.format,
        ]
        if args.shell:
            cmd.append("--shell")

        if args.verbose:
            print(f"  [{i+1}/{len(files)}] {base} ...")

        try:
            result = subprocess.run(
                cmd, cwd=exe_dir,
                capture_output=not args.verbose,
                text=True, timeout=120,
            )
            # 清理临时 .bin
            if os.path.isfile(tmp_bin):
                os.unlink(tmp_bin)

            if result.returncode != 0:
                print(f"  [{i+1}/{len(files)}] FAIL {base}")
                if not args.verbose:
                    for line in result.stderr.strip().splitlines():
                        print(f"    {line}")
                # 也要清理可能生成的 .bin
                for leftover in [tmp_bin, slices_tmp]:
                    if os.path.isfile(leftover):
                        os.unlink(leftover)
                fail += 1
            else:
                # 移动 .slices 到输出目录
                if os.path.isfile(slices_tmp):
                    shutil.move(slices_tmp, out_path)
                    print(f"  [{i+1}/{len(files)}] OK   {base}")
                    ok += 1
                else:
                    print(f"  [{i+1}/{len(files)}] FAIL {base} (slices 未生成)")
                    fail += 1
        except subprocess.TimeoutExpired:
            print(f"  [{i+1}/{len(files)}] TIMEOUT {base}")
            fail += 1
        except Exception as e:
            print(f"  [{i+1}/{len(files)}] ERROR {base}: {e}")
            fail += 1

    print(f"\n完成: {ok} 成功, {fail} 失败")
    if ok > 1:
        print(f"合并: python merge_slices.py {args.out_dir}/*.slices -o anim.slices")


if __name__ == "__main__":
    main()
