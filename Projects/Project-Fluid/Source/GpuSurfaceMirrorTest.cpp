#include "GpuSurfaceData.h"
#include <iostream>
#include <stdexcept>
#include <map>
using namespace Frontier::ProjectFluid;
void Check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(){try{
 SurfaceKernel kernel{{.04f,.5f,.02f},{.28f,0,0},{0,.17f,0},{0,0,.22f},1};
 auto input=PrepareGpuSurface({kernel});auto output=MirrorGpuSurface(input);Check(output.Indices.size()>0&&!output.Overflow,"mesh missing");
 auto p=input.Parameters;const int nx=p.Grid[0],ny=p.Grid[1],nz=p.Grid[2];float maximum=0;
 for(int z=0;z<nz;++z)for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){const Vec3 r=Vec3{p.OriginSpacing.x+x*p.OriginSpacing.w,p.OriginSpacing.y+y*p.OriginSpacing.w,p.OriginSpacing.z+z*p.OriginSpacing.w}-kernel.Centre;
  float u=Dot(r,kernel.AxisA)/Dot(kernel.AxisA,kernel.AxisA),v=Dot(r,kernel.AxisB)/Dot(kernel.AxisB,kernel.AxisB),w=Dot(r,kernel.AxisC)/Dot(kernel.AxisC,kernel.AxisC),q=1-u*u-v*v-w*w;
  maximum=std::max(maximum,std::abs(output.Field[(z*ny+y)*nx+x]-(q>0?q*q*q:0)));}
 Check(maximum<1e-5f,"field differs from exhaustive evaluation");
 for(auto i:output.Indices){Check(i<output.Vertices.size()&&output.Vertices[i].Position.w==1,"invalid edge vertex");const auto n=output.Vertices[i].Normal;Check(std::abs(Length({n.x,n.y,n.z})-1)<1e-4f,"nonunit normal");}
 std::map<uint64_t,unsigned> usage;double orientation=0;
 for(size_t i=0;i<output.Indices.size();i+=3){
  for(int e=0;e<3;++e){auto a=output.Indices[i+e],b=output.Indices[i+(e+1)%3];if(a>b)std::swap(a,b);++usage[(uint64_t(a)<<32)|b];}
  const auto a=output.Vertices[output.Indices[i]].Position,b=output.Vertices[output.Indices[i+1]].Position,c=output.Vertices[output.Indices[i+2]].Position,n=output.Vertices[output.Indices[i]].Normal;
  const Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};orientation+=Dot({u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x},{n.x,n.y,n.z});
 }
 for(auto [edge,count]:usage){(void)edge;Check(count==2,"non-watertight ellipsoid");}
 Check(orientation>0,"inward mesh winding");
 input.Parameters.Config[0]=3;auto overflow=MirrorGpuSurface(input);Check(overflow.Overflow&&overflow.Indices.size()==3,"capacity guard");
 auto empty=MirrorGpuSurface(PrepareGpuSurface({}));Check(empty.Indices.empty()&&empty.RequestedIndices==0,"empty field");
 kernel.Centre.y=3.7f;auto high=MirrorGpuSurface(PrepareGpuSurface({kernel}));Check(!high.Indices.empty(),"upper fluid domain clipped");
 PbfFluid fluid;for(int i=0;i<3;++i)fluid.Step(1.f/60);SurfaceReconstruction reconstruction;reconstruction.Update(fluid.Positions());
 const auto realInput=PrepareGpuSurface(reconstruction.Kernels());const auto real=MirrorGpuSurface(realInput);
 Check(!real.Overflow&&!real.Indices.empty(),"real fluid output");
 for(auto id:real.Indices)Check(id<real.Vertices.size()&&real.Vertices[id].Position.w==1,"real fluid dangling edge");
 for(size_t id=0;id<real.Field.size();id+=251){const int x=int(id)%nx,y=int(id/nx)%ny,z=int(id/(nx*ny));const Vec3 point{p.OriginSpacing.x+x*p.OriginSpacing.w,p.OriginSpacing.y+y*p.OriginSpacing.w,p.OriginSpacing.z+z*p.OriginSpacing.w};float f=0;
  for(const auto& k:reconstruction.Kernels()){const auto r=point-k.Centre;const float a=Dot(r,k.AxisA)/Dot(k.AxisA,k.AxisA),b=Dot(r,k.AxisB)/Dot(k.AxisB,k.AxisB),c=Dot(r,k.AxisC)/Dot(k.AxisC,k.AxisC),q2=a*a+b*b+c*c;if(q2<1){const auto q=1-q2;f+=k.VolumeWeight*q*q*q;}}
  Check(std::abs(real.Field[id]-f)<2e-4f*std::max(1.f,std::abs(f)),"real fluid exhaustive field mismatch");
 }
 std::cout<<"PASS GPU-layout CPU mirror: field, edge indexing, normals, overflow, empty and upper domain\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
