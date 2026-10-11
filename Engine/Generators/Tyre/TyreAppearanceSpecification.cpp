//============================================================================================================================================
//                                                     TYREAPPEARANCESPECIFICATION.CPP
//============================================================================================================================================
// 📦 The sidewall decal sequence a new tyre document starts with.

#include "TyreAppearanceSpecification.h"

namespace Frontier {

std::vector<SidewallDecalSpecification> DefaultSidewallDecals()
{
    std::vector<SidewallDecalSpecification> Decals;
    Decals.reserve(4u);

    // ① the brand mark, set heavy and moulded proud of the sidewall
    SidewallDecalSpecification Brand;
    Brand.Identity = "decal-1";
    Brand.Name = "Brand mark";
    Brand.Text = "{brand}";
    Brand.Face = DecalFace::Heavy;
    Brand.Size = 68.0f;
    Brand.Colour = 0xFFE8E8E8u;
    Brand.Across = 2.0f;
    Brand.Along = 50.0f;
    Brand.Alignment = DecalAlignment::Left;
    Brand.Weight = 900.0f;
    Brand.Embossed = true;
    Decals.push_back(Brand);

    // ② the model name, italic, a size down from the brand
    SidewallDecalSpecification Model;
    Model.Identity = "decal-2";
    Model.Name = "Model name";
    Model.Text = "{name}";
    Model.Face = DecalFace::Outfit;
    Model.Size = 54.0f;
    Model.Colour = 0xFFE8E8E8u;
    Model.Across = 14.0f;
    Model.Along = 50.0f;
    Model.Alignment = DecalAlignment::Left;
    Model.Weight = 800.0f;
    Model.Italic = true;
    Model.Embossed = true;
    Decals.push_back(Model);

    // ③ the marked size, which re-reads itself whenever the carcass changes
    SidewallDecalSpecification Marked;
    Marked.Identity = "decal-3";
    Marked.Name = "Specification";
    Marked.Text = "{size}  ·  {type}  ·  {depth}mm";
    Marked.Face = DecalFace::Mono;
    Marked.Size = 25.0f;
    Marked.Colour = 0xFFBFBFBFu;
    Marked.Across = 28.0f;
    Marked.Along = 50.0f;
    Marked.Alignment = DecalAlignment::Left;
    Marked.Weight = 600.0f;
    Decals.push_back(Marked);

    // ④ the compliance strip, which every road tyre is required to carry
    SidewallDecalSpecification Compliance;
    Compliance.Identity = "decal-4";
    Compliance.Name = "Compliance";
    Compliance.Text = "DOT SL8T {serial}  ·  TUBELESS RADIAL  ·  MAX 51 PSI";
    Compliance.Face = DecalFace::Mono;
    Compliance.Size = 16.0f;
    Compliance.Colour = 0xFFA7A7A7u;
    Compliance.Across = 2.0f;
    Compliance.Along = 72.0f;
    Compliance.Alignment = DecalAlignment::Left;
    Compliance.Weight = 600.0f;
    Compliance.Opacity = 0.65f;
    Compliance.Embossed = true;
    Decals.push_back(Compliance);

    return Decals;
}

}   // namespace Frontier
