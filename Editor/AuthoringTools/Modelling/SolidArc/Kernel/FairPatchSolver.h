//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/FairPatchSolver.h — Energy-fair NURBS surfaces over 3…N boundary curves with G0 / G1 / G2 rims and guides
//============================================================================================================================================
// A fill surface is only as good as its rims: the Coons patch (SkinSolver::Patch) is exact on the boundary but knows
//    nothing about the faces next to it, so it always meets them with a crease. FairPatch keeps the exact boundary and
//    solves for the interior instead:
//    · the boundary pole rows are fixed (G0),
//    · for a rim that has a *support* — the adjacent face of a body, or an explicit normal field — the first pole row
//      inside is constrained so the cross-boundary derivative lies in the support's tangent plane (G1), and the second
//      row so the normal curvature across the rim matches the support's second fundamental form (G2),
//    · guide curves become interior interpolation conditions (their points are re-projected each round),
//    · everything else is decided by a bending energy on the control net (second divided differences in u, v and uv),
//      so the interior is as flat as the rims allow — no ripples, no Coons twist.
//    The result is a linear least-squares problem in the interior poles (three right-hand sides for x, y, z), solved
//    directly. G2 is nonlinear through |S_v|², so the solve is repeated a few rounds with the previous magnitude.
//    Four rims give one untrimmed quad; three or five-plus rims (or four with Star) give N quads around a common
//    centre whose spokes carry a shared normal field, so neighbouring quads are G1 across them; the quads are sewn.
#pragma once
#include "Kernel/CurveSpecification.h"
#include "Kernel/SkinSolver.h"
#include "Kernel/SurfaceSpecification.h"
#include <vector>

namespace Frontier
{

enum class RimContinuity : uint8_t { Position = 0, Tangent, Curvature };                // G0 / G1 / G2
[[nodiscard]] const char* Describe(RimContinuity Continuity) noexcept;

// One boundary of the fill. The support says which tangent plane (and curvature) the rim must honour: either a
//    surface the rim lies on (queried by closest point, so its parameterisation is irrelevant) or a field of unit
//    normals at uniform parameters along Curve, start → end. Without a support the rim is G0 whatever was asked.
struct FairRim
{
    NurbsCurve          Curve;                                                          // [-]
    RimContinuity       Continuity = RimContinuity::Position;                           // [-]
    double              Tension = 1.0;                                                  // [-] cross-derivative magnitude relative to the Coons fill
    const NurbsSurface* Support = nullptr;                                              // [-] adjacent face (not owned)
    std::vector<Vec3>   NormalField;                                                    // [-] alternative support
    [[nodiscard]] bool Supported() const noexcept { return Support != nullptr || NormalField.size() >= 2; }
};

struct FairPatchOptions
{
    int    Spans = 10;                                                                  // [-] minimum knot spans per direction of each quad
    bool   Star = false;                                                                // [-] 4 rims: N quads about the centre instead of one quad
    double Fairness = 1.0;                                                              // [-] bending-energy weight
    int    Rounds = 3;                                                                  // [-] G2 / guide re-projection rounds
    double Tolerance = ScalarCriteria::MergeTolerance;                                  // [m] rim chaining
};

struct FairPatchReport
{
    int    Quads = 0;                                                                   // [-]
    int    Unknowns = 0;                                                                // [-] interior poles solved (largest quad)
    int    UnsupportedRims = 0;                                                         // [-] rims asked for G1/G2 without a support
    double TangentBreak = 0.0;                                                          // [rad] worst normal angle against supports along G1/G2 rims
    double SeamBreak = 0.0;                                                             // [rad] worst normal angle across the spokes between the quads of an N-sided fill
    double CurvatureBreak = 0.0;                                                        // [1/m] worst normal-curvature mismatch along G2 rims
    double GuideDeviation = 0.0;                                                        // [m] worst distance from guide samples to the surface
    double Energy = 0.0;                                                                // [m²] sampled bending energy Σ(Suu² + 2Suv² + Svv²)
    double CoonsEnergy = 0.0;                                                           // [m²] the same for the plain Coons fill
};

struct FairPatchSolver
{
    // 3…N rims in any order / sense (ends must meet within Options.Tolerance) plus optional guide curves.
    [[nodiscard]] static Deliver<SkinSolver::Skin> Build(std::vector<FairRim> Rims, const std::vector<NurbsCurve>& Guides,
                                                         const FairPatchOptions& Options, FairPatchReport* Report = nullptr) noexcept;
    // Exactly four rims → one untrimmed quad (u along the first rim, v along the second, after ring ordering).
    [[nodiscard]] static Deliver<NurbsSurface> Quad(std::vector<FairRim> Rims, const std::vector<NurbsCurve>& Guides,
                                                    const FairPatchOptions& Options, FairPatchReport* Report = nullptr) noexcept;

    //---------------------------------------------- measurement (public for verification) ----------------------------------------------
    // Worst angle [rad] between the surface normal and the rim support normal at `Samples` points along the rim.
    [[nodiscard]] static double TangentBreak(const NurbsSurface& S, const FairRim& Rim, int Samples = 24) noexcept;
    // Worst |κ_surface − κ_support| [1/m] of the normal curvature across the rim (direction: the surface's own cross derivative).
    [[nodiscard]] static double CurvatureBreak(const NurbsSurface& S, const FairRim& Rim, int Samples = 24) noexcept;
    // Sampled thin-plate energy Σ (Suu² + 2 Suv² + Svv²) du dv over the domain (finite differences on a lattice).
    [[nodiscard]] static double BendingEnergy(const NurbsSurface& S, int Lattice = 24) noexcept;
    // Unit normal + second fundamental form of a support at the point nearest P. II(d) for a 3D tangent direction d.
    struct SupportTouch { Vec3 Normal; Vec3 Du, Dv, Duu, Duv, Dvv; bool Live = false; [[nodiscard]] double SecondForm(Vec3 Direction) const noexcept; };
    [[nodiscard]] static SupportTouch Touch(const NurbsSurface& Support, Vec3 P) noexcept;
    // Normal of a rim's support at fraction F ∈ [0,1] along the rim (Support → closest point; NormalField → interpolated).
    [[nodiscard]] static Vec3 SupportNormal(const FairRim& Rim, double F, Vec3 P, SupportTouch* Full = nullptr) noexcept;
};

} // namespace Frontier
