#define private public
#include "VolumetricMedia.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
using namespace Frontier;
// ── the field as it was before the change ────────────────────────────────────────────────────────
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
static float OldDensity(const CloudLayerSettings&C,const WindSettings&W,const float P[3],float T){
  float Base=0,Top=0; if(!VolumetricMedia::SlabExtent(C,Base,Top))return 0;
  if(P[2]<Base||P[2]>Top)return 0;
  const float N=(P[2]-Base)/(Top-Base),Pr=VolumetricMedia::HeightProfile(C.Type,N,C.Anvil);
  if(Pr<=0)return 0;
  float dx=0,dy=0;
  if(C.FollowWind){float D[3]={0,0,0};WindField::SampleStep(W,P[2],D);dx=D[0]*(T*0.8f);dy=D[1]*(T*0.8f);}
  const float Inv=1.f/std::fmax(C.Scale*900.f,1.f);
  const float sx=(P[0]+dx)*Inv,sy=(P[1]+dy)*Inv,sz=P[2]*Inv;
  float S=OldNoise(sx,sy,sz)*.5f+OldNoise(sx*2.02f+3.1f,sy*2.02f+1.7f,sz*2.02f+9.2f)*.25f
         +OldNoise(sx*4.10f+7.7f,sy*4.10f+2.2f,sz*4.10f+1.1f)*.125f;
  S=(S/.875f)*.5f+.5f;
  const float Th=1-C.Coverage,Raw=std::fmin(std::fmax((S-Th)/std::fmax(1-Th,1e-3f),0.f),1.f);
  return Raw*Raw*(3-2*Raw)*Pr*C.Density;}

int main(int argc,char**argv){
  const bool NewField = std::string(argv[1])=="new";
  const bool Jitter   = NewField;                       // the jitter ships with the new field
  const float Time    = argc>2?float(std::atof(argv[2])):0.f;
  const int Wd=480,Ht=270,Steps=64;const int Frames=argc>4?std::atoi(argv[4]):1;
  CloudLayerSettings C;C.Enabled=true;C.Base=100;C.Thickness=1100;C.Coverage=.6f;C.Density=2.4f;C.Scale=1.93f;C.Anvil=.5f;
  WindSettings W;W.Speed=7;W.Bearing=247;
  const float Sun[3]={0.5299f,-0.6314f,0.5664f};        // 34.5 deg elevation, as the readout says
  const float Eye[3]={0,0,15.8f};
  std::vector<unsigned char> Rgb(size_t(Wd)*Ht*3);
  std::vector<float> Accum(size_t(Wd)*Ht*3,0.f);
  const auto Density=[&](const float P[3]){return NewField?VolumetricMedia::CloudDensity(C,W,P,Time)
                                                         :OldDensity(C,W,P,Time);};
  for(int Frame=0;Frame<Frames;++Frame)
  for(int y=0;y<Ht;++y)for(int x=0;x<Wd;++x){
    const float u=(float(x)+.5f)/Wd*2-1, v=1-(float(y)+.5f)/Ht*2;
    const float tanHalf=0.5773f, aspect=float(Wd)/Ht;
    float D[3]={u*tanHalf*aspect, 1.f, (v*tanHalf)+0.38f};    // looking north, tilted up ~21 deg
    const float L=std::sqrt(D[0]*D[0]+D[1]*D[1]+D[2]*D[2]);D[0]/=L;D[1]/=L;D[2]/=L;
    float base,top;VolumetricMedia::SlabExtent(C,base,top);
    float lo=0,hi=0;bool hit=D[2]>1e-4f;
    if(hit){lo=(base-Eye[2])/D[2];hi=std::fmin((top-Eye[2])/D[2],40000.f);hit=hi>lo;}
    float trans=1,scat=0;
    if(hit){
      const float ds=(hi-lo)/Steps;
      unsigned h=(unsigned(x)*73856093u)^(unsigned(y)*19349663u)^(unsigned(Frame)*83492791u);
      h^=h>>13;h*=2246822519u;h^=h>>16;                 // the shader's own mix
      const float j=Jitter?float(h)*2.3283064365386963e-10f:0.5f;
      for(int i=0;i<Steps;++i){
        const float t=lo+(i+j)*ds;
        const float P[3]={Eye[0]+D[0]*t,Eye[1]+D[1]*t,Eye[2]+D[2]*t};
        const float d=Density(P);
        if(d<=1e-5f)continue;
        float od=0;                                   // 8 shadow taps toward the sun
        for(int k=1;k<=8;++k){const float s=ds*(k-1+j)*.5f;
          const float Q[3]={P[0]+Sun[0]*s,P[1]+Sun[1]*s,P[2]+Sun[2]*s};od+=Density(Q)*ds*.005f;}
        const float sh=std::exp(-od);
        const float st=std::exp(-d*ds*.01f);
        scat+=trans*(1-st)*(0.25f+0.75f*sh);
        trans*=st;
        if(trans<.004f)break;
      }
    }
    const float sky=0.38f+0.30f*std::fmax(D[2],0.f);
    float r=sky*0.72f*trans+scat, g=sky*0.85f*trans+scat, b=sky*1.00f*trans+scat;
    const size_t o=(size_t(y)*Wd+x)*3;
    Accum[o]+=r/Frames;Accum[o+1]+=g/Frames;Accum[o+2]+=b/Frames;
  }
  {const auto Tone=[](float c){c=c/(1+c);return (unsigned char)(std::pow(std::fmin(std::fmax(c,0.f),1.f),1/2.2f)*255+.5f);};
   for(size_t i=0;i<Accum.size();++i)Rgb[i]=Tone(Accum[i]);}
  std::FILE* f=std::fopen(argv[3],"wb");
  std::fprintf(f,"P6\n%d %d\n255\n",Wd,Ht);std::fwrite(Rgb.data(),1,Rgb.size(),f);std::fclose(f);
}
