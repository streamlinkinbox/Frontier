/*====================================================================================================================================
                                                   ROLLINGBALLVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Exact procedural rolling-ball carrier: ball, contact, G1, chart, analytic reproduction, sections, bodies and transforms

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Blend/BlendCatalogue.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/BodyTransform.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Geometry/RollingBallCarrier.h"
#include "Engine/GeometryProcessing/Intersection/ImplicitSurface.h"
#include "Engine/GeometryProcessing/Intersection/IntersectionCurve.h"
#include "Engine/GeometryProcessing/Intersection/SurfaceIntersection.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    using Clock = std::chrono::steady_clock;
    constexpr double FullTurn = 2.0 * std::numbers::pi;                                      // [rad] - 2π

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    // 📝 One gate: worst measured value against its limit over every check.
    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - R1 …
        const char* Subject  = "";                                                           // [-] - What is measured
        const char* Limit    = "";                                                           // [-] - Acceptance, as printed
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-] - Measurements
        long        Failures = 0;                                                            // [-] - Measurements over the limit
    };

    void Tally(GateTally& Gate, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, std::isfinite(Value) ? Value : 1.0e300);
        Gate.Checks += 1;
        Gate.Failures += PassedCondition && std::isfinite(Value) ? 0 : 1;
    }

    GateTally BallGate        { "R1", "Ball: \\|foot − C(τ)\\| − r and \\|P(𝑢,𝑣) − C(𝑢)\\| − r, (P − C)·T, relative to r", "≤ 1e-9" };
    GateTally ContactGate     { "R2", "Contact: distance of each rail point to its support", "≤ 1e-9" };
    GateTally TangencyGate    { "R3", "G1: angle between carrier and support normals at the rails [rad]", "≤ 1e-8" };
    GateTally ProjectionGate  { "R4", "Projection round trip \\|P(proj(P(𝑢,𝑣))) − P(𝑢,𝑣)\\|", "≤ 1e-9" };
    GateTally NormalGate      { "R5", "Chart normal: ∠(∂𝑢 × ∂𝑣, radial) [rad]", "≤ 1e-8" };
    GateTally ChartGate       { "R5′", "Chart: ∂𝑢 vs a Richardson difference at 10⁻⁴·range (relative)", "≤ 1e-6" };
    GateTally ReproductionGate{ "R6", "Analytic reproduction: line spine ≡ cylinder, circle spine ≡ torus (distance)", "≤ 1e-12" };
    GateTally SectionGate     { "R7", "Carrier ∩ normal plane: a closed branch on the section circle (distance)", "≤ 1e-8" };
    GateTally BodyGate        { "R8", "Bodies: audit sound, \\|V − V_polygon\\| ≤ peak deviation × area, fillet removes material", "pass" };
    GateTally TransformGate   { "R9", "Transforms there and back: audit sound, V·s³ (relative), vertices home", "≤ 1e-9" };
    GateTally LawGate         { "R10", "Ball law on a constant wedge (cylinder rim, θ = 90°): spine ≡ circle (R − d, H − d), reversal C(−τ), chord ≡ cone, chord projection round trip (distance)", "≤ 1e-11" };
    GateTally LawRateGate     { "R10′", "Ball law rates: C′ vs the circle's (relative), chord normal ∥ cone normal [rad]", "≤ 1e-8" };
    GateTally CanalGate       { "R11", "Canal (r = r₀ + k·𝑢) on a line spine ≡ cone of sin β = k: chart, projection round trip, signed distance, offset (distance)", "≤ 1e-11" };
    GateTally CanalRateGate   { "R11′", "Canal rates: normal ∥ cone normal [rad], ∂𝑢 vs a sixth-order difference (relative)", "≤ 1e-9" };

    // ---- Carrier probes on one rolling-ball sheet -------------------------------------------------------------------------

    struct SheetProbe
    {
        double Ball = 0.0, Contact = 0.0, Tangency = 0.0, Projection = 0.0, Normal = 0.0, Chart = 0.0;
    };

    [[nodiscard]] Vector3 SupportNormal(const SurfaceGeometry& Support, Vector3 Position)
    {
        return EvaluateSurfaceNormal(Support, ProjectOntoSurface(Support, Position, { 0.0, 0.0 }));
    }

    [[nodiscard]] double AngleBetween(Vector3 First, Vector3 Second)
    {
        return std::atan2(Length(Cross(First, Second)), Dot(First, Second));
    }

    [[nodiscard]] SheetProbe ProbeSheet(const ModelPartition& Partition, const EdgeBlendSheet& Sheet, std::mt19937_64& Generator)
    {
        SheetProbe Probe;
        const CarrierRange        Range = MeasureCarrierRange(Sheet.Spine);
        const double              Low   = std::isfinite(Range.Low) ? Range.Low : -1.0, High = std::isfinite(Range.High) ? Range.High : 1.0;
        std::uniform_real_distribution<double> Along(Low, High), Around(0.0, FullTurn);
        const SurfaceGeometry* Supports[2] = { &Partition.Surfaces.Resolve(Partition.Faces.Resolve(Sheet.Faces[0]).Surface),
                                               &Partition.Surfaces.Resolve(Partition.Faces.Resolve(Sheet.Faces[1]).Surface) };
        for (int Sample = 0; Sample < 160; ++Sample)
        {
            const double      τ      = Along(Generator);
            const CurveSample Centre = EvaluateCurve(Sheet.Spine, τ);
            const Vector3     Tangent = Normalize(Centre.Derivative);
            for (int Side = 0; Side < 2; ++Side)
            {
                const Vector3 Foot = EvaluateCurve(Sheet.Contacts[Side], τ).Position;
                Probe.Ball    = std::max(Probe.Ball, std::fabs(Length(Foot - Centre.Position) - Sheet.Radius) / Sheet.Radius);
                Probe.Contact = std::max(Probe.Contact, std::fabs(MeasureSurfaceDistance(*Supports[Side], Foot).Distance));
                const Vector2 OnPipe = ProjectOntoSurface(Sheet.Surface, Foot, { τ, Sheet.CrossCentre });
                const double  Angle  = AngleBetween(EvaluateSurfaceNormal(Sheet.Surface, OnPipe), SupportNormal(*Supports[Side], Foot));
                Probe.Tangency = std::max(Probe.Tangency, std::min(Angle, std::numbers::pi - Angle));
            }
            // Random chart point: on the sphere, in the normal plane, and back through the projection.
            const Vector2       Parameter{ τ, Around(Generator) };
            const SurfaceSample Point = EvaluateSurface(Sheet.Surface, Parameter);
            const Vector3       Radial = Point.Position - Centre.Position;
            Probe.Ball = std::max({ Probe.Ball, std::fabs(Length(Radial) - Sheet.Radius) / Sheet.Radius, std::fabs(Dot(Radial, Tangent)) / Sheet.Radius });
            const Vector2 Back = ProjectOntoSurface(Sheet.Surface, Point.Position, Parameter);
            Probe.Projection = std::max(Probe.Projection, Length(EvaluateSurface(Sheet.Surface, Back).Position - Point.Position));
            // ∂𝑢 × ∂𝑣 ∥ radial; ∂𝑢 against a Richardson-extrapolated difference with a 10² larger step.
            const double Angle = AngleBetween(Cross(Point.DerivativeU, Point.DerivativeV), Radial);
            const double Step  = 1.0e-4 * (High - Low);
            const auto   At    = [&](double Offset) { return EvaluateSurface(Sheet.Surface, { τ + Offset, Parameter.Y }).Position; };
            const Vector3 Coarse = (At(Step) - At(-Step)) * (1.0 / (2.0 * Step));
            const Vector3 Fine   = (At(0.5 * Step) - At(-0.5 * Step)) * (1.0 / Step);
            const Vector3 Richardson = Fine + (Fine - Coarse) * (1.0 / 3.0);
            Probe.Normal = std::max(Probe.Normal, std::min(Angle, std::numbers::pi - Angle));
            Probe.Chart  = std::max(Probe.Chart, Length(Point.DerivativeU - Richardson) / Length(Richardson));
        }
        return Probe;
    }

    // Carrier ∩ the normal plane at C(𝑢₀): some closed branch must lie on the section circle.
    [[nodiscard]] double ProbeSection(const EdgeBlendSheet& Sheet)
    {
        const CarrierRange Range  = MeasureCarrierRange(Sheet.Spine);
        const double       Middle = 0.5 * (Range.Low + Range.High);
        const CurveSample  Centre = EvaluateCurve(Sheet.Spine, Middle);
        const Vector3      Tangent = Normalize(Centre.Derivative);
        const PlaneSurface Plane{ ConstructFrame(Centre.Position, Tangent, std::fabs(Tangent.X) < 0.9 ? Vector3{ 1, 0, 0 } : Vector3{ 0, 1, 0 }) };
        const double       Reach  = 4.0 * Sheet.Radius;                                      // [len] - Plane box half-width
        const SurfaceIntersectionOutcome Outcome = IntersectSurfaces(Sheet.Surface, { { Range.Low, 0.0 }, { Range.High, FullTurn } }, Plane, { { -Reach, -Reach }, { Reach, Reach } });
        double Best = 1.0e300;
        for (const IntersectionBranch& Branch : Outcome.Branches)
        {
            if (!Branch.ClosedCondition || Branch.Chart.size() < 8) continue;
            double Worst = 0.0;
            for (const IntersectionChartPoint& Point : Branch.Chart)
            {
                const Vector3 Radial = Point.Position - Centre.Position;
                Worst = std::max({ Worst, std::fabs(Length(Radial) - Sheet.Radius), std::fabs(Dot(Radial, Tangent)) });
            }
            Best = std::min(Best, Worst);
        }
        return Best;
    }

    // 📝 Ball law on the top rim of a solid cylinder (R 1, H 2, setback d 0.3, tilted frame): the wedge is 90° everywhere,
    //    so r = d and the law spine is the circle of radius R − d at height H − d (same angle τ as the rim); the chord
    //    between the feet is the cone through the rails (R − d, H) and (R, H − d), slope −1. Positions → LawGate,
    //    rates and normals → LawRateGate.
    struct LawProbe
    {
        double Position = 0.0;                                                               // [len] - Worst distance
        double Rate     = 0.0;                                                               // [-]   - Worst relative / angular
    };

    [[nodiscard]] LawProbe ProbeBallLaw(std::mt19937_64& Generator)
    {
        std::uniform_real_distribution<double> Fraction(0.0, 1.0);
        const double Radius = 1.0, Height = 2.0, Setback = 0.3;
        const OrthonormalFrame Frame = ConstructFrame({ 0.3, -0.2, 0.7 }, Normalize(Vector3{ 1, 2, 3 }), { 1, 0, 0 });
        OrthonormalFrame Top = Frame;
        Top.Origin = Frame.Origin + Frame.ZAxis * Height;
        const PlaneSurface    Lid{ Top };
        const CylinderSurface Wall{ Frame, Radius };
        BallLaw Law;
        Law.Edge      = CircularArc{ Top, Radius };
        Law.Size      = Setback;
        Law.Signs[0]  = Law.Signs[1] = -1.0;                                                 // Convex: centre inside both
        const std::optional<CurveGeometry> Spine = BuildBallLawCurve(Lid, Wall, Law, 0.0, FullTurn, true);
        LawProbe Probe;
        if (!Spine) { Probe.Position = Probe.Rate = INFINITY; return Probe; }
        const TracedIntersection& Trace    = *std::get<IntersectionCurve>(*Spine).Trace;
        const CurveGeometry       Reversed = ReverseIntersectionCurve(Trace);
        const SurfaceGeometry     Chord    = ConstructRollingBallSurface(*Spine, Setback, Lid, 1.0, RollingBallSection::Chord, Wall);
        const ConeSurface         Cone{ Top, Radius - Setback, -1.0 };
        for (int Sample = 0; Sample < 400; ++Sample)
        {
            const double  τ        = FullTurn * Fraction(Generator), 𝑣 = Fraction(Generator);
            const Vector3 Radial   = Frame.XAxis * std::cos(τ) + Frame.YAxis * std::sin(τ);
            const Vector3 Expected = Top.Origin - Frame.ZAxis * Setback + Radial * (Radius - Setback);
            const Vector3 Speed    = (Frame.YAxis * std::cos(τ) - Frame.XAxis * std::sin(τ)) * (Radius - Setback);
            const CurveSample Centre = EvaluateCurve(*Spine, τ);
            const CurveSample Back   = EvaluateCurve(Reversed, -τ);
            Probe.Position = std::max({ Probe.Position, Distance(Centre.Position, Expected), Distance(Back.Position, Expected), std::fabs(MeasureBallLawRadius(Trace, τ) - Setback) });
            Probe.Rate     = std::max({ Probe.Rate, Length(Centre.Derivative - Speed) / Length(Speed), Length(Back.Derivative + Speed) / Length(Speed) });
            const Vector3 Point   = EvaluateSurface(Chord, { τ, 𝑣 }).Position;
            const Vector2 Back2   = ProjectOntoSurface(Chord, Point, { τ + 0.3, 0.5 });
            const SurfaceDistance OnCone = MeasureSurfaceDistance(Cone, Point);
            const Vector3 Normal  = EvaluateSurfaceNormal(Chord, { τ, 𝑣 });
            Probe.Position = std::max({ Probe.Position, std::fabs(OnCone.Distance), Distance(EvaluateSurface(Chord, Back2).Position, Point) });
            Probe.Rate     = std::max(Probe.Rate, std::atan2(Length(Cross(Normal, OnCone.Normal)), std::fabs(Dot(Normal, OnCone.Normal))));
        }
        return Probe;
    }

    // 📝 Canal on a line spine C(𝑢) = O + 𝑢·Z (unit speed), r(𝑢) = r₀ + k·𝑢: the envelope of the balls is the cone of
    //    axis Z with half-angle β, sin β = k (each characteristic circle sits at z = 𝑢 − r·k, radius r·√(1 − k²)), i.e.
    //    radius r₀/√(1 − k²) at z = 0 and slope tan β. The guide is a plane parallel to the axis (E₁ constant). Points
    //    off the carrier by δ along the cone normal test the canal's signed distance; the canal of r + δ is the offset.
    struct CanalProbe
    {
        double Position = 0.0;                                                               // [len] - Worst distance
        double Rate     = 0.0;                                                               // [-]   - Worst angle / relative
    };

    [[nodiscard]] CanalProbe ProbeCanal(std::mt19937_64& Generator)
    {
        std::uniform_real_distribution<double> Fraction(0.0, 1.0);
        const double Start = 0.5, Rate = 0.2;                                                // [len], [-] - r₀, k
        const OrthonormalFrame Frame = ConstructFrame({ 0.3, -0.2, 0.7 }, Normalize(Vector3{ 1, 2, 3 }), { 1, 0, 0 });
        const PlaneSurface     Guide{ ConstructFrame(Frame.Origin + Frame.XAxis * 3.0, Frame.XAxis, Frame.YAxis) };
        const SurfaceGeometry  Canal = ConstructRollingBallSurface(StraightLine{ Frame.Origin, Frame.ZAxis }, Start, Guide, 1.0, RollingBallSection::Circle, PlaneSurface{}, Rate);
        const double           Lean  = std::sqrt(1.0 - Rate * Rate);                         // [-] - cos β
        const ConeSurface      Cone{ Frame, Start / Lean, Rate / Lean };
        const double           Shift = 0.04;                                                 // [len] - Offset δ
        const std::optional<SurfaceGeometry> Offset = OffsetSurface(Canal, Shift);
        CanalProbe Probe;
        if (!Offset) { Probe.Position = Probe.Rate = INFINITY; return Probe; }
        for (int Sample = 0; Sample < 400; ++Sample)
        {
            const Vector2 At{ -1.0 + 2.0 * Fraction(Generator), FullTurn * Fraction(Generator) };
            const SurfaceSample   Point  = EvaluateSurface(Canal, At);
            const SurfaceDistance OnCone = MeasureSurfaceDistance(Cone, Point.Position);
            const Vector3         Normal = EvaluateSurfaceNormal(Canal, At);
            const double          Align  = Dot(Normal, OnCone.Normal) > 0.0 ? 1.0 : -1.0;     // [-] - Natural normals agree?
            const double          Off    = Shift * (2.0 * Fraction(Generator) - 1.0);         // [len] - Signed probe offset
            const Vector3         Away   = Point.Position + Normal * Off;
            const SurfaceDistance CanalAway = MeasureSurfaceDistance(Canal, Away);
            const Vector2         Back   = ProjectOntoSurface(Canal, Point.Position, { At.X + 0.2, 0.5 });
            const Vector3         Lifted = EvaluateSurface(*Offset, At).Position;            // On the canal of r + δ
            Probe.Position = std::max({ Probe.Position, std::fabs(OnCone.Distance), Distance(EvaluateSurface(Canal, Back).Position, Point.Position),
                                        std::fabs(CanalAway.Distance - Off), std::fabs(MeasureSurfaceDistance(Cone, Away).Distance - Align * Off),
                                        std::fabs(MeasureSurfaceDistance(Cone, Lifted).Distance - Align * Shift) });
            constexpr double Step = 2.0e-3;
            const auto Pair = [&](int k) { return EvaluateSurface(Canal, { At.X + k * Step, At.Y }).Position - EvaluateSurface(Canal, { At.X - k * Step, At.Y }).Position; };
            const Vector3 Along = (Pair(1) * 45.0 - Pair(2) * 9.0 + Pair(3)) * (1.0 / (60.0 * Step));
            Probe.Rate = std::max({ Probe.Rate, std::atan2(Length(Cross(Normal, OnCone.Normal)), std::fabs(Dot(Normal, OnCone.Normal))), Length(Along - Point.DerivativeU) / Length(Along),
                                    std::atan2(Length(Cross(Normal, Normalize(Cross(Point.DerivativeU, Point.DerivativeV)))), Dot(Normal, Normalize(Cross(Point.DerivativeU, Point.DerivativeV)))) });
        }
        return Probe;
    }

    // Line spine on a plane guide is a cylinder; circle spine on a plane guide is a torus.
    [[nodiscard]] double ProbeReproduction(std::mt19937_64& Generator)
    {
        std::uniform_real_distribution<double> Fraction(0.0, 1.0);
        double Worst = 0.0;
        const OrthonormalFrame Frame = ConstructFrame({ 0.3, -0.2, 0.7 }, Normalize(Vector3{ 1, 2, 3 }), { 1, 0, 0 });
        const PlaneSurface     Guide{ ConstructFrame(Frame.Origin + Frame.XAxis * 0.4, Frame.XAxis, Frame.YAxis) };   // Plane 0.4 from the axis
        // Cylinder: spine = the axis line, radius 0.4 (the ball touches the guide plane).
        const SurfaceGeometry LinePipe = ConstructRollingBallSurface(StraightLine{ Frame.Origin, Frame.ZAxis }, 0.4, Guide);
        const CylinderSurface Cylinder{ Frame, 0.4 };
        // Torus: spine = circle of radius 2 in the frame's XY plane, tube 0.5, guide plane z = −0.5 (tangent to the tube).
        const SurfaceGeometry CirclePipe = ConstructRollingBallSurface(CircularArc{ Frame, 2.0 }, 0.5, PlaneSurface{ ConstructFrame(Frame.Origin - Frame.ZAxis * 0.5, Frame.ZAxis, Frame.XAxis) });
        const TorusSurface    Torus{ Frame, 2.0, 0.5 };
        for (int Sample = 0; Sample < 400; ++Sample)
        {
            const Vector2 OnLine{ -3.0 + 6.0 * Fraction(Generator), FullTurn * Fraction(Generator) };
            const Vector2 OnCircle{ FullTurn * Fraction(Generator), FullTurn * Fraction(Generator) };
            Worst = std::max(Worst, std::fabs(MeasureSurfaceDistance(Cylinder, EvaluateSurface(LinePipe, OnLine).Position).Distance));
            Worst = std::max(Worst, std::fabs(MeasureSurfaceDistance(Torus, EvaluateSurface(CirclePipe, OnCircle).Position).Distance));
        }
        return Worst;
    }

    // ---- Bodies ---------------------------------------------------------------------------------------------------------

    struct BoreSpecimen
    {
        const char* Name       = "";                                                         // [-]   - Row label
        Vector3     BoreOrigin = {};                                                         // [len] - Bore cylinder base
        double      BoreRadius = 0.2;                                                        // [len] - Bore radius
        double      Radius     = 0.03;                                                       // [len] - Fillet radius
        double      CrossAt    = 0.0;                                                        // [len] - x of an equal through-bore along y (0: none)
    };

    struct BodyRow
    {
        std::string Name        = {};
        std::string Outcome     = {};                                                        // [-]    - "blended" or the refusal
        int         Edges       = 0, Sheets = 0, Pipes = 0, Faces = 0, Genus = -1;
        double      Volume      = 0.0, Removed = 0.0, PolygonGap = 0.0, PolygonBound = 0.0;  // [len³]
        double      Milliseconds = 0.0;
        bool        PassedCondition = false;
    };

    struct TransformRow
    {
        std::string Name    = {};
        std::string Outcome = {};
        double      Volume  = 0.0, Home = 0.0;                                               // [-] - Relative volume error, worst vertex gap
        bool        PassedCondition = false;
    };

    [[nodiscard]] double VertexHomeGap(const ModelPartition& Partition, BodyToken Original, BodyToken Returned)
    {
        const std::vector<VertexToken> Before = Partition.CollectInventory(Original).Vertices;
        const std::vector<VertexToken> After  = Partition.CollectInventory(Returned).Vertices;
        if (Before.size() != After.size()) return 1.0e300;
        double Worst = 0.0;
        for (VertexToken Vertex : After)
        {
            double Nearest = 1.0e300;
            for (VertexToken Home : Before) Nearest = std::min(Nearest, Distance(Partition.Vertices.Resolve(Vertex).Position, Partition.Vertices.Resolve(Home).Position));
            Worst = std::max(Worst, Nearest);
        }
        return Worst;
    }

    // Applies Maps in order to Body; returns the last body (empty token + refusal on failure).
    [[nodiscard]] TransformOutcome ApplyMaps(ModelPartition& Partition, BodyToken Body, const std::vector<AffineMap>& Maps)
    {
        TransformOutcome Outcome{ Body, {} };
        for (const AffineMap& Map : Maps)
        {
            Outcome = TransformBody(Partition, Outcome.Body, Map);
            if (!Outcome.Refusal.empty()) return Outcome;
        }
        return Outcome;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    const auto       Start = Clock::now();
    std::mt19937_64  Generator(20261021);
    ModelPartition   Partition;
    std::vector<BodyRow>      BodyRows;
    std::vector<TransformRow> TransformRows;
    BodyToken        TransformSource = {};

    // ---- R6 analytic reproduction.
    const double Reproduction = ProbeReproduction(Generator);
    Tally(ReproductionGate, Reproduction, Reproduction <= 1.0e-12);

    // ---- R10 ball law and chord carrier against the constant-wedge closed form.
    std::mt19937_64 LawGenerator(20261022);                                                  // Own stream: R1–R9 samples unchanged
    const LawProbe  Law = ProbeBallLaw(LawGenerator);
    Tally(LawGate, Law.Position, Law.Position <= 1.0e-11);
    Tally(LawRateGate, Law.Rate, Law.Rate <= 1.0e-8);

    // ---- R11 canal carrier against the cone of its balls.
    std::mt19937_64  CanalGenerator(20261021);                                               // Own stream: earlier samples unchanged
    const CanalProbe Canal = ProbeCanal(CanalGenerator);
    Tally(CanalGate, Canal.Position, Canal.Position <= 1.0e-11);
    Tally(CanalRateGate, Canal.Rate, Canal.Rate <= 1.0e-9);

    // ---- Bodies: disc r 3 × 1 minus one radial bore, fillet the traced mouth.
    const BoreSpecimen Specimens[] = {
        { "Radial bore through the axis (crosses both seams), r 0.03", { 1.0, 0.0, 0.5 }, 0.2, 0.03 },
        { "Radial bore through the axis, r 0.06",                       { 1.0, 0.0, 0.5 }, 0.2, 0.06 },
        { "Offset bore (y 0.8, asymmetric mouth), r 0.04",              { 1.0, 0.8, 0.5 }, 0.25, 0.04 },
        { "Equal crossing bore at x 2 (Steinmetz: 4 half-ellipses, 2 pinch vertices), r 0.03", { 1.0, 0.0, 0.5 }, 0.2, 0.03, 2.0 },
    };
    for (const BoreSpecimen& Specimen : Specimens)
    {
        const auto Moment = Clock::now();
        BodyRow    Row;
        Row.Name = Specimen.Name;
        const auto Disc = ConstructCylinder(Partition, ConstructFrame({ 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }), 3.0, 1.0);
        const auto Bore = ConstructCylinder(Partition, ConstructFrame(Specimen.BoreOrigin, { 1, 0, 0 }, { 0, 0, 1 }), Specimen.BoreRadius, 2.5);
        if (!Disc.SucceededCondition() || !Bore.SucceededCondition()) { Row.Outcome = "primitive refused"; BodyRows.push_back(Row); continue; }
        BooleanOutcome Bored = ComputeBoolean(Partition, Disc.Value, Bore.Value, BooleanOperation::Difference);
        if (Bored.Refusal.empty() && Specimen.CrossAt > 0.0)
        {
            // 📝 Equal radii: the crossing edges are exact half-ellipses meeting at two singular points where the bores are
            //    tangent; only they are filleted (the traced mouths are covered by the rows above).
            const auto Cross = ConstructCylinder(Partition, ConstructFrame({ Specimen.CrossAt, -2.6, Specimen.BoreOrigin.Z }, { 0, 1, 0 }, { 0, 0, 1 }), Specimen.BoreRadius, 5.2);
            Bored = ComputeBoolean(Partition, Bored.Body, Cross.Value, BooleanOperation::Difference);
        }
        if (!Bored.Refusal.empty()) { Row.Outcome = "boolean: " + Bored.Refusal; BodyRows.push_back(Row); continue; }
        std::vector<EdgeToken> Mouth;
        for (EdgeToken Edge : Partition.CollectInventory(Bored.Body).Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (!Profile.Curve.PresentCondition()) continue;
            const CurveGeometry& Curve = Partition.Curves.Resolve(Profile.Curve);
            if (Specimen.CrossAt > 0.0 ? std::holds_alternative<EllipticalArc>(Curve) : std::holds_alternative<IntersectionCurve>(Curve)) Mouth.push_back(Edge);
        }
        Row.Edges = static_cast<int>(Mouth.size());
        BlendSettings Settings;
        Settings.Profile = BlendProfile::Fillet;
        Settings.Size    = Specimen.Radius;
        const BlendOutcome Blended = BlendEdges(Partition, Bored.Body, Mouth, Settings);
        Row.Outcome = Blended.Refusal.empty() ? "blended" : Blended.Refusal;
        const int Pinches = Blended.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Pinch)];
        if (Blended.Refusal.empty() && Specimen.CrossAt > 0.0) Row.Outcome += " · " + std::to_string(Pinches) + " pinch vertices";
        if (Blended.Refusal.empty())
        {
            Row.Sheets = static_cast<int>(Blended.Sheets.size());
            SheetProbe Worst;
            for (const EdgeBlendSheet& Sheet : Blended.Sheets)
            {
                if (!std::holds_alternative<RollingBallSurface>(Sheet.Surface)) continue;
                ++Row.Pipes;
                const SheetProbe Probe = ProbeSheet(Partition, Sheet, Generator);
                Tally(BallGate, Probe.Ball, Probe.Ball <= 1.0e-9);
                Tally(ContactGate, Probe.Contact, Probe.Contact <= 1.0e-9);
                Tally(TangencyGate, Probe.Tangency, Probe.Tangency <= 1.0e-8);
                Tally(ProjectionGate, Probe.Projection, Probe.Projection <= 1.0e-9);
                Tally(NormalGate, Probe.Normal, Probe.Normal <= 1.0e-8);
                Tally(ChartGate, Probe.Chart, Probe.Chart <= 1.0e-6);
                const double Section = ProbeSection(Sheet);
                Tally(SectionGate, Section, Section <= 1.0e-8);
            }
            const AuditReport Audit = AuditBody(Partition, Blended.Body);
            Row.Faces  = static_cast<int>(Partition.CollectInventory(Blended.Body).Faces.size());
            Row.Genus  = Audit.Genus;
            Row.Volume = IntegrateBodyVolume(Partition, Blended.Body);
            Row.Removed = IntegrateBodyVolume(Partition, Bored.Body) - Row.Volume;
            TessellationSettings Fine;
            Fine.SagRatio = 2.0e-5;
            const TessellationOutcome Mesh = TessellateBody(Partition, Blended.Body, Fine);
            const TessellationAuditReport Polygon = AuditTessellation(Partition, Blended.Body, Mesh);
            Row.PolygonGap   = std::fabs(Polygon.ExactVolume - Polygon.ComplexVolume);
            Row.PolygonBound = Polygon.PeakDeviation * Polygon.ComplexArea;
            Row.PassedCondition = Row.Pipes > 0 && (Specimen.CrossAt <= 0.0 || Pinches == 2) && Audit.SoundCondition() && Mesh.Refusal.empty() && Polygon.UnpairedSides == 0 && Row.PolygonGap <= Row.PolygonBound && Row.Removed > 0.0;
            if (!TransformSource.PresentCondition() && Row.PassedCondition) TransformSource = Blended.Body;
        }
        Tally(BodyGate, Row.PassedCondition ? 0.0 : 1.0, Row.PassedCondition);
        Row.Milliseconds = Elapsed(Moment);
        BodyRows.push_back(Row);
    }

    // ---- R9 transforms there and back on the first filleted body.
    if (TransformSource.PresentCondition())
    {
        const double Volume = IntegrateBodyVolume(Partition, TransformSource);
        struct Journey { const char* Name; std::vector<AffineMap> There, Back; double Factor; };
        const Vector3 AxisPoint{ 0.4, -1.1, 0.3 }, AxisDirection = Normalize(Vector3{ 1, -2, 2 });
        const Journey Journeys[] = {
            { "Rotation 37° about a skew axis + translation", { RotationMap(AxisPoint, AxisDirection, 0.6457718232379019), TranslationMap({ 5, -2, 7 }) },
                                                              { TranslationMap({ -5, 2, -7 }), RotationMap(AxisPoint, AxisDirection, -0.6457718232379019) }, 1.0 },
            { "Reflection in a skew plane",                   { ReflectionMap({ 0.2, 0.1, 0.5 }, Normalize(Vector3{ 1, 1, 0.5 })) },
                                                              { ReflectionMap({ 0.2, 0.1, 0.5 }, Normalize(Vector3{ 1, 1, 0.5 })) }, 1.0 },
            { "Uniform scale × 2.5 about (1, 1, 1)",          { ScalingMap({ 1, 1, 1 }, { 2.5, 2.5, 2.5 }) }, { ScalingMap({ 1, 1, 1 }, { 0.4, 0.4, 0.4 }) }, 2.5 },
        };
        for (const Journey& Trip : Journeys)
        {
            TransformRow Row;
            Row.Name = Trip.Name;
            const TransformOutcome There = ApplyMaps(Partition, TransformSource, Trip.There);
            const TransformOutcome Back  = There.Refusal.empty() ? ApplyMaps(Partition, There.Body, Trip.Back) : There;
            Row.Outcome = Back.Refusal.empty() ? "round trip" : Back.Refusal;
            if (Back.Refusal.empty())
            {
                const double Expected = Volume * Trip.Factor * Trip.Factor * Trip.Factor;
                Row.Volume = std::fabs(std::fabs(IntegrateBodyVolume(Partition, There.Body)) - Expected) / Expected;
                Row.Home   = VertexHomeGap(Partition, TransformSource, Back.Body);
                Row.PassedCondition = AuditBody(Partition, There.Body).SoundCondition() && AuditBody(Partition, Back.Body).SoundCondition() && Row.Volume <= 1.0e-9 && Row.Home <= 1.0e-9;
            }
            Tally(TransformGate, std::max(Row.Volume, Row.Home), Row.PassedCondition);
            TransformRows.push_back(Row);
        }
        // 📝 Ball-law chamfer (chord carrier on the tilted bore's traced rim): the law's edge and setback map with the body.
        {
            TransformRow Row;
            Row.Name = "Curved-edge chamfer (tilted bore, d 0.1): rotation + scale × 2 + skew reflection";
            const BlendSpecimen Specimen = ConstructBlendSpecimen(Partition, "TiltedBoreChamfer");
            const BlendOutcome  Chamfered = BlendEdges(Partition, Specimen.Body, Specimen.Edges, Specimen.Settings);
            Row.Outcome = Chamfered.Refusal;
            if (Chamfered.Refusal.empty())
            {
                const AffineMap Turn = RotationMap(AxisPoint, AxisDirection, 0.6457718232379019), Grow = ScalingMap({ 1, 1, 1 }, { 2.0, 2.0, 2.0 });
                const AffineMap Mirror = ReflectionMap({ 0.2, 0.1, 0.5 }, Normalize(Vector3{ 1, 1, 0.5 }));
                const TransformOutcome There = ApplyMaps(Partition, Chamfered.Body, { Turn, Grow, Mirror });
                const TransformOutcome Back  = There.Refusal.empty() ? ApplyMaps(Partition, There.Body, { Mirror, ScalingMap({ 1, 1, 1 }, { 0.5, 0.5, 0.5 }), RotationMap(AxisPoint, AxisDirection, -0.6457718232379019) }) : There;
                Row.Outcome = Back.Refusal.empty() ? "round trip" : Back.Refusal;
                if (Back.Refusal.empty())
                {
                    const double Expected = IntegrateBodyVolume(Partition, Chamfered.Body) * 8.0;
                    Row.Volume = std::fabs(std::fabs(IntegrateBodyVolume(Partition, There.Body)) - Expected) / Expected;
                    Row.Home   = VertexHomeGap(Partition, Chamfered.Body, Back.Body);
                    Row.PassedCondition = AuditBody(Partition, There.Body).SoundCondition() && AuditBody(Partition, Back.Body).SoundCondition() && Row.Volume <= 1.0e-9 && Row.Home <= 1.0e-9;
                }
            }
            Tally(TransformGate, std::max(Row.Volume, Row.Home), Row.PassedCondition);
            TransformRows.push_back(Row);
        }
        // A non-similar map has no exact rolling-ball image: it must refuse, never approximate.
        TransformRow Row;
        Row.Name = "Non-uniform scale (1, 1, 2) — must refuse";
        const TransformOutcome Stretched = TransformBody(Partition, TransformSource, ScalingMap({ 0, 0, 0 }, { 1, 1, 2 }));
        Row.Outcome = Stretched.Refusal.empty() ? "built (wrong: no exact image)" : Stretched.Refusal;
        Row.PassedCondition = !Stretched.Refusal.empty();
        Tally(TransformGate, 0.0, Row.PassedCondition);
        TransformRows.push_back(Row);
    }

    // ---- Report.
    const GateTally* Gates[] = { &BallGate, &ContactGate, &TangencyGate, &ProjectionGate, &NormalGate, &ChartGate, &ReproductionGate, &SectionGate, &BodyGate, &TransformGate, &LawGate, &LawRateGate, &CanalGate, &CanalRateGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Rolling-ball carrier validation (procedural pipe on traced spines)\n\n");
    std::printf("**%s** · %zu bodies · %zu transforms · %.0f ms\n\n", PassedCondition ? "🟢 PASS" : "🔴 FAIL", BodyRows.size(), TransformRows.size(), Elapsed(Start));
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Bodies (disc r 3 × 1 − one bore, fillet the traced mouth)\n\n| | Specimen | Edges | Sheets | Pipes | Faces | Genus | V | Removed | \\|V − V_polygon\\| | Bound | ms | Outcome |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const BodyRow& Row : BodyRows)
    {
        std::printf("| %s | %s | %d | %d | %d | %d | %d | %.12f | %.3e | %.1e | %.1e | %.0f | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Edges, Row.Sheets, Row.Pipes, Row.Faces, Row.Genus,
                    Row.Volume, Row.Removed, Row.PolygonGap, Row.PolygonBound, Row.Milliseconds, Row.Outcome.c_str());
    }
    std::printf("\n## Transforms\n\n| | Journey | \\|V − V*·s³\\| / V* | Vertices home | Outcome |\n|---|---|---|---|---|\n");
    for (const TransformRow& Row : TransformRows)
    {
        std::printf("| %s | %s | %.1e | %.1e | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Volume, Row.Home, Row.Outcome.c_str());
    }
    return PassedCondition ? 0 : 1;
}
