using RuiJi.Slicer.Core.Slicer;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;

namespace RuiJi.Slicer.Core.Array
{
    // 按线性方式生成一组平行切片平面
    public class LinearArrayCreater : IArrayCreater<LinearArrayDefine>
    {
        public ISlicePlane[] CreateArrayPlane(LinearArrayDefine define)
        {
            // 在 Dmin 到 Dmax 之间均匀分布 Count 个切片平面
            var planes = new List<ISlicePlane>();

            var step = (define.Dmax - define.Dmin) / define.Count;
            for (int i = 0; i < define.Count; i++)
            {
                var p = new Plane(define.Normal, define.Dmin + step * i);

                planes.Add(new LinearSlicePlaneInfo(p));
            }

            return planes.ToArray();
        }
    }
}
