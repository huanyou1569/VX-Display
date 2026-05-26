using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Numerics;

namespace RuiJi.Slicer.Core.Slicer
{
    // 线性切片结果转图片工具
    public class LinearSlicer
    {
        public static List<Bitmap> ToImage(List<SlicedPlane> slicedPlane, ModelSize size, int imageWidth, int imageHeight, int offsetX = 0, int offsetY = 0)
        {
            // 将每层切片线段投影到二维平面后，批量生成位图
            var firstNormal = (slicedPlane.First().SlicePlane as LinearSlicePlaneInfo).Plane.Normal;
            var images = new List<Bitmap>();

            foreach (var sp in slicedPlane)
            {
                var lines = new List<Vector2[]>();
                var info = sp.SlicePlane as LinearSlicePlaneInfo;

                foreach (var line in sp.Lines)
                {
                    // 线性切片默认把 3D 线段映射到 X/Z 平面
                    lines.Add(new Vector2[] {
                        new Vector2((float)line.Start.X,(float)line.Start.Z),
                        new Vector2((float)line.End.X,(float)line.End.Z)
                    });
                }

                var img = ToImage(lines, size, imageWidth, imageHeight, offsetX, offsetY);
                images.Add(img);
            }

            return images;
        }

        public static List<Bitmap> ToImage(List<SlicedPlane> slicedPlane, int imageWidth, int imageHeight)
        {
            var firstNormal = (slicedPlane.First().SlicePlane as LinearSlicePlaneInfo).Plane.Normal;
            var images = new List<Bitmap>();

            foreach (var sp in slicedPlane)
            {
                var lines = new List<Vector2[]>();
                var info = sp.SlicePlane as LinearSlicePlaneInfo;

                foreach (var line in sp.Lines)
                {
                    lines.Add(new Vector2[] {
                        new Vector2((float)line.Start.X,(float)line.Start.Z),
                        new Vector2((float)line.End.X,(float)line.End.Z)
                    });
                }

                var img = ToImage(lines,imageWidth, imageHeight);
                images.Add(img);
            }

            return images;
        }

        public static Bitmap ToImage(List<Vector2[]> lines, ModelSize size, int imageWidth, int imageHeight, int offsetX = 0, int offsetY = 0)
        {
            // 根据模型尺寸计算缩放比例，保证绘制结果能放进目标图片
            var fd = 1f;
            var fw = 1f;
            var fh = 1f;

            if (size.Length > imageWidth)
                fw = imageWidth / (float)size.Length;
            if (size.Height > imageHeight)
                fh = imageHeight / (float)size.Height;
            var f = Math.Min(fd, Math.Min(fw, fh));

            var ow = imageWidth / 2f + offsetX;
            var oh = imageHeight / 2f + offsetY;

            var bmp = new Bitmap(imageWidth, imageHeight);
            var g = Graphics.FromImage(bmp);
            g.FillRectangle(new SolidBrush(Color.White), new Rectangle(0, 0, imageWidth, imageHeight));

            foreach (var line in lines)
            {
                // 把二维坐标换算成像素坐标并画成红线
                int x1 = Convert.ToInt32((line[0].X * f) + ow);
                int y1 = Convert.ToInt32((line[0].Y * f) + oh);

                int x2 = Convert.ToInt32((line[1].X * f) + ow);
                int y2 = Convert.ToInt32((line[1].Y * f) + oh);

                Point p1 = new Point(x1, y1);
                Point p2 = new Point(x2, y2);

                g.DrawLine(new Pen(new SolidBrush(Color.Red)), p1, p2);
            }

            bmp.RotateFlip(RotateFlipType.Rotate180FlipX);
            return bmp;
        }

        public static Bitmap ToImage(List<Vector2[]> lines, int imageWidth, int imageHeight)
        {
            // 不带模型尺寸时，直接把线段按原始坐标画到图片中心
            var bmp = new Bitmap(imageWidth, imageHeight);
            var g = Graphics.FromImage(bmp);
            g.FillRectangle(new SolidBrush(Color.White), new Rectangle(0, 0, imageWidth, imageHeight));

            var ow = imageWidth / 2f;
            var oh = imageHeight / 2f;

            foreach (var line in lines)
            {
                int x1 = Convert.ToInt32(line[0].X + ow);
                int y1 = Convert.ToInt32(line[0].Y + oh);

                int x2 = Convert.ToInt32(line[1].X + ow);
                int y2 = Convert.ToInt32(line[1].Y + oh);

                Point p1 = new Point(x1, y1);
                Point p2 = new Point(x2, y2);

                g.DrawLine(new Pen(new SolidBrush(Color.Red)), p1, p2);
            }

            bmp.RotateFlip(RotateFlipType.Rotate180FlipX);
            return bmp;
        }

    }
}
