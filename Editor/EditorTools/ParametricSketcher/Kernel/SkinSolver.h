//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/SkinSolver.h — Surfaces skinned over curves: loft, sweep, pipe, Coons / N-sided patch
//============================================================================================================================================
// Everything here produces exact NURBS (no polygons) from NURBS inputs:
//    · Loft   — sections are harmonised first (same sense, seams aligned to the least twist, common degree / knots), then
//               interpolated across in V. Closed sections may be lofted into a solid; several loops per section (a filled
//               area with holes) give a solid with through-holes. Loop closes the loft back onto its first section.
//    · Sweep  — a profile is carried along a path by rotation-minimising bases (double reflection, Wang et al. 2008) or
//               Frenet bases, optionally scaled / twisted along the way, then lofted through the stations.
//    · Patch  — 3 or 4 boundary curves become a bilinearly blended Coons surface; N boundaries are split at their
//               midpoints into N Coons quads meeting at a common centre (the N-sided fill), delivered as one sewn sheet.
//    Rational boundaries (circles, arcs) are carried exactly through loft and sweep; the Coons blend needs a common
//    weight field, so rational patch boundaries are re-fitted by cubic interpolation within the given tolerance.
#pragma once
#include "Kernel/CurveSpecification.h"
#include "Kernel/SurfaceSpecification.h"
#include "Kernel/TopologySpecification.h"
#include <vector>

namespace Frontier
{

enum class SweepBases : uint8_t { RotationMinimising, Frenet, Fixed };                  // Fixed: profile keeps world orientation

struct LoftOptions
{
    int    DegreeV = 3;                                                                 // [-] across sections (clamped to count-1)
    bool   Loop = false;                                                                // [-] close back onto the first section
    bool   AlignSeams = true;                                                           // [-] rotate closed sections to the least twist
    bool   AlignSense = true;                                                           // [-] reverse sections running the other way
    bool   Solid = true;                                                                // [-] closed sections → capped body
};

struct SweepOptions
{
    SweepBases Bases = SweepBases::RotationMinimising;                               // [-]
    int    Stations = 0;                                                                // [-] 0 → chosen from the path's spans
    double ScaleEnd = 1.0;                                                              // [-] profile scale at the path end (1 = none)
    double TwistAngle = 0.0;                                                            // [rad] extra rotation about the path by the end
    bool   Solid = true;                                                                // [-] closed profile → capped body
};

// Guide curves for loft: after the loft is built, pull the sheet toward each guide by the given weight, using a
//    single linear least-squares pass on the surface control points (sample each guide at chord-length parameters,
//    project onto the surface at the closest (u,v), and bias the surface toward the guide). Weight 0 leaves the
//    loft untouched; the loft's boundary is still preserved exactly (the boundary rows are clamped in the solve).
struct LoftGuideOptions
{
    std::vector<NurbsCurve> Guides;                                                      // [-] interior curves the loft should pass through
    double                  Weight = 1.0;                                                // [-] pull strength (0 = ignore, 1 = strong)
    int                     SamplesPerGuide = 24;                                        // [-] chord-length samples
    int                     Rounds = 3;                                                  // [-] iterative re-projection rounds (1 = no re-project)
};

struct SkinSolver
{
    // Sections in flow order. Returns a sheet (open sections) or a body (closed sections and Solid) through `Body`.
    struct Skin { NurbsSurface Sheet; BrepBody Body; bool IsBody = false; };
    [[nodiscard]] static Deliver<Skin> Loft(const std::vector<NurbsCurve>& Sections, const LoftOptions& Options) noexcept;
    // Multi-loop sections: Sections[i] holds the loops of station i (same count per station, loop k ↔ loop k).
    [[nodiscard]] static Deliver<Skin> Loft(const std::vector<std::vector<NurbsCurve>>& Sections, const LoftOptions& Options) noexcept;
    [[nodiscard]] static Deliver<NurbsSurface> LoftSheet(const std::vector<NurbsCurve>& Sections, const LoftOptions& Options) noexcept;

    [[nodiscard]] static Deliver<Skin> Sweep(const NurbsCurve& Profile, const NurbsCurve& Path, const SweepOptions& Options) noexcept;
    [[nodiscard]] static Deliver<Skin> Sweep(const std::vector<NurbsCurve>& ProfileLoops, const NurbsCurve& Path, const SweepOptions& Options) noexcept;
    [[nodiscard]] static Deliver<Skin> Pipe(const NurbsCurve& Path, double Radius, bool Solid) noexcept;

    // After a loft (or any sheet) is built, project guide curves onto the sheet so it passes through them. Boundary
    //    poles are clamped, so the sheet's edges are preserved exactly; interior poles bend. Iterative (3 rounds by
    //    default) — each round re-projects the closest (u,v) so the bend follows the surface as it moves.
    [[nodiscard]] static NurbsSurface ProjectGuides(NurbsSurface Sheet, const LoftGuideOptions& Options) noexcept;

    // Coons patch over 3 or 4 boundaries (any order / sense; ends must meet within Tolerance).
    [[nodiscard]] static Deliver<NurbsSurface> CoonsPatch(std::vector<NurbsCurve> Boundaries, double Tolerance = ScalarCriteria::MergeTolerance) noexcept;
    // Any number of boundaries: 3–4 → one Coons sheet, N ≥ 5 → N Coons quads about the centre, sewn.
    [[nodiscard]] static Deliver<Skin> Patch(std::vector<NurbsCurve> Boundaries, double Tolerance = ScalarCriteria::MergeTolerance) noexcept;

    //---------------------------------------------- building blocks (public for verification) ----------------------------------------------
    // Same sense + aligned seams + common degree and knots. Rejects mixed open/closed sets.
    [[nodiscard]] static Deliver<std::vector<NurbsCurve>> Harmonise(std::vector<NurbsCurve> Sections, bool AlignSense, bool AlignSeams) noexcept;
    // Rotate a closed curve's seam so it starts at parameter T (exact: split + join).
    [[nodiscard]] static NurbsCurve SeamAt(const NurbsCurve& Closed, double T) noexcept;
    // Bases along a path: Positions, Tangents, Normals (unit, perpendicular to the tangent) at `Count` stations.
    struct PathBasis { Vec3 Point; Vec3 Tangent; Vec3 Normal; Vec3 Binormal; double Parameter; };
    [[nodiscard]] static std::vector<PathBasis> BasesAlong(const NurbsCurve& Path, int Count, SweepBases Bases) noexcept;
    // Chain boundary pieces end to start into a closed ring (reordering / reversing as needed); empty when they do not close.
    [[nodiscard]] static std::vector<NurbsCurve> RingOrder(std::vector<NurbsCurve> Pieces, double Tolerance) noexcept;
    // Curve for one face loop of a body (coedges joined in loop order); rejects when the pieces do not chain.
    [[nodiscard]] static Deliver<NurbsCurve> LoopCurve(const BrepBody& Body, int Loop) noexcept;
};

} // namespace Frontier
