// Shared C++ / Slang procedural layer kernel. Linear additive radiance, no display transform.
#ifndef FRONTIER_LENS_FLARE_KERNEL
#define FRONTIER_LENS_FLARE_KERNEL
#ifdef __cplusplus
#include <cmath>
#define LF_INLINE inline
#define LF_OUT float&
#define LF_MATH(Name) std::Name
#define LF_ATAN std::atan2
namespace Frontier {
#else
#define LF_INLINE
#define LF_OUT out float
#define LF_MATH(Name) Name
#define LF_ATAN atan
#endif
struct LFParams {
    float Gain, Spread, Anamorphic, Streaks;
    float Burst, Rotation, RayPairs, Ghosts;
    float GhostGain, GhostSpacing, GhostSides, HaloGain;
    float HaloRadius, HaloWidth, Chromatic, StreakGain;
};
LF_INLINE LFParams LFDefaults() {
    LFParams P;
    P.Gain=1; P.Spread=1; P.Anamorphic=1; P.Streaks=0;
    P.Burst=1; P.Rotation=0; P.RayPairs=8; P.Ghosts=6;
    P.GhostGain=0.35; P.GhostSpacing=1; P.GhostSides=6; P.HaloGain=0.5;
    P.HaloRadius=0.28; P.HaloWidth=0.045; P.Chromatic=0.6; P.StreakGain=1;
    return P;
}
LF_INLINE float LFClamp(float X,float A,float B) {return X<A?A:X>B?B:X;}
LF_INLINE float LFFract(float X) {return X-LF_MATH(floor)(X);}
LF_INLINE float LFSmooth(float A,float B,float X) {float T=LFClamp((X-A)/(B-A),0,1);return T*T*(3-2*T);}
LF_INLINE float LFHue(float H,float Shift) {float V=LFFract(H+Shift)*6;return LFClamp(LF_MATH(abs)(V-3)-1,0,1);}
LF_INLINE void LFRender(LFParams P,float U,float V,float SunU,float SunV,float Aspect,float Visibility,LF_OUT R,LF_OUT G,LF_OUT B) {
    R=0;G=0;B=0;
    if(Visibility<=0 || P.Gain<=0)return;
    float Edge=LFSmooth(-0.15,0.05,SunU)*(1-LFSmooth(0.95,1.15,SunU))*LFSmooth(-0.15,0.05,SunV)*(1-LFSmooth(0.95,1.15,SunV));
    float X=(U-0.5)*Aspect,Y=V-0.5,SX=(SunU-0.5)*Aspect,SY=SunV-0.5;
    float DX=X-SX,DY=Y-SY,Spread=LFClamp(P.Spread,0.25,2);
    float Distance=LF_MATH(sqrt)(DX*DX+DY*DY);
    float A=P.Rotation*0.0174532925199433;
    float RX=DX*LF_MATH(cos)(A)+DY*LF_MATH(sin)(A),RY=-DX*LF_MATH(sin)(A)+DY*LF_MATH(cos)(A);
    // Blue horizontal anamorphic and warm rotatable streak are independent layers.
    float Cool=LF_MATH(exp)(-LF_MATH(abs)(DY)*95/Spread)*LF_MATH(exp)(-LF_MATH(abs)(DX)*2.2/Spread)*0.55*P.Anamorphic*P.StreakGain;
    R+=Cool*(1-0.55*P.Chromatic);G+=Cool*(1-0.35*P.Chromatic);B+=Cool;
    float Warm=LF_MATH(exp)(-LF_MATH(abs)(RY)*160/Spread)*LF_MATH(exp)(-LF_MATH(abs)(RX)*3/Spread)*0.65*P.Streaks*P.StreakGain;
    R+=Warm;G+=Warm*0.8;B+=Warm*0.55;
    // Integer ray pairs give a continuous 2*pi periodic pattern and exactly 2N primary rays.
    float Angle=Distance>0.00000001?LF_ATAN(RY,RX):0,Pairs=LF_MATH(floor)(LFClamp(P.RayPairs,2,12)+0.5);
    float Burst=LF_MATH(pow)(LF_MATH(abs)(LF_MATH(sin)(Angle*Pairs)),32)*LF_MATH(exp)(-Distance*5/Spread)*0.6*P.Burst;
    R+=Burst;G+=Burst*0.95;B+=Burst*0.82;
    float Core=LF_MATH(exp)(-Distance*80/Spread)*0.8+LF_MATH(exp)(-Distance*9/Spread)*0.035;
    if(P.Anamorphic+P.Streaks+P.Burst>0){R+=Core;G+=Core*0.9;B+=Core*0.75;}
    int Count=int(LFClamp(P.Ghosts,0,24)+0.5);
    for(int I=0;I<Count;++I){
        float Index=float(I),K=1-(0.6+(Index+1)/float(Count)*(1.7+P.GhostSpacing));
        float GX=X-SX*K,GY=Y-SY*K;
        float D=LF_MATH(sqrt)(GX*GX+GY*GY);
        float Sides=P.GhostSides;
        if(Sides>=3){float Step=6.283185307179586/Sides;float T=D>0.00000001?LF_ATAN(GY,GX):0;float Fold=T-Step*LF_MATH(floor)((T+Step*0.5)/Step);D*=LF_MATH(cos)(Fold)/LF_MATH(cos)(3.141592653589793/Sides);}
        float Radius=(0.025+0.06*LFFract(Index*0.618+0.31))*Spread;
        float Shape=(LFSmooth(Radius,Radius*0.35,D)*0.6+LF_MATH(exp)(-LF_MATH(abs)(D-Radius)*350))*0.14*P.GhostGain;
        float Hue=LFFract(Index*0.23+0.5);
        R+=Shape*(1+(LFHue(Hue,0)-1)*P.Chromatic);G+=Shape*(1+(LFHue(Hue,0.6666667)-1)*P.Chromatic);B+=Shape*(1+(LFHue(Hue,0.3333333)-1)*P.Chromatic);
    }
    float HX=X-SX*0.25,HY=Y-SY*0.25;
    float HDistance=LF_MATH(sqrt)(HX*HX+HY*HY);
    float Offset=LF_MATH(abs)(HDistance-P.HaloRadius);
    float Halo=LFSmooth(LFClamp(P.HaloWidth,0.005,0.2),0,Offset)*0.09*P.HaloGain;
    float Hue=LFFract((HDistance>0.00000001?LF_ATAN(HY,HX):0)/6.283185307179586+Offset*8);
    R+=Halo*(1+(LFHue(Hue,0)-1)*P.Chromatic*0.8);G+=Halo*(1+(LFHue(Hue,0.6666667)-1)*P.Chromatic*0.8);B+=Halo*(1+(LFHue(Hue,0.3333333)-1)*P.Chromatic*0.8);
    float Gain=P.Gain*Visibility*Edge;R*=Gain;G*=Gain;B*=Gain;
}
#ifdef __cplusplus
} // namespace Frontier
#endif
#undef LF_INLINE
#undef LF_OUT
#undef LF_MATH
#undef LF_ATAN
#endif
