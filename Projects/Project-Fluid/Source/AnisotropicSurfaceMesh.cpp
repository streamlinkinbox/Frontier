#include "AnisotropicSurfaceMesh.h"
#include "MarchingCubesTables.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <unordered_map>

namespace Frontier::ProjectFluid {
namespace {
Vec3 Cross(Vec3 a,Vec3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float AxisLength(const SurfaceKernel& k){return std::max({Length(k.AxisA),Length(k.AxisB),Length(k.AxisC)});}
constexpr int Corner[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
constexpr int EdgeCorners[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
}

Vec3 AnisotropicSurfaceMesh::GridPosition(int x,int y,int z) noexcept{return{MinX+x*Spacing,MinY+y*Spacing,MinZ+z*Spacing};}
std::size_t AnisotropicSurfaceMesh::At(int x,int y,int z)const noexcept{return(static_cast<std::size_t>(z)*Ny+y)*Nx+x;}
float AnisotropicSurfaceMesh::Evaluate(Vec3 point,const std::vector<SurfaceKernel>& kernels)const noexcept{float field=0;for(const auto& k:kernels){const Vec3 r=point-k.Centre;const float aa=std::max(Dot(k.AxisA,k.AxisA),1e-10f),bb=std::max(Dot(k.AxisB,k.AxisB),1e-10f),cc=std::max(Dot(k.AxisC,k.AxisC),1e-10f);const float u=Dot(r,k.AxisA)/aa,v=Dot(r,k.AxisB)/bb,w=Dot(r,k.AxisC)/cc,q2=u*u+v*v+w*w;if(q2<1){const float q=1-q2;field+=k.VolumeWeight*q*q*q;}}return field;}
Vec3 AnisotropicSurfaceMesh::Gradient(Vec3 point)const noexcept{const int x=std::clamp(static_cast<int>(std::round((point.x-MinX)/Spacing)),1,Nx-2),y=std::clamp(static_cast<int>(std::round((point.y-MinY)/Spacing)),1,Ny-2),z=std::clamp(static_cast<int>(std::round((point.z-MinZ)/Spacing)),1,Nz-2);return{(Field_[At(x+1,y,z)]-Field_[At(x-1,y,z)])/(2*Spacing),(Field_[At(x,y+1,z)]-Field_[At(x,y-1,z)])/(2*Spacing),(Field_[At(x,y,z+1)]-Field_[At(x,y,z-1)])/(2*Spacing)};}
void AnisotropicSurfaceMesh::MarkKernel(const SurfaceKernel& k,std::vector<std::uint8_t>& dirty)const{const float radius=AxisLength(k)+2*Spacing;int x0=std::clamp(static_cast<int>((k.Centre.x-radius-MinX)/Spacing)/Brick-1,0,Bx-1),x1=std::clamp(static_cast<int>((k.Centre.x+radius-MinX)/Spacing)/Brick+1,0,Bx-1),y0=std::clamp(static_cast<int>((k.Centre.y-radius-MinY)/Spacing)/Brick-1,0,By-1),y1=std::clamp(static_cast<int>((k.Centre.y+radius-MinY)/Spacing)/Brick+1,0,By-1),z0=std::clamp(static_cast<int>((k.Centre.z-radius-MinZ)/Spacing)/Brick-1,0,Bz-1),z1=std::clamp(static_cast<int>((k.Centre.z+radius-MinZ)/Spacing)/Brick+1,0,Bz-1);for(int z=z0;z<=z1;++z)for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)dirty[(z*By+y)*Bx+x]=1;}
void AnisotropicSurfaceMesh::Prepare(const std::vector<SurfaceKernel>& kernels) {
    Prepared_.clear();for(auto& list:Candidates_)list.clear();
    for(uint32_t i=0;i<kernels.size();++i){const auto& k=kernels[i];
        PreparedKernel p{k.Centre,k.AxisA/std::max(Dot(k.AxisA,k.AxisA),1e-10f),k.AxisB/std::max(Dot(k.AxisB,k.AxisB),1e-10f),k.AxisC/std::max(Dot(k.AxisC,k.AxisC),1e-10f),k.VolumeWeight};

        // Inverse of the evaluation matrix bounds even non-orthogonal kernels.
        const float det=Dot(p.A,Cross(p.B,p.C));
        float radius=1e30f;
        if(std::abs(det)>1e-20f){const Vec3 a=Cross(p.B,p.C)/det,b=Cross(p.C,p.A)/det,c=Cross(p.A,p.B)/det;
            radius=std::sqrt(Dot(a,a)+Dot(b,b)+Dot(c,c))+Spacing*1e-3f;
            p.Extent={std::sqrt(a.x*a.x+b.x*b.x+c.x*c.x)+1e-5f,std::sqrt(a.y*a.y+b.y*b.y+c.y*c.y)+1e-5f,std::sqrt(a.z*a.z+b.z*b.z+c.z*c.z)+1e-5f};
        }else p.Extent={radius,radius,radius};
        Prepared_.push_back(p);
        for(int z=0;z<Bz;++z)for(int y=0;y<By;++y)for(int x=0;x<Bx;++x){
            const Vec3 lo=GridPosition(x*Brick,y*Brick,z*Brick),hi=GridPosition(std::min(Nx-1,(x+1)*Brick),std::min(Ny-1,(y+1)*Brick),std::min(Nz-1,(z+1)*Brick));
            if(k.Centre.x+radius<lo.x||k.Centre.x-radius>hi.x||k.Centre.y+radius<lo.y||k.Centre.y-radius>hi.y||k.Centre.z+radius<lo.z||k.Centre.z-radius>hi.z)continue;
            Candidates_[(z*By+y)*Bx+x].push_back(i);
        }
    }
}
void AnisotropicSurfaceMesh::RebuildFieldBrick(int bx,int by,int bz,const std::vector<SurfaceKernel>& kernels){
    const int x0=bx*Brick,x1=std::min(Nx-1,(bx+1)*Brick),y0=by*Brick,y1=std::min(Ny-1,(by+1)*Brick),z0=bz*Brick,z1=std::min(Nz-1,(bz+1)*Brick);
    const auto& candidates=Candidates_[(bz*By+by)*Bx+bx];
    for(int z=z0;z<=z1;++z)for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)
        Field_[At(x,y,z)]=Reference_?Evaluate(GridPosition(x,y,z),kernels):0;
    if(Reference_)return;
    // Kernel-major splatting into its tight support AABB. Particle order remains
    // ascending at every sample, including samples shared between bricks.
    auto lower=[](float v,float origin,int lo,int hi){return int(std::floor(std::clamp((v-origin)/Spacing,float(lo),float(hi))));};
    auto upper=[](float v,float origin,int lo,int hi){return int(std::ceil(std::clamp((v-origin)/Spacing,float(lo),float(hi))));};
    for(auto id:candidates){const auto& k=Prepared_[id];
        const int lx=lower(k.Centre.x-k.Extent.x,MinX,x0,x1),hx=upper(k.Centre.x+k.Extent.x,MinX,x0,x1);
        const int ly=lower(k.Centre.y-k.Extent.y,MinY,y0,y1),hy=upper(k.Centre.y+k.Extent.y,MinY,y0,y1);
        const int lz=lower(k.Centre.z-k.Extent.z,MinZ,z0,z1),hz=upper(k.Centre.z+k.Extent.z,MinZ,z0,z1);
        for(int z=lz;z<=hz;++z)for(int y=ly;y<=hy;++y)for(int x=lx;x<=hx;++x){
            const Vec3 r=GridPosition(x,y,z)-k.Centre;
            const float u=Dot(r,k.A),v=Dot(r,k.B),w=Dot(r,k.C),q2=u*u+v*v+w*w;
            if(q2<1){const float q=1-q2;Field_[At(x,y,z)]+=k.Weight*q*q*q;}
        }
    }
}
void AnisotropicSurfaceMesh::RebuildMeshBrick(int bx,int by,int bz){Chunk& chunk=Chunks_[(bz*By+by)*Bx+bx];chunk.TriangleVertices.clear();const int x1=std::min(Nx-1,(bx+1)*Brick),y1=std::min(Ny-1,(by+1)*Brick),z1=std::min(Nz-1,(bz+1)*Brick);for(int z=bz*Brick;z<z1;++z)for(int y=by*Brick;y<y1;++y)for(int x=bx*Brick;x<x1;++x){std::array<Vec3,8> p{};std::array<float,8> value{};int cube=0;for(int c=0;c<8;++c){p[c]=GridPosition(x+Corner[c][0],y+Corner[c][1],z+Corner[c][2]);value[c]=Field_[At(x+Corner[c][0],y+Corner[c][1],z+Corner[c][2])];if(value[c]>=IsoValue_)cube|=1<<c;}const auto mask=MarchingCubesTables::Edge[cube];if(!mask)continue;std::array<EdgeVertex,12> edge{};for(int e=0;e<12;++e)if(mask&(1<<e)){const int a=EdgeCorners[e][0],b=EdgeCorners[e][1];const float denominator=value[b]-value[a],t=std::abs(denominator)>1e-8f?std::clamp((IsoValue_-value[a])/denominator,0.0f,1.0f):.5f;const auto na=static_cast<std::uint32_t>(At(x+Corner[a][0],y+Corner[a][1],z+Corner[a][2])),nb=static_cast<std::uint32_t>(At(x+Corner[b][0],y+Corner[b][1],z+Corner[b][2])),lo=std::min(na,nb),hi=std::max(na,nb);edge[e]={p[a]+(p[b]-p[a])*t,(static_cast<std::uint64_t>(lo)<<32)|hi};}const int offset=cube*16;for(int t=0;t<16&&MarchingCubesTables::Tri[offset+t]>=0;t+=3){chunk.TriangleVertices.push_back(edge[MarchingCubesTables::Tri[offset+t]]);chunk.TriangleVertices.push_back(edge[MarchingCubesTables::Tri[offset+t+1]]);chunk.TriangleVertices.push_back(edge[MarchingCubesTables::Tri[offset+t+2]]);}}}
void AnisotropicSurfaceMesh::AssembleAndSmooth(){Mesh_={};std::vector<uint32_t> weld(Nx*Ny*Nz*3,0xffffffffu);for(const Chunk& chunk:Chunks_)for(const EdgeVertex& vertex:chunk.TriangleVertices){const uint32_t lo=uint32_t(vertex.EdgeKey>>32),hi=uint32_t(vertex.EdgeKey),axis=(hi-lo==1)?0:(hi-lo==Nx)?1:2;
        auto& slot=weld[lo*3+axis];if(slot==0xffffffffu){slot=static_cast<uint32_t>(Mesh_.Vertices.size());Mesh_.Vertices.push_back({vertex.Position,{}});}Mesh_.Indices.push_back(slot);}if(Mesh_.Indices.empty()){OpenEdgeCount_=0;NonManifoldEdgeCount_=0;return;}std::vector<uint64_t> edgeUse;edgeUse.reserve(Mesh_.Indices.size());for(std::size_t i=0;i<Mesh_.Indices.size();i+=3)for(int e=0;e<3;++e){std::uint32_t a=Mesh_.Indices[i+e],b=Mesh_.Indices[i+(e+1)%3];if(a>b)std::swap(a,b);edgeUse.push_back((static_cast<std::uint64_t>(a)<<32)|b);}OpenEdgeCount_=0;NonManifoldEdgeCount_=0;std::sort(edgeUse.begin(),edgeUse.end());for(size_t i=0;i<edgeUse.size();){size_t j=i+1;while(j<edgeUse.size()&&edgeUse[j]==edgeUse[i])++j;const auto uses=j-i;if(uses==1)++OpenEdgeCount_;else if(uses!=2)++NonManifoldEdgeCount_;i=j;}
    const auto smoothStart=std::chrono::steady_clock::now();
    std::vector<size_t> offsets(Mesh_.Vertices.size()+1,0);
    for(auto id:Mesh_.Indices)offsets[id+1]+=2;
    for(size_t i=1;i<offsets.size();++i)offsets[i]+=offsets[i-1];
    std::vector<uint32_t> adjacent(offsets.back());auto cursor=offsets;
    for(size_t i=0;i<Mesh_.Indices.size();i+=3)for(int e=0;e<3;++e){const auto a=Mesh_.Indices[i+e],b=Mesh_.Indices[i+(e+1)%3];adjacent[cursor[a]++]=b;adjacent[cursor[b]++]=a;}
    auto volume=[&](){double result=0;for(std::size_t i=0;i<Mesh_.Indices.size();i+=3){const Vec3 a=Mesh_.Vertices[Mesh_.Indices[i]].Position,b=Mesh_.Vertices[Mesh_.Indices[i+1]].Position,c=Mesh_.Vertices[Mesh_.Indices[i+2]].Position;result+=Dot(a,Cross(b,c))/6.0;}return std::abs(result);};const double originalVolume=volume();Vec3 centroid{};for(const auto& v:Mesh_.Vertices)centroid+=v.Position;centroid=centroid/static_cast<float>(Mesh_.Vertices.size());
    auto laplace=[&](float amount){std::vector<Vec3> next(Mesh_.Vertices.size());for(std::size_t i=0;i<next.size();++i){if(offsets[i]==offsets[i+1]){next[i]=Mesh_.Vertices[i].Position;continue;}Vec3 average{};for(size_t j=offsets[i];j<offsets[i+1];++j)average+=Mesh_.Vertices[adjacent[j]].Position;average=average/static_cast<float>(offsets[i+1]-offsets[i]);next[i]=Mesh_.Vertices[i].Position+(average-Mesh_.Vertices[i].Position)*amount;}for(std::size_t i=0;i<next.size();++i)Mesh_.Vertices[i].Position=next[i];};laplace(.18f);laplace(-.19f);const double smoothedVolume=volume();if(originalVolume>1e-12&&smoothedVolume>1e-12){const float scale=static_cast<float>(std::cbrt(originalVolume/smoothedVolume));for(auto& v:Mesh_.Vertices)v.Position=centroid+(v.Position-centroid)*scale;}for(auto& v:Mesh_.Vertices)v.Normal=Normalized(Gradient(v.Position)*-1.0f);
    Timings_.SmoothingMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-smoothStart).count();
}
bool AnisotropicSurfaceMesh::SaveObj(const std::string& path)const{std::ofstream out(path);if(!out)return false;out<<"# Project Fluid summed anisotropic field / Marching Cubes\n";for(const auto& v:Mesh_.Vertices)out<<"v "<<v.Position.x<<' '<<v.Position.y<<' '<<v.Position.z<<"\nvn "<<v.Normal.x<<' '<<v.Normal.y<<' '<<v.Normal.z<<'\n';for(std::size_t i=0;i<Mesh_.Indices.size();i+=3){const auto a=Mesh_.Indices[i]+1,b=Mesh_.Indices[i+1]+1,c=Mesh_.Indices[i+2]+1;out<<"f "<<a<<"//"<<a<<' '<<b<<"//"<<b<<' '<<c<<"//"<<c<<'\n';}return static_cast<bool>(out);}
void AnisotropicSurfaceMesh::Update(const std::vector<SurfaceKernel>& kernels){
    using Clock=std::chrono::steady_clock;
    auto elapsed=[](auto t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();};
    Timings_={};auto start=Clock::now();
    std::vector<std::uint8_t> dirty(Bx*By*Bz,0);
    if(ForceRebuild_||Previous_.size()!=kernels.size())std::fill(dirty.begin(),dirty.end(),1);
    else for(std::size_t i=0;i<kernels.size();++i){const auto& a=Previous_[i];const auto& b=kernels[i];
        if(!(a.Centre==b.Centre)||!(a.AxisA==b.AxisA)||!(a.AxisB==b.AxisB)||!(a.AxisC==b.AxisC)||a.VolumeWeight!=b.VolumeWeight){MarkKernel(a,dirty);MarkKernel(b,dirty);}}
    DirtyBrickCount_=0;for(auto d:dirty)DirtyBrickCount_+=d;
    ForceRebuild_=false;Previous_=kernels;if(DirtyBrickCount_==0){Timings_.IndexMs=elapsed(start);return;}
    Prepare(kernels);Timings_.IndexMs=elapsed(start);start=Clock::now();
    for(int z=0;z<Bz;++z)for(int y=0;y<By;++y)for(int x=0;x<Bx;++x)if(dirty[(z*By+y)*Bx+x])RebuildFieldBrick(x,y,z,kernels);
    Timings_.FieldMs=elapsed(start);start=Clock::now();
    for(int z=0;z<Bz;++z)for(int y=0;y<By;++y)for(int x=0;x<Bx;++x)if(dirty[(z*By+y)*Bx+x])RebuildMeshBrick(x,y,z);
    Timings_.TrianglesMs=elapsed(start);start=Clock::now();AssembleAndSmooth();Timings_.AssemblyMs=elapsed(start);
}
}
