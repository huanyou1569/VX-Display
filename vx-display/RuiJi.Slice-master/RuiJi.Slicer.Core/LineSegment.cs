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

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Numerics;
using System.Windows.Media.Media3D;

namespace RuiJi.Slicer.Core
{
    // 空间线段：既可以表示三角面的边，也可以表示切片后的轮廓线
    public class LineSegment
    {
        public PlaneFacet ChildFacet { get; set; }

        public bool Outter { get; set; }

        public Vector3 Start
        {
            get;
            set;
        }

        public Vector3 End
        {
            get;
            set;
        }

        public Vector3 Normal
        {
            get {
                // 这里返回的是线段方向向量，不是严格意义上的法线
                var p = End - Start;

                return new Vector3((float)p.X, (float)p.Y, (float)p.Z);
            }
        }

        public double Lenght
        {
            get
            {
                return (Start - End).Length();
            }
        }

        public LineSegment(Vector3 a, Vector3 b)
        {
            // 直接用两个端点构造线段
            this.Start = a;
            this.End = b;
        }

        public LineSegment(Point3D a, Point3D b)
        {
            this.Start = new Vector3((float)a.X, (float)a.Y, (float)a.Z);
            this.End = new Vector3((float)b.X, (float)b.Y, (float)b.Z);
        }

        public bool Equals(LineSegment line)
        {
            // 判断两条线段是否相同，正向和反向都认为是同一条线段
            return this.Start.X == line.Start.X && this.Start.Y == line.Start.Y && this.Start.Z == line.Start.Z
                && this.End.X == line.End.X && this.End.Y == line.End.Y && this.End.Z == line.End.Z ||
                this.End.X == line.Start.X && this.End.Y == line.Start.Y && this.End.Z == line.Start.Z
                && this.Start.X == line.End.X && this.Start.Y == line.End.Y && this.Start.Z == line.End.Z;
        }
    }
}
