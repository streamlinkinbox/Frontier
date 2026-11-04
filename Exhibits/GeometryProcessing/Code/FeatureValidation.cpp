/*====================================================================================================================================
                                                     FEATUREVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Step 7 (research § L): feature trees regenerated over parameter sweeps, persistent names, reference resolution

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Feature/FeatureCatalogue.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
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

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - F1 …
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

    GateTally VolumeGate      = { "F1", "Regenerated volume vs closed form, every sweep point", "\\|V − V*\\| ≤ 10⁻⁹·V*" };
    GateTally StatusGate      = { "F2", "Refused-step set equals the oracle; final body audit-sound", "equal · sound" };
    GateTally CoverageGate    = { "F3", "Every face / edge / vertex named, names unique per kind", "0 missing · 0 duplicate" };
    GateTally SurvivorGate    = { "F4", "Geometric survivors of every step: faces keep the exact name; every survivor resolves to itself", "0 wrong" };
    GateTally KeptGate        = { "F5", "Refused step keeps the previous body; later steps still run", "all hold" };
    GateTally ResolutionGate  = { "F6", "Expected resolution methods (exact · geometric rebind · ambiguous refusal · sketch refusal)", "all as expected" };
    GateTally DeterminismGate = { "F7", "Same tree twice in fresh partitions: identical names and volume bits", "equal" };
    GateTally AnchorGate      = { "F8", "Variable blend sizes stay at their anchor vertices: expected cap vertices present", "≤ 10⁻⁹" };

    struct SweepRow
    {
        std::string Specimen, Assignment, Refused, Methods;                                  // [-]    - Labels
        int         Faces = 0, Edges = 0, Vertices = 0;                                      // [-]    - Final topology
        double      Volume = 0.0, Expected = 0.0, Error = 0.0;                               // [len³] - Measured / oracle
        double      Milliseconds = 0.0;                                                      // [ms]   - Regeneration
        bool        PassedCondition = true;                                                  // [-]    - Row gates
    };

    struct SurvivorRow
    {
        std::string Specimen, Step;                                                          // [-] - Where
        int         Faces = 0, Edges = 0, Vertices = 0;                                      // [-] - Geometric survivors
        int         Renumbered = 0;                                                          // [-] - Survivors whose inventory index changed
        int         Renamed = 0;                                                             // [-] - Survivors whose name changed
        int         Exact = 0, Source = 0, Geometric = 0, Wrong = 0;                         // [-] - Resolution of the old name
    };

    struct ExampleRow
    {
        std::string Kind, Name;                                                              // [-] - Naming-table sample
    };

    std::string AssignmentText(const FeatureAssignment& Assignment)
    {
        if (Assignment.empty()) return "authoring";
        std::string Text;
        char Buffer[64];
        for (const auto& [Name, Value] : Assignment)
        {
            std::snprintf(Buffer, sizeof(Buffer), "%s%s = %g", Text.empty() ? "" : ", ", Name.c_str(), Value);
            Text += Buffer;
        }
        return Text;
    }

    [[nodiscard]] double MeasureDiagonal(const ModelPartition& Partition, BodyToken Body)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        Vector3 Low{ 1e300, 1e300, 1e300 }, High{ -1e300, -1e300, -1e300 };
        for (VertexToken Vertex : Inventory.Vertices)
        {
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y), std::min(Low.Z, Position.Z) };
            High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y), std::max(High.Z, Position.Z) };
        }
        return Inventory.Vertices.empty() ? 1.0 : std::max(1.0, Distance(Low, High));
    }

    void TallyCoverage(const ModelPartition& Partition, BodyToken Body, const BodyNaming& Naming)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        auto TallyKind = [&](const auto& Tokens, const std::map<uint32_t, std::string>& Names)
        {
            std::set<std::string> Seen;
            int Missing = 0, Duplicate = 0;
            for (const auto& Token : Tokens)
            {
                const auto Found = Names.find(Token.Slot);
                if (Found == Names.end() || Found->second.empty()) { ++Missing; continue; }
                if (!Seen.insert(Found->second).second) ++Duplicate;
            }
            Tally(CoverageGate, Missing + Duplicate, Missing + Duplicate == 0);
        };
        TallyKind(Inventory.Faces, Naming.Faces);
        TallyKind(Inventory.Edges, Naming.Edges);
        TallyKind(Inventory.Vertices, Naming.Vertices);
    }

    // 💡 Geometric survivor oracle, independent of the names: same category, same signature position / size, and for
    //    edges the same end points (unordered). A survivor's old reference (old name + old signature) must resolve to
    //    exactly its counterpart.
    SurvivorRow CompareStep(const ModelPartition& Partition, BodyToken Before, const BodyNaming& BeforeNaming, BodyToken After,
                            const BodyNaming& AfterNaming, const std::string& Specimen, const std::string& Step)
    {
        SurvivorRow Row{ Specimen, Step };
        const double        Reach = 1e-9 * MeasureDiagonal(Partition, After);
        const BodyInventory Old   = Partition.CollectInventory(Before);
        const BodyInventory New   = Partition.CollectInventory(After);
        auto EdgeEnds = [&](EdgeToken Edge) -> std::pair<Vector3, Vector3>
        {
            const EdgeProfile& Record = Partition.Edges.Resolve(Edge);
            if (!Record.Start.PresentCondition()) return { Vector3{}, Vector3{} };
            return { Partition.Vertices.Resolve(Record.Start).Position, Partition.Vertices.Resolve(Record.End).Position };
        };
        auto Same = [&](ReferenceKind Kind, uint32_t OldSlot, uint32_t NewSlot)
        {
            const ReferenceSignature A = CaptureSignature(Partition, Kind, OldSlot), B = CaptureSignature(Partition, Kind, NewSlot);
            if (A.Category != B.Category || Distance(A.Position, B.Position) > Reach || std::fabs(A.Size - B.Size) > Reach) return false;
            if (Kind != ReferenceKind::Edge) return true;
            const auto [OldStart, OldEnd] = EdgeEnds(Partition.Edges.TokenAt(OldSlot));
            const auto [NewStart, NewEnd] = EdgeEnds(Partition.Edges.TokenAt(NewSlot));
            return (Distance(OldStart, NewStart) <= Reach && Distance(OldEnd, NewEnd) <= Reach) || (Distance(OldStart, NewEnd) <= Reach && Distance(OldEnd, NewStart) <= Reach);
        };
        auto Visit = [&](ReferenceKind Kind, const auto& OldTokens, const auto& NewTokens, const std::map<uint32_t, std::string>& OldNames,
                         const std::map<uint32_t, std::string>& NewNames, int& Count)
        {
            for (size_t OldIndex = 0; OldIndex < OldTokens.size(); ++OldIndex)
            {
                int Matches = 0;
                size_t Counterpart = 0;
                for (size_t NewIndex = 0; NewIndex < NewTokens.size(); ++NewIndex)
                {
                    if (!Same(Kind, OldTokens[OldIndex].Slot, NewTokens[NewIndex].Slot)) continue;
                    ++Matches;
                    Counterpart = NewIndex;
                }
                if (Matches != 1) continue;
                ++Count;
                Row.Renumbered += Counterpart != OldIndex ? 1 : 0;
                const std::string& OldName = OldNames.at(OldTokens[OldIndex].Slot);
                const std::string& NewName = NewNames.at(NewTokens[Counterpart].Slot);
                Row.Renamed += OldName != NewName ? 1 : 0;
                const TopologyReference Reference{ Kind, OldName, CaptureSignature(Partition, Kind, OldTokens[OldIndex].Slot) };
                const ResolvedReference Resolved = ResolveReference(Partition, After, AfterNaming, Reference);
                const bool Correct = Resolved.Method != ResolutionMethod::Refused && Resolved.Slot == NewTokens[Counterpart].Slot;
                const bool FaceExact = Kind != ReferenceKind::Face || OldName == NewName;
                if (!Correct || !FaceExact) { ++Row.Wrong; std::fprintf(stderr, "survivor %s %s: %s → %s (%s)\n", Specimen.c_str(), Step.c_str(), OldName.c_str(), NewName.c_str(), Resolved.Refusal.c_str()); continue; }
                Row.Exact     += Resolved.Method == ResolutionMethod::Exact ? 1 : 0;
                Row.Source    += Resolved.Method == ResolutionMethod::Source ? 1 : 0;
                Row.Geometric += Resolved.Method == ResolutionMethod::Geometric ? 1 : 0;
            }
        };
        Visit(ReferenceKind::Face, Old.Faces, New.Faces, BeforeNaming.Faces, AfterNaming.Faces, Row.Faces);
        Visit(ReferenceKind::Edge, Old.Edges, New.Edges, BeforeNaming.Edges, AfterNaming.Edges, Row.Edges);
        Visit(ReferenceKind::Vertex, Old.Vertices, New.Vertices, BeforeNaming.Vertices, AfterNaming.Vertices, Row.Vertices);
        Tally(SurvivorGate, Row.Wrong, Row.Wrong == 0);
        return Row;
    }

    [[nodiscard]] std::string NamesDigest(const BodyNaming& Naming)
    {
        std::vector<std::string> All;
        for (const auto& Names : { Naming.Faces, Naming.Edges, Naming.Vertices }) for (const auto& [Slot, Name] : Names) All.push_back(Name);
        std::sort(All.begin(), All.end());
        std::string Digest;
        for (const std::string& Name : All) Digest += Name + "\n";
        return Digest;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** Arguments)
{
    const auto  Start = Clock::now();
    std::string OnlyPrefix;
    for (int Index = 1; Index < ArgumentCount; ++Index)
    {
        const std::string Argument = Arguments[Index];
        if (Argument == "--only" && Index + 1 < ArgumentCount) OnlyPrefix = Arguments[++Index];
    }
    std::vector<SweepRow>    Rows;
    std::vector<SurvivorRow> Survivors;
    std::vector<ExampleRow>  Examples;
    std::map<std::string, int> MethodTotals;
    std::vector<std::string> ResolutionNotes;

    for (const std::string& Name : EnumerateFeatureSpecimens())
    {
        if (!OnlyPrefix.empty() && Name.rfind(OnlyPrefix, 0) != 0) continue;
        FeatureSpecimen Specimen = BuildFeatureSpecimen(Name);
        FeatureTree     Authored = Specimen.Tree;
        for (size_t SweepIndex = 0; SweepIndex < Specimen.Sweep.size(); ++SweepIndex)
        {
            // ① Each sweep point regenerates the authored tree (signatures captured at Sweep[0]) with its parameters.
            ModelPartition Partition;
            FeatureTree    Tree = Authored;
            AssignFeatureParameters(Tree, Specimen.Sweep[SweepIndex]);
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Tree, SweepIndex == 0);
            if (SweepIndex == 0) Authored = Tree;
            SweepRow Row;
            Row.Specimen   = Name;
            Row.Assignment = AssignmentText(Specimen.Sweep[SweepIndex]);
            Row.Milliseconds = Outcome.Milliseconds;

            std::vector<std::string> Refused;
            for (const FeatureStepReport& Step : Outcome.Steps)
            {
                if (Step.Status == FeatureStepStatus::Refused)
                {
                    Refused.push_back(Step.Label);
                    Row.Refused += (Row.Refused.empty() ? "" : "; ") + Step.Label + ": " + Step.Refusal;
                }
                for (const ResolvedReference& Resolved : Step.Resolutions)
                {
                    ++MethodTotals[ResolutionMethodLabel(Resolved.Method)];
                    Row.Methods += (Row.Methods.empty() ? "" : ", ") + std::string(ResolutionMethodLabel(Resolved.Method));
                }
            }
            const std::vector<std::string> ExpectedRefused = Specimen.ExpectedRefusals(Tree);
            const bool StatusCondition = Refused == ExpectedRefused;

            // ② F5: refused steps keep the previous body and the later steps still ran.
            for (size_t StepIndex = 1; StepIndex < Outcome.Steps.size(); ++StepIndex)
            {
                const FeatureStepReport& Step = Outcome.Steps[StepIndex];
                if (Step.Status != FeatureStepStatus::Refused || Step.Kind == FeatureStepKind::Sketch) continue;
                const bool Kept = Step.Body == Outcome.Steps[StepIndex - 1].Body;
                Tally(KeptGate, Kept ? 0.0 : 1.0, Kept);
            }

            if (Outcome.Body.PresentCondition())
            {
                const AuditReport Audit = AuditBody(Partition, Outcome.Body);
                Tally(StatusGate, StatusCondition && Audit.SoundCondition() ? 0.0 : 1.0, StatusCondition && Audit.SoundCondition());
                Row.Volume   = IntegrateBodyVolume(Partition, Outcome.Body);
                Row.Expected = Specimen.ExpectedVolume(Tree);
                Row.Error    = std::fabs(Row.Volume - Row.Expected) / std::max(1e-300, std::fabs(Row.Expected));
                Tally(VolumeGate, Row.Error, Row.Error <= 1e-9);
                const BodyInventory Inventory = Partition.CollectInventory(Outcome.Body);
                Row.Faces = static_cast<int>(Inventory.Faces.size()); Row.Edges = static_cast<int>(Inventory.Edges.size()); Row.Vertices = static_cast<int>(Inventory.Vertices.size());
                Row.PassedCondition = StatusCondition && Audit.SoundCondition() && Row.Error <= 1e-9;
                if (Specimen.ExpectedVertices)
                {
                    // F8: every expected point is a vertex of the final body (nearest vertex distance).
                    for (const Vector3& Point : Specimen.ExpectedVertices(Tree))
                    {
                        double Nearest = std::numeric_limits<double>::infinity();            // [len] - Closest vertex
                        for (VertexToken Vertex : Inventory.Vertices) Nearest = std::min(Nearest, Distance(Partition.Vertices.Resolve(Vertex).Position, Point));
                        Tally(AnchorGate, Nearest, Nearest <= 1e-9);
                        Row.PassedCondition = Row.PassedCondition && Nearest <= 1e-9;
                    }
                }
                if (!Audit.SoundCondition()) Row.Refused += " · audit: " + std::to_string(Audit.Findings.size()) + " findings";
            }
            else { Tally(StatusGate, 1.0, false); Tally(VolumeGate, 1.0, false); Row.PassedCondition = false; }

            // ③ F3 + F4 on every built body-changing step.
            for (size_t StepIndex = 0; StepIndex < Outcome.Steps.size(); ++StepIndex)
            {
                const FeatureStepReport& Step = Outcome.Steps[StepIndex];
                if (Step.Status != FeatureStepStatus::Built || !Step.Body.PresentCondition()) continue;
                if (Step.Kind == FeatureStepKind::Sketch) continue;
                TallyCoverage(Partition, Step.Body, Step.Naming);
                if (StepIndex == 0 || !Outcome.Steps[StepIndex - 1].Body.PresentCondition()) continue;
                const FeatureStepReport& Previous = Outcome.Steps[StepIndex - 1];
                if (Previous.Body == Step.Body) continue;
                Survivors.push_back(CompareStep(Partition, Previous.Body, Previous.Naming, Step.Body, Step.Naming, Name + " · " + Row.Assignment, Step.Label));
            }

            // ④ F6 expected resolution methods.
            auto MethodOf = [&](const std::string& Label) -> std::string
            {
                for (const FeatureStepReport& Step : Outcome.Steps) if (Step.Label == Label && !Step.Resolutions.empty()) return ResolutionMethodLabel(Step.Resolutions[0].Method);
                return "?";
            };
            if (Name == "SplitFacePush")
            {
                const double X = FeatureParameterValue(Tree, "GrooveX");
                const std::string Expected = X < 0.0 ? "Exact" : (X == 1.5 ? "Refused" : "Geometric");
                const std::string Method   = MethodOf("FaceOperation#5");
                Tally(ResolutionGate, Method == Expected ? 0.0 : 1.0, Method == Expected);
                ResolutionNotes.push_back("SplitFacePush GrooveX = " + std::to_string(X).substr(0, 4) + ": expected " + Expected + ", got " + Method);
            }
            if (Name == "HoleLeavesPlate")
            {
                const std::string Expected = FeatureParameterValue(Tree, "HoleX") >= 4.0 ? "Refused" : "Exact";
                const std::string Method   = MethodOf("Blend#5");
                Tally(ResolutionGate, Method == Expected ? 0.0 : 1.0, Method == Expected);
            }
            if (SweepIndex == 0 && (Name == "BlockFillet" || Name == "BossFillet"))
            {
                for (const auto& [Slot, FaceName] : Outcome.Naming.Faces) Examples.push_back({ Name + " face", FaceName });
                int Shown = 0;
                for (const auto& [Slot, EdgeName] : Outcome.Naming.Edges) if (Shown++ < 4) Examples.push_back({ Name + " edge", EdgeName });
            }
            if (!Row.PassedCondition) std::fprintf(stderr, "row %s [%s]: V %.12f vs %.12f, refused {%s}\n", Name.c_str(), Row.Assignment.c_str(), Row.Volume, Row.Expected, Row.Refused.c_str());
            Rows.push_back(Row);
        }

        // ⑤ F7 determinism: authoring configuration twice.
        std::string Digests[2];
        double      Volumes[2] = {};
        for (int Run = 0; Run < 2; ++Run)
        {
            ModelPartition Partition;
            FeatureSpecimen Fresh = BuildFeatureSpecimen(Name);
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Fresh.Tree, true);
            Digests[Run] = NamesDigest(Outcome.Naming);
            Volumes[Run] = Outcome.Body.PresentCondition() ? IntegrateBodyVolume(Partition, Outcome.Body) : -1.0;
        }
        const bool Equal = Digests[0] == Digests[1] && Volumes[0] == Volumes[1] && !Digests[0].empty();
        Tally(DeterminismGate, Equal ? 0.0 : 1.0, Equal);
    }

    // ⑥ Sketch refusal propagates: a conflicting dimension refuses the sketch, then every dependent step.
    if (OnlyPrefix.empty() || OnlyPrefix == "Conflict")
    {
        FeatureSpecimen Specimen = BuildFeatureSpecimen("BlockFillet");
        FeatureStep& Sketch = Specimen.Tree.Steps[0];
        AppendSketchConstraint(Sketch.Sketch, SketchConstraintKind::Length, 0, -1, -1, 2.5, "TopLength");
        ModelPartition Partition;
        const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Specimen.Tree, false);
        const bool Expected = Outcome.Steps.size() == 3 && Outcome.Steps[0].Status == FeatureStepStatus::Refused && Outcome.Steps[0].Refusal.find("conflicting") != std::string::npos &&
                              Outcome.Steps[1].Status == FeatureStepStatus::Refused && Outcome.Steps[2].Status == FeatureStepStatus::Refused && !Outcome.Body.PresentCondition();
        Tally(ResolutionGate, Expected ? 0.0 : 1.0, Expected);
        for (const FeatureStepReport& Step : Outcome.Steps) ResolutionNotes.push_back("ConflictingSketch " + Step.Label + ": " + (Step.Status == FeatureStepStatus::Refused ? Step.Refusal : std::string("built")));
    }

    // ---- Report.
    const GateTally* Gates[] = { &VolumeGate, &StatusGate, &CoverageGate, &SurvivorGate, &KeptGate, &ResolutionGate, &DeterminismGate, &AnchorGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !OnlyPrefix.empty());
    int Passed = 0;
    for (const SweepRow& Row : Rows) Passed += Row.PassedCondition ? 1 : 0;
    int SurvivorTotal = 0, RenumberedTotal = 0, RenamedTotal = 0;
    for (const SurvivorRow& Row : Survivors) { SurvivorTotal += Row.Faces + Row.Edges + Row.Vertices; RenumberedTotal += Row.Renumbered; RenamedTotal += Row.Renamed; }

    std::printf("# Feature validation (step 7, L + feature tree)\n\n");
    if (!OnlyPrefix.empty()) std::printf("> 🔍 Filtered run: `--only %s` (gates with 0 checks were not exercised)\n\n", OnlyPrefix.c_str());
    std::printf("**%s** · regenerations %d/%d · survivors %d: index renumbered %d, name changed %d · references:", PassedCondition ? "🟢 PASS" : "🔴 FAIL", Passed, static_cast<int>(Rows.size()), SurvivorTotal, RenumberedTotal, RenamedTotal);
    for (const auto& [Method, Count] : MethodTotals) std::printf(" %s %d", Method.c_str(), Count);
    std::printf(" · %.0f ms\n\n", Elapsed(Start));
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Regenerations\n\n| | Tree | Parameters | F / E / V | V | V* | rel. error | References | Refused steps | ms |\n|---|---|---|---|---|---|---|---|---|---|\n");
    for (const SweepRow& Row : Rows)
    {
        std::printf("| %s | %s | %s | %d / %d / %d | %.10f | %.10f | %.1e | %s | %s | %.1f |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Specimen.c_str(), Row.Assignment.c_str(), Row.Faces, Row.Edges,
                    Row.Vertices, Row.Volume, Row.Expected, Row.Error, Row.Methods.empty() ? "–" : Row.Methods.c_str(), Row.Refused.empty() ? "–" : Row.Refused.c_str(), Row.Milliseconds);
    }
    std::printf("\n## Oracles\n\n| Tree | Closed form | Steps |\n|---|---|---|\n");
    for (const std::string& Name : EnumerateFeatureSpecimens())
    {
        if (!OnlyPrefix.empty() && Name.rfind(OnlyPrefix, 0) != 0) continue;
        const FeatureSpecimen Specimen = BuildFeatureSpecimen(Name);
        std::string Steps;
        for (const FeatureStep& Step : Specimen.Tree.Steps) Steps += (Steps.empty() ? "" : " → ") + Step.Label;
        std::printf("| %s | %s | %s |\n", Name.c_str(), Specimen.Oracle.c_str(), Steps.c_str());
    }
    std::printf("\n## Survivor naming table (F4)\n\nA survivor is found geometrically (same category, position, size, end points), never by name. "
                "*Renumbered* = its inventory index moved (what a raw-index reference would break on); *renamed* = its persistent name changed.\n\n"
                "| | Tree · parameters | Step | Faces | Edges | Vertices | Renumbered | Renamed | Exact | Source | Geometric | Wrong |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const SurvivorRow& Row : Survivors)
    {
        std::printf("| %s | %s | %s | %d | %d | %d | %d | %d | %d | %d | %d | %d |\n", Row.Wrong == 0 ? "🟢" : "🔴", Row.Specimen.c_str(), Row.Step.c_str(), Row.Faces, Row.Edges, Row.Vertices, Row.Renumbered,
                    Row.Renamed, Row.Exact, Row.Source, Row.Geometric, Row.Wrong);
    }
    std::printf("\n## Resolution expectations (F6)\n\n");
    for (const std::string& Note : ResolutionNotes) std::printf("- %s\n", Note.c_str());
    std::printf("\n## Name samples\n\n| Where | Name |\n|---|---|\n");
    for (const ExampleRow& Row : Examples) std::printf("| %s | `%s` |\n", Row.Kind.c_str(), Row.Name.c_str());
    std::fflush(stdout);
    return PassedCondition ? 0 : 1;
}
