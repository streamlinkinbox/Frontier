#include "SurfaceReconstruction.h"
#include <algorithm>
#include <cmath>

namespace Frontier::ProjectFluid {
namespace { constexpr float Pi=3.14159265358979323846f; }

void SurfaceReconstruction::Diagonalize(std::array<float,9>& c,std::array<float,9>& v) noexcept {
    v={1,0,0,0,1,0,0,0,1};
    auto rotate=[&](int p,int q){const int pq=p*3+q;if(std::abs(c[pq])<1e-12f)return;const float angle=.5f*std::atan2(2*c[pq],c[q*3+q]-c[p*3+p]),cs=std::cos(angle),sn=std::sin(angle),app=c[p*3+p],aqq=c[q*3+q],apq=c[pq];
        for(int k=0;k<3;++k){if(k!=p&&k!=q){const float kp=c[k*3+p],kq=c[k*3+q];c[k*3+p]=c[p*3+k]=cs*kp-sn*kq;c[k*3+q]=c[q*3+k]=sn*kp+cs*kq;}const float vp=v[k*3+p],vq=v[k*3+q];v[k*3+p]=cs*vp-sn*vq;v[k*3+q]=sn*vp+cs*vq;}
        c[p*3+p]=cs*cs*app-2*sn*cs*apq+sn*sn*aqq;c[q*3+q]=sn*sn*app+2*sn*cs*apq+cs*cs*aqq;c[pq]=c[q*3+p]=0;};
    for(int sweep=0;sweep<5;++sweep){rotate(0,1);rotate(0,2);rotate(1,2);}
}

void SurfaceReconstruction::Update(const std::vector<Vec3>& positions) {
    constexpr float support=.42f,support2=support*support;
    Kernels_.resize(positions.size());
    for(std::size_t i=0;i<positions.size();++i){const Vec3 origin=positions[i];float weight=0,xx=0,xy=0,xz=0,yy=0,yz=0,zz=0;Vec3 mean{};int neighbors=0;
        for(const Vec3& sample:positions){const Vec3 d=sample-origin;const float r2=Dot(d,d);if(r2>=support2)continue;const float w=1-r2*std::sqrt(r2)/(support*support2);weight+=w;mean+=d*w;xx+=w*d.x*d.x;xy+=w*d.x*d.y;xz+=w*d.x*d.z;yy+=w*d.y*d.y;yz+=w*d.y*d.z;zz+=w*d.z*d.z;++neighbors;}
        SurfaceKernel& kernel=Kernels_[i];
        auto sphere=[&](float radius){kernel={origin,{radius,0,0},{0,radius,0},{0,0,radius},(1.0f/PbfFluid::RestDensity)/(4*Pi/3*radius*radius*radius)};};
        if(neighbors<8||weight<1e-8f){sphere(.097f);continue;}mean=mean/weight;
        std::array<float,9> covariance{xx/weight-mean.x*mean.x,xy/weight-mean.x*mean.y,xz/weight-mean.x*mean.z,xy/weight-mean.x*mean.y,yy/weight-mean.y*mean.y,yz/weight-mean.y*mean.z,xz/weight-mean.x*mean.z,yz/weight-mean.y*mean.z,zz/weight-mean.z*mean.z},rotation{};
        Diagonalize(covariance,rotation);const float maximum=std::max({covariance[0],covariance[4],covariance[8],1e-8f});const float e0=std::max(covariance[0],maximum/3),e1=std::max(covariance[4],maximum/3),e2=std::max(covariance[8],maximum/3);const float radius=.097f+.058f*std::min(1.0f,(neighbors-7)/20.0f),factor=radius/std::cbrt(e0*e1*e2),a=e0*factor,b=e1*factor,c=e2*factor,smoothing=.3f*std::min(1.0f,(neighbors-7)/20.0f);
        kernel.Centre=origin+mean*smoothing;kernel.AxisA={rotation[0]*a,rotation[3]*a,rotation[6]*a};kernel.AxisB={rotation[1]*b,rotation[4]*b,rotation[7]*b};kernel.AxisC={rotation[2]*c,rotation[5]*c,rotation[8]*c};kernel.VolumeWeight=(1.0f/PbfFluid::RestDensity)/(4*Pi/3*radius*radius*radius);
    }
}
}
