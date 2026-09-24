#pragma once
#include "VolumetricMedia.h"
#include <algorithm>
namespace Frontier {
// Diagnostic density projections, not radiance, opacity, sky-cover measurements or GPU bakes.
// Global: fixed 6 km-wide window at the world origin. Local: aspect-correct framing of actual box bounds, world-anchored noise.
inline void CloudDensityPreview(unsigned char* Top,unsigned char* Side,unsigned W,unsigned H,
                                const CloudLayerSettings& Global,const LocalVolumeSettings& Local,bool IsLocal,float Aspect=2) noexcept {
 WindSettings Wind{};float Base=0,Upper=0;bool Extent=VolumetricMedia::SlabExtent(Global,Base,Upper);
 float Z0=IsLocal?Local.Centre[2]-Local.HalfSize[2]:0;
 float Z1=IsLocal?Local.Centre[2]+Local.HalfSize[2]:Global.CeilingMetres;
 auto Density=[&](float X,float Y,float Z){float P[]={X,Y,Z};return IsLocal?VolumetricMedia::LocalDensity(Local,Wind,P,0):Global.Enabled&&Extent?VolumetricMedia::CloudDensity(Global,Wind,P,0):0.f;};
 auto Store=[](unsigned char* P,float D){float V=1-std::exp(-std::max(0.f,D)*3);P[0]=static_cast<unsigned char>(26+V*180);P[1]=static_cast<unsigned char>(35+V*181);P[2]=static_cast<unsigned char>(47+V*182);P[3]=255;};
 for(unsigned Y=0;Y<H;++Y)for(unsigned X=0;X<W;++X){float U=(X+.5f)/W,V=(Y+.5f)/H;
  float HalfX=std::max(Local.HalfSize[0],Local.HalfSize[1]*Aspect);
  float PX=IsLocal?Local.Centre[0]+(U*2-1)*HalfX:(U-.5f)*6000;
  float PY=IsLocal?Local.Centre[1]+(1-V*2)*HalfX/Aspect:(.5f-V)*6000/Aspect;
  float Lo=IsLocal?Z0:Base,Hi=IsLocal?Z1:Upper,Sum=0;
  for(unsigned K=0;K<8;++K)Sum+=Density(PX,PY,Lo+(K+.5f)/8*(Hi-Lo));
  Store(Top+(Y*W+X)*4,Sum/8);
  Store(Side+(Y*W+X)*4,Density(IsLocal?Local.Centre[0]+(U*2-1)*Local.HalfSize[0]:PX,IsLocal?Local.Centre[1]:0,Z1-V*(Z1-Z0)));
 }
}
}
