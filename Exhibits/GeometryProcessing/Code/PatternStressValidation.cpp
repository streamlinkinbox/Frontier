/*====================================================================================================================================
                                                   PATTERNSTRESSVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Stress ladder: linear / circular arrays → chains of Booleans → fillets / chamfers on the results; where does it break?

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/BodyTransform.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Containment/WindingNumber.h"
#include "Engine/GeometryProcessing/Local/RedundantTopology.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"
#include "TessellationExport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr double π = std::numbers::pi;

    bool        VerboseCondition = false;                                                     // --verbose: per-phase timings on stderr
    std::string ExportDirectory  = {};                                                        // --export: tessellation JSON per built row

    void Narrate(const char* Phase, Clock::time_point Since)
    {
        if (VerboseCondition) std::fprintf(stderr, "      %-24s %9.1f ms\n", Phase, std::chrono::duration<double, std::milli>(Clock::now() - Since).count());
    }

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                ANALYTIC SHAPES
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 A primitive both as a modeller body and as an exact membership oracle. Box: [0, Extent] in the frame; cylinder:
    //    radius Extent.X about Frame.ZAxis from the origin up to height Extent.Z.
    enum class ShapeKind : uint8_t
    {
        Box       = 0,
        Cylinder  = 1,
        HalfSpace = 2,                                                                        // oracle only: { local z < 0 }
    };

    struct AnalyticShape
    {
        ShapeKind        Kind   = ShapeKind::Box;                                            // [-]   - Primitive
        OrthonormalFrame Frame  = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };    // [-]   - Placement
        Vector3          Extent = {};                                                        // [len] - Box sizes / (r, –, h)
    };

    [[nodiscard]] AnalyticShape BoxShape(Vector3 Low, Vector3 High) { return { ShapeKind::Box, { Low, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, High - Low }; }

    [[nodiscard]] AnalyticShape CylinderShape(Vector3 Foot, Vector3 Axis, double Radius, double Height)
    {
        return { ShapeKind::Cylinder, ConstructFrame(Foot, Axis, std::fabs(Axis.Z) > 0.9 ? Vector3{ 1, 0, 0 } : Vector3{ 0, 0, 1 }), { Radius, 0.0, Height } };
    }

    [[nodiscard]] Vector3 MapPoint(const AffineMap& Map, Vector3 Point) { return Map.ColumnX * Point.X + Map.ColumnY * Point.Y + Map.ColumnZ * Point.Z + Map.Shift; }
    [[nodiscard]] Vector3 MapVector(const AffineMap& Map, Vector3 Vector) { return Map.ColumnX * Vector.X + Map.ColumnY * Vector.Y + Map.ColumnZ * Vector.Z; }

    [[nodiscard]] AnalyticShape MapShape(const AnalyticShape& Shape, const AffineMap& Map)
    {
        AnalyticShape Mapped = Shape;
        Mapped.Frame = { MapPoint(Map, Shape.Frame.Origin), MapVector(Map, Shape.Frame.XAxis), MapVector(Map, Shape.Frame.YAxis), MapVector(Map, Shape.Frame.ZAxis) };
        return Mapped;
    }

    [[nodiscard]] Vector3 LocalCoordinates(const AnalyticShape& Shape, Vector3 Point)
    {
        const Vector3 Offset = Point - Shape.Frame.Origin;
        return { Dot(Offset, Shape.Frame.XAxis), Dot(Offset, Shape.Frame.YAxis), Dot(Offset, Shape.Frame.ZAxis) };
    }

    // Half-space { (P − Origin) · ZAxis < 0 } — an oracle term only (never constructed).
    [[nodiscard]] AnalyticShape HalfSpaceShape(Vector3 Origin, Vector3 Outward)
    {
        return { ShapeKind::HalfSpace, ConstructFrame(Origin, Outward, std::fabs(Outward.Z) > 0.9 ? Vector3{ 1, 0, 0 } : Vector3{ 0, 0, 1 }), {} };
    }

    // Signed distance (negative inside) — exact for the box, the finite cylinder and the half-space.
    [[nodiscard]] double SignedDistance(const AnalyticShape& Shape, Vector3 Point)
    {
        const Vector3 Local = LocalCoordinates(Shape, Point);
        if (Shape.Kind == ShapeKind::HalfSpace) return Local.Z;
        if (Shape.Kind == ShapeKind::Box)
        {
            const Vector3 Half = Shape.Extent * 0.5;
            const Vector3 Away = { std::fabs(Local.X - Half.X) - Half.X, std::fabs(Local.Y - Half.Y) - Half.Y, std::fabs(Local.Z - Half.Z) - Half.Z };
            const Vector3 Outer = { std::max(Away.X, 0.0), std::max(Away.Y, 0.0), std::max(Away.Z, 0.0) };
            return std::sqrt(Dot(Outer, Outer)) + std::min(std::max({ Away.X, Away.Y, Away.Z }), 0.0);
        }
        const double Radial = std::hypot(Local.X, Local.Y) - Shape.Extent.X;
        const double Axial  = std::fabs(Local.Z - 0.5 * Shape.Extent.Z) - 0.5 * Shape.Extent.Z;
        return std::hypot(std::max(Radial, 0.0), std::max(Axial, 0.0)) + std::min(std::max(Radial, Axial), 0.0);
    }

    // 📝 Membership = (any Added) ∧ ¬(any Removed) ∧ (Within empty ∨ any Within) — every scene here is of that form.
    struct ConstructiveOracle
    {
        std::vector<AnalyticShape> Added   = {};                                             // [-]   - Union terms
        std::vector<AnalyticShape> Removed = {};                                             // [-]   - Subtracted terms
        std::vector<AnalyticShape> Within  = {};                                             // [-]   - Intersected union
        Vector3                    Low     = {};                                             // [len] - Sampling box
        Vector3                    High    = {};                                             // [len]
    };

    [[nodiscard]] bool AnyContains(const std::vector<AnalyticShape>& Shapes, Vector3 Point)
    {
        return std::any_of(Shapes.begin(), Shapes.end(), [&](const AnalyticShape& Shape) { return SignedDistance(Shape, Point) < 0.0; });
    }

    [[nodiscard]] bool OracleContains(const ConstructiveOracle& Oracle, Vector3 Point)
    {
        return AnyContains(Oracle.Added, Point) && !AnyContains(Oracle.Removed, Point) && (Oracle.Within.empty() || AnyContains(Oracle.Within, Point));
    }

    // Distance to the nearest primitive boundary (a lower bound of the distance to the body boundary).
    [[nodiscard]] double OracleMargin(const ConstructiveOracle& Oracle, Vector3 Point)
    {
        double Margin = 1e300;
        for (const auto* Shapes : { &Oracle.Added, &Oracle.Removed, &Oracle.Within })
            for (const AnalyticShape& Shape : *Shapes) Margin = std::min(Margin, std::fabs(SignedDistance(Shape, Point)));
        return Margin;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CLOSED FORMS
    //--------------------------------------------------------------------------------------------------------------------

    // Area common to two discs of radii First, Second with centres Distance apart.
    [[nodiscard]] double LensArea(double First, double Second, double Distance)
    {
        if (Distance >= First + Second) return 0.0;
        if (Distance <= std::fabs(First - Second)) return π * std::min(First, Second) * std::min(First, Second);
        const double SectorFirst  = First * First * std::acos((Distance * Distance + First * First - Second * Second) / (2.0 * Distance * First));
        const double SectorSecond = Second * Second * std::acos((Distance * Distance + Second * Second - First * First) / (2.0 * Distance * Second));
        return SectorFirst + SectorSecond - 0.5 * std::sqrt((-Distance + First + Second) * (Distance + First - Second) * (Distance - First + Second) * (Distance + First + Second));
    }

    // ∫₀^{2π} g(θ) dθ for a smooth periodic g: the trapezoid rule converges spectrally (exact to rounding at 4096 points).
    [[nodiscard]] double PeriodicIntegral(const std::function<double(double)>& Integrand)
    {
        constexpr int Samples = 4096;
        double Sum = 0.0;
        for (int Index = 0; Index < Samples; ++Index) Sum += Integrand(2.0 * π * Index / Samples);
        return 2.0 * π * Sum / Samples;
    }

    // Common volume of two perpendicular cylinders with meeting axes, radii Wide ≥ Narrow: 8∫₀^{r₂} √(r₁² − y²)√(r₂² − y²) dy
    // (y = r₂ sin θ) = 2 r₂² ∫₀^{2π} cos²θ √(r₁² − r₂² sin²θ) dθ; equal radii give 16 r³ / 3.
    [[nodiscard]] double CrossedCylinderVolume(double Wide, double Narrow)
    {
        return 2.0 * Narrow * Narrow * PeriodicIntegral([&](double θ) { return std::cos(θ) * std::cos(θ) * std::sqrt(Wide * Wide - Narrow * Narrow * std::sin(θ) * std::sin(θ)); });
    }

    // Radial bore of radius r from radius Start out through a cylinder wall of radius R (axes meeting at right angles):
    // ∫∫_disc (√(R² − y²) − Start) dA = r² ∫₀^{2π} cos²θ √(R² − r² sin²θ) dθ − Start·π r².
    [[nodiscard]] double RadialBoreVolume(double Wall, double Bore, double Start)
    {
        return Bore * Bore * PeriodicIntegral([&](double θ) { return std::cos(θ) * std::cos(θ) * std::sqrt(Wall * Wall - Bore * Bore * std::sin(θ) * std::sin(θ)); }) - Start * π * Bore * Bore;
    }

    // ∫₀^a √(R² − y²) dy.
    [[nodiscard]] double CircleStripArea(double Radius, double Half) { return 0.5 * (Half * std::sqrt(Radius * Radius - Half * Half) + Radius * Radius * std::asin(Half / Radius)); }

    // Spandrel of a fillet r on a 90° edge: area (1 − π/4) r², centroid r(10 − 3π) / (3(4 − π)) off each face.
    constexpr double SpandrelFactor = 1.0 - π / 4.0;
    [[nodiscard]] double SpandrelOffset(double Radius) { return Radius * (10.0 - 3.0 * π) / (3.0 * (4.0 - π)); }

    [[nodiscard]] double EdgeFilletLoss(double Radius, double Length) { return SpandrelFactor * Radius * Radius * Length; }
    [[nodiscard]] double EdgeChamferLoss(double Setback, double Length) { return 0.5 * Setback * Setback * Length; }

    // Revolved spandrels (Pappus): a bore rim's material lies outside ρ, a boss rim's inside ρ, a boss foot fillet outside ρ.
    [[nodiscard]] double BoreRimFilletLoss(double Bore, double Radius) { return 2.0 * π * (Bore + SpandrelOffset(Radius)) * SpandrelFactor * Radius * Radius; }
    [[nodiscard]] double BossRimFilletLoss(double Boss, double Radius) { return 2.0 * π * (Boss - SpandrelOffset(Radius)) * SpandrelFactor * Radius * Radius; }
    [[nodiscard]] double BossFootFilletGain(double Boss, double Radius) { return 2.0 * π * (Boss + SpandrelOffset(Radius)) * SpandrelFactor * Radius * Radius; }
    [[nodiscard]] double BoreRimChamferLoss(double Bore, double Setback) { return 2.0 * π * (Bore + Setback / 3.0) * 0.5 * Setback * Setback; }
    [[nodiscard]] double BossRimChamferLoss(double Boss, double Setback) { return 2.0 * π * (Boss - Setback / 3.0) * 0.5 * Setback * Setback; }

    // Box Length × Width × Height with every edge rounded r (Steiner: inner box + face slabs + edge quarter-cylinders + corner octants).
    [[nodiscard]] double RoundedBoxVolume(double Length, double Width, double Height, double Radius)
    {
        const double InnerLength = Length - 2.0 * Radius, InnerWidth = Width - 2.0 * Radius, InnerHeight = Height - 2.0 * Radius;
        return InnerLength * InnerWidth * InnerHeight + 2.0 * Radius * (InnerLength * InnerWidth + InnerWidth * InnerHeight + InnerLength * InnerHeight) +
               π * Radius * Radius * (InnerLength + InnerWidth + InnerHeight) + 4.0 / 3.0 * π * Radius * Radius * Radius;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                ROW MODEL
    //--------------------------------------------------------------------------------------------------------------------

    struct StressBuilt
    {
        BodyToken          Body             = {};                                             // [-]    - Final body
        std::string        Refusal          = {};                                             // [-]    - Where it stopped
        int                Operations       = 0;                                              // [-]    - GeometryProcessing operations run (incl. arrays)
        double             Expected         = std::nan("");                                   // [len³] - Closed form (NaN: probe)
        int                ExpectedGenus    = -1;                                             // [-]    - −1: not gated
        int                ExpectedSolids   = 1;                                              // [-]    - Solid regions
        ConstructiveOracle Oracle           = {};                                             // [-]    - Membership oracle
        bool               OracleEnabled    = true;                                           // [-]    - Sample it
        double             OracleClearance  = 1.0e-3;                                         // [len]  - Skip samples nearer a primitive boundary
        bool               MaximalCondition = true;                                           // [-]    - Gate redundant edges
        bool               ExpectRefusal    = false;                                          // [-]    - A build would be wrong
        bool               ExtraCondition   = true;                                           // [-]    - Row-specific comparison
        std::string        Note             = {};                                             // [-]    - Row-specific remark
    };

    struct StressEntry
    {
        std::string                                Family = {};                               // [-] - L / C / M / B / P
        std::string                                Code   = {};                               // [-] - Row code
        std::string                                Scene  = {};                               // [-] - What is built
        std::function<StressBuilt(ModelPartition&)> Build  = {};                              // [-] - Construction
    };

    struct StressResult
    {
        StressEntry Entry       = {};                                                         // [-]    - Row
        StressBuilt Built       = {};                                                         // [-]    - Construction outcome
        std::string Verdict     = {};                                                         // [-]    - 🟢 / ⚠️ / 🔴
        std::string Finding     = {};                                                         // [-]    - Why
        int         Faces = 0, Edges = 0, Vertices = 0, Genus = -1, Solids = 0, Redundant = 0; // [-]
        double      Volume      = 0.0, Error = 0.0;                                           // [len³]
        int         Samples     = 0, Wrong = 0;                                               // [-]    - Oracle
        int64_t     Triangles   = 0;                                                          // [-]
        bool        Watertight  = false;                                                      // [-]
        double      Milliseconds = 0.0;                                                       // [ms]
    };

    //--------------------------------------------------------------------------------------------------------------------
    //                                                CONSTRUCTION HELPERS
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] BodyToken ConstructShape(ModelPartition& Partition, const AnalyticShape& Shape, std::string& Refusal)
    {
        if (Shape.Kind == ShapeKind::Cylinder)
        {
            const auto Outcome = ConstructCylinder(Partition, Shape.Frame, Shape.Extent.X, Shape.Extent.Z);
            if (!Outcome.SucceededCondition()) Refusal = Outcome.Refusal;
            return Outcome.Value;
        }
        const auto Block = ConstructBlock(Partition, { 0, 0, 0 }, Shape.Extent);
        if (!Block.SucceededCondition()) { Refusal = Block.Refusal; return {}; }
        const AffineMap Placement{ Shape.Frame.XAxis, Shape.Frame.YAxis, Shape.Frame.ZAxis, Shape.Frame.Origin };
        const TransformOutcome Placed = TransformBody(Partition, Block.Value, Placement);
        if (!Placed.Refusal.empty()) Refusal = Placed.Refusal;
        return Placed.Body;
    }

    // 📝 A pattern made by the modeller's array operation from one constructed instance, plus the same placements applied
    //    to the analytic shape (the oracle sees where the copies should be, not where the modeller put them).
    struct PatternInstances
    {
        std::vector<BodyToken>     Bodies  = {};                                             // [-] - Instance 0 first
        std::vector<AnalyticShape> Shapes  = {};                                             // [-] - Expected placements
        std::string                Refusal = {};                                             // [-] - Array refusal
    };

    [[nodiscard]] PatternInstances LinearPattern(ModelPartition& Partition, const AnalyticShape& Shape, Vector3 Step, int Count, int& Operations)
    {
        PatternInstances Pattern;
        const BodyToken First = ConstructShape(Partition, Shape, Pattern.Refusal);
        if (!Pattern.Refusal.empty()) return Pattern;
        Pattern.Bodies.push_back(First);
        Pattern.Shapes.push_back(Shape);
        ++Operations;
        int Instance = 0;
        for (const TransformOutcome& Copy : ArrayBody(Partition, First, Step, Count))
        {
            ++Instance;
            if (!Copy.Refusal.empty()) { Pattern.Refusal = "linear array copy " + std::to_string(Instance) + ": " + Copy.Refusal; return Pattern; }
            Pattern.Bodies.push_back(Copy.Body);
            Pattern.Shapes.push_back(MapShape(Shape, TranslationMap(Step * static_cast<double>(Instance))));
        }
        return Pattern;
    }

    [[nodiscard]] PatternInstances CircularPattern(ModelPartition& Partition, const AnalyticShape& Shape, Vector3 AxisPoint, Vector3 AxisDirection, int Count, int& Operations)
    {
        PatternInstances Pattern;
        const BodyToken First = ConstructShape(Partition, Shape, Pattern.Refusal);
        if (!Pattern.Refusal.empty()) return Pattern;
        Pattern.Bodies.push_back(First);
        Pattern.Shapes.push_back(Shape);
        ++Operations;
        int Instance = 0;
        for (const TransformOutcome& Copy : CircularArrayBody(Partition, First, AxisPoint, AxisDirection, Count))
        {
            ++Instance;
            if (!Copy.Refusal.empty()) { Pattern.Refusal = "circular array copy " + std::to_string(Instance) + ": " + Copy.Refusal; return Pattern; }
            Pattern.Bodies.push_back(Copy.Body);
            Pattern.Shapes.push_back(MapShape(Shape, RotationMap(AxisPoint, AxisDirection, 2.0 * π * Instance / Count)));
        }
        return Pattern;
    }

    // Two-level linear array (rows of columns): Count₁ along Step₁, each repeated Count₂ along Step₂.
    [[nodiscard]] PatternInstances GridPattern(ModelPartition& Partition, const AnalyticShape& Shape, Vector3 StepFirst, int CountFirst, Vector3 StepSecond, int CountSecond, int& Operations)
    {
        PatternInstances Row = LinearPattern(Partition, Shape, StepFirst, CountFirst, Operations);
        if (!Row.Refusal.empty()) return Row;
        PatternInstances Grid = Row;
        for (size_t Index = 0; Index < Row.Bodies.size(); ++Index)
        {
            int Instance = 0;
            for (const TransformOutcome& Copy : ArrayBody(Partition, Row.Bodies[Index], StepSecond, CountSecond))
            {
                ++Instance;
                if (!Copy.Refusal.empty()) { Grid.Refusal = "grid copy: " + Copy.Refusal; return Grid; }
                Grid.Bodies.push_back(Copy.Body);
                Grid.Shapes.push_back(MapShape(Row.Shapes[Index], TranslationMap(StepSecond * static_cast<double>(Instance))));
            }
        }
        return Grid;
    }

    // Folds Tools into Stock one Boolean at a time (Order permutes the tools); stops at the first refusal.
    [[nodiscard]] BodyToken ChainBooleans(ModelPartition& Partition, BodyToken Stock, const std::vector<BodyToken>& Tools, BooleanOperation Operation, StressBuilt& Built,
                                          const std::vector<int>& Order = {})
    {
        BodyToken Cursor = Stock;
        for (size_t Index = 0; Index < Tools.size(); ++Index)
        {
            const size_t         Pick    = Order.empty() ? Index : static_cast<size_t>(Order[Index]);
            const auto           Moment  = Clock::now();
            const BooleanOutcome Outcome = ComputeBoolean(Partition, Cursor, Tools[Pick], Operation);
            Narrate(BooleanOperationLabel(Operation), Moment);
            ++Built.Operations;
            if (!Outcome.Refusal.empty())
            {
                Built.Refusal = std::string(BooleanOperationLabel(Operation)) + " " + std::to_string(Index + 1) + " / " + std::to_string(Tools.size()) + ": " + Outcome.Refusal;
                return {};
            }
            Cursor = Outcome.Body;
        }
        return Cursor;
    }

    using EdgeCriterion = std::function<bool(Vector3, Vector3, const CurveGeometry&)>;

    [[nodiscard]] std::vector<EdgeToken> EdgesWhere(const ModelPartition& Partition, BodyToken Body, const EdgeCriterion& Criterion)
    {
        std::vector<EdgeToken> Edges;
        for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (Criterion(Partition.Vertices.Resolve(Profile.Start).Position, Partition.Vertices.Resolve(Profile.End).Position, Partition.Curves.Resolve(Profile.Curve))) Edges.push_back(Edge);
        }
        return Edges;
    }

    // Circle edges whose centre lies at height Height and whose radius is Radius.
    [[nodiscard]] EdgeCriterion CircleAt(double Height, double Radius)
    {
        return [=](Vector3, Vector3, const CurveGeometry& Curve)
        {
            const auto* Arc = std::get_if<CircularArc>(&Curve);
            return Arc && std::fabs(Arc->Frame.Origin.Z - Height) < 1e-9 && std::fabs(Arc->Radius - Radius) < 1e-9;
        };
    }

    // Blends Edges (Count < 0: any non-empty selection) of Built.Body in place; ChainCondition grows the seeds along G1 chains.
    void BlendStep(ModelPartition& Partition, StressBuilt& Built, const EdgeCriterion& Criterion, int Count, BlendProfile Profile, double Size,
                   bool ChainCondition = false)
    {
        if (!Built.Refusal.empty()) return;
        const std::vector<EdgeToken> Edges = EdgesWhere(Partition, Built.Body, Criterion);
        if ((Count >= 0 && static_cast<int>(Edges.size()) != Count) || Edges.empty())
        {
            Built.Refusal = "edge selection found " + std::to_string(Edges.size()) + " edges, expected " + std::to_string(Count);
            return;
        }
        BlendSettings Settings;
        Settings.Profile = Profile;
        Settings.Size    = Size;
        Settings.TangentChainEnabled = ChainCondition;
        const auto         Moment  = Clock::now();
        const BlendOutcome Outcome = BlendEdges(Partition, Built.Body, Edges, Settings);
        Narrate("BlendEdges", Moment);
        ++Built.Operations;
        if (!Outcome.Refusal.empty()) { Built.Refusal = std::string(BlendProfileLabel(Profile)) + " on " + std::to_string(Edges.size()) + " edges: " + Outcome.Refusal; return; }
        Built.Body             = Outcome.Body;
        Built.MaximalCondition = false;                                                       // blend faces may share a carrier with a support
        Built.OracleClearance = std::max(Built.OracleClearance, 1.5 * Size);
        if (Count < 0) Built.Note += std::to_string(Edges.size()) + " edges blended";
        if (ChainCondition) Built.Note += std::to_string(Edges.size()) + " seed(s) → " + std::to_string(Outcome.Statistics.BlendedEdges) + " edges along the tangent chain";
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SCENE FAMILIES
    //--------------------------------------------------------------------------------------------------------------------

    // Plate 12 × 6 × 1 at the origin.
    const AnalyticShape PlateShape = BoxShape({ 0, 0, 0 }, { 12, 6, 1 });
    constexpr double    PlateVolume = 72.0;

    [[nodiscard]] AnalyticShape Bore(Vector3 Centre, double Radius, double Low = -0.5, double Length = 2.0) { return CylinderShape({ Centre.X, Centre.Y, Low }, { 0, 0, 1 }, Radius, Length); }

    // Plate − linear array of N through holes r 0.3 at y = 3, x = 1, 2, ….
    [[nodiscard]] StressBuilt PlateHoles(ModelPartition& Partition, int Count, double Radius = 0.3)
    {
        StressBuilt Built;
        Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 1.5 } };
        const BodyToken Plate = ConstructShape(Partition, PlateShape, Built.Refusal);
        const PatternInstances Holes = LinearPattern(Partition, Bore({ 1, 3, 0 }, Radius), { 1, 0, 0 }, Count, Built.Operations);
        if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
        Built.Oracle.Removed = Holes.Shapes;
        Built.Body           = ChainBooleans(Partition, Plate, Holes.Bodies, BooleanOperation::Difference, Built);
        Built.Expected       = PlateVolume - Count * π * Radius * Radius;
        Built.ExpectedGenus  = Count;
        return Built;
    }

    // Plate ∪ 6 bosses r 0.4 at y = 3 (embedded from z 0.5, or touching the top face from z 1), top at 1.8.
    [[nodiscard]] StressBuilt PlateBosses(ModelPartition& Partition, bool TouchingCondition, PatternInstances* Kept = nullptr)
    {
        StressBuilt Built;
        Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 2.3 } };
        const BodyToken Plate = ConstructShape(Partition, PlateShape, Built.Refusal);
        const double    Foot  = TouchingCondition ? 1.0 : 0.5;
        const PatternInstances Bosses = LinearPattern(Partition, CylinderShape({ 1.5, 3, Foot }, { 0, 0, 1 }, 0.4, 1.8 - Foot), { 1.8, 0, 0 }, 6, Built.Operations);
        if (!Bosses.Refusal.empty()) { Built.Refusal = Bosses.Refusal; return Built; }
        if (Kept) *Kept = Bosses;
        for (const AnalyticShape& Shape : Bosses.Shapes) Built.Oracle.Added.push_back(Shape);
        Built.Body          = ChainBooleans(Partition, Plate, Bosses.Bodies, BooleanOperation::Union, Built);
        Built.Expected      = PlateVolume + 6.0 * π * 0.16 * 0.8;
        Built.ExpectedGenus = 0;
        return Built;
    }

    // Plate ∪ 5 ribs 0.3 × 6 × 0.7 standing on the top face, flush with both long sides.
    [[nodiscard]] StressBuilt PlateRibs(ModelPartition& Partition)
    {
        StressBuilt Built;
        Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 2.2 } };
        const BodyToken Plate = ConstructShape(Partition, PlateShape, Built.Refusal);
        const PatternInstances Ribs = LinearPattern(Partition, BoxShape({ 1, 0, 1 }, { 1.3, 6, 1.7 }), { 2, 0, 0 }, 5, Built.Operations);
        if (!Ribs.Refusal.empty()) { Built.Refusal = Ribs.Refusal; return Built; }
        for (const AnalyticShape& Shape : Ribs.Shapes) Built.Oracle.Added.push_back(Shape);
        Built.Body          = ChainBooleans(Partition, Plate, Ribs.Bodies, BooleanOperation::Union, Built);
        Built.Expected      = PlateVolume + 5.0 * 0.3 * 6.0 * 0.7;
        Built.ExpectedGenus = 0;
        return Built;
    }

    // Disc R 3 × 0.6 − circular array of N through holes r at radius 2.
    const AnalyticShape DiscShape = CylinderShape({ 0, 0, 0 }, { 0, 0, 1 }, 3.0, 0.6);
    const double        DiscVolume = π * 9.0 * 0.6;

    [[nodiscard]] StressBuilt DiscHoles(ModelPartition& Partition, int Count, double Radius, const std::vector<int>& Order = {})
    {
        StressBuilt Built;
        Built.Oracle = { { DiscShape }, {}, {}, { -3.3, -3.3, -0.4 }, { 3.3, 3.3, 1.0 } };
        const BodyToken Disc = ConstructShape(Partition, DiscShape, Built.Refusal);
        const PatternInstances Holes = CircularPattern(Partition, Bore({ 2, 0, 0 }, Radius), { 0, 0, 0 }, { 0, 0, 1 }, Count, Built.Operations);
        if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
        Built.Oracle.Removed = Holes.Shapes;
        Built.Body           = ChainBooleans(Partition, Disc, Holes.Bodies, BooleanOperation::Difference, Built, Order);
        Built.Expected       = DiscVolume - Count * π * Radius * Radius * 0.6;
        Built.ExpectedGenus  = Count;
        return Built;
    }

    // Hub R 1 × 1 ∪ circular array of N spokes x ∈ [0.5, 3], |y| ≤ 0.15, z ∈ [0.2, 0.8].
    [[nodiscard]] StressBuilt HubSpokes(ModelPartition& Partition, int Count)
    {
        StressBuilt Built;
        const AnalyticShape Hub = CylinderShape({ 0, 0, 0 }, { 0, 0, 1 }, 1.0, 1.0);
        Built.Oracle = { { Hub }, {}, {}, { -3.3, -3.3, -0.3 }, { 3.3, 3.3, 1.3 } };
        const BodyToken HubBody = ConstructShape(Partition, Hub, Built.Refusal);
        const PatternInstances Spokes = CircularPattern(Partition, BoxShape({ 0.5, -0.15, 0.2 }, { 3.0, 0.15, 0.8 }), { 0, 0, 0 }, { 0, 0, 1 }, Count, Built.Operations);
        if (!Spokes.Refusal.empty()) { Built.Refusal = Spokes.Refusal; return Built; }
        for (const AnalyticShape& Shape : Spokes.Shapes) Built.Oracle.Added.push_back(Shape);
        Built.Body          = ChainBooleans(Partition, HubBody, Spokes.Bodies, BooleanOperation::Union, Built);
        Built.Expected      = π + Count * 0.6 * (3.0 * 0.3 - 2.0 * CircleStripArea(1.0, 0.15));
        Built.ExpectedGenus = 0;
        return Built;
    }

    // Disc − 12 holes r 0.5 centred on the rim (scallops).
    [[nodiscard]] StressBuilt DiscScallops(ModelPartition& Partition)
    {
        StressBuilt Built;
        Built.Oracle = { { DiscShape }, {}, {}, { -3.6, -3.6, -0.4 }, { 3.6, 3.6, 1.0 } };
        const BodyToken Disc = ConstructShape(Partition, DiscShape, Built.Refusal);
        const PatternInstances Holes = CircularPattern(Partition, Bore({ 3, 0, 0 }, 0.5), { 0, 0, 0 }, { 0, 0, 1 }, 12, Built.Operations);
        if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
        Built.Oracle.Removed = Holes.Shapes;
        Built.Body           = ChainBooleans(Partition, Disc, Holes.Bodies, BooleanOperation::Difference, Built);
        Built.Expected       = DiscVolume - 12.0 * 0.6 * LensArea(3.0, 0.5, 3.0);
        Built.ExpectedGenus  = 0;
        return Built;
    }

    // Ring of 12 overlapping cylinders r 0.6 at radius 2 (each overlaps both neighbours; the last closes the ring).
    [[nodiscard]] StressBuilt OverlappingRing(ModelPartition& Partition)
    {
        StressBuilt Built;
        Built.Oracle = { {}, {}, {}, { -2.8, -2.8, -0.3 }, { 2.8, 2.8, 0.8 } };
        const PatternInstances Ring = CircularPattern(Partition, CylinderShape({ 2, 0, 0 }, { 0, 0, 1 }, 0.6, 0.5), { 0, 0, 0 }, { 0, 0, 1 }, 12, Built.Operations);
        if (!Ring.Refusal.empty()) { Built.Refusal = Ring.Refusal; return Built; }
        Built.Oracle.Added = Ring.Shapes;
        Built.Body = ChainBooleans(Partition, Ring.Bodies.front(), std::vector<BodyToken>(Ring.Bodies.begin() + 1, Ring.Bodies.end()), BooleanOperation::Union, Built);
        const double Pitch = 4.0 * std::sin(π / 12.0);                                       // [len] - Neighbour centre distance
        Built.Expected      = 0.5 * (12.0 * π * 0.36 - 12.0 * LensArea(0.6, 0.6, Pitch));
        Built.ExpectedGenus = 1;
        return Built;
    }

    // Disc R 3 × 1 − 8 radial blind bores r 0.2 at z 0.5 from radius 1 out through the wall (traced cylinder ∩ cylinder).
    [[nodiscard]] StressBuilt RadialBores(ModelPartition& Partition)
    {
        StressBuilt Built;
        const AnalyticShape Disc = CylinderShape({ 0, 0, 0 }, { 0, 0, 1 }, 3.0, 1.0);
        Built.Oracle = { { Disc }, {}, {}, { -3.6, -3.6, -0.3 }, { 3.6, 3.6, 1.3 } };
        const BodyToken DiscBody = ConstructShape(Partition, Disc, Built.Refusal);
        const PatternInstances Bores = CircularPattern(Partition, CylinderShape({ 1, 0, 0.5 }, { 1, 0, 0 }, 0.2, 2.5), { 0, 0, 0 }, { 0, 0, 1 }, 8, Built.Operations);
        if (!Bores.Refusal.empty()) { Built.Refusal = Bores.Refusal; return Built; }
        Built.Oracle.Removed = Bores.Shapes;
        Built.Body           = ChainBooleans(Partition, DiscBody, Bores.Bodies, BooleanOperation::Difference, Built);
        Built.Expected       = π * 9.0 - 8.0 * RadialBoreVolume(3.0, 0.2, 1.0);
        Built.ExpectedGenus  = 0;
        return Built;
    }

    // Block 4 × 4 × 2 − 3 bores along x (r Wide) and 3 along y (r Narrow) at z 1, crossing in 9 places.
    [[nodiscard]] StressBuilt CrossBores(ModelPartition& Partition, double Wide, double Narrow)
    {
        StressBuilt Built;
        const AnalyticShape Block = BoxShape({ 0, 0, 0 }, { 4, 4, 2 });
        Built.Oracle = { { Block }, {}, {}, { -0.5, -0.5, -0.5 }, { 4.5, 4.5, 2.5 } };
        const BodyToken BlockBody = ConstructShape(Partition, Block, Built.Refusal);
        const PatternInstances AlongX = LinearPattern(Partition, CylinderShape({ -0.5, 1, 1 }, { 1, 0, 0 }, Wide, 5.0), { 0, 1, 0 }, 3, Built.Operations);
        const PatternInstances AlongY = LinearPattern(Partition, CylinderShape({ 1, -0.5, 1 }, { 0, 1, 0 }, Narrow, 5.0), { 1, 0, 0 }, 3, Built.Operations);
        if (!AlongX.Refusal.empty() || !AlongY.Refusal.empty()) { Built.Refusal = AlongX.Refusal + AlongY.Refusal; return Built; }
        std::vector<BodyToken> Tools = AlongX.Bodies;
        Tools.insert(Tools.end(), AlongY.Bodies.begin(), AlongY.Bodies.end());
        Built.Oracle.Removed = AlongX.Shapes;
        Built.Oracle.Removed.insert(Built.Oracle.Removed.end(), AlongY.Shapes.begin(), AlongY.Shapes.end());
        Built.Body          = ChainBooleans(Partition, BlockBody, Tools, BooleanOperation::Difference, Built);
        Built.Expected      = 32.0 - (12.0 * π * (Wide * Wide + Narrow * Narrow) - 9.0 * CrossedCylinderVolume(Wide, Narrow));
        Built.ExpectedGenus = 15;                                                             // 24 tunnel segments − 10 junctions (9 + outside) + 1
        return Built;
    }

    [[nodiscard]] int CountFaces(const ModelPartition& Partition, BodyToken Body) { return static_cast<int>(Partition.CollectInventory(Body).Faces.size()); }
    [[nodiscard]] int CountEdges(const ModelPartition& Partition, BodyToken Body) { return static_cast<int>(Partition.CollectInventory(Body).Edges.size()); }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                STRESS TABLE
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] std::vector<StressEntry> AuthorStress()
    {
        using Partition = ModelPartition;
        std::vector<StressEntry> Rows;
        const auto Add = [&](const char* Family, const char* Code, const char* Scene, std::function<StressBuilt(Partition&)> Build) { Rows.push_back({ Family, Code, Scene, std::move(Build) }); };

        // ---- L · Linear arrays.
        Add("L", "L1a", "Plate − 1 hole (r 0.3)", [](Partition& P) { return PlateHoles(P, 1); });
        Add("L", "L1b", "Plate − linear array 4 holes", [](Partition& P) { return PlateHoles(P, 4); });
        Add("L", "L1c", "Plate − linear array 10 holes", [](Partition& P) { return PlateHoles(P, 10); });
        Add("L", "L2", "Plate − 10 × 4 grid array (40 holes r 0.25, 40 Booleans)", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 1.5 } };
            const BodyToken Plate = ConstructShape(P, PlateShape, Built.Refusal);
            const PatternInstances Holes = GridPattern(P, Bore({ 1, 1.2, 0 }, 0.25), { 1.1, 0, 0 }, 10, { 0, 1.2, 0 }, 4, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            Built.Oracle.Removed = Holes.Shapes;
            Built.Body           = ChainBooleans(P, Plate, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.Expected       = PlateVolume - 40.0 * π * 0.0625;
            Built.ExpectedGenus  = 40;
            return Built;
        });
        Add("L", "L3", "Tree order: ∪ 10 disjoint tools (multi-lump) then one −; must equal L1c", [](Partition& P)
        {
            StressBuilt Sequential = PlateHoles(P, 10);
            StressBuilt Built;
            Built.Oracle = Sequential.Oracle;
            const BodyToken Plate = ConstructShape(P, PlateShape, Built.Refusal);
            const PatternInstances Holes = LinearPattern(P, Bore({ 1, 3, 0 }, 0.3), { 1, 0, 0 }, 10, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            const BodyToken Cluster = ChainBooleans(P, Holes.Bodies.front(), std::vector<BodyToken>(Holes.Bodies.begin() + 1, Holes.Bodies.end()), BooleanOperation::Union, Built);
            if (!Built.Refusal.empty()) { Built.Refusal = "tool cluster: " + Built.Refusal; return Built; }
            const int ClusterLumps = static_cast<int>(P.CollectInventory(Cluster).Regions.size()) - 1;
            const BooleanOutcome Cut = ComputeBoolean(P, Plate, Cluster, BooleanOperation::Difference);
            ++Built.Operations;
            if (!Cut.Refusal.empty()) { Built.Refusal = "single cut: " + Cut.Refusal; return Built; }
            Built.Body          = Cut.Body;
            Built.Expected      = Sequential.Expected;
            Built.ExpectedGenus = 10;
            if (!Sequential.Refusal.empty()) { Built.ExtraCondition = false; Built.Note = "sequential reference refused"; return Built; }
            const double Gap = std::fabs(IntegrateBodyVolume(P, Cut.Body) - IntegrateBodyVolume(P, Sequential.Body));
            Built.ExtraCondition = ClusterLumps == 10 && CountFaces(P, Cut.Body) == CountFaces(P, Sequential.Body) && CountEdges(P, Cut.Body) == CountEdges(P, Sequential.Body) && Gap < 1e-12;
            Built.Note = "cluster lumps " + std::to_string(ClusterLumps) + " · F " + std::to_string(CountFaces(P, Cut.Body)) + " vs " + std::to_string(CountFaces(P, Sequential.Body)) +
                         " · |ΔV| vs sequential " + std::to_string(Gap);
            return Built;
        });
        Add("L", "L4", "Plate − 5 overlapping holes r 0.4 at pitch 0.5 (slot with cusps)", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 1.5 } };
            const BodyToken Plate = ConstructShape(P, PlateShape, Built.Refusal);
            const PatternInstances Holes = LinearPattern(P, Bore({ 2, 3, 0 }, 0.4), { 0.5, 0, 0 }, 5, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            Built.Oracle.Removed = Holes.Shapes;
            Built.Body           = ChainBooleans(P, Plate, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.Expected       = PlateVolume - (5.0 * π * 0.16 - 4.0 * LensArea(0.4, 0.4, 0.5));
            Built.ExpectedGenus  = 1;
            return Built;
        });
        Add("L", "L5a", "Plate ∪ linear array 6 bosses (embedded)", [](Partition& P) { return PlateBosses(P, false); });
        Add("L", "L5b", "Plate ∪ 6 bosses touching the top face (coplanar contact)", [](Partition& P) { return PlateBosses(P, true); });
        Add("L", "L6", "Plate ∪ 5 ribs flush with both sides (coplanar merges)", [](Partition& P) { return PlateRibs(P); });
        Add("L", "L7", "6 unit blocks at pitch 1 (face-to-face) ∪ → one 6 × 1 × 1 block", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { {}, {}, {}, { -0.5, -0.5, -0.5 }, { 6.5, 1.5, 1.5 } };
            const PatternInstances Blocks = LinearPattern(P, BoxShape({ 0, 0, 0 }, { 1, 1, 1 }), { 1, 0, 0 }, 6, Built.Operations);
            if (!Blocks.Refusal.empty()) { Built.Refusal = Blocks.Refusal; return Built; }
            Built.Oracle.Added = Blocks.Shapes;
            Built.Body = ChainBooleans(P, Blocks.Bodies.front(), std::vector<BodyToken>(Blocks.Bodies.begin() + 1, Blocks.Bodies.end()), BooleanOperation::Union, Built);
            Built.Expected      = 6.0;
            Built.ExpectedGenus = 0;
            if (Built.Refusal.empty()) { Built.ExtraCondition = CountFaces(P, Built.Body) == 6; Built.Note = std::to_string(CountFaces(P, Built.Body)) + " faces (6 expected)"; }
            return Built;
        });

        // ---- C · Circular arrays.
        Add("C", "C1a", "Disc − circular array 6 holes (r 0.2)", [](Partition& P) { return DiscHoles(P, 6, 0.2); });
        Add("C", "C1b", "Disc − circular array 12 holes", [](Partition& P) { return DiscHoles(P, 12, 0.2); });
        Add("C", "C1c", "Disc − circular array 24 holes", [](Partition& P) { return DiscHoles(P, 24, 0.2); });
        Add("C", "C2", "Disc ∪ circular array 8 bosses r 0.35", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { DiscShape }, {}, {}, { -3.3, -3.3, -0.4 }, { 3.3, 3.3, 1.5 } };
            const BodyToken Disc = ConstructShape(P, DiscShape, Built.Refusal);
            const PatternInstances Bosses = CircularPattern(P, CylinderShape({ 2, 0, 0.3 }, { 0, 0, 1 }, 0.35, 0.9), { 0, 0, 0 }, { 0, 0, 1 }, 8, Built.Operations);
            if (!Bosses.Refusal.empty()) { Built.Refusal = Bosses.Refusal; return Built; }
            for (const AnalyticShape& Shape : Bosses.Shapes) Built.Oracle.Added.push_back(Shape);
            Built.Body          = ChainBooleans(P, Disc, Bosses.Bodies, BooleanOperation::Union, Built);
            Built.Expected      = DiscVolume + 8.0 * π * 0.1225 * 0.6;
            Built.ExpectedGenus = 0;
            return Built;
        });
        Add("C", "C3a", "Hub ∪ circular array 6 spokes (oblique planes)", [](Partition& P) { return HubSpokes(P, 6); });
        Add("C", "C3b", "Hub ∪ circular array 12 spokes (overlapping inside the hub)", [](Partition& P) { return HubSpokes(P, 12); });
        Add("C", "C4", "Disc − 12 rim scallops r 0.5", [](Partition& P) { return DiscScallops(P); });
        Add("C", "C5", "Ring of 12 overlapping cylinders (closes on itself)", [](Partition& P) { return OverlappingRing(P); });
        Add("C", "C6", "Disc − 8 radial blind bores (traced cylinder ∩ cylinder)", [](Partition& P) { return RadialBores(P); });

        // ---- M · Mixed Boolean chains.
        Add("M", "M1", "Plate ∪ 6 bosses − 6 coaxial holes r 0.2 (12 Booleans)", [](Partition& P)
        {
            StressBuilt Built = PlateBosses(P, false);
            if (!Built.Refusal.empty()) return Built;
            const PatternInstances Holes = LinearPattern(P, Bore({ 1.5, 3, 0 }, 0.2, -0.5, 3.0), { 1.8, 0, 0 }, 6, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            Built.Oracle.Removed = Holes.Shapes;
            Built.Body           = ChainBooleans(P, Built.Body, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.Expected       = PlateVolume + 6.0 * π * 0.16 * 0.8 - 6.0 * π * 0.04 * 1.8;
            Built.ExpectedGenus  = 6;
            return Built;
        });
        Add("M", "M2", "Plate ∪ 6 bosses − 6 holes of the SAME radius (coincident cylinders)", [](Partition& P)
        {
            StressBuilt Built = PlateBosses(P, false);
            if (!Built.Refusal.empty()) return Built;
            const PatternInstances Holes = LinearPattern(P, Bore({ 1.5, 3, 0 }, 0.4, -0.5, 3.0), { 1.8, 0, 0 }, 6, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            Built.Oracle.Removed = Holes.Shapes;
            Built.Body           = ChainBooleans(P, Built.Body, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.Expected       = PlateVolume - 6.0 * π * 0.16;
            Built.ExpectedGenus  = 6;
            return Built;
        });
        Add("M", "M3", "Order independence: C1b in shuffled order = in order", [](Partition& P)
        {
            StressBuilt InOrder = DiscHoles(P, 12, 0.2);
            StressBuilt Built   = DiscHoles(P, 12, 0.2, { 7, 2, 11, 0, 5, 9, 3, 1, 10, 6, 4, 8 });
            if (!Built.Refusal.empty() || !InOrder.Refusal.empty()) { if (Built.Refusal.empty()) Built.Refusal = "in-order reference: " + InOrder.Refusal; return Built; }
            const double Gap = std::fabs(IntegrateBodyVolume(P, Built.Body) - IntegrateBodyVolume(P, InOrder.Body));
            Built.ExtraCondition = CountFaces(P, Built.Body) == CountFaces(P, InOrder.Body) && CountEdges(P, Built.Body) == CountEdges(P, InOrder.Body) && Gap < 1e-12;
            Built.Note = "F/E " + std::to_string(CountFaces(P, Built.Body)) + "/" + std::to_string(CountEdges(P, Built.Body)) + " vs " + std::to_string(CountFaces(P, InOrder.Body)) + "/" +
                         std::to_string(CountEdges(P, InOrder.Body)) + " · |ΔV| " + std::to_string(Gap);
            return Built;
        });
        Add("M", "M4", "Plate ∩ (∪ of 4 disjoint pins) → 4 lumps", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.8 }, { 12.5, 6.5, 1.8 } };
            const BodyToken Plate = ConstructShape(P, PlateShape, Built.Refusal);
            const PatternInstances Pins = LinearPattern(P, Bore({ 2, 3, 0 }, 0.5), { 3, 0, 0 }, 4, Built.Operations);
            if (!Pins.Refusal.empty()) { Built.Refusal = Pins.Refusal; return Built; }
            Built.Oracle.Within = Pins.Shapes;
            const BodyToken Cluster = ChainBooleans(P, Pins.Bodies.front(), std::vector<BodyToken>(Pins.Bodies.begin() + 1, Pins.Bodies.end()), BooleanOperation::Union, Built);
            if (!Built.Refusal.empty()) return Built;
            const BooleanOutcome Common = ComputeBoolean(P, Plate, Cluster, BooleanOperation::Intersection);
            ++Built.Operations;
            if (!Common.Refusal.empty()) { Built.Refusal = "intersection: " + Common.Refusal; return Built; }
            Built.Body           = Common.Body;
            Built.Expected       = 4.0 * π * 0.25;
            Built.ExpectedSolids = 4;
            return Built;
        });
        Add("M", "M5", "Block − 3 × 3 crossing bores, equal r 0.3 (9 Steinmetz crossings, 18 singular points)", [](Partition& P) { return CrossBores(P, 0.3, 0.3); });
        Add("M", "M6", "Block − 3 × 3 crossing bores, r 0.3 × r 0.2 (traced crossings)", [](Partition& P) { return CrossBores(P, 0.3, 0.2); });

        // ---- B · Blends on Boolean results (closed forms).
        const auto TopPlane = [](double Height) { return [=](Vector3 Start, Vector3 End, const CurveGeometry&) { return std::fabs(Start.Z - Height) < 1e-9 && std::fabs(End.Z - Height) < 1e-9; }; };
        const auto RibTop   = [](Vector3 Start, Vector3 End, const CurveGeometry&)              // long rib-top edges only (x constant)
        {
            return std::fabs(Start.Z - 1.7) < 1e-9 && std::fabs(End.Z - 1.7) < 1e-9 && std::fabs(Start.X - End.X) < 1e-9;
        };
        const auto PlateRim = [](Vector3 Start, Vector3 End, const CurveGeometry& Curve)       // the 12 plate box edges, not bore seams
        {
            const auto OnSide = [](Vector3 Point) { return std::fabs(Point.X) < 1e-9 || std::fabs(Point.X - 12.0) < 1e-9 || std::fabs(Point.Y) < 1e-9 || std::fabs(Point.Y - 6.0) < 1e-9; };
            return std::holds_alternative<StraightLine>(Curve) && OnSide(Start) && OnSide(End);
        };
        Add("B", "B1", "L1b + fillet 4 hole rims (top) r 0.1", [](Partition& P)
        {
            StressBuilt Built = PlateHoles(P, 4);
            BlendStep(P, Built, CircleAt(1.0, 0.3), 4, BlendProfile::Fillet, 0.1);
            Built.Expected -= 4.0 * BoreRimFilletLoss(0.3, 0.1);
            return Built;
        });
        Add("B", "B2", "L1b + chamfer 4 hole rims c 0.1", [](Partition& P)
        {
            StressBuilt Built = PlateHoles(P, 4);
            BlendStep(P, Built, CircleAt(1.0, 0.3), 4, BlendProfile::Chamfer, 0.1);
            Built.Expected -= 4.0 * BoreRimChamferLoss(0.3, 0.1);
            return Built;
        });
        Add("B", "B3", "L1b + fillet all 8 rims (top + bottom) r 0.1", [](Partition& P)
        {
            StressBuilt Built = PlateHoles(P, 4);
            BlendStep(P, Built, [](Vector3 Start, Vector3 End, const CurveGeometry& Curve) { return CircleAt(1.0, 0.3)(Start, End, Curve) || CircleAt(0.0, 0.3)(Start, End, Curve); }, 8,
                      BlendProfile::Fillet, 0.1);
            Built.Expected -= 8.0 * BoreRimFilletLoss(0.3, 0.1);
            return Built;
        });
        Add("B", "B4", "C1b + fillet 12 hole rims r 0.05", [](Partition& P)
        {
            StressBuilt Built = DiscHoles(P, 12, 0.2);
            BlendStep(P, Built, CircleAt(0.6, 0.2), 12, BlendProfile::Fillet, 0.05);
            Built.Expected -= 12.0 * BoreRimFilletLoss(0.2, 0.05);
            return Built;
        });
        Add("B", "B5", "L2 (40 holes) + fillet 40 rims r 0.05 in one operation", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 1.5 } };
            const BodyToken Plate = ConstructShape(P, PlateShape, Built.Refusal);
            const PatternInstances Holes = GridPattern(P, Bore({ 1, 1.2, 0 }, 0.25), { 1.1, 0, 0 }, 10, { 0, 1.2, 0 }, 4, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            Built.Oracle.Removed = Holes.Shapes;
            Built.Body           = ChainBooleans(P, Plate, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.Expected       = PlateVolume - 40.0 * π * 0.0625 - 40.0 * BoreRimFilletLoss(0.25, 0.05);
            Built.ExpectedGenus  = 40;
            BlendStep(P, Built, CircleAt(1.0, 0.25), 40, BlendProfile::Fillet, 0.05);
            return Built;
        });
        Add("B", "B6", "L5a + fillet 6 boss top rims r 0.1 (convex)", [](Partition& P)
        {
            StressBuilt Built = PlateBosses(P, false);
            BlendStep(P, Built, CircleAt(1.8, 0.4), 6, BlendProfile::Fillet, 0.1);
            Built.Expected -= 6.0 * BossRimFilletLoss(0.4, 0.1);
            return Built;
        });
        Add("B", "B7", "L5a + fillet 6 boss feet r 0.1 (concave, adds material)", [](Partition& P)
        {
            StressBuilt Built = PlateBosses(P, false);
            BlendStep(P, Built, CircleAt(1.0, 0.4), 6, BlendProfile::Fillet, 0.1);
            Built.Expected += 6.0 * BossFootFilletGain(0.4, 0.1);
            return Built;
        });
        Add("B", "B8", "L5a + chamfer 6 boss top rims c 0.1", [](Partition& P)
        {
            StressBuilt Built = PlateBosses(P, false);
            BlendStep(P, Built, CircleAt(1.8, 0.4), 6, BlendProfile::Chamfer, 0.1);
            Built.Expected -= 6.0 * BossRimChamferLoss(0.4, 0.1);
            return Built;
        });
        Add("B", "B9", "L6 + fillet 10 rib top edges r 0.1", [=](Partition& P)
        {
            StressBuilt Built = PlateRibs(P);
            BlendStep(P, Built, RibTop, 10, BlendProfile::Fillet, 0.1);
            Built.Expected -= 10.0 * EdgeFilletLoss(0.1, 6.0);
            return Built;
        });
        const auto RibFoot = [](Vector3 Start, Vector3 End, const CurveGeometry&)
        {
            return std::fabs(Start.Z - 1.0) < 1e-9 && std::fabs(End.Z - 1.0) < 1e-9 && std::fabs(Start.X - End.X) < 1e-9 && Start.X > 0.5 && Start.X < 11.5;
        };
        Add("B", "B10", "L6 + fillet 10 rib foot edges r 0.1 (concave)", [=](Partition& P)
        {
            StressBuilt Built = PlateRibs(P);
            BlendStep(P, Built, RibFoot, 10, BlendProfile::Fillet, 0.1);
            Built.Expected += 10.0 * EdgeFilletLoss(0.1, 6.0);
            return Built;
        });
        Add("B", "B11", "L6 + chamfer 10 rib top edges c 0.1", [=](Partition& P)
        {
            StressBuilt Built = PlateRibs(P);
            BlendStep(P, Built, RibTop, 10, BlendProfile::Chamfer, 0.1);
            Built.Expected -= 10.0 * EdgeChamferLoss(0.1, 6.0);
            return Built;
        });
        Add("B", "B12", "L6 + fillet 10 top AND 10 foot rib edges at once", [=](Partition& P)
        {
            StressBuilt Built = PlateRibs(P);
            BlendStep(P, Built, [=](Vector3 Start, Vector3 End, const CurveGeometry& Curve) { return RibTop(Start, End, Curve) || RibFoot(Start, End, Curve); }, 20,
                      BlendProfile::Fillet, 0.1);
            return Built;                                                                     // top loss and foot gain cancel
        });
        Add("B", "B13", "L7 merged block + fillet all edges r 0.2 (needs maximal faces)", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { {}, {}, {}, { -0.5, -0.5, -0.5 }, { 6.5, 1.5, 1.5 } };
            const PatternInstances Blocks = LinearPattern(P, BoxShape({ 0, 0, 0 }, { 1, 1, 1 }), { 1, 0, 0 }, 6, Built.Operations);
            if (!Blocks.Refusal.empty()) { Built.Refusal = Blocks.Refusal; return Built; }
            Built.Oracle.Added = Blocks.Shapes;
            Built.Body = ChainBooleans(P, Blocks.Bodies.front(), std::vector<BodyToken>(Blocks.Bodies.begin() + 1, Blocks.Bodies.end()), BooleanOperation::Union, Built);
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry&) { return true; }, 12, BlendProfile::Fillet, 0.2);
            Built.Expected = RoundedBoxVolume(6.0, 1.0, 1.0, 0.2);
            return Built;
        });
        Add("B", "B14", "M1 + fillet boss rims AND hole rims on the boss tops r 0.05", [](Partition& P)
        {
            StressBuilt Built = PlateBosses(P, false);
            if (!Built.Refusal.empty()) return Built;
            const PatternInstances Holes = LinearPattern(P, Bore({ 1.5, 3, 0 }, 0.2, -0.5, 3.0), { 1.8, 0, 0 }, 6, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            Built.Oracle.Removed = Holes.Shapes;
            Built.Body = ChainBooleans(P, Built.Body, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.ExpectedGenus = 6;
            BlendStep(P, Built, [](Vector3 Start, Vector3 End, const CurveGeometry& Curve) { return CircleAt(1.8, 0.4)(Start, End, Curve) || CircleAt(1.8, 0.2)(Start, End, Curve); }, 12,
                      BlendProfile::Fillet, 0.05);
            Built.Expected = PlateVolume + 6.0 * π * 0.16 * 0.8 - 6.0 * π * 0.04 * 1.8 - 6.0 * (BossRimFilletLoss(0.4, 0.05) + BoreRimFilletLoss(0.2, 0.05));
            return Built;
        });
        Add("B", "B15", "L1c + fillet the 12 outer plate edges r 0.2", [=](Partition& P)
        {
            StressBuilt Built = PlateHoles(P, 10);
            BlendStep(P, Built, PlateRim, 12, BlendProfile::Fillet, 0.2);
            Built.Expected = RoundedBoxVolume(12.0, 6.0, 1.0, 0.2) - 10.0 * π * 0.09;
            return Built;
        });
        Add("B", "B16", "B1 then a second blend: outer plate edges r 0.2 (blend after blend)", [=](Partition& P)
        {
            StressBuilt Built = PlateHoles(P, 4);
            BlendStep(P, Built, CircleAt(1.0, 0.3), 4, BlendProfile::Fillet, 0.1);
            BlendStep(P, Built, PlateRim, 12, BlendProfile::Fillet, 0.2);
            Built.Expected = RoundedBoxVolume(12.0, 6.0, 1.0, 0.2) - 4.0 * π * 0.09 - 4.0 * BoreRimFilletLoss(0.3, 0.1);
            return Built;
        });
        Add("B", "B17", "Blend then Boolean: rounded plate (r 0.2) − linear array 4 holes", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 1.5 } };
            Built.Body = ConstructShape(P, PlateShape, Built.Refusal);
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry&) { return true; }, 12, BlendProfile::Fillet, 0.2);
            if (!Built.Refusal.empty()) return Built;
            const PatternInstances Holes = LinearPattern(P, Bore({ 1, 3, 0 }, 0.3), { 1, 0, 0 }, 4, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            Built.Oracle.Removed = Holes.Shapes;
            Built.Body           = ChainBooleans(P, Built.Body, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.Expected       = RoundedBoxVolume(12.0, 6.0, 1.0, 0.2) - 4.0 * π * 0.09;
            Built.ExpectedGenus  = 4;
            return Built;
        });
        Add("B", "B18", "M5 cross-bores + fillet the 12 bore entry rims r 0.05", [](Partition& P)
        {
            StressBuilt Built = CrossBores(P, 0.3, 0.3);
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry& Curve)
            {
                const auto* Arc = std::get_if<CircularArc>(&Curve);
                if (!Arc || std::fabs(Arc->Radius - 0.3) > 1e-9) return false;
                const Vector3 Centre = Arc->Frame.Origin;
                return std::fabs(Centre.X) < 1e-9 || std::fabs(Centre.X - 4.0) < 1e-9 || std::fabs(Centre.Y) < 1e-9 || std::fabs(Centre.Y - 4.0) < 1e-9;
            }, 12, BlendProfile::Fillet, 0.05);
            Built.Expected -= 12.0 * BoreRimFilletLoss(0.3, 0.05);
            return Built;
        });
        Add("B", "B19", "C2 + fillet 8 boss feet r 0.08 (concave rims on a disc)", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { DiscShape }, {}, {}, { -3.3, -3.3, -0.4 }, { 3.3, 3.3, 1.5 } };
            const BodyToken Disc = ConstructShape(P, DiscShape, Built.Refusal);
            const PatternInstances Bosses = CircularPattern(P, CylinderShape({ 2, 0, 0.3 }, { 0, 0, 1 }, 0.35, 0.9), { 0, 0, 0 }, { 0, 0, 1 }, 8, Built.Operations);
            if (!Bosses.Refusal.empty()) { Built.Refusal = Bosses.Refusal; return Built; }
            for (const AnalyticShape& Shape : Bosses.Shapes) Built.Oracle.Added.push_back(Shape);
            Built.Body          = ChainBooleans(P, Disc, Bosses.Bodies, BooleanOperation::Union, Built);
            Built.ExpectedGenus = 0;
            BlendStep(P, Built, CircleAt(0.6, 0.35), 8, BlendProfile::Fillet, 0.08);
            Built.Expected = DiscVolume + 8.0 * π * 0.1225 * 0.6 + 8.0 * BossFootFilletGain(0.35, 0.08);
            return Built;
        });
        Add("B", "B20", "L1b + fillet 4 rims r 0.5 — neighbouring blends would overlap (must refuse)", [](Partition& P)
        {
            StressBuilt Built = PlateHoles(P, 4);
            BlendStep(P, Built, CircleAt(1.0, 0.3), 4, BlendProfile::Fillet, 0.5);
            Built.Expected     -= 4.0 * BoreRimFilletLoss(0.3, 0.5);                          // naive sum of 4 disjoint tori: |ΔV| ≈ 0 exposes double counting
            Built.ExpectRefusal = true;
            return Built;
        });
        Add("B", "B21", "L1b + fillet 4 rims r 0.19 — footprints 0.02 apart (legal, closed form)", [](Partition& P)
        {
            StressBuilt Built = PlateHoles(P, 4);
            BlendStep(P, Built, CircleAt(1.0, 0.3), 4, BlendProfile::Fillet, 0.19);
            Built.Expected -= 4.0 * BoreRimFilletLoss(0.3, 0.19);
            return Built;
        });
        Add("B", "B22", "L1b + fillet 4 rims r 0.21 — footprints overlap by 0.02 (must refuse)", [](Partition& P)
        {
            StressBuilt Built = PlateHoles(P, 4);
            BlendStep(P, Built, CircleAt(1.0, 0.3), 4, BlendProfile::Fillet, 0.21);
            Built.Expected     -= 4.0 * BoreRimFilletLoss(0.3, 0.21);
            Built.ExpectRefusal = true;
            return Built;
        });

        // ---- T · Tangent-continuous chains (line ↔ arc across a smooth edge): the joint is a shared cross-section.
        const auto StadiumSlot = [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 1.5 } };
            const BodyToken Plate = ConstructShape(P, PlateShape, Built.Refusal);
            const std::vector<AnalyticShape> Cutters = { BoxShape({ 3, 2.6, -0.5 }, { 7, 3.4, 1.5 }), Bore({ 3, 3, 0 }, 0.4), Bore({ 7, 3, 0 }, 0.4) };
            std::vector<BodyToken> Tools;
            for (const AnalyticShape& Cutter : Cutters) { Tools.push_back(ConstructShape(P, Cutter, Built.Refusal)); ++Built.Operations; }
            if (!Built.Refusal.empty()) return Built;
            Built.Oracle.Removed = Cutters;
            Built.Body           = ChainBooleans(P, Plate, Tools, BooleanOperation::Difference, Built);
            Built.Expected       = PlateVolume - (4.0 * 0.8 + π * 0.16);
            Built.ExpectedGenus  = 1;
            return Built;
        };
        const auto SlotRim = [](double Height)
        {
            return [=](Vector3 Start, Vector3 End, const CurveGeometry&)
            {
                const auto Inner = [](Vector3 Point) { return Point.X > 2.0 && Point.X < 8.0 && Point.Y > 2.0 && Point.Y < 4.0; };
                return std::fabs(Start.Z - Height) < 1e-9 && std::fabs(End.Z - Height) < 1e-9 && Inner(Start) && Inner(End);
            };
        };
        Add("T", "T1", "Plate − stadium slot (box + 2 tangent cylinders) + fillet the top rim r 0.1", [=](Partition& P)
        {
            StressBuilt Built = StadiumSlot(P);
            BlendStep(P, Built, SlotRim(1.0), -1, BlendProfile::Fillet, 0.1);
            Built.Expected -= 2.0 * EdgeFilletLoss(0.1, 4.0) + BoreRimFilletLoss(0.4, 0.1);
            return Built;
        });
        Add("T", "T2", "Stadium slot + chamfer the top rim c 0.1 (planes and cones, tangent joints)", [=](Partition& P)
        {
            StressBuilt Built = StadiumSlot(P);
            BlendStep(P, Built, SlotRim(1.0), -1, BlendProfile::Chamfer, 0.1);
            Built.Expected -= 2.0 * EdgeChamferLoss(0.1, 4.0) + BoreRimChamferLoss(0.4, 0.1);
            return Built;
        });
        Add("T", "T3", "Stadium slot + fillet top AND bottom rims r 0.1 in one operation", [=](Partition& P)
        {
            StressBuilt Built = StadiumSlot(P);
            BlendStep(P, Built, [=](Vector3 Start, Vector3 End, const CurveGeometry& Curve) { return SlotRim(1.0)(Start, End, Curve) || SlotRim(0.0)(Start, End, Curve); }, -1,
                      BlendProfile::Fillet, 0.1);
            Built.Expected -= 2.0 * (2.0 * EdgeFilletLoss(0.1, 4.0) + BoreRimFilletLoss(0.4, 0.1));
            return Built;
        });
        const auto RoundedBlock = [](Partition& P)
        {
            StressBuilt Built;
            const AnalyticShape Block = BoxShape({ 0, 0, 0 }, { 4, 2, 1 });
            Built.Oracle = { { Block }, {}, {}, { -0.5, -0.5, -0.5 }, { 4.5, 2.5, 1.5 } };
            Built.Body   = ConstructShape(P, Block, Built.Refusal);
            BlendStep(P, Built, [](Vector3 Start, Vector3 End, const CurveGeometry&) { return std::fabs(Start.X - End.X) < 1e-9 && std::fabs(Start.Y - End.Y) < 1e-9; }, 4,
                      BlendProfile::Fillet, 0.5);
            Built.Expected = 8.0 - 4.0 * EdgeFilletLoss(0.5, 1.0);
            // 💡 The rounded block is modelled exactly (2 boxes ∪ 4 corner cylinders), so only the second blend needs a clearance.
            Built.Oracle.Added = { BoxShape({ 0.5, 0, 0 }, { 3.5, 2, 1 }), BoxShape({ 0, 0.5, 0 }, { 4, 1.5, 1 }) };
            for (const Vector3 Corner : { Vector3{ 0.5, 0.5, 0 }, Vector3{ 3.5, 0.5, 0 }, Vector3{ 0.5, 1.5, 0 }, Vector3{ 3.5, 1.5, 0 } })
            {
                Built.Oracle.Added.push_back(CylinderShape(Corner, { 0, 0, 1 }, 0.5, 1.0));
            }
            Built.OracleClearance = 1.0e-3;
            return Built;
        };
        const auto TopLoop = [](Vector3 Start, Vector3 End, const CurveGeometry&) { return std::fabs(Start.Z - 1.0) < 1e-9 && std::fabs(End.Z - 1.0) < 1e-9; };
        Add("T", "T4", "Block, fillet 4 vertical edges r 0.5, then fillet the top loop r 0.2 (lines + quarter tori)", [=](Partition& P)
        {
            StressBuilt Built = RoundedBlock(P);
            BlendStep(P, Built, TopLoop, -1, BlendProfile::Fillet, 0.2);
            Built.Expected -= EdgeFilletLoss(0.2, 8.0) + BossRimFilletLoss(0.5, 0.2);
            return Built;
        });
        Add("T", "T5", "Block, fillet 4 vertical edges r 0.5, then chamfer the top loop c 0.1 (planes + cones)", [=](Partition& P)
        {
            StressBuilt Built = RoundedBlock(P);
            BlendStep(P, Built, TopLoop, -1, BlendProfile::Chamfer, 0.1);
            Built.Expected -= EdgeChamferLoss(0.1, 8.0) + BossRimChamferLoss(0.5, 0.1);
            return Built;
        });
        Add("T", "T7", "Stadium slot + fillet r 0.1 seeded with ONE rim line, tangent chain on (must equal T1)", [=](Partition& P)
        {
            StressBuilt Built = StadiumSlot(P);
            BlendStep(P, Built, [=](Vector3 Start, Vector3 End, const CurveGeometry& Curve)
                      { return SlotRim(1.0)(Start, End, Curve) && std::fabs(Start.Y - 2.6) < 1e-9 && std::fabs(End.Y - 2.6) < 1e-9; }, 1,
                      BlendProfile::Fillet, 0.1, true);
            Built.Expected -= 2.0 * EdgeFilletLoss(0.1, 4.0) + BoreRimFilletLoss(0.4, 0.1);
            return Built;
        });
        Add("T", "T8", "Rounded block + chamfer c 0.1 seeded with ONE top line, tangent chain on (must equal T5)", [=](Partition& P)
        {
            StressBuilt Built = RoundedBlock(P);
            BlendStep(P, Built, [=](Vector3 Start, Vector3 End, const CurveGeometry& Curve)
                      { return TopLoop(Start, End, Curve) && std::fabs(Start.Y) < 1e-9 && std::fabs(End.Y) < 1e-9; }, 1,
                      BlendProfile::Chamfer, 0.1, true);
            Built.Expected -= EdgeChamferLoss(0.1, 8.0) + BossRimChamferLoss(0.5, 0.1);
            return Built;
        });
        Add("T", "T6", "Plate ∪ boss 2 × 1, round the boss verticals r 0.3, then fillet the concave foot loop r 0.1", [](Partition& P)
        {
            StressBuilt Built;
            const AnalyticShape Boss = BoxShape({ 5, 2.5, 0.5 }, { 7, 3.5, 1.5 });
            Built.Oracle = { { PlateShape, Boss }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 2.0 } };
            const BodyToken Plate = ConstructShape(P, PlateShape, Built.Refusal);
            const BodyToken Lump  = ConstructShape(P, Boss, Built.Refusal);
            if (!Built.Refusal.empty()) return Built;
            Built.Body = ChainBooleans(P, Plate, { Lump }, BooleanOperation::Union, Built);
            BlendStep(P, Built, [](Vector3 Start, Vector3 End, const CurveGeometry&)
            {
                return std::fabs(Start.X - End.X) < 1e-9 && std::fabs(Start.Y - End.Y) < 1e-9 && Start.X > 4.5 && Start.X < 7.5 && Start.Y > 2.0 && Start.Y < 4.0;
            }, 4, BlendProfile::Fillet, 0.3);
            BlendStep(P, Built, [](Vector3 Start, Vector3 End, const CurveGeometry&)
            {
                return std::fabs(Start.Z - 1.0) < 1e-9 && std::fabs(End.Z - 1.0) < 1e-9 && Start.X > 4.5 && Start.X < 7.5 && Start.Y > 2.0 && Start.Y < 4.0;
            }, -1, BlendProfile::Fillet, 0.1);
            Built.Expected = PlateVolume + 2.0 * 1.0 * 0.5 - 4.0 * EdgeFilletLoss(0.3, 0.5) + EdgeFilletLoss(0.1, 3.6) + BossFootFilletGain(0.3, 0.1);
            return Built;
        });

        // ---- P · Probes without a closed form: soundness, genus, oracle away from the blends, watertight tessellation.
        Add("P", "P1", "L4 slot + fillet every top rim arc r 0.05 (arcs meet at cusps)", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { PlateShape }, {}, {}, { -0.5, -0.5, -0.5 }, { 12.5, 6.5, 1.5 } };
            const BodyToken Plate = ConstructShape(P, PlateShape, Built.Refusal);
            const PatternInstances Holes = LinearPattern(P, Bore({ 2, 3, 0 }, 0.4), { 0.5, 0, 0 }, 5, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return Built; }
            Built.Oracle.Removed = Holes.Shapes;
            Built.Body           = ChainBooleans(P, Plate, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.ExpectedGenus  = 1;
            BlendStep(P, Built, CircleAt(1.0, 0.4), -1, BlendProfile::Fillet, 0.05);
            return Built;
        });
        Add("P", "P2", "C4 scallops + fillet every top edge r 0.05", [](Partition& P)
        {
            StressBuilt Built = DiscScallops(P);
            Built.Expected = std::nan("");
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry& Curve)
            {
                const auto* Arc = std::get_if<CircularArc>(&Curve);
                return Arc && std::fabs(Arc->Frame.Origin.Z - 0.6) < 1e-9;
            }, -1, BlendProfile::Fillet, 0.05);
            return Built;
        });
        Add("P", "P3", "C4 scallops + chamfer every top edge c 0.05", [](Partition& P)
        {
            StressBuilt Built = DiscScallops(P);
            Built.Expected = std::nan("");
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry& Curve)
            {
                const auto* Arc = std::get_if<CircularArc>(&Curve);
                return Arc && std::fabs(Arc->Frame.Origin.Z - 0.6) < 1e-9;
            }, -1, BlendProfile::Chamfer, 0.05);
            return Built;
        });
        Add("P", "P4", "C3a spokes + fillet the 12 vertical spoke–hub edges r 0.05 (concave plane ∩ cylinder)", [](Partition& P)
        {
            StressBuilt Built = HubSpokes(P, 6);
            Built.Expected = std::nan("");
            BlendStep(P, Built, [](Vector3 Start, Vector3 End, const CurveGeometry& Curve)
            {
                return std::holds_alternative<StraightLine>(Curve) && std::fabs(Start.X - End.X) < 1e-9 && std::fabs(Start.Y - End.Y) < 1e-9 && std::fabs(std::hypot(Start.X, Start.Y) - 1.0) < 1e-6 &&
                       std::fabs(Start.Y) > 1e-6;                                         // not the hub seam at (1, 0), split by spoke 0
            }, 12, BlendProfile::Fillet, 0.05);
            return Built;
        });
        Add("P", "P5", "C3a spokes + fillet the spoke top edges r 0.05 (lines ending on the hub)", [=](Partition& P)
        {
            StressBuilt Built = HubSpokes(P, 6);
            Built.Expected = std::nan("");
            BlendStep(P, Built, [=](Vector3 Start, Vector3 End, const CurveGeometry& Curve)
            {
                return std::holds_alternative<StraightLine>(Curve) && TopPlane(0.8)(Start, End, Curve) && std::hypot(Start.X, Start.Y) > 0.99 && std::hypot(End.X, End.Y) > 0.99 &&
                       std::fabs(std::hypot(Start.X, Start.Y) - std::hypot(End.X, End.Y)) > 0.5;
            }, 12, BlendProfile::Fillet, 0.05);
            return Built;
        });
        Add("P", "P6", "C5 ring + fillet every top edge r 0.05 (convex outer, concave inner cusps)", [](Partition& P)
        {
            StressBuilt Built = OverlappingRing(P);
            Built.Expected = std::nan("");
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry& Curve)
            {
                const auto* Arc = std::get_if<CircularArc>(&Curve);
                return Arc && std::fabs(Arc->Frame.Origin.Z - 0.5) < 1e-9;
            }, -1, BlendProfile::Fillet, 0.05);
            return Built;
        });
        Add("P", "P7", "C6 radial bores + fillet the 8 traced bore mouths r 0.03", [](Partition& P)
        {
            StressBuilt Built = RadialBores(P);
            Built.Expected = std::nan("");
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry& Curve) { return std::holds_alternative<IntersectionCurve>(Curve); }, -1, BlendProfile::Fillet, 0.03);
            return Built;
        });
        Add("P", "P8", "M5 equal cross-bores + fillet the crossing edges r 0.03 (elliptical sections)", [](Partition& P)
        {
            StressBuilt Built = CrossBores(P, 0.3, 0.3);
            Built.Expected = std::nan("");
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry& Curve) { return !std::holds_alternative<CircularArc>(Curve) && !std::holds_alternative<StraightLine>(Curve); }, -1,
                      BlendProfile::Fillet, 0.03);
            return Built;
        });
        Add("P", "P10", "C6 radial bores + chamfer the 8 traced bore mouths d 0.03 (ball-law spine, chord carrier)", [](Partition& P)
        {
            StressBuilt Built = RadialBores(P);
            Built.Expected = std::nan("");
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry& Curve) { return std::holds_alternative<IntersectionCurve>(Curve); }, -1, BlendProfile::Chamfer, 0.03);
            return Built;
        });
        Add("P", "P9", "L5b touching bosses + fillet 6 foot circles r 0.1", [](Partition& P)
        {
            StressBuilt Built = PlateBosses(P, true);
            BlendStep(P, Built, CircleAt(1.0, 0.4), 6, BlendProfile::Fillet, 0.1);
            Built.Expected += 6.0 * BossFootFilletGain(0.4, 0.1);
            return Built;
        });
        // 📝 V family: setback vertex blends (fair NURBS quads where convex and concave blends meet, or ≥ 4 blends at one vertex).
        Add("V", "V1", "L block (prism) + fillet all 18 edges r 0.1 (2 setback vertices: 2 convex + 1 concave blend)", [](Partition& P)
        {
            StressBuilt Built;
            Built.Oracle = { { BoxShape({ 0, 0, 0 }, { 2, 1.5, 1 }), BoxShape({ 1, 0, 1 }, { 2, 1.5, 2 }) }, {}, {}, { -0.5, -0.5, -0.5 }, { 2.5, 2.0, 2.5 } };
            const auto Block = ConstructPrism(P, { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 0, 2 }, { 2, 0, 2 }, { 2, 0, 0 }, { 0, 0, 0 } }, { 0, 1.5, 0 });
            ++Built.Operations;
            if (!Block.SucceededCondition()) { Built.Refusal = Block.Refusal; return Built; }
            Built.Body          = Block.Value.Body;
            Built.Expected      = std::nan("");
            Built.ExpectedGenus = 0;
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry&) { return true; }, 18, BlendProfile::Fillet, 0.1);
            return Built;
        });
        Add("V", "V2", "Square pyramid (prism ∩ prism) + fillet all 8 edges r 0.1 (degree-4 apex → setback)", [](Partition& P)
        {
            StressBuilt Built;
            const double Slant = 1.0 / std::numbers::sqrt2;
            Built.Oracle = { { BoxShape({ -1, -1, 0 }, { 1, 1, 1 }) },
                             { HalfSpaceShape({ 1, 0, 0 }, { -Slant, 0, -Slant }), HalfSpaceShape({ -1, 0, 0 }, { Slant, 0, -Slant }),
                               HalfSpaceShape({ 0, 1, 0 }, { 0, -Slant, -Slant }), HalfSpaceShape({ 0, -1, 0 }, { 0, Slant, -Slant }) },
                             {}, { -1.5, -1.5, -0.5 }, { 1.5, 1.5, 1.5 } };
            const auto First  = ConstructPrism(P, { { 0, -1.5, 1 }, { 1, -1.5, 0 }, { -1, -1.5, 0 } }, { 0, 3, 0 });
            const auto Second = ConstructPrism(P, { { -1.5, -1.5, -0.5 }, { -1.5, 1.5, -0.5 }, { -1.5, 0, 1 } }, { 3, 0, 0 });
            Built.Operations += 3;
            if (!First.SucceededCondition() || !Second.SucceededCondition()) { Built.Refusal = "prism construction refused"; return Built; }
            const BooleanOutcome Pyramid = ComputeBoolean(P, First.Value.Body, Second.Value.Body, BooleanOperation::Intersection);
            if (!Pyramid.Refusal.empty()) { Built.Refusal = Pyramid.Refusal; return Built; }
            Built.Body          = Pyramid.Body;
            Built.Expected      = std::nan("");
            Built.ExpectedGenus = 0;
            BlendStep(P, Built, [](Vector3, Vector3, const CurveGeometry&) { return true; }, 8, BlendProfile::Fillet, 0.1);
            return Built;
        });
        // 📝 O family: cliff overflow. A thin plate (t 0.2) − an arrayed row of drills, then its front top edge blended wider
        //    than the front face: the front face is consumed and the bottom face trims the blend. The oracle is exact
        //    (kept box ∪ the fillet cylinder, or the plate minus the chamfer half-space), so its clearance returns to 10⁻³.
        const auto ThinPlate = [](Partition& P, StressBuilt& Built)
        {
            const AnalyticShape Plate = BoxShape({ 0, 0, 0 }, { 4, 2, 0.2 });
            const BodyToken     Body  = ConstructShape(P, Plate, Built.Refusal);
            const PatternInstances Holes = LinearPattern(P, Bore({ 0.5, 1.2, 0 }, 0.2), { 1, 0, 0 }, 4, Built.Operations);
            if (!Holes.Refusal.empty()) { Built.Refusal = Holes.Refusal; return; }
            Built.Oracle        = { {}, Holes.Shapes, { Plate }, { -0.5, -0.5, -0.5 }, { 4.5, 2.5, 0.7 } };
            Built.Body          = ChainBooleans(P, Body, Holes.Bodies, BooleanOperation::Difference, Built);
            Built.ExpectedGenus = 4;
        };
        const auto FrontTop = [](Vector3 Start, Vector3 End, const CurveGeometry&) { return Start.Y < 1e-9 && End.Y < 1e-9 && Start.Z > 0.19 && End.Z > 0.19; };
        Add("O", "O1", "Thin plate t 0.2 − 4 arrayed drills + fillet the front top edge r 0.3 (cliff overflow onto the bottom face)", [=](Partition& P)
        {
            StressBuilt Built;
            ThinPlate(P, Built);
            BlendStep(P, Built, FrontTop, 1, BlendProfile::Fillet, 0.3);
            const auto Primitive = [](double Level) { return 0.5 * (Level * std::sqrt(0.09 - Level * Level) + 0.09 * std::asin(Level / 0.3)); };
            Built.Expected        = 1.6 - 4.0 * π * 0.04 * 0.2 - 4.0 * (0.3 * 0.2 - (Primitive(0.3) - Primitive(0.1)));
            Built.Oracle.Added    = { BoxShape({ 0, 0.3, 0 }, { 4, 2, 0.2 }), CylinderShape({ 0, 0.3, -0.1 }, { 1, 0, 0 }, 0.3, 4.0) };
            Built.OracleClearance = 1.0e-3;
            return Built;
        });
        Add("O", "O2", "Thin plate t 0.2 − 4 arrayed drills + chamfer the front top edge c 0.3 (cliff overflow, plane trimmed by the bottom)", [=](Partition& P)
        {
            StressBuilt Built;
            ThinPlate(P, Built);
            BlendStep(P, Built, FrontTop, 1, BlendProfile::Chamfer, 0.3);
            Built.Expected        = 1.6 - 4.0 * π * 0.04 * 0.2 - 4.0 * 0.5 * (0.09 - 0.01);
            Built.Oracle.Added    = { BoxShape({ 0, 0, 0 }, { 4, 2, 0.2 }) };
            Built.Oracle.Removed.push_back(HalfSpaceShape({ 0, 0.1, 0 }, { 0, 1.0 / std::numbers::sqrt2, -1.0 / std::numbers::sqrt2 }));
            Built.OracleClearance = 1.0e-3;
            return Built;
        });
        return Rows;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                MEASUREMENT
    //--------------------------------------------------------------------------------------------------------------------

    void MeasureOracle(const ModelPartition& Partition, const StressBuilt& Built, StressResult& Result)
    {
        const BodyContainment Containment = PrepareBodyContainment(Partition, Built.Body);
        uint64_t              Seed        = 0x9E3779B97F4A7C15ull;
        const auto Next = [&]() { Seed = Seed * 6364136223846793005ull + 1442695040888963407ull; return static_cast<double>(Seed >> 11) * (1.0 / 9007199254740992.0); };
        const ConstructiveOracle& Oracle = Built.Oracle;
        WindingSettings           Settings = {};                                                // only the sign of an integer winding is needed;
        Settings.OuterTolerance            = 1.0e-6;                                           // samples sit ≥ 1e-3 from every primitive, so a
        Settings.InnerTolerance            = 1.0e-7;                                           // 1e-6 quadrature error cannot flip the rounding
        for (int Attempt = 0; Attempt < 4000 && Result.Samples < 300; ++Attempt)
        {
            const Vector3 Point = { Oracle.Low.X + (Oracle.High.X - Oracle.Low.X) * Next(), Oracle.Low.Y + (Oracle.High.Y - Oracle.Low.Y) * Next(), Oracle.Low.Z + (Oracle.High.Z - Oracle.Low.Z) * Next() };
            if (OracleMargin(Oracle, Point) < Built.OracleClearance) continue;
            ++Result.Samples;
            const PointMembership Expected = OracleContains(Oracle, Point) ? PointMembership::Inside : PointMembership::Outside;
            if (ClassifySolidPoint(Containment, Point, Settings).Membership != Expected) ++Result.Wrong;
        }
    }

    // ----------------------------------------------------------------------------------------------------------------------
    // TESSELLATION
    // ----------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] bool MeasureTessellation(const ModelPartition& Partition, BodyToken Body, const std::string& Code, int64_t& Triangles, std::string& Detail)
    {
        const TessellationOutcome     Outcome = TessellateBody(Partition, Body, {});
        if (!ExportDirectory.empty()) ExportTessellation(Partition, Outcome, ExportDirectory, Code);
        const TessellationAuditReport Report  = AuditTessellation(Partition, Body, Outcome);
        Triangles = static_cast<int64_t>(Outcome.Complex.Triangles.size());
        const double VolumeRatio = std::fabs(Report.ComplexVolume - Report.ExactVolume) / (Outcome.Sag * Report.ComplexArea);
        const bool Watertight = Outcome.Refusal.empty() && Report.UnpairedSides == Report.LaminarSides && Report.EulerComplex == Report.EulerBrep && Report.Inverted == 0 && VolumeRatio <= 1.0 && Report.MissingEdgeSides == 0;
        if (!Watertight)
        {
            char Text[200];
            std::snprintf(Text, sizeof Text, "%s unpaired %lld · χ %d vs %d · inverted %lld · volume ratio %.3g · missing edge sides %lld", Outcome.Refusal.c_str(), static_cast<long long>(Report.UnpairedSides - Report.LaminarSides),
                          Report.EulerComplex, Report.EulerBrep, static_cast<long long>(Report.Inverted), VolumeRatio, static_cast<long long>(Report.MissingEdgeSides));
            Detail = Text;
        }
        return Watertight;
    }

    [[nodiscard]] StressResult Measure(const StressEntry& Entry)
    {
        ModelPartition Partition;
        StressResult   Result{ Entry };
        const auto     Start = Clock::now();
        Result.Built         = Entry.Build(Partition);
        const StressBuilt& Built = Result.Built;
        if (!Built.Refusal.empty() || !Partition.Bodies.Authenticate(Built.Body))
        {
            Result.Milliseconds = Elapsed(Start);
            Result.Verdict      = Built.ExpectRefusal ? "🟢" : "⚠️";
            Result.Finding      = Built.Refusal.empty() ? "no body" : Built.Refusal;
            return Result;
        }
        const BodyInventory Inventory = Partition.CollectInventory(Built.Body);
        auto                Moment    = Clock::now();
        const AuditReport   Audit     = AuditBody(Partition, Built.Body);
        Narrate("AuditBody", Moment);
        Result.Faces     = static_cast<int>(Inventory.Faces.size());
        Result.Edges     = static_cast<int>(Inventory.Edges.size());
        Result.Vertices  = static_cast<int>(Inventory.Vertices.size());
        Result.Genus     = Audit.Genus;
        Result.Solids    = static_cast<int>(Inventory.Regions.size()) - 1;
        Result.Redundant = CountRedundantEdges(Partition, Built.Body);
        Moment = Clock::now();
        Result.Volume    = IntegrateBodyVolume(Partition, Built.Body);
        Narrate("volume", Moment);
        Moment = Clock::now();
        if (Built.OracleEnabled) MeasureOracle(Partition, Built, Result);
        Narrate("membership oracle", Moment);
        Moment = Clock::now();
        std::string Seams;
        Result.Watertight   = MeasureTessellation(Partition, Built.Body, Entry.Code, Result.Triangles, Seams);
        Narrate("tessellation", Moment);
        Result.Milliseconds = Elapsed(Start);

        std::vector<std::string> Faults;
        if (Built.ExpectRefusal) Faults.push_back("built although the blends overlap");
        if (!Audit.SoundCondition()) Faults.push_back("audit: " + std::to_string(Audit.Findings.size()) + " findings (" + Audit.Findings.front().Code + " " + Audit.Findings.front().Subject + ")");
        if (Built.ExpectedGenus >= 0 && Audit.Genus != Built.ExpectedGenus) Faults.push_back("genus " + std::to_string(Audit.Genus) + " ≠ " + std::to_string(Built.ExpectedGenus));
        if (Result.Solids != Built.ExpectedSolids) Faults.push_back("lumps " + std::to_string(Result.Solids) + " ≠ " + std::to_string(Built.ExpectedSolids));
        if (!std::isnan(Built.Expected))
        {
            Result.Error = std::fabs(Result.Volume - Built.Expected);
            if (Result.Error > 1.0e-9 * std::max(1.0, std::fabs(Built.Expected))) Faults.push_back("ΔV " + std::to_string(Result.Error));
        }
        if (Result.Wrong > 0) Faults.push_back("oracle " + std::to_string(Result.Wrong) + " / " + std::to_string(Result.Samples) + " wrong");
        if (!Result.Watertight) Faults.push_back("tessellation not watertight (" + Seams + ")");
        if (Built.MaximalCondition && Result.Redundant > 0) Faults.push_back(std::to_string(Result.Redundant) + " redundant edges");
        if (!Built.ExtraCondition) Faults.push_back("comparison failed");
        Result.Verdict = Faults.empty() ? "🟢" : "🔴";
        for (const std::string& Fault : Faults) Result.Finding += (Result.Finding.empty() ? "" : "; ") + Fault;
        if (!Built.Note.empty()) Result.Finding += (Result.Finding.empty() ? "" : " · ") + Built.Note;
        return Result;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    const auto  Start = Clock::now();
    std::string OnlyCode;                                                                    // --only: family letter or row code
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Argument = Arguments[Index];
        if (Argument == "--only" && Index + 1 < ArgumentCount) OnlyCode = Arguments[++Index];
        if (Argument == "--verbose") VerboseCondition = true;
        if (Argument == "--export" && Index + 1 < ArgumentCount) ExportDirectory = Arguments[++Index];
    }
    std::vector<StressResult> Results;
    for (const StressEntry& Entry : AuthorStress())
    {
        if (!OnlyCode.empty() && Entry.Family != OnlyCode && Entry.Code != OnlyCode) continue;
        try
        {
            Results.push_back(Measure(Entry));
        }
        catch (const std::exception& Failure)                                                  // a throw is worse than a refusal: record it as 🔴
        {
            StressResult Crashed{ Entry };
            Crashed.Verdict = "🔴";
            Crashed.Finding = std::string("🐞 threw ") + Failure.what() + " (no refusal)";
            Results.push_back(Crashed);
        }
        const StressResult& Last = Results.back();
        std::fprintf(stderr, "%-4s %s %8.1f ms  %s\n", Last.Entry.Code.c_str(), Last.Verdict.c_str(), Last.Milliseconds, Last.Finding.c_str());
    }

    int Correct = 0, Refused = 0, Wrong = 0, Operations = 0;
    for (const StressResult& Result : Results)
    {
        Correct += Result.Verdict == "🟢" ? 1 : 0;
        Refused += Result.Verdict == "⚠️" ? 1 : 0;
        Wrong += Result.Verdict == "🔴" ? 1 : 0;
        Operations += Result.Built.Operations;
    }
    std::printf("# Pattern stress validation (arrays → Boolean chains → blends)\n\n");
    if (!OnlyCode.empty()) std::printf("> 🔍 Filtered run: `--only %s`\n\n", OnlyCode.c_str());
    std::printf("**%s** · %zu rows · 🟢 %d correct · ⚠️ %d refused (honest limit) · 🔴 %d wrong · %d modeller operations · %.1f s\n\n", Wrong == 0 ? "🟢 NO WRONG ANSWERS" : "🔴 WRONG ANSWERS",
                Results.size(), Correct, Refused, Wrong, Operations, Elapsed(Start) / 1000.0);
    std::printf("> 📝 Every row: modeller array (ArrayBody / CircularArrayBody) → Boolean chain → optional BlendEdges. Built rows must be audit-sound with the expected genus and lump count, "
                "match the hand-derived closed form to 10⁻⁹·max(1, V*), classify 300 random points like the analytic CSG oracle (points nearer a primitive boundary than max(10⁻³, 1.5 × blend size) "
                "are skipped) and tessellate watertight. A refusal is an honest limit (⚠️); a built wrong body is 🔴.\n\n");
    std::printf("| | Code | Scene | Ops | F / E / V | Genus | Lumps | V | V* | \\|ΔV\\| | Oracle wrong | Triangles | ms | Finding |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const StressResult& Result : Results)
    {
        const StressBuilt& Built = Result.Built;
        if (Result.Faces == 0)
        {
            std::printf("| %s | %s | %s | %d | – | – | – | – | %s | – | – | – | %.0f | %s |\n", Result.Verdict.c_str(), Result.Entry.Code.c_str(), Result.Entry.Scene.c_str(), Built.Operations,
                        std::isnan(Built.Expected) ? "–" : std::to_string(Built.Expected).c_str(), Result.Milliseconds, Result.Finding.c_str());
            continue;
        }
        char Closed[32] = "–", Gap[32] = "–";
        if (!std::isnan(Built.Expected)) { std::snprintf(Closed, sizeof(Closed), "%.10f", Built.Expected); std::snprintf(Gap, sizeof(Gap), "%.1e", Result.Error); }
        std::printf("| %s | %s | %s | %d | %d / %d / %d | %d | %d | %.10f | %s | %s | %d / %d | %lld | %.0f | %s |\n", Result.Verdict.c_str(), Result.Entry.Code.c_str(), Result.Entry.Scene.c_str(),
                    Built.Operations, Result.Faces, Result.Edges, Result.Vertices, Result.Genus, Result.Solids, Result.Volume, Closed, Gap, Result.Wrong, Result.Samples,
                    static_cast<long long>(Result.Triangles), Result.Milliseconds, Result.Finding.empty() ? "–" : Result.Finding.c_str());
    }
    std::fflush(stdout);
    return Wrong == 0 ? 0 : 1;
}
