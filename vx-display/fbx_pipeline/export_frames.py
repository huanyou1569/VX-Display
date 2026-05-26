"""
export_frames.py  —— Blender 批量导出动画帧为 STL。

用法 (Blender 命令行模式):
    blender model.fbx --background --python export_frames.py -- --fps 10 --out frames/

或在 Blender 内部:
    - 打开 FBX 文件
    - Scripting 工作区 → 粘贴本脚本 → Run Script

输出: frames/frame_000.stl, frame_001.stl, ...

依赖: Blender 2.8+ (自带 Python bpy)
"""

import bpy
import os
import sys
import argparse


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def import_fbx(path):
    """导入 FBX, 返回所有 mesh 对象的列表"""
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.context.selected_objects if o.type == "MESH"]


def export_stl(path):
    """导出当前场景所有 mesh 为单个 STL"""
    bpy.ops.object.select_all(action="DESELECT")
    for o in bpy.data.objects:
        if o.type == "MESH":
            o.select_set(True)
    bpy.ops.export_mesh.stl(
        filepath=path,
        use_selection=True,
        global_scale=1.0,
    )


def get_anim_range():
    """返回动画帧范围 (start, end)"""
    scene = bpy.context.scene
    return scene.frame_start, scene.frame_end


def export_frames(input_path, output_dir, fps, start=None, end=None):
    """
    从 FBX 文件逐帧导出 STL。

    input_path: FBX/GLTF 模型文件
    output_dir: STL 输出目录
    fps:        每秒采样帧数
    start/end:  时间轴范围 (帧号), 不指定则用整个动画
    """
    os.makedirs(output_dir, exist_ok=True)

    # 导入
    print(f"导入: {input_path}")
    meshes = import_fbx(input_path)
    if not meshes:
        print("错误: 未找到 mesh 对象")
        return

    scene = bpy.context.scene
    frame_start = start if start is not None else scene.frame_start
    frame_end = end if end is not None else scene.frame_end

    # 根据 FPS 计算采样帧号
    render_fps = scene.render.fps  # Blender 时间轴 FPS (默认 24)
    step = max(1, round(render_fps / fps))
    sample_frames = list(range(frame_start, frame_end + 1, step))
    if sample_frames[-1] != frame_end:
        sample_frames.append(frame_end)

    print(f"动画范围: {frame_start} - {frame_end} (共 {frame_end - frame_start + 1} 帧 @ {render_fps}fps)")
    print(f"采样: {len(sample_frames)} 帧 @ {fps}fps (步长={step})")

    for i, fno in enumerate(sample_frames):
        scene.frame_set(fno)
        out_path = os.path.join(output_dir, f"frame_{i:03d}.stl")
        export_stl(out_path)
        print(f"  [{i+1}/{len(sample_frames)}] 帧 {fno} → {out_path}")

    print(f"\n完成: {len(sample_frames)} 个 STL 文件导出到 {output_dir}")
    print(f"下一步: python batch_convert.py {output_dir}/*.stl --shell -r X,90")


def main():
    # Blender 的 -- 之后的参数才能被 argparse 拿到
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []

    parser = argparse.ArgumentParser(description="Blender FBX → 逐帧 STL")
    parser.add_argument("input", nargs="?", help="FBX 模型文件 (Blender 内部运行时可选)")
    parser.add_argument("--fps", type=int, default=10, help="输出帧率 (默认 10)")
    parser.add_argument("--out", "--out-dir", dest="out_dir",
                        default=r"C:\Users\1569\Desktop\stm32\vx-display\fbx_pipeline\frames_stl",
                        help="输出目录")
    parser.add_argument("--start", type=int, help="起始帧号")
    parser.add_argument("--end", type=int, help="结束帧号")
    args = parser.parse_args(argv)

    # 如果已经打开了文件 (Blender GUI 模式), 直接导出已有的 mesh
    if not args.input:
        meshes = [o for o in bpy.data.objects if o.type == "MESH"]
        if not meshes:
            print("错误: 场景中没有 mesh 对象。请先导入 FBX 文件。")
            print("用法: blender model.fbx --background --python export_frames.py -- --fps 10")
            return
        os.makedirs(args.out_dir, exist_ok=True)
        scene = bpy.context.scene
        frame_start = args.start if args.start is not None else scene.frame_start
        frame_end = args.end if args.end is not None else scene.frame_end
        render_fps = scene.render.fps
        step = max(1, round(render_fps / args.fps))
        sample_frames = list(range(frame_start, frame_end + 1, step))
        if sample_frames[-1] != frame_end:
            sample_frames.append(frame_end)

        print(f"动画范围: {frame_start} - {frame_end} (共 {frame_end - frame_start + 1} 帧 @ {render_fps}fps)")
        print(f"采样: {len(sample_frames)} 帧 @ {args.fps}fps (步长={step})")
        print(f"输出: {args.out_dir}")

        for i, fno in enumerate(sample_frames):
            scene.frame_set(fno)
            out_path = os.path.join(args.out_dir, f"frame_{i:03d}.stl")
            export_stl(out_path)
            print(f"  [{i+1}/{len(sample_frames)}] 帧 {fno} → {out_path}")

        print(f"\n完成: {len(sample_frames)} 个 STL 文件")
        print(f"下一步: python batch_convert.py {args.out_dir}/*.stl --shell -r X,90")
    elif args.input:
        clear_scene()
        export_frames(
            os.path.abspath(args.input),
            args.out_dir,
            args.fps,
            args.start, args.end,
        )
    else:
        print("用法:")
        print("  blender model.fbx --background --python export_frames.py -- --fps 10")
        print("或在 Blender 内打开 FBX 后运行本脚本")


if __name__ == "__main__":
    main()
