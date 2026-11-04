/*====================================================================================================================================
                                                       SURFACINGVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Vehicle plan V2 + V4: curve fitting (tolerance, fairness, exact recovery, corners) and loft (interpolation, exact quadrics, sheet, bonnet)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Feature/FeatureTree.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Surfacing/CurveFitting.h"
#include "Engine/GeometryProcessing/Surfacing/SectionLoft.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double FullTurn  = 2.0 * std::numbers::pi;                                     // [rad] - 2π
    constexpr double Tolerance = 2.0e-4;                                                     // [len] - Plan fit tolerance (0.2 mm)
    constexpr double PanelTarget = 5.0e-4;                                                   // [len] - Plan V4 deviation target (0.5 mm)

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - S1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    void Tally(GateTally& Gate, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
    }

    GateTally ToleranceGate   = { "S1", "Fit: every input point within the tolerance of the curve (noise 0 / 0.05 / 0.1 mm)", "≤ 0.2 mm" };
    GateTally FairnessGate    = { "S2", "Fit: fitted curve vs the true curve stays within tolerance + noise amplitude; poles ≤ points / 2", "≤ 0.2 mm + a" };
    GateTally RecoveryGate    = { "S3", "Exact recovery: B-spline samples at their own parameters → poles; Bézier samples with parameter correction → curve", "≤ 10⁻¹²·L · ≤ 10⁻⁹" };
    GateTally CornerGate      = { "S4", "Corners split exactly where seeded (90°, 45°); runs share their joint points bit-exactly", "exact" };
    GateTally RefusalGate     = { "S5", "Refusals name the reason (and the worst point for a span-limit refusal)", "all named" };
    GateTally SectionGate     = { "L1", "Loft interpolates every section: S(𝑢, 𝑣ₖ) vs the merged section, merged vs the original (knot insertion)", "≤ 10⁻⁹" };
    GateTally QuadricGate     = { "L2", "Lofted circles reproduce a cylinder and a cone, two lines a bilinear patch (41 × 41 samples)", "≤ 10⁻¹²" };
    GateTally SheetGate       = { "L3", "Sheet body: audit sound · 1 face · every unpaired side on the laminar rim · tessellation conforming · area (quadrature) vs closed form", "rel. ≤ 10⁻⁹" };
    GateTally PanelGate       = { "L4", "Synthetic bonnet: 6 fitted sections lofted; deviation from all limit points and from the analytic surface", "≤ 0.5 mm" };
    GateTally TreeGate        = { "L6", "Feature tree: 6 SpaceCurve steps + Loft regenerate; a pole edit moves the panel locally; names persist; edits beyond the poles and sheet blends refused", "all hold" };
    GateTally DeterminismGate = { "L5", "Same input twice → bit-identical fitted poles and loft poles", "equal" };

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SPECIMENS
    //--------------------------------------------------------------------------------------------------------------------

    struct CurveSpecimen
    {
        std::string                    Name   = {};
        std::function<Vector3(double)> Path   = {};                                          // True curve on s ∈ [0, 1]
        int                            Points = 0;
    };

    [[nodiscard]] std::vector<CurveSpecimen> CurveCorpus()
    {
        return {
            { "Shoulder line (3D)", [](double S) { return Vector3{ 0.80 + 0.03 * std::sin(std::numbers::pi * S), 2.2 * S - 0.4, 0.90 + 0.05 * std::sin(std::numbers::pi * S) - 0.02 * S * S }; }, 60 },
            { "Wheel arch (semicircle r 0.36)", [](double S) { const double θ = std::numbers::pi * S; return Vector3{ 0.9, 1.3 + 0.36 * std::cos(θ), 0.33 + 0.36 * std::sin(θ) }; }, 40 },
            { "Helix (1 turn, r 0.3)", [](double S) { const double θ = FullTurn * S; return Vector3{ 0.3 * std::cos(θ), 0.3 * std::sin(θ), 0.4 * S }; }, 120 },
            { "Roof S-curve (planar)", [](double S) { return Vector3{ 0.0, 1.6 * S, 1.35 + 0.04 * std::tanh(6.0 * (1.6 * S - 0.8)) }; }, 80 },
        };
    }

    // Quad-model spacing: irregular but monotone samples, ends exact.
    [[nodiscard]] std::vector<double> IrregularStations(int Count)
    {
        std::vector<double> Stations(Count);
        for (int Index = 0; Index < Count; ++Index) Stations[Index] = (Index + 0.3 * std::sin(2.3 * Index)) / (Count - 1);
        Stations.front() = 0.0;
        Stations.back()  = 1.0;
        return Stations;
    }

    // Bounded noise: each coordinate uniform in ±a/√3, so |noise| ≤ a.
    [[nodiscard]] Vector3 Jitter(std::mt19937_64& Generator, double Amplitude)
    {
        std::uniform_real_distribution<double> Uniform(-1.0, 1.0);
        const double Scale = Amplitude / std::sqrt(3.0);
        return { Scale * Uniform(Generator), Scale * Uniform(Generator), Scale * Uniform(Generator) };
    }

    // Distance from each fitted sample to a dense polyline of the true curve (walking window; the path is monotone).
    [[nodiscard]] double MeasureTrueGap(const NurbsCurveDefinition& Curve, const std::function<Vector3(double)>& Path)
    {
        constexpr int Dense = 40000, Samples = 2000, Window = 400;
        std::vector<Vector3> Polyline(Dense + 1);
        for (int Index = 0; Index <= Dense; ++Index) Polyline[Index] = Path(static_cast<double>(Index) / Dense);
        double Worst = 0.0;
        int    Cursor = 0;
        for (int Sample = 0; Sample <= Samples; ++Sample)
        {
            const Vector3 Point = EvaluateNurbsCurveDerivatives(Curve, static_cast<double>(Sample) / Samples).Position;
            double Best = 1.0e300;
            int    BestIndex = Cursor;
            for (int Index = std::max(0, Cursor - Window); Index < std::min(Dense, Cursor + Window); ++Index)
            {
                const Vector3 Start = Polyline[Index], Span = Polyline[Index + 1] - Start;
                const double  Fraction = std::clamp(Dot(Point - Start, Span) / Dot(Span, Span), 0.0, 1.0);
                const double  Gap = Length(Point - (Start + Span * Fraction));
                if (Gap < Best) { Best = Gap; BestIndex = Index; }
            }
            Cursor = BestIndex;
            Worst  = std::max(Worst, Best);
        }
        return Worst;
    }

    [[nodiscard]] NurbsCurve CircleSection(double Height, double Radius)
    {
        const ConicForm Circle = { { 0.0, 0.0, Height }, { Radius, 0.0, 0.0 }, { 0.0, Radius, 0.0 }, { 0.0, 0.0, 1.0 } };
        return ConvertConicToNurbs(Circle, 0.0, FullTurn);
    }

    // 📝 Synthetic bonnet (half, mirror plane x = 0): ξ = x / 0.75 across, η = y / 1.1 along,
    //    H(x, y) = 0.78 + 0.16·η − 0.06·η² − 0.085·ξ² + 0.012·sin(πη)·(1 − ξ²).
    [[nodiscard]] double BonnetHeight(double X, double Y)
    {
        const double ξ = X / 0.75, η = Y / 1.1;
        return 0.78 + 0.16 * η - 0.06 * η * η - 0.085 * ξ * ξ + 0.012 * std::sin(std::numbers::pi * η) * (1.0 - ξ * ξ);
    }

    [[nodiscard]] Vector3 BonnetGradient(double X, double Y)
    {
        const double ξ = X / 0.75, η = Y / 1.1;
        const double Across = (-0.17 * ξ - 0.024 * ξ * std::sin(std::numbers::pi * η)) / 0.75;
        const double Along  = (0.16 - 0.12 * η + 0.012 * std::numbers::pi * std::cos(std::numbers::pi * η) * (1.0 - ξ * ξ)) / 1.1;
        return { Across, Along, 0.0 };
    }

    // One exported loop at η: 26 limit points across the half width, quad spacing, 0.05 mm noise, x = 0 snapped.
    [[nodiscard]] std::vector<Vector3> BonnetLoop(double η, std::mt19937_64& Generator)
    {
        std::vector<Vector3> Points;
        for (double Station : IrregularStations(26))
        {
            const double X = 0.75 * Station, Y = 1.1 * η;
            Vector3 Point = Vector3{ X, Y, BonnetHeight(X, Y) } + Jitter(Generator, 5.0e-5);
            if (Station == 0.0) Point.X = 0.0;                                               // Mirror snap
            Points.push_back(Point);
        }
        return Points;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                MEASURES
    //--------------------------------------------------------------------------------------------------------------------

    // Area ∬ |S𝑢 × S𝑣| d𝑢 d𝑣 by composite 8-point Gauss–Legendre on every knot rectangle.
    [[nodiscard]] double IntegrateSurfaceArea(const NurbsSurface& Surface)
    {
        constexpr std::array<double, 8> Abscissae = { -0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                                                       0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363 };
        constexpr std::array<double, 8> Weights   = {  0.1012285362903763,  0.2223810344533745,  0.3137066458778873,  0.3626837833783620,
                                                       0.3626837833783620,  0.3137066458778873,  0.2223810344533745,  0.1012285362903763 };
        auto Breaks = [](const std::vector<double>& Knots)
        {
            std::vector<double> Values(Knots.begin(), Knots.end());
            Values.erase(std::unique(Values.begin(), Values.end()), Values.end());
            return Values;
        };
        const std::vector<double> BreaksU = Breaks(Surface.Definition->KnotsU), BreaksV = Breaks(Surface.Definition->KnotsV);
        const SurfaceGeometry Geometry = Surface;
        double Area = 0.0;
        for (size_t SpanU = 0; SpanU + 1 < BreaksU.size(); ++SpanU)
        {
            for (size_t SpanV = 0; SpanV + 1 < BreaksV.size(); ++SpanV)
            {
                const double HalfU = 0.5 * (BreaksU[SpanU + 1] - BreaksU[SpanU]), HalfV = 0.5 * (BreaksV[SpanV + 1] - BreaksV[SpanV]);
                for (size_t First = 0; First < Abscissae.size(); ++First)
                {
                    for (size_t Second = 0; Second < Abscissae.size(); ++Second)
                    {
                        const Vector2 Parameter = { BreaksU[SpanU] + HalfU * (1.0 + Abscissae[First]), BreaksV[SpanV] + HalfV * (1.0 + Abscissae[Second]) };
                        const SurfaceSample Sample = EvaluateSurface(Geometry, Parameter);
                        Area += Weights[First] * Weights[Second] * HalfU * HalfV * Length(Cross(Sample.DerivativeU, Sample.DerivativeV));
                    }
                }
            }
        }
        return Area;
    }

    // max over 200 𝑢 of |S(𝑢, 𝑣ₖ) − merged section k|, and merged vs original (either direction when aligned by reversal).
    void MeasureSections(const std::vector<NurbsCurve>& Originals, const LoftSurfaceOutcome& Loft, double& SurfaceGap, double& MergeGap)
    {
        const SurfaceGeometry Geometry = Loft.Surface;
        SurfaceGap = 0.0;
        MergeGap   = 0.0;
        for (size_t Section = 0; Section < Originals.size(); ++Section)
        {
            const NurbsCurveDefinition& Merged = *Loft.Sections[Section].Definition;
            const NurbsCurveDefinition& Original = *Originals[Section].Definition;
            const NurbsDomain Domain = MeasureNurbsCurveDomain(Original);
            double Forward = 0.0, Backward = 0.0;
            for (int Sample = 0; Sample <= 200; ++Sample)
            {
                const double U = Sample / 200.0;
                const Vector3 OnMerged = EvaluateNurbsCurveDerivatives(Merged, U).Position;
                SurfaceGap = std::max(SurfaceGap, Length(EvaluateSurface(Geometry, { U, Loft.SectionParameters[Section] }).Position - OnMerged));
                Forward  = std::max(Forward,  Length(EvaluateNurbsCurveDerivatives(Original, Domain.Low + U * (Domain.High - Domain.Low)).Position - OnMerged));
                Backward = std::max(Backward, Length(EvaluateNurbsCurveDerivatives(Original, Domain.High - U * (Domain.High - Domain.Low)).Position - OnMerged));
            }
            MergeGap = std::max(MergeGap, std::min(Forward, Backward));
        }
    }

    struct FitRow
    {
        std::string Name;
        double      Noise = 0.0, Worst = 0.0, Mean = 0.0, TrueGap = 0.0, Fairing = 0.0;
        int         Points = 0, Poles = 0, Spans = 0, Extrema = 0;
        bool        PassedCondition = true;
        std::string Problem;
    };

    struct LoftRow
    {
        std::string Name;
        int         Sections = 0, PolesU = 0, PolesV = 0, Edges = 0;
        double      SurfaceGap = 0.0, MergeGap = 0.0, Analytic = -1.0, Area = 0.0, AreaError = -1.0;
        bool        PassedCondition = true;
        std::string Problem;
    };

    // Negative values mean "not measured" and print as a dash.
    [[nodiscard]] std::string Scientific(double Value)
    {
        if (Value < 0.0) return "—";
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.1e", Value);
        return Text;
    }

    struct RefusalRow { std::string Name, Expected, Measured; bool PassedCondition = false; };
    std::vector<RefusalRow> RefusalRows;

    void ExpectRefusal(const std::string& Name, const std::string& Measured, const std::string& Expected)
    {
        const bool Passed = Measured.find(Expected) != std::string::npos;
        RefusalRows.push_back({ Name, Expected, Measured.empty() ? "(built)" : Measured, Passed });
        Tally(RefusalGate, Passed ? 0.0 : 1.0, Passed);
    }

    [[nodiscard]] std::vector<NurbsCurve> BonnetSections(std::mt19937_64& Generator, std::vector<std::vector<Vector3>>& Loops, std::vector<CurveFitOutcome>& Fits)
    {
        std::vector<NurbsCurve> Sections;
        for (double η : { 0.0, 0.17, 0.38, 0.6, 0.81, 1.0 })
        {
            Loops.push_back(BonnetLoop(η, Generator));
            CurveFitSettings Settings;
            Fits.push_back(FitSpaceCurve(Loops.back(), Settings));
            Sections.push_back(Fits.back().Curve);
        }
        return Sections;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    // ---- S1 S2 over the curve corpus at three noise amplitudes.
    std::vector<FitRow> FitRows;
    std::mt19937_64 Generator(0x5EC710A5ULL);
    for (const CurveSpecimen& Specimen : CurveCorpus())
    {
        for (double Noise : { 0.0, 5.0e-5, 1.0e-4 })
        {
            FitRow Row;
            Row.Name  = Specimen.Name;
            Row.Noise = Noise;
            std::vector<Vector3> Points;
            for (double Station : IrregularStations(Specimen.Points)) Points.push_back(Specimen.Path(Station) + Jitter(Generator, Noise));
            const CurveFitOutcome Fit = FitSpaceCurve(Points, CurveFitSettings{});
            Row.Points = static_cast<int>(Points.size());
            if (!Fit.Refusal.empty()) { Row.PassedCondition = false; Row.Problem = Fit.Refusal; Tally(ToleranceGate, 1.0, false); FitRows.push_back(Row); continue; }
            Row.Worst   = Fit.WorstDeviation;
            Row.Mean    = Fit.MeanDeviation;
            Row.Poles   = static_cast<int>(Fit.Curve.Definition->Poles.size());
            Row.Spans   = Fit.Spans;
            Row.Extrema = Fit.Extrema;
            Row.Fairing = Fit.Fairing;
            Row.TrueGap = MeasureTrueGap(*Fit.Curve.Definition, Specimen.Path);
            const bool Within = Row.Worst <= Tolerance;
            const bool Fair   = Row.TrueGap <= Tolerance + Noise && Row.Poles * 2 <= Row.Points;
            Tally(ToleranceGate, Row.Worst, Within);
            Tally(FairnessGate, Row.TrueGap, Fair);
            Row.PassedCondition = Within && Fair;
            if (!Fair) Row.Problem = "true-curve gap or pole count";
            FitRows.push_back(Row);
        }
    }

    // ---- S3 exact recovery.
    struct RecoveryRow { std::string Name; double Error = 0.0; std::string Limit; bool PassedCondition = false; };
    std::vector<RecoveryRow> RecoveryRows;
    {
        // (a) Cubic B-spline, non-uniform knots, 8 poles: samples at its own parameters give back the poles.
        std::mt19937_64 Local(0xB5B11E5ULL);
        std::uniform_real_distribution<double> Uniform(-1.0, 1.0);
        NurbsCurveDefinition Truth;
        Truth.Degree = 3;
        Truth.Knots  = { 0.0, 0.0, 0.0, 0.0, 0.15, 0.4, 0.55, 0.8, 1.0, 1.0, 1.0, 1.0 };
        for (int Index = 0; Index < 8; ++Index) Truth.Poles.push_back({ Uniform(Local), Uniform(Local), Uniform(Local) });
        Truth.Weights.assign(8, 1.0);
        std::vector<Vector3> Points;
        std::vector<double>  Parameters = IrregularStations(50);
        for (double Parameter : Parameters) Points.push_back(EvaluateNurbsCurveDerivatives(Truth, Parameter).Position);
        const CurveFitOutcome Fit = FitSpaceCurveOnKnots(Points, Parameters, 3, Truth.Knots, 0.0);
        double Error = Fit.Refusal.empty() ? 0.0 : 1.0;
        if (Fit.Refusal.empty()) for (int Index = 0; Index < 8; ++Index) Error = std::max(Error, Length(Fit.Curve.Definition->Poles[Index] - Truth.Poles[Index]) / 2.0);
        RecoveryRows.push_back({ "B-spline (8 poles, knots 0.15 · 0.4 · 0.55 · 0.8) from 50 samples at its parameters: max pole error / L", Error, "≤ 10⁻¹²", Error <= 1.0e-12 });

        // (b) Cubic Bézier sampled at arbitrary spacing: centripetal start + foot-point correction converge onto it.
        NurbsCurveDefinition Bezier;
        Bezier.Degree  = 3;
        Bezier.Knots   = { 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0 };
        Bezier.Poles   = { { 0.0, 0.0, 0.0 }, { 0.3, 0.5, 0.1 }, { 0.8, -0.2, 0.3 }, { 1.0, 0.4, 0.0 } };
        Bezier.Weights = { 1.0, 1.0, 1.0, 1.0 };
        std::vector<Vector3> BezierPoints;
        for (double Parameter : IrregularStations(40)) BezierPoints.push_back(EvaluateNurbsCurveDerivatives(Bezier, Parameter * Parameter * (3.0 - 2.0 * Parameter)).Position);
        CurveFitSettings Settings;
        Settings.Tolerance = 1.0e-9; Settings.Fairing = 0.0; Settings.SpanLimit = 1; Settings.CorrectionRounds = 400;
        const CurveFitOutcome Refit = FitSpaceCurve(BezierPoints, Settings);
        double PoleError = 1.0;
        if (Refit.Refusal.empty()) { PoleError = 0.0; for (int Index = 0; Index < 4; ++Index) PoleError = std::max(PoleError, Length(Refit.Curve.Definition->Poles[Index] - Bezier.Poles[Index])); }
        RecoveryRows.push_back({ "Bézier from 40 samples (unknown parameters, 1 span): worst point deviation", Refit.Refusal.empty() ? Refit.WorstDeviation : 1.0, "≤ 10⁻⁹", Refit.Refusal.empty() && Refit.WorstDeviation <= 1.0e-9 });
        RecoveryRows.push_back({ "… and its poles vs the true poles", PoleError, "≤ 10⁻⁷", PoleError <= 1.0e-7 });
        for (const RecoveryRow& Row : RecoveryRows) Tally(RecoveryGate, Row.Error, Row.PassedCondition);
    }

    // ---- S4 corners: arc → 90° turn → straight → 45° kink → gentle curve.
    std::string CornerDetail;
    {
        std::vector<Vector3> Chain;
        for (int Index = 0; Index < 20; ++Index) { const double θ = 0.05 * Index; Chain.push_back({ std::sin(θ), 1.0 - std::cos(θ), 0.0 }); }
        const Vector3 Turn = Chain.back();
        const Vector3 Heading = Normalize(Vector3{ std::cos(0.95), std::sin(0.95), 0.0 });
        const Vector3 Across  = Normalize(Cross(Vector3{ 0.0, 0.0, 1.0 }, Heading));          // 90° turn
        for (int Index = 1; Index <= 15; ++Index) Chain.push_back(Turn + Across * (0.04 * Index));
        const Vector3 Kink = Chain.back();
        const Vector3 Onward = Normalize(Across * std::cos(std::numbers::pi / 4.0) + Vector3{ 0.0, 0.0, 1.0 } * std::sin(std::numbers::pi / 4.0));
        for (int Index = 1; Index <= 20; ++Index) Chain.push_back(Kink + Onward * (0.04 * Index) + Heading * (0.002 * Index * Index * 0.04));
        const std::vector<int> Corners = FindCurveCorners(Chain, CurveFitSettings{}.CornerAngle);
        const std::vector<CurveFitOutcome> Runs = FitSpaceCurveRuns(Chain, CurveFitSettings{});
        bool Joints = Runs.size() == 3;
        for (const CurveFitOutcome& Run : Runs) Joints = Joints && Run.Refusal.empty();
        auto Same = [](Vector3 First, Vector3 Second) { return First.X == Second.X && First.Y == Second.Y && First.Z == Second.Z; };
        if (Joints)
        {
            Joints = Same(Runs[0].Curve.Definition->Poles.back(), Chain[19]) && Same(Runs[1].Curve.Definition->Poles.front(), Chain[19])
                  && Same(Runs[1].Curve.Definition->Poles.back(), Chain[34]) && Same(Runs[2].Curve.Definition->Poles.front(), Chain[34]);
        }
        const bool Found = Corners == std::vector<int>{ 19, 34 };
        CornerDetail = "corners at";
        for (int Corner : Corners) CornerDetail += " " + std::to_string(Corner);
        CornerDetail += " (seeded 19 34) · " + std::to_string(Runs.size()) + " runs · joints " + (Joints ? "bit-exact" : "differ");
        Tally(CornerGate, Found && Joints ? 0.0 : 1.0, Found && Joints);
    }

    // ---- S5 / loft refusals.
    {
        ExpectRefusal("One point", FitSpaceCurve({ { 0.0, 0.0, 0.0 } }, CurveFitSettings{}).Refusal, "fewer than 2 distinct points");
        ExpectRefusal("Three equal points", FitSpaceCurve({ { 1.0, 2.0, 3.0 }, { 1.0, 2.0, 3.0 }, { 1.0, 2.0, 3.0 } }, CurveFitSettings{}).Refusal, "fewer than 2 distinct points");
        CurveFitSettings Zero;
        Zero.Tolerance = 0.0;
        ExpectRefusal("Tolerance 0", FitSpaceCurve({ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } }, Zero).Refusal, "tolerance must be positive");
        std::vector<Vector3> Spiked;
        const CurveSpecimen Shoulder = CurveCorpus().front();
        for (double Station : IrregularStations(Shoulder.Points)) Spiked.push_back(Shoulder.Path(Station));
        Spiked[37].Z += 0.02;                                                                // 20 mm outlier
        CurveFitSettings Limited;
        Limited.SpanLimit = 4;
        ExpectRefusal("20 mm outlier at point 37, span limit 4", FitSpaceCurve(Spiked, Limited).Refusal, "point 37 stays");
        ExpectRefusal("Loft of one section", LoftSectionSurface({ CircleSection(0.0, 1.0) }, LoftSettings{}).Refusal, "needs at least 2 sections");
        ExpectRefusal("Open cubic + closed circle", LoftSectionSurface({ FitSpaceCurve(std::vector<Vector3>(Spiked.begin(), Spiked.begin() + 30), CurveFitSettings{}).Curve, CircleSection(0.0, 1.0) }, LoftSettings{}).Refusal, "open and closed sections mixed");
        ExpectRefusal("Closed circle + open arc", LoftSectionSurface({ CircleSection(0.0, 1.0), ConvertConicToNurbs({ { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.0, 3.0) }, LoftSettings{}).Refusal, "open and closed sections mixed");
        ExpectRefusal("Same section twice", LoftSectionSurface({ CircleSection(0.0, 1.0), CircleSection(0.0, 1.0) }, LoftSettings{}).Refusal, "coincide");
    }

    // ---- L1 L2 L3 over the loft corpus.
    std::vector<LoftRow> LoftRows;
    std::vector<std::vector<Vector3>> BonnetLoops;
    std::vector<CurveFitOutcome>      BonnetFits;
    std::mt19937_64 BonnetGenerator(0xB0AA37ULL);
    const std::vector<NurbsCurve> Bonnet = BonnetSections(BonnetGenerator, BonnetLoops, BonnetFits);
    struct LoftSpecimen
    {
        std::string                                 Name;
        std::vector<NurbsCurve>                     Sections;
        std::function<double(Vector3, Vector2)>     Analytic;                                // Distance to the exact surface (empty: none)
        double                                      ExactArea = -1.0;
    };
    std::vector<LoftSpecimen> LoftCorpus;
    LoftCorpus.push_back({ "Cylinder r 0.4 (4 circles, uneven heights 0 · 0.3 · 0.45 · 1)",
                           { CircleSection(0.0, 0.4), CircleSection(0.3, 0.4), CircleSection(0.45, 0.4), CircleSection(1.0, 0.4) },
                           [](Vector3 Point, Vector2) { return std::abs(std::hypot(Point.X, Point.Y) - 0.4); }, FullTurn * 0.4 * 1.0 });
    LoftCorpus.push_back({ "Cone r = 0.5 − 0.2 z (3 circles at 0 · 0.5 · 1.2)",
                           { CircleSection(0.0, 0.5), CircleSection(0.5, 0.4), CircleSection(1.2, 0.26) },
                           [](Vector3 Point, Vector2) { return std::abs(std::hypot(Point.X, Point.Y) - (0.5 - 0.2 * Point.Z)); },
                           std::numbers::pi * (0.5 + 0.26) * std::hypot(1.2, 0.24) });
    {
        const NurbsCurve First  = ConvertLineToNurbs(StraightLine{ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } }, 0.0, 1.0);
        const NurbsCurve Second = ConvertLineToNurbs(StraightLine{ { 0.0, 1.0, 0.3 }, Normalize(Vector3{ 1.0, 0.0, -0.6 }) }, 0.0, std::hypot(1.0, 0.6));
        LoftCorpus.push_back({ "Twisted lines → bilinear patch", { First, Second },
                               [](Vector3 Point, Vector2 Parameter)
                               {
                                   const Vector3 Bottom = Vector3{ Parameter.X, 0.0, 0.0 }, Top = Vector3{ Parameter.X, 1.0, 0.3 - 0.6 * Parameter.X };
                                   return Length(Point - (Bottom * (1.0 - Parameter.Y) + Top * Parameter.Y));
                               }, -1.0 });
    }
    {
        // Three cubics with different knot vectors (one reversed): exercises knot merging and alignment.
        std::vector<NurbsCurve> Sections;
        const std::vector<std::vector<double>> Knots = { { 0, 0, 0, 0, 0.5, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0.25, 0.75, 1, 1, 1, 1 }, { 0, 0, 0, 0, 0.3, 0.5, 0.6, 1, 1, 1, 1 } };
        for (size_t Index = 0; Index < Knots.size(); ++Index)
        {
            std::vector<Vector3> Poles;
            const int Count = static_cast<int>(Knots[Index].size()) - 4;
            for (int Pole = 0; Pole < Count; ++Pole) Poles.push_back({ static_cast<double>(Pole) / (Count - 1), 0.4 * Index + 0.05 * std::sin(3.0 * Pole + Index), 0.1 * std::cos(2.0 * Pole) });
            if (Index == 1) std::reverse(Poles.begin(), Poles.end());
            Sections.push_back(*ConstructNurbsCurve(3, Knots[Index], Poles, std::vector<double>(Poles.size(), 1.0)));
        }
        LoftCorpus.push_back({ "Three cubics, different knots (middle one reversed)", Sections, {}, -1.0 });
    }
    LoftCorpus.push_back({ "Synthetic bonnet (6 fitted sections)", Bonnet, {}, -1.0 });
    for (const LoftSpecimen& Specimen : LoftCorpus)
    {
        LoftRow Row;
        Row.Name     = Specimen.Name;
        Row.Sections = static_cast<int>(Specimen.Sections.size());
        ModelPartition Partition;
        const LoftSheetOutcome Outcome = LoftSectionSheet(Partition, Specimen.Sections, LoftSettings{});
        if (!Outcome.Sheet.Refusal.empty()) { Row.PassedCondition = false; Row.Problem = Outcome.Sheet.Refusal; Tally(SectionGate, 1.0, false); LoftRows.push_back(Row); continue; }
        const NurbsSurfaceDefinition& Surface = *Outcome.Loft.Surface.Definition;
        Row.PolesU = Surface.CountU;
        Row.PolesV = Surface.CountV;
        MeasureSections(Specimen.Sections, Outcome.Loft, Row.SurfaceGap, Row.MergeGap);
        const bool Interpolates = Row.SurfaceGap <= 1.0e-9 && Row.MergeGap <= 1.0e-9;
        Tally(SectionGate, std::max(Row.SurfaceGap, Row.MergeGap), Interpolates);
        Row.PassedCondition = Interpolates;
        if (Specimen.Analytic)
        {
            Row.Analytic = 0.0;
            for (int First = 0; First <= 40; ++First)
            {
                for (int Second = 0; Second <= 40; ++Second)
                {
                    const Vector2 Parameter = { First / 40.0, Second / 40.0 };
                    Row.Analytic = std::max(Row.Analytic, Specimen.Analytic(EvaluateSurface(Outcome.Loft.Surface, Parameter).Position, Parameter));
                }
            }
            Tally(QuadricGate, Row.Analytic, Row.Analytic <= 1.0e-12);
            Row.PassedCondition = Row.PassedCondition && Row.Analytic <= 1.0e-12;
        }
        // L3 sheet: audit, topology, tessellation, area.
        const AuditReport Audit = AuditBody(Partition, Outcome.Sheet.Body);
        Row.Edges = Outcome.Loft.ClosedCondition ? 3 : 4;
        const TessellationOutcome Mesh = TessellateBody(Partition, Outcome.Sheet.Body, TessellationSettings{});
        const TessellationAuditReport MeshAudit = AuditTessellation(Partition, Outcome.Sheet.Body, Mesh);
        Row.Area = IntegrateSurfaceArea(Outcome.Loft.Surface);
        if (Specimen.ExactArea > 0.0) Row.AreaError = std::abs(Row.Area - Specimen.ExactArea) / Specimen.ExactArea;
        const bool Sheet = Audit.SoundCondition() && Outcome.Sheet.Faces.size() == 1 && Mesh.Refusal.empty() && MeshAudit.UnpairedSides == MeshAudit.LaminarSides
                        && MeshAudit.MissingEdgeSides == 0 && MeshAudit.Inverted == 0 && MeshAudit.Degenerate == 0 && MeshAudit.LaminarSides > 0
                        && (Row.AreaError < 0.0 || Row.AreaError <= 1.0e-9);
        if (!Sheet)
        {
            Row.Problem = !Audit.SoundCondition() ? std::string("audit ") + Audit.Findings.front().Code + " " + Audit.Findings.front().Detail
                        : !Mesh.Refusal.empty() ? Mesh.Refusal
                        : "unpaired " + std::to_string(MeshAudit.UnpairedSides) + " · laminar " + std::to_string(MeshAudit.LaminarSides) + " · missing edge sides "
                          + std::to_string(MeshAudit.MissingEdgeSides) + " · inverted " + std::to_string(MeshAudit.Inverted) + " · degenerate " + std::to_string(MeshAudit.Degenerate)
                          + " · area rel. " + Scientific(Row.AreaError);
        }
        Tally(SheetGate, std::max(0.0, Row.AreaError), Sheet);
        Row.PassedCondition = Row.PassedCondition && Sheet;
        LoftRows.push_back(Row);
    }

    // ---- L4 bonnet deviation: the 6 section loops, 5 loops between them (never used), and the analytic surface.
    struct PanelRow { std::string Name; int Samples = 0; double Worst = 0.0, Mean = 0.0; bool PassedCondition = false; };
    std::vector<PanelRow> PanelRows;
    std::string PanelDetail;
    {
        ModelPartition Partition;
        const LoftSurfaceOutcome Loft = LoftSectionSurface(Bonnet, LoftSettings{});
        if (Loft.Refusal.empty())
        {
            const SurfaceGeometry Geometry = Loft.Surface;
            auto PointGap = [&](const std::vector<std::vector<Vector3>>& Loops, const char* Name)
            {
                PanelRow Row;
                Row.Name = Name;
                for (const std::vector<Vector3>& Loop : Loops)
                {
                    for (const Vector3& Point : Loop)
                    {
                        const Vector2 Foot = ProjectOntoSurface(Geometry, Point, { Point.X / 0.75, Point.Y / 1.1 });
                        const double  Gap  = Length(EvaluateSurface(Geometry, Foot).Position - Point);
                        Row.Worst = std::max(Row.Worst, Gap);
                        Row.Mean += Gap;
                        ++Row.Samples;
                    }
                }
                Row.Mean /= std::max(1, Row.Samples);
                Row.PassedCondition = Row.Worst <= PanelTarget;
                PanelRows.push_back(Row);
            };
            PointGap(BonnetLoops, "Limit points of the 6 section loops (fitted, 0.05 mm noise)");
            std::vector<std::vector<Vector3>> Between;
            std::mt19937_64 BetweenGenerator(0xBE7EE7ULL);
            for (double η : { 0.08, 0.27, 0.5, 0.7, 0.9 }) Between.push_back(BonnetLoop(η, BetweenGenerator));
            PointGap(Between, "Limit points of 5 loops between the sections (never seen by the loft)");
            PanelRow Analytic;
            Analytic.Name = "Analytic surface H(x, y), normal distance at 61 × 61 (𝑢, 𝑣)";
            for (int First = 0; First <= 60; ++First)
            {
                for (int Second = 0; Second <= 60; ++Second)
                {
                    const Vector3 Point = EvaluateSurface(Geometry, { First / 60.0, Second / 60.0 }).Position;
                    const Vector3 Slope = BonnetGradient(Point.X, Point.Y);
                    const double  Gap   = std::abs(Point.Z - BonnetHeight(Point.X, Point.Y)) / std::sqrt(1.0 + Dot(Slope, Slope));
                    Analytic.Worst = std::max(Analytic.Worst, Gap);
                    Analytic.Mean += Gap;
                    ++Analytic.Samples;
                }
            }
            Analytic.Mean /= Analytic.Samples;
            Analytic.PassedCondition = Analytic.Worst <= PanelTarget;
            PanelRows.push_back(Analytic);
            int Poles = 0;
            for (const CurveFitOutcome& Fit : BonnetFits) Poles += static_cast<int>(Fit.Curve.Definition->Poles.size());
            PanelDetail = std::to_string(BonnetLoops.size() * BonnetLoops.front().size()) + " limit points → " + std::to_string(Poles) + " section poles → surface "
                        + std::to_string(Loft.Surface.Definition->CountU) + " × " + std::to_string(Loft.Surface.Definition->CountV) + " poles";
        }
        else PanelRows.push_back({ "Loft refused: " + Loft.Refusal, 0, 1.0, 1.0, false });
        for (const PanelRow& Row : PanelRows) Tally(PanelGate, Row.Worst, Row.PassedCondition);
    }

    // ---- L5 determinism.
    {
        auto Run = [](std::vector<double>& Values)
        {
            std::mt19937_64 Local(0xB0AA37ULL);
            std::vector<std::vector<Vector3>> Loops;
            std::vector<CurveFitOutcome>      Fits;
            const LoftSurfaceOutcome Loft = LoftSectionSurface(BonnetSections(Local, Loops, Fits), LoftSettings{});
            for (const CurveFitOutcome& Fit : Fits) for (const Vector3& Pole : Fit.Curve.Definition->Poles) Values.insert(Values.end(), { Pole.X, Pole.Y, Pole.Z });
            if (Loft.Refusal.empty()) for (const Vector3& Pole : Loft.Surface.Definition->Poles) Values.insert(Values.end(), { Pole.X, Pole.Y, Pole.Z });
        };
        std::vector<double> First, Second;
        Run(First); Run(Second);
        const bool Same = !First.empty() && First.size() == Second.size() && std::memcmp(First.data(), Second.data(), First.size() * sizeof(double)) == 0;
        Tally(DeterminismGate, Same ? 0.0 : 1.0, Same);
    }

    // ---- L6 feature tree: SpaceCurve + Loft steps, pole edit regeneration, persistent names, refusals.
    struct TreeRow { std::string Name, Detail; bool PassedCondition = false; };
    std::vector<TreeRow> TreeRows;
    {
        FeatureTree Tree;
        Tree.Name = "Bonnet";
        for (size_t Index = 0; Index < BonnetLoops.size(); ++Index)
        {
            FeatureStep Step;
            Step.Kind   = FeatureStepKind::SpaceCurve;
            Step.Label  = "Section" + std::to_string(Index + 1);
            Step.Points = BonnetLoops[Index];
            Tree.Steps.push_back(Step);
        }
        FeatureStep Loft;
        Loft.Kind  = FeatureStepKind::Loft;
        Loft.Label = "Loft#1";
        for (size_t Index = 0; Index < BonnetLoops.size(); ++Index) Loft.Sections.push_back("Section" + std::to_string(Index + 1));
        Tree.Steps.push_back(Loft);
        auto Regenerate = [&](ModelPartition& Partition) { return RegenerateFeatureTree(Partition, Tree, false); };
        // Each regeneration runs in a fresh partition holding only the loft sheet: its one named face.
        auto FaceName = [](const ModelPartition&, const RegenerationOutcome& Outcome)
        {
            return Outcome.Naming.Faces.empty() ? std::string("?") : Outcome.Naming.Faces.begin()->second;
        };
        auto Sample = [](const ModelPartition& Partition, const RegenerationOutcome& Outcome, Vector2 Parameter)
        {
            const FaceToken Face = Partition.Faces.TokenAt(Outcome.Naming.Faces.begin()->first);
            return EvaluateSurface(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface), Parameter).Position;
        };
        ModelPartition First;
        const RegenerationOutcome Before = Regenerate(First);
        const bool Built = Before.Refused == 0 && Before.Body.PresentCondition() && Before.Curves.size() == BonnetLoops.size();
        TreeRows.push_back({ "6 SpaceCurve + Loft regenerate (0 refused)", "face " + FaceName(First, Before) + " · " + std::to_string(Before.Curves.size()) + " curves", Built });
        if (Built)
        {
            // Pole edit: lift the middle pole of section 3 by 10 mm.
            const NurbsCurveDefinition& Middle = *Before.Curves.at("Section3").Curve.Definition;
            const int Pole = static_cast<int>(Middle.Poles.size()) / 2;
            Tree.Steps[2].PoleEdits.push_back({ Pole, Middle.Poles[Pole] + Vector3{ 0.0, 0.0, 0.01 } });
            ModelPartition Second;
            const RegenerationOutcome After = Regenerate(Second);
            const Vector3 Old = Middle.Poles[Pole], New = After.Curves.count("Section3") ? After.Curves.at("Section3").Curve.Definition->Poles[Pole] : Vector3{};
            const bool    Edited = After.Refused == 0 && Length(New - Old - Vector3{ 0.0, 0.0, 0.01 }) <= 1.0e-15;
            // The panel moves near section 3 and stays put on the first and last sections (they are interpolated).
            double MovedMiddle = 0.0, MovedEnds = 0.0;
            if (After.Refused == 0)
            {
                for (int Index = 0; Index <= 20; ++Index)
                {
                    const double U = Index / 20.0;
                    MovedEnds   = std::max({ MovedEnds, Length(Sample(First, Before, { U, 0.0 }) - Sample(Second, After, { U, 0.0 })), Length(Sample(First, Before, { U, 1.0 }) - Sample(Second, After, { U, 1.0 })) });
                    MovedMiddle = std::max(MovedMiddle, Length(Sample(First, Before, { U, 0.38 }) - Sample(Second, After, { U, 0.38 })));
                }
            }
            TreeRows.push_back({ "Pole edit (+10 mm, Section3 pole " + std::to_string(Pole) + ") regenerates the panel", "pole moved exactly · panel near 𝑣 = 0.38 moved " + std::to_string(MovedMiddle * 1000.0).substr(0, 5)
                                 + " mm · end sections moved " + Scientific(MovedEnds), Edited && MovedMiddle > 1.0e-3 && MovedEnds <= 1.0e-12 });
            TreeRows.push_back({ "Persistent face name survives the edit", FaceName(First, Before) + " → " + FaceName(Second, After), FaceName(First, Before) == FaceName(Second, After) && FaceName(Second, After) != "?" });
            Tree.Steps[2].PoleEdits.back().Index = 99;
            ModelPartition Third;
            const RegenerationOutcome Beyond = Regenerate(Third);
            const std::string Refusal = Beyond.Steps[2].Refusal;
            TreeRows.push_back({ "Pole edit beyond the fitted poles refuses (and the loft after it)", Refusal + " · loft: " + Beyond.Steps[6].Refusal,
                                 Refusal.find("pole edit 99 is beyond") != std::string::npos && Beyond.Steps[6].Refusal.find("Section3 is unavailable") != std::string::npos });
            Tree.Steps[2].PoleEdits.clear();
            FeatureStep Blend;
            Blend.Kind  = FeatureStepKind::Blend;
            Blend.Label = "Blend#1";
            Blend.Size  = LiteralValue(0.01);
            Blend.References.push_back({});
            Tree.Steps.push_back(Blend);
            ModelPartition Fourth;
            const RegenerationOutcome Sheet = Regenerate(Fourth);
            TreeRows.push_back({ "Blend on the sheet refuses", Sheet.Steps.back().Refusal, Sheet.Steps.back().Refusal.find("the body is a sheet") != std::string::npos });
        }
        for (const TreeRow& Row : TreeRows) Tally(TreeGate, Row.PassedCondition ? 0.0 : 1.0, Row.PassedCondition);
    }

    // ---- Report.
    const GateTally* Gates[] = { &ToleranceGate, &FairnessGate, &RecoveryGate, &CornerGate, &RefusalGate, &SectionGate, &QuadricGate, &SheetGate, &PanelGate, &TreeGate, &DeterminismGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Surfacing validation\n\n%s Vehicle plan V2 (curve fitting) and V4 (loft): points → fair B-splines within 0.2 mm → exact skinned NURBS sheet\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Fitting (S1 S2) · tolerance 0.2 mm · fairing λ ≤ 10⁻⁷ (lowered by decades only when it would cost tolerance)\n\n| | Curve | Noise a [mm] | Points | Poles | Spans | λ used | worst [mm] | mean [mm] | vs true curve [mm] | κ maxima | Note |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const FitRow& Row : FitRows)
    {
        std::printf("| %s | %s | %.2f | %d | %d | %d | %.0e | %.4f | %.4f | %.4f | %d | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Noise * 1000.0, Row.Points, Row.Poles, Row.Spans, Row.Fairing,
                    Row.Worst * 1000.0, Row.Mean * 1000.0, Row.TrueGap * 1000.0, Row.Extrema, Row.Problem.c_str());
    }
    std::printf("\n## Exact recovery (S3)\n\n| | Case | Measured | Limit |\n|---|---|---|---|\n");
    for (const RecoveryRow& Row : RecoveryRows) std::printf("| %s | %s | %.2e | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Error, Row.Limit.c_str());
    std::printf("\n## Corners (S4)\n\n%s %s\n", CornerGate.Failures == 0 ? "🟢" : "🔴", CornerDetail.c_str());
    std::printf("\n## Refusals (S5)\n\n| | Case | Expected | Measured |\n|---|---|---|---|\n");
    for (const RefusalRow& Row : RefusalRows) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Expected.c_str(), Row.Measured.c_str());
    std::printf("\n## Loft (L1 L2 L3)\n\n| | Specimen | Sections | Poles 𝑢 × 𝑣 | Rim edges | S(𝑢, 𝑣ₖ) vs section | merged vs original | vs exact surface | Area | area rel. | Note |\n|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const LoftRow& Row : LoftRows)
    {
        std::printf("| %s | %s | %d | %d × %d | %d | %.1e | %.1e | %s | %.9f | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Sections, Row.PolesU, Row.PolesV, Row.Edges,
                    Row.SurfaceGap, Row.MergeGap, Scientific(Row.Analytic).c_str(),
                    Row.Area, Scientific(Row.AreaError).c_str(), Row.Problem.c_str());
    }
    std::printf("\n## Bonnet panel (L4) · %s\n\n| | Measured against | Samples | worst [mm] | mean [mm] |\n|---|---|---|---|---|\n", PanelDetail.c_str());
    for (const PanelRow& Row : PanelRows) std::printf("| %s | %s | %d | %.4f | %.4f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Samples, Row.Worst * 1000.0, Row.Mean * 1000.0);
    std::printf("\n## Feature tree (L6)\n\n| | Check | Detail |\n|---|---|---|\n");
    for (const TreeRow& Row : TreeRows) std::printf("| %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
