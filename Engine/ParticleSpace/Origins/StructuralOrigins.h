/*============================================================================================================================================
                                                          STRUCTURALORIGINS.H
============================================================================================================================================*/
// 🧩 Structural release-origin rest-anchor builders for spiderweb silk, woven cloth grids, botanical stems, and canopy trees

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"

#include <cstdint>
#include <vector>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct StructuralStrandAnchor
{
    uint32_t            StrandIndex       = 0u;                  // [idx]   - Logical strand / spoke / row index
    uint32_t            VertexCount       = 0u;                  // [count] - Vertex count along strand (<= 20)
    float               RestSegmentLength = 0.12f;               // [m]     - Rest distance between adjacent vertices
    Mathematics::Axis3D RootPosition      { 0.0f, 0.0f, 0.0f };  // [m]     - Fixed anchor root (+Z Up)
    Mathematics::Axis3D TipDirection      { 0.0f, 0.0f, 1.0f };  // [-]     - Initial unit growth/hang axis
    bool                RootPinned        = true;                // [-]     - True when vertex 0 is constrained
    bool                TipPinned         = false;               // [-]     - True when final vertex is constrained
};

struct StructuralBondLink
{
    uint32_t VertexA    = 0u;    // [idx] - First anchor index
    uint32_t VertexB    = 0u;    // [idx] - Second anchor index
    float    RestLength = 0.1f;  // [m]   - Rest edge length
    float    DewWeight  = 0.0f;  // [0-1] - Droplet / highlight intensity weight
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<StructuralStrandAnchor> BuildClothGridAnchors(const ClothGridOriginSpec& Spec);
[[nodiscard]] std::vector<StructuralStrandAnchor> BuildBotanicalAnchors(const BotanicalRootOriginSpec& Spec);
[[nodiscard]] std::vector<StructuralStrandAnchor> BuildCanopySkeletonAnchors(const CanopySkeletonOriginSpec& Spec);
[[nodiscard]] std::vector<StructuralBondLink>     BuildSpiderwebAnchors(
    const SpiderwebOriginSpec&        Spec,
    std::vector<Mathematics::Axis3D>& OutPositions);

} // namespace Frontier::ParticleSpace
