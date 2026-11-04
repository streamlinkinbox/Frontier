/*====================================================================================================================================
                                                        REPLAYVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Incremental replay E1–E4: restored prefixes are bit-identical to full replays, resume points, speed, capture runs

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Feature/FeatureCatalogue.h"
#include "Engine/GeometryProcessing/Feature/FeatureTree.h"
#include "Engine/GeometryProcessing/Feature/ReplayDigest.h"
#include "Engine/GeometryProcessing/Sketch/SketchCatalogue.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;
    using Kind  = SketchConstraintKind;

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - E1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally EquivalenceGate = { "E1", "Randomized edits (parameter, literal, delete, truncate, restore) on the 9 catalogue trees and a 34-step studded plate: the incremental replay equals a full replay on a fresh partition — step reports, body keys / tokens, persistent names, solved sketches, fitted curves, record stores (slot · generation lists), vertex positions and body volumes bit for bit, tessellation of the active body bit for bit", "0 differing bytes" };
    GateTally ResumeGate      = { "E2", "Resume point: an edit of step k (literal / delete) resumes at k, a parameter edit at the first step binding it, truncation and an unchanged tree restore every step, a re-added Duplicate resumes at its source step (the snapshot is taken there), a Mirror change at the first SpaceCurve", "exact index" };
    GateTally SpeedGate       = { "E3", "Studded plate (34 steps): replay time after an edit of the final fillet / a middle boss vs the full replay; recording overhead of the checkpoints", "tail ≥ 3× faster · middle ≥ 0.85× · overhead ≤ 40 %" };
    GateTally CaptureGate     = { "E4", "Capture runs (authoring): steps last replayed without capture are replayed again; the captured signatures and the result equal a full capture replay", "0 differing bytes" };

    std::vector<GateTally*> AllGates() { return { &EquivalenceGate, &ResumeGate, &SpeedGate, &CaptureGate }; }

    struct DetailRow
    {
        std::string Gate            = {};
        std::string Name            = {};
        std::string Detail          = {};
        bool        PassedCondition = false;
    };
    std::vector<DetailRow> Details;

    void Report(GateTally& Gate, const std::string& Name, const std::string& Detail, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
        Details.push_back({ Gate.Code, Name, Detail, PassedCondition });
    }

    [[nodiscard]] std::string Fixed(double Value, int Digits = 2)
    {
        char Text[48];
        std::snprintf(Text, sizeof Text, "%.*f", Digits, Value);
        return Text;
    }

    // ---- byte-level comparison ------------------------------------------------------------------------------------

    // 📝 Appends the comparable state of a regeneration; two runs agree when their transcripts are equal byte for byte.
    class Transcript
    {
    public:
        void Word(uint64_t Value) { Text.append(reinterpret_cast<const char*>(&Value), sizeof Value); }
        void Number(double Value) { uint64_t Bits = 0; std::memcpy(&Bits, &Value, sizeof Bits); Word(Bits); }
        void Phrase(const std::string& Value) { Word(Value.size()); Text.append(Value); }
        void Point(const Vector3& Value) { Number(Value.X); Number(Value.Y); Number(Value.Z); }
        void Names(const std::map<uint32_t, std::string>& Map) { Word(Map.size()); for (const auto& [Slot, Name] : Map) { Word(Slot); Phrase(Name); } }
        void Naming(const BodyNaming& Value) { Names(Value.Faces); Names(Value.Edges); Names(Value.Vertices); }
        template <typename Store> void Tokens(const Store& Records)
        {
            Word(Records.OccupiedCount());
            Records.Enumerate([&](auto Token) { Word(Token.Slot); Word(Token.Generation); });
        }
        std::string Text;
    };

    [[nodiscard]] std::string Transcribe(const ModelPartition& Partition, const RegenerationOutcome& Outcome, bool TessellateCondition)
    {
        Transcript Out;
        Out.Word(Outcome.Steps.size());
        for (const FeatureStepReport& Step : Outcome.Steps)
        {
            Out.Phrase(Step.Label);
            Out.Word(static_cast<uint64_t>(Step.Kind));
            Out.Word(static_cast<uint64_t>(Step.Status));
            Out.Phrase(Step.Refusal);
            Out.Word(Step.Body.Slot);
            Out.Word(Step.Body.Generation);
            Out.Phrase(Step.Target);
            Out.Naming(Step.Naming);
            for (const std::string& Note : Step.Notes) Out.Phrase(Note);
            for (const ResolvedReference& Resolved : Step.Resolutions) { Out.Word(static_cast<uint64_t>(Resolved.Method)); Out.Word(Resolved.Slot); Out.Phrase(Resolved.Matched); }
        }
        Out.Word(Outcome.Bodies.size());
        for (const RegeneratedBody& Body : Outcome.Bodies)
        {
            Out.Phrase(Body.Key);
            Out.Word(Body.Body.Slot);
            Out.Word(Body.Body.Generation);
            Out.Naming(Body.Naming);
            Out.Number(IntegrateBodyVolume(Partition, Body.Body));
        }
        Out.Phrase(Outcome.Active);
        Out.Word(Outcome.Body.Slot);
        Out.Word(static_cast<uint64_t>(Outcome.Refused));
        for (const auto& [Label, Model] : Outcome.Sketches)
        {
            Out.Phrase(Label);
            for (const SketchPoint& Point : Model.Points) { Out.Number(Point.Position.X); Out.Number(Point.Position.Y); }
            for (const SketchCurve& Curve : Model.Curves) Out.Number(Curve.Radius);
        }
        for (const auto& [Label, Fit] : Outcome.Curves)
        {
            Out.Phrase(Label);
            Out.Number(Fit.WorstDeviation);
            for (double Parameter : Fit.Parameters) Out.Number(Parameter);
        }
        Out.Word(Outcome.Panels.size());
        Out.Tokens(Partition.Bodies);   Out.Tokens(Partition.Regions); Out.Tokens(Partition.Shells);
        Out.Tokens(Partition.Faces);    Out.Tokens(Partition.Loops);   Out.Tokens(Partition.Fins);
        Out.Tokens(Partition.Edges);    Out.Tokens(Partition.Vertices);
        Out.Tokens(Partition.Surfaces); Out.Tokens(Partition.Curves);
        Partition.Vertices.Enumerate([&](VertexToken Vertex) { Out.Point(Partition.Vertices.Resolve(Vertex).Position); });
        if (TessellateCondition && Outcome.Body.PresentCondition())
        {
            const TessellationOutcome Mesh = TessellateBody(Partition, Outcome.Body, TessellationSettings{});
            Out.Phrase(Mesh.Refusal);
            Out.Word(Mesh.Complex.Triangles.size());
            for (const Vector3& Position : Mesh.Complex.Positions) Out.Point(Position);
            for (const auto& Triangle : Mesh.Complex.Triangles) for (int Corner : Triangle) Out.Word(static_cast<uint64_t>(Corner));
        }
        return Out.Text;
    }

    [[nodiscard]] size_t DifferingBytes(const std::string& First, const std::string& Second)
    {
        size_t Count = First.size() > Second.size() ? First.size() - Second.size() : Second.size() - First.size();
        for (size_t Index = 0; Index < std::min(First.size(), Second.size()); ++Index) Count += First[Index] != Second[Index] ? 1 : 0;
        return Count;
    }

    // ---- the studded plate (34 steps) -----------------------------------------------------------------------------

    void Bound(FeatureTree& Tree, SketchModel& Sketch, Kind Constraint, int First, int Second, const std::string& Name)
    {
        const int Index = AppendSketchConstraint(Sketch, Constraint, First, Second, -1, FeatureParameterValue(Tree, Name), Name);
        Sketch.Constraints[Index].Parameter = Name;
    }

    FeatureStep& Append(FeatureTree& Tree, FeatureStepKind StepKind, const std::string& Label)
    {
        FeatureStep Step;
        Step.Kind  = StepKind;
        Step.Label = Label;
        Tree.Steps.push_back(std::move(Step));
        return Tree.Steps.back();
    }

    FeatureStep& CircleStep(FeatureTree& Tree, const std::string& Label, double X, double Y, const std::string& Radius, FeatureValue Elevation)
    {
        FeatureStep& Sketch = Append(Tree, FeatureStepKind::Sketch, Label);
        const int Origin = AppendSketchPoint(Sketch.Sketch, { 0.0, 0.0 });
        const int Centre = AppendSketchPoint(Sketch.Sketch, { X, Y });
        const int Circle = AppendSketchCircle(Sketch.Sketch, Centre, FeatureParameterValue(Tree, Radius), 1);
        AppendSketchConstraint(Sketch.Sketch, Kind::Fixed, Origin, -1, -1, 0.0, "Origin", 0.0);
        AppendSketchConstraint(Sketch.Sketch, Kind::HorizontalDistance, Origin, Centre, -1, X, "X");
        AppendSketchConstraint(Sketch.Sketch, Kind::VerticalDistance, Origin, Centre, -1, Y, "Y");
        Bound(Tree, Sketch.Sketch, Kind::Radius, Circle, -1, Radius);
        Sketch.Elevation = Elevation;
        return Sketch;
    }

    FeatureStep& SweepStep(FeatureTree& Tree, const std::string& Label, const std::string& Profile, FeatureValue Extent, FeatureCombination Combination)
    {
        FeatureStep& Swept = Append(Tree, FeatureStepKind::Extrude, Label);
        Swept.Profile     = Profile;
        Swept.Extent      = Extent;
        Swept.Combination = Combination;
        return Swept;
    }

    [[nodiscard]] FeatureTree BuildStuddedPlate()
    {
        FeatureTree Tree;
        Tree.Name = "StuddedPlate";
        Tree.Parameters.push_back({ "Width", 8.0, 6.0, 10.0, "plate length" });
        Tree.Parameters.push_back({ "Depth", 3.0, 2.5, 4.0, "plate depth" });
        Tree.Parameters.push_back({ "Height", 0.6, 0.4, 0.8, "plate thickness" });
        Tree.Parameters.push_back({ "Radius", 0.15, 0.05, 0.25, "front fillet" });
        Tree.Parameters.push_back({ "Tube", 0.12, 0.08, 0.2, "pipe radius" });
        int Number = 1;
        auto Label = [&](const char* Verb) { return std::string(Verb) + "#" + std::to_string(Number++); };

        FeatureStep& Plate = Append(Tree, FeatureStepKind::Sketch, Label("Sketch"));
        const SketchRectangle Rectangle = AppendSketchRectangle(Plate.Sketch, { 0.0, 0.0 }, { 8.0, 3.0 }, 1);
        AppendSketchConstraint(Plate.Sketch, Kind::Fixed, Rectangle.FirstPoint, -1, -1, 0.0, "Origin", 0.0);
        Bound(Tree, Plate.Sketch, Kind::HorizontalDistance, Rectangle.FirstPoint, Rectangle.FirstPoint + 1, "Width");
        Bound(Tree, Plate.Sketch, Kind::VerticalDistance, Rectangle.FirstPoint, Rectangle.FirstPoint + 3, "Depth");
        const std::string PlateSketch = Tree.Steps.back().Label;
        SweepStep(Tree, Label("Extrude"), PlateSketch, ParameterValue("Height"), FeatureCombination::New);
        const std::string PlateBody = Tree.Steps.back().Label;
        for (int Boss = 0; Boss < 8; ++Boss)
        {
            const std::string Name = "Boss" + std::to_string(Boss);
            Tree.Parameters.push_back({ Name, 0.25 + 0.01 * Boss, 0.15, 0.35, "boss radius" });
            const std::string Sketch = Label("Sketch");
            CircleStep(Tree, Sketch, 0.75 + 0.9 * Boss, 2.1, Name, ParameterValue("Height", 0.5));
            SweepStep(Tree, Label("Extrude"), Sketch, LiteralValue(0.5 + 0.05 * Boss), FeatureCombination::Union);
        }
        for (int Hole = 0; Hole < 4; ++Hole)
        {
            const std::string Name = "Hole" + std::to_string(Hole);
            Tree.Parameters.push_back({ Name, 0.2, 0.1, 0.3, "hole radius" });
            const std::string Sketch = Label("Sketch");
            CircleStep(Tree, Sketch, 1.3 + 1.8 * Hole, 0.9, Name, LiteralValue(-0.5));
            SweepStep(Tree, Label("Extrude"), Sketch, ParameterValue("Height", 1.0, 1.0), FeatureCombination::Difference);
        }
        // A separate cylinder: moved, copied as it stood after the move, and merged with its copy.
        Tree.Parameters.push_back({ "Post", 0.4, 0.3, 0.5, "post radius" });
        const std::string PostSketch = Label("Sketch");
        CircleStep(Tree, PostSketch, 10.0, 1.5, "Post", LiteralValue(0.0));
        SweepStep(Tree, Label("Extrude"), PostSketch, LiteralValue(1.3), FeatureCombination::Separate);
        const std::string Post = Tree.Steps.back().Label;
        FeatureStep& Moved = Append(Tree, FeatureStepKind::Transform, Label("Transform"));
        Moved.Target = Post;
        Moved.Placement.Shift = { 1.0, 0.0, 0.1 };
        const std::string MovedLabel = Moved.Label;
        FeatureStep& Copy = Append(Tree, FeatureStepKind::Duplicate, Label("Duplicate"));
        Copy.Target = Post;
        Copy.Source = MovedLabel;
        Copy.Placement.Shift = { 0.37, 0.21, 0.17 };
        const std::string CopyLabel = Copy.Label;
        FeatureStep& Merged = Append(Tree, FeatureStepKind::Combine, Label("Combine"));
        Merged.Target      = Post;
        Merged.Tool        = CopyLabel;
        Merged.Combination = FeatureCombination::Union;
        // A pipe of its own along a fitted space curve above the plate.
        FeatureStep& Rail = Append(Tree, FeatureStepKind::SpaceCurve, Label("SpaceCurve"));
        for (int Sample = 0; Sample <= 8; ++Sample)
        {
            const double S = Sample / 8.0;
            Rail.Points.push_back({ 0.5 + 7.0 * S, -1.0 - 0.4 * std::sin(3.0 * S), 0.5 + 0.6 * S * (1.0 - S) });
        }
        const std::string RailLabel = Rail.Label;
        FeatureStep& Tube = Append(Tree, FeatureStepKind::Pipe, Label("Pipe"));
        Tube.Sections    = { RailLabel };
        Tube.Size        = ParameterValue("Tube");
        Tube.Combination = FeatureCombination::Separate;
        // The front edge of the plate, filleted last (the edit a user tunes most).
        FeatureStep& Fillet = Append(Tree, FeatureStepKind::Blend, Label("Blend"));
        Fillet.Target = PlateBody;
        Fillet.Shape  = BlendProfile::Fillet;
        Fillet.Size   = ParameterValue("Radius");
        Fillet.References.push_back({ ReferenceKind::Edge, "Edge(" + PlateBody + ".Cap(End)|" + PlateBody + ".Side(Line1))", {} });
        return Tree;
    }

    // ---- edits ----------------------------------------------------------------------------------------------------

    // Parameter names a step binds (independent of the digest code: scans the values and constraints directly).
    [[nodiscard]] bool BindsParameter(const FeatureStep& Step, const std::string& Name)
    {
        auto Bound = [&](const FeatureValue& Value) { return Value.Parameter == Name || Value.SecondParameter == Name; };
        if (Bound(Step.Elevation) || Bound(Step.Extent) || Bound(Step.Angle) || Bound(Step.Size) || Bound(Step.Amount) || Bound(Step.Tolerance)) return true;
        if (Step.EndSize && Bound(*Step.EndSize)) return true;
        for (const SketchConstraint& Constraint : Step.Sketch.Constraints) if (Constraint.Parameter == Name) return true;
        return false;
    }

    // Removing or changing a Mirror step re-fits every SpaceCurve (their ends snap onto mirror planes): the first one.
    [[nodiscard]] int FirstSpaceCurve(const FeatureTree& Tree, int Limit)
    {
        for (int Index = 0; Index < Limit; ++Index) if (Tree.Steps[Index].Kind == FeatureStepKind::SpaceCurve) return Index;
        return Limit;
    }

    [[nodiscard]] int StepIndex(const FeatureTree& Tree, const std::string& Label)
    {
        for (size_t Index = 0; Index < Tree.Steps.size(); ++Index) if (Tree.Steps[Index].Label == Label) return static_cast<int>(Index);
        return -1;
    }

    struct EditOutcome
    {
        std::string Description = {};                                                        // [-] - What changed
        int         Expected    = -1;                                                        // [-] - Expected resume index
    };

    // One random edit of Tree (Authored: the original, for restores). Expected = the step replay must resume at.
    EditOutcome EditTree(FeatureTree& Tree, const FeatureTree& Authored, std::mt19937_64& Random)
    {
        std::uniform_real_distribution<double> Unit(0.0, 1.0);
        const int Choice = static_cast<int>(Random() % 10);
        const int Count  = static_cast<int>(Tree.Steps.size());
        if (Choice <= 3 && !Tree.Parameters.empty())
        {
            FeatureParameter& Parameter = Tree.Parameters[Random() % Tree.Parameters.size()];
            Parameter.Value = Parameter.Low + (Parameter.High - Parameter.Low) * Unit(Random);
            int First = Count;
            for (int Index = 0; Index < Count; ++Index) if (BindsParameter(Tree.Steps[Index], Parameter.Name)) { First = Index; break; }
            return { "parameter " + Parameter.Name + " = " + Fixed(Parameter.Value, 4), First };
        }
        if (Choice <= 5)
        {
            std::vector<int> Literal;
            for (int Index = 0; Index < Count; ++Index)
            {
                const FeatureStep& Step = Tree.Steps[Index];
                if ((Step.Kind == FeatureStepKind::Extrude || Step.Kind == FeatureStepKind::Revolve) && Step.Extent.Parameter.empty() && Step.Extent.Literal != 0.0) Literal.push_back(Index);
                if (Step.Kind == FeatureStepKind::Transform) Literal.push_back(Index);
            }
            if (!Literal.empty())
            {
                const int Index = Literal[Random() % Literal.size()];
                FeatureStep& Step = Tree.Steps[Index];
                const double Factor = 0.9 + 0.2 * Unit(Random);
                if (Step.Kind == FeatureStepKind::Transform) Step.Placement.Shift.Z *= Factor;
                else                                         Step.Extent.Literal *= Factor;
                return { "literal of " + Step.Label + " × " + Fixed(Factor, 4), Index };
            }
        }
        if (Choice == 6 && Count > 2)
        {
            const int Index = 1 + static_cast<int>(Random() % (Count - 1));
            const std::string Label = Tree.Steps[Index].Label;
            const bool Mirror = Tree.Steps[Index].Kind == FeatureStepKind::Mirror;
            Tree.Steps.erase(Tree.Steps.begin() + Index);
            return { "delete " + Label, Mirror ? FirstSpaceCurve(Tree, Index) : Index };
        }
        if (Choice == 7 && Count > 2)
        {
            const int Kept = 1 + static_cast<int>(Random() % (Count - 1));
            bool Mirror = false;
            for (int Index = Kept; Index < Count; ++Index) Mirror = Mirror || Tree.Steps[Index].Kind == FeatureStepKind::Mirror;
            Tree.Steps.resize(Kept);
            return { "truncate to " + std::to_string(Kept) + " steps", Mirror ? FirstSpaceCurve(Tree, Kept) : Kept };
        }
        if (Choice == 8) return { "no change", Count };
        Tree = Authored;
        return { "restore the authored tree", -1 };                                          // −1: the first step that differs from before
    }

    // ---- gates ----------------------------------------------------------------------------------------------------

    void ValidateTree(const std::string& Name, FeatureTree Authored, int Edits, uint64_t Seed, bool TessellateCondition)
    {
        // Authoring: one capture run fills the signatures (as the document does on every authoring step).
        RegenerationCache Cache;
        {
            ModelPartition Partition;
            (void)RegenerateFeatureTree(Partition, Authored, true, &Cache);
        }
        FeatureTree Tree = Authored;
        std::mt19937_64 Random(Seed);
        size_t WorstBytes = 0;
        int    Matched    = 0;
        int    Restored   = 0;
        for (int Edit = 0; Edit < Edits; ++Edit)
        {
            const FeatureTree Before = Tree;
            const EditOutcome Change = EditTree(Tree, Authored, Random);
            int Expected = Change.Expected;
            if (Expected < 0)
            {
                // Restore: the first step whose content differs from the tree before, or the source step of a Duplicate
                // snapshot the tree before did not request (its restored state lacks it).
                Expected = static_cast<int>(std::min(Before.Steps.size(), Tree.Steps.size()));
                for (size_t Index = 0; Index < std::min(Before.Steps.size(), Tree.Steps.size()); ++Index)
                {
                    if (DigestFeatureStep(Before, Before.Steps[Index]) != DigestFeatureStep(Tree, Tree.Steps[Index])) { Expected = static_cast<int>(Index); break; }
                }
                for (const FeatureStep& Step : Tree.Steps)
                {
                    if (Step.Kind != FeatureStepKind::Duplicate || Step.Source.empty()) continue;
                    bool Requested = false;
                    for (const FeatureStep& Old : Before.Steps) Requested = Requested || (Old.Kind == FeatureStepKind::Duplicate && Old.Source == Step.Source && Old.Target == Step.Target);
                    const int Source = StepIndex(Tree, Step.Source);
                    if (!Requested && Source >= 0 && Source < Expected) Expected = Source;
                }
            }
            ModelPartition Incremental;
            const RegenerationOutcome Fast = RegenerateFeatureTree(Incremental, Tree, false, &Cache);
            FeatureTree Reference = Tree;
            ModelPartition Fresh;
            const RegenerationOutcome Full = RegenerateFeatureTree(Fresh, Reference, false);
            const size_t Bytes = DifferingBytes(Transcribe(Incremental, Fast, TessellateCondition), Transcribe(Fresh, Full, TessellateCondition));
            WorstBytes = std::max(WorstBytes, Bytes);
            Restored  += Fast.Resumed;
            Report(EquivalenceGate, Name + " · edit " + std::to_string(Edit + 1), Change.Description + " · resumed at " + std::to_string(Fast.Resumed) + "/" + std::to_string(Tree.Steps.size()) +
                   " · " + std::to_string(Bytes) + " differing bytes", static_cast<double>(Bytes), Bytes == 0);
            const bool Exact = Fast.Resumed == Expected;
            Matched += Exact ? 1 : 0;
            if (!Exact) Report(ResumeGate, Name + " · edit " + std::to_string(Edit + 1), Change.Description + ": resumed at " + std::to_string(Fast.Resumed) + ", expected " + std::to_string(Expected), 1.0, false);
        }
        Report(ResumeGate, Name, std::to_string(Matched) + "/" + std::to_string(Edits) + " resume points exact · " + std::to_string(Restored) + " steps restored instead of replayed", 0.0, Matched == Edits);
        (void)WorstBytes;

        // E4: a capture run after the non-capture edits equals a full capture run (result and the signatures written).
        FeatureTree Captured  = Tree;
        FeatureTree Reference = Tree;
        ModelPartition First, Second;
        const RegenerationOutcome Fast = RegenerateFeatureTree(First, Captured, true, &Cache);
        const RegenerationOutcome Full = RegenerateFeatureTree(Second, Reference, true);
        size_t Bytes = DifferingBytes(Transcribe(First, Fast, TessellateCondition), Transcribe(Second, Full, TessellateCondition));
        for (size_t Index = 0; Index < Captured.Steps.size(); ++Index) Bytes += DifferingBytes(DigestFeatureStep(Captured, Captured.Steps[Index]), DigestFeatureStep(Reference, Reference.Steps[Index]));
        Report(CaptureGate, Name, "capture run resumed at " + std::to_string(Fast.Resumed) + "/" + std::to_string(Captured.Steps.size()) + " · " + std::to_string(Bytes) + " differing bytes (result + signatures)", static_cast<double>(Bytes), Bytes == 0);
    }

    // Median of Runs timings of Action (ms).
    template <typename Action> double MedianMilliseconds(int Runs, Action&& Run)
    {
        std::vector<double> Times;
        for (int Index = 0; Index < Runs; ++Index)
        {
            const auto Started = Clock::now();
            Run();
            Times.push_back(std::chrono::duration<double, std::milli>(Clock::now() - Started).count());
        }
        std::sort(Times.begin(), Times.end());
        return Times[Times.size() / 2];
    }

    void ValidateSpeed()
    {
        const FeatureTree Authored = BuildStuddedPlate();
        FeatureTree Tree = Authored;
        const int Runs = 5;
        const double Full = MedianMilliseconds(Runs, [&]() { ModelPartition Partition; (void)RegenerateFeatureTree(Partition, Tree, false); });
        const double Recording = MedianMilliseconds(Runs, [&]() { ModelPartition Partition; RegenerationCache Fresh; (void)RegenerateFeatureTree(Partition, Tree, false, &Fresh); });
        RegenerationCache Cache;
        { ModelPartition Partition; (void)RegenerateFeatureTree(Partition, Tree, false, &Cache); }
        int Flip = 0;
        auto EditRun = [&](const std::string& Parameter, double First, double Second, int& Resumed)
        {
            return MedianMilliseconds(Runs, [&]()
            {
                (void)AssignFeatureParameter(Tree, Parameter, (Flip++ % 2) ? First : Second);
                ModelPartition Partition;
                Resumed = RegenerateFeatureTree(Partition, Tree, false, &Cache).Resumed;
            });
        };
        int TailResumed = 0, MiddleResumed = 0, NoneResumed = 0;
        const double Tail   = EditRun("Radius", 0.12, 0.18, TailResumed);
        const double Middle = EditRun("Boss4", 0.22, 0.28, MiddleResumed);
        const double None   = MedianMilliseconds(Runs, [&]() { ModelPartition Partition; NoneResumed = RegenerateFeatureTree(Partition, Tree, false, &Cache).Resumed; });
        const double Overhead = Recording / Full - 1.0;
        const int Steps = static_cast<int>(Tree.Steps.size());
        Report(SpeedGate, "full replay", std::to_string(Steps) + " steps · " + Fixed(Full) + " ms (median of 5)", 0.0, true);
        Report(SpeedGate, "recording", "full replay saving a checkpoint per step · " + Fixed(Recording) + " ms · overhead " + Fixed(100.0 * Overhead, 1) + " %", Overhead, Overhead <= 0.40);
        Report(SpeedGate, "tail edit (fillet radius)", "resumed at " + std::to_string(TailResumed) + "/" + std::to_string(Steps) + " · " + Fixed(Tail) + " ms · " + Fixed(Full / Tail, 1) + "× faster", Full / Tail, Full / Tail >= 3.0);
        Report(SpeedGate, "middle edit (Boss4 radius)", "resumed at " + std::to_string(MiddleResumed) + "/" + std::to_string(Steps) + " · " + Fixed(Middle) + " ms · " + Fixed(Full / Middle, 1) + "× faster", Full / Middle, Full / Middle >= 0.85);
        Report(SpeedGate, "unchanged tree", "resumed at " + std::to_string(NoneResumed) + "/" + std::to_string(Steps) + " · " + Fixed(None) + " ms · " + Fixed(Full / std::max(None, 1e-3), 0) + "× faster", 0.0, NoneResumed == Steps);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    uint64_t Seed = 0x5EED0001u;
    for (const std::string& Name : EnumerateFeatureSpecimens())
    {
        FeatureSpecimen Specimen = BuildFeatureSpecimen(Name);
        ValidateTree(Name, Specimen.Tree, 12, Seed++, true);
    }
    ValidateTree("StuddedPlate", BuildStuddedPlate(), 30, Seed++, true);
    ValidateSpeed();

    std::printf("# Incremental replay — E1–E4\n\n");
    std::printf("Each regeneration saves the replay state after every step (partition copy + bodies, sketches, curves, panels); the next\n");
    std::printf("regeneration restores the longest prefix whose step digests (fields + bound parameter values; SpaceCurves also the\n");
    std::printf("Mirror planes) are unchanged and whose Duplicate snapshots are all taken, and replays from the first changed step.\n\n");
    std::printf("| Gate | Subject | Limit | Worst | Checks | Result |\n|---|---|---|---|---|---|\n");
    long Failures = 0;
    for (const GateTally* Gate : AllGates())
    {
        Failures += Gate->Failures;
        char Worst[32];
        std::snprintf(Worst, sizeof Worst, "%.3g", Gate->Worst);
        std::printf("| %s | %s | %s | %s | %ld | %s |\n", Gate->Code, Gate->Subject, Gate->Limit, Worst, Gate->Checks, Gate->Failures == 0 ? "🟢" : "🔴");
    }
    std::printf("\n## Details\n\n| Gate | Case | Detail | Result |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str(), Row.PassedCondition ? "🟢" : "🔴");
    std::printf("\n%s\n", Failures == 0 ? "ALL GATES PASSED" : "GATES FAILED");
    return Failures == 0 ? 0 : 1;
}
