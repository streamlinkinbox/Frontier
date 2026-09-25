#include "PbfFluid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Frontier::ProjectFluid {
namespace {
constexpr float Pi = 3.14159265358979323846f;
constexpr std::array<FluidMaterial, 4> Materials{{
    {"Water",     {0.36f,0.82f,0.86f}, {0.85f,0.20f,0.12f}, 0.01f,0.09f,1.333f, 0.018f,0.018f,0.45f,0.00f,20.0f},
    {"Milk",      {0.94f,0.90f,0.81f}, {0.20f,0.25f,0.40f}, 0.96f,0.28f,1.350f, 0.090f,0.030f,0.55f,0.08f,20.0f},
    {"Honey",     {0.89f,0.52f,0.07f}, {0.18f,1.60f,5.80f}, 0.08f,0.18f,1.490f, 0.780f,0.080f,0.85f,0.00f,25.0f},
    {"Chocolate", {0.31f,0.12f,0.06f}, {2.10f,4.50f,6.50f}, 0.97f,0.24f,1.460f, 0.580f,0.070f,0.70f,0.80f,40.0f},
}};
float CohesionKernel(float r, float h) noexcept {
    if (r <= 0.0f || r >= h) return 0.0f;
    const float base=std::pow(h-r,3.0f)*std::pow(r,3.0f),factor=32.0f/(Pi*std::pow(h,9.0f));
    return factor*(r>h*.5f?base:2.0f*base-std::pow(h,6.0f)/64.0f);
}
}

Vec3 operator+(Vec3 a, Vec3 b) noexcept { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) noexcept { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 operator*(Vec3 a, float s) noexcept { return {a.x*s,a.y*s,a.z*s}; }
Vec3 operator/(Vec3 a, float s) noexcept { return a * (1.0f/s); }
float Dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
float Length(Vec3 a) noexcept { return std::sqrt(Dot(a,a)); }
Vec3 Normalized(Vec3 a) noexcept { const float l=Length(a); return l>1e-8f?a/l:Vec3{}; }

PbfFluid::PbfFluid() {
    Positions_.reserve(MaxParticles); Velocities_.reserve(MaxParticles); Previous_.reserve(MaxParticles);
    Corrections_.reserve(MaxParticles); BoundaryGradients_.reserve(MaxParticles); Density_.reserve(MaxParticles); Lambda_.reserve(MaxParticles);
    ApparentViscosity_.reserve(MaxParticles); Neighbours_.reserve(MaxParticles);
    Reset();
}

const FluidMaterial& PbfFluid::ActiveMaterial() const noexcept { return Materials[static_cast<std::size_t>(Material_)]; }
void PbfFluid::SetMaterial(Material material) noexcept { Material_ = material; }

void PbfFluid::Add(Vec3 position, Vec3 velocity) {
    if (Positions_.size() >= MaxParticles) return;
    Collide(position);
    Positions_.push_back(position); Velocities_.push_back(velocity); Previous_.push_back(position);
    Corrections_.push_back({}); BoundaryGradients_.push_back({}); Density_.push_back(0.0f); Lambda_.push_back(0.0f);
    ApparentViscosity_.push_back(0.0f); Neighbours_.emplace_back(); Neighbours_.back().reserve(128);
    BoundaryNeighbours_.emplace_back(); BoundaryNeighbours_.back().reserve(96);
}

void PbfFluid::Reset(Experiment experiment) {
    Experiment_ = experiment; Time_ = 0.0f; Emission_ = 0.0f; Diagnostics_ = {};
    Positions_.clear(); Velocities_.clear(); Previous_.clear(); Corrections_.clear(); BoundaryGradients_.clear(); Density_.clear();
    Lambda_.clear(); ApparentViscosity_.clear(); Neighbours_.clear(); BoundaryNeighbours_.clear();
    if (experiment == Experiment::Basin) {
        Gravity_ = 9.81f;
        for (int y=0;y<4;++y) for (int x=0;x<24;++x) for (int z=0;z<15;++z) {
            const float px=(x-11.5f)*0.157f;
            Add({px,0.22f+y*0.157f+0.07f*std::sin(px*1.8f),(z-7.0f)*0.157f});
        }
    } else {
        Gravity_ = experiment == Experiment::WettingDrop ? 1.0f : 0.0f;
        for (int x=-4;x<=4;++x) for(int y=-3;y<=3;++y) for(int z=-4;z<=4;++z)
            if (std::pow(x*0.157f/0.72f,2)+std::pow(y*0.157f/0.48f,2)+std::pow(z*0.157f/0.63f,2)<1.0f)
                Add({x*0.157f,1.0f+y*0.157f,z*0.157f});
    }
    RebuildBoundarySamples(); BuildNeighbours(); BuildBoundaryNeighbours(); ComputeDensity(false);
    if(experiment==Experiment::Basin){SolvePressure();std::fill(Velocities_.begin(),Velocities_.end(),Vec3{});Diagnostics_={};}
}

float PbfFluid::Poly6(float r2) const noexcept {
    constexpr float h=SmoothingRadius;
    if (r2>=h*h) return 0.0f;
    const float q=h*h-r2;
    return 315.0f/(64.0f*Pi*std::pow(h,9.0f))*q*q*q;
}

void PbfFluid::BuildNeighbours() {
    const float h2=SmoothingRadius*SmoothingRadius;
    for(auto& list:Neighbours_) list.clear();
    for(std::uint32_t i=0;i<Positions_.size();++i) {
        auto& list=Neighbours_[i];
        for(std::uint32_t j=0;j<Positions_.size();++j) {
            Vec3 d=Positions_[i]-Positions_[j];
            if(Dot(d,d)<h2 && list.size()<128) list.push_back(static_cast<std::uint16_t>(j));
        }
    }
    BuildBoundaryNeighbours();
}

void PbfFluid::RebuildBoundarySamples() {
    BoundaryPositions_.clear(); BoundaryPseudoMasses_.clear();
    const Vec3 lo=BoundsMin(),hi=BoundsMax(); constexpr float spacing=.157f,clearance=.0785f;
    const float bx=hi.x+clearance,bz=hi.z+clearance,floor=lo.y-clearance,wallTop=1.38f;
    const int nx=static_cast<int>(std::ceil(2.0f*bx/spacing)),nz=static_cast<int>(std::ceil(2.0f*bz/spacing)),ny=9;
    auto add=[&](Vec3 p){for(const Vec3& q:BoundaryPositions_)if(Length(p-q)<1e-5f)return;BoundaryPositions_.push_back(p);};
    for(int ix=0;ix<=nx;++ix)for(int iz=0;iz<=nz;++iz)add({-bx+2*bx*ix/nx,floor,-bz+2*bz*iz/nz});
    for(int iy=1;iy<=ny;++iy){const float y=floor+(wallTop-floor)*iy/ny;
        for(int ix=0;ix<=nx;++ix){const float x=-bx+2*bx*ix/nx;add({x,y,-bz});add({x,y,bz});}
        for(int iz=1;iz<nz;++iz){const float z=-bz+2*bz*iz/nz;add({-bx,y,z});add({bx,y,z});}}
    if(ObstacleEnabled_){const Vec3 c=ObstacleCentre();const float radius=ObstacleRadius();const int samples=static_cast<int>(std::ceil(4*Pi*radius*radius/.021f));
        for(int i=0;i<samples;++i){const float y=1-2*(i+.5f)/samples,r=std::sqrt(1-y*y),a=i*2.39996323f;add(c+Vec3{radius*std::cos(a)*r,radius*y,radius*std::sin(a)*r});}}
    BoundaryPseudoMasses_.resize(BoundaryPositions_.size());
    const float h2=SmoothingRadius*SmoothingRadius;
    for(std::size_t i=0;i<BoundaryPositions_.size();++i){float sum=0;for(const Vec3& q:BoundaryPositions_){const Vec3 d=BoundaryPositions_[i]-q;if(Dot(d,d)<h2)sum+=Poly6(Dot(d,d));}BoundaryPseudoMasses_[i]=RestDensity/std::max(sum,1e-8f);}
}

void PbfFluid::BuildBoundaryNeighbours() {
    const float h2=SmoothingRadius*SmoothingRadius;
    if(BoundaryNeighbours_.size()!=Positions_.size())BoundaryNeighbours_.resize(Positions_.size());
    for(std::size_t i=0;i<Positions_.size();++i){auto& list=BoundaryNeighbours_[i];list.clear();for(std::size_t b=0;b<BoundaryPositions_.size();++b)if(Dot(Positions_[i]-BoundaryPositions_[b],Positions_[i]-BoundaryPositions_[b])<h2&&list.size()<96)list.push_back(static_cast<std::uint16_t>(b));}
}

void PbfFluid::ComputeDensity(bool computeLambda) {
    const float h=SmoothingRadius,h2=h*h;
    const float poly=315.0f/(64.0f*Pi*std::pow(h,9.0f));
    const float gradFactor=-6.0f*poly/RestDensity;
    float total=0.0f,peak=0.0f;
    for(std::size_t i=0;i<Positions_.size();++i) {
        float density=0.0f,grad2=0.0f; Vec3 grad{};
        for(std::uint16_t ji:Neighbours_[i]) {
            const Vec3 d=Positions_[i]-Positions_[ji]; const float r2=Dot(d,d); if(r2>=h2) continue;
            const float q=h2-r2; density+=poly*q*q*q;
            if(computeLambda) { const Vec3 g=d*(gradFactor*q*q); grad+=g; grad2+=Dot(g,g); }
        }
        Vec3 boundaryGradient{};
        for(std::uint16_t bi:BoundaryNeighbours_[i]) {
            const Vec3 d=Positions_[i]-BoundaryPositions_[bi];const float r2=Dot(d,d);if(r2>=h2)continue;
            const float q=h2-r2,mass=BoundaryPseudoMasses_[bi];density+=mass*poly*q*q*q;
            if(computeLambda)boundaryGradient+=d*(mass*gradFactor*q*q);
        }
        if(computeLambda){grad+=boundaryGradient;BoundaryGradients_[i]=boundaryGradient;}
        Density_[i]=density;
        const float compression=std::max(0.0f,density/RestDensity-1.0f);
        total+=compression;peak=std::max(peak,compression);
        if(computeLambda) Lambda_[i]=-0.65f*compression/(grad2+Dot(grad,grad)+1e-5f);
    }
    Diagnostics_.MeanCompression=total/std::max<std::size_t>(1,Positions_.size()); Diagnostics_.PeakCompression=peak;
}

void PbfFluid::SolvePressure() {
    BuildNeighbours(); ComputeDensity(false); Diagnostics_.PressureIterations=0;
    const float h=SmoothingRadius,h2=h*h,poly=315.0f/(64.0f*Pi*std::pow(h,9.0f));
    const float g0=-6.0f*poly/RestDensity;
    for(std::uint32_t iteration=0;iteration<6;++iteration) {
        if(iteration>0 && iteration%2==0) BuildNeighbours();
        ComputeDensity(true);
        if(iteration>=2 && Diagnostics_.PeakCompression<=0.03f) break;
        for(std::size_t i=0;i<Positions_.size();++i) {
            Vec3 correction=BoundaryGradients_[i]*Lambda_[i];
            for(std::uint16_t ji:Neighbours_[i]) if(ji!=i) {
                const Vec3 d=Positions_[i]-Positions_[ji];const float r2=Dot(d,d);if(r2>=h2)continue;
                correction+=d*((Lambda_[i]+Lambda_[ji])*g0*std::pow(h2-r2,2.0f));
            }
            const float length=Length(correction); if(length>0.035f) correction=correction*(0.035f/length);
            Corrections_[i]=correction;
        }
        for(std::size_t i=0;i<Positions_.size();++i){Positions_[i]+=Corrections_[i];Collide(Positions_[i]);}
        ++Diagnostics_.PressureIterations;
    }
    BuildNeighbours();ComputeDensity(false);
}

void PbfFluid::ApplySurfaceTension(float dt) {
    const FluidMaterial& material=ActiveMaterial();const float tension=material.SurfaceTension*8.0f,wetting=material.Wetting,h=SmoothingRadius,h2=h*h;
    const float polyGradient=-6.0f*315.0f/(64.0f*Pi*std::pow(h,9.0f));
    std::vector<Vec3> normals(Positions_.size());std::fill(Corrections_.begin(),Corrections_.end(),Vec3{});
    for(std::size_t i=0;i<Positions_.size();++i){
        for(std::uint16_t ji:Neighbours_[i]){const Vec3 d=Positions_[i]-Positions_[ji];const float r2=Dot(d,d);if(r2>=h2)continue;normals[i]+=d*(h*polyGradient*std::pow(h2-r2,2.0f)/std::max(Density_[ji],.2f*RestDensity));}
        for(std::uint16_t bi:BoundaryNeighbours_[i]){const Vec3 d=Positions_[i]-BoundaryPositions_[bi];const float r2=Dot(d,d);if(r2>=h2)continue;normals[i]+=d*(h*BoundaryPseudoMasses_[bi]*polyGradient*std::pow(h2-r2,2.0f)/RestDensity);}
    }
    for(std::size_t i=0;i<Positions_.size();++i){
        for(std::uint16_t ji:Neighbours_[i])if(ji>i){const Vec3 d=Positions_[i]-Positions_[ji];const float r=Length(d);if(r<1e-8f||r>=h)continue;const float correction=std::min(4.0f,2*RestDensity/std::max(.2f*RestDensity,Density_[i]+Density_[ji]));const float cohesion=CohesionKernel(r,h)/r;const Vec3 a=(d*cohesion+normals[i]-normals[ji])*(-tension*correction);Corrections_[i]+=a;Corrections_[ji]-=a;}
        for(std::uint16_t bi:BoundaryNeighbours_[i]){const Vec3 d=Positions_[i]-BoundaryPositions_[bi];const float r=Length(d);if(r<=h*.5f||r>=h)continue;const float kernel=.007f/std::pow(h,3.25f)*std::pow(std::max(0.0f,-4*r*r/h+6*r-2*h),.25f);Corrections_[i]+=d*(-wetting*BoundaryPseudoMasses_[bi]*kernel/r);}
    }
    float maximum=0;for(const Vec3& a:Corrections_)maximum=std::max(maximum,Length(a));const float scale=std::min(1.0f,35.0f/std::max(maximum,1e-12f));for(std::size_t i=0;i<Velocities_.size();++i)Velocities_[i]+=Corrections_[i]*(dt*scale);
}

void PbfFluid::ApplyViscosity(float dt) {
    const FluidMaterial& material=ActiveMaterial();const std::size_t count=Positions_.size();float shearTotal=0,viscosityTotal=0;
    for(std::size_t i=0;i<count;++i){float weight=0,shear=0;for(std::uint16_t j:Neighbours_[i])if(j!=i){const Vec3 d=Positions_[j]-Positions_[i];const float r=Length(d);if(r>=SmoothingRadius||r<1e-6f)continue;const float w=std::pow(1-Dot(d,d)/(SmoothingRadius*SmoothingRadius),3.0f);shear+=Length(Velocities_[j]-Velocities_[i])/r*w;weight+=w;}shear=weight>0?std::min(250.0f,shear/weight):0;const float n=1-.85f*material.ShearThinning;const float temperature=std::clamp(std::exp(1800.0f*(1/(material.TemperatureC+273.15f)-1/(material.TemperatureC+273.15f))),std::exp(-3.0f),std::exp(3.0f));const float nu=.5f*material.Viscosity*material.Viscosity*(.12f+.88f*std::pow(1+std::pow(.6f*shear,2.0f),(n-1)*.5f))*temperature;ApparentViscosity_[i]=std::clamp(nu,0.0f,2.0f);shearTotal+=shear;viscosityTotal+=ApparentViscosity_[i];}
    struct Edge{std::uint16_t a,b;Vec3 n;float w;};std::vector<Edge> edges;edges.reserve(count*32);std::vector<std::array<float,9>> diagonal(count),boundaryBlocks(count);for(auto& d:diagonal)d={1,0,0,0,1,0,0,0,1};for(auto& b:boundaryBlocks)b.fill(0);
    auto addBlock=[](std::array<float,9>& m,Vec3 n,float w){m[0]+=w*n.x*n.x;m[1]+=w*n.x*n.y;m[2]+=w*n.x*n.z;m[3]+=w*n.y*n.x;m[4]+=w*n.y*n.y;m[5]+=w*n.y*n.z;m[6]+=w*n.z*n.x;m[7]+=w*n.z*n.y;m[8]+=w*n.z*n.z;};
    const float h=SmoothingRadius,h2=h*h,spiky=-45.0f/(Pi*std::pow(h,6.0f));
    for(std::size_t i=0;i<count;++i){for(std::uint16_t j:Neighbours_[i])if(j>i){const Vec3 d=Positions_[j]-Positions_[i];const float r2=Dot(d,d),r=std::sqrt(r2);if(r2>=h2||r<1e-6f)continue;const float sum=ApparentViscosity_[i]+ApparentViscosity_[j],pair=sum>0?2*ApparentViscosity_[i]*ApparentViscosity_[j]/sum:0;const float gradient=-spiky*std::pow(h-r,2.0f)/(RestDensity*r);const float w=dt*10*pair*gradient*r2/(r2+.01f*h2);if(w<=0)continue;const Vec3 n=d/r;edges.push_back({static_cast<std::uint16_t>(i),j,n,w});addBlock(diagonal[i],n,w);addBlock(diagonal[j],n,w);}
        for(std::uint16_t b:BoundaryNeighbours_[i]){const Vec3 d=Positions_[i]-BoundaryPositions_[b];const float r2=Dot(d,d),r=std::sqrt(r2);if(r2>=h2||r<1e-6f)continue;const float gradient=-spiky*std::pow(h-r,2.0f)/(RestDensity*r);const float w=dt*10*ApparentViscosity_[i]*BoundaryPseudoMasses_[b]*gradient*r2/(r2+.01f*h2);addBlock(diagonal[i],d/r,w);addBlock(boundaryBlocks[i],d/r,w);}}
    auto invert=[](const std::array<float,9>& a){std::array<float,9> r{};const float det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);if(std::abs(det)<1e-12f){r={1,0,0,0,1,0,0,0,1};return r;}const float q=1/det;r={q*(a[4]*a[8]-a[5]*a[7]),q*(a[2]*a[7]-a[1]*a[8]),q*(a[1]*a[5]-a[2]*a[4]),q*(a[5]*a[6]-a[3]*a[8]),q*(a[0]*a[8]-a[2]*a[6]),q*(a[2]*a[3]-a[0]*a[5]),q*(a[3]*a[7]-a[4]*a[6]),q*(a[1]*a[6]-a[0]*a[7]),q*(a[0]*a[4]-a[1]*a[3])};return r;};
    std::vector<std::array<float,9>> inverse(count);for(std::size_t i=0;i<count;++i)inverse[i]=invert(diagonal[i]);
    auto multiply=[&](const std::vector<Vec3>& x,std::vector<Vec3>& out){out=x;for(const Edge& e:edges){const float f=e.w*Dot(x[e.a]-x[e.b],e.n);out[e.a]+=e.n*f;out[e.b]-=e.n*f;}for(std::size_t i=0;i<count;++i){const auto& b=boundaryBlocks[i];out[i]+=Vec3{b[0]*x[i].x+b[1]*x[i].y+b[2]*x[i].z,b[3]*x[i].x+b[4]*x[i].y+b[5]*x[i].z,b[6]*x[i].x+b[7]*x[i].y+b[8]*x[i].z};}};
    auto precondition=[&](const std::vector<Vec3>& r,std::vector<Vec3>& z){for(std::size_t i=0;i<count;++i){const auto& m=inverse[i];z[i]={m[0]*r[i].x+m[1]*r[i].y+m[2]*r[i].z,m[3]*r[i].x+m[4]*r[i].y+m[5]*r[i].z,m[6]*r[i].x+m[7]*r[i].y+m[8]*r[i].z};}};
    auto dotAll=[](const std::vector<Vec3>& a,const std::vector<Vec3>& b){double s=0;for(std::size_t i=0;i<a.size();++i)s+=Dot(a[i],b[i]);return s;};
    std::vector<Vec3> x=Velocities_,r(count),z(count),p(count),ap(count);multiply(x,ap);for(std::size_t i=0;i<count;++i)r[i]=Velocities_[i]-ap[i];precondition(r,z);p=z;double rz=dotAll(r,z),rhs=dotAll(Velocities_,Velocities_);
    Diagnostics_.ViscosityIterations=0;
    for(int iteration=0;iteration<18&&dotAll(r,r)>1e-10*std::max(rhs,1e-16);++iteration){multiply(p,ap);const double pap=dotAll(p,ap);if(pap<=1e-30)break;const float alpha=static_cast<float>(rz/pap);for(std::size_t i=0;i<count;++i){x[i]+=p[i]*alpha;r[i]-=ap[i]*alpha;}precondition(r,z);const double next=dotAll(r,z);const float beta=static_cast<float>(next/std::max(rz,1e-30));for(std::size_t i=0;i<count;++i)p[i]=z[i]+p[i]*beta;rz=next;Diagnostics_.ViscosityIterations=static_cast<std::uint32_t>(iteration+1);}
    Diagnostics_.ViscosityRelativeResidual=static_cast<float>(std::sqrt(dotAll(r,r)/std::max(rhs,1e-16)));
    Velocities_=std::move(x);Diagnostics_.MeanShear=shearTotal/std::max<std::size_t>(1,count);Diagnostics_.MeanApparentViscosity=viscosityTotal/std::max<std::size_t>(1,count);
}

void PbfFluid::Collide(Vec3& p) {
    const Vec3 lo=BoundsMin(),hi=BoundsMax();
    const Vec3 before=p;
    p.x=std::clamp(p.x,lo.x,hi.x);p.y=std::clamp(p.y,lo.y,hi.y);p.z=std::clamp(p.z,lo.z,hi.z);
    if(p.x!=before.x||p.y!=before.y||p.z!=before.z)++Diagnostics_.WallContacts;
    if(ObstacleEnabled_) {
        const Vec3 centre=ObstacleCentre();Vec3 d=p-centre;const float distance=Length(d),radius=ObstacleRadius();
        if(distance<radius) {p=centre+(distance<1e-8f?Vec3{0,1,0}:d/distance)*(radius+1e-5f);++Diagnostics_.SphereContacts;}
    }
}

void PbfFluid::Step(float dt) {
    if(!std::isfinite(dt)||dt<0) throw std::range_error("invalid fluid timestep");
    if(dt==0) return;
    dt=std::min(dt,1.0f/45.0f);Time_+=dt;Diagnostics_.SphereContacts=0;Diagnostics_.WallContacts=0;
    BuildNeighbours();ComputeDensity(false);ApplySurfaceTension(dt);Previous_=Positions_;
    for(std::size_t i=0;i<Positions_.size();++i){Velocities_[i].y-=Gravity_*dt;Positions_[i]+=Velocities_[i]*dt;Collide(Positions_[i]);}
    SolvePressure();
    for(std::size_t i=0;i<Positions_.size();++i)Velocities_[i]=(Positions_[i]-Previous_[i])/dt;
    ApplyViscosity(dt);
}

void PbfFluid::Pour(float dt,float rate) {
    if(Experiment_!=Experiment::Basin) return;
    Emission_+=dt*rate*120.0f;
    while(Emission_>=1.0f&&Positions_.size()<MaxParticles){Emission_-=1.0f;const float a=Time_*23.0f+Positions_.size()*2.39996f;const float r=0.12f*std::sqrt((Positions_.size()%11)/10.0f);Add({-0.65f+std::cos(a)*r,2.55f,-0.15f+std::sin(a)*r},{0.08f,-4.1f,0});}
    Emission_=std::min(Emission_,1.0f);
}

void PbfFluid::Stir(float strength,float cx,float cz) {
    for(std::size_t i=0;i<Positions_.size();++i){const float x=Positions_[i].x-cx,z=Positions_[i].z-cz,f=std::exp(-(x*x+z*z)*0.6f)*strength;Velocities_[i].x-=z*f;Velocities_[i].z+=x*f;Velocities_[i].y+=f*0.38f;}
}

} // namespace Frontier::ProjectFluid
