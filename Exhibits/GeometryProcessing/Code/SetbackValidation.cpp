/*====================================================================================================================================
                                                     SETBACKVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Setback vertex blends: exact rims, G1 to the neighbours and across spokes, sound bodies, determinism and refusals

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Geometry/NurbsCarrier.h"
#include "Engine/GeometryProcessing/Intersection/ImplicitSurface.h"
#include "Engine/GeometryProcessing/Local/FaceChange.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <numbers>
#include <set>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    // 📝 One gate: worst measured value against its limit over every check.
    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - S1 …
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

    constexpr double RimLimit  = 1.0e-9;                                                     // [-]   - Edge-on-carrier, relative to the model size
    constexpr double G1Limit   = 1.0e-2;                                                     // [rad] - Quad ↔ neighbouring blend / face
    constexpr double SeamLimit = 5.0e-2;                                                     // [rad] - Quad ↔ quad across a spoke

    GateTally RimGate         { "S1", "Exact rims: every edge of the result on both face carriers (16 samples, relative to the model size)", "≤ 1e-9" };
    GateTally TangencyGate    { "S2", "G1 setback quad ↔ neighbouring blend / face: ∠ of outward normals across the shared edge [rad]", "≤ 1e-2" };
    GateTally SeamGate        { "S3", "G1 setback quad ↔ quad across a spoke: ∠ of outward normals [rad]", "≤ 5e-2" };
    GateTally BodyGate        { "S4", "Bodies: audit sound, genus kept, watertight polygon complex, \\|V − V_polygon\\| ≤ peak deviation × area, expected vertex categories", "pass" };
    GateTally DeterminismGate { "S5", "Determinism: rebuilt twice → identical volume bits and polygon digest", "identical" };
    GateTally RefusalGate     { "S6", "Refusals: atomic, prefixed, source intact", "all refused" };

    // ---- Specimens ---------------------------------------------------------------------------------------------------------

    using EdgeTest = std::function<bool(Vector3, Vector3)>;

    struct SetbackSpecimen
    {
        const char*  Name      = "";                                                         // [-]   - Row label
        int          Shape     = 0;                                                          // [-]   - 0 = L block, 1 = square pyramid
        EdgeTest     Pick      = {};                                                         // [-]   - Edge selection by end points (empty = all)
        BlendProfile Profile   = BlendProfile::Fillet;                                       // [-]
        double       Size      = 0.1;                                                        // [len] - Radius
        int          Setbacks  = 0;                                                          // [-]   - Expected setback vertices
        int          Corners   = 0;                                                          // [-]   - Expected ball corners
        int          Caps      = 0;                                                          // [-]   - Expected caps
        bool         Refusal   = false;                                                      // [-]   - Must refuse
    };

    // 📝 L block: profile (x, z) counter-clockwise about +y, extruded 1.5 — a concave edge along y at (1, ·, 1) whose two ends
    //    are mixed vertices (two convex + one concave edge). Square pyramid: two wedges (slopes 1, apex height 1) intersected
    //    → base 2 × 2, apex (0, 0, 1) of degree 4, volume 4/3.
    [[nodiscard]] BodyToken ConstructShape(ModelPartition& Partition, int Shape, std::string& Refusal)
    {
        if (Shape == 0)
        {
            const auto Block = ConstructPrism(Partition, { { 0, 0, 1 }, { 1, 0, 1 }, { 1, 0, 2 }, { 2, 0, 2 }, { 2, 0, 0 }, { 0, 0, 0 } }, { 0, 1.5, 0 });
            if (Block.Refusal) Refusal = Block.Refusal;
            return Block.Value.Body;
        }
        const auto First  = ConstructPrism(Partition, { { 0, -1.5, 1 }, { 1, -1.5, 0 }, { -1, -1.5, 0 } }, { 0, 3, 0 });
        const auto Second = ConstructPrism(Partition, { { -1.5, -1.5, -0.5 }, { -1.5, 1.5, -0.5 }, { -1.5, 0, 1 } }, { 3, 0, 0 });
        if (First.Refusal || Second.Refusal) { Refusal = "wedge refused"; return {}; }
        const BooleanOutcome Pyramid = ComputeBoolean(Partition, First.Value.Body, Second.Value.Body, BooleanOperation::Intersection);
        Refusal = Pyramid.Refusal;
        return Pyramid.Body;
    }

    [[nodiscard]] std::vector<EdgeToken> SelectEdges(const ModelPartition& Partition, BodyToken Body, const EdgeTest& Pick)
    {
        std::vector<EdgeToken> Edges;
        for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (!Pick || Pick(Partition.Vertices.Resolve(Profile.Start).Position, Partition.Vertices.Resolve(Profile.End).Position)) Edges.push_back(Edge);
        }
        return Edges;
    }

    // ---- Independent measurements on the result -----------------------------------------------------------------------------

    [[nodiscard]] double AngleBetween(Vector3 First, Vector3 Second)
    {
        return std::atan2(Length(Cross(First, Second)), Dot(First, Second));
    }

    struct Continuity
    {
        double Rim     = 0.0;                                                                // [-]   - Worst edge-on-carrier distance / size
        double Tangent = 0.0;                                                                // [rad] - Worst quad ↔ neighbour angle
        double Seam    = 0.0;                                                                // [rad] - Worst quad ↔ quad angle
        int    Quads   = 0;                                                                  // [-]   - Freeform vertex faces found
    };

    // 📝 Every edge sampled at 16 interior points against both face carriers (S1). Across each edge of a freeform vertex
    //    face the outward normals of the two faces are compared at 16 points (ends excluded: the corners of a quad meet
    //    neighbours at an angle by construction); a neighbour that is itself such a quad is a spoke seam (S3).
    [[nodiscard]] Continuity MeasureContinuity(const ModelPartition& Partition, const BlendOutcome& Outcome, double Size)
    {
        Continuity Measure;
        std::set<uint32_t> Quads;
        for (const BlendFaceHistory& History : Outcome.FaceHistories)
        {
            const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Partition.Faces.Resolve(History.Result).Surface);
            if (History.Origin == BlendFaceOrigin::Vertex && std::holds_alternative<NurbsSurface>(Surface)) Quads.insert(History.Result.Slot);
        }
        Measure.Quads = static_cast<int>(Quads.size());
        const BodyInventory Inventory = Partition.CollectInventory(Outcome.Body);
        for (EdgeToken Edge : Inventory.Edges)
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
            const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
            const FinToken       Fin     = Profile.RadialFin;
            const FaceToken      Faces[2] = { Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face, Partition.Loops.Resolve(Partition.Fins.Resolve(Partition.RadialMate(Fin)).Loop).Face };
            const bool           QuadSides[2] = { Quads.count(Faces[0].Slot) != 0, Quads.count(Faces[1].Slot) != 0 };
            for (int Sample = 1; Sample <= 16; ++Sample)
            {
                const double  Fraction = 0.05 + 0.9 * (Sample - 1) / 15.0;
                const Vector3 Point    = EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Fraction).Position;
                for (FaceToken Face : Faces)
                {
                    const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface);
                    Measure.Rim = std::max(Measure.Rim, std::fabs(MeasureSurfaceDistance(Surface, Point).Distance) / Size);
                }
                if (!QuadSides[0] && !QuadSides[1]) continue;
                const double Angle = AngleBetween(EvaluateOutwardNormal(Partition, Faces[0], Point), EvaluateOutwardNormal(Partition, Faces[1], Point));
                if (QuadSides[0] && QuadSides[1]) Measure.Seam = std::max(Measure.Seam, Angle);
                else                              Measure.Tangent = std::max(Measure.Tangent, Angle);
            }
        }
        return Measure;
    }

    struct BodyRow
    {
        std::string Name;
        int         Edges = 0, Setbacks = 0, Corners = 0, Caps = 0, Quads = 0, Faces = 0, Genus = 0;
        double      Volume = 0.0, Source = 0.0, Rim = 0.0, Tangent = 0.0, Seam = 0.0, SolverRim = 0.0, PolygonGap = 0.0, PolygonBound = 0.0, Milliseconds = 0.0;
        bool        PassedCondition = false;
        std::string Outcome;
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    const auto Start = Clock::now();
    const auto Apex  = [](Vector3 First, Vector3 Second) { return std::fabs(First.Z - 1.0) < 1e-9 || std::fabs(Second.Z - 1.0) < 1e-9; };
    const auto Inner = [](Vector3 First, Vector3 Second)
    {
        const auto OnStep = [](Vector3 Point) { return std::fabs(Point.X - 1.0) < 1e-9 && std::fabs(Point.Z - 1.0) < 1e-9; };
        const auto Above  = [](Vector3 Point) { return std::fabs(Point.X - 1.0) < 1e-9 && Point.Z > 1.0 + 1e-9; };
        return (OnStep(First) && OnStep(Second)) || (OnStep(First) && Above(Second)) || (Above(First) && OnStep(Second));
    };
    const SetbackSpecimen Specimens[] = {
        { "L block, every edge r 0.1 (2 mixed vertices: 2 convex + 1 concave)",      0, {},    BlendProfile::Fillet,  0.1,  2, 10, 0 },
        { "L block, every edge r 0.05",                                               0, {},    BlendProfile::Fillet,  0.05, 2, 10, 0 },
        { "Square pyramid, every edge r 0.1 (apex of degree 4)",                      1, {},    BlendProfile::Fillet,  0.1,  1, 4,  0 },
        { "Square pyramid, the 4 apex edges r 0.1 (setback + 4 caps)",                1, Apex,  BlendProfile::Fillet,  0.1,  1, 0,  4 },
        { "L block, concave edge + its riser edge only (mixed beside a sharp edge) — must refuse", 0, Inner, BlendProfile::Fillet, 0.1, 0, 0, 0, true },
    };
    std::vector<BodyRow> Rows, Refusals;
    for (const SetbackSpecimen& Specimen : Specimens)
    {
        const auto     Moment = Clock::now();
        ModelPartition Partition;
        BodyRow        Row;
        Row.Name = Specimen.Name;
        std::string    Refusal;
        const BodyToken Body = ConstructShape(Partition, Specimen.Shape, Refusal);
        if (!Refusal.empty()) { Row.Outcome = "shape: " + Refusal; Tally(BodyGate, 1.0, false); Rows.push_back(Row); continue; }
        const std::vector<EdgeToken> Edges = SelectEdges(Partition, Body, Specimen.Pick);
        Row.Edges  = static_cast<int>(Edges.size());
        Row.Source = IntegrateBodyVolume(Partition, Body);
        BlendSettings Settings;
        Settings.Profile = Specimen.Profile;
        Settings.Size    = Specimen.Size;
        const BlendOutcome Blended = BlendEdges(Partition, Body, Edges, Settings);
        Row.Outcome = Blended.Refusal.empty() ? "blended" : Blended.Refusal;
        if (Specimen.Refusal)
        {
            const bool Refused = !Blended.Refusal.empty() && Blended.Refusal.rfind("Blend: ", 0) == 0 && IntegrateBodyVolume(Partition, Body) == Row.Source &&
                                 AuditBody(Partition, Body).SoundCondition();
            Row.PassedCondition = Refused;
            Tally(RefusalGate, Refused ? 0.0 : 1.0, Refused);
            Row.Milliseconds = Elapsed(Moment);
            Refusals.push_back(Row);
            continue;
        }
        if (Blended.Refusal.empty())
        {
            const BlendStatistics& Statistics = Blended.Statistics;
            Row.Setbacks  = Statistics.Vertices[static_cast<int>(VertexBlendCategory::Setback)];
            Row.Corners   = Statistics.Vertices[static_cast<int>(VertexBlendCategory::BallCorner)];
            Row.Caps      = Statistics.Vertices[static_cast<int>(VertexBlendCategory::Cap)];
            Row.SolverRim = Statistics.MaxRimGap;
            const Continuity Measure = MeasureContinuity(Partition, Blended, 2.0);
            Row.Quads   = Measure.Quads;
            Row.Rim     = Measure.Rim;
            Row.Tangent = Measure.Tangent;
            Row.Seam    = Measure.Seam;
            Tally(RimGate, Row.Rim, Row.Rim <= RimLimit);
            Tally(TangencyGate, Row.Tangent, Row.Tangent <= G1Limit);
            Tally(SeamGate, Row.Seam, Row.Seam <= SeamLimit);
            const AuditReport Audit = AuditBody(Partition, Blended.Body);
            Row.Faces  = static_cast<int>(Partition.CollectInventory(Blended.Body).Faces.size());
            Row.Genus  = Audit.Genus;
            Row.Volume = IntegrateBodyVolume(Partition, Blended.Body);
            TessellationSettings Fine;
            Fine.SagRatio = 2.0e-5;
            const TessellationOutcome     Mesh    = TessellateBody(Partition, Blended.Body, Fine);
            const TessellationAuditReport Polygon = AuditTessellation(Partition, Blended.Body, Mesh);
            Row.PolygonGap   = std::fabs(Polygon.ExactVolume - Polygon.ComplexVolume);
            Row.PolygonBound = Polygon.PeakDeviation * Polygon.ComplexArea;
            const int ExpectedQuads = Specimen.Shape == 0 ? 12 : 8;
            Row.PassedCondition = Audit.SoundCondition() && Audit.Genus == AuditBody(Partition, Body).Genus && Mesh.Refusal.empty() && Polygon.UnpairedSides == 0 && Polygon.Inverted == 0 &&
                                  Polygon.MissingEdgeSides == 0 && Row.PolygonGap <= Row.PolygonBound && Row.Setbacks == Specimen.Setbacks && Row.Corners == Specimen.Corners &&
                                  Row.Caps == Specimen.Caps && Row.Quads == ExpectedQuads && Statistics.SetbackPatches == ExpectedQuads;

            // ---- S5: the same construction again in a fresh partition.
            ModelPartition Again;
            std::string    Unused;
            const BodyToken    Twin        = ConstructShape(Again, Specimen.Shape, Unused);
            const BlendOutcome TwinBlended = BlendEdges(Again, Twin, SelectEdges(Again, Twin, Specimen.Pick), Settings);
            const bool Same = TwinBlended.Refusal.empty() && IntegrateBodyVolume(Again, TwinBlended.Body) == Row.Volume &&
                              DigestPolygonComplex(TessellateBody(Again, TwinBlended.Body, Fine).Complex) == DigestPolygonComplex(Mesh.Complex);
            Tally(DeterminismGate, Same ? 0.0 : 1.0, Same);
        }
        Tally(BodyGate, Row.PassedCondition ? 0.0 : 1.0, Row.PassedCondition);
        Row.Milliseconds = Elapsed(Moment);
        Rows.push_back(Row);
    }

    // ---- Report.
    const GateTally* Gates[] = { &RimGate, &TangencyGate, &SeamGate, &BodyGate, &DeterminismGate, &RefusalGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Setback vertex blend validation (fair NURBS quads)\n\n");
    std::printf("**%s** · %zu bodies · %zu refusals · %.0f ms\n\n", PassedCondition ? "🟢 PASS" : "🔴 FAIL", Rows.size(), Refusals.size(), Elapsed(Start));
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Bodies\n\n| | Specimen | Edges | Setback | Ball | Cap | Quads | Faces | Genus | V₀ | V | Rim (S1) | Solver rim | G1 (S2) | Seam (S3) | \\|V − V_polygon\\| | Bound | ms | Outcome |\n"
                "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const BodyRow& Row : Rows)
    {
        std::printf("| %s | %s | %d | %d | %d | %d | %d | %d | %d | %.12f | %.12f | %.1e | %.1e | %.2e | %.2e | %.1e | %.1e | %.0f | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Edges,
                    Row.Setbacks, Row.Corners, Row.Caps, Row.Quads, Row.Faces, Row.Genus, Row.Source, Row.Volume, Row.Rim, Row.SolverRim, Row.Tangent, Row.Seam, Row.PolygonGap, Row.PolygonBound,
                    Row.Milliseconds, Row.Outcome.c_str());
    }
    std::printf("\n## Refusals\n\n| | Specimen | Refusal |\n|---|---|---|\n");
    for (const BodyRow& Row : Refusals) std::printf("| %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Outcome.c_str());
    return PassedCondition ? 0 : 1;
}
