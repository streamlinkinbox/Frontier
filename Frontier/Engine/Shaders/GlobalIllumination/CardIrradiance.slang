//============================================================================================================================================
//                                                          CARDIRRADIANCE.SLANG
//============================================================================================================================================
// 🧩 The scheduled-tile profile common-domain by the three card lighting dispatches (direct, radiosity, final).

#pragma once
#include "IntrinsicParity.slang"

// 📝 Three dispatches walk the same scheduled 8×8 card tiles, in this order each refresh:
//    ① CardDirect.slang    — E_direct = E_sun · max(n·l, 0) · V, V traced through the scene distance field;
//    ② CardRadiosity.slang — one site per 4×4 texels gathers 16 cosine rays whose hits read LAST refresh's card
//       radiance, so every refresh adds a bounce and the depot converges toward multiple bounces;
//    ③ CardFinal.slang     — L_card = (albedo/π)·(E_direct + E_indirect) + incandescence, the radiance tracers read.

struct CardTileJob
{
    uint TileCount;       // [-]    scheduled tiles this refresh
    uint SiteStride;      // [site] sites per row of the card texel space
    float StaticWeight;     // [-]    minimum radiosity weight for static cards
    float DynamicWeight;    // [-]    minimum radiosity weight for moving cards
};

uint2 ResolveTileTexel(uint2 Assignment, uint2 Local)
{
    return uint2(Assignment.y & 0xFFFFu, Assignment.y >> 16u) * 8u + Local;
}

uint ResolveWorkIndex(uint3 Group)
{
    return Group.x + Group.y * 65535u;
}
