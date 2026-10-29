/*==========================================================================================================================================
                                                MEDIUMRADIANCEFIXTURES.H
==========================================================================================================================================*/
// 🧩 Deterministic cloud and fog specifications for historical payload comparisons.

#pragma once

#include "Engine/Rendering/MediumRadiancePayload.h"

namespace
{

Frontier::CloudRadianceSpecification ConstructCloudRadianceFixture(uint32_t Index)
{
    Frontier::CloudRadianceSpecification Specification;
    auto& Medium = Specification.Medium;
    Medium.BaseHeightM = 100 + Index * 7;
    Medium.TopHeightM = Medium.BaseHeightM + 200 + Index;
    Medium.Coverage = double(Index % 17) / 16;
    Medium.DensityScale = double(Index % 9) / 3;
    Medium.ExtinctionPerM = double(Index % 8) / 1000;
    Medium.PhaseG = double(int(Index % 11) - 5) / 10;
    Medium.WindTimeS = double(Index) * 37;
    Medium.WindXMps = double(int(Index % 11) - 5);
    Medium.WindYMps = double(int(Index % 9) - 4);
    Medium.UniformDensityOverride = Index % 2 ? double(Index % 5) / 4 : -1;
    Medium.AmbientRadiance = {Index * .001, Index * .002, Index * .003};
    Medium.Seed = (uint64_t(0x7fc00001u + Index) << 32) | (0x7f800001u + Index);
    Specification.SolarDirection = {0.6, 0, 0.8};
    Specification.SolarRadiance = {Index * .1, Index * .2, Index * .3};
    Specification.ObserverHeightM = Index * 11;
    Specification.ViewSamples = Index;
    return Specification;
}

Frontier::FogRadianceSpecification ConstructFogRadianceFixture()
{
    Frontier::FogRadianceSpecification Specification;
    Specification.Medium.DensityAtBase = .0015;
    Specification.Medium.BaseHeightM = 0;
    Specification.Medium.FalloffHeightM = 450;
    return Specification;
}

} // namespace
