//============================================================================================================================================
//                                                             TREADMESHSOLVER.H
//============================================================================================================================================
// 📦 Mesh stage: depth-ordered floor pieces become one welded quad-dominant tread, walls included.

#pragma once

#include "TreadRegionSolver.h"
#include "TreadSpecification.h"
#include "TyreMeshStructure.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        SETTINGS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 How finely the tread is meshed.
/// note  ⚠️ The grid sets quad size on the floors only. Groove walls are quad strips taken from the
///       outline itself, so their density follows the pattern and not this.
/// tag   schema
struct TreadMeshSettings
{
    float    CellCircumferential = 6.0f;   // [mm]  - floor grid pitch around the tyre
    float    CellLateral         = 6.0f;   // [mm]  - floor grid pitch across the tread
    float    ShoulderLateral     = 2.5f;   // [mm]  - finer pitch once the shoulder arc starts turning
};

/// 📦 What the mesh stage produced, beyond the mesh itself.
/// tag   schema, diagnostic
struct TreadMeshMetrics
{
    uint32_t FloorQuad     = 0u;   // [-]  - quads on the floors, whole grid cells
    uint32_t FloorTriangle = 0u;   // [-]  - triangles on the floors, cells a groove edge cut
    uint32_t WallQuad      = 0u;   // [-]  - quads forming groove walls
    double   QuadFraction  = 0.0;  // [-]  - quads over all faces, the topology claim made measurable
    uint32_t BandFailure   = 0u;   // [-]  - row bands the triangulator refused; must be 0
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         SOLVER
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Builds the tread surface: floors clipped to a grid, walls extruded from the region outlines.
/// in    Regions        [-]   output of SolveTreadRegions; its partition is assumed, not rechecked
/// in    Specification  [-]   carcass parameters, read for TreadDepth
/// in    Derived        [-]   values from DeriveTreadValues for the same specification
/// in    Settings       [-]   grid pitch
/// out   Mesh           [-]   cleared and filled; positions welded, so shared edges are shared
/// out   TreadMeshMetrics     face counts and the quad fraction
/// cost  🔴
/// note  💡 A groove wall is a quad strip by nature: extrude one boundary edge downward and it is exactly
///       one quad. Both the floor above and the floor below consume the same outline vertices, so the wall
///       closes against both without a seam — which is also what removes the T-junctions that made the
///       prototype's independently triangulated pieces leak.
/// note  ⚠️ Floors are clipped against the same grid lines from both sides of every outline, and the
///       boolean stage is exact in integers, so the two sides produce identical crossing points rather than
///       points that merely agree to a few decimals.
/// tag   api, allocating, nonthrowing
[[nodiscard]] TreadMeshMetrics SolveTreadMesh(const TreadRegionResult&  Regions,
                                              const TreadSpecification& Specification,
                                              const TreadDerivedValues& Derived,
                                              const TreadMeshSettings&  Settings,
                                              TyreMeshStructure&        Mesh) noexcept;

}   // namespace Frontier
