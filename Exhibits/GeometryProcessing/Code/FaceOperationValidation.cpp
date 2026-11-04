/*====================================================================================================================================
                                                  FACEOPERATIONVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step 6 (research § J + K): corpus section E, chains and refusals with closed forms, audit, GWN and tessellation gates

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Containment/WindingNumber.h"
#include "Engine/GeometryProcessing/Local/FaceOperationCatalogue.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    // 📝 One gate: worst measured quantity against its limit.
    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - E1 …
        const char* Subject  = "";                                                           // [-] - What is measured
        const char* Limit    = "";                                                           // [-] - Pass condition (text)
        double      Worst    = 0.0;                                                          // [-] - Worst value
        long        Checks   = 0;                                                            // [-] - Measurements
        long        Failures = 0;                                                            // [-] - Measurements over the limit
    };

    void Tally(GateTally& Gate, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
    }

    GateTally VolumeGate       = { "E1", "Volume vs hand-derived closed form", "\\|V − V*\\| ≤ 10⁻⁹·max(1, V*)" };
    GateTally TopologyGate     = { "E2", "Audit sound · category · genus · F/E/V counts · source untouched", "all hold" };
    GateTally GeometryGate     = { "E3", "Every edge on both face carriers (9 samples) · vertices on every incident curve", "≤ 10⁻⁹·L" };
    GateTally TessellationGate = { "E4", "Tessellation N1 · N3 · N4 · N6 (sheets: laminar sides single, no N4)", "0 · 0 · ≤ 1 · 0" };
    GateTally MembershipGate   = { "E5", "Generalized winding number ±ε off every face agrees with its outward side (self-intersection probe)", "0 wrong" };
    GateTally ChainGate        = { "E6", "Chains ≡ independent reference construction (V, F/E/V)", "rel. ≤ 10⁻⁹, equal" };
    GateTally RefusalGate      = { "E7", "Refusals: atomic, verb-prefixed, source intact", "all refused" };
    GateTally DeterminismGate  = { "E8", "Same specimen twice → identical tessellation digest", "equal" };
    GateTally ScaleGate        = { "E9", "Scale 10⁻³ … 10³: relative volume error", "≤ 10⁻⁹" };

    struct SpecimenRow
    {
        std::string Name, Family, Corpus, Legacy, Outcome, Method;                          // [-]    - Labels / refusal text
        int         Faces = 0, Edges = 0, Vertices = 0, Genus = -1;                          // [-]    - Result topology
        BodyCategory Category = BodyCategory::Solid;                                          // [-]    - Result category
        double      Volume = 0.0, Expected = 0.0, Error = 0.0;                               // [len³] - Measured / closed form
        double      Residual = 0.0;                                                          // [-]    - Worst E3 residual / L
        int64_t     Triangles = 0;                                                           // [-]    - Tessellation size
        double      Milliseconds = 0.0;                                                      // [ms]   - Operation time
        bool        PassedCondition = false;                                                 // [-]    - All gates for this row
    };

    struct AppliedChain
    {
        FaceOperationOutcome Last    = {};                                                   // [-] - Final outcome
        std::string          Refusal = {};                                                   // [-] - First refusal of the chain
        double               Milliseconds = 0.0;                                             // [ms] - Total
    };

    [[nodiscard]] AppliedChain ApplySteps(ModelPartition& Partition, BodyToken Body, const std::vector<FaceOperationSettings>& Steps)
    {
        AppliedChain Chain;
        BodyToken    Cursor = Body;
        for (const FaceOperationSettings& Step : Steps)
        {
            Chain.Last = ApplyFaceOperation(Partition, Cursor, Step);
            Chain.Milliseconds += Chain.Last.Milliseconds;
            if (!Chain.Last.Refusal.empty()) { Chain.Refusal = Chain.Last.Refusal; return Chain; }
            Cursor = Chain.Last.Body;
        }
        return Chain;
    }

    [[nodiscard]] std::array<int, 3> CountTopology(const ModelPartition& Partition, BodyToken Body)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        return { static_cast<int>(Inventory.Faces.size()), static_cast<int>(Inventory.Edges.size()), static_cast<int>(Inventory.Vertices.size()) };
    }

    [[nodiscard]] double MeasureDiagonal(const ModelPartition& Partition, BodyToken Body)
    {
        Vector3 Low{ 1e300, 1e300, 1e300 }, High{ -1e300, -1e300, -1e300 };
        for (VertexToken Vertex : Partition.CollectInventory(Body).Vertices)
        {
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y), std::min(Low.Z, Position.Z) };
            High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y), std::max(High.Z, Position.Z) };
        }
        return std::max(1.0e-300, Distance(Low, High));
    }

    // 📝 E3, independent of the audit: each edge sampled at 9 τ lies on the carrier of every face that uses it, and each
    //    vertex lies on every incident edge's carrier (closest-point distances).
    [[nodiscard]] double MeasureGeometryResidual(const ModelPartition& Partition, BodyToken Body)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        double Worst = 0.0;
        for (EdgeToken Edge : Inventory.Edges)
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
            const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
            FinToken Fin = Profile.RadialFin;
            for (int Guard = 0; Fin.PresentCondition() && Guard < 8; ++Guard)
            {
                const FaceToken        Face    = Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face;
                const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface);
                for (int Sample = 0; Sample <= 8; ++Sample)
                {
                    const Vector3 Point = EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / 8.0).Position;
                    Worst = std::max(Worst, Distance(Point, EvaluateSurface(Surface, ProjectOntoSurface(Surface, Point, {})).Position));
                }
                Fin = Partition.Fins.Resolve(Fin).RadialSuccessor;
                if (Fin == Profile.RadialFin) break;
            }
            for (VertexToken Vertex : { Profile.Start, Profile.End })
            {
                const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
                const double  τ        = ProjectOntoCurve(Curve, Position, Profile.IntervalLow - 0.5);
                Worst = std::max(Worst, Distance(Position, EvaluateCurve(Curve, τ).Position));
            }
        }
        return Worst;
    }

    [[nodiscard]] bool MeasureTessellation(const ModelPartition& Partition, BodyToken Body, bool SheetCondition, int64_t& Triangles)
    {
        const TessellationOutcome     Outcome = TessellateBody(Partition, Body, {});
        const TessellationAuditReport Report  = AuditTessellation(Partition, Body, Outcome);
        Triangles = static_cast<int64_t>(Outcome.Complex.Triangles.size());
        const double VolumeRatio = SheetCondition ? 0.0 : std::fabs(Report.ComplexVolume - Report.ExactVolume) / (Outcome.Sag * Report.ComplexArea);
        const bool   PassedCondition = Outcome.Refusal.empty() && Report.UnpairedSides == Report.LaminarSides && Report.EulerComplex == Report.EulerBrep && Report.Inverted == 0 &&
                                     VolumeRatio <= 1.0 && Report.MissingEdgeSides == 0;
        Tally(TessellationGate, VolumeRatio, PassedCondition);
        return PassedCondition;
    }

    // 📝 E5: a point on the TRUE face carrier (chart centroid of a triangle) pushed ±ε along the true outward normal must
    //    classify outside / inside — wrong answers flag inverted faces, overlapping (self-intersecting) faces or leaks.
    //    Probes nearer than 20ε to a body edge are skipped; about 40 probes per body.
    [[nodiscard]] bool MeasureMembership(const ModelPartition& Partition, BodyToken Body, double Diagonal)
    {
        const TessellationOutcome Outcome     = TessellateBody(Partition, Body, {});
        const BodyContainment     Containment = PrepareBodyContainment(Partition, Body);
        const double              ε           = 1.0e-4 * Diagonal;
        std::vector<std::pair<Vector3, Vector3>> Segments;
        for (const PolygonComplexEdge& Edge : Outcome.Complex.Edges)
        {
            for (size_t Index = 1; Index < Edge.Vertices.size(); ++Index) Segments.push_back({ Outcome.Complex.Positions[Edge.Vertices[Index - 1]], Outcome.Complex.Positions[Edge.Vertices[Index]] });
        }
        const auto EdgeDistance = [&](Vector3 Point)
        {
            double Nearest = 1e300;
            for (const auto& [From, Until] : Segments)
            {
                const Vector3 Span     = Until - From;
                const double  Fraction = std::clamp(Dot(Point - From, Span) / std::max(1e-300, Dot(Span, Span)), 0.0, 1.0);
                Nearest = std::min(Nearest, Distance(Point, From + Span * Fraction));
            }
            return Nearest;
        };
        WindingSettings Coarse;
        Coarse.Precision = 1.0e-6;
        const size_t Stride = std::max<size_t>(1, Outcome.Complex.Triangles.size() / 40);
        int Wrong = 0;
        for (size_t Index = 0; Index < Outcome.Complex.Triangles.size(); Index += Stride)
        {
            const auto&            Chart   = Outcome.Complex.TriangleCharts[Index];
            const FaceToken        Face    = Outcome.Complex.Faces[Outcome.Complex.TriangleFaces[Index]].Face;
            const FaceProfile&     Profile = Partition.Faces.Resolve(Face);
            const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
            const Vector2          Middle  = (Chart[0] + Chart[1] + Chart[2]) * (1.0 / 3.0);
            const Vector3          Point   = EvaluateSurface(Surface, Middle).Position;
            if (EdgeDistance(Point) < 20.0 * ε) continue;
            const Vector3 Outward = EvaluateSurfaceNormal(Surface, Middle) * (Profile.Sense == Orientation::Forward ? 1.0 : -1.0);
            const bool Outside = ClassifySolidPoint(Containment, Point + Outward * ε, Coarse).Membership == PointMembership::Outside;
            const bool Inside  = ClassifySolidPoint(Containment, Point - Outward * ε, Coarse).Membership == PointMembership::Inside;
            Tally(MembershipGate, Outside && Inside ? 0.0 : 1.0, Outside && Inside);
            Wrong += Outside && Inside ? 0 : 1;
        }
        return Wrong == 0;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    const auto  Start = Clock::now();
    std::string OnlyPrefix;                                                                  // --only: specimen prefix ("Scale" selects E9)
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Argument = Arguments[Index];
        if (Argument == "--only" && Index + 1 < ArgumentCount) OnlyPrefix = Arguments[++Index];
    }
    std::vector<SpecimenRow> Rows;
    for (const std::string& Name : EnumerateFaceOperationSpecimens())
    {
        if (!OnlyPrefix.empty() && Name.rfind(OnlyPrefix, 0) != 0) continue;
        ModelPartition        Partition;
        FaceOperationSpecimen Specimen = ConstructFaceOperationSpecimen(Partition, Name);
        SpecimenRow Row{ Specimen.Name, Specimen.Family, Specimen.Corpus, Specimen.LegacyStatus };
        const double             Before       = IntegrateBodyVolume(Partition, Specimen.Body);
        const std::array<int, 3> BeforeCounts = CountTopology(Partition, Specimen.Body);
        const double             Diagonal     = MeasureDiagonal(Partition, Specimen.Body);
        const AppliedChain       Chain        = ApplySteps(Partition, Specimen.Body, Specimen.Steps);
        Row.Milliseconds = Chain.Milliseconds;
        const bool SourceIntact = AuditBody(Partition, Specimen.Body).SoundCondition() && IntegrateBodyVolume(Partition, Specimen.Body) == Before && CountTopology(Partition, Specimen.Body) == BeforeCounts;
        if (Specimen.RefusalExpected)
        {
            const FaceOperationKind LastKind = Specimen.Steps.back().Kind;
            const bool Refused = !Chain.Refusal.empty() && !Chain.Last.Body.PresentCondition() && Chain.Refusal.rfind(std::string(FaceOperationLabel(LastKind)) + ":", 0) == 0 && SourceIntact;
            Tally(RefusalGate, Refused ? 0.0 : 1.0, Refused);
            Row.Outcome         = Chain.Refusal.empty() ? "NOT refused" : Chain.Refusal;
            Row.PassedCondition = Refused;
            Rows.push_back(Row);
            continue;
        }
        if (!Chain.Refusal.empty())
        {
            Tally(VolumeGate, 1.0, false);
            Row.Outcome = Chain.Refusal;
            Rows.push_back(Row);
            continue;
        }
        const BodyToken    Result = Chain.Last.Body;
        const AuditReport  Audit  = AuditBody(Partition, Result);
        const std::array<int, 3> Counts = CountTopology(Partition, Result);
        Row.Category = Partition.Bodies.Resolve(Result).Category;
        Row.Faces = Counts[0]; Row.Edges = Counts[1]; Row.Vertices = Counts[2]; Row.Genus = Audit.Genus;
        Row.Method = Chain.Last.Method;
        const bool SheetCondition = Row.Category == BodyCategory::Sheet;

        // E1 volume.
        bool VolumeCondition = true;
        if (!SheetCondition)
        {
            Row.Volume   = IntegrateBodyVolume(Partition, Result);
            Row.Expected = Specimen.ExpectedVolume;
            Row.Error    = std::fabs(Row.Volume - Row.Expected);
            VolumeCondition = Row.Error <= 1.0e-9 * std::max(1.0, std::fabs(Row.Expected));
            Tally(VolumeGate, Row.Error / std::max(1.0, std::fabs(Row.Expected)), VolumeCondition);
        }
        // E2 topology.
        const bool TopologyCondition = Audit.SoundCondition() && Row.Category == Specimen.ExpectedCategory && (Specimen.ExpectedGenus < 0 || Audit.Genus == Specimen.ExpectedGenus) &&
                                       (Specimen.ExpectedCounts[0] < 0 || Counts == Specimen.ExpectedCounts) && SourceIntact;
        Tally(TopologyGate, TopologyCondition ? 0.0 : 1.0, TopologyCondition);
        // E3 geometry.
        Row.Residual = MeasureGeometryResidual(Partition, Result) / Diagonal;
        const bool GeometryCondition = Row.Residual <= 1.0e-9;
        Tally(GeometryGate, Row.Residual, GeometryCondition);
        // E4 tessellation, E5 membership.
        const bool TessellationCondition = MeasureTessellation(Partition, Result, SheetCondition, Row.Triangles);
        const bool MembershipCondition   = SheetCondition || MeasureMembership(Partition, Result, Diagonal);
        // E6 chain reference.
        bool ChainCondition = true;
        if (!Specimen.Reference.empty())
        {
            const AppliedChain Reference = ApplySteps(Partition, Specimen.Body, Specimen.Reference);
            ChainCondition = Reference.Refusal.empty();
            double Relative = 1.0;
            if (ChainCondition)
            {
                const double ReferenceVolume = IntegrateBodyVolume(Partition, Reference.Last.Body);
                Relative       = std::fabs(ReferenceVolume - Row.Volume) / std::max(1.0, std::fabs(ReferenceVolume));
                ChainCondition = Relative <= 1.0e-9 && CountTopology(Partition, Reference.Last.Body) == Counts;
            }
            Tally(ChainGate, Relative, ChainCondition);
        }
        Row.Outcome         = SheetCondition ? "sheet" : "solid";
        Row.PassedCondition = VolumeCondition && TopologyCondition && GeometryCondition && TessellationCondition && MembershipCondition && ChainCondition;
        Rows.push_back(Row);
    }

    // E8 determinism: a refusal-free specimen per verb family, twice.
    for (const char* Name : { "HollowHoleSlab", "DraftCan", "ThickenDeleteHoleSlab", "RotateHoleSlab" })
    {
        if (!OnlyPrefix.empty() && std::string(Name).rfind(OnlyPrefix, 0) != 0) continue;
        uint64_t Digests[2] = {};
        for (uint64_t& Digest : Digests)
        {
            ModelPartition        Partition;
            FaceOperationSpecimen Specimen = ConstructFaceOperationSpecimen(Partition, Name);
            const AppliedChain    Chain    = ApplySteps(Partition, Specimen.Body, Specimen.Steps);
            if (Chain.Refusal.empty()) Digest = DigestPolygonComplex(TessellateBody(Partition, Chain.Last.Body, {}).Complex);
        }
        Tally(DeterminismGate, Digests[0] == Digests[1] ? 0.0 : 1.0, Digests[0] == Digests[1] && Digests[0] != 0);
    }

    // E9 scale.
    struct ScaleRow { double Scale; double Errors[3]; };
    std::vector<ScaleRow> ScaleRows;
    const char* const ScaleSpecimens[3] = { "PushHoleSlab", "DraftCan", "HollowHoleSlab" };
    if (OnlyPrefix.empty() || OnlyPrefix == "Scale")
    {
        for (double Scale : { 1.0e-3, 1.0e-1, 1.0e1, 1.0e3 })
        {
            ScaleRow Row{ Scale, {} };
            for (int Index = 0; Index < 3; ++Index)
            {
                ModelPartition        Partition;
                FaceOperationSpecimen Specimen = ConstructFaceOperationSpecimen(Partition, ScaleSpecimens[Index], Scale);
                const AppliedChain    Chain    = ApplySteps(Partition, Specimen.Body, Specimen.Steps);
                Row.Errors[Index] = Chain.Refusal.empty() ? std::fabs(IntegrateBodyVolume(Partition, Chain.Last.Body) - Specimen.ExpectedVolume) / Specimen.ExpectedVolume : 1.0;
                Tally(ScaleGate, Row.Errors[Index], Row.Errors[Index] <= 1.0e-9);
            }
            ScaleRows.push_back(Row);
        }
    }

    // ---- Report.
    const GateTally* Gates[] = { &VolumeGate, &TopologyGate, &GeometryGate, &TessellationGate, &MembershipGate, &ChainGate, &RefusalGate, &DeterminismGate, &ScaleGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !OnlyPrefix.empty());   // ⚠️ Full run: every gate must be exercised
    std::map<std::string, std::pair<int, int>> Families;
    int CorpusHoleSlabCanRefused = 0, LegacyRefused = 0;
    for (const SpecimenRow& Row : Rows)
    {
        Families[Row.Family].first  += Row.PassedCondition ? 1 : 0;
        Families[Row.Family].second += 1;
        if (Row.Family == "CorpusE")
        {
            LegacyRefused += Row.Legacy == "refused" ? 1 : 0;
            if ((Row.Name.find("HoleSlab") != std::string::npos || Row.Name.find("Can") != std::string::npos) && Row.Outcome != "solid" && Row.Outcome != "sheet") ++CorpusHoleSlabCanRefused;
        }
    }
    std::printf("# Face-operation validation (step 6, J + K)\n\n");
    if (!OnlyPrefix.empty()) std::printf("> 🔍 Filtered run: `--only %s` (gates with 0 checks were not exercised)\n\n", OnlyPrefix.c_str());
    std::printf("**%s** · ", PassedCondition ? "🟢 PASS" : "🔴 FAIL");
    for (const auto& [Family, Count] : Families) std::printf("%s %d/%d · ", Family.c_str(), Count.first, Count.second);
    std::printf("corpus E refusals on HoleSlab / Can: %d (old FaceEditSolver: %d) · %.0f ms\n\n", CorpusHoleSlabCanRefused, LegacyRefused, Elapsed(Start));
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Corpus section E\n\n| | Specimen | Corpus command | Old kernel | Result | F / E / V | Genus | V | V* (closed form) | \\|error\\| | E3 / L | Triangles | ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const SpecimenRow& Row : Rows)
    {
        if (Row.Family != "CorpusE") continue;
        if (Row.Outcome != "solid" && Row.Outcome != "sheet")
        {
            std::printf("| %s | %s | `%s` | %s | refused | – | – | – | – | – | – | – | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Corpus.c_str(), Row.Legacy.c_str(), Row.Milliseconds);
            continue;
        }
        if (Row.Outcome == "sheet")
        {
            std::printf("| %s | %s | `%s` | %s | sheet | %d / %d / %d | %d | – | – | – | %.1e | %lld | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Corpus.c_str(), Row.Legacy.c_str(),
                        Row.Faces, Row.Edges, Row.Vertices, Row.Genus, Row.Residual, static_cast<long long>(Row.Triangles), Row.Milliseconds);
            continue;
        }
        std::printf("| %s | %s | `%s` | %s | solid | %d / %d / %d | %d | %.10f | %.10f | %.1e | %.1e | %lld | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Corpus.c_str(), Row.Legacy.c_str(),
                    Row.Faces, Row.Edges, Row.Vertices, Row.Genus, Row.Volume, Row.Expected, Row.Error, Row.Residual, static_cast<long long>(Row.Triangles), Row.Milliseconds);
    }
    std::printf("\n## Chains and extras\n\n| | Specimen | Family | Result | F / E / V | Genus | V | V* (closed form) | \\|error\\| | E3 / L | Method (last step) | ms |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const SpecimenRow& Row : Rows)
    {
        if (Row.Family != "Chain" && Row.Family != "Extra") continue;
        if (Row.Outcome != "solid" && Row.Outcome != "sheet") { std::printf("| %s | %s | %s | refused: %s | – | – | – | – | – | – | – | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Family.c_str(), Row.Outcome.c_str(), Row.Milliseconds); continue; }
        std::printf("| %s | %s | %s | %s | %d / %d / %d | %d | %.10f | %.10f | %.1e | %.1e | %s | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Family.c_str(), Row.Outcome.c_str(), Row.Faces, Row.Edges,
                    Row.Vertices, Row.Genus, Row.Volume, Row.Expected, Row.Error, Row.Residual, Row.Method.c_str(), Row.Milliseconds);
    }
    std::printf("\n## Refusals\n\n| | Specimen | Refusal |\n|---|---|---|\n");
    for (const SpecimenRow& Row : Rows) if (Row.Family == "Refusal") std::printf("| %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Outcome.c_str());
    if (!ScaleRows.empty())
    {
        std::printf("\n## Scale (E9)\n\n| | s | %s rel. error | %s rel. error | %s rel. error |\n|---|---|---|---|---|\n", ScaleSpecimens[0], ScaleSpecimens[1], ScaleSpecimens[2]);
        for (const ScaleRow& Row : ScaleRows)
        {
            const double Worst = std::max({ Row.Errors[0], Row.Errors[1], Row.Errors[2] });
            std::printf("| %s | %.0e | %.1e | %.1e | %.1e |\n", Worst <= 1.0e-9 ? "🟢" : "🔴", Row.Scale, Row.Errors[0], Row.Errors[1], Row.Errors[2]);
        }
    }
    std::fflush(stdout);
    return PassedCondition ? 0 : 1;
}
