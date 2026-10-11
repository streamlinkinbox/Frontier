//============================================================================================================================================
//                                                            TREADREGIONSOLVER.H
//============================================================================================================================================
// 📦 Boolean stage: an ordered layer sequence becomes depth-ordered floor pieces that partition the tread.

#pragma once

#include "TreadPatternSpecification.h"
#include "TreadSpecification.h"

#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     RESULT RECORDS
//------------------------------------------------------------------------------------------------------------------------

/// 📦 One point of a floor outline, in the tread's unrolled parametric domain.
/// note  Circumferential runs 0 to the circumference; Lateral is signed about the centreline.
/// tag   schema
struct TreadContourPoint
{
    float    Circumferential = 0.0f;   // [mm]  - distance around the tyre
    float    Lateral         = 0.0f;   // [mm]  - distance across, 0 at the centreline
};

/// 📦 One closed outline of a floor piece.
/// tag   schema
struct TreadContour
{
    std::vector<TreadContourPoint> Points;
    bool     Hole = false;   // [-]  - true when this contour subtracts from the piece around it
};

/// 📦 All the tread that sits at one depth below the moulded surface.
/// note  A piece may be many disjoint islands and may carry holes; both arrive as contours.
/// tag   schema
struct TreadFloorPiece
{
    float    Depth = 0.0f;   // [mm]  - drop below the moulded surface; 0 is the untouched crown
    std::vector<TreadContour> Contours;
};

/// 📦 The depth-ordered floors, plus what the partition check measured.
/// tag   schema
struct TreadRegionResult
{
    std::vector<TreadFloorPiece> Pieces;
    std::vector<float>           Levels;            // [mm]  - the distinct depths, shallow to deep
    double   DomainArea     = 0.0;                  // [mm²] - area of the tread domain the pieces must fill
    double   GapArea        = 0.0;                  // [mm²] - domain no piece covers
    double   ExcessArea     = 0.0;                  // [mm²] - piece area outside the domain
    double   OverlapArea    = 0.0;                  // [mm²] - area counted by more than one piece
    double   BoundaryLength = 0.0;                  // [mm]  - total contour length, sets the sliver tolerance
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         SOLVER
//------------------------------------------------------------------------------------------------------------------------

/// 📦 Cuts the layer sequence into floor pieces that tile the tread exactly once.
/// in    Pattern        [-]   the ordered layer sequence; Noise layers are skipped here
/// in    Specification  [-]   carcass parameters, read for TreadDepth and Wear
/// note  Layer depths are authored as fractions of TreadDepth; the pieces returned carry millimetres.
/// in    Derived        [-]   values from DeriveTreadValues for the same specification
/// out   TreadRegionResult    pieces shallow-to-deep, with the partition measurements filled in
/// note  ⚠️ Gaps and overlaps are measured as set operations against the domain, never by comparing a sum of
///       piece areas against it. Two pieces meeting along a shared boundary produce sliver artifacts one
///       integer unit wide whose signed areas are tiny and of either sign — a sum double-counts them and a
///       sum is also blind to a gap that happens to equal an overlap. The set difference is blind to neither.
/// cost  🔴
/// note  📐 Nested regions Rᵢ hold everything cut to at least depth dᵢ, so R₁ ⊇ R₂ ⊇ … The floor at each
///       depth is the shell between consecutive regions, Sᵢ = Rᵢ − Rᵢ₊₁, and the uncut crown is the
///       domain minus R₁. Those shells are disjoint and their union is the whole domain, which is the
///       invariant the mesh stage depends on and the one the gate measures.
/// note  ⚠️ Boolean work happens in 64-bit integers at hundredths of a millimetre. Two pieces sharing a
///       boundary therefore carry bit-identical coordinates along it, which is what lets the mesh stage
///       weld them into one edge instead of two that merely look alike.
/// tag   api, allocating, nonthrowing
[[nodiscard]] TreadRegionResult SolveTreadRegions(const TreadPatternSpecification& Pattern,
                                                  const TreadSpecification&        Specification,
                                                  const TreadDerivedValues&        Derived) noexcept;

}   // namespace Frontier
