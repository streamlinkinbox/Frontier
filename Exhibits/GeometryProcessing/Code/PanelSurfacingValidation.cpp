/*====================================================================================================================================
                                                    PANELSURFACINGVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Vehicle plan V5–V7: curve networks (Gordon / Coons), G1 / G2 across shared sections, sewing → mirror → caps → solid

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"
#include "Engine/GeometryProcessing/Feature/FeatureTree.h"
#include "Engine/GeometryProcessing/Surfacing/CurveFitting.h"
#include "Engine/GeometryProcessing/Surfacing/CurveNetwork.h"
#include "Engine/GeometryProcessing/Surfacing/PanelAssembly.h"
#include "Engine/GeometryProcessing/Surfacing/SectionLoft.h"
#include "Engine/GeometryProcessing/Surfacing/SurfaceAlgebra.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
    constexpr double PanelTarget = 5.0e-4;                                                   // [len] - Plan deviation target (0.5 mm)

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - N1 …
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

    GateTally CoonsGate        = { "N1", "Coons: 2 + 2 lines → the bilinear patch (41 × 41); 4 curved boundaries interpolated", "≤ 10⁻¹² · ≤ 10⁻⁹" };
    GateTally GordonGate       = { "N2", "Gordon: iso-curve networks of B-spline surfaces → S(𝑢, 𝑣ⱼ) ≡ first family, S(𝑢ᵢ, 𝑣) ≡ second, vs refit and original curves", "≤ 10⁻⁹" };
    GateTally SortingGate      = { "N3", "Unsorted, reversed input sorts into the same surface (auto families, orientation)", "≤ 10⁻¹²" };
    GateTally BonnetNetGate    = { "N4", "Bonnet network: 6 sections + 4 spines fitted from noisy points → surface vs analytic H and vs every point", "≤ 0.5 mm" };
    GateTally NetworkRefuseGate= { "N5", "Refusals name the curves: no crossing, open network (overrun), odd cycle, isolated curve, too few curves", "all named" };
    GateTally NetworkSheetGate = { "N6", "Network sheet: audit sound · 1 face · 4 laminar edges; L5-style determinism (bit-identical twice)", "sound · equal" };

    GateTally TangentGate      = { "C1", "G1 loft against a neighbour: normal angle along the shared section (bonnet front 1–3 · rear 3–6, 4 side combinations)", "≤ 10⁻⁸ rad" };
    GateTally CurvatureGate    = { "C2", "G2 loft: normal angle and relative jump of the cross normal curvature along the shared section", "≤ 10⁻⁸ rad · ≤ 10⁻⁶" };
    GateTally DiscriminateGate = { "C3", "The gates see a difference: G0 shows a tangent break, G1 a curvature jump (so C1 / C2 are not vacuous)", "> 10⁻⁵ rad · > 10⁻³" };
    GateTally SharedRowGate    = { "C4", "Shared section unchanged: G1 / G2 panels still interpolate every section; shared rows bit-identical", "≤ 10⁻⁹ · equal" };
    GateTally ElevationGate    = { "C5", "Mixed-degree loft (line + quadratic + cubic): exact elevation, sections interpolated", "≤ 10⁻⁹" };
    GateTally ContinuityRefuseGate = { "C6", "Continuity refusals: reference without the section, rational reference, G3, closed sections", "all named" };

    GateTally ExactSolidGate   = { "A1", "Exact solids: half arcs lofted → mirror x = 0 → plane caps → cylinder πr²L and frustum πL(r₀² + r₀r₁ + r₁²)/3 (two integrators)", "rel. ≤ 10⁻⁹" };
    GateTally SolidBodyGate    = { "A2", "Closed bodies: audit sound · 4 faces · 2 sewn + caps · tessellation watertight (no unpaired side)", "sound · 0" };
    GateTally MirrorSeamGate   = { "A3", "Mirror seam G1: halves meet with normal angle ≤ 10⁻⁸ when the sections leave x = 0 perpendicularly (fit constraint); unconstrained fits show a break", "≤ 10⁻⁸ rad" };
    GateTally BonnetSheetGate  = { "A4", "Bonnet sheet: front + rear (G2) + mirrored pair → 4 panels, 4 sewn, 8 rim edges, audit sound, tessellation conforming", "sound" };
    GateTally PodSolidGate     = { "A5", "Pod: 5 noisy half sections fitted planar + mirror tangent → loft → mirror → caps → solid; volume vs ∫ πab dy", "rel. ≤ 2·10⁻³" };
    GateTally AssemblyRefuseGate = { "A6", "Assembly refusals: non-planar rim cap, boundary parametrised differently, boundary claimed by 3 panels, constraint on a line", "all named" };

    GateTally TreeBonnetGate   = { "T1", "Tree: 6 SpaceCurves → Loft → Loft (G2) → Mirror x = 0 → sheet sound · 4 named faces · seam G1 (look-ahead tangents) · G2 kept", "≤ 10⁻⁸ rad · ≤ 10⁻⁶" };
    GateTally TreePodGate      = { "T2", "Tree: 5 planar SpaceCurves → Loft → Mirror → Close → solid · Close#8.Cap(0/1) · volume vs ∫ πab dy", "rel. ≤ 2·10⁻³" };
    GateTally TreeNetworkGate  = { "T3", "Tree: 10 SpaceCurves → Network → Mirror → sheet sound, 2 faces sewn on x = 0 (curves lying in the plane get no tangent)", "sound" };
    GateTally TreeRefuseGate   = { "T4", "Tree refusals: G1 without a neighbour, Close twice, Loft after Close, Mirror without panels, Close on an open rim, network G1", "all named" };

    std::vector<GateTally*> AllGates()
    {
        return { &CoonsGate, &GordonGate, &SortingGate, &BonnetNetGate, &NetworkRefuseGate, &NetworkSheetGate,
                 &TangentGate, &CurvatureGate, &DiscriminateGate, &SharedRowGate, &ElevationGate, &ContinuityRefuseGate,
                 &ExactSolidGate, &SolidBodyGate, &MirrorSeamGate, &BonnetSheetGate, &PodSolidGate, &AssemblyRefuseGate,
                 &TreeBonnetGate, &TreePodGate, &TreeNetworkGate, &TreeRefuseGate };
    }

    struct DetailRow
    {
        std::string Gate     = {};
        std::string Name     = {};
        std::string Detail   = {};
        bool        PassedCondition = false;
    };
    std::vector<DetailRow> Details;

    void Report(GateTally& Gate, const std::string& Name, const std::string& Detail, double Value, bool PassedCondition)
    {
        Tally(Gate, Value, PassedCondition);
        Details.push_back({ Gate.Code, Name, Detail, PassedCondition });
    }

    [[nodiscard]] std::string Scientific(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.2e", Value);
        return Text;
    }

    [[nodiscard]] std::string Millimetres(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.4f mm", Value * 1000.0);
        return Text;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SPECIMENS
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] std::vector<double> IrregularStations(int Count)
    {
        std::vector<double> Stations(Count);
        for (int Index = 0; Index < Count; ++Index) Stations[Index] = (Index + 0.3 * std::sin(2.3 * Index)) / (Count - 1);
        Stations.front() = 0.0;
        Stations.back()  = 1.0;
        return Stations;
    }

    [[nodiscard]] Vector3 Jitter(std::mt19937_64& Generator, double Amplitude)
    {
        std::uniform_real_distribution<double> Uniform(-1.0, 1.0);
        const double Scale = Amplitude / std::sqrt(3.0);
        return { Scale * Uniform(Generator), Scale * Uniform(Generator), Scale * Uniform(Generator) };
    }

    // 📝 Synthetic bonnet (half, mirror plane x = 0), as SurfacingValidation: ξ = x / 0.75, η = y / 1.1.
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

    // Distance from a point to the analytic bonnet (vertical gap scaled by the slope: first order in the gap).
    [[nodiscard]] double BonnetGap(Vector3 Point)
    {
        const Vector3 Gradient = BonnetGradient(Point.X, Point.Y);
        return std::abs(Point.Z - BonnetHeight(Point.X, Point.Y)) / std::sqrt(1.0 + Dot(Gradient, Gradient));
    }

    // Section loop at η (26 points across), spine at ξ (30 points along): quad spacing, 0.05 mm noise, x = 0 snapped.
    [[nodiscard]] std::vector<Vector3> BonnetSectionPoints(double η, std::mt19937_64& Generator)
    {
        std::vector<Vector3> Points;
        for (double Station : IrregularStations(26))
        {
            const double X = 0.75 * Station, Y = 1.1 * η;
            Vector3 Point = Vector3{ X, Y, BonnetHeight(X, Y) } + Jitter(Generator, 5.0e-5);
            if (Station == 0.0) Point.X = 0.0;
            Points.push_back(Point);
        }
        return Points;
    }

    [[nodiscard]] std::vector<Vector3> BonnetSpinePoints(double ξ, std::mt19937_64& Generator)
    {
        std::vector<Vector3> Points;
        for (double Station : IrregularStations(30))
        {
            const double X = 0.75 * ξ, Y = 1.1 * Station;
            Vector3 Point = Vector3{ X, Y, BonnetHeight(X, Y) } + Jitter(Generator, 5.0e-5);
            if (ξ == 0.0) Point.X = 0.0;
            Points.push_back(Point);
        }
        return Points;
    }

    constexpr double BonnetSectionStations[] = { 0.0, 0.17, 0.38, 0.6, 0.81, 1.0 };
    constexpr double BonnetSpineStations[]   = { 0.0, 0.35, 0.7, 1.0 };

    struct BonnetNetwork
    {
        std::vector<NurbsCurve>           Curves = {};                                       // Sections then spines
        std::vector<std::string>          Names  = {};
        std::vector<std::vector<Vector3>> Points = {};
    };

    [[nodiscard]] BonnetNetwork ComposeBonnetNetwork(uint64_t Seed)
    {
        BonnetNetwork Network;
        std::mt19937_64 Generator(Seed);
        int Index = 1;
        for (double η : BonnetSectionStations)
        {
            Network.Points.push_back(BonnetSectionPoints(η, Generator));
            Network.Curves.push_back(FitSpaceCurve(Network.Points.back(), CurveFitSettings{}).Curve);
            Network.Names.push_back("Section" + std::to_string(Index++));
        }
        Index = 1;
        for (double ξ : BonnetSpineStations)
        {
            Network.Points.push_back(BonnetSpinePoints(ξ, Generator));
            Network.Curves.push_back(FitSpaceCurve(Network.Points.back(), CurveFitSettings{}).Curve);
            Network.Names.push_back("Spine" + std::to_string(Index++));
        }
        return Network;
    }

    // Cubic B-spline surface with non-uniform knots and a smooth pole field (exact test carrier for N2).
    [[nodiscard]] NurbsSurface TestSurface(int CountU, int CountV, double Twist)
    {
        auto Knots = [](int Count)
        {
            std::vector<double> Values(4, 0.0);
            for (int Index = 1; Index < Count - 3; ++Index) Values.push_back((Index + 0.25 * std::sin(1.7 * Index)) / (Count - 3));
            Values.insert(Values.end(), 4, 1.0);
            return Values;
        };
        std::vector<Vector3> Poles;
        for (int IndexU = 0; IndexU < CountU; ++IndexU)
        {
            for (int IndexV = 0; IndexV < CountV; ++IndexV)
            {
                const double U = static_cast<double>(IndexU) / (CountU - 1), V = static_cast<double>(IndexV) / (CountV - 1);
                Poles.push_back({ 1.2 * U + 0.1 * V * V, 0.9 * V + Twist * U * V, 0.25 * std::sin(2.6 * U) * std::cos(1.9 * V) + 0.1 * U * V });
            }
        }
        return *ConstructNurbsSurface(3, 3, CountU, CountV, Knots(CountU), Knots(CountV), Poles, std::vector<double>(Poles.size(), 1.0));
    }

    [[nodiscard]] Vector3 SurfacePoint(const NurbsSurface& Surface, double U, double V) { return EvaluateNurbsSurfaceDerivatives(*Surface.Definition, { U, V }).Position; }
    [[nodiscard]] Vector3 CurvePoint(const NurbsCurve& Curve, double T) { return EvaluateNurbsCurveDerivatives(*Curve.Definition, T).Position; }

    [[nodiscard]] NurbsCurve LineCurve(Vector3 Start, Vector3 End)
    {
        return *ConstructNurbsCurve(1, { 0.0, 0.0, 1.0, 1.0 }, { Start, End }, { 1.0, 1.0 });
    }

    [[nodiscard]] NurbsCurve CubicCurve(Vector3 A, Vector3 B, Vector3 C, Vector3 D)
    {
        return *ConstructNurbsCurve(3, { 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0 }, { A, B, C, D }, { 1.0, 1.0, 1.0, 1.0 });
    }

    // Largest |S(𝑢, 𝑣ⱼ) − Fⱼ(𝑢)| and |S(𝑢ᵢ, 𝑣) − Gᵢ(𝑣)| over 201 samples per curve (refit families).
    [[nodiscard]] double MeasureNetworkInterpolation(const NetworkSurfaceOutcome& Network)
    {
        double Worst = 0.0;
        for (size_t Row = 0; Row < Network.First.size(); ++Row)
        {
            for (int Sample = 0; Sample <= 200; ++Sample)
            {
                const double U = Sample / 200.0;
                Worst = std::max(Worst, Length(SurfacePoint(Network.Surface, U, Network.FirstParameters[Row]) - CurvePoint(Network.First[Row], U)));
            }
        }
        for (size_t Column = 0; Column < Network.Second.size(); ++Column)
        {
            for (int Sample = 0; Sample <= 200; ++Sample)
            {
                const double V = Sample / 200.0;
                Worst = std::max(Worst, Length(SurfacePoint(Network.Surface, Network.SecondParameters[Column], V) - CurvePoint(Network.Second[Column], V)));
            }
        }
        return Worst;
    }

    // Largest distance from the original curves to the surface (each sample projected): the network honours its input.
    [[nodiscard]] double MeasureOriginalGap(const NetworkSurfaceOutcome& Network, const std::vector<NurbsCurve>& Originals)
    {
        double Worst = 0.0;
        for (const NurbsCurve& Curve : Originals)
        {
            for (int Sample = 0; Sample <= 100; ++Sample)
            {
                const Vector3 Point = CurvePoint(Curve, MeasureNurbsCurveDomain(*Curve.Definition).Low + (MeasureNurbsCurveDomain(*Curve.Definition).High - MeasureNurbsCurveDomain(*Curve.Definition).Low) * Sample / 100.0);
                const Vector2 Foot  = ProjectOntoNurbsSurface(Network.Surface, Point, { 0.5, 0.5 });
                Worst = std::max(Worst, Length(SurfacePoint(Network.Surface, Foot.X, Foot.Y) - Point));
            }
        }
        return Worst;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                V5 · NETWORKS
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateNetworks()
    {
        // ---- N1 Coons: four lines of a skew quadrilateral → the bilinear patch.
        {
            const Vector3 A = { 0.0, 0.0, 0.0 }, B = { 1.3, 0.1, 0.2 }, C = { 1.1, 0.9, 0.7 }, D = { -0.2, 1.0, -0.1 };
            const NetworkSurfaceOutcome Coons = ConstructGordonSurface({ LineCurve(A, B), LineCurve(D, C) }, { LineCurve(A, D), LineCurve(B, C) }, NetworkSettings{});
            double Worst = Coons.Refusal.empty() ? 0.0 : 1.0;
            if (Coons.Refusal.empty())
            {
                for (int IndexU = 0; IndexU <= 40; ++IndexU)
                {
                    for (int IndexV = 0; IndexV <= 40; ++IndexV)
                    {
                        const double U = IndexU / 40.0, V = IndexV / 40.0;
                        const Vector3 Bilinear = A * ((1 - U) * (1 - V)) + B * (U * (1 - V)) + C * (U * V) + D * ((1 - U) * V);
                        Worst = std::max(Worst, Length(SurfacePoint(Coons.Surface, U, V) - Bilinear));
                    }
                }
            }
            Report(CoonsGate, "Coons of 4 lines (skew quad) vs bilinear, 41 × 41", Coons.Refusal.empty() ? "max " + Scientific(Worst) + " · degree " + std::to_string(Coons.Surface.Definition->DegreeU) + " × "
                   + std::to_string(Coons.Surface.Definition->DegreeV) : Coons.Refusal, Worst, Worst <= 1.0e-12);

            // Curved boundary loop: four cubics meeting at the corners.
            const Vector3 P = { 0.0, 0.0, 0.0 }, Q = { 1.0, 0.0, 0.1 }, R = { 1.0, 1.0, 0.0 }, S = { 0.0, 1.0, 0.2 };
            const std::vector<NurbsCurve> Bottom = { CubicCurve(P, { 0.3, -0.1, 0.2 }, { 0.7, 0.1, 0.25 }, Q), CubicCurve(S, { 0.3, 1.1, 0.1 }, { 0.6, 0.9, -0.1 }, R) };
            const std::vector<NurbsCurve> Sides  = { CubicCurve(P, { -0.1, 0.4, 0.1 }, { 0.1, 0.7, 0.3 }, S), CubicCurve(Q, { 1.1, 0.3, 0.2 }, { 0.9, 0.6, 0.15 }, R) };
            const NetworkSurfaceOutcome Curved = ConstructGordonSurface(Bottom, Sides, NetworkSettings{});
            const double Boundary = Curved.Refusal.empty() ? std::max(MeasureNetworkInterpolation(Curved), MeasureOriginalGap(Curved, { Bottom[0], Bottom[1], Sides[0], Sides[1] })) : 1.0;
            Report(CoonsGate, "Coons of 4 cubic boundaries: boundaries interpolated", Curved.Refusal.empty() ? "max " + Scientific(Boundary) : Curved.Refusal, Boundary, Boundary <= 1.0e-9);
        }

        // ---- N2 Gordon on iso-curve networks of B-spline surfaces (3 grids).
        struct GridCase { const char* Name; int CountU; int CountV; double Twist; std::vector<double> AtU; std::vector<double> AtV; };
        const std::vector<GridCase> Grids = {
            { "7 × 6 poles, 4 × 4 iso-curves",  7, 6, 0.15, { 0.0, 0.31, 0.68, 1.0 }, { 0.0, 0.27, 0.71, 1.0 } },
            { "9 × 8 poles, 5 × 6 iso-curves",  9, 8, 0.30, { 0.0, 0.2, 0.45, 0.8, 1.0 }, { 0.0, 0.15, 0.4, 0.55, 0.83, 1.0 } },
            { "6 × 6 poles, 3 × 3 iso-curves",  6, 6, -0.2, { 0.0, 0.5, 1.0 }, { 0.0, 0.42, 1.0 } },
        };
        for (const GridCase& Grid : Grids)
        {
            const NurbsSurface Carrier = TestSurface(Grid.CountU, Grid.CountV, Grid.Twist);
            std::vector<NurbsCurve> First, Second;
            for (double V : Grid.AtV) First.push_back(ExtractNurbsIsoCurve(Carrier, false, V));
            for (double U : Grid.AtU) Second.push_back(ExtractNurbsIsoCurve(Carrier, true, U));
            const NetworkSurfaceOutcome Gordon = ConstructGordonSurface(First, Second, NetworkSettings{});
            if (!Gordon.Refusal.empty()) { Report(GordonGate, Grid.Name, Gordon.Refusal, 1.0, false); continue; }
            const double Refit = MeasureNetworkInterpolation(Gordon);
            std::vector<NurbsCurve> Originals = First;
            Originals.insert(Originals.end(), Second.begin(), Second.end());
            const double Original = MeasureOriginalGap(Gordon, Originals);
            Report(GordonGate, Grid.Name, "vs refit " + Scientific(Refit) + " · vs original " + Scientific(Original) + " · reapprox " + Scientific(Gordon.WorstReapproximation) + " · poles "
                   + std::to_string(Gordon.Surface.Definition->CountU) + " × " + std::to_string(Gordon.Surface.Definition->CountV), std::max(Refit, Original), Refit <= 1.0e-9 && Original <= 1.0e-9);

            // ---- N3 the same curves shuffled, half of them reversed, families mixed: auto sorting reproduces the surface.
            std::vector<NurbsCurve> Mixed;
            for (size_t Index = 0; Index < std::max(First.size(), Second.size()); ++Index)
            {
                if (Index < Second.size()) Mixed.push_back(Index % 2 ? ReverseNurbsCurve(*Second[Second.size() - 1 - Index].Definition) : Second[Second.size() - 1 - Index]);
                if (Index < First.size())  Mixed.push_back(Index % 2 ? First[Index] : ReverseNurbsCurve(*First[Index].Definition));
            }
            // Curve 0 of Mixed is a second-family curve: the auto first family is then the 𝑣-curves (transposed surface).
            const NetworkSurfaceOutcome Sorted = ConstructNetworkSurface(Mixed, NetworkSettings{});
            if (!Sorted.Refusal.empty()) { Report(SortingGate, Grid.Name, Sorted.Refusal, 1.0, false); continue; }
            double Difference = 0.0;
            for (int IndexU = 0; IndexU <= 20; ++IndexU)
            {
                for (int IndexV = 0; IndexV <= 20; ++IndexV)
                {
                    const double U = IndexU / 20.0, V = IndexV / 20.0;
                    const Vector3 Reference = SurfacePoint(Gordon.Surface, U, V);
                    // Map (U, V) of the reference into the sorted surface: it may be transposed and / or reversed.
                    double Best = 1.0e300;
                    for (int Variant = 0; Variant < 8; ++Variant)
                    {
                        double A = (Variant & 1) ? 1.0 - U : U, B = (Variant & 2) ? 1.0 - V : V;
                        if (Variant & 4) std::swap(A, B);
                        Best = std::min(Best, Length(SurfacePoint(Sorted.Surface, A, B) - Reference));
                    }
                    Difference = std::max(Difference, Best);
                }
            }
            Report(SortingGate, Grid.Name, "max " + Scientific(Difference) + " · reversed " + std::to_string(Sorted.Reversed) + " · families " + std::to_string(Sorted.FirstOrder.size()) + " + "
                   + std::to_string(Sorted.SecondOrder.size()), Difference, Difference <= 1.0e-12);
        }

        // ---- N4 bonnet network from noisy points.
        {
            const BonnetNetwork Network = ComposeBonnetNetwork(0xB0AA37ULL);
            const NetworkSurfaceOutcome Bonnet = ConstructNetworkSurface(Network.Curves, NetworkSettings{}, Network.Names);
            if (!Bonnet.Refusal.empty()) Report(BonnetNetGate, "Bonnet network", Bonnet.Refusal, 1.0, false);
            else
            {
                double Analytic = 0.0, Points = 0.0;
                for (int IndexU = 0; IndexU <= 60; ++IndexU)
                {
                    for (int IndexV = 0; IndexV <= 60; ++IndexV) Analytic = std::max(Analytic, BonnetGap(SurfacePoint(Bonnet.Surface, IndexU / 60.0, IndexV / 60.0)));
                }
                int Count = 0;
                for (const std::vector<Vector3>& Loop : Network.Points)
                {
                    for (const Vector3& Point : Loop)
                    {
                        const Vector2 Foot = ProjectOntoNurbsSurface(Bonnet.Surface, Point, { 0.5, 0.5 });
                        Points = std::max(Points, Length(SurfacePoint(Bonnet.Surface, Foot.X, Foot.Y) - Point));
                        ++Count;
                    }
                }
                std::string FirstFamily;
                for (int Index : Bonnet.FirstOrder) FirstFamily += (FirstFamily.empty() ? "" : ", ") + Network.Names[Index];
                Report(BonnetNetGate, "vs analytic H (61 × 61)", "max " + Millimetres(Analytic) + " · first family " + FirstFamily, Analytic, Analytic <= PanelTarget);
                Report(BonnetNetGate, "vs all " + std::to_string(Count) + " input points", "max " + Millimetres(Points) + " · crossing gaps ≤ " + Millimetres(Bonnet.WorstGap) + " · curve shift ≤ "
                       + Millimetres(Bonnet.WorstShift) + " · poles " + std::to_string(Bonnet.Surface.Definition->CountU) + " × " + std::to_string(Bonnet.Surface.Definition->CountV), Points, Points <= PanelTarget);
                Report(BonnetNetGate, "Surface interpolates the refit families", "max " + Scientific(MeasureNetworkInterpolation(Bonnet)), MeasureNetworkInterpolation(Bonnet) * 1.0e-3,
                       MeasureNetworkInterpolation(Bonnet) <= 1.0e-9);
            }

            // ---- N5 refusals.
            struct RefusalCase { std::string Name; std::vector<NurbsCurve> Curves; std::vector<std::string> Names; std::string Expected; };
            std::vector<RefusalCase> Cases;
            {
                // Section3 bent up towards its outer end (10 mm at x = 0.75): it still meets Spine1, not the outer spines.
                RefusalCase Case{ "Section3 bent 10 mm at its outer end", Network.Curves, Network.Names, "Section3 and Spine" };
                Case.Curves[2] = MapNurbsCurve(*Network.Curves[2].Definition, [](Vector3 Point)
                {
                    const double Bend = std::max(0.0, (Point.X - 0.4) / 0.35);
                    return Point + Vector3{ 0.0, 0.0, 0.01 * Bend * Bend };
                });
                Cases.push_back(Case);
            }
            {
                // Spine2 shortened: it stops 60 mm before the last section (open network).
                RefusalCase Case{ "Spine2 stops short of Section6", Network.Curves, Network.Names, "do not meet" };
                std::mt19937_64 Generator(0x5B1E2ULL);
                std::vector<Vector3> Points;
                for (double Station : IrregularStations(28)) { const double X = 0.75 * 0.35, Y = 1.04 * Station; Points.push_back(Vector3{ X, Y, BonnetHeight(X, Y) }); }
                Case.Curves[7] = FitSpaceCurve(Points, CurveFitSettings{}).Curve;
                Cases.push_back(Case);
            }
            {
                // Spine3 runs 50 mm past Section6.
                RefusalCase Case{ "Spine3 runs past Section6", Network.Curves, Network.Names, "past the boundary curve" };
                std::vector<Vector3> Points;
                for (double Station : IrregularStations(30)) { const double X = 0.75 * 0.7, Y = 1.15 * Station; Points.push_back(Vector3{ X, Y, BonnetHeight(X, Y) }); }
                Case.Curves[8] = FitSpaceCurve(Points, CurveFitSettings{}).Curve;
                Cases.push_back(Case);
            }
            {
                // A diagonal curve meeting both families: odd cycle.
                RefusalCase Case{ "Diagonal curve (meets both families)", Network.Curves, Network.Names, "same family" };
                std::vector<Vector3> Points;
                for (int Index = 0; Index <= 30; ++Index) { const double S = Index / 30.0, X = 0.75 * S, Y = 1.1 * S; Points.push_back(Vector3{ X, Y, BonnetHeight(X, Y) }); }
                Case.Curves.push_back(FitSpaceCurve(Points, CurveFitSettings{}).Curve);
                Case.Names.push_back("Diagonal");
                Cases.push_back(Case);
            }
            {
                RefusalCase Case{ "A stray curve meeting nothing", Network.Curves, Network.Names, "meets no other curve" };
                Case.Curves.push_back(LineCurve({ 2.0, 0.0, 0.0 }, { 2.0, 1.0, 0.0 }));
                Case.Names.push_back("Stray");
                Cases.push_back(Case);
            }
            Cases.push_back({ "Three curves", { Network.Curves[0], Network.Curves[6], Network.Curves[7] }, { "Section1", "Spine1", "Spine2" }, "at least 4 curves" });
            for (const RefusalCase& Case : Cases)
            {
                const NetworkSurfaceOutcome Outcome = ConstructNetworkSurface(Case.Curves, NetworkSettings{}, Case.Names);
                const bool Named = Outcome.Refusal.find(Case.Expected) != std::string::npos;
                Report(NetworkRefuseGate, Case.Name, Outcome.Refusal.empty() ? "(built — expected a refusal)" : Outcome.Refusal, Named ? 0.0 : 1.0, Named);
            }

            // ---- N6 sheet body and determinism.
            ModelPartition Partition;
            const NetworkSheetOutcome Sheet = ConstructNetworkSheet(Partition, Network.Curves, NetworkSettings{}, Network.Names);
            if (!Sheet.Sheet.Refusal.empty()) Report(NetworkSheetGate, "Network sheet", Sheet.Sheet.Refusal, 1.0, false);
            else
            {
                const AuditReport Audit = AuditBody(Partition, Sheet.Sheet.Body);
                const BodyInventory Inventory = Partition.CollectInventory(Sheet.Sheet.Body);
                const bool Sound = Audit.SoundCondition() && Inventory.Faces.size() == 1 && Inventory.Edges.size() == 4;
                Report(NetworkSheetGate, "Network sheet body", std::string(Audit.SoundCondition() ? "sound" : "UNSOUND: " + std::string(Audit.Findings.front().Code) + " " + Audit.Findings.front().Detail) + " · " + std::to_string(Inventory.Faces.size()) + " face · "
                       + std::to_string(Inventory.Edges.size()) + " edges", Sound ? 0.0 : 1.0, Sound);
            }
            const BonnetNetwork Again = ComposeBonnetNetwork(0xB0AA37ULL);
            const NetworkSurfaceOutcome Repeat = ConstructNetworkSurface(Again.Curves, NetworkSettings{}, Again.Names);
            const bool Equal = Bonnet.Refusal.empty() && Repeat.Refusal.empty() && Bonnet.Surface.Definition->Poles.size() == Repeat.Surface.Definition->Poles.size()
                            && std::equal(Bonnet.Surface.Definition->Poles.begin(), Bonnet.Surface.Definition->Poles.end(), Repeat.Surface.Definition->Poles.begin(),
                                          [](Vector3 A, Vector3 B) { return A.X == B.X && A.Y == B.Y && A.Z == B.Z; });
            Report(NetworkSheetGate, "Same input twice → bit-identical network poles", Equal ? "equal" : "DIFFERENT", Equal ? 0.0 : 1.0, Equal);
        }
    }
}

namespace
{
    //--------------------------------------------------------------------------------------------------------------------
    //                                                V6 · CONTINUITY
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 Normal curvature of a surface at (𝑢, 𝑣) in tangent direction W: II(W) / I(W), with W written in (S𝑢, S𝑣) and the
    //    normal oriented along Up.
    [[nodiscard]] double NormalCurvature(const NurbsSurface& Surface, Vector2 Parameter, Vector3 W, Vector3 Up)
    {
        const NurbsSurfaceDerivatives D = EvaluateNurbsSurfaceDerivatives(*Surface.Definition, Parameter);
        Vector3 Normal = Cross(D.U, D.V);
        Normal = Normal * ((Dot(Normal, Up) < 0.0 ? -1.0 : 1.0) / Length(Normal));
        const double E = Dot(D.U, D.U), F = Dot(D.U, D.V), G = Dot(D.V, D.V);
        const double Determinant = E * G - F * F;
        const double WU = Dot(W, D.U), WV = Dot(W, D.V);
        const double A = (G * WU - F * WV) / Determinant, B = (E * WV - F * WU) / Determinant;
        const double Second = A * A * Dot(D.UU, Normal) + 2.0 * A * B * Dot(D.UV, Normal) + B * B * Dot(D.VV, Normal);
        const double First  = A * A * E + 2.0 * A * B * F + B * B * G;
        return Second / First;
    }

    [[nodiscard]] Vector3 UnitNormal(const NurbsSurface& Surface, Vector2 Parameter)
    {
        const NurbsSurfaceDerivatives D = EvaluateNurbsSurfaceDerivatives(*Surface.Definition, Parameter);
        const Vector3 Normal = Cross(D.U, D.V);
        return Normal * (1.0 / Length(Normal));
    }

    struct SeamMeasure
    {
        double Position  = 0.0;                                                              // [len] - Largest gap between the two sides
        double Angle     = 0.0;                                                              // [rad] - Largest normal angle
        double Curvature = 0.0;                                                              // [-]   - Largest relative cross-curvature jump
    };

    // 📝 Along a shared boundary: A at (𝑢, VA) and B at (𝑢 or 1 − 𝑢, VB). Normals compared up to orientation; the cross
    //    direction is N × T (T the edge tangent), the same tangent direction on both sides.
    [[nodiscard]] SeamMeasure MeasureSeam(const NurbsSurface& A, double VA, const NurbsSurface& B, double VB, bool ReversedCondition)
    {
        SeamMeasure Measure;
        for (int Sample = 0; Sample <= 100; ++Sample)
        {
            const double U = Sample / 100.0, UB = ReversedCondition ? 1.0 - U : U;
            const NurbsSurfaceDerivatives DA = EvaluateNurbsSurfaceDerivatives(*A.Definition, { U, VA });
            Measure.Position = std::max(Measure.Position, Length(DA.Position - SurfacePoint(B, UB, VB)));
            const Vector3 NormalA = UnitNormal(A, { U, VA });
            Vector3 NormalB = UnitNormal(B, { UB, VB });
            if (Dot(NormalA, NormalB) < 0.0) NormalB = NormalB * -1.0;
            Measure.Angle = std::max(Measure.Angle, std::atan2(Length(Cross(NormalA, NormalB)), Dot(NormalA, NormalB)));
            const Vector3 Tangent = DA.U * (1.0 / Length(DA.U));
            const Vector3 Across  = Cross(NormalA, Tangent);
            const double  KappaA  = NormalCurvature(A, { U, VA }, Across, NormalA), KappaB = NormalCurvature(B, { UB, VB }, Across, NormalA);
            Measure.Curvature = std::max(Measure.Curvature, std::abs(KappaA - KappaB) / std::max({ std::abs(KappaA), std::abs(KappaB), 1.0e-2 }));
        }
        return Measure;
    }

    [[nodiscard]] std::vector<NurbsCurve> BonnetSections(uint64_t Seed)
    {
        std::mt19937_64 Generator(Seed);
        std::vector<NurbsCurve> Sections;
        for (double η : BonnetSectionStations) Sections.push_back(FitSpaceCurve(BonnetSectionPoints(η, Generator), CurveFitSettings{}).Curve);
        return Sections;
    }

    [[nodiscard]] double MeasureSectionInterpolation(const LoftSurfaceOutcome& Loft, const std::vector<NurbsCurve>& Originals)
    {
        double Worst = 0.0;
        for (size_t Section = 0; Section < Originals.size(); ++Section)
        {
            for (int Sample = 0; Sample <= 100; ++Sample)
            {
                const double U = Sample / 100.0;
                const Vector3 Point = CurvePoint(Originals[Section], U);
                const double  V = Loft.SectionParameters[Section];
                // Sections may run reversed in the loft (alignment): compare against the nearer end.
                Worst = std::max(Worst, std::min(Length(SurfacePoint(Loft.Surface, U, V) - Point), Length(SurfacePoint(Loft.Surface, 1.0 - U, V) - Point)));
            }
        }
        return Worst;
    }

    void ValidateContinuity()
    {
        const std::vector<NurbsCurve> Sections = BonnetSections(0xB0AA37ULL);
        const std::vector<NurbsCurve> FrontSections(Sections.begin(), Sections.begin() + 3), RearSections(Sections.begin() + 2, Sections.end());

        // Four side combinations: the front panel built forward or backward, the rear panel's shared section first or last.
        struct Combination { const char* Name; bool FrontBackward; bool RearBackward; };
        const Combination Combinations[] = { { "front 1→3 · rear 3→6", false, false }, { "front 3→1 · rear 3→6", true, false },
                                             { "front 1→3 · rear 6→3", false, true }, { "front 3→1 · rear 6→3", true, true } };
        for (const Combination& Case : Combinations)
        {
            std::vector<NurbsCurve> Front = FrontSections, Rear = RearSections;
            if (Case.FrontBackward) std::reverse(Front.begin(), Front.end());
            if (Case.RearBackward)  std::reverse(Rear.begin(), Rear.end());
            const LoftSurfaceOutcome FrontLoft = LoftSectionSurface(Front, LoftSettings{});
            if (!FrontLoft.Refusal.empty()) { Report(TangentGate, Case.Name, FrontLoft.Refusal, 1.0, false); continue; }
            const double FrontRow = Case.FrontBackward ? 0.0 : 1.0;
            std::array<SeamMeasure, 3> Measures;
            std::array<LoftSurfaceOutcome, 3> RearLofts;
            for (int Order = 0; Order <= 2; ++Order)
            {
                LoftSettings Settings;
                LoftContinuity& Continuity = Case.RearBackward ? Settings.End : Settings.Start;
                Continuity.Order     = Order;
                Continuity.Reference = FrontLoft.Surface;
                RearLofts[Order] = LoftSectionSurface(Rear, Settings);
                if (!RearLofts[Order].Refusal.empty()) { Report(Order == 2 ? CurvatureGate : TangentGate, std::string(Case.Name) + " G" + std::to_string(Order), RearLofts[Order].Refusal, 1.0, false); continue; }
                // The front's shared row and the rear's: same 𝑢 direction (the rear aligns its sections with section 3 or 6).
                const double RearRow = Case.RearBackward ? 1.0 : 0.0;
                const bool Reversed = Dot(SurfacePoint(FrontLoft.Surface, 1.0, FrontRow) - SurfacePoint(FrontLoft.Surface, 0.0, FrontRow),
                                          SurfacePoint(RearLofts[Order].Surface, 1.0, RearRow) - SurfacePoint(RearLofts[Order].Surface, 0.0, RearRow)) < 0.0;
                Measures[Order] = MeasureSeam(FrontLoft.Surface, FrontRow, RearLofts[Order].Surface, RearRow, Reversed);
            }
            if (!RearLofts[1].Refusal.empty() || !RearLofts[2].Refusal.empty() || !RearLofts[0].Refusal.empty()) continue;
            Report(TangentGate, std::string(Case.Name) + " · G1", "angle " + Scientific(Measures[1].Angle) + " rad · gap " + Scientific(Measures[1].Position) + " · α " + std::to_string(RearLofts[1].StartScale + RearLofts[1].EndScale).substr(0, 5),
                   Measures[1].Angle, Measures[1].Angle <= 1.0e-8 && Measures[1].Position <= 1.0e-12);
            Report(CurvatureGate, std::string(Case.Name) + " · G2", "angle " + Scientific(Measures[2].Angle) + " rad · curvature jump " + Scientific(Measures[2].Curvature) + " · gap " + Scientific(Measures[2].Position),
                   std::max(Measures[2].Angle, Measures[2].Curvature), Measures[2].Angle <= 1.0e-8 && Measures[2].Curvature <= 1.0e-6 && Measures[2].Position <= 1.0e-12);
            Report(DiscriminateGate, std::string(Case.Name) + " · G0 / G1 differ", "G0 angle " + Scientific(Measures[0].Angle) + " rad · G1 curvature jump " + Scientific(Measures[1].Curvature),
                   0.0, Measures[0].Angle > 1.0e-5 && Measures[1].Curvature > 1.0e-3);
            double Interpolation = 0.0;
            for (int Order = 1; Order <= 2; ++Order) Interpolation = std::max(Interpolation, MeasureSectionInterpolation(RearLofts[Order], Rear));
            Report(SharedRowGate, std::string(Case.Name) + " · sections kept", "G1 / G2 vs every rear section " + Scientific(Interpolation), Interpolation, Interpolation <= 1.0e-9);
        }

        // Shared rows bit-identical: the rear's first pole row is the front's last (refined to the same knots).
        {
            const LoftSurfaceOutcome FrontLoft = LoftSectionSurface(FrontSections, LoftSettings{});
            LoftSettings Settings;
            Settings.Start = { 2, FrontLoft.Surface };
            const LoftSurfaceOutcome RearLoft = LoftSectionSurface(RearSections, Settings);
            const NurbsSurfaceDefinition Refined = RefineNurbsSurfaceKnots(*FrontLoft.Surface.Definition,
                                                                           MissingInteriorKnots(FrontLoft.Surface.Definition->KnotsU, RearLoft.Surface.Definition->KnotsU, 3), {});
            const NurbsCurveDefinition FrontRow = ExtractPoleRow(Refined, Refined.CountV - 1), RearRow = ExtractPoleRow(*RearLoft.Surface.Definition, 0);
            bool Equal = FrontRow.Poles.size() == RearRow.Poles.size();
            for (size_t Index = 0; Equal && Index < FrontRow.Poles.size(); ++Index) Equal = FrontRow.Poles[Index].X == RearRow.Poles[Index].X && FrontRow.Poles[Index].Y == RearRow.Poles[Index].Y && FrontRow.Poles[Index].Z == RearRow.Poles[Index].Z;
            Report(SharedRowGate, "Rear first pole row ≡ front last row (refined)", Equal ? "bit-identical (" + std::to_string(RearRow.Poles.size()) + " poles)" : "DIFFERENT", Equal ? 0.0 : 1.0, Equal);
        }

        // ---- C5 mixed degrees.
        {
            const NurbsCurve Line      = LineCurve({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.1 });
            const NurbsCurve Quadratic = *ConstructNurbsCurve(2, { 0.0, 0.0, 0.0, 1.0, 1.0, 1.0 }, { { 0.0, 0.5, 0.1 }, { 0.5, 0.45, 0.3 }, { 1.0, 0.5, 0.0 } }, { 1.0, 1.0, 1.0 });
            const NurbsCurve Cubic     = CubicCurve({ 0.0, 1.0, 0.0 }, { 0.3, 1.1, 0.2 }, { 0.7, 0.95, 0.25 }, { 1.0, 1.0, 0.05 });
            const LoftSurfaceOutcome Mixed = LoftSectionSurface({ Line, Quadratic, Cubic }, LoftSettings{});
            const double Interpolation = Mixed.Refusal.empty() ? MeasureSectionInterpolation(Mixed, { Line, Quadratic, Cubic }) : 1.0;
            Report(ElevationGate, "Line + quadratic + cubic", Mixed.Refusal.empty() ? "sections " + Scientific(Interpolation) + " · elevated " + std::to_string(Mixed.Elevated) + " · degree "
                   + std::to_string(Mixed.Surface.Definition->DegreeU) : Mixed.Refusal, Interpolation, Interpolation <= 1.0e-9 && Mixed.Elevated == 2);
            // Elevation itself is exact: a cubic raised to 5 matches at 201 points.
            const std::optional<NurbsCurveDefinition> Raised = ElevateNurbsCurveDegree(*Sections[2].Definition, 5);
            double Worst = Raised ? 0.0 : 1.0;
            for (int Sample = 0; Raised && Sample <= 200; ++Sample) Worst = std::max(Worst, Length(EvaluateNurbsCurveDerivatives(*Raised, Sample / 200.0).Position - CurvePoint(Sections[2], Sample / 200.0)));
            Report(ElevationGate, "Fitted section raised 3 → 5", "max " + Scientific(Worst), Worst, Worst <= 1.0e-12);
        }

        // ---- C6 refusals.
        {
            const LoftSurfaceOutcome FrontLoft = LoftSectionSurface(FrontSections, LoftSettings{});
            auto Expect = [&](const std::string& Name, const std::string& Refusal, const std::string& Expected)
            {
                const bool Named = Refusal.find(Expected) != std::string::npos;
                Report(ContinuityRefuseGate, Name, Refusal.empty() ? "(built — expected a refusal)" : Refusal, Named ? 0.0 : 1.0, Named);
            };
            LoftSettings Away;
            Away.Start = { 1, FrontLoft.Surface };
            Expect("Reference that does not carry the section", LoftSectionSurface({ Sections[3], Sections[4], Sections[5] }, Away).Refusal, "is not a boundary");
            const NurbsCurve Circle = ConvertConicToNurbs({ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.0, std::numbers::pi);
            const NurbsCurve Higher = ConvertConicToNurbs({ { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.0, std::numbers::pi);
            const NurbsCurve Top    = ConvertConicToNurbs({ { 0.0, 0.0, 2.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.0, std::numbers::pi);
            const LoftSurfaceOutcome HalfTube = LoftSectionSurface({ Circle, Higher }, LoftSettings{});
            LoftSettings Rational;
            Rational.Start = { 1, HalfTube.Surface };
            Expect("Rational reference (half cylinder)", LoftSectionSurface({ Higher, Top }, Rational).Refusal, "rational reference");
            LoftSettings Third;
            Third.Start = { 3, FrontLoft.Surface };
            Expect("G3 requested", LoftSectionSurface(RearSections, Third).Refusal, "only G0 / G1 / G2");
            const NurbsCurve Ring = ConvertConicToNurbs({ { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.0, 2.0 * std::numbers::pi);
            const NurbsCurve Ring2 = ConvertConicToNurbs({ { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.0, 2.0 * std::numbers::pi);
            LoftSettings Closed;
            Closed.Start = { 1, FrontLoft.Surface };
            Expect("Continuity on closed sections", LoftSectionSurface({ Ring, Ring2 }, Closed).Refusal, "closed sections");
        }
    }
}

namespace
{
    //--------------------------------------------------------------------------------------------------------------------
    //                                                V7 · ASSEMBLY
    //--------------------------------------------------------------------------------------------------------------------

    // Half circle in the plane y = Y from (0, Y, −r) through (r, Y, 0) to (0, Y, r): exact rational arc.
    [[nodiscard]] NurbsCurve HalfArc(double Y, double Radius)
    {
        const ConicForm Circle = { { 0.0, Y, 0.0 }, { Radius, 0.0, 0.0 }, { 0.0, 0.0, Radius }, { 0.0, -1.0, 0.0 } };
        return ConvertConicToNurbs(Circle, -0.5 * std::numbers::pi, 0.5 * std::numbers::pi);
    }

    // The mirror seam of a panel and its mirror image: the panel's 𝑢 = Side boundary is the image's 𝑢 = 1 − Side boundary.
    [[nodiscard]] double MeasureMirrorSeam(const SurfacePanel& Panel, const SurfacePanel& Image, double Side)
    {
        const NurbsSurface A = *FinishNurbsSurface(TransposeNurbsSurface(*Panel.Surface.Definition));
        const NurbsSurface B = *FinishNurbsSurface(TransposeNurbsSurface(*Image.Surface.Definition));
        return MeasureSeam(A, Side, B, 1.0 - Side, false).Angle;
    }

    struct MeshCheck { bool Watertight = false; bool Conforming = false; std::string Detail; };

    [[nodiscard]] MeshCheck CheckTessellation(const ModelPartition& Partition, BodyToken Body)
    {
        const TessellationOutcome Mesh = TessellateBody(Partition, Body, TessellationSettings{});
        const TessellationAuditReport Audit = AuditTessellation(Partition, Body, Mesh);
        MeshCheck Check;
        const bool Clean = Mesh.Refusal.empty() && Audit.MissingEdgeSides == 0 && Audit.Inverted == 0 && Audit.Degenerate == 0;
        Check.Watertight = Clean && Audit.UnpairedSides == 0;
        Check.Conforming = Clean && Audit.UnpairedSides == Audit.LaminarSides;
        Check.Detail = Mesh.Refusal.empty() ? "unpaired " + std::to_string(Audit.UnpairedSides) + " · laminar " + std::to_string(Audit.LaminarSides) + " · inverted " + std::to_string(Audit.Inverted)
                                            : Mesh.Refusal;
        return Check;
    }

    [[nodiscard]] std::string AuditText(const AuditReport& Audit)
    {
        return Audit.SoundCondition() ? "sound" : "UNSOUND: " + std::string(Audit.Findings.front().Code) + " " + Audit.Findings.front().Detail;
    }

    void ValidateAssembly()
    {
        // ---- A1 / A2 exact solids.
        struct SolidCase { const char* Name; double Start; double Finish; double Span; };
        for (const SolidCase& Case : { SolidCase{ "Cylinder r 0.3 · L 1.2", 0.3, 0.3, 1.2 }, SolidCase{ "Frustum r 0.4 → 0.15 · L 0.9", 0.4, 0.15, 0.9 } })
        {
            std::vector<NurbsCurve> Arcs;
            for (int Index = 0; Index <= 2; ++Index) Arcs.push_back(HalfArc(Case.Span * Index / 2.0, Case.Start + (Case.Finish - Case.Start) * Index / 2.0));
            const LoftSurfaceOutcome Loft = LoftSectionSurface(Arcs, LoftSettings{});
            if (!Loft.Refusal.empty()) { Report(ExactSolidGate, Case.Name, Loft.Refusal, 1.0, false); continue; }
            const SurfacePanel Half{ Loft.Surface }, Image = MirrorPanel(Half, {}, { 1.0, 0.0, 0.0 });
            PanelAssemblySettings Settings;
            Settings.CapCondition = true;
            ModelPartition Partition;
            const PanelAssemblyOutcome Solid = AssemblePanels(Partition, { Half, Image }, Settings, "Close");
            if (!Solid.Refusal.empty()) { Report(ExactSolidGate, Case.Name, Solid.Refusal, 1.0, false); continue; }
            const double Exact    = std::numbers::pi * Case.Span * (Case.Start * Case.Start + Case.Start * Case.Finish + Case.Finish * Case.Finish) / 3.0;
            const double Integral = IntegrateBodyVolume(Partition, Solid.Body);
            const double Error    = std::max(std::abs(Integral - Exact), std::abs(Solid.Volume - Exact)) / Exact;
            Report(ExactSolidGate, Case.Name, "body integral " + std::to_string(Integral).substr(0, 10) + " · panel quadrature rel. " + Scientific(std::abs(Solid.Volume - Exact) / Exact) + " · exact "
                   + std::to_string(Exact).substr(0, 10) + " · rel. " + Scientific(Error) + (Solid.FlippedCondition ? " · panels turned outward" : ""), Error, Error <= 1.0e-9);
            const AuditReport Audit = AuditBody(Partition, Solid.Body);
            const BodyInventory Inventory = Partition.CollectInventory(Solid.Body);
            const MeshCheck Mesh = CheckTessellation(Partition, Solid.Body);
            const bool Sound = Audit.SoundCondition() && Solid.SolidCondition && Inventory.Faces.size() == 4 && Solid.SewnEdges == 2 && Solid.Caps.size() == 2 && Mesh.Watertight;
            Report(SolidBodyGate, Case.Name, AuditText(Audit) + " · " + std::to_string(Inventory.Faces.size()) + " faces · sewn " + std::to_string(Solid.SewnEdges) + " · caps " + std::to_string(Solid.Caps.size())
                   + " · mesh " + Mesh.Detail, Sound ? 0.0 : 1.0, Sound);
            const double Seam = std::max(MeasureMirrorSeam(Half, Image, 0.0), MeasureMirrorSeam(Half, Image, 1.0));
            Report(MirrorSeamGate, std::string(Case.Name) + " (arcs meet x = 0 perpendicularly)", "angle " + Scientific(Seam) + " rad", Seam, Seam <= 1.0e-8);
        }

        // ---- A3 / A4 bonnet: constrained and unconstrained section fits.
        for (int Constrained = 1; Constrained >= 0; --Constrained)
        {
            std::mt19937_64 Generator(0xB0AA37ULL);
            std::vector<NurbsCurve> Sections;
            for (double η : BonnetSectionStations)
            {
                CurveFitSettings Settings;
                if (Constrained) Settings.StartTangent = Vector3{ 1.0, 0.0, 0.0 };
                const CurveFitOutcome Fit = FitSpaceCurve(BonnetSectionPoints(η, Generator), Settings);
                Sections.push_back(Fit.Curve);
            }
            const LoftSurfaceOutcome Front = LoftSectionSurface({ Sections[0], Sections[1], Sections[2] }, LoftSettings{});
            LoftSettings RearSettings;
            RearSettings.Start = { 2, Front.Surface };
            const LoftSurfaceOutcome Rear = LoftSectionSurface({ Sections[2], Sections[3], Sections[4], Sections[5] }, RearSettings);
            if (!Front.Refusal.empty() || !Rear.Refusal.empty()) { Report(BonnetSheetGate, "Bonnet lofts", Front.Refusal + Rear.Refusal, 1.0, false); continue; }
            const SurfacePanel FrontPanel{ Front.Surface }, RearPanel{ Rear.Surface };
            const SurfacePanel FrontImage = MirrorPanel(FrontPanel, {}, { 1.0, 0.0, 0.0 }), RearImage = MirrorPanel(RearPanel, {}, { 1.0, 0.0, 0.0 });
            const double Seam = std::max(MeasureMirrorSeam(FrontPanel, FrontImage, 0.0), MeasureMirrorSeam(RearPanel, RearImage, 0.0));
            if (Constrained)
            {
                Report(MirrorSeamGate, "Bonnet front + rear, sections fitted with P₁ − P₀ ∥ x̂", "angle " + Scientific(Seam) + " rad", Seam, Seam <= 1.0e-8);
                ModelPartition Partition;
                const PanelAssemblyOutcome Sheet = AssemblePanels(Partition, { FrontPanel, RearPanel, FrontImage, RearImage }, PanelAssemblySettings{}, "Sew");
                if (!Sheet.Refusal.empty()) { Report(BonnetSheetGate, "Bonnet sheet", Sheet.Refusal, 1.0, false); continue; }
                const AuditReport Audit = AuditBody(Partition, Sheet.Body);
                const MeshCheck Mesh = CheckTessellation(Partition, Sheet.Body);
                const bool Sound = Audit.SoundCondition() && !Sheet.SolidCondition && Sheet.SewnEdges == 4 && Sheet.LaminarEdges == 8 && Mesh.Conforming;
                Report(BonnetSheetGate, "4 panels sewn into one sheet", AuditText(Audit) + " · sewn " + std::to_string(Sheet.SewnEdges) + " · rim " + std::to_string(Sheet.LaminarEdges) + " · mesh " + Mesh.Detail,
                       Sound ? 0.0 : 1.0, Sound);
                // G2 across the front / rear seam survives the mirror on both halves.
                const double Across = std::max(MeasureSeam(Front.Surface, 1.0, Rear.Surface, 0.0, false).Curvature, MeasureSeam(*FinishNurbsSurface(*FrontImage.Surface.Definition), 1.0,
                                                                                                                                 *FinishNurbsSurface(*RearImage.Surface.Definition), 0.0, false).Curvature);
                Report(BonnetSheetGate, "Front / rear G2 on both halves", "curvature jump " + Scientific(Across), Across, Across <= 1.0e-6);
                // ---- A6 a non-planar rim refuses the caps.
                PanelAssemblySettings Caps;
                Caps.CapCondition = true;
                ModelPartition Other;
                const PanelAssemblyOutcome Closed = AssemblePanels(Other, { FrontPanel, RearPanel, FrontImage, RearImage }, Caps, "Close");
                const bool Named = Closed.Refusal.find("is not planar") != std::string::npos || Closed.Refusal.find("does not close") != std::string::npos;
                Report(AssemblyRefuseGate, "Caps on the open bonnet rim", Closed.Refusal.empty() ? "(built — expected a refusal)" : Closed.Refusal, Named ? 0.0 : 1.0, Named);
            }
            else Report(MirrorSeamGate, "Unconstrained fits show the break (gate discriminates)", "angle " + Scientific(Seam) + " rad", 0.0, Seam > 1.0e-5);
        }

        // ---- A5 pod: noisy half sections (top x = 0 → side → bottom x = 0), planar + mirror tangent fits.
        {
            const double Length = 1.6;
            auto HalfWidth  = [&](double Y) { return 0.4 + 0.1 * std::sin(std::numbers::pi * Y / Length); };
            auto HalfHeight = [&](double Y) { return 0.3 + 0.05 * Y; };
            std::mt19937_64 Generator(0x90D5ULL);
            std::vector<NurbsCurve> Sections;
            double PlanarOffset = 0.0;
            std::string Problem;
            for (int Index = 0; Index < 5; ++Index)
            {
                const double Y = Length * Index / 4.0;
                std::vector<Vector3> Points;
                for (double Station : IrregularStations(28))
                {
                    const double θ = 0.5 * std::numbers::pi - std::numbers::pi * Station;
                    Vector3 Point = Vector3{ HalfWidth(Y) * std::cos(θ), Y, HalfHeight(Y) * std::sin(θ) } + Jitter(Generator, 5.0e-5);
                    if (Station == 0.0 || Station == 1.0) Point.X = 0.0;
                    Points.push_back(Point);
                }
                CurveFitSettings Settings;
                Settings.PlanarCondition = true;
                Settings.StartTangent = Settings.EndTangent = Vector3{ 1.0, 0.0, 0.0 };
                const CurveFitOutcome Fit = FitSpaceCurve(Points, Settings);
                if (!Fit.Refusal.empty()) Problem = Fit.Refusal;
                PlanarOffset = std::max(PlanarOffset, Fit.PlanarOffset);
                Sections.push_back(Fit.Curve);
            }
            const LoftSurfaceOutcome Loft = Problem.empty() ? LoftSectionSurface(Sections, LoftSettings{}) : LoftSurfaceOutcome{};
            if (!Problem.empty() || !Loft.Refusal.empty()) Report(PodSolidGate, "Pod", Problem + Loft.Refusal, 1.0, false);
            else
            {
                const SurfacePanel Half{ Loft.Surface }, Image = MirrorPanel(Half, {}, { 1.0, 0.0, 0.0 });
                PanelAssemblySettings Settings;
                Settings.CapCondition = true;
                ModelPartition Partition;
                const PanelAssemblyOutcome Solid = AssemblePanels(Partition, { Half, Image }, Settings, "Close");
                if (!Solid.Refusal.empty()) Report(PodSolidGate, "Pod solid", Solid.Refusal, 1.0, false);
                else
                {
                    double Exact = 0.0;                                                      // ∫ π·a·b dy, composite Simpson
                    for (int Index = 0; Index <= 2000; ++Index)
                    {
                        const double Y = Length * Index / 2000.0, Weight = (Index == 0 || Index == 2000) ? 1.0 : (Index % 2 ? 4.0 : 2.0);
                        Exact += Weight * std::numbers::pi * HalfWidth(Y) * HalfHeight(Y) * Length / 6000.0;
                    }
                    const double Integral = IntegrateBodyVolume(Partition, Solid.Body);
                    const double Error = std::abs(Integral - Exact) / Exact;
                    const AuditReport Audit = AuditBody(Partition, Solid.Body);
                    const MeshCheck Mesh = CheckTessellation(Partition, Solid.Body);
                    const double Seam = std::max(MeasureMirrorSeam(Half, Image, 0.0), MeasureMirrorSeam(Half, Image, 1.0));
                    const bool Passed = Error <= 2.0e-3 && Audit.SoundCondition() && Solid.SolidCondition && Mesh.Watertight && Seam <= 1.0e-8;
                    Report(PodSolidGate, "Pod solid (5 sections, 0.05 mm noise)", "volume " + std::to_string(Integral).substr(0, 8) + " vs " + std::to_string(Exact).substr(0, 8) + " · rel. " + Scientific(Error)
                           + " · " + AuditText(Audit) + " · mesh " + Mesh.Detail + " · seam " + Scientific(Seam) + " rad · plane offset ≤ " + Millimetres(PlanarOffset), Error, Passed);
                }
            }
        }

        // ---- A6 refusals.
        auto Expect = [&](const std::string& Name, const std::string& Refusal, const std::string& Expected)
        {
            const bool Named = Refusal.find(Expected) != std::string::npos;
            Report(AssemblyRefuseGate, Name, Refusal.empty() ? "(built — expected a refusal)" : Refusal, Named ? 0.0 : 1.0, Named);
        };
        {
            const NurbsCurve A = CubicCurve({ 0.0, 0.0, 0.0 }, { 0.3, 0.0, 0.1 }, { 0.7, 0.0, 0.1 }, { 1.0, 0.0, 0.0 });
            const NurbsCurve B = CubicCurve({ 0.0, 0.5, 0.1 }, { 0.3, 0.5, 0.2 }, { 0.7, 0.5, 0.2 }, { 1.0, 0.5, 0.1 });
            const NurbsCurve C = CubicCurve({ 0.0, 1.0, 0.0 }, { 0.3, 1.0, 0.1 }, { 0.7, 1.0, 0.1 }, { 1.0, 1.0, 0.0 });
            const NurbsCurve D = CubicCurve({ 0.0, 0.5, 0.6 }, { 0.3, 0.5, 0.7 }, { 0.7, 0.5, 0.7 }, { 1.0, 0.5, 0.6 });
            // B reparametrised exactly (weights λⁱ: same points, different τ).
            NurbsCurveDefinition Moved = *B.Definition;
            for (size_t Index = 0; Index < Moved.Weights.size(); ++Index) Moved.Weights[Index] = std::pow(1.6, static_cast<double>(Index));
            const NurbsCurve Reparametrised{ std::make_shared<NurbsCurveDefinition>(Moved) };
            const SurfacePanel First{ LoftSectionSurface({ A, B }, LoftSettings{}).Surface }, Second{ LoftSectionSurface({ Reparametrised, C }, LoftSettings{}).Surface };
            ModelPartition Partition;
            Expect("Shared boundary parametrised differently", AssemblePanels(Partition, { First, Second }, PanelAssemblySettings{}, "Sew").Refusal, "parametrised differently");
            const SurfacePanel Plain{ LoftSectionSurface({ B, C }, LoftSettings{}).Surface }, Third{ LoftSectionSurface({ B, D }, LoftSettings{}).Surface };
            Expect("One boundary claimed by 3 panels", AssemblePanels(Partition, { First, Plain, Third }, PanelAssemblySettings{}, "Sew").Refusal, "third panel");
            CurveFitSettings Settings;
            Settings.StartTangent = Vector3{ 1.0, 0.0, 0.0 };
            Expect("End tangent on a 2-point line", FitSpaceCurve({ { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 } }, Settings).Refusal, "degree ≥ 2");
            CurveFitSettings Planar;
            Planar.PlanarCondition = true;
            std::vector<Vector3> Twisted;
            for (int Index = 0; Index <= 20; ++Index) { const double T = Index / 20.0; Twisted.push_back({ T, std::sin(3.0 * T) * 0.2, 0.05 * T * T * T }); }
            Expect("Planar fit of a twisted curve", FitSpaceCurve(Twisted, Planar).Refusal, "off their best plane");
        }
    }
}

namespace
{
    //--------------------------------------------------------------------------------------------------------------------
    //                                                T1–T4 FEATURE TREE
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] FeatureStep CurveStep(const std::string& Label, const std::vector<Vector3>& Points, bool Planar = false)
    {
        FeatureStep Step;
        Step.Kind            = FeatureStepKind::SpaceCurve;
        Step.Label           = Label;
        Step.Points          = Points;
        Step.PlanarCondition = Planar;
        return Step;
    }

    [[nodiscard]] FeatureStep SurfaceStep(FeatureStepKind Kind, const std::string& Label, const std::vector<std::string>& Sections = {}, int Continuity = 0)
    {
        FeatureStep Step;
        Step.Kind       = Kind;
        Step.Label      = Label;
        Step.Sections   = Sections;
        Step.Continuity = Continuity;
        Step.Plane.Origin = { 0.0, 0.0, 0.0 };
        Step.Plane.ZAxis  = { 1.0, 0.0, 0.0 };
        return Step;
    }

    [[nodiscard]] std::string RefusalsText(const RegenerationOutcome& Outcome)
    {
        std::string Text;
        for (const FeatureStepReport& Step : Outcome.Steps) if (Step.Status == FeatureStepStatus::Refused) Text += (Text.empty() ? "" : " | ") + Step.Label + ": " + Step.Refusal;
        return Text;
    }

    [[nodiscard]] bool NamedFace(const RegenerationOutcome& Outcome, const std::string& Name)
    {
        for (const auto& [Slot, FaceName] : Outcome.Naming.Faces) if (FaceName == Name) return true;
        return false;
    }

    [[nodiscard]] std::vector<Vector3> PodSectionPoints(double Y, std::mt19937_64& Generator)
    {
        const double HalfWidth = 0.4 + 0.1 * std::sin(std::numbers::pi * Y / 1.6), HalfHeight = 0.3 + 0.05 * Y;
        std::vector<Vector3> Points;
        for (double Station : IrregularStations(28))
        {
            const double θ = 0.5 * std::numbers::pi - std::numbers::pi * Station;
            Vector3 Point = Vector3{ HalfWidth * std::cos(θ), Y, HalfHeight * std::sin(θ) } + Jitter(Generator, 5.0e-5);
            if (Station == 0.0 || Station == 1.0) Point.X = 0.0;
            Points.push_back(Point);
        }
        return Points;
    }

    [[nodiscard]] FeatureTree PodTree()
    {
        FeatureTree Tree;
        std::mt19937_64 Generator(0x90D5ULL);
        std::vector<std::string> Sections;
        for (int Index = 0; Index < 5; ++Index)
        {
            Sections.push_back("Hoop" + std::to_string(Index + 1));
            Tree.Steps.push_back(CurveStep(Sections.back(), PodSectionPoints(1.6 * Index / 4.0, Generator), true));
        }
        Tree.Steps.push_back(SurfaceStep(FeatureStepKind::Loft, "Loft#6", Sections));
        Tree.Steps.push_back(SurfaceStep(FeatureStepKind::Mirror, "Mirror#7"));
        Tree.Steps.push_back(SurfaceStep(FeatureStepKind::Close, "Close#8"));
        return Tree;
    }

    void ValidateTree()
    {
        // ---- T1 bonnet: the Mirror step's plane reaches back into the SpaceCurve fits (tangent ∥ x̂ at x = 0).
        {
            FeatureTree Tree;
            std::mt19937_64 Generator(0xB0AA37ULL);
            for (int Index = 0; Index < 6; ++Index) Tree.Steps.push_back(CurveStep("Section" + std::to_string(Index + 1), BonnetSectionPoints(BonnetSectionStations[Index], Generator)));
            Tree.Steps.push_back(SurfaceStep(FeatureStepKind::Loft, "Loft#7", { "Section1", "Section2", "Section3" }));
            Tree.Steps.push_back(SurfaceStep(FeatureStepKind::Loft, "Loft#8", { "Section3", "Section4", "Section5", "Section6" }, 2));
            Tree.Steps.push_back(SurfaceStep(FeatureStepKind::Mirror, "Mirror#9"));
            ModelPartition Partition;
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Tree);
            const std::string Front = "Loft#7.Side(Surface)", Rear = "Loft#8.Side(Surface)";
            const std::string FrontImage = "Mirror#9.Image(" + Front + ")", RearImage = "Mirror#9.Image(" + Rear + ")";
            if (Outcome.Refused || Outcome.Panels.size() != 4) Report(TreeBonnetGate, "Bonnet tree", RefusalsText(Outcome) + " · panels " + std::to_string(Outcome.Panels.size()), 1.0, false);
            else
            {
                const AuditReport Audit = AuditBody(Partition, Outcome.Body);
                const bool Named = NamedFace(Outcome, Front) && NamedFace(Outcome, Rear) && NamedFace(Outcome, FrontImage) && NamedFace(Outcome, RearImage);
                const bool Sound = Audit.SoundCondition() && Named && Partition.CollectInventory(Outcome.Body).Faces.size() == 4;
                Report(TreeBonnetGate, "Sheet and names", AuditText(Audit) + " · " + (Named ? Front + " · " + FrontImage + " …" : std::string("names missing")), Sound ? 0.0 : 1.0, Sound);
                const double Seam = std::max(MeasureMirrorSeam(SurfacePanel{ Outcome.Panels.at(Front) }, SurfacePanel{ Outcome.Panels.at(FrontImage) }, 0.0),
                                             MeasureMirrorSeam(SurfacePanel{ Outcome.Panels.at(Rear) }, SurfacePanel{ Outcome.Panels.at(RearImage) }, 0.0));
                Report(TreeBonnetGate, "Mirror seam (no tangent authored: the Mirror step supplies it)", "angle " + Scientific(Seam) + " rad", Seam, Seam <= 1.0e-8);
                const double Across = MeasureSeam(Outcome.Panels.at(Front), 1.0, Outcome.Panels.at(Rear), 0.0, false).Curvature;
                Report(TreeBonnetGate, "Loft#8 G2 to Loft#7 (found by the shared Section3)", "curvature jump " + Scientific(Across), Across, Across <= 1.0e-6);
            }
        }

        // ---- T2 pod: planar SpaceCurves → solid with named caps.
        {
            FeatureTree Tree = PodTree();
            ModelPartition Partition;
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Tree);
            if (Outcome.Refused) Report(TreePodGate, "Pod tree", RefusalsText(Outcome), 1.0, false);
            else
            {
                double Exact = 0.0;
                for (int Index = 0; Index <= 2000; ++Index)
                {
                    const double Y = 1.6 * Index / 2000.0, Weight = (Index == 0 || Index == 2000) ? 1.0 : (Index % 2 ? 4.0 : 2.0);
                    Exact += Weight * std::numbers::pi * (0.4 + 0.1 * std::sin(std::numbers::pi * Y / 1.6)) * (0.3 + 0.05 * Y) * 1.6 / 6000.0;
                }
                const double Integral = IntegrateBodyVolume(Partition, Outcome.Body);
                const double Error = std::abs(Integral - Exact) / Exact;
                const AuditReport Audit = AuditBody(Partition, Outcome.Body);
                const bool Named = NamedFace(Outcome, "Close#8.Cap(0)") && NamedFace(Outcome, "Close#8.Cap(1)") && NamedFace(Outcome, "Mirror#7.Image(Loft#6.Side(Surface))");
                const bool Passed = Error <= 2.0e-3 && Audit.SoundCondition() && Named && Partition.Bodies.Resolve(Outcome.Body).Category == BodyCategory::Solid;
                Report(TreePodGate, "Pod solid from the tree", "volume " + std::to_string(Integral).substr(0, 8) + " vs " + std::to_string(Exact).substr(0, 8) + " · rel. " + Scientific(Error) + " · "
                       + AuditText(Audit) + (Named ? " · Close#8.Cap(0) · Close#8.Cap(1)" : " · cap names missing"), Error, Passed);
            }
        }

        // ---- T3 network tree: Spine1 lies in x = 0 (no tangent), the sections leave it (tangent).
        {
            FeatureTree Tree;
            const BonnetNetwork Network = ComposeBonnetNetwork(0xB0AA37ULL);
            for (size_t Index = 0; Index < Network.Names.size(); ++Index) Tree.Steps.push_back(CurveStep(Network.Names[Index], Network.Points[Index]));
            Tree.Steps.push_back(SurfaceStep(FeatureStepKind::Network, "Network#11", Network.Names));
            Tree.Steps.push_back(SurfaceStep(FeatureStepKind::Mirror, "Mirror#12"));
            ModelPartition Partition;
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Tree);
            if (Outcome.Refused) Report(TreeNetworkGate, "Network tree", RefusalsText(Outcome), 1.0, false);
            else
            {
                const AuditReport Audit = AuditBody(Partition, Outcome.Body);
                const bool Named = NamedFace(Outcome, "Network#11.Side(Surface)") && NamedFace(Outcome, "Mirror#12.Image(Network#11.Side(Surface))");
                const bool Sound = Audit.SoundCondition() && Named && Partition.CollectInventory(Outcome.Body).Faces.size() == 2;
                const double Seam = MeasureMirrorSeam(SurfacePanel{ Outcome.Panels.at("Network#11.Side(Surface)") }, SurfacePanel{ Outcome.Panels.at("Mirror#12.Image(Network#11.Side(Surface))") }, 0.0);
                Report(TreeNetworkGate, "Network + mirror sheet", AuditText(Audit) + " · 2 faces · seam angle " + Scientific(Seam) + " rad (networks: G1 at the mirror 🚧, reported only)", Sound ? 0.0 : 1.0, Sound);
            }
        }

        // ---- T4 refusals, each through a whole regeneration.
        auto Expect = [&](const std::string& Name, FeatureTree Tree, const std::string& Label, const std::string& Expected)
        {
            ModelPartition Partition;
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Tree);
            std::string Refusal;
            for (const FeatureStepReport& Step : Outcome.Steps) if (Step.Label == Label) Refusal = Step.Refusal;
            const bool Named = Refusal.find(Expected) != std::string::npos;
            Report(TreeRefuseGate, Name, Refusal.empty() ? "(built — expected a refusal)" : Label + ": " + Refusal, Named ? 0.0 : 1.0, Named);
        };
        {
            FeatureTree Lone = PodTree();
            Lone.Steps.resize(5);
            Lone.Steps.push_back(SurfaceStep(FeatureStepKind::Loft, "Loft#6", { "Hoop1", "Hoop2" }, 1));
            Expect("G1 with no neighbouring panel", Lone, "Loft#6", "needs a panel ending on");
            FeatureTree Twice = PodTree();
            Twice.Steps.push_back(SurfaceStep(FeatureStepKind::Close, "Close#9"));
            Expect("Close after Close", Twice, "Close#9", "already closed by Close#8");
            FeatureTree After = PodTree();
            After.Steps.push_back(SurfaceStep(FeatureStepKind::Loft, "Loft#9", { "Hoop4", "Hoop5" }));
            Expect("Loft after Close", After, "Loft#9", "already closed by Close#8");
            FeatureTree Empty;
            Empty.Steps.push_back(SurfaceStep(FeatureStepKind::Mirror, "Mirror#1"));
            Expect("Mirror without panels", Empty, "Mirror#1", "no surface panels");
            FeatureTree Open;
            std::mt19937_64 Generator(0xB0AA37ULL);
            for (int Index = 0; Index < 3; ++Index) Open.Steps.push_back(CurveStep("Section" + std::to_string(Index + 1), BonnetSectionPoints(BonnetSectionStations[Index], Generator)));
            Open.Steps.push_back(SurfaceStep(FeatureStepKind::Loft, "Loft#4", { "Section1", "Section2", "Section3" }));
            Open.Steps.push_back(SurfaceStep(FeatureStepKind::Close, "Close#5"));
            Expect("Close on an open, non-planar rim", Open, "Close#5", "not planar");
            FeatureTree Net = Open;
            Net.Steps.resize(3);
            Net.Steps.push_back(SurfaceStep(FeatureStepKind::Network, "Network#4", { "Section1", "Section2", "Section3" }, 1));
            Expect("Network with G1", Net, "Network#4", "🚧");
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    std::string Only = Count > 2 && std::string(Arguments[1]) == "--only" ? Arguments[2] : "";
    if (Only.empty() || Only == "networks")   ValidateNetworks();
    if (Only.empty() || Only == "continuity") ValidateContinuity();
    if (Only.empty() || Only == "assembly")   ValidateAssembly();
    if (Only.empty() || Only == "tree")       ValidateTree();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !Only.empty());
    std::printf("# Panel surfacing validation\n\n%s Vehicle plan V5–V7: curve networks, G1 / G2 across shared sections, sewing → mirror → caps → solid, feature-tree steps\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
