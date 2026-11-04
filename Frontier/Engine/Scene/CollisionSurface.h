/*==========================================================================================================================================
                                                            COLLISIONSURFACE.H
==========================================================================================================================================*/
// 🧩 The collision surface of a Geometry record: its shown facets as world-space triangles, the same placements the renderer
//    draws, so a collider matches what the viewport shows. The Rendering extension fills an archive of these from the scene's
//    facet sets; the Physics extension turns each into a fixed triangle set or a moving convex hull. Engine-owned so the readout,
//    the renderer and the solver share one type without including one another.

#pragma once

#include "Frontier/Engine/Scene/GeometryLayout.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One record's collision facets in world space. Corners holds three positions per triangle; Triangles indexes them. Placement
//    is the first shown copy's world transform, the frame a moving hull is expressed in. Copies is how many shown copies fed it.
struct CollisionSurface
{
    std::vector<std::array<float, 3>> Corners{};      // [m] world-space triangle corners, three per facet
    std::vector<uint32_t>             Triangles{};    // [-] three corner indices per triangle
    PlacementMatrix                   Placement{};    // [-] the first shown copy's world transform (column-major)
    uint32_t                          Copies = 0;     // [-] shown copies that contributed
};

// 📝 Record token value → its collision surface. Empty for a record the scene did not resolve to facets.
using CollisionSurfaceArchive = std::unordered_map<uint64_t, CollisionSurface>;

} // namespace Frontier
