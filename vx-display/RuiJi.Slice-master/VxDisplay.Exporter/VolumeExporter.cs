using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using Assimp;

namespace VxDisplay.Exporter
{
    public class VolumeExporter
    {
        public const int VOLUME_WIDTH  = 32;
        public const int VOLUME_HEIGHT = 24;  // LED strip方向 (Z轴)
        public const int VOLUME_DEPTH  = 32;

        private struct Tri
        {
            public Vector3 V0, V1, V2;
        }

        private List<Tri> _tris;
        private float _scaleX, _scaleY, _scaleZ;
        private float _offsetX, _offsetY, _offsetZ;

        public void LoadScene(Scene scene, string upAxis = "Y")
        {
            _tris = new List<Tri>();
            AppendScene(scene, upAxis);
        }

        public void AppendScene(Scene scene, string upAxis = "Y")
        {
            if (!scene.HasMeshes)
                throw new Exception("模型中没有网格数据");

            int countBefore = _tris.Count;

            foreach (var mesh in scene.Meshes)
            {
                if (!mesh.HasFaces || !mesh.HasVertices)
                    continue;

                var verts = mesh.Vertices;
                foreach (var face in mesh.Faces)
                {
                    if (face.IndexCount < 3)
                        continue;

                    ExtractTriangles(verts, face, upAxis);
                }
            }

            Console.WriteLine(string.Format("  从该文件添加了 {0} 个三角面", _tris.Count - countBefore));
        }

        /// <summary>
        /// 绕模型空间指定轴旋转全部三角面（在包围盒计算之前调用）
        /// axis: "X", "Y", "Z"  angle: 角度(度)
        /// </summary>
        public void RotateModel(string axis, float degrees)
        {
            float rad = degrees * (float)Math.PI / 180.0f;
            Vector3 ax;
            switch (axis.ToUpper())
            {
                case "X": ax = new Vector3(1, 0, 0); break;
                case "Y": ax = new Vector3(0, 1, 0); break;
                case "Z": ax = new Vector3(0, 0, 1); break;
                default:  ax = new Vector3(1, 0, 0); break;
            }
            var m = System.Numerics.Matrix4x4.CreateFromAxisAngle(ax, rad);

            for (int i = 0; i < _tris.Count; i++)
            {
                var t = _tris[i];
                t.V0 = Vector3.Transform(t.V0, m);
                t.V1 = Vector3.Transform(t.V1, m);
                t.V2 = Vector3.Transform(t.V2, m);
                _tris[i] = t;
            }
            Console.WriteLine(string.Format("模型已旋转: 绕{0}轴 {1}°", axis.ToUpper(), degrees));
        }

        public void FinalizeBounds()
        {
            if (_tris.Count == 0)
                throw new Exception("没有有效的三角面");

            ComputeScaleAndOffset();

            Console.WriteLine(string.Format("共计 {0} 个三角面", _tris.Count));
            Console.WriteLine(string.Format("模型包围盒缩放: scale=({0:F3}, {1:F3}, {2:F3}), offset=({3:F3}, {4:F3}, {5:F3})",
                    _scaleX, _scaleY, _scaleZ, _offsetX, _offsetY, _offsetZ));
        }

        /// <summary>
        /// 射线投射体素化：对每个X-Y列射一条垂直射线(Z方向)，
        /// 统计穿过的三角面数量，奇偶规则判断内外
        /// </summary>
        public byte[] Voxelize()
        {
            var volume = new byte[VOLUME_WIDTH * VOLUME_DEPTH * VOLUME_HEIGHT];

            // 模型空间Y轴范围（射线从最低点到最高点）
            float modelYMin = float.MaxValue, modelYMax = float.MinValue;
            foreach (var t in _tris)
            {
                UpdateYRange(t.V0, ref modelYMin, ref modelYMax);
                UpdateYRange(t.V1, ref modelYMin, ref modelYMax);
                UpdateYRange(t.V2, ref modelYMin, ref modelYMax);
            }

            // 构建2D空间索引：按三角面在体素空间的XZ包围盒分组
            var spatial = new List<int>[VOLUME_WIDTH, VOLUME_DEPTH];
            for (int x = 0; x < VOLUME_WIDTH; x++)
                for (int y = 0; y < VOLUME_DEPTH; y++)
                    spatial[x, y] = new List<int>();

            for (int i = 0; i < _tris.Count; i++)
            {
                var t = _tris[i];
                float vx0 = t.V0.X * _scaleX + _offsetX;
                float vy0 = t.V0.Z * _scaleZ + _offsetZ;
                float vx1 = t.V1.X * _scaleX + _offsetX;
                float vy1 = t.V1.Z * _scaleZ + _offsetZ;
                float vx2 = t.V2.X * _scaleX + _offsetX;
                float vy2 = t.V2.Z * _scaleZ + _offsetZ;

                int xMin = Math.Max(0, (int)Math.Floor(Min3(vx0, vx1, vx2)));
                int xMax = Math.Min(VOLUME_WIDTH - 1, (int)Math.Floor(Max3(vx0, vx1, vx2)));
                int yMin = Math.Max(0, (int)Math.Floor(Min3(vy0, vy1, vy2)));
                int yMax = Math.Min(VOLUME_DEPTH - 1, (int)Math.Floor(Max3(vy0, vy1, vy2)));

                for (int gx = xMin; gx <= xMax; gx++)
                    for (int gy = yMin; gy <= yMax; gy++)
                        spatial[gx, gy].Add(i);
            }

            float rayLen = modelYMax - modelYMin;

            // 2×2 超采样偏移：每条射线若从柱中心穿过，可能错过厚度<1体素的薄壁
            float[] subOff = { 0.25f, 0.75f };

            for (int vy = 0; vy < VOLUME_DEPTH; vy++)
            {
                for (int vx = 0; vx < VOLUME_WIDTH; vx++)
                {
                    var filled = new bool[VOLUME_HEIGHT];

                    // 4条子射线，任一条判定为"内部"则该列填充
                    for (int so = 0; so < 4; so++)
                    {
                        float cx = vx + subOff[so & 1];
                        float cy = vy + subOff[so >> 1];

                        float rayX = (cx - _offsetX) / _scaleX;
                        float rayZ = (cy - _offsetZ) / _scaleZ;

                        var hits = new List<float>();
                        foreach (int ti in spatial[vx, vy])
                        {
                            float t;
                            if (RayTriangleIntersect(rayX, rayZ, modelYMin, _tris[ti], out t) && t >= 0 && t <= rayLen)
                            {
                                float vz = modelYMin * _scaleY + _offsetY + t * _scaleY;
                                hits.Add(vz);
                            }
                        }

                        if (hits.Count < 2)
                            continue;

                        hits.Sort();

                        for (int i = 0; i + 1 < hits.Count; i += 2)
                        {
                            int zStart = Math.Max(0, (int)Math.Ceiling(hits[i]));
                            int zEnd   = Math.Min(VOLUME_HEIGHT - 1, (int)Math.Floor(hits[i + 1]));

                            for (int vz = zStart; vz <= zEnd; vz++)
                                filled[vz] = true;
                        }
                    }

                    // 写入体素
                    for (int vz = 0; vz < VOLUME_HEIGHT; vz++)
                    {
                        if (filled[vz])
                        {
                            int idx = vz * VOLUME_WIDTH * VOLUME_DEPTH + vy * VOLUME_WIDTH + vx;
                            volume[idx] = 1;
                        }
                    }
                }

                if (vy % 8 == 0)
                    Console.WriteLine(string.Format("  列进度: {0}/{1}", vy + 1, VOLUME_DEPTH));
            }

            return volume;
        }

        /// <summary>
        /// 掏空模型内部，只保留外壳。
        /// 迭代剥离：每一轮检查每个体素的6个面邻居，若全部为实心则该体素为内部体素，将其掏空。
        /// 反复迭代直到没有更多内部体素，最终留下1体素厚的壳体。
        /// </summary>
        public byte[] MakeHollow(byte[] volume)
        {
            int W = VOLUME_WIDTH, D = VOLUME_DEPTH, H = VOLUME_HEIGHT;
            int WD = W * D;
            var result = new byte[volume.Length];
            Array.Copy(volume, result, volume.Length);

            bool changed = true;
            int pass = 0;
            while (changed)
            {
                changed = false;
                pass++;

                // 对上一轮结果做快照，本轮检查基于快照
                var prev = new byte[result.Length];
                Array.Copy(result, prev, result.Length);

                for (int z = 0; z < H; z++)
                {
                    for (int y = 0; y < D; y++)
                    {
                        for (int x = 0; x < W; x++)
                        {
                            int idx = z * WD + y * W + x;
                            if (prev[idx] == 0) continue;

                            // 六个面邻居全部为实心 → 内部 → 清除
                            int holes = 0;
                            if (x == 0      || prev[idx - 1]   == 0) holes++;
                            if (x == W - 1  || prev[idx + 1]   == 0) holes++;
                            if (y == 0      || prev[idx - W]   == 0) holes++;
                            if (y == D - 1  || prev[idx + W]   == 0) holes++;
                            if (z == 0      || prev[idx - WD]  == 0) holes++;
                            if (z == H - 1  || prev[idx + WD]  == 0) holes++;

                            if (holes == 0)
                            {
                                result[idx] = 0;
                                changed = true;
                            }
                        }
                    }
                }
            }

            Console.WriteLine(string.Format("  外壳剥离: {0} 次迭代完成", pass));
            return result;
        }

        /// <summary>
        /// 射线-三角面求交 (Möller–Trumbore)
        /// 射线: origin=(rayX, rayY0, rayZ), direction=(0, 1, 0), 参数t为沿Y轴的距离
        /// </summary>
        private bool RayTriangleIntersect(float rayX, float rayZ, float rayY0, Tri tri, out float t)
        {
            t = 0;

            float e1x = tri.V1.X - tri.V0.X, e1y = tri.V1.Y - tri.V0.Y, e1z = tri.V1.Z - tri.V0.Z;
            float e2x = tri.V2.X - tri.V0.X, e2y = tri.V2.Y - tri.V0.Y, e2z = tri.V2.Z - tri.V0.Z;

            // H = RayDir(0,1,0) × Edge2 = (e2z, 0, -e2x)
            float hx = e2z, hz = -e2x;

            // a = Edge1 · H = e1x*e2z - e1z*e2x
            float a = e1x * e2z - e1z * e2x;

            if (Math.Abs(a) < 0.0000001f)
                return false; // 射线平行于三角面

            float f = 1.0f / a;

            // S = RayOrigin - V0
            float sx = rayX - tri.V0.X;
            float sy = rayY0 - tri.V0.Y;
            float sz = rayZ - tri.V0.Z;

            // u = f * (S · H)
            float u = f * (sx * hx + sz * hz);
            if (u < -0.00001f || u > 1.00001f)
                return false;

            // Q = S × Edge1
            float qx = sy * e1z - sz * e1y;
            float qy = sz * e1x - sx * e1z;
            float qz = sx * e1y - sy * e1x;

            // v = f * (RayDir · Q) = f * qy   (因为RayDir=(0,1,0))
            float v = f * qy;
            if (v < -0.00001f || u + v > 1.00001f)
                return false;

            // t = f * (Edge2 · Q)
            t = f * (e2x * qx + e2y * qy + e2z * qz);

            return true;
        }

        private void ExtractTriangles(List<Vector3D> verts, Face face, string upAxis)
        {
            Vector3 v0, v1, v2;
            RemapAxes(verts[face.Indices[0]], verts[face.Indices[1]], verts[face.Indices[2]],
                      upAxis, out v0, out v1, out v2);

            _tris.Add(new Tri { V0 = v0, V1 = v1, V2 = v2 });

            if (face.IndexCount == 4)
            {
                RemapAxes(verts[face.Indices[2]], verts[face.Indices[3]], verts[face.Indices[0]],
                          upAxis, out v0, out v1, out v2);

                _tris.Add(new Tri { V0 = v0, V1 = v1, V2 = v2 });
            }
        }

        private void RemapAxes(Vector3D v0, Vector3D v1, Vector3D v2, string upAxis,
                                out Vector3 r0, out Vector3 r1, out Vector3 r2)
        {
            r0 = Remap(v0, upAxis);
            r1 = Remap(v1, upAxis);
            r2 = Remap(v2, upAxis);
        }

        private Vector3 Remap(Vector3D v, string upAxis)
        {
            switch (upAxis.ToUpper())
            {
                case "Y":
                    // 模型Y轴 → 体素Z轴(高度)，模型X→体素X，模型Z→体素Y
                    return new Vector3(v.X, v.Z, v.Y);
                case "Z":
                    return new Vector3(v.X, v.Y, v.Z);
                case "X":
                    return new Vector3(v.Y, v.Z, v.X);
                default:
                    return new Vector3(v.X, v.Z, v.Y);
            }
        }

        private void ComputeScaleAndOffset()
        {
            float minX = float.MaxValue, maxX = float.MinValue;
            float minY = float.MaxValue, maxY = float.MinValue;
            float minZ = float.MaxValue, maxZ = float.MinValue;

            foreach (var tri in _tris)
            {
                UpdateMinMax(tri.V0, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ);
                UpdateMinMax(tri.V1, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ);
                UpdateMinMax(tri.V2, ref minX, ref maxX, ref minY, ref maxY, ref minZ, ref maxZ);
            }

            float sizeX = maxX - minX;
            float sizeY = maxY - minY;
            float sizeZ = maxZ - minZ;

            if (sizeX < 0.0001f) sizeX = 1;
            if (sizeY < 0.0001f) sizeY = 1;
            if (sizeZ < 0.0001f) sizeZ = 1;

            float margin = 0.90f;
            _scaleX = VOLUME_WIDTH  * margin / sizeX;
            _scaleY = VOLUME_HEIGHT * margin / sizeY;
            _scaleZ = VOLUME_DEPTH  * margin / sizeZ;

            float scale = Math.Min(_scaleX, Math.Min(_scaleY, _scaleZ));
            _scaleX = scale;
            _scaleY = scale;
            _scaleZ = scale;

            _offsetX = (VOLUME_WIDTH  - sizeX * scale) / 2f - minX * scale;
            _offsetY = (VOLUME_HEIGHT - sizeY * scale) / 2f - minY * scale;
            _offsetZ = (VOLUME_DEPTH  - sizeZ * scale) / 2f - minZ * scale;
        }

        private void UpdateMinMax(Vector3 v, ref float minX, ref float maxX,
                                   ref float minY, ref float maxY,
                                   ref float minZ, ref float maxZ)
        {
            if (v.X < minX) minX = v.X; if (v.X > maxX) maxX = v.X;
            if (v.Y < minY) minY = v.Y; if (v.Y > maxY) maxY = v.Y;
            if (v.Z < minZ) minZ = v.Z; if (v.Z > maxZ) maxZ = v.Z;
        }

        private void UpdateYRange(Vector3 v, ref float minY, ref float maxY)
        {
            if (v.Y < minY) minY = v.Y;
            if (v.Y > maxY) maxY = v.Y;
        }

        /* ---- 切片导出 ---- */
        private static readonly (byte, byte, byte)[] _slicePalette = BuildSlicePalette();

        private static (byte, byte, byte)[] BuildSlicePalette()
        {
            var pal = new (byte, byte, byte)[256];
            pal[0] = (0, 0, 0);
            // 锚点
            var anchors = new (int idx, int r, int g, int b)[] {
                (1,0,8,0),(2,0,16,0),(3,0,24,0),(4,0,32,0),(5,0,40,0),(6,0,48,0),(7,0,56,0),(8,0,64,0),
                (9,0,72,0),(10,0,80,0),(11,0,88,0),(12,0,96,0),(13,0,104,0),(14,0,112,0),(15,0,120,0),(16,0,128,0),
                (17,0,136,0),(18,0,144,0),(19,0,152,0),(20,0,160,0),(21,0,168,0),(22,0,176,0),(23,0,184,0),(24,0,192,0),
                (25,0,200,0),(26,0,208,0),(27,0,216,0),(28,0,224,0),(29,0,232,0),(30,0,240,0),(31,0,255,0),
                (32,0,255,8),(40,0,255,72),(48,0,255,136),(56,0,255,200),
                (64,0,200,255),(72,0,136,255),(80,0,72,255),(88,0,8,255),
                (96,32,0,255),(104,96,0,255),(112,160,0,255),(120,224,0,255),
                (128,255,0,200),(136,255,0,136),(144,255,0,72),(152,255,0,8),
                (160,255,32,0),(168,255,96,0),(176,255,160,0),(184,255,224,0),
                (192,255,255,0),(200,255,255,64),(208,255,255,128),(216,255,255,192),(224,255,255,255)
            };
            for (int i = 0; i < anchors.Length; i++)
                pal[anchors[i].idx] = ((byte)anchors[i].r, (byte)anchors[i].g, (byte)anchors[i].b);
            // 插值
            var keys = anchors.Select(a => a.idx).OrderBy(x => x).ToArray();
            for (int k = 0; k < keys.Length - 1; k++) {
                int i0 = keys[k], i1 = keys[k + 1];
                var (r0, g0, b0) = pal[i0];
                var (r1, g1, b1) = pal[i1];
                for (int i = i0 + 1; i < i1; i++) {
                    float t = (float)(i - i0) / (i1 - i0);
                    pal[i] = ((byte)(r0 + (r1 - r0) * t + 0.5f),
                              (byte)(g0 + (g1 - g0) * t + 0.5f),
                              (byte)(b0 + (b1 - b0) * t + 0.5f));
                }
            }
            for (int i = 225; i < 256; i++) pal[i] = (255, 255, 255);
            return pal;
        }

        public byte[] ExportSlices(byte[] volume, byte fileR, byte fileG, byte fileB, int filePalCount, int totalSlices)
        {
            int strips = 32, leds = 24;
            // 576 个 uint32_t state / 片
            int statesPerSlice = strips * leds * 24 / 32;  // = 576
            int sliceBytes = statesPerSlice * 4;            // = 2304
            var slices = new byte[totalSlices * sliceBytes];

            for (int phase = 0; phase < totalSlices; phase++)
            {
                double angle = 2.0 * Math.PI * phase / totalSlices;
                float cos = (float)Math.Cos(angle);
                float sin = (float)Math.Sin(angle);
                int off = phase * sliceBytes;

                // 先收集所有 32×24 LED 的 GRB 值
                uint[] grbAll = new uint[strips * leds];
                for (int strip = 0; strip < strips; strip++)
                {
                    int dx = strip * 2 - 31;
                    int x = 16 + (int)Math.Round(dx * cos * 0.5f);
                    int y = 16 - (int)Math.Round(dx * sin * 0.5f);
                    if (x < 0) x = 0; if (x > 31) x = 31;
                    if (y < 0) y = 0; if (y > 31) y = 31;

                    for (int z = 0; z < leds; z++)
                    {
                        int vi = z * VOLUME_WIDTH * VOLUME_DEPTH + y * VOLUME_WIDTH + x;
                        byte idx = volume[vi];
                        (byte r, byte g, byte b) = ResolveSliceColor(idx, fileR, fileG, fileB, filePalCount);
                        // GRB 格式 (与 WS2812 线序一致)
                        grbAll[strip * leds + z] = ((uint)g << 16) | ((uint)r << 8) | b;
                    }
                }

                // 展开为 576 个 uint32_t state (位片)
                byte[] stateBytes = new byte[sliceBytes];
                for (int bitPos = 0; bitPos < 576; bitPos++)
                {
                    int led = bitPos / 24;
                    int bitInLed = bitPos % 24;
                    uint mask = 1u << (23 - bitInLed);
                    uint state = 0;
                    for (int strip = 0; strip < 32; strip++)
                        if ((grbAll[strip * leds + led] & mask) != 0)
                            state |= 1u << strip;
                    int bo = bitPos * 4;
                    stateBytes[bo]     = (byte)(state);
                    stateBytes[bo + 1] = (byte)(state >> 8);
                    stateBytes[bo + 2] = (byte)(state >> 16);
                    stateBytes[bo + 3] = (byte)(state >> 24);
                }
                Array.Copy(stateBytes, 0, slices, off, sliceBytes);

                if (phase % 20 == 0)
                    Console.WriteLine(string.Format("  切片进度: {0}/{1}", phase + 1, totalSlices));
            }
            return slices;
        }

        private static readonly byte SLICE_MAX_BRIGHTNESS = 35;  // 与 MCU 端 PALETTE_MAX_BRIGHTNESS 一致

        private static byte Cap(byte v) => (byte)((v * SLICE_MAX_BRIGHTNESS + 127) / 255);

        private static (byte, byte, byte) ResolveSliceColor(byte idx, byte fileR, byte fileG, byte fileB, int filePalCount)
        {
            if (idx == 1 && filePalCount >= 1)
                return (Cap(fileR), Cap(fileG), Cap(fileB));
            var (r, g, b) = _slicePalette[idx];
            return (Cap(r), Cap(g), Cap(b));
        }

        /// <summary>
        /// 导出1-bit体素切片 (format=0x03, 50片).
        /// 每片96字节: 24 LEDs × 32 strips 打包为位图.
        /// LED_i 的 strip_j 对应 data[led*4 + strip/8] 的第 (strip%8) 位.
        /// </summary>
        public byte[] ExportSlicesV2(byte[] volume, int totalSlices)
        {
            int strips = 32, leds = 24;
            int bitBytes = strips * leds / 8;  // 96
            var slices = new byte[totalSlices * bitBytes];

            for (int phase = 0; phase < totalSlices; phase++)
            {
                double angle = 2.0 * Math.PI * phase / (totalSlices * 2); // 100相位等效
                float cos = (float)Math.Cos(angle);
                float sin = (float)Math.Sin(angle);
                int off = phase * bitBytes;

                for (int strip = 0; strip < strips; strip++)
                {
                    int dx = strip * 2 - 31;
                    int x = 16 + (int)Math.Round(dx * cos * 0.5f);
                    int y = 16 - (int)Math.Round(dx * sin * 0.5f);
                    if (x < 0) x = 0; if (x > 31) x = 31;
                    if (y < 0) y = 0; if (y > 31) y = 31;

                    for (int led = 0; led < leds; led++)
                    {
                        int vi = led * VOLUME_WIDTH * VOLUME_DEPTH + y * VOLUME_WIDTH + x;
                        if (volume[vi] != 0)
                        {
                            int byteIdx = off + led * 4 + strip / 8;
                            int bitIdx = strip % 8;
                            slices[byteIdx] |= (byte)(1 << bitIdx);
                        }
                    }
                }

                if (phase % 20 == 0)
                    Console.WriteLine(string.Format("  V2切片进度: {0}/{1}", phase + 1, totalSlices));
            }
            return slices;
        }

        /// <summary>
        /// 导出6-bit体素切片 (format=0x04, 50片).
        /// 每片576字节: 768体素 × 6bit打包 (4体素→3字节).
        /// palette[64][3]: 64色调色板, 索引0=灭.
        /// </summary>
        public byte[] ExportSlicesV3(byte[] volume, byte[,] palette, int paletteCount, int totalSlices)
        {
            int strips = 32, leds = 24;
            int voxelsPerSlice = strips * leds;  // 768
            int packedBytes = (voxelsPerSlice * 6 + 7) / 8;  // 576
            var slices = new byte[totalSlices * packedBytes];

            for (int phase = 0; phase < totalSlices; phase++)
            {
                double angle = 2.0 * Math.PI * phase / (totalSlices * 2);
                float cos = (float)Math.Cos(angle);
                float sin = (float)Math.Sin(angle);
                int off = phase * packedBytes;

                for (int strip = 0; strip < strips; strip++)
                {
                    int dx = strip * 2 - 31;
                    int x = 16 + (int)Math.Round(dx * cos * 0.5f);
                    int y = 16 - (int)Math.Round(dx * sin * 0.5f);
                    if (x < 0) x = 0; if (x > 31) x = 31;
                    if (y < 0) y = 0; if (y > 31) y = 31;

                    for (int led = 0; led < leds; led++)
                    {
                        int vi = led * VOLUME_WIDTH * VOLUME_DEPTH + y * VOLUME_WIDTH + x;
                        if (volume[vi] == 0) continue;

                        /* 按 LED 层分配颜色索引: LED 0→1, LED 23→63 */
                        int colorIdx = 1 + led * 62 / (leds - 1);
                        if (colorIdx >= paletteCount) colorIdx = paletteCount - 1;
                        byte idx = (byte)colorIdx;

                        int voxelIdx = led * strips + strip;
                        int group = voxelIdx / 4;
                        int pos = voxelIdx % 4;
                        int byteBase = off + group * 3;

                        int existing = slices[byteBase] | (slices[byteBase + 1] << 8) | (slices[byteBase + 2] << 16);
                        int shift = pos * 6;
                        existing &= ~(0x3F << shift);
                        existing |= (idx & 0x3F) << shift;
                        slices[byteBase] = (byte)(existing);
                        slices[byteBase + 1] = (byte)(existing >> 8);
                        slices[byteBase + 2] = (byte)(existing >> 16);
                    }
                }

                if (phase % 20 == 0)
                    Console.WriteLine(string.Format("  V3切片进度: {0}/{1}", phase + 1, totalSlices));
            }
            return slices;
        }

        private float Min3(float a, float b, float c)
        {
            return Math.Min(a, Math.Min(b, c));
        }

        private float Max3(float a, float b, float c)
        {
            return Math.Max(a, Math.Max(b, c));
        }
    }
}
