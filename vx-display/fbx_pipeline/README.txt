FBX 动画 → 多帧 .slices 制作流程
====================================

## 第1步: 从 FBX 导出逐帧 STL

方式A — Blender 脚本批量导出 (推荐):
  blender model.fbx --background --python export_frames.py -- --fps 10

  输出: frames_stl/frame_000.stl, frame_001.stl, ...

方式B — Blockbench 手动导出:
  File → Export → Export Model, 选 STL,
  逐帧移动时间轴, 每帧导出为 frame_000.stl, frame_001.stl, ...

## 第2步: 批量转换 STL → .slices

python batch_convert.py frames_stl/*.stl --shell -r X,90 -c 255,100,100 --slice-count 100

选项:
  --shell          仅保留外壳 (推荐, 体素更少)
  -r X,90          绕X轴旋转90度 (立起来)
  -c R,G,B         实体颜色
  --slice-count N  每帧N片 (默认100, 越小加载越快)
  --out-dir DIR    输出目录 (默认 slices_out)

## 第3步: 合并为动画 .slices

python merge_slices.py slices_out/*.slices -o anim.slices

## 第4步: 发送到 MCU

在上位机中:
  1. 连接蓝牙
  2. 文件传输 → 选择 anim.slices → 发送到设备
  3. 发送 :LOAD anim.slices 加载动画

MCU 端会每秒自动切换一帧。
