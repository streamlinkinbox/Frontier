/*==========================================================================================================================================
                                                RADIANTFACETFIXTURES.H
==========================================================================================================================================*/
// 🧩 Deterministic triangle inputs for historical and Engine probability comparisons.

#pragma once

#include "Engine/Rendering/PathReplayFootprint.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{

std::vector<Frontier::PathReplayFacet> ConstructRadiantFacetFixture()
{
    std::vector<Frontier::PathReplayFacet> Facets(4096);
    uint32_t Bits = 0x7d8c5a31u;
    auto Next = [&Bits]()
    {
        Bits = Bits * 1664525u + 1013904223u;
        return Bits;
    };
    for (std::size_t FacetIndex = 0; FacetIndex < Facets.size(); ++FacetIndex)
    {
        auto& Facet = Facets[FacetIndex];
        const float OriginX = float(int(Next() % 20001) - 10000) / 257.0f;
        const float OriginY = float(int(Next() % 20001) - 10000) / 257.0f;
        const float OriginZ = float(int(Next() % 20001) - 10000) / 257.0f;
        const float Length  = float(1 + Next() % 1000) / 31.0f;
        const float Width   = float(1 + Next() % 1000) / 37.0f;
        Facet.A = {OriginX, OriginY, OriginZ, 0};
        Facet.B = {OriginX + Length, OriginY + Length * .13f, OriginZ - Length * .07f, 0};
        Facet.C = {OriginX - Width * .21f, OriginY + Width, OriginZ + Width * .11f, 0};
        if (FacetIndex % 31 == 0) Facet.C = Facet.B;
        if (FacetIndex % 7 != 0)
        {
            const float Brightness = std::ldexp(1.0f, int(Next() % 201) - 100);
            Facet.Emissive = {Brightness, Brightness * .375f, Brightness * .125f, 0};
        }
    }
    return Facets;
}

} // namespace
