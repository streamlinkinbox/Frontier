//============================================================================================================================================
//                                                           RIMSPECIFICATION.H
//============================================================================================================================================
// 📦 The procedural rim: a barrel lathed from a bead-seat profile, with an optional centre of repeated pieces.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "TreadSpecification.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                        MATERIALS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One rim material, as the three numbers a metal needs.
/// tag   schema
struct RimMaterial
{
    const char* Name       = "Steel";
    uint32_t    Colour     = 0xFF9EA5ADu;   // [-]  - ARGB base colour
    float       Metalness  = 0.88f;         // [-]
    float       Roughness  = 0.30f;         // [-]
};

/// 📦 The materials the generator offers, in the order the editor lists them.
/// out   span of RimMaterial  five entries, Steel first
/// tag   api, nonthrowing
[[nodiscard]] const std::vector<RimMaterial>& RimMaterials();

//------------------------------------------------------------------------------------------------------------------------
//                                                   CENTRE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 How the centre of the rim is built between hub and barrel.
/// note  Blank barrel is not a missing feature. A barrel with no centre is what a tyre is fitted to while the
///       wheel design is still being decided, and it is the state every other layout branches from.
/// tag   schema
enum class RimCentreLayout : uint8_t
{
    BlankBarrel    = 0,   // nothing between hub and barrel
    StraightSpokes = 1,   // one beam per piece, hub to barrel
    SplitSpokes    = 2,   // each beam forks in two before it lands
    YSpokes        = 3    // each beam forks, and the fork forks
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

/// 📦 The rim half of a tyre document.
/// note  Diameter and bead width deliberately are NOT held here. They live on TreadSpecification, because a
///       rim that carried its own copy could be set to a diameter the tyre is not moulded for, and then every
///       preset change would need a reconciliation pass. Sharing the two numbers makes the mismatch
///       unrepresentable instead of merely unlikely.
/// tag   schema
struct RimSpecification
{
    uint32_t        Material   = 0u;              // [-]   - index into RimMaterials()
    uint32_t        Tint       = 0xFFFFFFFFu;     // [-]   - ARGB multiplied over the material colour
    float           Flange     = 8.0f;            // [mm]  - height of the lip above the bead seat
    float           DropWell   = 10.0f;           // [mm]  - depth of the well below the bead seat

    RimCentreLayout Layout     = RimCentreLayout::BlankBarrel;
    float           PieceCount = 5.0f;            // [-]   - beams around the centre
    float           PieceWidth = 18.0f;           // [mm]
    float           PieceDepth = 8.0f;            // [mm]
    float           HubFraction = 0.28f;          // [-]   - hub radius as a fraction of the bead radius

    bool            Valve      = true;
    float           ValveAngle = 215.0f;          // [deg] - where the valve sits around the rim
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        PROFILE
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One point of the rim's lathe profile, in the lateral-radial plane.
/// tag   schema
struct RimProfilePoint
{
    float Lateral = 0.0f;   // [mm]  - signed across the wheel
    float Radius  = 0.0f;   // [mm]  - from the axis
};

/// 📦 The closed profile the rim barrel is lathed from: outer face down into the drop well, back along the
///    inside at a constant wall thickness.
/// in    Rim      [-]  flange and drop-well controls
/// in    Derived  [-]  values from DeriveTreadValues, read for the bead radius and half width
/// out   28 points, counter-clockwise, closed implicitly
/// tag   api, allocating, nonthrowing
[[nodiscard]] std::vector<RimProfilePoint> RimProfilePoints(const RimSpecification& Rim,
                                                            const TreadDerivedValues& Derived);

}   // namespace Frontier
