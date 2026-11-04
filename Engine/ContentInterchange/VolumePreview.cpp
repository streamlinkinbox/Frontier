/*============================================================================================================================================
                                                       VOLUMEPREVIEW.CPP
============================================================================================================================================*/
// 🧩 Compose SDF slices and an isosurface in depth order, preserving anisotropic volume bounds.
#include "Engine/ContentInterchange/VolumePreview.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
namespace
{
uint8_t Byte(float Value) { return uint8_t(std::lround(std::clamp(Value,0.f,255.f))); }
float Clamp(float Value) { return std::clamp(Value,0.f,1.f); }
void Validate(const VolumeDistanceField &Field)
{
    if (!Field.Dimensions[0] || !Field.Dimensions[1] || !Field.Dimensions[2] ||
        Field.Distances.size() != size_t(Field.Dimensions[0])*Field.Dimensions[1]*Field.Dimensions[2] ||
        (!Field.Confidence.empty() && Field.Confidence.size()!=Field.Distances.size()))
        throw std::runtime_error("Volume preview received inconsistent dimensions or voxel buffers.");
    for (int A=0;A<3;++A)
        if (!std::isfinite(Field.Minimum[A]) || !std::isfinite(Field.Maximum[A]) ||
            !std::isfinite(Field.Step[A]) || Field.Step[A]<=0 || Field.Maximum[A]<=Field.Minimum[A])
            throw std::runtime_error("Volume preview received invalid volume bounds.");
}
}

std::array<uint8_t, 4> VolumePreviewColour(const VolumeDistanceField &Volume, size_t Index, VolumePreviewPalette Palette)
{
    const float D=Volume.Distances.at(Index);
    const float Confidence=Volume.Confidence.empty()?1.f:Volume.Confidence.at(Index);
    const float MaxAbs=std::max({std::abs(Volume.MinimumDistance),std::abs(Volume.MaximumDistance),1e-4f});
    const float Diagonal=std::sqrt(Volume.Step[0]*Volume.Step[0]+Volume.Step[1]*Volume.Step[1]+Volume.Step[2]*Volume.Step[2]);
    if (Palette==VolumePreviewPalette::Confidence)
    { const float C=Clamp(Confidence); return {Byte(235-C*155),Byte(88+C*135),Byte(72+C*92),255}; }
    if (Palette==VolumePreviewPalette::Inside)
    {
        if (std::abs(D)<=Diagonal*.68f) return {245,250,246,255};
        return D<0 ? std::array<uint8_t,4>{62,212,168,255} : std::array<uint8_t,4>{16,18,21,255};
    }
    if (Palette==VolumePreviewPalette::Normalized)
    {
        const float T=Volume.Signed ? Clamp(.5f+D/(2*MaxAbs)) : Clamp(D/MaxAbs);
        const auto G=Byte(T*255); return {G,G,G,255};
    }
    const float N=D/MaxAbs, Frequency=Palette==VolumePreviewPalette::Contours?18.f:11.f;
    const bool Contour=std::abs(N*Frequency-std::round(N*Frequency))<.075f;
    const float Glow=std::exp(-std::pow(std::abs(D)/(Diagonal*.72f),2.f));
    float R,G,B;
    if (D<0)
    {
        const float T=Clamp(-N*1.45f), Wave=.88f+.12f*std::cos(N*6.283185307f*Frequency);
        R=(76*(1-T)+20*T)*Wave; G=(212*(1-T)+96*T)*Wave; B=(176*(1-T)+104*T)*Wave;
        if (Palette==VolumePreviewPalette::Contours && Contour) { R+=54; G+=68; B+=60; }
    }
    else
    {
        const float T=Clamp(N*1.15f), Near=std::exp(-T*3.2f);
        const float Boost=Contour ? (Palette==VolumePreviewPalette::Contours?36.f:16.f)*(.35f+.65f*Near) : 0.f;
        R=13+32*Near+Boost; G=15+38*Near+Boost*1.06f; B=18+44*Near+Boost*1.12f;
    }
    return {Byte(R*(1-Glow)+245*Glow),Byte(G*(1-Glow)+249*Glow),Byte(B*(1-Glow)+246*Glow),255};
}

std::vector<uint8_t> RenderVolumePreview(const VolumeDistanceField &Volume, const VolumePreviewOptions &Options)
{
    Validate(Volume);
    if (Options.Axis>2 || Options.Resolution<8 || Options.Resolution>256 || !std::isfinite(Options.Slice) ||
        !std::isfinite(Options.Yaw) || !std::isfinite(Options.Pitch) || !std::isfinite(Options.Zoom) || Options.Zoom<=0)
        throw std::runtime_error("Invalid volume preview options.");
    const auto D=Volume.Dimensions; const uint32_t Side=Options.Resolution;
    std::vector<uint8_t> Pixels(size_t(Side)*Side*4);
    const float CY=std::cos(Options.Yaw), SY=std::sin(Options.Yaw), CP=std::cos(Options.Pitch), SP=std::sin(Options.Pitch);
    const std::array<float,3> Right{CY,0,-SY}, Up{SY*SP,CP,CY*SP}, Forward{SY*CP,-SP,CY*CP};
    const float Largest=std::max({Volume.Maximum[0]-Volume.Minimum[0],Volume.Maximum[1]-Volume.Minimum[1],Volume.Maximum[2]-Volume.Minimum[2]});
    const float Extent[3]{(Volume.Maximum[0]-Volume.Minimum[0])/Largest,(Volume.Maximum[1]-Volume.Minimum[1])/Largest,(Volume.Maximum[2]-Volume.Minimum[2])/Largest};
    const auto Voxel = [&](const float P[3])
    {
        uint32_t C[3]; for(int A=0;A<3;++A) C[A]=std::min(D[A]-1,uint32_t(std::clamp(P[A]/Extent[A]+.5f,0.f,.999999f)*D[A]));
        return (size_t(C[2])*D[1]+C[1])*D[0]+C[0];
    };
    const auto Within = [&](const float P[3])
    { for(int A=0;A<3;++A) if(std::abs(P[A])>Extent[A]*.5f+1e-6f) return false; return true; };
    const float Diagonal=std::sqrt(Volume.Step[0]*Volume.Step[0]+Volume.Step[1]*Volume.Step[1]+Volume.Step[2]*Volume.Step[2]);
    const float Threshold=std::min(.62f*Diagonal,.8f*std::max(std::abs(Volume.MinimumDistance),std::abs(Volume.MaximumDistance)));
    struct Sample { float Depth; std::array<uint8_t,4> Colour; float Alpha; };
    for(uint32_t Y=0;Y<Side;++Y) for(uint32_t X=0;X<Side;++X)
    {
        const float Zoom=std::clamp(Options.Zoom,.5f,5.f);
        const float U=((X+.5f)/Side-.5f)*1.75f/Zoom, V=(.5f-(Y+.5f)/Side)*1.75f/Zoom;
        float Base[3]; for(int A=0;A<3;++A) Base[A]=U*Right[A]+V*Up[A];
        std::array<Sample,9> Hits{}; size_t Count=0;
        const auto Plane=[&](uint32_t Axis, float Slice, float Alpha)
        {
            if(std::abs(Forward[Axis])<1e-6f) return;
            const float Position=(Clamp(Slice)-.5f)*Extent[Axis];
            const float Depth=(Position-Base[Axis])/Forward[Axis];
            float P[3]; for(int A=0;A<3;++A) P[A]=Base[A]+Depth*Forward[A];
            if(!Within(P) || Count>=Hits.size()) return;
            const auto Index=Voxel(P); const float Distance=Volume.Distances[Index];
            Hits[Count++]={Depth,VolumePreviewColour(Volume,Index,Options.Palette),
                std::abs(Distance)<=Diagonal*.85f ? std::min(.95f,Alpha+.25f) : Distance>0 ? Alpha*.45f : Alpha};
        };
        if(Options.Mode==VolumePreviewMode::IsoCut) Plane(Options.Axis,Options.Slice,.78f);
        else if(Options.Mode==VolumePreviewMode::Triplane)
            for(uint32_t A=0;A<3;++A) Plane(A,A==Options.Axis?Options.Slice:.5f,A==Options.Axis?.8f:.36f);
        else
            for(int I=0;I<7;++I)
            {
                const float S=float(I)/6.f; const bool Current=std::abs(S-Clamp(Options.Slice))<=1.f/12.f;
                Plane(Options.Axis,Current?Options.Slice:S,Current?.78f:.22f);
            }
        if(Options.Mode==VolumePreviewMode::IsoCut)
        {
            constexpr int Steps=180;
            for(int Step=0;Step<Steps;++Step)
            {
                const float T=1.5f-3.f*Step/(Steps-1); float P[3];
                for(int A=0;A<3;++A) P[A]=Base[A]+T*Forward[A];
                if(!Within(P) || std::abs(Volume.Distances[Voxel(P)])>Threshold) continue;
                float N[3];
                for(int A=0;A<3;++A)
                {
                    float Q[3]{P[0],P[1],P[2]}, E=Extent[A]/float(D[A]);
                    Q[A]+=E; float Plus=Volume.Distances[Voxel(Q)]; Q[A]-=2*E; float Minus=Volume.Distances[Voxel(Q)];
                    N[A]=(Plus-Minus)/Volume.Step[A];
                }
                const float L=std::sqrt(N[0]*N[0]+N[1]*N[1]+N[2]*N[2]);
                const float Light=L>1e-9f ? std::abs((N[0]*.3f+N[1]*.7f+N[2]*.65f)/L) : .5f;
                const float Shade=.3f+.7f*Clamp(Light);
                Hits[Count++]={T,{Byte(102*Shade),Byte(203*Shade),Byte(167*Shade),255},.8f}; break;
            }
        }
        std::sort(Hits.begin(),Hits.begin()+Count,[](const Sample &A,const Sample &B){return A.Depth<B.Depth;});
        float RGB[3]{23,26,28};
        for(size_t I=0;I<Count;++I) for(int C=0;C<3;++C) RGB[C]=RGB[C]*(1-Hits[I].Alpha)+Hits[I].Colour[C]*Hits[I].Alpha;
        const size_t P=(size_t(Y)*Side+X)*4; for(int C=0;C<3;++C) Pixels[P+C]=Byte(RGB[C]); Pixels[P+3]=255;
    }
    return Pixels;
}
}
