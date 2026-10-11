#include "PbfFluid.h"
#include "SurfaceReconstruction.h"
#include "AnisotropicSurfaceMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace PF = Frontier::ProjectFluid;
namespace {
struct Pixel { float r{},g{},b{}; };
struct Screen { float x{},y{},z{}; bool visible{}; };
PF::Vec3 Cross(PF::Vec3 a,PF::Vec3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Pixel Mix(Pixel a,Pixel b,float t){t=std::clamp(t,0.0f,1.0f);return{a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}

class ProofRenderer {
public:
    ProofRenderer(std::uint32_t width,std::uint32_t height):W(width),H(height),Colour(W*H),Depth(W*H,std::numeric_limits<float>::max()) {
        Camera={4.4f,3.0f,5.2f};const PF::Vec3 target{0.0f,0.9f,0.0f};Forward=PF::Normalized(target-Camera);Right=PF::Normalized(Cross(Forward,{0,1,0}));Up=PF::Normalized(Cross(Right,Forward));Focal=(H*0.5f)/std::tan(52.0f*3.14159265f/360.0f);
        for(std::uint32_t y=0;y<H;++y)for(std::uint32_t x=0;x<W;++x){float t=static_cast<float>(y)/H;Colour[y*W+x]=Mix({0.055f,0.075f,0.105f},{0.22f,0.25f,0.27f},t);}
    }
    Screen Project(PF::Vec3 p)const{PF::Vec3 q=p-Camera;float z=PF::Dot(q,Forward);return{W*.5f+PF::Dot(q,Right)*Focal/z,H*.5f-PF::Dot(q,Up)*Focal/z,z,z>.05f};}
    void Line(PF::Vec3 a,PF::Vec3 b,Pixel c,int thickness=2){Screen x=Project(a),y=Project(b);if(!x.visible||!y.visible)return;int steps=std::max(1,static_cast<int>(std::max(std::abs(y.x-x.x),std::abs(y.y-x.y))));for(int i=0;i<=steps;++i){float t=static_cast<float>(i)/steps;int px=static_cast<int>(x.x+(y.x-x.x)*t),py=static_cast<int>(x.y+(y.y-x.y)*t);for(int oy=-thickness;oy<=thickness;++oy)for(int ox=-thickness;ox<=thickness;++ox)if(px+ox>=0&&py+oy>=0&&px+ox<(int)W&&py+oy<(int)H)Colour[(py+oy)*W+px+ox]=c;}}
    void Sphere(PF::Vec3 centre,float radius,Pixel base,bool obstacle=false,float roughness=.1f,float opacity=1.0f,float ior=1.33f,PF::Vec3 absorption={}){
        Screen s=Project(centre);if(!s.visible)return;int r=std::clamp(static_cast<int>(Focal*radius/s.z),1,90);
        PF::Vec3 light=PF::Normalized({-.4f,.8f,.3f});PF::Vec3 halfVector=PF::Normalized(light+PF::Vec3{0,0,1});
        for(int oy=-r;oy<=r;++oy)for(int ox=-r;ox<=r;++ox){
            float nx=static_cast<float>(ox)/r,ny=-static_cast<float>(oy)/r,d2=nx*nx+ny*ny;if(d2>1.0f)continue;
            float nz=std::sqrt(std::max(0.0f,1.0f-d2));int x=static_cast<int>(s.x)+ox,y=static_cast<int>(s.y)+oy;
            if(x<0||y<0||x>=(int)W||y>=(int)H) continue;
            float z=s.z-radius*nz;std::size_t index=static_cast<std::size_t>(y)*W+x;
            if(z>=Depth[index]) continue;
            Depth[index]=z;PF::Vec3 normal{nx,ny,nz};
            float lit=.14f+.72f*std::max(0.0f,PF::Dot(normal,light));float edge=std::pow(1.0f-nz,3.0f);
            float f0=std::pow((ior-1.0f)/(ior+1.0f),2.0f),fresnel=f0+(1.0f-f0)*std::pow(1.0f-nz,5.0f);
            float specular=std::pow(std::max(0.0f,PF::Dot(normal,halfVector)),120.0f*(1.0f-roughness)+16.0f)*(.9f-roughness*.45f);
            Pixel colour{base.r*lit,base.g*lit,base.b*lit};
            Pixel transmitted{base.r*.24f+.08f*std::exp(-absorption.x*radius*3.0f),base.g*.24f+.11f*std::exp(-absorption.y*radius*3.0f),base.b*.24f+.13f*std::exp(-absorption.z*radius*3.0f)};
            colour=Mix(colour,transmitted,(1.0f-opacity)*.58f);colour.r+=specular+fresnel*.34f+edge*.06f;colour.g+=specular*.94f+fresnel*.38f+edge*.07f;colour.b+=specular*.82f+fresnel*.45f+edge*.09f;
            if(obstacle){float stripe=std::fmod(std::abs(nx+ny)*12.0f,2.0f)<1.0f?1.0f:.3f;colour=Mix(colour,{1.0f,.24f,.025f},stripe*.35f);}Colour[index]=colour;
        }
    }

    void FluidSurface(const std::vector<PF::SurfaceKernel>& kernels,const PF::FluidMaterial& material){
        const std::size_t count=static_cast<std::size_t>(W)*H;
        std::vector<float> front(count,std::numeric_limits<float>::max()),thickness(count,0.0f),scratch(count,std::numeric_limits<float>::max());
        for(const PF::SurfaceKernel& kernel:kernels){
            const PF::Vec3 centre=kernel.Centre;Screen s=Project(centre);if(!s.visible)continue;
            const Screen ea=Project(centre+kernel.AxisA),eb=Project(centre+kernel.AxisB),ec=Project(centre+kernel.AxisC);
            const float ax=ea.x-s.x,ay=ea.y-s.y,bx=eb.x-s.x,by=eb.y-s.y,cx=ec.x-s.x,cy=ec.y-s.y;
            const float m00=ax*ax+bx*bx+cx*cx,m01=ax*ay+bx*by+cx*cy,m11=ay*ay+by*by+cy*cy,trace=m00+m11,disc=std::sqrt(std::max(0.0f,(m00-m11)*(m00-m11)+4*m01*m01));
            const float majorPixels=std::sqrt(std::max(1.0f,(trace+disc)*.5f)),minorPixels=std::sqrt(std::max(1.0f,(trace-disc)*.5f)),angle=.5f*std::atan2(2*m01,m00-m11),ux=std::cos(angle),uy=std::sin(angle);
            const float da=PF::Dot(kernel.AxisA,Forward),db=PF::Dot(kernel.AxisB,Forward),dc=PF::Dot(kernel.AxisC,Forward),depthRadius=std::sqrt(da*da+db*db+dc*dc);const int extent=std::clamp(static_cast<int>(majorPixels)+1,1,72);
            for(int oy=-extent;oy<=extent;++oy)for(int ox=-extent;ox<=extent;++ox){
                const float along=(ox*ux+oy*uy)/majorPixels,across=(-ox*uy+oy*ux)/minorPixels,d2=along*along+across*across;if(d2>1.0f)continue;
                const int x=static_cast<int>(s.x)+ox,y=static_cast<int>(s.y)+oy;if(x<0||y<0||x>=(int)W||y>=(int)H)continue;
                const float nz=std::sqrt(1.0f-d2);const std::size_t at=static_cast<std::size_t>(y)*W+x;
                front[at]=std::min(front[at],s.z-depthRadius*nz);thickness[at]+=2.0f*depthRadius*nz*kernel.VolumeWeight;
            }
        }
        // Six edge-aware depth/thickness passes mirror the source renderer's
        // bilateral reconstruction and close sub-pixel gaps between samples.
        for(int pass=0;pass<12;++pass){
            for(std::uint32_t y=1;y+1<H;++y)for(std::uint32_t x=1;x+1<W;++x){
                std::size_t at=static_cast<std::size_t>(y)*W+x;float centre=front[at],sum=0,weight=0;
                for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox){float sample=front[(y+oy)*W+x+ox];if(!std::isfinite(sample))continue;float spatial=(ox==0&&oy==0)?2.0f:1.0f;float range=std::isfinite(centre)?std::exp(-std::abs(sample-centre)*1.0f):1.0f;sum+=sample*spatial*range;weight+=spatial*range;}
                scratch[at]=weight>0?sum/weight:std::numeric_limits<float>::max();
            }
            front.swap(scratch);std::fill(scratch.begin(),scratch.end(),std::numeric_limits<float>::max());
        }
        std::fill(scratch.begin(),scratch.end(),0.0f);
        for(int pass=0;pass<5;++pass){
            for(std::uint32_t y=1;y+1<H;++y)for(std::uint32_t x=1;x+1<W;++x){std::size_t at=static_cast<std::size_t>(y)*W+x;scratch[at]=(thickness[at]*4.0f+thickness[at-1]+thickness[at+1]+thickness[at-W]+thickness[at+W])/8.0f;}
            thickness.swap(scratch);std::fill(scratch.begin(),scratch.end(),0.0f);
        }
        const std::vector<Pixel> backdrop=Colour;PF::Vec3 light=PF::Normalized({-.4f,.8f,.3f});PF::Vec3 halfVector=PF::Normalized(light+PF::Vec3{0,0,1});
        for(std::uint32_t y=3;y+3<H;++y)for(std::uint32_t x=3;x+3<W;++x){
            std::size_t at=static_cast<std::size_t>(y)*W+x;if(!std::isfinite(front[at])||thickness[at]<.040f)continue;
            float left=front[at-3],right=front[at+3],top=front[at-3*W],bottom=front[at+3*W];if(!std::isfinite(left)||!std::isfinite(right)||!std::isfinite(top)||!std::isfinite(bottom))continue;
            PF::Vec3 normal=PF::Normalized({-(right-left)*3.0f,(bottom-top)*3.0f,1.0f});float diffuse=std::max(0.0f,PF::Dot(normal,light));
            float f0=std::pow((material.Ior-1.0f)/(material.Ior+1.0f),2.0f);float fresnel=f0+(1.0f-f0)*std::pow(1.0f-normal.z,5.0f);
            float specular=std::pow(std::max(0.0f,PF::Dot(normal,halfVector)),110.0f*(1.0f-material.Roughness)+14.0f)*(1.0f-material.Roughness*.55f);
            int rx=std::clamp(static_cast<int>(x-normal.x*thickness[at]*38.0f),0,static_cast<int>(W)-1),ry=std::clamp(static_cast<int>(y+normal.y*thickness[at]*38.0f),0,static_cast<int>(H)-1);Pixel behind=backdrop[static_cast<std::size_t>(ry)*W+rx];
            Pixel transmit{behind.r*std::exp(-material.Absorption.x*thickness[at]*2.8f)+material.Colour.x*.12f,behind.g*std::exp(-material.Absorption.y*thickness[at]*2.8f)+material.Colour.y*.12f,behind.b*std::exp(-material.Absorption.z*thickness[at]*2.8f)+material.Colour.z*.12f};
            Pixel base{material.Colour.x*(.18f+.65f*diffuse),material.Colour.y*(.18f+.65f*diffuse),material.Colour.z*(.18f+.65f*diffuse)};
            Pixel c=Mix(transmit,base,material.Opacity);c.r+=specular+fresnel*.38f;c.g+=specular*.95f+fresnel*.42f;c.b+=specular*.84f+fresnel*.50f;
            Colour[at]=c;Depth[at]=front[at];
        }
    }
    void FluidMesh(const PF::SurfaceMesh& mesh,const PF::FluidMaterial& material){
        const std::size_t count=static_cast<std::size_t>(W)*H;std::vector<float> front(count,std::numeric_limits<float>::max()),back(count,-std::numeric_limits<float>::max());std::vector<PF::Vec3> normal(count);
        for(std::size_t triangle=0;triangle+2<mesh.Indices.size();triangle+=3){const auto& va=mesh.Vertices[mesh.Indices[triangle]],&vb=mesh.Vertices[mesh.Indices[triangle+1]],&vc=mesh.Vertices[mesh.Indices[triangle+2]];const Screen a=Project(va.Position),b=Project(vb.Position),c=Project(vc.Position);if(!a.visible||!b.visible||!c.visible)continue;const float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);if(std::abs(area)<1e-5f)continue;const int minX=std::clamp(static_cast<int>(std::floor(std::min({a.x,b.x,c.x}))),0,static_cast<int>(W)-1),maxX=std::clamp(static_cast<int>(std::ceil(std::max({a.x,b.x,c.x}))),0,static_cast<int>(W)-1),minY=std::clamp(static_cast<int>(std::floor(std::min({a.y,b.y,c.y}))),0,static_cast<int>(H)-1),maxY=std::clamp(static_cast<int>(std::ceil(std::max({a.y,b.y,c.y}))),0,static_cast<int>(H)-1);
            for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x){const float px=x+.5f,py=y+.5f,w0=((b.x-px)*(c.y-py)-(b.y-py)*(c.x-px))/area,w1=((c.x-px)*(a.y-py)-(c.y-py)*(a.x-px))/area,w2=1-w0-w1;if(w0<0||w1<0||w2<0)continue;const float z=w0*a.z+w1*b.z+w2*c.z;const std::size_t at=static_cast<std::size_t>(y)*W+x;if(z<front[at]){front[at]=z;normal[at]=PF::Normalized(va.Normal*w0+vb.Normal*w1+vc.Normal*w2);}back[at]=std::max(back[at],z);}}
        const std::vector<Pixel> backdrop=Colour;const PF::Vec3 light=PF::Normalized({-.4f,.8f,.3f}),halfVector=PF::Normalized(light+PF::Vec3{0,0,1});for(std::uint32_t y=0;y<H;++y)for(std::uint32_t x=0;x<W;++x){const std::size_t at=static_cast<std::size_t>(y)*W+x;if(!std::isfinite(front[at])||back[at]<front[at])continue;const PF::Vec3 worldNormal=normal[at],viewNormal=PF::Normalized({PF::Dot(worldNormal,Right),PF::Dot(worldNormal,Up),-PF::Dot(worldNormal,Forward)});const float thickness=std::clamp(back[at]-front[at],.015f,.9f),diffuse=std::max(0.0f,PF::Dot(viewNormal,light)),f0=std::pow((material.Ior-1)/(material.Ior+1),2.0f),fresnel=f0+(1-f0)*std::pow(1-std::max(0.0f,viewNormal.z),5.0f),specular=std::pow(std::max(0.0f,PF::Dot(viewNormal,halfVector)),110*(1-material.Roughness)+14)*(1-material.Roughness*.55f);const int rx=std::clamp(static_cast<int>(x-viewNormal.x*thickness*38),0,static_cast<int>(W)-1),ry=std::clamp(static_cast<int>(y+viewNormal.y*thickness*38),0,static_cast<int>(H)-1);const Pixel behind=backdrop[static_cast<std::size_t>(ry)*W+rx],transmit{behind.r*std::exp(-material.Absorption.x*thickness*2.8f)+material.Colour.x*.12f,behind.g*std::exp(-material.Absorption.y*thickness*2.8f)+material.Colour.y*.12f,behind.b*std::exp(-material.Absorption.z*thickness*2.8f)+material.Colour.z*.12f},base{material.Colour.x*(.18f+.65f*diffuse),material.Colour.y*(.18f+.65f*diffuse),material.Colour.z*(.18f+.65f*diffuse)};Pixel colour=Mix(transmit,base,material.Opacity);colour.r+=specular+fresnel*.38f;colour.g+=specular*.95f+fresnel*.42f;colour.b+=specular*.84f+fresnel*.5f;Colour[at]=colour;Depth[at]=front[at];}
    }
    void Bounds(bool drawFloorGrid=true){PF::Vec3 lo=PF::PbfFluid::BoundsMin(),hi=PF::PbfFluid::BoundsMax();Pixel c{1.0f,.32f,.05f};std::array<PF::Vec3,8> p{{{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}}};int e[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};for(auto& q:e)Line(p[q[0]],p[q[1]],c,1);if(drawFloorGrid){for(float x=-1.5f;x<=1.5f;x+=.5f)Line({x,lo.y,lo.z},{x,lo.y,hi.z},{.23f,.27f,.29f},0);for(float z=-1;z<=1;z+=.5f)Line({lo.x,lo.y,z},{hi.x,lo.y,z},{.23f,.27f,.29f},0);}}
    bool Save(const std::string& path){std::ofstream f(path,std::ios::binary);if(!f)return false;f<<"P6\n"<<W<<' '<<H<<"\n255\n";for(auto c:Colour){for(float v:{c.r,c.g,c.b}){unsigned char q=static_cast<unsigned char>(std::pow(std::clamp(v,0.0f,1.0f),1.0f/2.2f)*255+.5f);f.write(reinterpret_cast<char*>(&q),1);}}return(bool)f;}
private:
    std::uint32_t W,H;std::vector<Pixel>Colour;std::vector<float>Depth;PF::Vec3 Camera,Forward,Right,Up;float Focal{};
};
}

int main(int argc,char**argv){
    PF::PbfFluid fluid;fluid.SetObstacle(true);
    if(argc>3){const std::string material=argv[3];if(material=="milk")fluid.SetMaterial(PF::Material::Milk);else if(material=="honey")fluid.SetMaterial(PF::Material::Honey);else if(material=="chocolate")fluid.SetMaterial(PF::Material::Chocolate);}
    int steps=argc>2?std::max(0,std::stoi(argv[2])):18;
    for(int i=0;i<steps;++i){fluid.Pour(1.0f/60.0f);fluid.Step(1.0f/60.0f);}
    const auto& d=fluid.Diagnostics();
    const PF::Vec3 lo=PF::PbfFluid::BoundsMin(),hi=PF::PbfFluid::BoundsMax(),centre=PF::PbfFluid::ObstacleCentre();
    float minimumObstacleDistance=1e9f;
    for(const auto& p:fluid.Positions()) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||p.x<lo.x-1e-5f||p.x>hi.x+1e-5f||p.y<lo.y-1e-5f||p.y>hi.y+1e-5f||p.z<lo.z-1e-5f||p.z>hi.z+1e-5f) return 3;
        minimumObstacleDistance=std::min(minimumObstacleDistance,PF::Length(p-centre));
    }
    if(fluid.ObstacleEnabled()&&minimumObstacleDistance<PF::PbfFluid::ObstacleRadius()-1e-5f) return 4;
    std::cout<<std::fixed<<std::setprecision(5)<<"Flux PBF C++ mirror | material "<<fluid.ActiveMaterial().Name<<" | particles "<<fluid.Positions().size()<<" | t "<<fluid.Time()<<" s\n"
             <<"bounds [-1.95,1.95] x [0.19,3.70] x [-1.25,1.25] m | sphere contacts "<<d.SphereContacts<<" | wall contacts "<<d.WallContacts<<" | min sphere distance "<<minimumObstacleDistance<<" m\n"
             <<"pressure passes "<<d.PressureIterations<<" | mean/peak compression "<<d.MeanCompression<<" / "<<d.PeakCompression<<"\n"
             <<"viscosity PCG iterations "<<d.ViscosityIterations<<" | relative residual "<<d.ViscosityRelativeResidual<<"\n";
    if(argc>1){ProofRenderer renderer(1280,720);renderer.Bounds();const auto& material=fluid.ActiveMaterial();PF::SurfaceReconstruction reconstruction;reconstruction.Update(fluid.Positions());PF::AnisotropicSurfaceMesh surface;surface.Update(reconstruction.Kernels());const auto initialDirty=surface.DirtyBrickCount();surface.Update(reconstruction.Kernels());renderer.FluidMesh(surface.Mesh(),material);std::cout<<"surface mesh: "<<surface.Mesh().Vertices.size()<<" vertices | "<<surface.Mesh().Indices.size()/3<<" triangles | dirty bricks "<<initialDirty<<" initial / "<<surface.DirtyBrickCount()<<" unchanged | open/nonmanifold edges "<<surface.OpenEdgeCount()<<'/'<<surface.NonManifoldEdgeCount()<<"\n";if(argc>4&&!surface.SaveObj(argv[4]))return 5;renderer.Sphere(PF::PbfFluid::ObstacleCentre(),PF::PbfFluid::ObstacleRadius(),{.16f,.18f,.20f},true,.25f,1.0f,1.45f);renderer.Bounds(false);if(!renderer.Save(argv[1]))return 2;std::cout<<"proof frame: "<<argv[1]<<'\n';}
    return std::isfinite(d.PeakCompression)?0:1;
}
