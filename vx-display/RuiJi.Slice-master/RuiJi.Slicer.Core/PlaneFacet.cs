using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Shapes;

namespace RuiJi.Slicer.Core
{
    // 共面面片组织结构：把多个同平面 Facet 归并到一起，方便递归展开和边界绘制
    public class PlaneFacet
    {
        // 当前平面上的所有三角面
        public List<Facet> Facets = new List<Facet>();

        public LineSegment ParentLineSegment
        {
            get;
            set;
        }

        // 父平面对象，用于构建层级关系
        public PlaneFacet Parent { get; set; }  

        // 当前节点在递归树中的深度
        public int Deep { get; set; }

        public Plane Plane
        {
            get { 
                // 该组平面默认以第一个 Facet 的平面作为代表
                return this.Facets.First().Plane;
            }
        }

        public List<LineSegment> AroundLines
        {
            get
            {
                // 找出只被当前 PlaneFacet 使用一次的边，作为外轮廓边
                var lines = new List<LineSegment>();

                foreach (var facet in Facets)
                {
                    foreach (var line in facet.Lines)
                    {
                        if (CountLine(line) == 1)
                            lines.Add(line);
                    }
                }

                return lines;
            }
        }

        public Vector3 Center
        {
            get
            {
                // 所有子 Facet 中心点的平均值，作为该平面的中心
                var x = Facets.Sum(m => m.Center.X) / Facets.Count;
                var y = Facets.Sum(m => m.Center.Y) / Facets.Count;
                var z = Facets.Sum(m => m.Center.Z) / Facets.Count;

                return new Vector3(x, y, z);
            }
        }

        public float Area
        {
            get
            {
                // 当前平面包含的所有三角面面积总和
                return Facets.Sum(m => m.Area);
            }
        }

        public float Angle
        {
            get
            {
                if(Parent == null)
                    return 0;

                // 当前平面与父平面之间的夹角，用于递归展开时的旋转修正
                return (float)((float)Math.Acos(Vector3.Dot(this.Plane.Normal, Parent.Plane.Normal)) * 180 / Math.PI);
            }
        }

        public PlaneFacet()
        { 
            // 空构造函数，便于后续手动填充 Facet 集合
        }

        public PlaneFacet(Facet facet)
        {            
            // 直接用一个三角面初始化该平面组
            Facets.Add(facet);
        }

        public void AddFacet(Facet facet)
        {
            // 把新的共面三角面加入当前平面组
            Facets.Add(facet);
        }

        public int CountLine(LineSegment line)
        {
            // 统计某条边在当前平面组内被引用了多少次
            var c = 0;

            foreach (var facet in Facets)
            {
                c += facet.Lines.Count(m => m.Equals(line));
            }

            return c;
        }

        public LineSegment Collinear(PlaneFacet planeFacet)
        {
            // 寻找两个平面组之间的公共边，作为层级连接边
            LineSegment line = null;
            foreach (var line0 in AroundLines)
            {
                foreach (var line1 in planeFacet.AroundLines)
                {
                    if(line0.Equals(line1))
                        line = line0;
                }
            }

            return line;
        }

        public void Flatten(int deep = int.MaxValue)
        {
            // 递归展开平面层级
            if(this.Deep > deep)
            {
                return;
            }

            if (ParentLineSegment != null)
            {
                var angle = (float)((Angle-45) * Math.PI / 180.0);
                var axis = Vector3.Normalize(ParentLineSegment.Normal);
                Transform(axis, angle);
            }

            foreach (var line in AroundLines)
            {
                if (line.ChildFacet != null)
                {
                    line.ChildFacet.Flatten(deep);
                }
            }
        }

        public void Transform(Vector3 axis,float angle)
        {
            // 对当前平面及其子平面整体做旋转变换
            foreach (var facet in Facets)
            {
                facet.Transform(axis, angle);
            }

            foreach (var line in AroundLines)
            {
                if (line.ChildFacet != null)
                {
                    line.ChildFacet.Transform(axis, angle);
                }
            }
        }
    }
}
