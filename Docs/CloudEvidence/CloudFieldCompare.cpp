#define private public
#include "VolumetricMedia.h"
#undef private
#include <cmath>
#include <cstdio>
#include <vector>
using namespace Frontier;
// ── the field as it was before this change, transcribed from git HEAD~ ───────────────────────────
static float OldHash(float X,float Y,float Z){float S=std::sin(X*127.1f+Y*311.7f+Z*74.7f)*43758.5453f;return S-std::floor(S);}
static float OldNoise(float X,float Y,float Z){
  const float Ix=std::floor(X),Iy=std::floor(Y),Iz=std::floor(Z);
  const float Fx=X-Ix,Fy=Y-Iy,Fz=Z-Iz;
  const float Ux=Fx*Fx*(3-2*Fx),Uy=Fy*Fy*(3-2*Fy),Uz=Fz*Fz*(3-2*Fz);
  const float N000=OldHash(Ix,Iy,Iz),N100=OldHash(Ix+1,Iy,Iz),N010=OldHash(Ix,Iy+1,Iz),N110=OldHash(Ix+1,Iy+1,Iz);
  const float N001=OldHash(Ix,Iy,Iz+1),N101=OldHash(Ix+1,Iy,Iz+1),N011=OldHash(Ix,Iy+1,Iz+1),N111=OldHash(Ix+1,Iy+1,Iz+1);
  const float X00=N000+(N100-N000)*Ux,X10=N010+(N110-N010)*Ux,X01=N001+(N101-N001)*Ux,X11=N011+(N111-N011)*Ux;
  const float Y0=X00+(X10-X00)*Uy,Y1=X01+(X11-X01)*Uy;
  return (Y0+(Y1-Y0)*Uz)*2-1;}
static float OldShape(float sx,float sy,float sz){
  float S=OldNoise(sx,sy,sz)*.5f+OldNoise(sx*2.02f+3.1f,sy*2.02f+1.7f,sz*2.02f+9.2f)*.25f
         +OldNoise(sx*4.10f+7.7f,sy*4.10f+2.2f,sz*4.10f+1.1f)*.125f;
  return (S/.875f)*.5f+.5f;}
static float OldDensity(const CloudLayerSettings&C,float x,float y,float z){
  float Base=0,Top=0; if(!VolumetricMedia::SlabExtent(C,Base,Top))return 0;
  if(z<Base||z>Top)return 0;
  const float N=(z-Base)/(Top-Base), P=VolumetricMedia::HeightProfile(C.Type,N,C.Anvil);
  if(P<=0)return 0;
  const float Inv=1.f/std::fmax(C.Scale*900.f,1.f);
  const float Shape=OldShape(x*Inv,y*Inv,z*Inv);
  const float Th=1-C.Coverage, Raw=std::fmin(std::fmax((Shape-Th)/std::fmax(1-Th,1e-3f),0.f),1.f);
  const float Body=Raw*Raw*(3-2*Raw);
  return Body*P*C.Density;}

template<class F> static void Metrics(const char* name,F Density,float cell){
  // ① anisotropy: mean |gradient| along the lattice axes vs along the diagonals
  double axis=0,diag=0;int n=0;
  const float h=cell*0.08f;
  for(int i=0;i<200;++i)for(int j=0;j<200;++j){
    const float x=float(i)*cell*0.07f,y=float(j)*cell*0.07f,z=600.f;
    axis+=std::abs(Density(x+h,y,z)-Density(x,y,z));
    axis+=std::abs(Density(x,y+h,z)-Density(x,y,z));
    diag+=std::abs(Density(x+h*0.7071f,y+h*0.7071f,z)-Density(x,y,z));
    diag+=std::abs(Density(x+h*0.7071f,y-h*0.7071f,z)-Density(x,y,z));
    ++n;}
  // ② lattice visibility: how much bigger the field's step is ACROSS a cell wall than inside a cell
  double wall=0,inside=0;int m=0;
  for(int i=0;i<400;++i)for(int j=0;j<400;++j){
    const float x=float(i)*cell,y=float(j)*cell,z=600.f;         // exactly on the lattice
    wall  +=std::abs(Density(x+0.02f*cell,y,z)-Density(x-0.02f*cell,y,z));
    inside+=std::abs(Density(x+0.52f*cell,y,z)-Density(x+0.48f*cell,y,z));
    ++m;}
  std::printf("%-8s anisotropy %.3f (1 = isotropic)   cell-wall step / mid-cell step %.3f (1 = invisible lattice)\n",
              name, (axis/(2*n))/((diag/(2*n))+1e-12), (wall/m)/((inside/m)+1e-12));
}
int main(){
  CloudLayerSettings C;C.Enabled=true;C.Base=100;C.Thickness=1100;C.Coverage=.6f;C.Density=2.4f;C.Scale=1.93f;
  WindSettings W;W.Speed=7;W.Bearing=247;
  const float cell=C.Scale*900.f;
  Metrics("before",[&](float x,float y,float z){return OldDensity(C,x,y,z);},cell);
  Metrics("after", [&](float x,float y,float z){const float P[3]={x,y,z};return VolumetricMedia::CloudDensity(C,W,P,0.f);},cell);
  // ③ the smallest feature each field can express, as the distance over which density crosses half its range
  const auto Finest=[&](auto Density){
    double best=1e9;
    for(int i=0;i<4000;++i){
      const float x=float(i)*3.f;double last=Density(x,17.f,600.f),run=0;
      for(int k=1;k<64;++k){const double v=Density(x+float(k)*3.f,17.f,600.f);
        if((v>0.5&&last<=0.5)||(v<=0.5&&last>0.5)){if(run>0)best=std::min(best,run);run=0;}else run+=3;last=v;}
    }
    return best;};
  std::printf("finest resolved feature: before %.0f m   after %.0f m\n",
    Finest([&](float x,float y,float z){return OldDensity(C,x,y,z);}),
    Finest([&](float x,float y,float z){const float P[3]={x,y,z};return VolumetricMedia::CloudDensity(C,W,P,0.f);}));
}
