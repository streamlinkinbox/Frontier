#pragma once
#include "SkyDomeSheet.h"
#include <algorithm>
#include <array>
namespace Frontier {
// Decode the actual retained RGBA16F bake, not a new atmosphere integration.
inline float SkyPreviewHalf(uint16_t H) noexcept {
    const int E=(H>>10)&31,M=H&1023;float V=E==0?std::ldexp(float(M),-24):E==31?0.0f:std::ldexp(1.0f+float(M)/1024,E-15);
    return H&0x8000?-V:V;
}
inline std::array<float,3> SkyPreviewSample(const uint16_t* Pixels,uint32_t Side,const float D[3],bool Transmission=false) noexcept {
    std::array<float,3> RGB{};if(!Pixels||Side==0)return RGB;
    float U,V;SkyDomeOctFromDirection(D,U,V);float X=U*Side-.5f,Y=V*Side-.5f;int IX=int(std::floor(X)),IY=int(std::floor(Y));
    float FX=X-IX,FY=Y-IY;
    for(int J=0;J<2;++J)for(int I=0;I<2;++I){
        uint32_t PX=uint32_t(std::clamp(IX+I,0,int(Side)-1)),PY=uint32_t(std::clamp(IY+J,0,int(Side)-1))+(Transmission?Side:0);
        float Weight=(I?FX:1-FX)*(J?FY:1-FY);for(int C=0;C<3;++C)RGB[C]+=SkyPreviewHalf(Pixels[(size_t(PY)*Side+PX)*4+C])*Weight;
    }
    return RGB;
}
inline std::array<float,3> SkyPanoramaDirection(float U,float V) noexcept {
    constexpr float Pi=3.14159265358979323846f;float A=U*2*Pi,E=(.5f-V)*Pi;
    return {std::cos(E)*std::cos(A),std::cos(E)*std::sin(A),std::sin(E)};
}
// An altitude profile opposite the BAKED Sun, repeated horizontally for a clean overview.
inline std::array<float,3> SkyAtmosphereProfileDirection(float V,const float Sun[3]) noexcept {
    constexpr float Pi=3.14159265358979323846f;
    float Az=(Sun[0]*Sun[0]+Sun[1]*Sun[1])>1e-12f?std::atan2(Sun[1],Sun[0]):0;
    return SkyPanoramaDirection(Az/(2*Pi)+.5f,V*.5f);
}
}
