/*====================================================================================================================================
                                                  INTERSECTIONVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step-2 gates: Bézier clipping (E1), curve–surface intersection (E2), analytic SSI (F1), marching vs analytic (F2),
//    and the scene gallery plus random oblique pairs through the independent intersection audit (F3)

#include "Engine/GeometryProcessing/Audit/IntersectionAudit.h"
#include "Engine/GeometryProcessing/Inspection/IntersectionCatalogue.h"
#include "Engine/GeometryProcessing/Intersection/BernsteinPolynomial.h"
#include "Engine/GeometryProcessing/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometryProcessing/Intersection/ImplicitSurface.h"

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
    constexpr double π        = std::numbers::pi;                                            // [rad] - π
    constexpr double FullTurn = 2.0 * π;                                                     // [rad] - 2π

    using Clock = std::chrono::steady_clock;

    struct GateRow
    {
        std::string Gate     = {};                                                           // [-]  - E1 … F3
        std::string Family   = {};                                                           // [-]  - What was tested
        long        Cases    = 0;                                                            // [-]  - Instances
        long        Failures = 0;                                                            // [-]  - Instances over the allowance
        std::string Measured = {};                                                           // [-]  - Worst measured value(s)
        std::string Allowed  = {};                                                           // [-]  - Allowance
        double      Milliseconds = 0.0;                                                      // [ms] - Wall time for the family
    };

    std::vector<GateRow> Rows;

    [[nodiscard]] std::string Format(const char* Pattern, double First, double Second = 0.0, double Third = 0.0)
    {
        char Buffer[160];
        std::snprintf(Buffer, sizeof(Buffer), Pattern, First, Second, Third);
        return Buffer;
    }

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    std::mt19937_64 Generator(20261020);

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

    [[nodiscard]] Vector3 RandomPoint(double Extent) { return { Uniform(-Extent, Extent), Uniform(-Extent, Extent), Uniform(-Extent, Extent) }; }

    [[nodiscard]] OrthonormalFrame RandomFrame(Vector3 Origin, Vector3 Axis)
    {
        Vector3 Hint = RandomDirection();
        while (Length(Cross(Hint, Axis)) < 0.3) Hint = RandomDirection();
        return ConstructFrame(Origin, Axis, Hint);
    }

    const ParameterBox WholeSphere{ { 0.0, -0.5 * π }, { FullTurn, 0.5 * π } };              // [-] - Full sphere chart
    const ParameterBox WholeTorus{ { 0.0, 0.0 }, { FullTurn, FullTurn } };                   // [-] - Full torus chart
    const ParameterBox WidePlane{ { -10.0, -10.0 }, { 10.0, 10.0 } };                         // [-] - Plane patch
    const ParameterBox TallCylinder{ { 0.0, -6.0 }, { FullTurn, 6.0 } };                      // [-] - Cylinder patch

    // ---- E1 ----------------------------------------------------------------------------------------------------------

    struct RootFamilyOutcome
    {
        long   Failures      = 0;                                                            // [-] - Polynomials with a wrong root set
        double WorstError    = 0.0;                                                          // [-] - Worst raw root error
        double WorstRatio    = 0.0;                                                          // [-] - Worst error / condition bound
        long   Clips         = 0;                                                            // [-] - Σ clip steps
        long   Subdivisions  = 0;                                                            // [-] - Σ halvings
        long   Spurious      = 0;                                                            // [-] - Reported, not a root
        long   Missed        = 0;                                                            // [-] - Root not reported
    };

    struct TrueRoot
    {
        double Position     = 0.0;                                                           // [-] - Requested root
        int    Multiplicity = 1;                                                             // [-] - 1 or 2
    };

    // 📝 Wilkinson bound: the coefficients are themselves rounded (≈ n·ε relative), so a root can only be located to
    //    δ ≈ K·n·ε·Σ|cₖ|Bₖ(r) / |𝑓′(r)| — or √(that with |𝑓″|/2) at a double root. Floor 1e-12, K = 16.
    [[nodiscard]] double ConditionBound(const BernsteinPolynomial& Polynomial, const TrueRoot& Root)
    {
        BernsteinPolynomial Magnitude = Polynomial;
        for (double& Coefficient : Magnitude.Coefficients) Coefficient = std::fabs(Coefficient);
        const double Noise = 16.0 * Polynomial.Degree() * 2.220446049250313e-16 * EvaluateBernstein(Magnitude, Root.Position);
        const BernsteinPolynomial Slope = DifferentiateBernstein(Polynomial);
        if (Root.Multiplicity == 1) return std::max(1.0e-12, Noise / std::max(std::fabs(EvaluateBernstein(Slope, Root.Position)), 1.0e-300));
        const double Curvature = 0.5 * std::fabs(EvaluateBernstein(DifferentiateBernstein(Slope), Root.Position));
        return std::max(1.0e-12, std::sqrt(Noise / std::max(Curvature, 1.0e-300)));
    }

    // 📝 Clusters: roots of 𝑓 (and 𝑓′ when DerivativeAssist) merged within twice the widest bound; each true distinct root
    //    must own exactly one cluster within its bound, and no cluster may be unowned.
    void ExamineRoots(const std::vector<TrueRoot>& Distinct, const BernsteinPolynomial& Polynomial, bool DerivativeAssist, RootFamilyOutcome& Outcome)
    {
        const RootIsolation Isolation = IsolateBernsteinRoots(Polynomial, 1.0e-13);
        Outcome.Clips += Isolation.ClipSteps;
        Outcome.Subdivisions += Isolation.Subdivisions;
        std::vector<double> Found = Isolation.Roots;
        if (DerivativeAssist)
        {
            const RootIsolation Slopes = IsolateBernsteinRoots(DifferentiateBernstein(Polynomial), 1.0e-13);
            BernsteinPolynomial Magnitude = Polynomial;
            for (double& Coefficient : Magnitude.Coefficients) Coefficient = std::fabs(Coefficient);
            for (double Root : Slopes.Roots)
            {
                if (std::fabs(EvaluateBernstein(Polynomial, Root)) <= 64.0 * Polynomial.Degree() * 2.220446049250313e-16 * EvaluateBernstein(Magnitude, Root)) Found.push_back(Root);
            }
            std::sort(Found.begin(), Found.end());
        }
        std::vector<double> Bounds;
        double ClusterWidth = 1.0e-12;                                                       // [-] - Merge distance
        for (const TrueRoot& Root : Distinct)
        {
            Bounds.push_back(ConditionBound(Polynomial, Root));
            if (Root.Multiplicity > 1) ClusterWidth = std::max(ClusterWidth, 2.0 * Bounds.back());
        }
        std::vector<double> Clusters;
        for (double Root : Found) if (Clusters.empty() || Root - Clusters.back() > ClusterWidth) Clusters.push_back(Root);
        bool Wrong = false;
        std::vector<bool> Owned(Clusters.size(), false);
        for (size_t RootIndex = 0; RootIndex < Distinct.size(); ++RootIndex)
        {
            double Best = 1.0e300; size_t BestIndex = 0;
            for (size_t Index = 0; Index < Clusters.size(); ++Index)
            {
                if (std::fabs(Clusters[Index] - Distinct[RootIndex].Position) < Best) { Best = std::fabs(Clusters[Index] - Distinct[RootIndex].Position); BestIndex = Index; }
            }
            Outcome.WorstRatio = std::max(Outcome.WorstRatio, Best / Bounds[RootIndex]);
            if (Best > Bounds[RootIndex]) { Outcome.Missed += 1; Wrong = true; continue; }
            Owned[BestIndex] = true;
            Outcome.WorstError = std::max(Outcome.WorstError, Best);
        }
        for (bool Condition : Owned) if (!Condition) { Outcome.Spurious += 1; Wrong = true; }
        Outcome.Failures += Wrong ? 1 : 0;
    }

    void ValidateBernstein()
    {
        constexpr long Cases = 20000;                                                        // [-] - Polynomials per family
        const auto Family = [&](const char* Name, bool DerivativeAssist, const std::function<std::vector<double>(std::vector<TrueRoot>&)>& Draw)
        {
            const Clock::time_point Start = Clock::now();
            RootFamilyOutcome Outcome;
            for (long Case = 0; Case < Cases; ++Case)
            {
                std::vector<TrueRoot> Distinct;
                const std::vector<double> Roots = Draw(Distinct);
                std::sort(Distinct.begin(), Distinct.end(), [](const TrueRoot& A, const TrueRoot& B) { return A.Position < B.Position; });
                const double Scale = Uniform(0.5, 2.0) * (Uniform(0.0, 1.0) < 0.5 ? -1.0 : 1.0);
                ExamineRoots(Distinct, ConstructFromRoots(Roots, Scale), DerivativeAssist, Outcome);
            }
            Rows.push_back({ "E1", Name, Cases, Outcome.Failures,
                             Format("worst err %.1e (%.2f × bound) · missed %.0f", Outcome.WorstError, Outcome.WorstRatio, static_cast<double>(Outcome.Missed))
                                 + Format(" · spurious %.0f · %.1f clips", static_cast<double>(Outcome.Spurious), static_cast<double>(Outcome.Clips) / Cases)
                                 + Format(" + %.2f halvings / poly", static_cast<double>(Outcome.Subdivisions) / Cases),
                             "err ≤ condition bound", Elapsed(Start) });
        };
        // 📝 Roots drawn on [−0.2, 1.2] (outside ones must NOT be reported), kept ≥ 1e-6 from the ends and ≥ Gap apart.
        const auto DrawSeparated = [&](std::vector<TrueRoot>& Distinct, int Degree, double Gap)
        {
            std::vector<double> Roots;
            while (static_cast<int>(Roots.size()) < Degree)
            {
                const double Candidate = Uniform(-0.2, 1.2);
                if (std::fabs(Candidate) < 1.0e-6 || std::fabs(Candidate - 1.0) < 1.0e-6) continue;
                if (std::any_of(Roots.begin(), Roots.end(), [&](double Root) { return std::fabs(Root - Candidate) < Gap; })) continue;
                Roots.push_back(Candidate);
            }
            for (double Root : Roots) if (Root > 0.0 && Root < 1.0) Distinct.push_back({ Root, 1 });
            return Roots;
        };
        Family("Simple roots, degree 1–8, gap ≥ 1e-3", false, [&](std::vector<TrueRoot>& Distinct)
        {
            return DrawSeparated(Distinct, 1 + static_cast<int>(Generator() % 8), 1.0e-3);
        });
        Family("Close pair (gap 1e-5 … 1e-4) + simple, degree ≤ 8", false, [&](std::vector<TrueRoot>& Distinct)
        {
            std::vector<double> Roots = DrawSeparated(Distinct, static_cast<int>(Generator() % 6), 0.05);
            const double Centre = Uniform(0.05, 0.95), Gap = std::pow(10.0, Uniform(-5.0, -4.0));
            if (std::any_of(Roots.begin(), Roots.end(), [&](double Root) { return std::fabs(Root - Centre) < 0.02; })) return Roots;
            Roots.push_back(Centre - 0.5 * Gap); Roots.push_back(Centre + 0.5 * Gap);
            Distinct.push_back({ Centre - 0.5 * Gap, 1 }); Distinct.push_back({ Centre + 0.5 * Gap, 1 });
            return Roots;
        });
        Family("Double root + simple, degree ≤ 8, 𝑓 ∪ 𝑓′ roots", true, [&](std::vector<TrueRoot>& Distinct)
        {
            std::vector<double> Roots = DrawSeparated(Distinct, static_cast<int>(Generator() % 5), 0.05);
            const double Double = Uniform(0.05, 0.95);
            if (std::any_of(Roots.begin(), Roots.end(), [&](double Root) { return std::fabs(Root - Double) < 0.02; })) return Roots;
            Roots.push_back(Double); Roots.push_back(Double);
            Distinct.push_back({ Double, 2 });
            return Roots;
        });
    }

    // ---- E2 ----------------------------------------------------------------------------------------------------------

    struct CurveCase
    {
        const char*         Name     = "";                                                   // [-] - Row label
        CurveGeometry       Curve    = StraightLine{};                                       // [-] - Curve
        double              Low      = 0.0;                                                  // [-] - τ range
        double              High     = 0.0;                                                  // [-] - τ range
        SurfaceGeometry     Surface  = PlaneSurface{};                                       // [-] - Surface
        std::vector<double> Expected = {};                                                   // [-] - Exact τ values
        int                 Tangents = 0;                                                    // [-] - Of which tangent
        bool                CoincidentCondition = false;                                     // [-] - Curve lies in the surface
    };

    void ValidateCurveSurfaceTable()
    {
        const Clock::time_point Start = Clock::now();
        const StraightLine Axis{ { -5.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } };
        const StraightLine Raised{ { -5.0, 0.0, 0.5 }, { 1.0, 0.0, 0.0 } };
        const StraightLine Grazing{ { -5.0, 2.0, 0.0 }, { 1.0, 0.0, 0.0 } };
        const CircularArc  Ring{ {}, 2.0 };
        const std::vector<CurveCase> Cases =
        {
            { "line × plane",            Axis,    0.0, 10.0, PlaneSurface{ ConstructFrame({ 1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }) }, { 6.0 } },
            { "line × sphere",           Axis,    0.0, 10.0, SphereSurface{ {}, 2.0 },                                     { 3.0, 7.0 } },
            { "line × sphere (tangent)", Grazing, 0.0, 10.0, SphereSurface{ {}, 2.0 },                                     { 5.0 }, 1 },
            { "line × cylinder",         Axis,    0.0, 10.0, CylinderSurface{ {}, 1.5 },                                   { 3.5, 6.5 } },
            { "line × cone",             Raised,  0.0, 10.0, ConeSurface{ {}, 2.0, -1.0 },                                 { 3.5, 6.5 } },
            { "line × torus",            Axis,    0.0, 10.0, TorusSurface{ {}, 3.0, 1.0 },                                 { 1.0, 3.0, 7.0, 9.0 } },
            { "circle × plane",          Ring,    0.0, FullTurn, PlaneSurface{ ConstructFrame({ 1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }) }, { π / 3.0, 5.0 * π / 3.0 } },
            { "circle × cylinder",       Ring,    0.0, FullTurn, CylinderSurface{ ConstructFrame({}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }), 1.0 }, { π / 6.0, 5.0 * π / 6.0, 7.0 * π / 6.0, 11.0 * π / 6.0 } },
            { "circle × sphere",         Ring,    0.0, FullTurn, SphereSurface{ { { 2.0, 0.0, 0.0 } }, 2.0 },              { π / 3.0, 5.0 * π / 3.0 } },
            { "circle × torus (inside)", Ring,    0.0, FullTurn, TorusSurface{ {}, 3.0, 1.0 },                             {}, 0, true },
            { "circle × plane (inside)", Ring,    0.0, FullTurn, PlaneSurface{},                                           {}, 0, true },
            { "circle × cone (inside)",  Ring,    0.0, FullTurn, ConeSurface{ {}, 2.0, -1.0 },                             {}, 0, true },
        };
        long   Failures = 0;
        double Worst    = 0.0;                                                               // [-] - Worst τ error
        std::string Failed;
        for (const CurveCase& Case : Cases)
        {
            const CurveSurfaceOutcome Outcome = IntersectCurveSurface(Case.Curve, Case.Low, Case.High, Case.Surface);
            bool Wrong = Outcome.Crossings.size() != Case.Expected.size() || Case.CoincidentCondition != !Outcome.Coincidences.empty();
            int  Tangents = 0;
            for (size_t Index = 0; !Wrong && Index < Case.Expected.size(); ++Index)
            {
                const double Error = std::fabs(Outcome.Crossings[Index].CurveParameter - Case.Expected[Index]);
                Worst = std::max(Worst, Outcome.Crossings[Index].Category == CrossingCategory::Tangent ? 0.0 : Error);
                Tangents += Outcome.Crossings[Index].Category == CrossingCategory::Tangent ? 1 : 0;
                // ⚠️ A tangent root is located only to √precision (the distance is quadratic in τ there).
                Wrong = Wrong || Error > (Outcome.Crossings[Index].Category == CrossingCategory::Tangent ? 1.0e-4 : 1.0e-12);
            }
            Wrong = Wrong || Tangents != Case.Tangents;
            if (Wrong) { Failures += 1; Failed += std::string(" ") + Case.Name; }
        }
        Rows.push_back({ "E2", "Exact table: line/circle × plane, sphere, cylinder, cone, torus (incl. tangent, coincident)", static_cast<long>(Cases.size()), Failures,
                         Format("worst transversal τ error %.1e", Worst) + (Failed.empty() ? "" : " · failed:" + Failed), "τ ≤ 1e-12 (tangent 1e-4)", Elapsed(Start) });
    }

    [[nodiscard]] SurfaceGeometry RandomSurface(int Category)
    {
        const Vector3 Origin = RandomPoint(0.8);
        switch (Category)
        {
            case 0:  return PlaneSurface{ RandomFrame(Origin, RandomDirection()) };
            case 1:  return CylinderSurface{ RandomFrame(Origin, RandomDirection()), Uniform(0.3, 2.0) };
            case 2:  return ConeSurface{ RandomFrame(Origin, RandomDirection()), Uniform(0.5, 2.0), Uniform(-1.0, 1.0) };
            case 3:  return SphereSurface{ RandomFrame(Origin, RandomDirection()), Uniform(0.3, 2.0) };
            default:
            {
                const double Major = Uniform(1.0, 2.5);
                return TorusSurface{ RandomFrame(Origin, RandomDirection()), Major, Uniform(0.2, 0.9) * Major };
            }
        }
    }

    // 📝 Independent reference: sample the exact signed distance densely; every genuine sign change (both samples within
    //    two steps' travel of zero, which excludes the cone's nappe switch) must hold a reported crossing.
    void ValidateCurveSurfaceRandom()
    {
        const Clock::time_point Start = Clock::now();
        constexpr long Cases = 2000;                                                         // [-] - Random pairs
        constexpr int  Samples = 20000;                                                      // [-] - Reference samples per curve
        long   Failures = 0, SignChanges = 0, Missed = 0, Reported = 0;
        double WorstResidual = 0.0;                                                          // [len] - Worst |d| at a crossing
        for (long Case = 0; Case < Cases; ++Case)
        {
            const SurfaceGeometry Surface = RandomSurface(static_cast<int>(Case % 5));
            CurveGeometry Curve;
            double        Low = 0.0, High = FullTurn, Speed = 1.0;
            if (Case % 2 == 0) { Curve = StraightLine{ RandomPoint(0.5), RandomDirection() }; Low = -6.0; High = 6.0; }
            else { const double Radius = Uniform(0.3, 3.0); Curve = CircularArc{ RandomFrame(RandomPoint(0.8), RandomDirection()), Radius }; Speed = Radius; }
            const CurveSurfaceOutcome Outcome = IntersectCurveSurface(Curve, Low, High, Surface);
            if (!Outcome.Coincidences.empty()) continue;
            bool Wrong = false;
            for (const CurveSurfaceCrossing& Crossing : Outcome.Crossings)
            {
                const double Off = std::fabs(MeasureSurfaceDistance(Surface, EvaluateCurve(Curve, Crossing.CurveParameter).Position).Distance);
                WorstResidual = std::max(WorstResidual, Off);
                Wrong = Wrong || Off > SessionPrecision;
                Reported += 1;
            }
            const double Step = (High - Low) / Samples;
            double Previous = MeasureSurfaceDistance(Surface, EvaluateCurve(Curve, Low).Position).Distance;
            for (int Sample = 1; Sample <= Samples; ++Sample)
            {
                const double τ = Low + Step * Sample;
                const double Current = MeasureSurfaceDistance(Surface, EvaluateCurve(Curve, τ).Position).Distance;
                const bool   Change = (Previous < 0.0) != (Current < 0.0) && std::fabs(Previous) <= 2.0 * Speed * Step && std::fabs(Current) <= 2.0 * Speed * Step;
                if (Change)
                {
                    SignChanges += 1;
                    const bool Covered = std::any_of(Outcome.Crossings.begin(), Outcome.Crossings.end(), [&](const CurveSurfaceCrossing& Crossing)
                    {
                        return Crossing.CurveParameter >= τ - 2.0 * Step && Crossing.CurveParameter <= τ + Step;
                    });
                    if (!Covered) { Missed += 1; Wrong = true; }
                }
                Previous = Current;
            }
            Failures += Wrong ? 1 : 0;
        }
        Rows.push_back({ "E2", "Random line/circle × 5 surface kinds vs 20000-sample sign scan", Cases, Failures,
                         Format("%.0f sign changes, %.0f missed · ", static_cast<double>(SignChanges), static_cast<double>(Missed)) + Format("%.0f crossings, worst |d| %.1e", static_cast<double>(Reported), WorstResidual),
                         "0 missed · |d| ≤ 1e-8", Elapsed(Start) });
    }

    // ---- F1 / F2 -----------------------------------------------------------------------------------------------------

    struct ExpectedCircle
    {
        Vector3 Centre = {};                                                                 // [len] - Circle centre
        Vector3 Axis   = {};                                                                 // [-]   - Circle normal
        double  Radius = 0.0;                                                                // [len] - Radius
    };

    struct AnalyticPair
    {
        SurfaceGeometry             First     = PlaneSurface{};                              // [-] - First surface
        ParameterBox                FirstBox  = {};                                          // [-] - Its box
        SurfaceGeometry             Second    = PlaneSurface{};                              // [-] - Second surface
        ParameterBox                SecondBox = {};                                          // [-] - Its box
        std::vector<ExpectedCircle> Circles   = {};                                          // [-] - Exact circles
    };

    using PairDraw = std::function<AnalyticPair()>;

    [[nodiscard]] double CircleDistance(const ExpectedCircle& Circle, Vector3 Position)
    {
        const Vector3 Offset = Position - Circle.Centre;
        const double  Along  = Dot(Offset, Circle.Axis);
        return std::hypot(Length(Offset - Circle.Axis * Along) - Circle.Radius, Along);
    }

    [[nodiscard]] std::vector<std::pair<const char*, PairDraw>> AnalyticFamilies()
    {
        return {
            { "sphere × plane", []()
            {
                AnalyticPair Pair;
                const Vector3 Centre = RandomPoint(2.0), Normal = RandomDirection();
                const double  Radius = Uniform(0.5, 3.0), Height = Uniform(-0.95, 0.95) * Radius;
                Pair.First = SphereSurface{ RandomFrame(Centre, RandomDirection()), Radius }; Pair.FirstBox = WholeSphere;
                Pair.Second = PlaneSurface{ RandomFrame(Centre + Normal * Height, Normal) };   Pair.SecondBox = WidePlane;
                Pair.Circles = { { Centre + Normal * Height, Normal, std::sqrt(Radius * Radius - Height * Height) } };
                return Pair;
            } },
            { "sphere × sphere", []()
            {
                AnalyticPair Pair;
                const Vector3 Centre = RandomPoint(2.0), Direction = RandomDirection();
                const double  First = Uniform(0.5, 3.0), Second = Uniform(0.5, 3.0);
                const double  Separation = Uniform(std::fabs(First - Second) + 0.05, First + Second - 0.05);
                Pair.First = SphereSurface{ RandomFrame(Centre, RandomDirection()), First };                            Pair.FirstBox = WholeSphere;
                Pair.Second = SphereSurface{ RandomFrame(Centre + Direction * Separation, RandomDirection()), Second }; Pair.SecondBox = WholeSphere;
                const double Along = (Separation * Separation + First * First - Second * Second) / (2.0 * Separation);
                Pair.Circles = { { Centre + Direction * Along, Direction, std::sqrt(First * First - Along * Along) } };
                return Pair;
            } },
            { "torus × plane ⟂ axis", []()
            {
                AnalyticPair Pair;
                const OrthonormalFrame Frame = RandomFrame(RandomPoint(2.0), RandomDirection());
                const double Major = Uniform(1.5, 3.0), Minor = Uniform(0.3, 0.9) * Major, Height = Uniform(-0.9, 0.9) * Minor;
                const Vector3 Centre = Frame.Origin + Frame.ZAxis * Height;
                Pair.First = TorusSurface{ Frame, Major, Minor };            Pair.FirstBox = WholeTorus;
                Pair.Second = PlaneSurface{ RandomFrame(Centre, Frame.ZAxis * (Uniform(0.0, 1.0) < 0.5 ? -1.0 : 1.0)) }; Pair.SecondBox = WidePlane;
                const double Half = std::sqrt(Minor * Minor - Height * Height);
                Pair.Circles = { { Centre, Frame.ZAxis, Major - Half }, { Centre, Frame.ZAxis, Major + Half } };
                return Pair;
            } },
            { "cylinder × plane ⟂ axis", []()
            {
                AnalyticPair Pair;
                const OrthonormalFrame Frame = RandomFrame(RandomPoint(2.0), RandomDirection());
                const double Radius = Uniform(0.3, 3.0), Height = Uniform(-4.0, 4.0);
                Pair.First = CylinderSurface{ Frame, Radius };                                                Pair.FirstBox = TallCylinder;
                Pair.Second = PlaneSurface{ RandomFrame(Frame.Origin + Frame.ZAxis * Height, Frame.ZAxis) };  Pair.SecondBox = WidePlane;
                Pair.Circles = { { Frame.Origin + Frame.ZAxis * Height, Frame.ZAxis, Radius } };
                return Pair;
            } },
            { "cone × plane ⟂ axis", []()
            {
                AnalyticPair Pair;
                const OrthonormalFrame Frame = RandomFrame(RandomPoint(2.0), RandomDirection());
                const double OriginRadius = Uniform(1.0, 2.0), Slope = Uniform(-0.5, 0.5), Height = Uniform(-1.2, 1.2);
                Pair.First = ConeSurface{ Frame, OriginRadius, Slope };                                               Pair.FirstBox = { { 0.0, -1.5 }, { FullTurn, 1.5 } };
                Pair.Second = PlaneSurface{ RandomFrame(Frame.Origin + Frame.ZAxis * Height, Frame.ZAxis) };  Pair.SecondBox = WidePlane;
                Pair.Circles = { { Frame.Origin + Frame.ZAxis * Height, Frame.ZAxis, OriginRadius + Slope * Height } };
                return Pair;
            } },
            { "cylinder × coaxial sphere", []()
            {
                AnalyticPair Pair;
                const OrthonormalFrame Frame = RandomFrame(RandomPoint(2.0), RandomDirection());
                const double Radius = Uniform(0.3, 2.0), Sphere = Radius + Uniform(0.1, 2.0), Shift = Uniform(-1.0, 1.0);
                const Vector3 Centre = Frame.Origin + Frame.ZAxis * Shift;
                Pair.First = CylinderSurface{ Frame, Radius };                                 Pair.FirstBox = TallCylinder;
                Pair.Second = SphereSurface{ RandomFrame(Centre, RandomDirection()), Sphere }; Pair.SecondBox = WholeSphere;
                const double Half = std::sqrt(Sphere * Sphere - Radius * Radius);
                Pair.Circles = { { Centre - Frame.ZAxis * Half, Frame.ZAxis, Radius }, { Centre + Frame.ZAxis * Half, Frame.ZAxis, Radius } };
                return Pair;
            } },
        };
    }

    void ValidateAnalytic()
    {
        constexpr long Cases = 500;                                                          // [-] - Random cases per family
        for (const auto& [Name, Draw] : AnalyticFamilies())
        {
            const Clock::time_point Start = Clock::now();
            long   Failures = 0, NotAnalytic = 0;
            double Worst = 0.0;                                                              // [len] - Worst circle parameter error
            for (long Case = 0; Case < Cases; ++Case)
            {
                const AnalyticPair Pair = Draw();
                const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(Pair.First, Pair.FirstBox, Pair.Second, Pair.SecondBox);
                bool Wrong = Outcome.Branches.size() != Pair.Circles.size();
                NotAnalytic += Outcome.Method.rfind("Analytic", 0) == 0 ? 0 : 1;
                Wrong = Wrong || Outcome.Method.rfind("Analytic", 0) != 0;
                for (const ExpectedCircle& Circle : Pair.Circles)
                {
                    double Best = 1.0e300;
                    for (const IntersectionBranch& Branch : Outcome.Branches)
                    {
                        if (const CircularArc* Arc = std::get_if<CircularArc>(&Branch.Curve))
                        {
                            const double Error = std::max({ Distance(Arc->Frame.Origin, Circle.Centre), std::fabs(Arc->Radius - Circle.Radius), Length(Cross(Arc->Frame.ZAxis, Circle.Axis)) });
                            Best = std::min(Best, Error);
                        }
                    }
                    Worst = std::max(Worst, Best);
                    Wrong = Wrong || Best > 1.0e-11;
                }
                Failures += Wrong ? 1 : 0;
            }
            Rows.push_back({ "F1", std::string("Analytic ") + Name + " (random frames)", Cases, Failures,
                             Format("worst centre/radius/axis error %.1e · %.0f not analytic", Worst, static_cast<double>(NotAnalytic)), "≤ 1e-11", Elapsed(Start) });
        }
        // ---- plane × plane: exact line
        const Clock::time_point Start = Clock::now();
        long   Failures = 0;
        double Worst = 0.0;                                                                  // [-] - Worst direction / offset error
        for (long Case = 0; Case < Cases; ++Case)
        {
            const Vector3 First = RandomDirection();
            Vector3 Second = RandomDirection();
            while (Length(Cross(First, Second)) < 0.1) Second = RandomDirection();
            const PlaneSurface A{ RandomFrame(RandomPoint(1.0), First) }, B{ RandomFrame(RandomPoint(1.0), Second) };
            const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(A, WidePlane, B, WidePlane);
            const StraightLine* Line = Outcome.Branches.size() == 1 ? std::get_if<StraightLine>(&Outcome.Branches.front().Curve) : nullptr;
            if (Line == nullptr) { Failures += 1; continue; }
            const double Error = std::max({ Length(Cross(Line->Direction, Normalize(Cross(First, Second)))),
                                            std::fabs(Dot(Line->Origin - A.Frame.Origin, First)), std::fabs(Dot(Line->Origin - B.Frame.Origin, Second)) });
            Worst = std::max(Worst, Error);
            Failures += Error > 1.0e-12 ? 1 : 0;
        }
        Rows.push_back({ "F1", "Analytic plane × plane (random)", Cases, Failures, Format("worst direction/offset error %.1e", Worst), "≤ 1e-12", Elapsed(Start) });
    }

    void ValidateMarchingAgainstAnalytic()
    {
        constexpr long Cases = 100;                                                          // [-] - Random cases per family
        SurfaceIntersectionSettings Marching;
        Marching.Policy = IntersectionPolicy::MarchingOnly;
        for (const auto& [Name, Draw] : AnalyticFamilies())
        {
            const Clock::time_point Start = Clock::now();
            long   Failures = 0;
            double WorstOff = 0.0, WorstGap = 0.0, WorstLength = 0.0;                        // [len] / [len] / [-]
            for (long Case = 0; Case < Cases; ++Case)
            {
                const AnalyticPair Pair = Draw();
                const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(Pair.First, Pair.FirstBox, Pair.Second, Pair.SecondBox, Marching);
                bool Wrong = Outcome.Branches.size() != Pair.Circles.size();
                double Measured = 0.0, Exact = 0.0;                                          // [len] - Total lengths
                // 📝 Every marched point on an exact circle; every exact-circle sample near the marched polylines.
                for (const IntersectionBranch& Branch : Outcome.Branches)
                {
                    for (size_t Index = 0; Index < Branch.Chart.size(); ++Index)
                    {
                        double Off = 1.0e300;
                        for (const ExpectedCircle& Circle : Pair.Circles) Off = std::min(Off, CircleDistance(Circle, Branch.Chart[Index].Position));
                        WorstOff = std::max(WorstOff, Off);
                        Wrong = Wrong || Off > 1.0e-9;
                        const size_t Next = (Index + 1) % Branch.Chart.size();
                        if (Next != 0 || Branch.ClosedCondition) Measured += Distance(Branch.Chart[Index].Position, Branch.Chart[Next].Position);
                    }
                }
                for (const ExpectedCircle& Circle : Pair.Circles)
                {
                    Exact += FullTurn * Circle.Radius;
                    const OrthonormalFrame Frame = ConstructFrame(Circle.Centre, Circle.Axis, std::fabs(Circle.Axis.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 });
                    for (int Sample = 0; Sample < 256; ++Sample)
                    {
                        const double  Angle = FullTurn * Sample / 256.0;
                        const Vector3 Point = Circle.Centre + (Frame.XAxis * std::cos(Angle) + Frame.YAxis * std::sin(Angle)) * Circle.Radius;
                        double Gap = 1.0e300;
                        for (const IntersectionBranch& Branch : Outcome.Branches)
                        {
                            const auto& Chart = Branch.Chart;
                            for (size_t Index = 0; Index < Chart.size(); ++Index)
                            {
                                const size_t Next = (Index + 1) % Chart.size();
                                if (Next == 0 && !Branch.ClosedCondition) continue;
                                const Vector3 Span = Chart[Next].Position - Chart[Index].Position;
                                const double  λ = std::clamp(Dot(Point - Chart[Index].Position, Span) / std::max(Dot(Span, Span), 1.0e-300), 0.0, 1.0);
                                Gap = std::min(Gap, Distance(Chart[Index].Position + Span * λ, Point));
                            }
                        }
                        WorstGap = std::max(WorstGap, Gap);
                        Wrong = Wrong || Gap > 1.5 * Marching.ChordTolerance;
                    }
                }
                WorstLength = std::max(WorstLength, std::fabs(Measured - Exact) / Exact);
                Failures += Wrong ? 1 : 0;
            }
            Rows.push_back({ "F2", std::string("MarchingOnly ") + Name + " vs exact circles", Cases, Failures,
                             Format("on-curve %.1e · coverage gap %.1e · length rel %.1e", WorstOff, WorstGap, WorstLength), "on ≤ 1e-9 · gap ≤ 1.5e-3", Elapsed(Start) });
        }
    }

    // ---- F3 ----------------------------------------------------------------------------------------------------------

    void ValidateGallery()
    {
        for (const char* Policy : { "0", "1" })
        {
            const Clock::time_point Start = Clock::now();
            long        Failures = 0, Cases = 0;
            std::string Failed;
            double      WorstLength = 0.0;                                                   // [-] - Worst relative length error
            for (const std::string& Name : EnumerateIntersectionScenes())
            {
                SpecimenParameters Parameters;
                Parameters.Values["MarchingOnly"] = Policy[0] == '1' ? 1.0 : 0.0;
                const IntersectionScene Scene = ConstructIntersectionScene(Name, Parameters);
                const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(Scene.First, Scene.FirstBox, Scene.Second, Scene.SecondBox, Scene.Settings);
                const IntersectionAuditReport Audit = AuditIntersection(Scene, Outcome);
                Cases += 1;
                if (std::isfinite(Audit.LengthError)) WorstLength = std::max(WorstLength, Audit.LengthError);
                if (!Audit.SoundCondition()) { Failures += 1; Failed += " " + Name; }
            }
            Rows.push_back({ "F3", std::string("Scene gallery through the audit (I1–I6), ") + (Policy[0] == '1' ? "MarchingOnly" : "Hybrid"), Cases, Failures,
                             Format("worst length rel %.1e", WorstLength) + (Failed.empty() ? "" : " · unsound:" + Failed), "all sound", Elapsed(Start) });
        }
        // ---- negative control: the audit must catch a loop the solver is told not to look for
        const Clock::time_point Start = Clock::now();
        SpecimenParameters Blind;
        Blind.Values["LoopDetection"] = 0.0;
        const IntersectionScene Scene = ConstructIntersectionScene("SmallLoop", Blind);
        const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(Scene.First, Scene.FirstBox, Scene.Second, Scene.SecondBox, Scene.Settings);
        const IntersectionAuditReport Audit = AuditIntersection(Scene, Outcome);
        Rows.push_back({ "F3", "Negative control: SmallLoop with LoopDetection = 0 must be flagged", 1, Audit.SoundCondition() ? 1 : 0,
                         Format("%.0f of %.0f reference crossings missed", Audit.ReferenceMisses, Audit.ReferencePoints), "audit unsound", Elapsed(Start) });
    }

    // 📝 Random oblique pairs (no closed form): the dense reference lattice is the only oracle — I1–I4 must hold.
    void ValidateRandomOblique()
    {
        const Clock::time_point Start = Clock::now();
        constexpr long Cases = 300;                                                          // [-] - Random pairs
        long   Failures = 0, Branches = 0, References = 0;
        double WorstResidual = 0.0, WorstGap = 0.0;                                          // [len]
        std::string FirstFailure;
        for (long Case = 0; Case < Cases; ++Case)
        {
            IntersectionScene Scene;
            const int Kind = static_cast<int>(Case % 3);
            if (Kind == 0)
            {
                Scene.First = CylinderSurface{ RandomFrame(RandomPoint(0.3), RandomDirection()), Uniform(0.5, 1.5) };  Scene.FirstBox = { { 0.0, -3.0 }, { FullTurn, 3.0 } };
                Scene.Second = CylinderSurface{ RandomFrame(RandomPoint(0.3), RandomDirection()), Uniform(0.5, 1.5) }; Scene.SecondBox = { { 0.0, -3.0 }, { FullTurn, 3.0 } };
            }
            else if (Kind == 1)
            {
                Scene.First = SphereSurface{ RandomFrame(RandomPoint(0.5), RandomDirection()), Uniform(1.0, 2.0) };   Scene.FirstBox = WholeSphere;
                Scene.Second = ConeSurface{ RandomFrame(RandomPoint(0.5), RandomDirection()), Uniform(0.6, 1.2), Uniform(-0.3, 0.3) }; Scene.SecondBox = { { 0.0, -1.5 }, { FullTurn, 1.5 } };
            }
            else
            {
                Scene.First = TorusSurface{ RandomFrame(RandomPoint(0.3), RandomDirection()), Uniform(1.5, 2.5), Uniform(0.3, 0.8) }; Scene.FirstBox = WholeTorus;
                Scene.Second = CylinderSurface{ RandomFrame(RandomPoint(0.8), RandomDirection()), Uniform(0.3, 1.2) };                  Scene.SecondBox = { { 0.0, -4.0 }, { FullTurn, 4.0 } };
            }
            const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(Scene.First, Scene.FirstBox, Scene.Second, Scene.SecondBox, Scene.Settings);
            const IntersectionAuditReport Audit = AuditIntersection(Scene, Outcome);
            Branches += static_cast<long>(Outcome.Branches.size());
            References += Audit.ReferencePoints;
            WorstResidual = std::max(WorstResidual, Audit.MaximumResidual);
            if (Audit.ReferencePoints > 0) WorstGap = std::max(WorstGap, Audit.ReferenceReach);
            if (!Audit.SoundCondition())
            {
                Failures += 1;
                if (FirstFailure.empty()) FirstFailure = " · first: case " + std::to_string(Case) + " " + Audit.Findings.front().Code + " " + Audit.Findings.front().Detail;
            }
        }
        Rows.push_back({ "F3", "Random oblique cylinder×cylinder, sphere×cone, torus×cylinder through the audit", Cases, Failures,
                         Format("%.0f branches · %.0f reference crossings · ", static_cast<double>(Branches), static_cast<double>(References)) + Format("residual %.1e · worst gap %.1e", WorstResidual, WorstGap) + FirstFailure,
                         "I1–I4 sound", Elapsed(Start) });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ValidateBernstein();
    ValidateCurveSurfaceTable();
    ValidateCurveSurfaceRandom();
    ValidateAnalytic();
    ValidateMarchingAgainstAnalytic();
    ValidateGallery();
    ValidateRandomOblique();

    long FailingRows = 0;
    std::printf("# GeometryProcessing — IntersectionValidation\n\n");
    std::printf("| | Gate | Family | Cases | Failures | Measured | Allowed | ms |\n|---|---|---|---|---|---|---|---|\n");
    for (const GateRow& Row : Rows)
    {
        FailingRows += Row.Failures > 0 ? 1 : 0;
        std::printf("| %s | %s | %s | %ld | %ld | %s | %s | %.0f |\n", Row.Failures == 0 ? "🟢" : "🔴", Row.Gate.c_str(), Row.Family.c_str(), Row.Cases, Row.Failures, Row.Measured.c_str(), Row.Allowed.c_str(), Row.Milliseconds);
    }
    std::printf("\n**%s** — %ld failing row(s)\n", FailingRows == 0 ? "PASS" : "FAIL", FailingRows);
    return FailingRows == 0 ? 0 : 1;
}
