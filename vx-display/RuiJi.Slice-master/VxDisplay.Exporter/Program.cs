using System;
using System.IO;
using System.Text;
using Assimp;

namespace VxDisplay.Exporter
{
    class Program
    {
        static int Main(string[] args)
        {
            if (args.Length == 0)
            {
                PrintUsage();
                return 1;
            }

            var inputPaths = new System.Collections.Generic.List<string>();
            string outputPath = null;
            string upAxis = "Y";
            byte r = 255, g = 255, b = 255;
            bool cHeader = false;
            bool preview = false;
            bool shell = false;
            bool slices = false;
            int sliceCount = 100;
            int paletteBits = 8;
            string format = "v1";
            string rotateAxis = null;
            float rotateDeg = 0;

            for (int i = 0; i < args.Length; i++)
            {
                switch (args[i])
                {
                    case "-o":
                    case "--output":
                        outputPath = args[++i];
                        break;
                    case "-c":
                    case "--color":
                        var parts = args[++i].Split(',');
                        r = byte.Parse(parts[0]);
                        g = byte.Parse(parts[1]);
                        b = byte.Parse(parts[2]);
                        break;
                    case "-u":
                    case "--up":
                        upAxis = args[++i].ToUpper();
                        break;
                    case "--c-header":
                        cHeader = true;
                        break;
                    case "--slices":
                        slices = true;
                        break;
                    case "--slice-count":
                        sliceCount = int.Parse(args[++i]);
                        break;
                    case "--shell":
                        shell = true;
                        break;
                    case "--preview":
                        preview = true;
                        break;
                    case "-r":
                    case "--rotate":
                        {
                            string rotArg = args[++i];  // e.g. "X,90" or "Y,-90"
                            var rp = rotArg.Split(',');
                            string rotAxis = rp[0].ToUpper();
                            float rotDeg = float.Parse(rp[1]);
                            rotateAxis = rotAxis;
                            rotateDeg = rotDeg;
                        }
                        break;
                    case "-p":
                    case "--palette-bits":
                        paletteBits = int.Parse(args[++i]);
                        break;
                    case "--format":
                        format = args[++i].ToLower();
                        break;
                    default:
                        if (!args[i].StartsWith("-"))
                            inputPaths.Add(args[i]);
                        break;
                }
            }

            if (inputPaths.Count == 0)
            {
                Console.WriteLine("错误: 未指定输入模型文件");
                return 1;
            }

            foreach (var fp in inputPaths)
            {
                if (!File.Exists(fp))
                {
                    Console.WriteLine(string.Format("错误: 文件不存在: {0}", fp));
                    return 1;
                }
            }

            if (outputPath == null)
                outputPath = Path.ChangeExtension(inputPaths[0], ".bin");

            if (paletteBits != 8 && paletteBits != 24)
            {
                Console.WriteLine("错误: palette-bits 必须为 8 或 24");
                return 1;
            }

            try
            {
                Console.WriteLine("=== VxDisplay 3D模型体素化工具 ===");
                Console.WriteLine(string.Format("输入文件数: {0}", inputPaths.Count));
                foreach (var fp in inputPaths)
                    Console.WriteLine(string.Format("  - {0}", fp));
                Console.WriteLine(string.Format("输出文件: {0}", outputPath));
                Console.WriteLine(string.Format("实体颜色: R={0} G={1} B={2}", r, g, b));
                Console.WriteLine(string.Format("上轴: {0}", upAxis));
                if (rotateAxis != null)
                    Console.WriteLine(string.Format("预旋转: 绕{0}轴 {1}°", rotateAxis, rotateDeg));
                if (shell)
                    Console.WriteLine("外壳模式: 仅保留模型表面");
                Console.WriteLine(string.Format("调色板位深: {0} bits", paletteBits));

                // 加载所有模型，合并三角面
                Console.WriteLine("\n正在加载模型...");
                var exporter = new VolumeExporter();
                var context = new AssimpContext();

                // 第一个文件
                var scene0 = context.ImportFile(inputPaths[0],
                    PostProcessSteps.Triangulate |
                    PostProcessSteps.JoinIdenticalVertices |
                    PostProcessSteps.SortByPrimitiveType);
                exporter.LoadScene(scene0, upAxis);
                Console.WriteLine(string.Format("  文件 [{0}]: {1} 个网格", inputPaths[0], scene0.MeshCount));

                // 追加其余文件
                for (int f = 1; f < inputPaths.Count; f++)
                {
                    var sceneN = context.ImportFile(inputPaths[f],
                        PostProcessSteps.Triangulate |
                        PostProcessSteps.JoinIdenticalVertices |
                        PostProcessSteps.SortByPrimitiveType);
                    exporter.AppendScene(sceneN, upAxis);
                    Console.WriteLine(string.Format("  文件 [{0}]: {1} 个网格", inputPaths[f], sceneN.MeshCount));
                }

                // 预旋转（在包围盒计算之前）
                if (rotateAxis != null)
                    exporter.RotateModel(rotateAxis, rotateDeg);

                // 统一计算包围盒
                exporter.FinalizeBounds();

                Console.WriteLine("\n正在体素化...");
                var volume = exporter.Voxelize();

                if (shell)
                {
                    Console.WriteLine("\n正在掏空内部，仅保留外壳...");
                    volume = exporter.MakeHollow(volume);
                }

                int solidVoxels = 0;
                for (int i = 0; i < volume.Length; i++)
                    if (volume[i] != 0) solidVoxels++;

                float fillPct = 100.0f * solidVoxels / volume.Length;
                Console.WriteLine(string.Format("\n体素化完成: {0} / {1} 个体素被填充 ({2:F1}%)",
                    solidVoxels, volume.Length, fillPct));

                WriteBinFile(outputPath, volume, r, g, b, paletteBits);

                if (slices)
                {
                    if (format == "v2")
                    {
                        Console.WriteLine("\n正在导出1-bit预计算切片 (format=0x03, 50片)...");
                        sliceCount = 50;
                        var sliceData = exporter.ExportSlicesV2(volume, sliceCount);
                        string slicePath = Path.ChangeExtension(inputPaths[0], ".slices");
                        WriteSliceFileV2(slicePath, sliceData, sliceCount, 1, 32, 24, r, g, b);
                    }
                    else if (format == "v3")
                    {
                        Console.WriteLine("\n正在导出6-bit彩色预计算切片 (format=0x04, 50片)...");
                        sliceCount = 50;
                        byte[,] palette64 = DefaultPalette64();
                        var sliceData = exporter.ExportSlicesV3(volume, palette64, 64, sliceCount);
                        string slicePath = Path.ChangeExtension(inputPaths[0], ".slices");
                        WriteSliceFileV3(slicePath, sliceData, sliceCount, 1, 32, 24, palette64);
                    }
                    else
                    {
                        Console.WriteLine("\n正在导出预计算切片...");
                        var sliceData = exporter.ExportSlices(volume, r, g, b, 1, sliceCount);
                        string slicePath = Path.ChangeExtension(inputPaths[0], ".slices");
                        WriteSliceFile(slicePath, sliceData, sliceCount, 32, 24);
                    }
                }

                if (cHeader)
                {
                    string headerPath = Path.ChangeExtension(inputPaths[0], ".h");
                    WriteCHeader(headerPath, volume, r, g, b, paletteBits);
                }

                if (preview)
                {
                    string previewPath = Path.ChangeExtension(inputPaths[0], ".html");
                    WritePreviewHtml(previewPath, volume, r, g, b);
                }

                long fileSize = new FileInfo(outputPath).Length;
                Console.WriteLine(string.Format("\n完成! 输出: {0}", outputPath));
                Console.WriteLine(string.Format("文件大小: {0:F1} KB", fileSize / 1024.0f));

                return 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine(string.Format("\n错误: {0}", ex.Message));
                Console.WriteLine(ex.StackTrace);
                return 1;
            }
        }

        static void WriteBinFile(string path, byte[] indices, byte r, byte g, byte b, int paletteBits)
        {
            if (paletteBits == 24)
            {
                Console.WriteLine("警告: .bin 格式仅支持 8-bit 调色板模式, 已自动切换");
            }

            using (var fs = new FileStream(path, FileMode.Create))
            using (var bw = new BinaryWriter(fs))
            {
                // Header: "VXAN" magic (4B)
                bw.Write((byte)'V');
                bw.Write((byte)'X');
                bw.Write((byte)'A');
                bw.Write((byte)'N');

                // Dimensions (8B)
                int W = VolumeExporter.VOLUME_WIDTH;   // 32
                int H = VolumeExporter.VOLUME_HEIGHT;  // 24
                int D = VolumeExporter.VOLUME_DEPTH;   // 32
                bw.Write((ushort)W);
                bw.Write((ushort)D);
                bw.Write((ushort)H);
                bw.Write((ushort)1);   // total_frames = 1 (static model)

                // Palette (4B): 1 non-zero color, palette index 1
                bw.Write((byte)1);     // palette_count (excluding index 0)
                bw.Write(r);
                bw.Write(g);
                bw.Write(b);

                // Voxel data: z-major layout (z*D*W + y*W + x)
                bw.Write(indices, 0, indices.Length);
            }
        }

        static void WriteSliceFile(string path, byte[] data, int sliceCount, int strips, int leds)
        {
            using (var fs = new FileStream(path, FileMode.Create))
            using (var bw = new BinaryWriter(fs))
            {
                // Header: 16 bytes
                // 复用 VXAN 头布局: magic(4) + w(2) + h(2) + d(2) + frames(2) + pal(4)
                bw.Write((byte)'V');
                bw.Write((byte)'X');
                bw.Write((byte)'S');
                bw.Write((byte)'L');
                bw.Write((ushort)strips);           // width  = 32
                bw.Write((ushort)leds);             // height = 24
                bw.Write((ushort)0);                // depth  = 0 (保留)
                bw.Write((ushort)sliceCount);       // total_frames = 100
                bw.Write((byte)0x02);               // palette_count = 0x02 (state-packed)
                bw.Write((byte)0); bw.Write((byte)0); bw.Write((byte)0);

                bw.Write(data, 0, data.Length);
            }

            long fileSize = new FileInfo(path).Length;
            Console.WriteLine(string.Format("切片文件: {0}", path));
            Console.WriteLine(string.Format("  切片数: {0}, 每片: {1} 字节, 总大小: {2:F1} KB",
                             sliceCount, strips * leds * 3, fileSize / 1024.0f));
        }

        static void WriteSliceFileV2(string path, byte[] data, int sliceCount, int totalFrames,
                                      int strips, int leds, byte r, byte g, byte b)
        {
            using (var fs = new FileStream(path, FileMode.Create))
            using (var bw = new BinaryWriter(fs))
            {
                // Header: 16 bytes, format=0x03 (1-bit + palette)
                bw.Write((byte)'V');
                bw.Write((byte)'X');
                bw.Write((byte)'S');
                bw.Write((byte)'L');
                bw.Write((ushort)strips);           // width  = 32
                bw.Write((ushort)leds);             // height = 24
                bw.Write((ushort)sliceCount);       // depth  = 50 (>0 triggers anim mode)
                bw.Write((ushort)totalFrames);      // frames = N
                bw.Write((byte)0x03);               // format = 0x03 (1-bit voxel + palette)
                bw.Write(r);                        // palette_r
                bw.Write(g);                        // palette_g
                bw.Write(b);                        // palette_b

                bw.Write(data, 0, data.Length);
            }

            long fileSize = new FileInfo(path).Length;
            Console.WriteLine(string.Format("切片文件 (V2): {0}", path));
            Console.WriteLine(string.Format("  切片数: {0}, 帧数: {1}, 每片: {2} 字节, 总大小: {3:F1} KB",
                             sliceCount, totalFrames, strips * leds / 8, fileSize / 1024.0f));
        }

        static byte[,] DefaultPalette64()
        {
            // 64色 HSV 彩虹渐变: H 0-360, S=1, V=1, 跳过全黑(索引0)
            var pal = new byte[64, 3];
            pal[0, 0] = 0; pal[0, 1] = 0; pal[0, 2] = 0; // 索引0 = 灭
            for (int i = 1; i < 64; i++)
            {
                double hue = (i - 1) * 360.0 / 63.0;
                double s = 1.0, v = 1.0;
                int hi = (int)(hue / 60.0) % 6;
                double f = hue / 60.0 - hi;
                double p = v * (1 - s);
                double q = v * (1 - f * s);
                double t = v * (1 - (1 - f) * s);
                double r, g, b;
                switch (hi)
                {
                    case 0: r = v; g = t; b = p; break;
                    case 1: r = q; g = v; b = p; break;
                    case 2: r = p; g = v; b = t; break;
                    case 3: r = p; g = q; b = v; break;
                    case 4: r = t; g = p; b = v; break;
                    default: r = v; g = p; b = q; break;
                }
                pal[i, 0] = (byte)(r * 255);
                pal[i, 1] = (byte)(g * 255);
                pal[i, 2] = (byte)(b * 255);
            }
            return pal;
        }

        static void WriteSliceFileV3(string path, byte[] data, int sliceCount, int totalFrames,
                                      int strips, int leds, byte[,] palette)
        {
            int paletteCount = palette.GetLength(0); // 64
            using (var fs = new FileStream(path, FileMode.Create))
            using (var bw = new BinaryWriter(fs))
            {
                // Header: 16 bytes, format=0x04
                bw.Write((byte)'V');
                bw.Write((byte)'X');
                bw.Write((byte)'S');
                bw.Write((byte)'L');
                bw.Write((ushort)strips);           // width  = 32
                bw.Write((ushort)leds);             // height = 24
                bw.Write((ushort)sliceCount);       // depth  = 50
                bw.Write((ushort)totalFrames);      // frames = N
                bw.Write((byte)0x04);               // format = 0x04 (6-bit + 64色调色板)
                bw.Write((byte)0);                  // reserved
                bw.Write((ushort)paletteCount);     // 64

                // Palette: 64 × 3 bytes (R,G,B)
                for (int i = 0; i < paletteCount; i++)
                {
                    bw.Write(palette[i, 0]);
                    bw.Write(palette[i, 1]);
                    bw.Write(palette[i, 2]);
                }

                bw.Write(data, 0, data.Length);
            }

            long fileSize = new FileInfo(path).Length;
            int frameBytes = sliceCount * (strips * leds * 6 + 7) / 8;
            Console.WriteLine(string.Format("切片文件 (V3): {0}", path));
            Console.WriteLine(string.Format("  切片数: {0}, 帧数: {1}, 每帧: {2:F1}KB, 总大小: {3:F1}KB",
                             sliceCount, totalFrames, frameBytes / 1024.0f, fileSize / 1024.0f));
        }

        static void WriteCHeader(string path, byte[] indices, byte r, byte g, byte b, int paletteBits)
        {
            var sb = new StringBuilder();
            int W = VolumeExporter.VOLUME_WIDTH;
            int H = VolumeExporter.VOLUME_HEIGHT;
            int D = VolumeExporter.VOLUME_DEPTH;
            int solidCount = CountSolid(indices);

            sb.AppendLine("// VxDisplay Volume Data - Auto Generated");
            sb.AppendLine(string.Format("// Solid voxels: {0} / {1}", solidCount, indices.Length));
            sb.AppendLine();
            sb.AppendLine("#ifndef VX_VOLUME_DATA_H");
            sb.AppendLine("#define VX_VOLUME_DATA_H");
            sb.AppendLine();
            sb.AppendLine("#include <stdint.h>");
            sb.AppendLine();
            sb.AppendLine(string.Format("#define VX_VOLUME_WIDTH  {0}", W));
            sb.AppendLine(string.Format("#define VX_VOLUME_HEIGHT {0}", H));
            sb.AppendLine(string.Format("#define VX_VOLUME_DEPTH  {0}", D));
            sb.AppendLine();

            // 三维数组: g_volumeData[z][y][x]
            // z = LED高度方向 (0..23)
            // y = 体素空间Y轴 (0..31)
            // x = 体素空间X轴 (0..31)
            // 使用时: VolumeBuffer_SetVoxel(x, y, z, color)

            if (paletteBits == 8)
            {
                sb.AppendLine("// 8-bit palette index, 0=empty, 1=solid");
                sb.AppendLine(string.Format("static const uint8_t g_volumeData[{0}][{1}][{2}] = {{", H, D, W));

                for (int z = 0; z < H; z++)
                {
                    sb.AppendLine(string.Format("    {{ // z={0}", z));
                    for (int y = 0; y < D; y++)
                    {
                        sb.Append("        { ");
                        for (int x = 0; x < W; x++)
                        {
                            int idx = z * D * W + y * W + x;
                            sb.Append(string.Format("0x{0:X2}", indices[idx]));
                            if (x < W - 1) sb.Append(", ");
                        }
                        sb.Append(" }");
                        if (y < D - 1) sb.Append(",");
                        sb.AppendLine();
                    }
                    sb.Append("    }");
                    if (z < H - 1) sb.Append(",");
                    sb.AppendLine();
                }
                sb.AppendLine("};");
            }
            else
            {
                sb.AppendLine("// 24-bit RGB per voxel");
                sb.AppendLine(string.Format("static const uint8_t g_volumeData[{0}][{1}][{2}][3] = {{", H, D, W));

                for (int z = 0; z < H; z++)
                {
                    sb.AppendLine(string.Format("    {{ // z={0}", z));
                    for (int y = 0; y < D; y++)
                    {
                        sb.Append("        { ");
                        for (int x = 0; x < W; x++)
                        {
                            int idx = z * D * W + y * W + x;
                            byte vr = indices[idx] != 0 ? r : (byte)0;
                            byte vg = indices[idx] != 0 ? g : (byte)0;
                            byte vb = indices[idx] != 0 ? b : (byte)0;
                            sb.Append(string.Format("{{0x{0:X2},0x{1:X2},0x{2:X2}}}", vr, vg, vb));
                            if (x < W - 1) sb.Append(", ");
                        }
                        sb.Append(" }");
                        if (y < D - 1) sb.Append(",");
                        sb.AppendLine();
                    }
                    sb.Append("    }");
                    if (z < H - 1) sb.Append(",");
                    sb.AppendLine();
                }
                sb.AppendLine("};");
            }

            sb.AppendLine();
            sb.AppendLine("#endif // VX_VOLUME_DATA_H");

            File.WriteAllText(path, sb.ToString());
            Console.WriteLine(string.Format("C头文件: {0}", path));
        }

        static void WritePreviewHtml(string path, byte[] indices, byte r, byte g, byte b)
        {
            int W = VolumeExporter.VOLUME_WIDTH;
            int H = VolumeExporter.VOLUME_HEIGHT;
            int D = VolumeExporter.VOLUME_DEPTH;

            // 把体素坐标收集成JSON数组
            var voxels = new System.Text.StringBuilder();
            bool first = true;
            for (int z = 0; z < H; z++)
            {
                for (int y = 0; y < D; y++)
                {
                    for (int x = 0; x < W; x++)
                    {
                        int idx = z * D * W + y * W + x;
                        if (indices[idx] != 0)
                        {
                            if (!first) voxels.Append(",");
                            first = false;
                            voxels.Append(string.Format("[{0},{1},{2}]", x, y, z));
                        }
                    }
                }
            }

            string html = string.Format(@"<!DOCTYPE html>
<html><head><meta charset='utf-8'><title>VxDisplay Preview</title>
<style>body{{margin:0;overflow:hidden;background:#111;font-family:Arial}}
#info{{position:absolute;top:10px;left:10px;color:#888;font-size:14px}}</style></head>
<body>
<div id='info'>拖拽旋转 | 滚轮缩放 | 右键平移<br>体素空间: {0}×{1}×{2} | 填充: {3}</div>
<script src='https://cdn.jsdelivr.net/npm/three@0.128.0/build/three.min.js'></script>
<script src='https://cdn.jsdelivr.net/npm/three@0.128.0/examples/js/controls/OrbitControls.js'></script>
<script>
var voxels=[{4}];
var W={0},H={1},D={2},color=[{5},{6},{7}];
var scene=new THREE.Scene();scene.background=new THREE.Color(0x111111);
var camera=new THREE.PerspectiveCamera(45,W/H,0.1,200);
camera.position.set(W*2,D*2,H*1.5);
camera.lookAt(W/2,D/2,H/2);
var renderer=new THREE.WebGLRenderer({{antialias:true}});
renderer.setSize(window.innerWidth,window.innerHeight);
document.body.appendChild(renderer.domElement);
var controls=new THREE.OrbitControls(camera,renderer.domElement);
controls.target.set(W/2,D/2,H/2);
// 包围盒线框
var box=new THREE.Box3(new THREE.Vector3(0,0,0),new THREE.Vector3(W,D,H));
var boxGeo=new THREE.BoxGeometry(W,D,H);
var boxEdge=new THREE.EdgesGeometry(boxGeo);
var boxLine=new THREE.LineSegments(boxEdge,new THREE.LineBasicMaterial({{color:0x333333}}));
scene.add(boxLine);
// 地面参考线
var grid=new THREE.GridHelper(Math.max(W,D)*2,Math.max(W,D)*2,0x222222,0x111111);
grid.position.set(W/2,0,D/2);
scene.add(grid);
// 轴标
var ax=new THREE.AxesHelper(Math.max(W,D));
scene.add(ax);
// 体素点云 - 用小立方体
var mat=new THREE.MeshLambertMaterial({{color:new THREE.Color(color[0]/255,color[1]/255,color[2]/255)}});
var boxGeoSingle=new THREE.BoxGeometry(0.92,0.92,0.92);
for(var i=0;i<voxels.length;i++){{
    var v=voxels[i];
    var cube=new THREE.Mesh(boxGeoSingle,mat);
    cube.position.set(v[0]+0.5,v[1]+0.5,v[2]+0.5);
    scene.add(cube);
}}
// 光照
scene.add(new THREE.AmbientLight(0x444444));
var light=new THREE.DirectionalLight(0xffffff,1.5);
light.position.set(W,D*2,H*2);
scene.add(light);
var light2=new THREE.DirectionalLight(0x666666,0.8);
light2.position.set(-W,-D,-H);
scene.add(light2);
// 渲染循环
function animate(){{
    requestAnimationFrame(animate);
    controls.update();
    renderer.render(scene,camera);
}}
animate();
window.addEventListener('resize',function(){{
    camera.aspect=window.innerWidth/window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth,window.innerHeight);
}});
</script></body></html>",
                W, D, H,
                CountSolid(indices),
                voxels.ToString(),
                r, g, b);

            File.WriteAllText(path, html);
            Console.WriteLine(string.Format("预览文件: {0}  (浏览器打开即可查看3D模型)", path));
        }

        static int CountSolid(byte[] indices)
        {
            int count = 0;
            for (int i = 0; i < indices.Length; i++)
                if (indices[i] != 0) count++;
            return count;
        }

        static void PrintUsage()
        {
            Console.WriteLine(@"
VxDisplay 3D模型体素化工具
===========================

用法:
  VxDisplay.Exporter.exe <模型1> [模型2] [模型3...] [选项]

选项:
  -o, --output <path>    输出文件路径 (默认: 第一个输入文件名.bin)
  -c, --color <R,G,B>    实体颜色，逗号分隔 (默认: 255,255,255)
  -u, --up <axis>        模型的""上""轴: Y, Z, X (默认: Y)
  --c-header             同时生成C头文件(.h)
  --preview              生成3D预览HTML文件 (浏览器打开查看)
  -r, --rotate <轴,角度>  预旋转模型, 如 -r X,90 绕X轴转90度立起来
  -p, --palette-bits N   调色板位深: 8 或 24 (默认: 8, .bin仅支持8-bit)

示例:
  VxDisplay.Exporter.exe model.stl --preview
  VxDisplay.Exporter.exe logo.stl -r X,90 -u Y --c-header  (扁平Logo立起来)
  VxDisplay.Exporter.exe part1.stl part2.stl part3.stl -o combined.bin --preview
  VxDisplay.Exporter.exe model.obj -u Z --c-header -p 24

支持格式: STL, FBX, OBJ, Collada, 3DS, PLY 等 (通过Assimp)
");
        }
    }
}
