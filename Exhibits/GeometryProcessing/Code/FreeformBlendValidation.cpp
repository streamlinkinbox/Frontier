/*====================================================================================================================================
                                                  FREEFORMBLENDVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Priority 10: exact offsets of NURBS surfaces (parallel carrier), rolling-ball fillets / chamfers on NURBS supports,
//    offsets and hollows of NURBS walls (N4) and certified NURBS stand-ins for the procedural carriers (N3)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Construction/BodyTransform.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Feature/ProfileSweep.h"
#include "Engine/GeometryProcessing/Geometry/CurveGeometry.h"
#include "Engine/GeometryProcessing/Geometry/NurbsCarrier.h"
#include "Engine/GeometryProcessing/Geometry/ParallelCarrier.h"
#include "Engine/GeometryProcessing/Geometry/SurfaceGeometry.h"
#include "Engine/GeometryProcessing/Intersection/ImplicitSurface.h"
#include "Engine/GeometryProcessing/Intersection/SurfaceIntersection.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Local/FaceOperations.h"
#include "Engine/GeometryProcessing/Measurement/MassProperties.h"
#include "Engine/GeometryProcessing/Surfacing/CarrierApproximation.h"
#include "Engine/GeometryProcessing/Sketch/SketchSpline.h"
#include "Engine/GeometryProcessing/Intersection/IntersectionCurve.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"
#include <map>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#if defined(__unix__)
#include <unistd.h>
#endif

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double Pi = std::numbers::pi;

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - P1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally CarrierGate  = { "P1", "Parallel carrier = closed form: offsets of NURBS cylinder / sphere / torus are the analytic offsets (position, O𝑢 = (1 − dκ)·S𝑢, normal), peak curvature κ/(1 − dκ)", "≤ 10⁻¹²" };
    GateTally FreeformGate = { "P2", "Freeform bicubic patch: O𝑢, O𝑣 vs central differences, |O𝑢 × O𝑣| = (1 − dκ₁)(1 − dκ₂)·|n|, the normal is the basis normal, inversion and signed distance of points off the offset", "≤ 10⁻⁷ (differences) · ≤ 10⁻¹⁰" };
    GateTally SectionGate  = { "P3", "Intersections with parallel carriers: offset NURBS sphere ∩ plane = the circle, offset cylinder ∩ offset cylinder (Steinmetz), offset freeform ∩ plane — every chart point on both exact carriers", "interior ≤ 10⁻⁹ · open ends ≤ 10⁻⁷" };
    GateTally FoldGate     = { "P4", "Fold margin min(1 − dκᵢ): inward offsets of a unit NURBS cylinder by 0.5 (margin ½) and 1.2 (folds); offset of an offset adds distances; d = 0 returns the basis", "exact" };

    GateTally VolumeGate   = { "F1", "Rolling-ball fillets on NURBS supports = closed forms: promoted block edge (3 − (1 − π/4)·r²·L), promoted cylinder rim (Pappus), freeform D-extrusion spline rim (A·(L − c·Θ), curved Pappus) — and equal to the analytic-body fillet", "≤ 10⁻⁹ relative" };
    GateTally LawGate      = { "F2", "Rolling-ball law on every NURBS sheet: |ball − support| = r, feet on the supports along their normals, contact on the carrier, G1 between blend and supports", "≤ 10⁻⁹ (len · rad)" };
    GateTally RefusalGate  = { "F3", "Honest refusals on NURBS supports: a fillet, chamfer or variable fillet past the support's curvature radius (fold guard on both the offset and the ball-law paths) — atomic, input untouched", "exact" };
    GateTally ChamferGate  = { "C1", "Chamfers on NURBS supports (N5, ball law on extended supports) = closed forms: D-extrusion spline rim at d 0.2 and 0.5 (V₀ − d²/2·(L − d·Θ/3), curved Pappus), promoted block edge and cylinder rim — equal to the analytic-body chamfer, and every result tessellates", "≤ 10⁻⁹ relative" };
    GateTally VariableGate = { "C2", "Variable blends on NURBS supports (N5): variable chamfer on the spline rim = ∫ d(t)²/2·(|E′| − d(t)·κ|E′|/3) dt (sections in the normal planes), variable fillet on a promoted block edge = the analytic-body fillet, variable fillet on the spline rim strictly between its constant end sizes", "≤ 10⁻⁹ relative" };

    GateTally StandInGate     = { "A1", "NURBS stand-ins of rolling-ball / parallel carriers, measured off the certification lattice: 2000 random chart points — parametric |S − A|, geometric distance of A to the exact carrier, normal drift", "≤ tolerance (len) · drift ≤ angular tolerance 10⁻³ rad" };
    GateTally ExchangeGate    = { "A2", "All-NURBS bodies (ApproximateBodyAsNurbs): no procedural carrier left, audit sound, |ΔV| ≤ tolerance × area, every edge on its stand-in faces, G1 drift across blend boundaries", "≤ tolerance · 2 × tolerance (edges) · drift ≤ 10⁻³ rad" };
    GateTally ApproximateGate = { "A3", "Honest refusals: a tolerance the span cap cannot reach, a non-positive tolerance, an empty window — atomic, no body created", "exact" };
    GateTally OrderGate       = { "A4", "Convergence: tolerance 10⁻⁴ → 10⁻¹⁰ on a parallel carrier (position only); spans per direction grow as ε^(−1/4) for cubic interpolation, every step certified", "3.5 ≤ order ≤ 5" };

    GateTally OffsetGate  = { "O1", "Face offsets of a NURBS wall = closed form: the D-extrusion's spline wall ±0.1 → ΔV = H·(±d·L + π·d²/2) (Steiner: the wall leaves the base perpendicular, so the offset ends on the base plane untrimmed)", "≤ 10⁻⁹ relative" };
    GateTally HollowGate  = { "O2", "Hollow with a NURBS wall: D-extrusion, wall 0.1, top open → V = A·H − A_in·(H − t); A_in by Green's theorem on the inward offset curve trimmed at y = t (root-found crossings, Simpson per knot span)", "≤ 10⁻⁹ relative" };
    GateTally MeetingGate = { "O3", "Traced meetings (FaceChange N4): new edges between a parallel carrier and planes are exact traced intersections — on both carriers ≤ 10⁻⁹·scale, audit sound, the wall is a parallel carrier", "exact" };

    std::vector<GateTally*> AllGates() { return { &CarrierGate, &FreeformGate, &SectionGate, &FoldGate, &VolumeGate, &LawGate, &RefusalGate, &ChamferGate, &VariableGate, &OffsetGate, &HollowGate, &MeetingGate, &StandInGate, &ExchangeGate, &ApproximateGate, &OrderGate }; }

    struct DetailRow
    {
        std::string Gate            = {};
        std::string Name            = {};
        std::string Detail          = {};
        bool        PassedCondition = false;
    };
    std::vector<DetailRow> Details;

    void Report(GateTally& Gate, const std::string& Name, const std::string& Detail, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
        Details.push_back({ Gate.Code, Name, Detail, PassedCondition });
        std::fprintf(stderr, "%s %s %s\n      %s\n", PassedCondition ? "🟢" : "🔴", Gate.Code, Name.c_str(), Detail.c_str());
    }

    [[nodiscard]] std::string Scientific(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof(Text), "%.2e", Value);
        return Text;
    }

    [[nodiscard]] OrthonormalFrame Upright(Vector3 Origin) { return { Origin, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }; }

    // 📝 Bicubic freeform test patch over [0, 2]² with two knot spans each way: a saddle-and-bump height field
    //    z = 0.3·sin(x)·cos(y) + 0.1·x·y sampled into the poles (non-rational, so every curvature is genuinely freeform).
    [[nodiscard]] NurbsSurface FreeformPatch()
    {
        const int Count = 5;
        std::vector<Vector3> Poles;
        std::vector<double>  Weights;
        for (int Row = 0; Row < Count; ++Row)
            for (int Column = 0; Column < Count; ++Column)
            {
                const double X = 2.0 * Row / (Count - 1), Y = 2.0 * Column / (Count - 1);
                Poles.push_back({ X, Y, 0.3 * std::sin(X) * std::cos(Y) + 0.1 * X * Y });
                Weights.push_back(1.0);
            }
        const std::vector<double> Knots = { 0.0, 0.0, 0.0, 0.0, 1.0, 2.0, 2.0, 2.0, 2.0 };
        return *ConstructNurbsSurface(3, 3, Count, Count, Knots, Knots, Poles, Weights);
    }

    // Exact residual of a point against an offset: |X − S(foot)| − |d| along N, plus the off-normal part.
    [[nodiscard]] double OffsetResidual(const ParallelSurface& Offset, Vector3 Position, Vector2 Reference)
    {
        const Vector2 Foot   = ProjectOntoNurbsSurface(Offset.Definition->Basis, Position, Reference);
        const Vector3 Basis  = EvaluateNurbsSurface(Offset.Definition->Basis, Foot).Position;
        const Vector3 Normal = EvaluateNurbsNormal(Offset.Definition->Basis, Foot);
        const Vector3 Gap    = Position - Basis;
        return std::max(std::fabs(Dot(Gap, Normal) - Offset.Definition->Distance), Length(Gap - Normal * Dot(Gap, Normal)));
    }

    // ---- P1 closed forms ---------------------------------------------------------------------------------------------
    void ValidateClosedForms()
    {
        struct Case { const char* Name; SurfaceGeometry Analytic; Vector2 Low, High; double Distance; std::function<double(Vector3)> Residual; };
        const OrthonormalFrame Frame = Upright({ 0.3, -0.2, 0.1 });
        const auto Radial = [Frame](Vector3 Point) { const Vector3 L = Point - Frame.Origin; return std::hypot(L.X, L.Y); };
        const std::vector<Case> Cases = {
            { "NURBS cylinder r 1 offset +0.25 → r 1.25", CylinderSurface{ Frame, 1.0 }, { 0.0, -1.0 }, { 2.0 * Pi, 1.0 }, 0.25,
              [&](Vector3 Point) { return std::fabs(Radial(Point) - 1.25); } },
            { "NURBS cylinder r 1 offset −0.4 → r 0.6", CylinderSurface{ Frame, 1.0 }, { 0.0, -1.0 }, { 2.0 * Pi, 1.0 }, -0.4,
              [&](Vector3 Point) { return std::fabs(Radial(Point) - 0.6); } },
            { "NURBS sphere R 2 offset +0.5 → R 2.5", SphereSurface{ Frame, 2.0 }, { 0.0, -1.2 }, { 2.0 * Pi, 1.2 }, 0.5,
              [&](Vector3 Point) { return std::fabs(Distance(Point, Frame.Origin) - 2.5); } },
            { "NURBS torus R 3 r 1 offset −0.3 → tube 0.7", TorusSurface{ Frame, 3.0, 1.0 }, { 0.0, 0.0 }, { 2.0 * Pi, 2.0 * Pi }, -0.3,
              [&](Vector3 Point) { const Vector3 L = Point - Frame.Origin; return std::fabs(std::hypot(std::hypot(L.X, L.Y) - 3.0, L.Z) - 0.7); } },
        };
        for (const Case& Each : Cases)
        {
            const std::optional<NurbsSurface> Basis = ConvertSurfaceToNurbs(Each.Analytic, Each.Low, Each.High);
            if (!Basis) { Report(CarrierGate, Each.Name, "conversion failed", 1.0, false); continue; }
            const std::optional<SurfaceGeometry> Offset = OffsetSurface(SurfaceGeometry{ *Basis }, Each.Distance);
            const auto* Parallel = Offset ? std::get_if<ParallelSurface>(&*Offset) : nullptr;
            if (Parallel == nullptr) { Report(CarrierGate, Each.Name, "no parallel carrier", 1.0, false); continue; }
            const NurbsDomain U = MeasureNurbsSurfaceDomain(*Basis->Definition, true), V = MeasureNurbsSurfaceDomain(*Basis->Definition, false);
            double PositionError = 0.0, DerivativeError = 0.0, NormalError = 0.0, CurvatureError = 0.0;
            for (int Row = 1; Row < 12; ++Row)
                for (int Column = 1; Column < 12; ++Column)
                {
                    const Vector2 At{ U.Low + (U.High - U.Low) * Column / 12.0, V.Low + (V.High - V.Low) * Row / 12.0 };
                    const SurfaceSample Sample = EvaluateSurface(*Offset, At);
                    const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(*Basis->Definition, At);
                    PositionError = std::max(PositionError, Each.Residual(Sample.Position));
                    // 📝 Principal directions of these carriers are the chart directions: O𝑢 = (1 − dκ₁)·S𝑢, O𝑣 = (1 − dκ₂)·S𝑣
                    //    (cylinder: κ = −1/r around, 0 along; sphere −1/R both; torus tube −1/r along 𝑣, ring along 𝑢).
                    std::array<double, 2> κ{};
                    (void)MeasureNurbsPrincipalCurvatures(*Basis->Definition, At, κ);
                    const Vector3 Normal = EvaluateNurbsNormal(*Basis, At);
                    // Which principal curvature belongs to S𝑢: the normal curvature of S𝑢 is L / E.
                    const double κU = Dot(S.UU, Normal) / Dot(S.U, S.U), κV = Dot(S.VV, Normal) / Dot(S.V, S.V);
                    DerivativeError = std::max({ DerivativeError, Length(Sample.DerivativeU - S.U * (1.0 - Each.Distance * κU)) / Length(S.U),
                                                 Length(Sample.DerivativeV - S.V * (1.0 - Each.Distance * κV)) / Length(S.V) });
                    NormalError = std::max(NormalError, Length(EvaluateSurfaceNormal(*Offset, At) - Normal));
                    const double Expected = std::max(std::fabs(κ[0] / (1.0 - Each.Distance * κ[0])), std::fabs(κ[1] / (1.0 - Each.Distance * κ[1])));
                    CurvatureError = std::max(CurvatureError, std::fabs(MeasureParallelCurvature(*Parallel, At) - Expected) / std::max(1.0, Expected));
                    if (std::holds_alternative<CylinderSurface>(Each.Analytic) || std::holds_alternative<SphereSurface>(Each.Analytic))
                    {
                        const double Analytic = 1.0 / (std::holds_alternative<CylinderSurface>(Each.Analytic) ? 1.0 + Each.Distance : 2.0 + Each.Distance);
                        CurvatureError = std::max(CurvatureError, std::fabs(MeasureParallelCurvature(*Parallel, At) - Analytic));
                    }
                }
            const double Worst = std::max({ PositionError, DerivativeError, NormalError, CurvatureError });
            Report(CarrierGate, Each.Name, "position " + Scientific(PositionError) + " · O𝑢 / O𝑣 " + Scientific(DerivativeError) + " · normal " + Scientific(NormalError) + " · curvature " + Scientific(CurvatureError) + " (121 points)",
                   Worst, Worst <= 1.0e-12);
        }
    }

    // ---- P2 freeform ------------------------------------------------------------------------------------------------
    void ValidateFreeform()
    {
        const NurbsSurface Patch = FreeformPatch();
        for (const double d : { 0.15, -0.2 })
        {
            const ParallelSurface Offset = ConstructParallelSurface(Patch, d);
            double DifferenceError = 0.0, AreaError = 0.0, NormalError = 0.0, InversionError = 0.0, DistanceError = 0.0;
            constexpr double h = 1.0e-5;                                                         // [-] - Central-difference step
            for (int Row = 1; Row < 10; ++Row)
                for (int Column = 1; Column < 10; ++Column)
                {
                    const Vector2 At{ 2.0 * (Column + 0.37) / 10.5, 2.0 * (Row + 0.37) / 10.5 };              // Off the knot at 1
                    const SurfaceSample Sample = EvaluateParallelSurface(Offset, At);
                    const Vector3 DifferenceU = (EvaluateParallelSurface(Offset, { At.X + h, At.Y }).Position - EvaluateParallelSurface(Offset, { At.X - h, At.Y }).Position) * (0.5 / h);
                    const Vector3 DifferenceV = (EvaluateParallelSurface(Offset, { At.X, At.Y + h }).Position - EvaluateParallelSurface(Offset, { At.X, At.Y - h }).Position) * (0.5 / h);
                    DifferenceError = std::max({ DifferenceError, Length(DifferenceU - Sample.DerivativeU) / Length(Sample.DerivativeU), Length(DifferenceV - Sample.DerivativeV) / Length(Sample.DerivativeV) });
                    const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(*Patch.Definition, At);
                    std::array<double, 2> κ{};
                    (void)MeasureNurbsPrincipalCurvatures(*Patch.Definition, At, κ);
                    const double Expected = (1.0 - d * κ[0]) * (1.0 - d * κ[1]) * Length(Cross(S.U, S.V));
                    AreaError = std::max(AreaError, std::fabs(Length(Cross(Sample.DerivativeU, Sample.DerivativeV)) - Expected) / Expected);
                    NormalError = std::max(NormalError, Length(Normalize(Cross(Sample.DerivativeU, Sample.DerivativeV)) - EvaluateParallelNormal(Offset, At)));
                    // A point t = 0.03 off the offset along N: the foot is At and the signed distance is t.
                    const Vector3 Off = Sample.Position + EvaluateParallelNormal(Offset, At) * 0.03;
                    const Vector2 Foot = ProjectOntoSurface(SurfaceGeometry{ Offset }, Off, { 1.0, 1.0 });
                    InversionError = std::max(InversionError, Length(Foot - At));
                    DistanceError = std::max(DistanceError, std::fabs(MeasureSurfaceDistance(SurfaceGeometry{ Offset }, Off).Distance - 0.03));
                }
            const double Exact = std::max({ AreaError, NormalError, InversionError, DistanceError });
            Report(FreeformGate, "bicubic patch offset " + Scientific(d), "O𝑢 / O𝑣 vs differences " + Scientific(DifferenceError) + " · |O𝑢 × O𝑣| " + Scientific(AreaError) + " · normal " + Scientific(NormalError)
                   + " · foot " + Scientific(InversionError) + " · signed distance " + Scientific(DistanceError) + " (81 points)", std::max(DifferenceError * 1.0e-3, Exact), DifferenceError <= 1.0e-7 && Exact <= 1.0e-10);
        }
    }

    // ---- P3 intersections ---------------------------------------------------------------------------------------------
    void ValidateSections()
    {
        const auto Box = [](const SurfaceGeometry& Surface) -> ParameterBox
        {
            const NurbsSurface* Chart = ResolveNurbsChart(Surface);
            if (Chart == nullptr) return { { -6.0, -6.0 }, { 6.0, 6.0 } };
            const NurbsDomain U = MeasureNurbsSurfaceDomain(*Chart->Definition, true), V = MeasureNurbsSurfaceDomain(*Chart->Definition, false);
            return { { U.Low, V.Low }, { U.High, V.High } };
        };
        const auto Validate = [&](const char* Name, const SurfaceGeometry& First, const SurfaceGeometry& Second, const std::function<double(Vector3)>& Residual, int Branches)
        {
            const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(First, Box(First), Second, Box(Second));
            // 📝 Interior points must sit on both exact carriers; an open branch's end on a patch boundary may be marched up
            //    to the SSI's foot gate (10 × precision) past it, as on any NURBS face.
            double Worst = 0.0, EndWorst = 0.0;
            long   Points = 0;
            for (const IntersectionBranch& Branch : Outcome.Branches)
                for (size_t Index = 0; Index < Branch.Chart.size(); ++Index)
                {
                    const bool End = !Branch.ClosedCondition && (Index == 0 || Index + 1 == Branch.Chart.size());
                    (End ? EndWorst : Worst) = std::max(End ? EndWorst : Worst, Residual(Branch.Chart[Index].Position));
                    ++Points;
                }
            const bool Passed = static_cast<int>(Outcome.Branches.size()) >= Branches && Points >= 12 && Worst <= 1.0e-9 && EndWorst <= 1.0e-7;
            Report(SectionGate, Name, std::to_string(Outcome.Branches.size()) + " branch(es), " + std::to_string(Points) + " chart points, worst interior residual " + Scientific(Worst) + " · open ends " + Scientific(EndWorst) + " · " + Outcome.Method + " · lattice " + std::to_string(Outcome.Statistics.LatticeCurves) + " / " + std::to_string(Outcome.Statistics.LatticeCrossings) + " · traced " + std::to_string(Outcome.Statistics.SeedsTraced) + " skipped " + std::to_string(Outcome.Statistics.SeedsSkipped), Worst, Passed);
        };

        // Offset NURBS sphere R 1 by +0.5 against the plane z = 0.3: the circle ρ = √(1.5² − 0.3²)
        const OrthonormalFrame Frame = Upright({});
        const NurbsSurface Sphere = *ConvertSurfaceToNurbs(SphereSurface{ Frame, 1.0 }, { 0.0, -1.5 }, { 2.0 * Pi, 1.5 });
        const SurfaceGeometry Swollen = *OffsetSurface(SurfaceGeometry{ Sphere }, 0.5);
        Validate("offset sphere (R 1 + 0.5) ∩ plane z = 0.3 → circle ρ = √2.16", Swollen, PlaneSurface{ Upright({ 0.0, 0.0, 0.3 }) },
                 [](Vector3 Point) { return std::max(std::fabs(std::hypot(Point.X, Point.Y) - std::sqrt(2.16)), std::fabs(Point.Z - 0.3)); }, 1);

        // Two crossing NURBS cylinders r 1 (axes z and x), each offset by +0.25: equal cylinders r 1.25 (Steinmetz)
        const NurbsSurface Upward  = *ConvertSurfaceToNurbs(CylinderSurface{ Frame, 1.0 }, { 0.0, -3.0 }, { 2.0 * Pi, 3.0 });
        const OrthonormalFrame Sideways{ {}, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
        const NurbsSurface Across  = *ConvertSurfaceToNurbs(CylinderSurface{ Sideways, 1.0 }, { 0.0, -3.0 }, { 2.0 * Pi, 3.0 });
        Validate("offset cylinder z ∩ offset cylinder x (r 1 + 0.25) → Steinmetz", *OffsetSurface(SurfaceGeometry{ Upward }, 0.25), *OffsetSurface(SurfaceGeometry{ Across }, 0.25),
                 [](Vector3 Point) { return std::max(std::fabs(std::hypot(Point.X, Point.Y) - 1.25), std::fabs(std::hypot(Point.Y, Point.Z) - 1.25)); }, 2);

        // Offset freeform patch by 0.15 against the plane x = 1.1
        const ParallelSurface Raised = ConstructParallelSurface(FreeformPatch(), 0.15);
        const OrthonormalFrame Wall{ { 1.1, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
        Validate("offset freeform (+0.15) ∩ plane x = 1.1", Raised, PlaneSurface{ Wall },
                 [&](Vector3 Point) { return std::max(std::fabs(Point.X - 1.1), OffsetResidual(Raised, Point, { 1.0, 1.0 })); }, 1);
    }

    // ---- P4 fold margin and composition --------------------------------------------------------------------------------
    void ValidateFolds()
    {
        const NurbsSurface Cylinder = *ConvertSurfaceToNurbs(CylinderSurface{ Upright({}), 1.0 }, { 0.0, -1.0 }, { 2.0 * Pi, 1.0 });
        const NurbsDomain U = MeasureNurbsSurfaceDomain(*Cylinder.Definition, true), V = MeasureNurbsSurfaceDomain(*Cylinder.Definition, false);
        for (const auto& [Inward, Expected] : { std::pair{ -0.5, 0.5 }, std::pair{ -1.2, -0.2 } })
        {
            const ParallelFold Fold = MeasureParallelFold(ConstructParallelSurface(Cylinder, Inward), { U.Low, V.Low }, { U.High, V.High });
            const double Error = std::fabs(Fold.Margin - Expected);
            Report(FoldGate, "unit NURBS cylinder offset " + Scientific(Inward), "margin " + Scientific(Fold.Margin) + " (closed form 1 + d/r = " + Scientific(Expected) + ")" + (Fold.Margin <= 0.0 ? " → folds" : " → regular"),
                   Error, Error <= 1.0e-12);
        }
        const SurfaceGeometry Once  = *OffsetSurface(SurfaceGeometry{ Cylinder }, 0.2);
        const SurfaceGeometry Twice = *OffsetSurface(Once, 0.3);
        const auto* Collapsed = std::get_if<ParallelSurface>(&Twice);
        const bool  Composed  = Collapsed && Collapsed->Definition->Basis.Definition == Cylinder.Definition && std::fabs(Collapsed->Definition->Distance - 0.5) <= 1.0e-15;
        Report(FoldGate, "offset 0.2 then 0.3", Composed ? "one parallel carrier on the original basis, d = 0.5" : "not collapsed", 0.0, Composed);
        const SurfaceGeometry Back = *OffsetSurface(Twice, -0.5);
        const auto* Basis = std::get_if<NurbsSurface>(&Back);
        Report(FoldGate, "offset back by −0.5", Basis && Basis->Definition == Cylinder.Definition ? "the basis itself" : "not the basis", 0.0, Basis && Basis->Definition == Cylinder.Definition);
    }
}

namespace
{
    // ---- F blends on NURBS supports ------------------------------------------------------------------------------------

    [[nodiscard]] EdgeToken NearestEdge(const ModelPartition& Partition, BodyToken Body, Vector3 Probe)
    {
        EdgeToken Best    = {};
        double    Nearest = std::numeric_limits<double>::max();                                  // [len] - Closest sample so far
        for (const EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
            const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
            for (int Sample = 0; Sample <= 32; ++Sample)
            {
                const double Gap = Distance(EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / 32.0).Position, Probe);
                if (Gap < Nearest) { Nearest = Gap; Best = Edge; }
            }
        }
        return Best;
    }

    // 📝 Independent of the blend's own residuals: every section τ comes from projecting a point of the ORIGINAL edge
    //    onto the spine; the ball is tested against the exact supports by inversion, the feet against the support normal,
    //    and G1 as the angle between the carrier normal and the support normal at each foot.
    struct SheetMeasure
    {
        double Ball     = 0.0;                                                                   // [len] - ||ball − support| − r|
        double Foot     = 0.0;                                                                   // [len] - Foot off support / off normal / off carrier
        double Tangency = 0.0;                                                                   // [rad] - Blend ↔ support normal angle
        int    Sections = 0;                                                                     // [-]   - Sections sampled
    };

    [[nodiscard]] SheetMeasure MeasureSheet(const ModelPartition& Partition, const EdgeBlendSheet& Sheet, bool TangencyEnabled = true)
    {
        SheetMeasure         Measure;
        const EdgeProfile&   Edge  = Partition.Edges.Resolve(Sheet.Edge);
        const CurveGeometry& Curve = Partition.Curves.Resolve(Edge.Curve);
        for (int Sample = 1; Sample < 40; ++Sample)
        {
            const Vector3 OnEdge = EvaluateCurve(Curve, Edge.IntervalLow + (Edge.IntervalHigh - Edge.IntervalLow) * Sample / 40.0).Position;
            const double  τ      = ProjectOntoCurve(Sheet.Spine, OnEdge, 0.0);
            const Vector3 Ball   = EvaluateCurve(Sheet.Spine, τ).Position;                       // [len] - Ball centre
            for (int Side = 0; Side < 2; ++Side)
            {
                const SurfaceGeometry& Support = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Sheet.Faces[Side]).Surface);
                const Vector3          Foot    = EvaluateCurve(Sheet.Contacts[Side], τ).Position;
                const SurfaceDistance  Reach   = MeasureSurfaceDistance(Support, Ball);
                const SurfaceDistance  OnBase  = MeasureSurfaceDistance(Support, Foot);
                const SurfaceDistance  OnBlend = MeasureSurfaceDistance(Sheet.Surface, Foot);
                const auto*            Law     = std::get_if<IntersectionCurve>(&Sheet.Spine);
                const double           Radius  = Law && Law->Trace->Law ? MeasureBallLawRadius(*Law->Trace, τ) : Sheet.Radius;   // [len] - r(τ) on a ball-law spine
                Measure.Ball     = std::max(Measure.Ball, std::fabs(std::fabs(Reach.Distance) - Radius));
                Measure.Foot     = std::max({ Measure.Foot, std::fabs(OnBase.Distance), Length(Cross(Ball - Foot, OnBase.Normal)), std::fabs(OnBlend.Distance) });
                if (TangencyEnabled) Measure.Tangency = std::max(Measure.Tangency, std::asin(std::min(1.0, Length(Cross(OnBlend.Normal, OnBase.Normal)))));
            }
            ++Measure.Sections;
        }
        return Measure;
    }

    // 📝 D-profile: the base line (0, 0) → (2, 0), then a convex clamped cubic back to (0, 0) leaving and arriving
    //    perpendicular to the line (first and last legs vertical), so the plane y = 0 is a normal plane of the rim at both
    //    ends and the fillet's end caps are exact cross-sections. Turning of the rim Θ = π.
    [[nodiscard]] SketchProfile DProfile()
    {
        SketchProfile  Profile;
        ProfileLoop    Loop;
        ProfileSegment Base;
        Base.Kind  = ProfileSegmentKind::Line;
        Base.Start = { 0.0, 0.0 };
        Base.End   = { 2.0, 0.0 };
        Base.Curve = 0;
        Base.Name  = "Line1";
        ProfileSegment Arch;
        Arch.Kind         = ProfileSegmentKind::Spline;
        Arch.Start        = { 2.0, 0.0 };
        Arch.End          = { 0.0, 0.0 };
        Arch.Curve        = 1;
        Arch.Name         = "Spline2";
        Arch.Spline.Degree = 3;
        Arch.Spline.Knots  = { 0.0, 0.0, 0.0, 0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0, 1.0, 1.0, 1.0 };
        Arch.Spline.Poles  = { { 2.0, 0.0 }, { 2.0, 0.8 }, { 1.7, 1.7 }, { 0.3, 1.7 }, { 0.0, 0.8 }, { 0.0, 0.0 } };
        Loop.Segments = { Base, Arch };
        Profile.Loops = { Loop };
        Profile.Scale = 2.0;
        ArrangeProfileLoops(Profile);
        return Profile;
    }

    // Arc length of a profile segment (composite Simpson on |C′|, 4096 panels per knot span: error ≪ 10⁻¹³).
    [[nodiscard]] double SegmentLength(const ProfileSegment& Segment)
    {
        const std::vector<double> Breaks = CollectProfileBreaks(Segment);
        double Total = 0.0;
        for (size_t Index = 0; Index + 1 < Breaks.size(); ++Index)
        {
            const int    Panels = 4096;
            const double Step   = (Breaks[Index + 1] - Breaks[Index]) / Panels;
            for (int Panel = 0; Panel < Panels; ++Panel)
            {
                const double Low = Breaks[Index] + Step * Panel;
                const auto   Speed = [&](double Fraction) { const Vector2 T = EvaluateProfileTangent(Segment, Fraction); return std::hypot(T.X, T.Y); };
                Total += Step / 6.0 * (Speed(Low) + 4.0 * Speed(Low + 0.5 * Step) + Speed(Low + Step));
            }
        }
        return Total;
    }

    void ValidateBlends(const std::string& Only)
    {
        const double Corner   = 1.0 - Pi / 4.0;                                                  // [-]   - Spandrel area / r²
        const double Centroid = (10.0 - 3.0 * Pi) / (12.0 - 3.0 * Pi);                           // [-]   - Spandrel centroid / r (from the corner)
        struct Specimen
        {
            const char*                                           Key;
            const char*                                           Name;
            std::function<BodyToken(ModelPartition&)>             Build;
            Vector3                                               Probe;
            double                                                Radius;
            std::function<double()>                               Expected;
            bool                                                  AnalyticTwin;
        };
        const SketchProfile Arch       = DProfile();
        const double        ArchLength = SegmentLength(Arch.Loops[0].Segments[1].Kind == ProfileSegmentKind::Spline ? Arch.Loops[0].Segments[1] : Arch.Loops[0].Segments[0]);
        const double        Height     = 1.0;                                                    // [len] - D-extrusion height
        const std::vector<Specimen> Specimens =
        {
            { "block", "Promoted block 2 × 1.5 × 1 · fillet r 0.2 on the top front edge (NURBS plane ∩ NURBS plane)",
              [](ModelPartition& Partition) { return ConstructBlock(Partition, { 0, 0, 0 }, { 2, 1.5, 1 }).Value; }, { 1.0, 0.0, 1.0 }, 0.2,
              [&] { return 3.0 - Corner * 0.04 * 2.0; }, true },
            { "rim", "Promoted cylinder r 1 × 1.5 · fillet r 0.2 on the top rim (NURBS cylinder ∩ NURBS plane, closed)",
              [](ModelPartition& Partition) { return ConstructCylinder(Partition, Upright({ 0, 0, 0 }), 1.0, 1.5).Value; }, { 1.0, 0.0, 1.5 }, 0.2,
              [&] { return Pi * 1.5 - 2.0 * Pi * (1.0 - Centroid * 0.2) * Corner * 0.04; }, true },
            { "arch", "Freeform D-extrusion · fillet r 0.25 on the spline rim (bicubic-ruled NURBS ∩ plane, curved Pappus)",
              [&](ModelPartition& Partition) { return ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, Height).Body; },
              { 1.0, 1.7 * 0.75 + 0.0, 1.0 }, 0.25,
              [&] { return Arch.Area * Height - Corner * 0.0625 * (ArchLength - Centroid * 0.25 * Pi); }, false },
        };
        for (const Specimen& Each : Specimens)
        {
            if (!Only.empty() && Only != "blend" && Only != Each.Key) continue;
            ModelPartition Partition;
            const BodyToken Source = Each.Build(Partition);
            const TransformOutcome Promoted = Each.AnalyticTwin ? PromoteBodyToNurbs(Partition, Source) : TransformOutcome{ Source, {} };
            if (!Promoted.Refusal.empty()) { Report(VolumeGate, Each.Name, "promotion refused: " + Promoted.Refusal, 1.0, false); continue; }
            BlendSettings Settings;
            Settings.Profile = BlendProfile::Fillet;
            Settings.Size    = Each.Radius;
            const auto         Moment  = std::chrono::steady_clock::now();
            const EdgeToken    Edge    = NearestEdge(Partition, Promoted.Body, Each.Probe);
            const BlendOutcome Blended = BlendEdges(Partition, Promoted.Body, { Edge }, Settings);
            const double       Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Moment).count();
            if (!Blended.Refusal.empty()) { Report(VolumeGate, Each.Name, "blend refused: " + Blended.Refusal, 1.0, false); continue; }
            const double Volume   = IntegrateBodyVolume(Partition, Blended.Body);
            const double Expected = Each.Expected();
            const double Error    = std::fabs(Volume - Expected) / Expected;
            std::string  Twin;
            double       TwinError = 0.0;
            if (Each.AnalyticTwin)
            {
                const BlendOutcome Analytic = BlendEdges(Partition, Source, { NearestEdge(Partition, Source, Each.Probe) }, Settings);
                TwinError = Analytic.Refusal.empty() ? std::fabs(IntegrateBodyVolume(Partition, Analytic.Body) - Volume) / Expected : 1.0;
                Twin = " · analytic twin " + Scientific(TwinError);
            }
            const BlendStatistics& Statistics = Blended.Statistics;
            Report(VolumeGate, Each.Name, "V " + std::to_string(Volume) + " vs closed form " + std::to_string(Expected) + " → " + Scientific(Error) + Twin + " · procedural carriers " + std::to_string(Statistics.CarrierProcedural)
                   + " · traced sections " + std::to_string(Statistics.TracedSections) + " · audit " + (Blended.Audit.SoundCondition() ? "sound" : "UNSOUND") + " · " + std::to_string(static_cast<int>(Seconds * 1000.0)) + " ms",
                   std::max(Error, TwinError), Error <= 1.0e-9 && TwinError <= 1.0e-9 && Blended.Audit.SoundCondition());
            for (const EdgeBlendSheet& Sheet : Blended.Sheets)
            {
                const SheetMeasure Measure = MeasureSheet(Partition, Sheet);
                Report(LawGate, Each.Name, "ball " + Scientific(Measure.Ball) + " · feet " + Scientific(Measure.Foot) + " · G1 " + Scientific(Measure.Tangency) + " rad · sheet residual " + Scientific(Sheet.MaxResidual) + " (" + std::to_string(Measure.Sections) + " sections × 2 supports)",
                       std::max({ Measure.Ball, Measure.Foot, Measure.Tangency }), Measure.Ball <= 1.0e-9 && Measure.Foot <= 1.0e-9 && Measure.Tangency <= 1.0e-9);
            }
        }

        // ---- F3 refusals: atomic, the input body untouched.
        struct Refusal { const char* Name; double Size; BlendProfile Profile; std::optional<double> EndSize; const char* Expected; };
        const std::vector<Refusal> Refusals =
        {
            { "D-extrusion rim, r 0.9 (past the arch's curvature radius)", 0.9, BlendProfile::Fillet, std::nullopt, "fold margin" },
            { "D-extrusion rim, chamfer 0.9 (setback past the arch's curvature radius)", 0.9, BlendProfile::Chamfer, std::nullopt, "fold margin" },
            { "D-extrusion rim, variable fillet 0.15 → 0.9 (past the curvature radius at the large end)", 0.15, BlendProfile::Fillet, 0.9, "fold margin" },
        };
        for (const Refusal& Each : Refusals)
        {
            if (!Only.empty() && Only != "blend" && Only != "refusal") continue;
            ModelPartition  Partition;
            const BodyToken Body   = ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, Height).Body;
            const double    Before = IntegrateBodyVolume(Partition, Body);
            BlendSettings   Settings;
            Settings.Profile = Each.Profile;
            Settings.Size    = Each.Size;
            Settings.EndSize = Each.EndSize;
            const BlendOutcome Blended = BlendEdges(Partition, Body, { NearestEdge(Partition, Body, { 1.0, 1.275, 1.0 }) }, Settings);
            const bool Passed = !Blended.Refusal.empty() && Blended.Refusal.find(Each.Expected) != std::string::npos && IntegrateBodyVolume(Partition, Body) == Before;
            Report(RefusalGate, Each.Name, Blended.Refusal.empty() ? "accepted (expected a refusal)" : "\"" + Blended.Refusal + "\"", Passed ? 0.0 : 1.0, Passed);
        }
    }
}

namespace
{
    // ---- C chamfers and variable blends on NURBS supports (N5) --------------------------------------------------------

    // 📝 Variable chamfer on the D-rim, exact: the rim wedge is 90° everywhere and both supports' normals lie in the rim's
    //    normal plane, so every section of the chamfer in a normal plane is the right triangle with legs d(t) and centroid
    //    d/3 in from the edge. Tube coordinates along a planar edge give dV = (1 − κ·x) dA ds, hence
    //    V_removed = ∫ d²/2 · (|E′| − d/3 · |E′ × E″| / |E′|²) dt (the arch is convex), composite Simpson per knot span.
    [[nodiscard]] double VariableChamferLoss(const NurbsCurveDefinition& Rim, double Low, double High, double StartSize, double EndSize, double From)
    {
        const double To = From == Low ? High : Low;                                          // [-] - t at the end vertex
        std::vector<double> Breaks = { Low };
        for (double Knot : CollectNurbsCurveKnots(Rim, Low, High)) Breaks.push_back(Knot);
        Breaks.push_back(High);
        const auto Integrand = [&](double t)
        {
            const NurbsCurveDerivatives Sample  = EvaluateNurbsCurveDerivatives(Rim, t);
            const double                Setback = StartSize + (EndSize - StartSize) * (t - From) / (To - From);   // [len] - d(t)
            const double                Speed   = Length(Sample.First);
            return 0.5 * Setback * Setback * (Speed - Setback / 3.0 * Length(Cross(Sample.First, Sample.Second)) / (Speed * Speed));
        };
        double Loss = 0.0;
        for (size_t Index = 0; Index + 1 < Breaks.size(); ++Index)
        {
            const int    Panels = 4096;
            const double Step   = (Breaks[Index + 1] - Breaks[Index]) / Panels;
            for (int Panel = 0; Panel < Panels; ++Panel)
            {
                const double Start = Breaks[Index] + Step * Panel;
                Loss += Step / 6.0 * (Integrand(Start) + 4.0 * Integrand(Start + 0.5 * Step) + Integrand(Start + Step));
            }
        }
        return Loss;
    }

    void ValidateNurbsBlends(const std::string& Only)
    {
        const SketchProfile   Arch       = DProfile();
        const ProfileSegment& Spline     = Arch.Loops[0].Segments[0].Kind == ProfileSegmentKind::Spline ? Arch.Loops[0].Segments[0] : Arch.Loops[0].Segments[1];
        const double          ArchLength = SegmentLength(Spline);
        const double          Height     = 1.0;                                                  // [len] - D-extrusion height
        const double          Corner     = 1.0 - Pi / 4.0;                                       // [-]   - Spandrel area / r²
        const double          Centroid   = (10.0 - 3.0 * Pi) / (12.0 - 3.0 * Pi);                // [-]   - Spandrel centroid / r
        const Vector3         RimProbe   = { 1.0, 1.275, 1.0 };                                  // [len] - On the spline rim
        const auto LawReport = [&](GateTally& Gate, const std::string& Name, const ModelPartition& Partition, const BlendOutcome& Blended, bool Fillet)
        {
            for (const EdgeBlendSheet& Sheet : Blended.Sheets)
            {
                const SheetMeasure Measure = MeasureSheet(Partition, Sheet, Fillet);
                Report(Gate, Name + " · ball law", "ball " + Scientific(Measure.Ball) + " · feet " + Scientific(Measure.Foot) + (Fillet ? " · G1 " + Scientific(Measure.Tangency) + " rad" : std::string()) + " · sheet residual " + Scientific(Sheet.MaxResidual)
                       + " (" + std::to_string(Measure.Sections) + " sections × 2 supports)", std::max({ Measure.Ball, Measure.Foot, Measure.Tangency }), Measure.Ball <= 1.0e-9 && Measure.Foot <= 1.0e-9 && Measure.Tangency <= 1.0e-9);
            }
        };

        // ---- C1 chamfers: closed forms, analytic twins for the promoted bodies, and a tessellation of every result (the
        //      0.5 chamfer's spine bends tighter than its ball, the case that once sent the edge sampler to its depth limit).
        struct Specimen
        {
            std::string                                Key;
            std::string                                Name;
            std::function<BodyToken(ModelPartition&)>  Build;
            Vector3                                    Probe;
            bool                                       Promote;
            double                                     Setback;
            std::function<double(double)>              Expected;
        };
        const auto RimChamfer = [&](double Setback) { return Arch.Area * Height - 0.5 * Setback * Setback * (ArchLength - Setback / 3.0 * Pi); };
        const std::vector<Specimen> Chamfers =
        {
            { "chamfer", "Freeform D-extrusion · chamfer 0.2 on the spline rim (bicubic-ruled NURBS ∩ plane, curved Pappus)",
              [&](ModelPartition& Partition) { return ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, Height).Body; }, RimProbe, false, 0.2, RimChamfer },
            { "chamfer", "Freeform D-extrusion · chamfer 0.5 on the spline rim (spine radius below the ball radius)",
              [&](ModelPartition& Partition) { return ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, Height).Body; }, RimProbe, false, 0.5, RimChamfer },
            { "chamfer", "Promoted block 2 × 1.5 × 1 · chamfer 0.2 on the top front edge (NURBS plane ∩ NURBS plane)",
              [](ModelPartition& Partition) { return ConstructBlock(Partition, { 0, 0, 0 }, { 2, 1.5, 1 }).Value; }, { 1.0, 0.0, 1.0 }, true, 0.2,
              [](double Setback) { return 3.0 - 0.5 * Setback * Setback * 2.0; } },
            { "chamfer", "Promoted cylinder r 1 × 1.5 · chamfer 0.2 on the top rim (NURBS cylinder ∩ NURBS plane, closed)",
              [](ModelPartition& Partition) { return ConstructCylinder(Partition, Upright({ 0, 0, 0 }), 1.0, 1.5).Value; }, { 1.0, 0.0, 1.5 }, true, 0.2,
              [](double Setback) { return Pi * 1.5 - 2.0 * Pi * (1.0 - Setback / 3.0) * 0.5 * Setback * Setback; } },
        };
        for (const Specimen& Each : Chamfers)
        {
            if (!Only.empty() && Only != "nurbsblend" && Only != Each.Key) continue;
            ModelPartition Partition;
            const BodyToken        Source   = Each.Build(Partition);
            const TransformOutcome Promoted = Each.Promote ? PromoteBodyToNurbs(Partition, Source) : TransformOutcome{ Source, {} };
            if (!Promoted.Refusal.empty()) { Report(ChamferGate, Each.Name, "promotion refused: " + Promoted.Refusal, 1.0, false); continue; }
            BlendSettings Settings;
            Settings.Profile = BlendProfile::Chamfer;
            Settings.Size    = Each.Setback;
            const BlendOutcome Blended = BlendEdges(Partition, Promoted.Body, { NearestEdge(Partition, Promoted.Body, Each.Probe) }, Settings);
            if (!Blended.Refusal.empty()) { Report(ChamferGate, Each.Name, "chamfer refused: " + Blended.Refusal, 1.0, false); continue; }
            const double Volume   = IntegrateBodyVolume(Partition, Blended.Body);
            const double Expected = Each.Expected(Each.Setback);
            const double Error    = std::fabs(Volume - Expected) / Expected;
            std::string  Twin;
            double       TwinError = 0.0;
            if (Each.Promote)
            {
                const BlendOutcome Analytic = BlendEdges(Partition, Source, { NearestEdge(Partition, Source, Each.Probe) }, Settings);
                TwinError = Analytic.Refusal.empty() ? std::fabs(IntegrateBodyVolume(Partition, Analytic.Body) - Volume) / Expected : 1.0;
                Twin = " · analytic twin " + Scientific(TwinError);
            }
            const auto                Moment = std::chrono::steady_clock::now();
            const TessellationOutcome Mesh   = TessellateBody(Partition, Blended.Body, TessellationSettings{});
            const int                 Meshed = static_cast<int>(std::chrono::duration<double>(std::chrono::steady_clock::now() - Moment).count() * 1000.0);
            Report(ChamferGate, Each.Name, "V " + std::to_string(Volume) + " vs closed form " + std::to_string(Expected) + " → " + Scientific(Error) + Twin + " · procedural carriers " + std::to_string(Blended.Statistics.CarrierProcedural)
                   + " · audit " + (Blended.Audit.SoundCondition() ? "sound" : "UNSOUND") + " · tessellation " + (Mesh.Refusal.empty() ? std::to_string(Meshed) + " ms" : "refused: " + Mesh.Refusal) + " · " + std::to_string(static_cast<int>(Blended.Statistics.Milliseconds)) + " ms",
                   std::max(Error, TwinError), Error <= 1.0e-9 && TwinError <= 1.0e-9 && Blended.Audit.SoundCondition() && Mesh.Refusal.empty());
            LawReport(ChamferGate, Each.Name, Partition, Blended, false);
        }

        if (!Only.empty() && Only != "nurbsblend" && Only != "variable") return;
        // ---- C2 variable chamfer on the spline rim: exact normal-plane integral.
        {
            const std::string Name = "Freeform D-extrusion · variable chamfer 0.15 → 0.3 on the spline rim";
            ModelPartition  Partition;
            const BodyToken Body = ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, Height).Body;
            const EdgeToken Rim  = NearestEdge(Partition, Body, RimProbe);
            BlendSettings Settings;
            Settings.Profile = BlendProfile::Chamfer;
            Settings.Size    = 0.15;
            Settings.EndSize = 0.3;
            const BlendOutcome Blended = BlendEdges(Partition, Body, { Rim }, Settings);
            const EdgeProfile& Edge    = Partition.Edges.Resolve(Rim);
            const auto*        Curve   = std::get_if<NurbsCurve>(&Partition.Curves.Resolve(Edge.Curve));
            if (!Blended.Refusal.empty() || !Curve) Report(VariableGate, Name, Curve ? "refused: " + Blended.Refusal : std::string("rim is not a NURBS curve"), 1.0, false);
            else
            {
                const Vector3 Start    = Partition.Vertices.Resolve(Edge.Start).Position;
                const double  From     = Distance(EvaluateCurve(*Curve, Edge.IntervalLow).Position, Start) <= Distance(EvaluateCurve(*Curve, Edge.IntervalHigh).Position, Start) ? Edge.IntervalLow : Edge.IntervalHigh;
                const double  Expected = Arch.Area * Height - VariableChamferLoss(*Curve->Definition, Edge.IntervalLow, Edge.IntervalHigh, Settings.Size, *Settings.EndSize, From);
                const double  Volume   = IntegrateBodyVolume(Partition, Blended.Body);
                const double  Error    = std::fabs(Volume - Expected) / Expected;
                Report(VariableGate, Name, "V " + std::to_string(Volume) + " vs normal-plane integral " + std::to_string(Expected) + " → " + Scientific(Error) + " · audit " + (Blended.Audit.SoundCondition() ? "sound" : "UNSOUND")
                       + " · " + std::to_string(static_cast<int>(Blended.Statistics.Milliseconds)) + " ms", Error, Error <= 1.0e-9 && Blended.Audit.SoundCondition());
                LawReport(VariableGate, Name, Partition, Blended, false);
            }
        }
        // ---- C2 variable fillet on a promoted block edge: equal to the analytic-body cone fillet (closed-form validated
        //      in BlendValidation).
        {
            const std::string Name = "Promoted block 2 × 1.5 × 1 · variable fillet 0.15 → 0.3 on the top front edge";
            ModelPartition         Partition;
            const BodyToken        Source   = ConstructBlock(Partition, { 0, 0, 0 }, { 2, 1.5, 1 }).Value;
            const TransformOutcome Promoted = PromoteBodyToNurbs(Partition, Source);
            BlendSettings Settings;
            Settings.Profile = BlendProfile::Fillet;
            Settings.Size    = 0.15;
            Settings.EndSize = 0.3;
            const Vector3 Probe = { 1.0, 0.0, 1.0 };
            // Size anchored at the x = 0 vertex on both bodies, so the law runs the same way whatever the edge senses.
            const auto Anchor = [&](EdgeToken Edge)
            {
                const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
                return Partition.Vertices.Resolve(Profile.Start).Position.X < 0.5 ? Profile.Start : Profile.End;
            };
            const EdgeToken Edge     = NearestEdge(Partition, Promoted.Body, Probe);
            const EdgeToken Twin     = NearestEdge(Partition, Source, Probe);
            BlendSettings   Mirrored = Settings;
            Settings.SizeAnchors = { Anchor(Edge) };
            Mirrored.SizeAnchors = { Anchor(Twin) };
            const BlendOutcome Blended  = Promoted.Refusal.empty() ? BlendEdges(Partition, Promoted.Body, { Edge }, Settings) : BlendOutcome{};
            const BlendOutcome Analytic = BlendEdges(Partition, Source, { Twin }, Mirrored);
            if (!Promoted.Refusal.empty() || !Blended.Refusal.empty() || !Analytic.Refusal.empty())
                Report(VariableGate, Name, "refused: " + Promoted.Refusal + Blended.Refusal + (Analytic.Refusal.empty() ? "" : " / analytic: " + Analytic.Refusal), 1.0, false);
            else
            {
                const double Volume    = IntegrateBodyVolume(Partition, Blended.Body);
                const double Reference = IntegrateBodyVolume(Partition, Analytic.Body);
                const double Error     = std::fabs(Volume - Reference) / Reference;
                Report(VariableGate, Name, "V " + std::to_string(Volume) + " vs analytic cone fillet " + std::to_string(Reference) + " → " + Scientific(Error) + " · procedural carriers " + std::to_string(Blended.Statistics.CarrierProcedural)
                       + " · audit " + (Blended.Audit.SoundCondition() ? "sound" : "UNSOUND") + " · " + std::to_string(static_cast<int>(Blended.Statistics.Milliseconds)) + " ms", Error, Error <= 1.0e-9 && Blended.Audit.SoundCondition());
                LawReport(VariableGate, Name, Partition, Blended, true);
            }
        }
        // ---- C2 variable fillet on the spline rim: the canal's circles lean out of the normal planes, so no closed form —
        //      gated by the exact ball law (balls touching both supports, feet on their normals, G1) and by the strict
        //      bracket between the constant fillets of its end sizes (curved Pappus, as F1).
        {
            const std::string Name = "Freeform D-extrusion · variable fillet 0.15 → 0.3 on the spline rim";
            ModelPartition  Partition;
            const BodyToken Body = ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, Height).Body;
            BlendSettings Settings;
            Settings.Profile = BlendProfile::Fillet;
            Settings.Size    = 0.15;
            Settings.EndSize = 0.3;
            const BlendOutcome Blended = BlendEdges(Partition, Body, { NearestEdge(Partition, Body, RimProbe) }, Settings);
            if (!Blended.Refusal.empty()) Report(VariableGate, Name, "refused: " + Blended.Refusal, 1.0, false);
            else
            {
                const auto   Constant = [&](double Radius) { return Arch.Area * Height - Corner * Radius * Radius * (ArchLength - Centroid * Radius * Pi); };
                const double Volume   = IntegrateBodyVolume(Partition, Blended.Body);
                const double Upper    = Constant(0.15), Lower = Constant(0.3);
                const bool   Inside   = Volume < Upper && Volume > Lower;
                Report(VariableGate, Name, "V " + std::to_string(Volume) + " strictly inside (" + std::to_string(Lower) + ", " + std::to_string(Upper) + ") of the constant r 0.3 / 0.15 fillets · audit " + (Blended.Audit.SoundCondition() ? "sound" : "UNSOUND")
                       + " · " + std::to_string(static_cast<int>(Blended.Statistics.Milliseconds)) + " ms", Inside ? 0.0 : 1.0, Inside && Blended.Audit.SoundCondition());
                LawReport(VariableGate, Name, Partition, Blended, true);
            }
        }
    }
}

namespace
{
    // ---- O offsets and hollows of NURBS walls (N4) --------------------------------------------------------------------

    [[nodiscard]] FaceOperationOutcome OffsetSplineWall(ModelPartition& Partition, BodyToken Body, double Distance)
    {
        FaceOperationSettings Settings;
        Settings.Kind     = FaceOperationKind::Offset;
        Settings.Distance = Distance;
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
            if (std::holds_alternative<NurbsSurface>(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface))) Settings.Faces.push_back(Face);
        return ApplyFaceOperation(Partition, Body, Settings);
    }

    // 📝 Area inside the inward offset of the D-profile's arch by t, above the offset base line y = t (Green's theorem:
    //    A = ∫ x dy along the offset arch between its two crossings of y = t; the closing segment on y = t has dy = 0).
    //    O = C + t·N with N the unit left normal (inward for the counter-clockwise D), O′ = C′ + t·J·(C″ − T(T·C″))/|C′|.
    [[nodiscard]] double InwardOffsetArea(const ProfileSegment& Arch, double Thickness)
    {
        const auto Offset = [&](double τ, Vector2& Derivative)
        {
            const SketchSplineDerivatives Sample = EvaluateSketchSpline(Arch.Spline, τ);
            const double  Speed   = std::hypot(Sample.First.X, Sample.First.Y);
            const Vector2 Tangent = { Sample.First.X / Speed, Sample.First.Y / Speed };
            const double  Along   = Tangent.X * Sample.Second.X + Tangent.Y * Sample.Second.Y;
            const Vector2 Turn    = { (Sample.Second.X - Tangent.X * Along) / Speed, (Sample.Second.Y - Tangent.Y * Along) / Speed };
            Derivative = { Sample.First.X - Thickness * Turn.Y, Sample.First.Y + Thickness * Turn.X };
            return Vector2{ Sample.Position.X - Thickness * Tangent.Y, Sample.Position.Y + Thickness * Tangent.X };
        };
        const auto Height = [&](double τ) { Vector2 Unused; return Offset(τ, Unused).Y - Thickness; };
        const auto Crossing = [&](double Low, double High)
        {
            for (int Step = 0; Step < 200; ++Step)
            {
                const double Middle = 0.5 * (Low + High);
                if ((Height(Low) < 0.0) == (Height(Middle) < 0.0)) Low = Middle; else High = Middle;
            }
            return 0.5 * (Low + High);
        };
        const double First = Crossing(0.0, 0.2), Last = Crossing(0.8, 1.0);
        std::vector<double> Breaks = { First };
        for (double Knot : { 1.0 / 3.0, 2.0 / 3.0 }) if (Knot > First && Knot < Last) Breaks.push_back(Knot);
        Breaks.push_back(Last);
        double Area = 0.0;
        for (size_t Index = 0; Index + 1 < Breaks.size(); ++Index)
        {
            const int    Panels = 8192;
            const double Step   = (Breaks[Index + 1] - Breaks[Index]) / Panels;
            const auto   Integrand = [&](double τ) { Vector2 Derivative; const Vector2 Point = Offset(τ, Derivative); return Point.X * Derivative.Y; };
            for (int Panel = 0; Panel < Panels; ++Panel)
            {
                const double Low = Breaks[Index] + Step * Panel;
                Area += Step / 6.0 * (Integrand(Low) + 4.0 * Integrand(Low + 0.5 * Step) + Integrand(Low + Step));
            }
        }
        return Area;
    }

    void ValidateOffsets()
    {
        const SketchProfile   Profile = DProfile();
        const ProfileSegment& Arch    = Profile.Loops[0].Segments[0].Kind == ProfileSegmentKind::Spline ? Profile.Loops[0].Segments[0] : Profile.Loops[0].Segments[1];
        const double          Length  = SegmentLength(Arch);
        const double          Height  = 1.0;                                                     // [len] - Extrusion height
        const auto Meeting = [&](const std::string& Name, ModelPartition& Partition, const FaceOperationOutcome& Outcome)
        {
            int Parallel = 0;
            for (FaceToken Face : Partition.CollectInventory(Outcome.Body).Faces) Parallel += std::holds_alternative<ParallelSurface>(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface)) ? 1 : 0;
            const bool Passed = Outcome.Audit.SoundCondition() && Outcome.Statistics.MaxEdgeResidual <= 3.0e-9 && Outcome.Statistics.TracedEdges > 0 && Parallel > 0;
            Report(MeetingGate, Name, std::to_string(Outcome.Statistics.TracedEdges) + " traced edge(s) · edge residual " + Scientific(Outcome.Statistics.MaxEdgeResidual) + " · vertex residual " + Scientific(Outcome.Statistics.MaxVertexResidual)
                   + " · " + std::to_string(Parallel) + " parallel face(s) · audit " + (Outcome.Audit.SoundCondition() ? "sound" : "UNSOUND") + " · " + std::to_string(static_cast<int>(Outcome.Milliseconds)) + " ms",
                   Outcome.Statistics.MaxEdgeResidual, Passed);
        };
        for (const double Distance : { 0.1, -0.1 })
        {
            ModelPartition  Partition;
            const BodyToken Body   = ExtrudeProfile(Partition, Profile, OrthonormalFrame{}, Height).Body;
            const std::string Name = std::string("D-extrusion · spline wall offset ") + (Distance > 0.0 ? "+" : "−") + "0.1";
            const FaceOperationOutcome Outcome = OffsetSplineWall(Partition, Body, Distance);
            if (!Outcome.Refusal.empty()) { Report(OffsetGate, Name, "refused: " + Outcome.Refusal, 1.0, false); continue; }
            const double Expected = (Profile.Area + Distance * Length + Pi * Distance * Distance / 2.0) * Height;
            const double Volume   = IntegrateBodyVolume(Partition, Outcome.Body);
            const double Error    = std::fabs(Volume - Expected) / Expected;
            Report(OffsetGate, Name, "V " + std::to_string(Volume) + " vs closed form " + std::to_string(Expected) + " (arch length " + std::to_string(Length) + ") → " + Scientific(Error), Error, Error <= 1.0e-9);
            Meeting(Name, Partition, Outcome);
        }
        {
            const double    Thickness = 0.1;                                                     // [len] - Wall
            ModelPartition  Partition;
            const BodyToken Body = ExtrudeProfile(Partition, Profile, OrthonormalFrame{}, Height).Body;
            FaceOperationSettings Settings;
            Settings.Kind     = FaceOperationKind::Hollow;
            Settings.Distance = Thickness;
            for (FaceToken Face : Partition.CollectInventory(Body).Faces)
                if (const auto* Plane = std::get_if<PlaneSurface>(&Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface)))
                    if (std::fabs(Plane->Frame.ZAxis.Z) > 0.9 && Plane->Frame.Origin.Z > 0.5 * Height) Settings.Faces.push_back(Face);
            const std::string Name = "D-extrusion · hollow, wall 0.1, top open";
            const FaceOperationOutcome Outcome = ApplyFaceOperation(Partition, Body, Settings);
            if (Settings.Faces.size() != 1 || !Outcome.Refusal.empty()) { Report(HollowGate, Name, "refused: " + Outcome.Refusal, 1.0, false); return; }
            const double Inner    = InwardOffsetArea(Arch, Thickness);
            const double Expected = Profile.Area * Height - Inner * (Height - Thickness);
            const double Volume   = IntegrateBodyVolume(Partition, Outcome.Body);
            const double Error    = std::fabs(Volume - Expected) / Expected;
            Report(HollowGate, Name, "V " + std::to_string(Volume) + " vs closed form " + std::to_string(Expected) + " (A_in " + std::to_string(Inner) + " by Green; the O(t²) corner formula would give "
                   + std::to_string(Profile.Area - (Length + 2.0) * Thickness + Thickness * Thickness * (2.0 + Pi / 2.0)) + ") → " + Scientific(Error) + " · audit " + (Outcome.Audit.SoundCondition() ? "sound" : "UNSOUND"),
                   Error, Error <= 1.0e-9 && Outcome.Audit.SoundCondition());
        }
    }
}

namespace
{
    // ---- A NURBS stand-ins (N3) ----------------------------------------------------------------------------------------

    // Chart box of a face from its SP-curves (256 samples per fin, as the approximation itself measures it).
    [[nodiscard]] std::pair<Vector2, Vector2> FaceChartBox(const ModelPartition& Partition, FaceToken Face)
    {
        Vector2 Low{ INFINITY, INFINITY }, High{ -INFINITY, -INFINITY };
        for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const FinProfile&  Use  = Partition.Fins.Resolve(Fin);
                const EdgeProfile& Edge = Partition.Edges.Resolve(Use.Edge);
                if (!Use.SurfaceCurve) return;
                for (int Sample = 0; Sample <= 256; ++Sample)
                {
                    const Vector2 Point = EvaluateParameterCurve(*Use.SurfaceCurve, Edge.IntervalLow + (Edge.IntervalHigh - Edge.IntervalLow) * Sample / 256.0).Position;
                    Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y) };
                    High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y) };
                }
            });
        return { Low, High };
    }

    [[nodiscard]] bool StandInCondition(const SurfaceGeometry& Surface) { return std::holds_alternative<RollingBallSurface>(Surface) || std::holds_alternative<ParallelSurface>(Surface); }

    // Unit normal from the chart derivatives (sign ignored by the callers: they compare lines, not senses).
    [[nodiscard]] Vector3 ChartNormal(const SurfaceGeometry& Surface, Vector2 Parameter)
    {
        const SurfaceSample Sample = EvaluateSurface(Surface, Parameter);
        return Normalize(Cross(Sample.DerivativeU, Sample.DerivativeV));
    }

    [[nodiscard]] double LineAngle(Vector3 First, Vector3 Second) { return std::asin(std::min(1.0, Length(Cross(First, Second)))); }

    // 📝 Deterministic chart points (64-bit LCG), so a run is reproducible and still off every lattice.
    struct ChartScatter
    {
        uint64_t State = 0x9E3779B97F4A7C15ull;                                                  // [-] - LCG state
        double Next()
        {
            State = State * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<double>(State >> 11) * (1.0 / 9007199254740992.0);
        }
    };

    struct StandInMeasure
    {
        double Parametric = 0.0;                                                                 // [len] - |S − A| at the same chart point
        double Geometric  = 0.0;                                                                 // [len] - |distance of A to S|
        double Drift      = 0.0;                                                                 // [rad] - Normal line angle
    };

    [[nodiscard]] StandInMeasure MeasureStandIn(const SurfaceGeometry& Exact, const SurfaceGeometry& StandIn, Vector2 Low, Vector2 High)
    {
        StandInMeasure Measure;
        ChartScatter   Scatter;
        for (int Sample = 0; Sample < 2000; ++Sample)
        {
            const Vector2 Point   = { Low.X + (High.X - Low.X) * Scatter.Next(), Low.Y + (High.Y - Low.Y) * Scatter.Next() };
            const Vector3 Spline  = EvaluateSurface(StandIn, Point).Position;
            Measure.Parametric = std::max(Measure.Parametric, Distance(EvaluateSurface(Exact, Point).Position, Spline));
            Measure.Geometric  = std::max(Measure.Geometric, std::fabs(MeasureSurfaceDistance(Exact, Spline).Distance));
            Measure.Drift      = std::max(Measure.Drift, LineAngle(ChartNormal(Exact, Point), ChartNormal(StandIn, Point)));
        }
        return Measure;
    }

    struct ExchangeSpecimen
    {
        const char*                               Name;
        std::function<BodyToken(ModelPartition&)> Build;                                         // Exact body with procedural carriers
    };

    [[nodiscard]] BodyToken BlendNearest(ModelPartition& Partition, BodyToken Body, Vector3 Probe, double Radius)
    {
        BlendSettings Settings;
        Settings.Profile = BlendProfile::Fillet;
        Settings.Size    = Radius;
        const BlendOutcome Blended = BlendEdges(Partition, Body, { NearestEdge(Partition, Body, Probe) }, Settings);
        return Blended.Refusal.empty() ? Blended.Body : BodyToken{};
    }

    void ValidateApproximation(const std::string& Only)
    {
        const SketchProfile Arch = DProfile();
        const std::vector<ExchangeSpecimen> Specimens =
        {
            { "Promoted block · fillet r 0.2 (rolling ball on NURBS planes)", [](ModelPartition& Partition)
              { const BodyToken Block = ConstructBlock(Partition, { 0, 0, 0 }, { 2, 1.5, 1 }).Value; return BlendNearest(Partition, PromoteBodyToNurbs(Partition, Block).Body, { 1.0, 0.0, 1.0 }, 0.2); } },
            { "Promoted cylinder rim · fillet r 0.2 (closed rolling ball)", [](ModelPartition& Partition)
              { const BodyToken Drum = ConstructCylinder(Partition, Upright({ 0, 0, 0 }), 1.0, 1.5).Value; return BlendNearest(Partition, PromoteBodyToNurbs(Partition, Drum).Body, { 1.0, 0.0, 1.5 }, 0.2); } },
            { "Freeform D-extrusion · fillet r 0.25 on the spline rim (traced spine, knots at ⅓ and ⅔)", [&](ModelPartition& Partition)
              { return BlendNearest(Partition, ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, 1.0).Body, { 1.0, 1.275, 1.0 }, 0.25); } },
            { "Freeform D-extrusion · spline wall offset outward 0.1 (parallel carrier, traced edges)", [&](ModelPartition& Partition)
              { return OffsetSplineWall(Partition, ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, 1.0).Body, 0.1).Body; } },
        };
        // ---- A1 parallel carriers, surface level (bodies with a parallel wall follow in the A2 list).
        if (Only.empty() || Only == "approximate" || Only == "standin")
        {
            struct Offset { const char* Name; double Distance; };
            for (const Offset& Each : { Offset{ "Freeform bicubic patch offset +0.1 (parallel carrier)", 0.1 }, Offset{ "Freeform bicubic patch offset −0.25 (parallel carrier)", -0.25 } })
                for (const double Tolerance : { 1.0e-6, 1.0e-8 })
                {
                    const SurfaceGeometry      Exact = ConstructParallelSurface(FreeformPatch(), Each.Distance);
                    ApproximationSettings      Settings;
                    Settings.Tolerance = Tolerance;
                    const auto                 Moment = std::chrono::steady_clock::now();
                    const SurfaceApproximation Fit    = ApproximateSurfaceAsNurbs(Exact, { 0.0, 0.0 }, { 2.0, 2.0 }, Settings);
                    const double               Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Moment).count();
                    const std::string          Label   = std::string(Each.Name) + " · tolerance " + Scientific(Tolerance);
                    if (!Fit.Refusal.empty()) { Report(StandInGate, Label, "refused: " + Fit.Refusal, 1.0, false); continue; }
                    const StandInMeasure Measure = MeasureStandIn(Exact, *Fit.Surface, { 0.0, 0.0 }, { 2.0, 2.0 });
                    Report(StandInGate, Label,
                           "parametric " + Scientific(Measure.Parametric) + " · geometric " + Scientific(Measure.Geometric) + " · normal drift " + Scientific(Measure.Drift) + " rad · "
                           + std::to_string(Fit.Surface->Definition->CountU) + " × " + std::to_string(Fit.Surface->Definition->CountV) + " poles · " + std::to_string(static_cast<int>(Seconds * 1000.0)) + " ms",
                           std::max(Measure.Parametric, Measure.Geometric) / Tolerance, Measure.Parametric <= Tolerance && Measure.Geometric <= Tolerance && Measure.Drift <= Settings.AngularTolerance);
                }
        }

        for (const ExchangeSpecimen& Each : Specimens)
        {
            if (!Only.empty() && Only != "approximate" && Only != "exchange") break;
            ModelPartition  Partition;
            const BodyToken Exact = Each.Build(Partition);
            if (!Exact.PresentCondition()) { Report(ExchangeGate, Each.Name, "the exact specimen could not be built", 1.0, false); continue; }
            const double ExactVolume = IntegrateBodyVolume(Partition, Exact);
            for (const double Tolerance : { 1.0e-6, 1.0e-8 })
            {
                const std::string Label = std::string(Each.Name) + " · tolerance " + Scientific(Tolerance);
                ApproximationSettings Settings;
                Settings.Tolerance = Tolerance;
                const auto                 Moment  = std::chrono::steady_clock::now();
                const ApproximationOutcome Outcome = ApproximateBodyAsNurbs(Partition, Exact, Settings);
                const double               Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Moment).count();
                if (!Outcome.Transform.Refusal.empty()) { Report(ExchangeGate, Label, "refused: " + Outcome.Transform.Refusal, 1.0, false); continue; }
                const BodyToken Copy = Outcome.Transform.Body;

                // A1 per approximated face (source and image share the copy order).
                for (size_t Index = 0; Index < Outcome.Transform.SourceFaces.size(); ++Index)
                {
                    const SurfaceGeometry& Source = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Outcome.Transform.SourceFaces[Index]).Surface);
                    if (!StandInCondition(Source)) continue;
                    const SurfaceGeometry& Image  = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Outcome.Transform.Faces[Index]).Surface);
                    const auto [Low, High]        = FaceChartBox(Partition, Outcome.Transform.SourceFaces[Index]);
                    const StandInMeasure Measure  = MeasureStandIn(Source, Image, Low, High);
                    const NurbsSurfaceDefinition& Spline = *std::get<NurbsSurface>(Image).Definition;
                    Report(StandInGate, Label + " · " + SurfaceCategoryLabel(Source) + " face",
                           "parametric " + Scientific(Measure.Parametric) + " · geometric " + Scientific(Measure.Geometric) + " · normal drift " + Scientific(Measure.Drift) + " rad · "
                           + std::to_string(Spline.CountU) + " × " + std::to_string(Spline.CountV) + " poles",
                           std::max(Measure.Parametric, Measure.Geometric) / Tolerance, Measure.Parametric <= Tolerance && Measure.Geometric <= Tolerance && Measure.Drift <= Settings.AngularTolerance);
                }

                // A2 the body: carriers, audit, volume, edges on faces, G1 drift.
                const BodyInventory Inventory = Partition.CollectInventory(Copy);
                int Leftovers = 0;
                for (FaceToken Face : Inventory.Faces) Leftovers += std::holds_alternative<NurbsSurface>(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface)) ? 0 : 1;
                for (EdgeToken Edge : Inventory.Edges) Leftovers += std::holds_alternative<NurbsCurve>(Partition.Curves.Resolve(Partition.Edges.Resolve(Edge).Curve)) ? 0 : 1;
                const auto        AuditMoment = std::chrono::steady_clock::now();
                const AuditReport Audit  = AuditBody(Partition, Copy);
                const auto        MassMoment = std::chrono::steady_clock::now();
                // |ΔV| ≤ tolerance × (area of the approximated faces): each stand-in point is within the tolerance.
                double StandInArea = 0.0;
                for (FaceToken Face : Outcome.Transform.SourceFaces)
                    if (StandInCondition(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface))) StandInArea += MeasureFaceArea(Partition, Face).Area;
                const double VolumeGap   = std::fabs(IntegrateBodyVolume(Partition, Copy) - ExactVolume);
                const double VolumeLimit = Tolerance * StandInArea;
                const auto        EdgeMoment = std::chrono::steady_clock::now();
                std::map<uint32_t, std::vector<FaceToken>> EdgeFaces;
                for (FaceToken Face : Inventory.Faces)
                    for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
                        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin) { EdgeFaces[Partition.Fins.Resolve(Fin).Edge.Slot].push_back(Face); });
                double OffFace = 0.0, Drift = 0.0;
                for (EdgeToken Edge : Inventory.Edges)
                {
                    const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
                    const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
                    const std::vector<FaceToken>& Faces = EdgeFaces[Edge.Slot];
                    for (int Sample = 0; Sample <= 32; ++Sample)
                    {
                        const Vector3 Point = EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / 32.0).Position;
                        std::vector<Vector3> Normals;
                        for (FaceToken Face : Faces)
                        {
                            const SurfaceDistance Reach = MeasureSurfaceDistance(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface), Point);
                            OffFace = std::max(OffFace, std::fabs(Reach.Distance));
                            Normals.push_back(Reach.Normal);
                        }
                        // G1 drift only where the exact body is tangent (blend boundaries), not across sharp edges.
                        if (Normals.size() == 2 && LineAngle(Normals[0], Normals[1]) < 1.0e-2) Drift = std::max(Drift, LineAngle(Normals[0], Normals[1]));
                    }
                }
                const bool Passed = Leftovers == 0 && Audit.SoundCondition() && VolumeGap <= VolumeLimit && Outcome.SurfaceDeviation <= Tolerance && Outcome.CurveDeviation <= Tolerance
                                    && OffFace <= 2.0 * Tolerance && Drift <= Settings.AngularTolerance && Outcome.ApproximatedFaces > 0;
                const std::string Finding = Audit.SoundCondition() ? "" : std::string(" (") + Audit.Findings[0].Code + " " + Audit.Findings[0].Subject + ": " + Audit.Findings[0].Detail + ", "
                                                                           + Scientific(Audit.Findings[0].Measured) + " > " + Scientific(Audit.Findings[0].Allowed) + ")";
                Report(ExchangeGate, Label,
                       std::to_string(Outcome.ApproximatedFaces) + " face(s) · " + std::to_string(Outcome.ApproximatedEdges) + " edge(s) approximated · " + std::to_string(Outcome.PoleCount) + " poles · certified "
                       + Scientific(Outcome.SurfaceDeviation) + " / " + Scientific(Outcome.CurveDeviation) + " · procedural left " + std::to_string(Leftovers) + " · audit " + (Audit.SoundCondition() ? "sound" : "UNSOUND" + Finding)
                       + " · |ΔV| " + Scientific(VolumeGap) + " ≤ " + Scientific(VolumeLimit) + " · edges off faces " + Scientific(OffFace) + " · G1 drift " + Scientific(Drift) + " rad · "
                       + std::to_string(static_cast<int>(Seconds * 1000.0)) + " ms (audit " + std::to_string(static_cast<int>(std::chrono::duration<double>(MassMoment - AuditMoment).count() * 1000.0))
                       + " · volumes " + std::to_string(static_cast<int>(std::chrono::duration<double>(EdgeMoment - MassMoment).count() * 1000.0)) + " · edges "
                       + std::to_string(static_cast<int>(std::chrono::duration<double>(std::chrono::steady_clock::now() - EdgeMoment).count() * 1000.0)) + ")",
                       std::max({ VolumeGap / VolumeLimit, OffFace / (2.0 * Tolerance) }), Passed);
            }
        }

        // ---- A3 refusals: atomic, no body created.
        if (Only.empty() || Only == "approximate" || Only == "refusal")
        {
            ModelPartition  Partition;
            const BodyToken Exact = BlendNearest(Partition, ExtrudeProfile(Partition, Arch, OrthonormalFrame{}, 1.0).Body, { 1.0, 1.275, 1.0 }, 0.25);
            struct Refusal { const char* Name; double Tolerance; int MaximumSpans; const char* Expected; };
            const std::vector<Refusal> Refusals =
            {
                { "Tolerance 10⁻¹² with at most 4 spans per direction", 1.0e-12, 4, "spans needed" },
                { "Tolerance 0", 0.0, 512, "positive" },
                { "Tolerance NaN", std::numeric_limits<double>::quiet_NaN(), 512, "positive" },
            };
            for (const Refusal& Each : Refusals)
            {
                const size_t Before = (Partition.Bodies.OccupiedCount() + Partition.Faces.OccupiedCount() + Partition.Edges.OccupiedCount());
                ApproximationSettings Settings;
                Settings.Tolerance    = Each.Tolerance;
                Settings.MaximumSpans = Each.MaximumSpans;
                const ApproximationOutcome Outcome = ApproximateBodyAsNurbs(Partition, Exact, Settings);
                const bool Passed = !Outcome.Transform.Refusal.empty() && Outcome.Transform.Refusal.find(Each.Expected) != std::string::npos && (Partition.Bodies.OccupiedCount() + Partition.Faces.OccupiedCount() + Partition.Edges.OccupiedCount()) == Before;
                Report(ApproximateGate, Each.Name, Outcome.Transform.Refusal.empty() ? "accepted (expected a refusal)" : "\"" + Outcome.Transform.Refusal + "\"", Passed ? 0.0 : 1.0, Passed);
            }
            const SurfaceApproximation Empty = ApproximateSurfaceAsNurbs(PlaneSurface{}, { 0.0, 0.0 }, { 0.0, 1.0 });
            Report(ApproximateGate, "Empty window [0, 0] × [0, 1]", Empty.Refusal.empty() ? "accepted (expected a refusal)" : "\"" + Empty.Refusal + "\"", Empty.Refusal.empty() ? 1.0 : 0.0, !Empty.Refusal.empty());

            // ---- A4 convergence (position only: the angular test off, a carrier with one basis knot per direction).
            const SurfaceGeometry Offset = ConstructParallelSurface(FreeformPatch(), 0.1);
            const std::vector<double> Tolerances = { 1.0e-4, 1.0e-6, 1.0e-8, 1.0e-10 };
            std::vector<double> Spans;
            std::string Detail;
            bool Certified = true;
            for (double Tolerance : Tolerances)
            {
                ApproximationSettings Settings;
                Settings.Tolerance        = Tolerance;
                Settings.AngularTolerance = 1.0;
                const SurfaceApproximation Fit = ApproximateSurfaceAsNurbs(Offset, { 0.0, 0.0 }, { 2.0, 2.0 }, Settings);
                Certified = Certified && Fit.Refusal.empty() && Fit.Deviation <= Tolerance;
                Spans.push_back(std::sqrt(static_cast<double>(Fit.SpansU) * Fit.SpansV));
                Detail += Scientific(Tolerance) + ": " + std::to_string(Fit.SpansU) + " × " + std::to_string(Fit.SpansV) + " spans, certified " + Scientific(Fit.Deviation) + " · ";
            }
            // Mean spans per direction N ∝ ε^(−1/p): p = ln(ε₁/ε₄) / ln(N₄/N₁) over six decades.
            const double Order = std::log(Tolerances.front() / Tolerances.back()) / std::log(Spans.back() / Spans.front());
            Detail += "order p = " + std::to_string(Order).substr(0, 4) + " (cubic interpolation: 4)";
            Report(OrderGate, "Parallel offset +0.1 of the freeform patch", Detail, Order, Certified && Order >= 3.5 && Order <= 5.0);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    const std::string Only = Count > 2 && std::string(Arguments[1]) == "--only" ? Arguments[2] : "";
    // --deadline SECONDS (after --only KEY, POSIX only): exit cleanly on the alarm so profilers still write their report.
#if defined(__unix__)
    if (Count > 4 && std::string(Arguments[3]) == "--deadline")
    {
        std::signal(SIGALRM, [](int) { std::fprintf(stderr, "⚠️ deadline reached\n"); std::exit(3); });
        alarm(static_cast<unsigned>(std::atoi(Arguments[4])));
    }
#endif
    const auto Start = std::chrono::steady_clock::now();
    if (Only.empty() || Only == "carrier") ValidateClosedForms();
    if (Only.empty() || Only == "freeform") ValidateFreeform();
    if (Only.empty() || Only == "section") ValidateSections();
    if (Only.empty() || Only == "fold") ValidateFolds();
    if (Only.empty() || Only == "blend" || Only == "block" || Only == "rim" || Only == "arch" || Only == "refusal") ValidateBlends(Only);
    if (Only.empty() || Only == "nurbsblend" || Only == "chamfer" || Only == "variable") ValidateNurbsBlends(Only);
    if (Only.empty() || Only == "offset") ValidateOffsets();
    if (Only.empty() || Only == "approximate" || Only == "exchange" || Only == "standin" || Only == "refusal") ValidateApproximation(Only);
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !Only.empty());
    std::printf("# Freeform blend validation\n\n%s Priority 10: exact offsets of NURBS surfaces, rolling-ball blends on NURBS supports, offsets / hollows of NURBS walls and certified NURBS stand-ins (%.2f s)\n\n", PassedCondition ? "🟢" : "🔴", Seconds);
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
