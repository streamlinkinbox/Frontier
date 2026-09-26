#pragma once
#include "VolumetricMedia.h"
#include <algorithm>
#include <cstddef>

namespace Frontier {
// Appended to binding 24; 21 std140 vec4 rows. No new descriptor binding.
// 0..3 cloud, 4..8 local cloud, 9..13 local fog, 14..16 analytic fog,
// Row 8 yzw also holds the per-frame sky ambient probe (set by the sequence).
// 17 budgets/active, 18..20 resolved layer/local-cloud/local-fog wind speed/bearing/shear/veer. All coordinates Z-up.
struct WeatherConstantRecord { float Rows[21][4]{}; };
static_assert(sizeof(WeatherConstantRecord)==336);
inline WeatherConstantRecord PackWeatherConstants(const CloudLayerSettings& C,
    const LocalVolumeSettings& L,const LocalVolumeSettings& F,const FogSettings& Fog,
    const WindSettings& Wind,const VolumetricBudget& Budget,float Time,const WindSettings* MediaWinds=nullptr) noexcept {
    WeatherConstantRecord R;auto& W=R.Rows;
    if(C.Enabled){
        W[0][0]=C.Base;W[0][1]=C.Thickness;W[0][2]=C.Coverage;W[0][3]=C.Density;
        W[1][0]=C.Scale;W[1][1]=C.Anvil;W[1][2]=C.CeilingMetres;W[1][3]=float(C.Type);
        std::copy_n(C.Albedo,3,W[2]);W[2][3]=C.Anisotropy;
        W[3][1]=C.FollowWind?1.f:0.f;W[3][3]=1;
    }
    auto Local=[&](const LocalVolumeSettings& V,int B){if(!V.Enabled)return;
        std::copy_n(V.Centre,3,W[B]);W[B][3]=1;
        std::copy_n(V.HalfSize,3,W[B+1]);W[B+1][3]=V.Scale;
        W[B+2][0]=V.Density;W[B+2][1]=V.Coverage;W[B+2][2]=V.Anisotropy;
        std::copy_n(V.Albedo,3,W[B+3]);W[B+4][0]=V.FollowWind?1.f:0.f;
    };Local(L,4);Local(F,9);
    if(Fog.HeightEnabled){W[14][0]=Fog.HeightDensity;W[14][1]=Fog.FalloffHeight;W[14][2]=Fog.SunScatter;W[14][3]=1;std::copy_n(Fog.HeightColour,3,W[15]);}
    if(Fog.AerialEnabled){W[15][3]=1;W[16][0]=Fog.AerialDensity;W[16][1]=Fog.AerialStart;W[16][2]=Fog.AerialMie;}
    const bool Volumes=C.Enabled||L.Enabled||F.Enabled;
    if(Volumes){W[17][0]=float(std::clamp(Budget.CloudSteps,1u,128u));W[17][1]=float(std::clamp(Budget.LocalSteps,1u,128u));W[17][2]=float(std::clamp(Budget.LightTaps,1u,16u));}
    W[17][3]=(Volumes||Fog.HeightEnabled||Fog.AerialEnabled)?1.f:0.f;
    const bool Follow[3]={C.Enabled&&C.FollowWind,L.Enabled&&L.FollowWind,F.Enabled&&F.FollowWind};
    bool Moving=false;
    for(int I=0;I<3;++I){const auto& Source=MediaWinds?MediaWinds[I]:Wind;
        if(Follow[I]&&Source.Speed!=0){Moving=true;W[18+I][0]=Source.Speed;W[18+I][1]=Source.Bearing;W[18+I][2]=Source.Shear;W[18+I][3]=Source.Veer;}}
    if(Moving)W[3][0]=Time;
    return R;
}
}
