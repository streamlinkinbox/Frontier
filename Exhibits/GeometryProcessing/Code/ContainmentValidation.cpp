/*====================================================================================================================================
                                                   CONTAINMENTVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step-3 containment gates: generalized winding number against analytic membership (H1), near-boundary offsets
//    (H2), on-boundary detection (H3), an independent ray-cast oracle (H4), a leaky surface against the closed-form
//    solid angle (H5) and the 2D point-in-face winding against analytic face shapes (H6)

#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Containment/WindingNumber.h"
#include "Engine/GeometryProcessing/Intersection/CurveSurfaceIntersection.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double π = std::numbers::pi;                                                   // [rad] - π

    using Clock = std::chrono::steady_clock;

    struct GateRow
    {
        std::string Gate         = {};                                                       // [-]  - H1 … H6
        std::string Family       = {};                                                       // [-]  - What was tested
        long        Cases        = 0;                                                        // [-]  - Instances
        long        Failures     = 0;                                                        // [-]  - Wrong answers
        std::string Measured     = {};                                                       // [-]  - Worst measured value(s)
        std::string Allowed      = {};                                                       // [-]  - Allowance
        double      Milliseconds = 0.0;                                                      // [ms] - Wall time
    };

    std::vector<GateRow> Rows;
    std::mt19937_64      Generator(20261021);

    [[nodiscard]] std::string Format(const char* Pattern, double First, double Second = 0.0, double Third = 0.0)
    {
        char Buffer[160];
        std::snprintf(Buffer, sizeof(Buffer), Pattern, First, Second, Third);
        return Buffer;
    }

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }
    [[nodiscard]] double Uniform(double Low, double High) { return std::uniform_real_distribution<double>(Low, High)(Generator); }

    [[nodiscard]] Vector3 RandomDirection()
    {
        for (;;)
        {
            const Vector3 Candidate{ Uniform(-1.0, 1.0), Uniform(-1.0, 1.0), Uniform(-1.0, 1.0) };
            const double  Magnitude = Length(Candidate);
            if (Magnitude > 0.2 && Magnitude <= 1.0) return Candidate * (1.0 / Magnitude);
        }
    }

    [[nodiscard]] double BoxDistance(Vector3 Position, Vector3 Low, Vector3 High)
    {
        const Vector3 Centre = (Low + High) * 0.5;                                           // [len] - Box centre
        const Vector3 Half   = (High - Low) * 0.5;                                           // [len] - Half extents
        const Vector3 Offset = { std::fabs(Position.X - Centre.X) - Half.X, std::fabs(Position.Y - Centre.Y) - Half.Y, std::fabs(Position.Z - Centre.Z) - Half.Z };
        const Vector3 Outer  = { std::max(Offset.X, 0.0), std::max(Offset.Y, 0.0), std::max(Offset.Z, 0.0) };
        return Length(Outer) + std::min(std::max(Offset.X, std::max(Offset.Y, Offset.Z)), 0.0);
    }

    // 📝 Specimen with an independent analytic signed distance (negative inside; a lower bound on |distance| is enough
    //    because it only excludes points too close to call).
    struct ContainmentSpecimen
    {
        std::string                    Name           = {};                                  // [-]   - Label
        BodyToken                      Body           = {};                                  // [-]   - Built body
        std::function<double(Vector3)> SignedDistance = {};                                  // [len] - Analytic oracle
        Vector3                        Low            = {};                                  // [len] - Sampling box
        Vector3                        High           = {};                                  // [len] - Sampling box
    };

    [[nodiscard]] std::vector<ContainmentSpecimen> BuildSpecimens(ModelPartition& Partition)
    {
        std::vector<ContainmentSpecimen> Specimens;
        const OrthonormalFrame World = {};

        Specimens.push_back({ "Block", ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 1.5, 1.0 }).Value,
                              [](Vector3 P) { return BoxDistance(P, { 0.0, 0.0, 0.0 }, { 2.0, 1.5, 1.0 }); }, { 0.0, 0.0, 0.0 }, { 2.0, 1.5, 1.0 } });

        // Rotated block: a prism over a rotated rectangle swept along a tilted axis frame.
        const OrthonormalFrame Tilt = ConstructFrame({ 0.3, -0.2, 0.1 }, { 0.3, 0.4, 1.0 }, { 1.0, 0.5, 0.0 });
        const auto Local = [Tilt](Vector3 P) { const Vector3 D = P - Tilt.Origin; return Vector3{ Dot(D, Tilt.XAxis), Dot(D, Tilt.YAxis), Dot(D, Tilt.ZAxis) }; };
        const std::vector<Vector3> Profile = { Tilt.Origin, Tilt.Origin + Tilt.XAxis * 1.6, Tilt.Origin + Tilt.XAxis * 1.6 + Tilt.YAxis * 0.9, Tilt.Origin + Tilt.YAxis * 0.9 };
        Specimens.push_back({ "RotatedBlock", ConstructPrism(Partition, Profile, Tilt.ZAxis * 1.2).Value.Body,
                              [Local](Vector3 P) { return BoxDistance(Local(P), { 0.0, 0.0, 0.0 }, { 1.6, 0.9, 1.2 }); }, { -1.2, -1.2, -0.6 }, { 2.2, 1.8, 1.8 } });

        Specimens.push_back({ "Cylinder", ConstructCylinder(Partition, World, 1.0, 2.0).Value,
                              [](Vector3 P) { return std::max({ std::hypot(P.X, P.Y) - 1.0, -P.Z, P.Z - 2.0 }); }, { -1.0, -1.0, 0.0 }, { 1.0, 1.0, 2.0 } });

        const double Slope = 1.0 / std::hypot(1.0, 0.5);                                     // [-] - cos of the half-angle (r 1, h 2)
        Specimens.push_back({ "Cone", ConstructCone(Partition, World, 1.0, 2.0).Value,
                              [Slope](Vector3 P) { return std::max({ (std::hypot(P.X, P.Y) - (1.0 - P.Z * 0.5)) * Slope, -P.Z, P.Z - 2.0 }); },
                              { -1.0, -1.0, 0.0 }, { 1.0, 1.0, 2.0 } });

        Specimens.push_back({ "Sphere", ConstructSphere(Partition, World, 1.0).Value,
                              [](Vector3 P) { return Length(P) - 1.0; }, { -1.0, -1.0, -1.0 }, { 1.0, 1.0, 1.0 } });

        Specimens.push_back({ "Torus", ConstructTorus(Partition, World, 2.0, 0.6).Value,
                              [](Vector3 P) { return std::hypot(std::hypot(P.X, P.Y) - 2.0, P.Z) - 0.6; }, { -2.6, -2.6, -0.6 }, { 2.6, 2.6, 0.6 } });

        Specimens.push_back({ "PerforatedBlock", ConstructPerforatedBlock(Partition, { 3.0, 3.0, 1.0 }, { 1.0, 1.4, 1.0 }).Value,
                              [](Vector3 P)
                              {
                                  const double Hole = std::max(std::fabs(P.X - 1.5) - 0.5, std::fabs(P.Y - 1.5) - 0.7);   // [len] - 2D hole SDF
                                  return std::max(BoxDistance(P, { 0.0, 0.0, 0.0 }, { 3.0, 3.0, 1.0 }), -Hole);
                              }, { 0.0, 0.0, 0.0 }, { 3.0, 3.0, 1.0 } });

        Specimens.push_back({ "HollowBlock", ConstructHollowBlock(Partition, { 3.0, 2.0, 2.0 }, { 1.0, 1.0, 1.0 }).Value,
                              [](Vector3 P) { return std::max(BoxDistance(P, { 0.0, 0.0, 0.0 }, { 3.0, 2.0, 2.0 }), -BoxDistance(P, { 1.0, 0.5, 0.5 }, { 2.0, 1.5, 1.5 })); },
                              { 0.0, 0.0, 0.0 }, { 3.0, 2.0, 2.0 } });
        return Specimens;
    }

    [[nodiscard]] Vector3 RandomInBox(const ContainmentSpecimen& Specimen, double Growth)
    {
        const Vector3 Span = (Specimen.High - Specimen.Low) * Growth;                        // [len] - Grown span
        const Vector3 Low  = (Specimen.Low + Specimen.High) * 0.5 - Span * 0.5;              // [len] - Grown corner
        return { Low.X + Span.X * Uniform(0.0, 1.0), Low.Y + Span.Y * Uniform(0.0, 1.0), Low.Z + Span.Z * Uniform(0.0, 1.0) };
    }

    // 📝 Independent oracle: parity of transversal ray crossings found by exact curve–surface intersection and the 2D
    //    point-in-face test. Rays that graze (tangent crossing, edge hit, coincidence) are re-drawn.
    [[nodiscard]] int RayParity(const BodyContainment& Containment, Vector3 Origin)
    {
        for (int Attempt = 0; Attempt < 16; ++Attempt)
        {
            const StraightLine Ray = { Origin, RandomDirection() };
            int  Crossings = 0;                                                              // [-] - Transversal face hits
            bool Graze     = false;                                                          // [-] - Redraw required
            for (const FaceDomain& Domain : Containment.Faces)
            {
                const CurveSurfaceOutcome Hits = IntersectCurveSurface(Ray, 0.0, 50.0, Domain.Surface);
                if (!Hits.Coincidences.empty()) { Graze = true; break; }
                for (const CurveSurfaceCrossing& Hit : Hits.Crossings)
                {
                    if (Hit.CurveParameter < 1.0e-9) continue;
                    const FacePositionClass Position = ClassifyFacePosition(Domain, Hit.Position, 1.0e-7);
                    if (Position.Membership == PointMembership::Boundary || (Hit.Category == CrossingCategory::Tangent && Position.Membership == PointMembership::Inside))
                    {
                        Graze = true;
                        break;
                    }
                    if (Position.Membership == PointMembership::Inside) ++Crossings;
                }
                if (Graze) break;
            }
            if (!Graze) return Crossings % 2;
        }
        return -1;
    }

    // Random interior chart point of a face with at least Clearance to every boundary curve (3D).
    [[nodiscard]] bool SampleFacePoint(const FaceDomain& Domain, double Clearance, Vector2& Parameter)
    {
        for (int Attempt = 0; Attempt < 400; ++Attempt)
        {
            const Vector2 Candidate = { Uniform(Domain.Window.Low.X, Domain.Window.High.X), Uniform(Domain.Window.Low.Y, Domain.Window.High.Y) };
            if (ClassifyParameterPoint(Domain, Candidate) != PointMembership::Inside) continue;
            const SurfaceSample Sample = EvaluateSurface(Domain.Surface, Candidate);
            if (Length(Cross(Sample.DerivativeU, Sample.DerivativeV)) < 1.0e-6) continue;
            if (ClassifyFacePosition(Domain, Sample.Position, Clearance).Membership == PointMembership::Boundary) continue;
            Parameter = Candidate;
            return true;
        }
        return false;
    }

    [[nodiscard]] Vector3 OutwardNormal(const FaceDomain& Domain, Vector2 Parameter)
    {
        const Vector3 Normal = EvaluateSurfaceNormal(Domain.Surface, Parameter);
        return Domain.Sense == Orientation::Forward ? Normal : Normal * -1.0;
    }

    void ValidateAnalyticMembership(const std::vector<ContainmentSpecimen>& Specimens, const ModelPartition& Partition)
    {
        for (const ContainmentSpecimen& Specimen : Specimens)
        {
            const auto Start = Clock::now();
            const BodyContainment Containment = PrepareBodyContainment(Partition, Specimen.Body);
            constexpr int Count = 1500;                                                      // [-] - Random points
            long   Failures = 0, Excluded = 0;
            double WorstResidual = 0.0;                                                      // [-] - max |w − round w|
            double Evaluations   = 0.0;                                                      // [-] - Mean surface evaluations
            for (int Index = 0; Index < Count; ++Index)
            {
                const Vector3 Position = RandomInBox(Specimen, 1.3);
                const double  Distance = Specimen.SignedDistance(Position);                  // [len] - Analytic
                if (std::fabs(Distance) < 1.0e-6) { ++Excluded; continue; }
                const SolidPointClass Outcome = ClassifySolidPoint(Containment, Position);
                const PointMembership Expected = Distance < 0.0 ? PointMembership::Inside : PointMembership::Outside;
                if (Outcome.Membership != Expected || Outcome.IntegerResidual > 1.0e-6) ++Failures;
                WorstResidual = std::max(WorstResidual, Outcome.IntegerResidual);
                Evaluations  += static_cast<double>(Outcome.Evaluations) / Count;
            }
            Rows.push_back({ "H1", Specimen.Name + " random", Count - Excluded, Failures,
                             Format("residual %.1e · %.0f evals/pt", WorstResidual, Evaluations), "0 wrong · residual ≤ 1e-6", Elapsed(Start) });
        }
    }

    void ValidateNearBoundary(const std::vector<ContainmentSpecimen>& Specimens, const ModelPartition& Partition)
    {
        const double Offsets[] = { 1.0e-3, 1.0e-5, 1.0e-7 };                                 // [len] - Normal offsets
        const auto Start = Clock::now();
        long   Cases = 0, Failures = 0, ByWinding = 0, ByNormal = 0;
        long   PureCases = 0, PureFailures = 0;
        double WorstResidual = 0.0;                                                          // [-] - max |w − round w|
        double PureResidual  = 0.0;                                                          // [-] - Shortcut disabled
        WindingSettings Pure;
        Pure.NearDistance = 0.0;
        for (const ContainmentSpecimen& Specimen : Specimens)
        {
            const BodyContainment Containment = PrepareBodyContainment(Partition, Specimen.Body);
            for (int Index = 0; Index < 24; ++Index)
            {
                const FaceDomain& Domain = Containment.Faces[std::uniform_int_distribution<size_t>(0, Containment.Faces.size() - 1)(Generator)];
                Vector2 Parameter;
                if (!SampleFacePoint(Domain, 2.0e-2, Parameter)) continue;
                const Vector3 Surface = EvaluateSurface(Domain.Surface, Parameter).Position;
                const Vector3 Normal  = OutwardNormal(Domain, Parameter);
                for (double Offset : Offsets)
                {
                    const SolidPointClass Inner = ClassifySolidPoint(Containment, Surface - Normal * Offset);
                    const SolidPointClass Outer = ClassifySolidPoint(Containment, Surface + Normal * Offset);
                    Cases += 2;
                    Failures += Inner.Membership != PointMembership::Inside ? 1 : 0;
                    Failures += Outer.Membership != PointMembership::Outside ? 1 : 0;
                    WorstResidual = std::max({ WorstResidual, Inner.IntegerResidual, Outer.IntegerResidual });
                    for (const SolidPointClass* Outcome : { &Inner, &Outer })
                    {
                        ByWinding += Outcome->Method == ContainmentMethod::Winding ? 1 : 0;
                        ByNormal  += Outcome->Method == ContainmentMethod::NearestNormal ? 1 : 0;
                    }
                    // Pure winding number (shortcut off) on every fourth sample down to 1e-5.
                    if (Index % 4 == 0 && Offset >= 1.0e-5)
                    {
                        const SolidPointClass PureInner = ClassifySolidPoint(Containment, Surface - Normal * Offset, Pure);
                        const SolidPointClass PureOuter = ClassifySolidPoint(Containment, Surface + Normal * Offset, Pure);
                        PureCases += 2;
                        PureFailures += PureInner.Membership != PointMembership::Inside ? 1 : 0;
                        PureFailures += PureOuter.Membership != PointMembership::Outside ? 1 : 0;
                        PureResidual = std::max({ PureResidual, PureInner.IntegerResidual, PureOuter.IntegerResidual });
                    }
                }
            }
        }
        Rows.push_back({ "H2", "± normal offsets 1e-3 / 1e-5 / 1e-7, all specimens", Cases, Failures,
                         Format("residual %.1e · %.0f winding / %.0f nearest-normal", WorstResidual, static_cast<double>(ByWinding), static_cast<double>(ByNormal)),
                         "0 wrong · residual ≤ 1e-6", Elapsed(Start) });
        Rows.push_back({ "H2w", "± offsets 1e-3 / 1e-5, pure winding number (shortcut off)", PureCases, PureFailures,
                         Format("residual %.1e", PureResidual), "0 wrong · residual ≤ 1e-6", 0.0 });
    }

    void ValidateOnBoundary(const std::vector<ContainmentSpecimen>& Specimens, const ModelPartition& Partition)
    {
        const auto Start = Clock::now();
        long Cases = 0, Failures = 0;
        for (const ContainmentSpecimen& Specimen : Specimens)
        {
            const BodyContainment Containment = PrepareBodyContainment(Partition, Specimen.Body);
            for (const FaceDomain& Domain : Containment.Faces)
            {
                for (int Index = 0; Index < 12; ++Index)
                {
                    Vector2 Parameter;
                    if (!SampleFacePoint(Domain, 0.0, Parameter)) continue;
                    const Vector3 Surface = EvaluateSurface(Domain.Surface, Parameter).Position;
                    const Vector3 Normal  = OutwardNormal(Domain, Parameter);
                    for (double Offset : { 0.0, 0.5 * SessionPrecision, -0.5 * SessionPrecision })
                    {
                        ++Cases;
                        Failures += ClassifySolidPoint(Containment, Surface + Normal * Offset).Membership != PointMembership::Boundary ? 1 : 0;
                    }
                }
                // Points on the 3D boundary curves (edges, seams, poles) are on the boundary too.
                for (const DomainBoundaryCurve& Edge : Domain.BoundaryCurves)
                {
                    ++Cases;
                    const Vector3 Position = EvaluateCurve(Edge.Curve, Uniform(Edge.Low, Edge.High)).Position;
                    Failures += ClassifySolidPoint(Containment, Position).Membership != PointMembership::Boundary ? 1 : 0;
                }
            }
        }
        Rows.push_back({ "H3", "on faces, edges, ±½ precision", Cases, Failures, "—", "0 wrong", Elapsed(Start) });
    }

    void ValidateRayOracle(const std::vector<ContainmentSpecimen>& Specimens, const ModelPartition& Partition)
    {
        const auto Start = Clock::now();
        long Cases = 0, Failures = 0, Undecided = 0;
        for (const ContainmentSpecimen& Specimen : Specimens)
        {
            const BodyContainment Containment = PrepareBodyContainment(Partition, Specimen.Body);
            for (int Index = 0; Index < 250; ++Index)
            {
                const Vector3 Position = RandomInBox(Specimen, 1.3);
                if (std::fabs(Specimen.SignedDistance(Position)) < 1.0e-6) continue;
                const int Parity = RayParity(Containment, Position);
                if (Parity < 0) { ++Undecided; continue; }
                ++Cases;
                const bool Inside = ClassifySolidPoint(Containment, Position).Membership == PointMembership::Inside;
                Failures += (Parity == 1) != Inside ? 1 : 0;
            }
        }
        Rows.push_back({ "H4", "GWN vs ray-cast parity (CSI + 2D face test)", Cases, Failures,
                         Format("%.0f undecided rays", static_cast<double>(Undecided)), "0 disagreements", Elapsed(Start) });
    }

    // 📝 Van Oosterom–Strackee: signed solid angle of triangle (A, B, C) seen from P, positive when counter-clockwise
    //    about the normal that faces away from P.
    [[nodiscard]] double TriangleSolidAngle(Vector3 Position, Vector3 First, Vector3 Second, Vector3 Third)
    {
        const Vector3 A = First - Position, B = Second - Position, C = Third - Position;     // [len] - Relative corners
        const double  La = Length(A), Lb = Length(B), Lc = Length(C);                        // [len] - Distances
        const double  Numerator   = Dot(A, Cross(B, C));                                     // [len³] - Triple product
        const double  Denominator = La * Lb * Lc + Dot(A, B) * Lc + Dot(A, C) * Lb + Dot(B, C) * La;
        return 2.0 * std::atan2(Numerator, Denominator);
    }

    void ValidateLeakySurface(ModelPartition& Partition)
    {
        const auto Start = Clock::now();
        const BodyToken Body = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
        std::vector<FaceToken> Kept;
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
        {
            const FaceDomain Domain = DescribeFaceDomain(Partition, Face);
            const Vector3    Centre = EvaluateSurface(Domain.Surface, *LocateFaceInterior(Domain)).Position;
            if (std::fabs(Centre.Z - 1.0) > 1.0e-12) Kept.push_back(Face);                   // Drop the top face (z = 1)
        }
        const BodyContainment Leaky = PrepareFaceContainment(Partition, Kept);
        const Vector3 Top[4] = { { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 1.0 }, { 1.0, 1.0, 1.0 }, { 0.0, 1.0, 1.0 } };
        long   Cases = 0, Failures = 0;
        double WorstError = 0.0;                                                             // [-] - |w − w_expected|
        for (int Index = 0; Index < 300; ++Index)
        {
            const Vector3 Position = { Uniform(-0.5, 1.5), Uniform(-0.5, 1.5), Uniform(-0.5, 1.5) };
            if (std::fabs(BoxDistance(Position, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 })) < 1.0e-3) continue;
            const double Closed  = BoxDistance(Position, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }) < 0.0 ? 1.0 : 0.0;
            const double OmegaTop = TriangleSolidAngle(Position, Top[0], Top[1], Top[2]) + TriangleSolidAngle(Position, Top[0], Top[2], Top[3]);
            const double Expected = Closed - OmegaTop / (4.0 * π);                          // [-] - Leaky winding number
            WindingTally Tally;
            const double Measured = MeasureWindingNumber(Leaky, Position, {}, Tally);
            const double Error    = std::fabs(Measured - Expected);                          // [-] - Deviation
            ++Cases;
            Failures  += Error > 1.0e-8 ? 1 : 0;
            WorstError = std::max(WorstError, Error);
        }
        Rows.push_back({ "H5", "open box (top removed) vs 1 − Ω_top/4π (Van Oosterom–Strackee)", Cases, Failures,
                         Format("|Δw| %.1e", WorstError), "|Δw| ≤ 1e-8", Elapsed(Start) });
    }

    void ValidateFaceWinding(const std::vector<ContainmentSpecimen>& Specimens, const ModelPartition& Partition)
    {
        // The perforated block's top face is a square with a rectangular hole: analytic membership in the chart.
        const auto Start = Clock::now();
        long Cases = 0, Failures = 0;
        const BodyContainment Containment = PrepareBodyContainment(Partition, Specimens[6].Body);
        for (const FaceDomain& Domain : Containment.Faces)
        {
            if (Domain.Loops.size() != 2) continue;                                          // Faces with a hole
            for (int Index = 0; Index < 4000; ++Index)
            {
                const Vector3 Position = { Uniform(-0.3, 3.3), Uniform(-0.3, 3.3), 0.0 };
                const Vector3 Lifted   = { Position.X, Position.Y, EvaluateSurface(Domain.Surface, { 0.0, 0.0 }).Position.Z };
                const double  Hole     = std::max(std::fabs(Position.X - 1.5) - 0.5, std::fabs(Position.Y - 1.5) - 0.7);
                const double  Outer    = std::max(std::fabs(Position.X - 1.5) - 1.5, std::fabs(Position.Y - 1.5) - 1.5);
                if (std::fabs(Hole) < 1.0e-9 || std::fabs(Outer) < 1.0e-9) continue;
                const bool    Expected = Outer < 0.0 && Hole > 0.0;
                const Vector2 Chart    = ProjectOntoSurface(Domain.Surface, Lifted, {});
                ++Cases;
                Failures += (ClassifyParameterPoint(Domain, Chart) == PointMembership::Inside) != Expected ? 1 : 0;
            }
        }
        Rows.push_back({ "H6", "2D winding: square face with a hole (top + bottom)", Cases, Failures, "—", "0 wrong", Elapsed(Start) });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ModelPartition Partition;
    const std::vector<ContainmentSpecimen> Specimens = BuildSpecimens(Partition);
    const std::function<void()> Families[] = {
        [&] { ValidateAnalyticMembership(Specimens, Partition); }, [&] { ValidateNearBoundary(Specimens, Partition); },
        [&] { ValidateOnBoundary(Specimens, Partition); },         [&] { ValidateRayOracle(Specimens, Partition); },
        [&] { ValidateLeakySurface(Partition); },                  [&] { ValidateFaceWinding(Specimens, Partition); } };
    for (const auto& Family : Families)
    {
        Family();
        std::fprintf(stderr, "  %s %s\n", Rows.back().Gate.c_str(), Rows.back().Failures == 0 ? "ok" : "FAILING");
    }

    long FailingRows = 0;
    std::printf("# GeometryProcessing — ContainmentValidation\n\n");
    std::printf("| | Gate | Family | Cases | Failures | Measured | Allowed | ms |\n|---|---|---|---|---|---|---|---|\n");
    for (const GateRow& Row : Rows)
    {
        FailingRows += Row.Failures > 0 ? 1 : 0;
        std::printf("| %s | %s | %s | %ld | %ld | %s | %s | %.0f |\n", Row.Failures == 0 ? "🟢" : "🔴", Row.Gate.c_str(), Row.Family.c_str(), Row.Cases, Row.Failures, Row.Measured.c_str(), Row.Allowed.c_str(), Row.Milliseconds);
    }
    std::printf("\n**%s** — %ld failing row(s)\n", FailingRows == 0 ? "PASS" : "FAIL", FailingRows);
    return FailingRows == 0 ? 0 : 1;
}
