#define private public
#include "VolumetricMedia.h"
#undef private
#include <cmath>
#include <cstdio>
#include <vector>
using namespace Frontier;
// The march the shader runs, reduced to transmittance: fixed step, optional sub-step offset.
static double March(const CloudLayerSettings&C,const WindSettings&W,const float O[3],const float D[3],
                    float lo,float hi,int steps,float jitter){
  const double ds=(hi-lo)/steps;double optical=0;
  for(int i=0;i<steps;++i){
    const double t=lo+(i+jitter)*ds;
    const float P[3]={float(O[0]+D[0]*t),float(O[1]+D[1]*t),float(O[2]+D[2]*t)};
    optical+=VolumetricMedia::CloudDensity(C,W,P,0.f)*ds*0.01;
  }
  return std::exp(-optical);
}
int main(){
  CloudLayerSettings C;C.Enabled=true;C.Base=100;C.Thickness=1100;C.Coverage=.6f;C.Density=2.4f;C.Scale=1.93f;
  WindSettings W;W.Speed=7;W.Bearing=247;
  const float O[3]={0,0,15};
  // 600 rays fanned across the sky, as a scanline of pixels would be
  int steps=48;                      // what the cloud budget actually affords over this interval
  double e_fixed=0,e_jit=0,e_acc=0;std::vector<double> rowF,rowJ,rowA;
  for(int px=0;px<600;++px){
    const double a=0.35+0.35*px/600.0;                 // elevation sweep
    const float D[3]={float(std::cos(a)),0.f,float(std::sin(a))};
    float lo,hi;{float base,top;VolumetricMedia::SlabExtent(C,base,top);
      lo=(base-O[2])/D[2];hi=(top-O[2])/D[2];}
    const double ref=March(C,W,O,D,lo,hi,4096,0.5f);
    const double fixedT=March(C,W,O,D,lo,hi,steps,0.5f);
    // one jittered sample (what a single frame shows)
    const unsigned h=(unsigned(px)*2654435761u)^0x9e3779b9u;const float j=float((h>>8)&0xffff)/65536.f;
    const double jitT=March(C,W,O,D,lo,hi,steps,j);
    // 64 jittered samples averaged (what the accumulator shows after a second of holding still)
    double acc=0;for(int f=0;f<64;++f){const unsigned g=(unsigned(px)*2654435761u)^(unsigned(f)*83492791u);
      acc+=March(C,W,O,D,lo,hi,steps,float((g>>8)&0xffff)/65536.f);}
    acc/=64;
    e_fixed+=std::abs(fixedT-ref);e_jit+=std::abs(jitT-ref);e_acc+=std::abs(acc-ref);
    rowF.push_back(fixedT-ref);rowJ.push_back(jitT-ref);rowA.push_back(acc-ref);
  }
  // banding = how much of the error is a SMOOTH RIPPLE across neighbouring pixels (structure the eye sees)
  const auto Ripple=[](const std::vector<double>&E){double s=0;for(size_t i=1;i<E.size();++i)s+=std::abs(E[i]-E[i-1]);
    double m=0;for(double v:E)m+=std::abs(v);return 1.0-(s/(E.size()-1))/(2*m/E.size()+1e-12);};
  std::printf("march error vs a 4096-step reference, %d steps\n",steps);
  std::printf("  fixed midpoint : mean |dT| %.5f   structured %.3f\n",e_fixed/600,Ripple(rowF));
  std::printf("  jittered 1 frame: mean |dT| %.5f   structured %.3f\n",e_jit/600,Ripple(rowJ));
  std::printf("  jittered 64 frames accumulated: mean |dT| %.5f   structured %.3f\n",e_acc/600,Ripple(rowA));
}
