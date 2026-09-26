#include "AnisotropicSurfaceMesh.h"
#include "PondWave.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace Frontier::ProjectFluid;
void Check(bool V,const char* Name){if(!V)throw std::runtime_error(Name);}
int main(){
 try {
  AnisotropicSurfaceMesh Surface;
  SurfaceKernel K{{0,.8f,0},{.32f,0,0},{0,.20f,0},{0,0,.10f},1};
  Surface.Update({K});Check(!Surface.Mesh().Indices.empty(),"initial mesh");
  Surface.Update({K});Check(Surface.DirtyBrickCount()==0,"unchanged cache");
  K.AxisA={0,.32f,0};K.AxisB={.20f,0,0};Surface.Update({K});Check(Surface.DirtyBrickCount()>0,"rotation must invalidate cache");
  K.VolumeWeight=1.5f;Surface.Update({K});Check(Surface.DirtyBrickCount()>0,"density weight must invalidate cache");
  for(int I=0;I<10;++I){K.Centre.x+=.0001f;Surface.Update({K});Check(Surface.DirtyBrickCount()>0,"subthreshold motion must not get lost");}
  Surface.Update({});Check(Surface.Mesh().Indices.empty(),"removal clears mesh");
  const float Nan=std::numeric_limits<float>::quiet_NaN();
  for(int I=0;I<4;++I){bool Rejected=false;try{PondWave P(I==0?2:96,I==1?Nan:8,I==2?.0001f:I==3?1e9f:12);}catch(const std::range_error&){Rejected=true;}Check(Rejected,"invalid/oversized grid rejected");}
  PondWave P;for(float Radius:{0.f,-1.f,Nan}){bool Rejected=false;try{P.Disturb(0,0,1,Radius);}catch(const std::range_error&){Rejected=true;}Check(Rejected,"invalid disturbance rejected");}
  std::cout<<"PASS cache rotation/weight/small-motion/removal and pond input guards\n";
 }catch(const std::exception& E){std::cerr<<E.what()<<'\n';return 1;}
}
