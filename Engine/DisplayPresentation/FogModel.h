#pragma once
#include "AtmosphereModel.h"
#include "VolumetricMedia.h"
#include <algorithm>
namespace Frontier {
// Analytic artistic fog layers. Height uses the legacy reference's exp(-OD^2);
// aerial uses planar exponential air profiles and the current atmosphere coefficients.
// Aerial is an extra surface-distance effect, not a second sky-atmosphere integration.
struct FogModel {
 static float Column(float Z0,float Z1,float Distance,float H) noexcept {
  Z0=std::max(0.f,Z0);Z1=std::max(0.f,Z1);H=std::max(1.f,H);
  FogSettings F;F.HeightEnabled=true;F.HeightDensity=1;F.FalloffHeight=H;
  return std::max(0.f,VolumetricMedia::HeightFogOpticalDepth(F,Z0,Z1,std::max(0.f,Distance)));
 }
 static void Transmission(const FogSettings& F,const AtmosphereMedium& M,float Z0,float Z1,float Distance,float Out[3]) noexcept {
  float D=std::clamp(Distance,0.f,100000.f);
  float OD=F.HeightEnabled?std::max(0.f,F.HeightDensity)*Column(Z0,Z1,D,F.FalloffHeight):0;
  float Height=std::exp(-std::min(OD*OD,80.f));
  float Start=std::clamp(F.AerialStart,0.f,D),Length=F.AerialEnabled?D-Start:0;
  float ZA=D>0?Z0+(Z1-Z0)*Start/D:Z0;
  float R=Column(ZA,Z1,Length,M.RayleighScaleHeight),A=Column(ZA,Z1,Length,M.MieScaleHeight);
  float Mix=std::clamp(F.AerialMie,0.f,1.f),Gain=std::max(0.f,F.AerialDensity);
  for(int C=0;C<3;++C){float Tau=Gain*((1-Mix)*std::max(0.f,M.RayleighScattering[C]*M.RayleighStrength)*R+Mix*std::max(0.f,M.MieScattering*M.MieStrength)*A);Out[C]=Height*std::exp(-std::min(Tau,80.f));}
 }
 static void Apply(const FogSettings& F,const AtmosphereMedium& M,const AtmosphereLight& Light,
                   float Z0,float Z1,float Distance,const float Direction[3],const float Ambient[3],float RGB[3]) noexcept {
  if(!F.HeightEnabled&&!F.AerialEnabled)return;
  // Compose each analytic layer separately so height tint never colours aerial extinction.
  FogSettings H=F;H.AerialEnabled=false;FogSettings A=F;A.HeightEnabled=false;float TH[3],TA[3];
  Transmission(H,M,Z0,Z1,Distance,TH);Transmission(A,M,Z0,Z1,Distance,TA);
  float Dot=std::clamp(Direction[0]*Light.Direction[0]+Direction[1]*Light.Direction[1]+Direction[2]*Light.Direction[2],-1.f,1.f);
  float Phase=VolumetricMedia::HenyeyGreenstein(Dot,.55f),SunGate=std::clamp(Light.Direction[2]+.1f,0.f,1.f);
  for(int C=0;C<3;++C){float HeightSource=F.HeightColour[C]*std::max(0.f,Ambient[C])+.02f*F.SunScatter*Phase*SunGate*Light.Colour[C]*Light.Intensity;
   RGB[C]=RGB[C]*TH[C]+HeightSource*(1-TH[C]);RGB[C]=RGB[C]*TA[C]+std::max(0.f,Ambient[C])*(1-TA[C]);}
 }
 // Static +Y probe from the near box face, through its centre. This invokes the real local-fog march.
 static VolumetricSample LocalProbe(const LocalVolumeSettings& V,float Distance) noexcept {
  float O[]={V.Centre[0],V.Centre[1]-V.HalfSize[1],V.Centre[2]},D[]={0,1,0},Sun[]={0,.70710678f,.70710678f},L[]={1,1,1},A[]={.18f,.18f,.18f};
  return VolumetricMedia::March({}, {}, V,WindSettings{},VolumetricBudget{},O,D,std::clamp(Distance,0.f,V.HalfSize[1]*2),Sun,L,A,0);
 }
};
}
