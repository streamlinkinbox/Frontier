/*====================================================================================================================================
                                                     TOPOLOGYVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Phase-1 gate: specimens vs independent expectations, Euler invariance per edit, tokens, shells, tolerance, audit sensitivity

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Inspection/SpecimenCatalogue.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double VolumeBoundary = 1.0e-12;                                               // [-] - Relative volume error allowed

    int GateFailures = 0;                                                                    // [-] - Failed gate rows

    void ReportRow(bool PassedCondition, const std::string& Row)
    {
        if (!PassedCondition) GateFailures += 1;
        std::printf("| %s | %s\n", PassedCondition ? "\xF0\x9F\x9F\xA2" : "\xF0\x9F\x94\xB4", Row.c_str());
    }

    [[nodiscard]] std::string FormatScientific(double Magnitude)
    {
        char Buffer[32];
        std::snprintf(Buffer, sizeof(Buffer), "%.2e", Magnitude);
        return Buffer;
    }

    [[nodiscard]] std::string FirstFinding(const AuditReport& Report)
    {
        if (Report.Findings.empty()) return "-";
        const AuditFinding& Finding = Report.Findings.front();
        return std::string(Finding.Code) + " " + Finding.Subject + ": " + Finding.Detail;
    }

    [[nodiscard]] bool FindingPresent(const AuditReport& Report, const char* Code)
    {
        for (const AuditFinding& Finding : Report.Findings) if (std::string(Finding.Code) == Code) return true;
        return false;
    }

    // ---- A · specimens against hand-derived expectations
    void ValidateSpecimens()
    {
        std::printf("\n## A. Specimens (audit, counts, genus, exact volume)\n\n");
        std::printf("| | Specimen | V E F L S G (measured / expected) | Volume | Relative error | Audit |\n|---|---|---|---|---|---|\n");
        for (const std::string& Name : EnumerateSpecimens())
        {
            ModelPartition Partition;
            const SpecimenOutcome Specimen = ConstructSpecimen(Partition, Name, {});
            if (!Specimen.Refusal.empty()) { ReportRow(false, Name + " | refused: " + Specimen.Refusal + " | | | |"); continue; }
            const AuditReport   Audit     = AuditBody(Partition, Specimen.Body);
            const BodyInventory Inventory = Partition.CollectInventory(Specimen.Body);
            const double        Volume    = IntegrateBodyVolume(Partition, Specimen.Body);
            const SpecimenExpectation& Expected = Specimen.Expectation;
            const double Error = std::fabs(Volume - Expected.Volume) / Expected.Volume;      // [-] - Relative volume error
            const bool CountsMatch = static_cast<int>(Inventory.Vertices.size()) == Expected.Vertices && static_cast<int>(Inventory.Edges.size()) == Expected.Edges &&
                                     static_cast<int>(Inventory.Faces.size()) == Expected.Faces && static_cast<int>(Inventory.Loops.size()) == Expected.Loops &&
                                     static_cast<int>(Inventory.Shells.size()) == Expected.Shells && Audit.Genus == Expected.Genus;
            char Counts[160];
            std::snprintf(Counts, sizeof(Counts), "%zu %zu %zu %zu %zu %d / %d %d %d %d %d %d", Inventory.Vertices.size(), Inventory.Edges.size(), Inventory.Faces.size(),
                          Inventory.Loops.size(), Inventory.Shells.size(), Audit.Genus, Expected.Vertices, Expected.Edges, Expected.Faces, Expected.Loops, Expected.Shells, Expected.Genus);
            char VolumeText[64];
            std::snprintf(VolumeText, sizeof(VolumeText), "%.12g", Volume);
            ReportRow(CountsMatch && Error <= VolumeBoundary && Audit.SoundCondition(),
                      Name + " | " + Counts + " | " + VolumeText + " | " + FormatScientific(Error) + " | " + (Audit.SoundCondition() ? "sound" : FirstFinding(Audit)) + " |");
        }
    }

    // ---- B · teardown by collapsing edits, auditing topology after every single edit
    struct TeardownTally
    {
        int         Edits      = 0;                                                          // [-] - Edits applied
        int         Unsound    = 0;                                                          // [-] - Edits leaving invalid topology
        std::string Stall      = {};                                                         // [-] - Why the teardown stopped early
        std::string Sequence     = {};                                                       // [-] - Edit mnemonics, condensed
        std::string LastMnemonic = {};                                                       // [-] - Previous edit's mnemonic
    };

    void RecordEdit(TeardownTally& Tally, const ModelPartition& Partition, BodyToken Body, const char* Mnemonic)
    {
        Tally.Edits += 1;
        if (!AuditBody(Partition, Body, AuditScope::Topology).SoundCondition()) Tally.Unsound += 1;
        // 📝 Condense runs: append the mnemonic only when it differs from the previous edit's.
        if (Tally.LastMnemonic != Mnemonic) Tally.Sequence += std::string(Tally.Sequence.empty() ? "" : " ") + Mnemonic;
        Tally.LastMnemonic = Mnemonic;
    }

    [[nodiscard]] TeardownTally TeardownBody(ModelPartition& Partition, BodyToken Body)
    {
        TeardownTally Tally;
        for (int Guard = 0; Guard < 10000; ++Guard)
        {
            const BodyInventory Inventory = Partition.CollectInventory(Body);
            if (Inventory.Edges.empty())
            {
                // ⑤ KFMRH merges edge-less faces; ⑥ MEKR joins two isolated-vertex loops so KEV can follow.
                if (Inventory.Faces.size() > 1 && CollapseFaceConstructRing(Partition, Inventory.Faces[0], Inventory.Faces[1]).SucceededCondition())
                {
                    RecordEdit(Tally, Partition, Body, "KFMRH");
                    continue;
                }
                const FaceToken Face = Inventory.Faces.front();
                if (Partition.Faces.Resolve(Face).Loops.size() > 1)
                {
                    const auto& Loops = Partition.Faces.Resolve(Face).Loops;
                    if (!ConstructEdgeCollapseRing(Partition, { Loops[0], {} }, { Loops[1], {} }).SucceededCondition()) { Tally.Stall = "MEKR refused"; return Tally; }
                    RecordEdit(Tally, Partition, Body, "MEKR");
                    continue;
                }
                break;
            }
            bool Progress = false;                                                           // [-] - An edit was applied
            // ① KEF across two faces.
            for (EdgeToken Edge : Inventory.Edges)
            {
                const FinToken First  = Partition.Edges.Resolve(Edge).RadialFin;
                const FinToken Second = Partition.Fins.Resolve(First).RadialSuccessor;
                const FaceToken FirstFace  = Partition.Loops.Resolve(Partition.Fins.Resolve(First).Loop).Face;
                const FaceToken SecondFace = Partition.Loops.Resolve(Partition.Fins.Resolve(Second).Loop).Face;
                if (FirstFace == SecondFace) continue;
                if (CollapseEdgeFace(Partition, Edge).SucceededCondition()) { RecordEdit(Tally, Partition, Body, "KEF"); Progress = true; break; }
            }
            if (Progress) continue;
            // ② KEV on a spur (vertex of degree one).
            for (EdgeToken Edge : Inventory.Edges)
            {
                for (VertexToken Vertex : { Partition.Edges.Resolve(Edge).Start, Partition.Edges.Resolve(Edge).End })
                {
                    int Degree = 0;                                                          // [-] - Edge ends at Vertex
                    for (EdgeToken Other : Inventory.Edges)
                    {
                        Degree += Partition.Edges.Resolve(Other).Start == Vertex;
                        Degree += Partition.Edges.Resolve(Other).End == Vertex;
                    }
                    if (Degree == 1 && CollapseEdgeVertex(Partition, Edge, Vertex).SucceededCondition()) { RecordEdit(Tally, Partition, Body, "KEV"); Progress = true; break; }
                }
                if (Progress) break;
            }
            if (Progress) continue;
            // ③ KEMR on an edge used twice by one loop.
            for (EdgeToken Edge : Inventory.Edges)
            {
                if (CollapseEdgeConstructRing(Partition, Edge).SucceededCondition()) { RecordEdit(Tally, Partition, Body, "KEMR"); Progress = true; break; }
            }
            if (Progress) continue;
            // ④ MFKRH: release a ring as its own face so KEF can continue.
            for (FaceToken Face : Inventory.Faces)
            {
                if (Partition.Faces.Resolve(Face).Loops.size() < 2) continue;
                if (ConstructFaceCollapseRing(Partition, Partition.Faces.Resolve(Face).Loops.back()).SucceededCondition()) { RecordEdit(Tally, Partition, Body, "MFKRH"); Progress = true; break; }
            }
            if (!Progress) { Tally.Stall = "no collapsing edit applies"; return Tally; }
        }
        if (!CollapseBodyFaceVertex(Partition, Body).SucceededCondition()) { Tally.Stall = "KVFS refused"; return Tally; }
        Tally.Edits += 1;
        Tally.Sequence += " KVFS";
        return Tally;
    }

    void ValidateTeardown()
    {
        std::printf("\n## B. Teardown to an empty partition (topology audit after every edit)\n\n");
        std::printf("| | Specimen | Edits | Unsound after edit | Records left | Edit sequence (runs condensed) |\n|---|---|---|---|---|---|\n");
        for (const std::string& Name : EnumerateSpecimens())
        {
            if (Name == "HollowBlock") continue;
            ModelPartition Partition;
            const SpecimenOutcome Specimen = ConstructSpecimen(Partition, Name, {});
            const TeardownTally   Tally    = TeardownBody(Partition, Specimen.Body);
            const size_t Remaining = Partition.Bodies.OccupiedCount() + Partition.Regions.OccupiedCount() + Partition.Shells.OccupiedCount() +
                                     Partition.Faces.OccupiedCount() + Partition.Loops.OccupiedCount() + Partition.Fins.OccupiedCount() +
                                     Partition.Edges.OccupiedCount() + Partition.Vertices.OccupiedCount() + Partition.Curves.OccupiedCount() +
                                     Partition.Surfaces.OccupiedCount();
            ReportRow(Tally.Stall.empty() && Tally.Unsound == 0 && Remaining == 0,
                      Name + " | " + std::to_string(Tally.Edits) + " | " + std::to_string(Tally.Unsound) + " | " + std::to_string(Remaining) + " | " +
                      (Tally.Stall.empty() ? Tally.Sequence : "stalled: " + Tally.Stall) + " |");
        }
        // 📝 HollowBlock: two shells — DetachShell hands the cavity shell to a body of its own, then both are torn down.
        ModelPartition Partition;
        const SpecimenOutcome Specimen = ConstructSpecimen(Partition, "HollowBlock", {});
        const RegionToken     Solid    = Partition.Bodies.Resolve(Specimen.Body).Regions[1];
        const BodyToken       Detached = DetachShell(Partition, Partition.Regions.Resolve(Solid).Shells[1]).Value;
        const TeardownTally Outer   = TeardownBody(Partition, Specimen.Body);
        const TeardownTally Inner   = TeardownBody(Partition, Detached);
        const size_t Remaining = Partition.Bodies.OccupiedCount() + Partition.Faces.OccupiedCount() + Partition.Edges.OccupiedCount() + Partition.Vertices.OccupiedCount();
        ReportRow(Outer.Stall.empty() && Inner.Stall.empty() && Outer.Unsound + Inner.Unsound == 0 && Remaining == 0,
                  "HollowBlock (DetachShell, then both) | " + std::to_string(Outer.Edits + Inner.Edits) + " | " + std::to_string(Outer.Unsound + Inner.Unsound) + " | " +
                  std::to_string(Remaining) + " | " + Outer.Sequence + " / " + Inner.Sequence + " |");
    }

    // ---- C · tokens detect staleness across slot recycling
    void ValidateTokens()
    {
        std::printf("\n## C. Token authentication\n\n| | Check | Result |\n|---|---|---|\n");
        ModelPartition Partition;
        const BodyToken Block = ConstructBlock(Partition, {}, { 1.0, 1.0, 1.0 }).Value;
        const BodyInventory Inventory = Partition.CollectInventory(Block);
        const EdgeToken Doomed = Inventory.Edges[4];
        const bool      Collapsed = CollapseEdgeFace(Partition, Doomed).SucceededCondition();
        const bool      StaleRejected = !Partition.Edges.Authenticate(Doomed);
        const EdgeToken Reissued = Partition.Edges.Issue({});
        ReportRow(Collapsed && StaleRejected, "KEF reclaims the edge; the old token no longer authenticates | " + std::string(StaleRejected ? "rejected" : "ACCEPTED") + " |");
        ReportRow(Reissued.Slot == Doomed.Slot && Reissued.Generation == Doomed.Generation + 1 && !Partition.Edges.Authenticate(Doomed) && Partition.Edges.Authenticate(Reissued),
                  "Recycled slot " + std::to_string(Reissued.Slot) + " gets generation " + std::to_string(Reissued.Generation) + "; old token still rejected | " + LabelToken("E", Reissued) + " |");
    }

    // ---- D · shell reversal
    void ValidateShellReversal()
    {
        std::printf("\n## D. Shell reversal\n\n| | Check | Result |\n|---|---|---|\n");
        ModelPartition Partition;
        const BodyToken       Cylinder = ConstructCylinder(Partition, {}, 1.0, 2.0).Value;
        const SolidShellToken Shell    = Partition.Regions.Resolve(Partition.Bodies.Resolve(Cylinder).Regions[1]).Shells.front();
        const double          Before   = IntegrateBodyVolume(Partition, Cylinder);
        ReverseShellOrientation(Partition, Shell);
        const AuditReport Inverted = AuditBody(Partition, Cylinder);
        const double      Negated  = IntegrateBodyVolume(Partition, Cylinder);
        ReportRow(std::fabs(Negated + Before) < 1.0e-12 && FindingPresent(Inverted, "G6") && !FindingPresent(Inverted, "T3"),
                  "Once: volume " + FormatScientific(Before) + " becomes " + FormatScientific(Negated) + "; audit flags the inside-out solid region (G6), topology stays valid | " + FirstFinding(Inverted) + " |");
        ReverseShellOrientation(Partition, Shell);
        const AuditReport Restored = AuditBody(Partition, Cylinder);
        ReportRow(Restored.SoundCondition() && std::fabs(IntegrateBodyVolume(Partition, Cylinder) - Before) < 1.0e-12, "Twice: identity, audit sound | " + std::string(Restored.SoundCondition() ? "sound" : FirstFinding(Restored)) + " |");
    }

    // ---- E · tolerant modelling
    void ValidateTolerance()
    {
        std::printf("\n## E. Tolerant modelling\n\n| | Check | Result |\n|---|---|---|\n");
        {
            ModelPartition Partition;
            const SpecimenOutcome Exact = ConstructSpecimen(Partition, "TolerantBlock", { { { "Precision", 0.0 } } });
            const AuditReport     Audit = AuditBody(Partition, Exact.Body);
            ReportRow(FindingPresent(Audit, "G1") && !Audit.SoundCondition(),
                      "Vertex displaced 4e-6 with NO local precision: audit refuses (G1) | " + std::to_string(Audit.Findings.size()) + " findings, e.g. " + FirstFinding(Audit) + " |");
        }
        {
            ModelPartition Partition;
            const SpecimenOutcome Tolerant = ConstructSpecimen(Partition, "TolerantBlock", {});
            const AuditReport     Audit    = AuditBody(Partition, Tolerant.Body);
            ReportRow(Audit.SoundCondition(), "Same vertex with local precision 1e-5: audit sound (sphere covers the gap) | " + std::string(Audit.SoundCondition() ? "sound" : FirstFinding(Audit)) + " |");
        }
        {
            ModelPartition Partition;
            const BodyToken Block = ConstructBlock(Partition, {}, { 1.0, 1.0, 1.0 }).Value;
            Partition.AssignEdgePrecision(Partition.CollectInventory(Block).Edges.front(), 1.0e-4);
            const AuditReport Audit = AuditBody(Partition, Block);
            ReportRow(FindingPresent(Audit, "T5"), "Edge tube 1e-4 wider than its exact vertices: precision order violated (T5) | " + FirstFinding(Audit) + " |");
        }
    }

    // ---- F · audit sensitivity: deliberate corruption must be detected
    void ValidateSensitivity()
    {
        std::printf("\n## F. Audit sensitivity (each corruption must be caught by the named check)\n\n| | Corruption | Expected | First finding |\n|---|---|---|---|\n");
        const auto Corrupt = [](const char* Description, const char* Expected, auto&& Mutation)
        {
            ModelPartition Partition;
            const BodyToken Body = ConstructSpecimen(Partition, "PerforatedBlock", {}).Body;
            Mutation(Partition, Body);
            const AuditReport Audit = AuditBody(Partition, Body);
            ReportRow(FindingPresent(Audit, Expected), std::string(Description) + " | " + Expected + " | " + FirstFinding(Audit) + " |");
        };
        Corrupt("Successor link of one fin skips a fin", "T2", [](ModelPartition& Partition, BodyToken Body)
        {
            const FinToken Fin = Partition.CollectInventory(Body).Fins[3];
            Partition.Fins.Resolve(Fin).Successor = Partition.Fins.Resolve(Partition.Fins.Resolve(Fin).Successor).Successor;
        });
        Corrupt("Both fins of one edge forced to the same sense", "T3", [](ModelPartition& Partition, BodyToken Body)
        {
            const EdgeToken Edge = Partition.CollectInventory(Body).Edges[5];
            const FinToken  Fin  = Partition.Edges.Resolve(Edge).RadialFin;
            Partition.Fins.Resolve(Partition.Fins.Resolve(Fin).RadialSuccessor).Sense = Partition.Fins.Resolve(Fin).Sense;
        });
        Corrupt("One face sense flipped", "G5", [](ModelPartition& Partition, BodyToken Body)
        {
            const FaceToken Face = Partition.CollectInventory(Body).Faces[2];
            Partition.Faces.Resolve(Face).Sense = InvertOrientation(Partition.Faces.Resolve(Face).Sense);
        });
        Corrupt("One plane moved 1e-6 along its normal", "G2", [](ModelPartition& Partition, BodyToken Body)
        {
            auto& Plane = std::get<PlaneSurface>(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Partition.CollectInventory(Body).Faces[1]).Surface));
            Plane.Frame.Origin = Plane.Frame.Origin + Plane.Frame.ZAxis * 1.0e-6;
        });
        Corrupt("One SP-curve shifted 1e-6 in (u, v)", "G3", [](ModelPartition& Partition, BodyToken Body)
        {
            auto& Line = std::get<ParameterLine>(*Partition.Fins.Resolve(Partition.CollectInventory(Body).Fins[7]).SurfaceCurve);
            Line.Origin = Line.Origin + Vector2{ 1.0e-6, 0.0 };
        });
        Corrupt("One vertex moved 1e-6 (exact model)", "G1", [](ModelPartition& Partition, BodyToken Body)
        {
            VertexProfile& Vertex = Partition.Vertices.Resolve(Partition.CollectInventory(Body).Vertices[6]);
            Vertex.Position = Vertex.Position + Vector3{ 0.0, 0.0, 1.0e-6 };
        });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("# GeometryProcessing — TopologyValidation\n");
    ValidateSpecimens();
    ValidateTeardown();
    ValidateTokens();
    ValidateShellReversal();
    ValidateTolerance();
    ValidateSensitivity();
    std::printf("\n**%s** — %d failing row(s)\n", GateFailures == 0 ? "PASS" : "FAIL", GateFailures);
    return GateFailures == 0 ? 0 : 1;
}
