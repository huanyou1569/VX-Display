/*
This file is part of RuiJi.Slice: A library for slicing 3D model.
RuiJi.Slice is part of RuiJiHG: RuiJiHG is holographic projection.
see http://www.ruijihg.com/ for more infomation.

Copyright (C) 2017 Pingqi(416803633@qq.com)
Copyright (c) 2017, githublixiang(271800249@qq.com)

RuiJi.Slice is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

RuiJi.Slice is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with wiringPi.
If not, see <http://www.gnu.org/licenses/>.
*/

using RuiJi.Slicer.Core.Slicer;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Media3D;
using System.Windows.Shapes;

namespace RuiJi.Slicer.Core
{
    // 三角面片：切片算法的基本几何单元
    // 一个模型会被拆成很多 Facet，后续所有切片计算都围绕这些三角面展开
    public class Facet
    {
        public Plane Plane
        {
            get {
                // 由三个顶点实时计算所在平面
                return Plane.CreateFromVertices(Vertices[0], Vertices[1], Vertices[2]);
            }
        }

        public IList<Vector3> Vertices
        {
            get;
            private set;
        }

        /// <summary>
        /// 法线
        /// </summary>
        public Vector3 Normal
        {
            get
            {
                // 三角面所属平面的法向量
                return Plane.Normal;
            }
        }

        public List<LineSegment> Lines
        {
            get;
            private set;
        }

        public Vector3 Center
        {
            get
            {
                // 三角面的几何中心，用于排序、展示或辅助变换
                var x = (float)Math.Round((Vertices[0].X + Vertices[1].X + Vertices[2].X) / 3, 2);
                var y = (float)Math.Round((Vertices[0].Y + Vertices[1].Y + Vertices[2].Y) / 3, 2);
                var z = (float)Math.Round((Vertices[0].Z + Vertices[1].Z + Vertices[2].Z) / 3, 2);

                return new Vector3(x, y, z);
            }
        }

        public bool TooSmall
        {
            get
            {
                // 退化三角形：三条边长度都接近 0 时认为无效
                return Math.Round(Lines[0].Lenght) == 0 && Math.Round(Lines[1].Lenght) == 0 && Math.Round(Lines[2].Lenght) == 0;
            }
        }

        public float Area
        {
            get
            {
                // 计算三角面面积，供排序和筛选使用
                var area = 0f;

                for (int i = 0; i < Vertices.Count; i += 3)
                {
                    area += (float)Math.Round(getPolygonArea(Vertices.Skip(i).Take(3).ToList()), 2);
                }

                return area;
            }
        }

        public Facet(Vector3 v1, Vector3 v2, Vector3 v3)
        {
            // 输入顶点先做简单保留两位小数的归一化，减少浮点误差带来的判断问题
            v1 = new Vector3((float)Math.Round(v1.X, 2), (float)Math.Round(v1.Y, 2), (float)Math.Round(v1.Z, 2));
            v2 = new Vector3((float)Math.Round(v2.X, 2), (float)Math.Round(v2.Y, 2), (float)Math.Round(v2.Z, 2));
            v3 = new Vector3((float)Math.Round(v3.X, 2), (float)Math.Round(v3.Y, 2), (float)Math.Round(v3.Z, 2));

            this.Vertices = new List<Vector3> {
                v1,
                v2,
                v3
            };

            this.Lines = new List<LineSegment>() {
                    new LineSegment(v1,v2),
                    new LineSegment(v2,v3),
                new LineSegment(v1,v3)
            };
        }

        public Facet(Point3D p1, Point3D p2, Point3D p3) : this(
            new Vector3((float)Math.Round(p1.X, 2), (float)Math.Round(p1.Y, 2), (float)Math.Round(p1.Z, 2)),
            new Vector3((float)Math.Round(p2.X, 2), (float)Math.Round(p2.Y, 2), (float)Math.Round(p2.Z, 2)),
            new Vector3((float)Math.Round(p3.X, 2), (float)Math.Round(p3.Y, 2), (float)Math.Round(p3.Z, 2))
            )
        {
            // 兼容 WPF 的 Point3D 输入，最终仍转换成 System.Numerics.Vector3
        }

        public Facet(IList<Vector3> vs) : this(vs[0], vs[1], vs[2])
        {
            // 允许从顶点列表直接构造三角面
        }

        public void Merge(Facet facet)
        {
            // 把另一个三角面顶点并入当前对象，常用于面片合并或调试输出
            foreach (var f in facet.Vertices)
            {
                this.Vertices.Add(f);
            }
        }

        private double getPolygonArea(List<Vector3> points)
        {
            // 用海伦公式计算三角形面积
            var sizep = points.Count();
            if (sizep < 3)
                return 0.0;

            //根号(p*(p-a)*(p-b)*(p-c)) 其中p=(a+b+c)/2.

            var ls0 = new LineSegment(points[0] , points[1]);
            var ls1 = new LineSegment(points[1], points[2]);
            var ls2 = new LineSegment(points[2], points[0]);

            var a = ls0.Lenght;
            var b = ls1.Lenght;
            var c = ls2.Lenght;

            var p = (a + b + c) / 2;
            var s = p * (p - a) * (p - b) * (p - c);
            return Math.Sqrt(s);
        }

        public void Transform(Vector3 axis, float angle)
        {
            // 按轴角旋转整个三角面，同时同步更新三条边
            var q = System.Numerics.Matrix4x4.CreateFromAxisAngle(axis, angle);

            for (int i = 0; i < Vertices.Count; i++)
            {
                Vertices[i] = Vector3.Transform(Vertices[i], q);
            }

            var v1 = Vertices[0];
            var v2 = Vertices[1];
            var v3 = Vertices[2];

            //v1 = new Vector3((float)Math.Round(v1.X, 2), (float)Math.Round(v1.Y, 2), (float)Math.Round(v1.Z, 2));
            //v2 = new Vector3((float)Math.Round(v2.X, 2), (float)Math.Round(v2.Y, 2), (float)Math.Round(v2.Z, 2));
            //v3 = new Vector3((float)Math.Round(v3.X, 2), (float)Math.Round(v3.Y, 2), (float)Math.Round(v3.Z, 2));

            this.Lines[0].Start = v1;
            this.Lines[0].End = v2;
            this.Lines[1].Start = v2;
            this.Lines[1].End = v3;
            this.Lines[2].Start = v1;
            this.Lines[2].End = v3;
        }
    }
}
