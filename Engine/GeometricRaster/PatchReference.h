#pragma once
#include "SceneStructure.h"
#include "ClipProjection.h"
#include "PatchGeometry.h"
#include "../Shaders/PatchPolicy.shared.h"

namespace Frontier::PatchGeometry {
// CPU mirror of PatchSelection.slang for offline diagnostics/tests; never substitutes
// a camera-selected mesh into the ray scene. Material inputs are the CURRENT records.
inline bool Select(const ClusterRecord& c,const InstanceRecord& i,const MaterialRecord& m,
                   const MaterialSlabRecord* s,const CameraClipConfiguration& camera,
                   uint32_t height,bool preview=true)
{
    if(!preview||!s||!c.CoarseTriangleCount||m.SlabCount!=1)return false;
    bool textures=(s->TextureSlots[4]&65535u)!=65535u||(s->TextureSlots[4]>>16)!=65535u||(s->TextureSlots[3]>>16)!=65535u;
    bool opaque=PatchOpaque(int(m.SlabCount),int(m.Flags),s->TransmissionWeight,s->SubsurfaceWeight,s->GeometryOpacity,
        textures,m.EmissiveR!=0||m.EmissiveG!=0||m.EmissiveB!=0,(s->SlabFlags&1)!=0);
    auto world=ProjectionFromColumns(i.World);
    Vector3 x{i.World[0],i.World[1],i.World[2]},y{i.World[4],i.World[5],i.World[6]},z{i.World[8],i.World[9],i.World[10]};
    float xx=Dot(x,x),yy=Dot(y,y),zz=Dot(z,z),scale=std::sqrt(xx+yy+zz);
    auto v=TransformPoint(world,{c.CenterX,c.CenterY,c.CenterZ})-camera.Origin;
    float radius=c.Radius*scale,depth=Dot(v,camera.Forward),distance=Length(v);
    float lateral=std::max(std::abs(Dot(v,camera.Right)),std::abs(Dot(v,camera.Up)));
    bool similarity=xx>0&&std::abs(xx-yy)<xx*1e-5f&&std::abs(xx-zz)<xx*1e-5f
        &&std::abs(Dot(x,y))<xx*1e-5f&&std::abs(Dot(x,z))<xx*1e-5f&&std::abs(Dot(y,z))<xx*1e-5f&&Dot(Cross(x,y),z)>0;
    bool away=false;
    if(similarity&&c.Cutoff<1&&distance>radius)
        away=Dot(TransformDirection(world,{c.AxisX,c.AxisY,c.AxisZ}).Normalized(),v/distance)>c.Cutoff+radius/distance;
    return PatchChooseCoarse(true,opaque,true,away,c.CoarseError,scale,depth,radius,lateral,float(height)/(2*camera.TanHalfFieldOfView),camera.NearDistance);
}
}
