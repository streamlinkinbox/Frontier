#pragma once
#include "MaterialDescriptor.h"
#include <algorithm>

namespace Frontier {
// IDs persist in extras.slate_automotive_profile. Zero preserves the standard BSDF.
// Colours are LINEAR Rec.709; coat colour is normal-incidence round-trip transmittance.
inline constexpr const char* kAutomotiveFamilyNames[5] = {
    "Candy Ruby", "RGB Glitter", "Iridescent Pearl", "Metallic Cobalt", "Metallic Copper"
};
inline void AuthorAutomotiveShowcase(MaterialSlabDescriptor& s, unsigned profile, float sweep) noexcept {
    if (profile < 1 || profile > 5) return;
    const float t=std::clamp(sweep,0.0f,1.0f);
    s.SlateAutomotiveProfile=static_cast<float>(profile);s.SlateAutomotiveSweep=t;
    s.CoatWeight=1.0f;s.CoatIor=1.5f;s.CoatRoughness=0.08f+0.14f*t;
    s.CoatDarkening=1.0f;s.SpecularIor=1.5f;s.SpecularWeight=1.0f;
    s.SpecularColor[0]=.72f;s.SpecularColor[1]=.77f;s.SpecularColor[2]=.82f;
    s.SpecularRoughness=.055f+.045f*t;s.SlateGlintUvScale=1.0f;
    s.SlateGlintDensity=1.0f+7.0f*t;
    // Conventional fields remain a useful approximation for non-Frontier readers.
    s.BaseMetalness=.8f;s.BaseColor[0]=.014f;s.BaseColor[1]=.045f;s.BaseColor[2]=.13f;
    if(profile==1){ // transparent red candy dye over silver flakes; neutral outer reflection
        s.BaseColor[0]=.18f;s.BaseColor[1]=.18f;s.BaseColor[2]=.18f;
        s.CoatColor[0]=.88f;s.CoatColor[1]=.012f+.028f*t;s.CoatColor[2]=.006f+.01f*t;
        s.SlateGlintDensity=4.0f+8.0f*t;s.CoatRoughness=.08f+.04f*t;
    } else if(profile==2){
        s.BaseColor[0]=.004f;s.BaseColor[1]=.008f;s.BaseColor[2]=.022f;
        s.SlateGlintDensity=.5f+15.5f*t;s.CoatColor[0]=.5f;s.CoatColor[1]=.72f;s.CoatColor[2]=1.0f;
    } else if(profile==3){
        s.BaseColor[0]=.12f;s.BaseColor[1]=.16f;s.BaseColor[2]=.22f;
        s.SpecularColor[0]=.04f;s.SpecularColor[1]=.04f;s.SpecularColor[2]=.04f;
        s.ThinFilmWeight=1.0f;s.ThinFilmThickness=.22f+.63f*t;s.ThinFilmIor=2.3f;
        s.SlateGlintDensity=2.0f+6.0f*t;
    } else if(profile==5){
        s.BaseColor[0]=.16f;s.BaseColor[1]=.034f;s.BaseColor[2]=.009f;
        s.SpecularColor[0]=.95f;s.SpecularColor[1]=.64f;s.SpecularColor[2]=.42f;
        s.SlateGlintDensity=2.0f+10.0f*t;
    }
}
} // namespace Frontier
