//============================================================================================================================================
// 📦 Editor/EditorTools/ParametricSketcher/Kernel/BlendSolver.h — edge blends (chamfer / fillet) on a solid, and planar face push
//============================================================================================================================================
// The old BrepBody::ChamferEdge rewrote coedge pointers in place: it redirected the two adjacent coedges onto new
//    set-back edges and deliberately left the original edge orphaned in the table ("future passes can prune it"). The
//    loops never closed again, so every chamfered body came back as a Sheet — χ=3, genus −1, volume 0. It is not a
//    tolerance bug, it is the wrong formulation, so this file replaces it rather than patching it.
//
//    Here a blend is a set operation against an exactly-built tool solid, which is how a solid modeller states it.
//    Curved-edge routes are deliberately bounded and direct: a complete native right-cylinder cap is rebuilt from its
//    retained cylinder and an exact conical frustum/quarter torus, while the first general smooth-support case rebuilds
//    a planar annular shoulder and cylindrical boss around their offset-surface spine. A G1 tangent-chain walker lets
//    that boss root be selected through one member of a representation-split full ring, bounded semicircle, or
//    general-angle sector with two radial caps. Intentional multi-edge sets deduplicate those members and compose
//    independent rolls transactionally:
//
//      chamfer(E, s) = Body − Wedge(E, s)                 the planar corner prism beyond the set-back plane is cut away
//      circular chamfer = Cylinder(R, H−s) ∪ Cone(R, R−s, s)     exact right-cylinder cap bevel
//      circular fillet  = Cylinder(R, H−r) ∪ QuarterTorus(R−r, r) exact right-cylinder cap rolling-ball fillet
//      boss-root fillet = Shoulder(offset r) ∪ QuarterTorus(R+r, r) ∪ Boss(offset r) exact plane–cylinder G1 roll
//      fillet (E, R) = Body − Wedge(E, t) , then the flat  t = R / tan(θ/2) is the tangent set-back for dihedral θ
//                      face is re-seated on the tangent cylinder of radius R and its two cap edges rebuilt as arcs
//      circular cap push  = Cylinder(R, H+d)                        exact native-cylinder cap offset
//      cylindrical side push = Cylinder(R+d, H)                     exact native-cylinder radial face offset
//      push  (F, d)  = Body ∪ Prism(F, d)  ·  Body − Prism(F, −d)
//
//    IntersectionSolver::Combine already returns a closed, consistently wound, manifold B-rep with (u,v) trims on
//    every coedge, so the blends inherit those guarantees instead of re-establishing them by hand — and they compose,
//    so chamfering an edge of an already-pushed face works. Removal volumes match the closed form (see the Removal
//    helpers); verification asserts against those, so a regression shows up as a number, not as a picture.
#pragma once

#include "TopologySpecification.h"
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                  PHASE 32z · ASYMMETRIC (UNEQUAL-RADIUS) SUPPORT PAIRS
//------------------------------------------------------------------------------------------------------------------------
// Two coaxial circular supports of unequal radius bound a conical (tapered) run instead of a cylindrical one. The
//    specification-level validators below are the prerequisites the plane–cone boss-root fillet (see FilletEdge) and
//    the later Phase 33 variable-radius work both rest on; they operate on explicit descriptors so their acceptance
//    boundaries can be verified without a B-rep. The B-rep route itself classifies its supports from topology.

enum class AsymmetricSupportClassification : uint8_t
{
    TaperedFrustum,                                                                     // one conical run between two unequal coaxial circles
    VariableRadiusRoll,                                                                 // linear radius law along one straight spine
    UnequalRadialCaps,                                                                  // radial caps of unequal reach on one axis
    EqualRadiusAsymmetricPlanes,                                                        // equal circles on parallel, non-coaxial planes
    PartialEndpointChain                                                                // chain with one open continuation (refused)
};

struct EndpointSupport
{
    Vec3   Centre{};                                                                    // [m]   circle centre
    Vec3   Normal{};                                                                    // [-]   support plane normal (any length)
    double Radius = 0.0;                                                                // [m]   circle radius
    double EndpointAngle = 0.0;                                                         // [rad] optional radial-cap angle, 0 = full circle
};

// r(T) = Start + (End − Start)·T on T ∈ [0, 1]: the only radius law Phase 32z admits.
struct VariableRadiusLaw
{
    double Start = 0.0;                                                                 // [m]
    double End = 0.0;                                                                   // [m]

    [[nodiscard]] double Radius(double T) const noexcept { return Start + (End - Start) * T; }
    [[nodiscard]] double Slope() const noexcept { return End - Start; }
    [[nodiscard]] bool Positive() const noexcept { return std::isfinite(Start) && std::isfinite(End) && Start > 0.0 && End > 0.0; }
    [[nodiscard]] bool Decreasing() const noexcept { return Slope() < -ScalarCriteria::CircularTolerance; }
    [[nodiscard]] bool Increasing() const noexcept { return Slope() > ScalarCriteria::CircularTolerance; }
    // Frustum volume of the law swept along a straight spine of this length.
    [[nodiscard]] double SweptVolume(double Length) const noexcept
    {
        return ScalarCriteria::Pi * Length * (Start * Start + Start * End + End * End) / 3.0;
    }
};

struct AsymmetricEndpointChain
{
    std::vector<EndpointSupport> Supports;                                              // [-] ordered along one straight axis
};

// Ruled surface of revolution P(T, θ) = Origin + A·L·T + (R cos θ + B sin θ)·r(T) with a linear r(T): a cone frustum
//    written as a parametric surface so that endpoint tangency and curvature can be measured before any B-rep is built.
struct VariableRadiusSurface
{
    Vec3 Origin{};                                                                      // [m]
    Vec3 Axis{ 0, 0, 1 };                                                               // [-]
    Vec3 Radial{ 1, 0, 0 };                                                             // [-] any vector not parallel to Axis
    double Length = 0.0;                                                                // [m]
    VariableRadiusLaw Law{};

    [[nodiscard]] Vec3 Sample(double T, double Angle) const noexcept
    {
        Vec3 A = Axis.Normalised();
        Vec3 R = (Radial - A * Radial.Dot(A)).Normalised();
        Vec3 B = A.Cross(R);
        double S = ScalarCriteria::Clamp(T, 0.0, 1.0);
        return Origin + A * (Length * S) + (R * std::cos(Angle) + B * std::sin(Angle)) * Law.Radius(S);
    }

    // ∂P/∂T: the generator direction at this angle. It is constant along T because the law is linear.
    [[nodiscard]] Vec3 TangentAlong(double Angle) const noexcept
    {
        Vec3 A = Axis.Normalised();
        Vec3 R = (Radial - A * Radial.Dot(A)).Normalised();
        Vec3 B = A.Cross(R);
        return A * Length + (R * std::cos(Angle) + B * std::sin(Angle)) * Law.Slope();
    }

    // Outward unit normal (∂P/∂θ × ∂P/∂T), independent of T for a linear law.
    [[nodiscard]] Vec3 Normal(double Angle) const noexcept
    {
        Vec3 A = Axis.Normalised();
        Vec3 R = (Radial - A * Radial.Dot(A)).Normalised();
        Vec3 B = A.Cross(R);
        Vec3 Circumferential = R * -std::sin(Angle) + B * std::cos(Angle);
        return Circumferential.Cross(TangentAlong(Angle)).Normalised();
    }

    // Normal curvature around the axis: cos α / r(T) with tan α = slope / length. Infinite at a degenerate radius.
    [[nodiscard]] double CircumferentialCurvature(double T) const noexcept
    {
        double Radius = Law.Radius(ScalarCriteria::Clamp(T, 0.0, 1.0));
        if (Length <= ScalarCriteria::GeometricTolerance) return ScalarCriteria::Infinity;
        return Radius > ScalarCriteria::GeometricTolerance
            ? 1.0 / (Radius * std::sqrt(1.0 + (Law.Slope() / Length) * (Law.Slope() / Length))) : ScalarCriteria::Infinity;
    }

    [[nodiscard]] double MeridionalCurvature() const noexcept { return 0.0; }           // straight generators
};

struct AsymmetricBlendSpecification
{
    EndpointSupport Low{};
    EndpointSupport High{};
    AsymmetricSupportClassification Classification = AsymmetricSupportClassification::TaperedFrustum;
    double MinimumClearance = 0.0;                                                      // [m]
    double BlendRadius = 0.0;                                                           // [m]
    VariableRadiusLaw RadiusLaw{};
};

// Local frame of a straight manifold edge shared by two planar faces.
struct EdgeCornerFrame
{
    Vec3   Start, End;                                                                  // [m] edge endpoints
    Vec3   Tangent;                                                                     // [-] unit, Start → End
    Vec3   NormalA, NormalB;                                                            // [-] unit outward normals of the two adjacent faces
    Vec3   InA, InB;                                                                    // [-] unit, in each face's plane, pointing away from the edge over that face
    Vec3   Bisector;                                                                    // [-] unit outward corner bisector
    double Length = 0.0;                                                                // [m]
    double Dihedral = 0.0;                                                              // [rad] interior angle between the two faces
    int    FaceA = -1, FaceB = -1;                                                      // [-]
};

class BlendSolver
{
public:
    // Validate an explicitly paired endpoint-support set before reconstruction. This foundation is intentionally
    // conservative: it accepts two finite, non-coincident, parallel support normals and unequal positive radii,
    // while construction/topology ownership remains in the asymmetric route.
    [[nodiscard]] static bool ValidateAsymmetricEndpointPair(const EndpointSupport& Low, const EndpointSupport& High,
                                                              double MinimumClearance, std::string& Refusal) noexcept;
    [[nodiscard]] static bool ValidateAsymmetricSpecification(const AsymmetricBlendSpecification& Specification,
                                                               std::string& Refusal) noexcept;
    [[nodiscard]] static Deliver<BrepBody> ReconstructAsymmetricFrustum(const AsymmetricBlendSpecification& Specification) noexcept;
    [[nodiscard]] static Deliver<BrepBody> ReconstructAsymmetricSupport(const AsymmetricBlendSpecification& Specification) noexcept;
    [[nodiscard]] static bool ValidateAsymmetricEndpointChain(const AsymmetricEndpointChain& Chain,
                                                               double MinimumClearance, std::string& Refusal) noexcept;
    [[nodiscard]] static bool ValidateG1EndpointMatch(Vec3 SurfaceNormal, Vec3 SupportNormal,
                                                       std::string& Refusal) noexcept;
    [[nodiscard]] static bool ValidateVariableSurfaceG1(const VariableRadiusSurface& Surface,
                                                         Vec3 LowSupportNormal, Vec3 HighSupportNormal,
                                                         double Angle, std::string& Refusal) noexcept;
    [[nodiscard]] static Deliver<VariableRadiusSurface> BuildVariableRadiusSurface(const AsymmetricBlendSpecification& Specification) noexcept;
    [[nodiscard]] static Deliver<BrepBody> ReconstructVariableRadiusRuledSolid(const AsymmetricBlendSpecification& Specification) noexcept;
    [[nodiscard]] static bool ValidateVariableSurfaceCurvature(const VariableRadiusSurface& Surface,
                                                                double MaximumCircumferentialCurvature,
                                                                std::string& Refusal) noexcept;

    // Follow G1 edge-to-edge continuations from a manifold seed. A closed edge is a singleton; an ambiguous tangent
    // branch refuses rather than selecting by edge-table order. The returned indices describe one complete chain.
    [[nodiscard]] static Deliver<std::vector<int>> TangentChain(const BrepBody& Body, int SeedEdge) noexcept;

    // Local frame of a straight manifold edge between two planar faces. Refuses anything else, with the reason.
    [[nodiscard]] static bool Frame(const BrepBody& Body, int Edge, EdgeCornerFrame& Out, std::string& Refusal) noexcept;

    // Planar-setback chamfer of one straight planar edge, or an exact conical bevel of a complete native right-cylinder cap edge.
    // SetBack is measured in each adjacent face, away from the edge (and is radial/axial for the circular-cap case).
    [[nodiscard]] static Deliver<BrepBody> ChamferEdge(const BrepBody& Body, int Edge, double SetBack) noexcept;

    // Rolling-ball fillet of one straight planar edge, an exact quarter-torus on a native right-cylinder cap, or an exact
    // concave quarter-torus at the circular root of a bounded planar-shoulder/cylindrical-boss topology. One member of a
    // representation-split full ring, semicircle, or general radial sector propagates over the chain and heals internal
    // angular support seams. Both contacts are G1; finite routes retain exact end meridians on one diameter cap or two
    // radial caps. Asymmetric/unsupported/branching smooth-support arrangements refuse without approximation.
    [[nodiscard]] static Deliver<BrepBody> FilletEdge(const BrepBody& Body, int Edge, double Radius) noexcept;

    // Transactional constant-radius fillet of an intentional seed set. Members of the same curved tangent chain are
    // deduplicated and independent chains apply in deterministic geometric order. Three orthogonal box edges at one
    // vertex rebuild with an exact spherical corner patch; four parallel box edges rebuild one rounded-prism family,
    // preserving up to eight selected-axis through/blind cavities, up to eight parallel orthogonal side blind cavities,
    // selected-axis stepped cavities, one two-to-eight-stage side cavity, or up to eight side stepped cavities with two
    // through eight stages each and at most sixteen total stages. Exactly two selected-axis two-stage cavities also remain supported.
    // AppliedChains receives the committed count, or zero.
    [[nodiscard]] static Deliver<BrepBody> FilletEdges(const BrepBody& Body, const std::vector<int>& SeedEdges,
                                                       double Radius, int* AppliedChains = nullptr) noexcept;

    // Push a face along its own outward normal. Native right-cylinder caps and side face rebuild directly as exact
    // height/radius edits; all other planar faces use the established direct/Boolean paths. Positive adds material.
    [[nodiscard]] static Deliver<BrepBody> PushFace(const BrepBody& Body, int Face, double Distance) noexcept;

    // Closed-form volume a blend of this edge removes — the check verification asserts against.
    [[nodiscard]] static double ChamferRemoval(const EdgeCornerFrame& F, double SetBack) noexcept;
    [[nodiscard]] static double FilletRemoval(const EdgeCornerFrame& F, double Radius) noexcept;
    [[nodiscard]] static double TangentSetBack(const EdgeCornerFrame& F, double Radius) noexcept;   // R / tan(θ/2)
};

} // namespace Frontier
