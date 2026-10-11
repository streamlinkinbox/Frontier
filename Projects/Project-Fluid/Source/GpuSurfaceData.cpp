#include "GpuSurfaceData.h"
#include "MarchingCubesTables.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace Frontier::ProjectFluid {
namespace {
Vec3 V(Float4 a){return {a.x,a.y,a.z};}
constexpr int Corners[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
constexpr int Edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
}
SurfaceGpuInput PrepareGpuSurface(const std::vector<SurfaceKernel>& kernels){
    if(kernels.size()>PbfFluid::MaxParticles)throw std::runtime_error("GPU kernel capacity exceeded");
    SurfaceGpuInput out;out.Parameters.Grid[3]=uint32_t(kernels.size());
    const auto& p=out.Parameters;const int nx=p.Grid[0],ny=p.Grid[1],nz=p.Grid[2],b=p.Config[2];
    const int bx=(nx+b-1)/b,by=(ny+b-1)/b,bz=(nz+b-1)/b;
    std::vector<std::vector<uint32_t>> lists(bx*by*bz);
    for(uint32_t id=0;id<kernels.size();++id){const auto& k=kernels[id];
        const Vec3 a=k.AxisA/std::max(Dot(k.AxisA,k.AxisA),1e-10f),c=k.AxisC/std::max(Dot(k.AxisC,k.AxisC),1e-10f),d=k.AxisB/std::max(Dot(k.AxisB,k.AxisB),1e-10f);
        out.Kernels.push_back({{k.Centre.x,k.Centre.y,k.Centre.z,k.VolumeWeight},{a.x,a.y,a.z,0},{d.x,d.y,d.z,0},{c.x,c.y,c.z,0}});
        // PCA produces orthogonal axes. Conservative radius also pads roundoff.
        const float radius=std::max({Length(k.AxisA),Length(k.AxisB),Length(k.AxisC)})+p.OriginSpacing.w;
        for(int z=0;z<bz;++z)for(int y=0;y<by;++y)for(int x=0;x<bx;++x){
            const Vec3 lo=V(p.OriginSpacing)+Vec3{float(x*b),float(y*b),float(z*b)}*p.OriginSpacing.w;
            const Vec3 hi=lo+Vec3{float(b),float(b),float(b)}*p.OriginSpacing.w;
            if(k.Centre.x+radius<lo.x||k.Centre.x-radius>hi.x||k.Centre.y+radius<lo.y||k.Centre.y-radius>hi.y||k.Centre.z+radius<lo.z||k.Centre.z-radius>hi.z)continue;
            lists[(z*by+y)*bx+x].push_back(id);
        }
    }
    out.Offsets.push_back(0);for(const auto& list:lists){out.Ids.insert(out.Ids.end(),list.begin(),list.end());out.Offsets.push_back(uint32_t(out.Ids.size()));}return out;
}
SurfaceGpuOutput MirrorGpuSurface(const SurfaceGpuInput& in){
    const auto& p=in.Parameters;const int nx=p.Grid[0],ny=p.Grid[1],nz=p.Grid[2],b=p.Config[2];const float h=p.OriginSpacing.w,iso=p.Scalar.x;
    const int bx=(nx+b-1)/b,by=(ny+b-1)/b;const uint32_t n=nx*ny*nz;
    SurfaceGpuOutput out;out.Field.resize(n);out.Vertices.resize(n*3);
    auto at=[&](int x,int y,int z){return uint32_t((z*ny+y)*nx+x);};
    auto pos=[&](int x,int y,int z){return V(p.OriginSpacing)+Vec3{float(x),float(y),float(z)}*h;};
    for(int z=0;z<nz;++z)for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){float f=0;const auto brick=((z/b)*by+y/b)*bx+x/b;
        for(uint32_t j=in.Offsets[brick];j<in.Offsets[brick+1];++j){const auto& k=in.Kernels[in.Ids[j]];const Vec3 r=pos(x,y,z)-V(k.Centre);const float u=Dot(r,V(k.A)),v=Dot(r,V(k.B)),w=Dot(r,V(k.C));const float q2=u*u+v*v+w*w;if(q2<1){const float q=1-q2;f+=k.Centre.w*q*q*q;}}
        out.Field[at(x,y,z)]=f;
    }
    auto gradient=[&](int x,int y,int z){return Vec3{out.Field[at(std::min(x+1,nx-1),y,z)]-out.Field[at(std::max(x-1,0),y,z)],out.Field[at(x,std::min(y+1,ny-1),z)]-out.Field[at(x,std::max(y-1,0),z)],out.Field[at(x,y,std::min(z+1,nz-1))]-out.Field[at(x,y,std::max(z-1,0))]};};
    for(int z=0;z<nz;++z)for(int y=0;y<ny;++y)for(int x=0;x<nx;++x)for(int axis=0;axis<3;++axis){int xx=x+(axis==0),yy=y+(axis==1),zz=z+(axis==2);if(xx>=nx||yy>=ny||zz>=nz)continue;
        const float a=out.Field[at(x,y,z)],c=out.Field[at(xx,yy,zz)];if((a>=iso)==(c>=iso))continue;
        const float t=std::abs(c-a)>1e-8f?std::clamp((iso-a)/(c-a),0.f,1.f):.5f;
        const Vec3 v=pos(x,y,z)+(pos(xx,yy,zz)-pos(x,y,z))*t,normal=Normalized((gradient(x,y,z)*(1-t)+gradient(xx,yy,zz)*t)*-1.f);
        out.Vertices[at(x,y,z)*3+axis]={{v.x,v.y,v.z,1},{normal.x,normal.y,normal.z,0}};
    }
    for(int z=0;z<nz-1;++z)for(int y=0;y<ny-1;++y)for(int x=0;x<nx-1;++x){uint32_t nodes[8];int cube=0;
        for(int c=0;c<8;++c){nodes[c]=at(x+Corners[c][0],y+Corners[c][1],z+Corners[c][2]);if(out.Field[nodes[c]]>=iso)cube|=1<<c;}
        const auto* table=MarchingCubesTables::Tri.data()+cube*16;
        for(int j=0;j<16&&table[j]>=0;j+=3){const auto base=out.RequestedIndices;out.RequestedIndices+=3;if(base+3>p.Config[0]){out.Overflow=1;continue;}
            for(int c=0;c<3;++c){const auto edge=table[j+(c==0?0:3-c)];const uint32_t a=nodes[Edges[edge][0]],d=nodes[Edges[edge][1]],lo=std::min(a,d),delta=std::max(a,d)-lo;out.Indices.push_back(lo*3+(delta==1?0:delta==uint32_t(nx)?1:2));}}
    }return out;
}
void SaveGpuSurfaceObj(const SurfaceGpuOutput& mesh,const char* path){
    if(mesh.Overflow)throw std::runtime_error("Refusing truncated GPU mesh export");
    std::ofstream out(path);if(!out)throw std::runtime_error("Cannot open mesh export");
    std::vector<uint32_t> remap(mesh.Vertices.size(),0);uint32_t count=0;
    for(auto id:mesh.Indices){if(id>=mesh.Vertices.size()||mesh.Vertices[id].Position.w!=1)throw std::runtime_error("Invalid GPU edge index");if(!remap[id]){remap[id]=++count;const auto& v=mesh.Vertices[id];
        // Y-up fluid -> Z-up Frontier.
        out<<"v "<<v.Position.x<<' '<<-v.Position.z<<' '<<v.Position.y<<"\nvn "<<v.Normal.x<<' '<<-v.Normal.z<<' '<<v.Normal.y<<'\n';}}
    for(size_t i=0;i<mesh.Indices.size();i+=3){out<<"f";for(int c=0;c<3;++c){auto id=remap[mesh.Indices[i+c]];out<<' '<<id<<"//"<<id;}out<<'\n';}if(!out)throw std::runtime_error("Mesh export write failed");
}
}
