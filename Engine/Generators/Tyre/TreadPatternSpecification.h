//============================================================================================================================================
//                                                        TREADPATTERNSPECIFICATION.H
//============================================================================================================================================
// 📦 The tread pattern as an ordered layer sequence: each layer cuts grooves of one kind to one depth.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                      LAYER KINDS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 What a layer cuts. The kind selects which fields of TreadLayerSpecification are read.
/// note  ⚠️ Noise carries no polygons. It perturbs the depth field only, so the region solver skips it and
///       the mesh solver applies it after the floors are placed. A noise layer in the boolean stack would
///       produce an unbounded number of microscopic pieces.
/// tag   schema
enum class TreadLayerKind : uint8_t
{
    Circumferential = 0,   // a groove running around the tyre, optionally zig-zagged
    Lateral         = 1,   // straight slots running across the tread
    Chevron         = 2,   // lateral slots mirrored about the centreline
    Sipe            = 3,   // very narrow lateral cuts
    Dimple          = 4,   // discs or rings in rows
    Hexagon         = 5,   // a hexagonal lattice of groove lines
    Noise           = 6    // depth perturbation only, never a polygon
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         LAYER
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One groove layer. Fields not named by the kind are ignored.
/// note  Lateral coordinates are fractions of the patterned half width, so From -1 to To 1 spans the tread.
///       DepthFraction is likewise a fraction of TreadDepth rather than a length. Both choices mean one
///       pattern survives a change of section width or tread depth without being re-authored, which is why
///       a preset can be worn, re-sized and re-moulded and still read as the same pattern.
/// tag   schema
struct TreadLayerSpecification
{
    TreadLayerKind Kind  = TreadLayerKind::Circumferential;
    float    DepthFraction = 1.0f;  // [-]   - how deep this layer cuts, as a fraction of TreadDepth
    float    Width       = 8.0f;    // [mm]  - groove width, or line width for Hexagon and ringed Dimple

    float    Position    = 0.0f;    // [-]   - Circumferential: lateral seat, fraction of the tread half
    float    Zig         = 0.0f;    // [mm]  - Circumferential and Lateral: zig-zag amplitude
    float    ZigCount    = 8.0f;    // [-]   - Circumferential: zig-zag periods around the circumference

    float    Angle       = 0.0f;    // [deg] - Lateral family: slot lean from the radial direction
    float    Count       = 48.0f;   // [-]   - repeats around the circumference
    float    Phase       = 0.0f;    // [-]   - rotational offset, fraction of one repeat
    bool     Mirror      = false;   // [-]   - Chevron: mirror the slot about the centreline
    float    From        = -1.0f;   // [-]   - lateral start, fraction of the tread half
    float    To          = 1.0f;    // [-]   - lateral end, fraction of the tread half
    float    Curve       = 0.0f;    // [-]   - quadratic bow along the slot

    float    Rows        = 1.0f;    // [-]   - Dimple: lateral rows
    bool     Stagger     = false;   // [-]   - Dimple: offset alternate rows by half a pitch
    float    Radius      = 3.0f;    // [mm]  - Dimple: disc radius
    bool     Ring        = false;   // [-]   - Dimple: cut an annulus rather than a disc

    float    Size        = 8.0f;    // [mm]  - Hexagon: circumradius of one cell
    float    Rotate      = 0.0f;    // [deg] - Hexagon: lattice rotation

    float    Amount      = 0.06f;   // [-]   - Noise: depth perturbation, as a fraction of TreadDepth
    float    Scale       = 3.0f;    // [mm]  - Noise: feature size of the perturbation
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PATTERN
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The ordered layer sequence that defines one tread.
/// note  ⚠️ Order is read, not decorative. The region solver groups layers by depth and unions each group,
///       so two layers at the same depth merge into one floor rather than cutting each other.
/// tag   schema
struct TreadPatternSpecification
{
    std::string                          Name;
    std::vector<TreadLayerSpecification> Layers;

    [[nodiscard]] bool HasPolygonLayer() const noexcept
    {
        for (const TreadLayerSpecification& Layer : Layers)
            if (Layer.Kind != TreadLayerKind::Noise)
                return true;
        return false;
    }
};

}   // namespace Frontier
