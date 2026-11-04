/*====================================================================================================================================
                                                     BODYEDITINGVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Editing phase E1: Transform (Plasticity G / R / S), Duplicate (Shift+D, Ctrl+C / Ctrl+V) and Pipe (P) feature steps

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Feature/FeatureTree.h"
#include "Engine/GeometryProcessing/Feature/PathSweep.h"
#include "Engine/GeometryProcessing/Geometry/NurbsCarrier.h"

#include <algorithm>
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
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double Pi = std::numbers::pi;

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - M1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally MoveGate      = { "M1", "Move (G): box shifted by (0.3, −0.2, 0.5) — every vertex = source + shift, volume kept, same face names, audit sound", "≤ 10⁻¹² · rel. ≤ 10⁻¹²" };
    GateTally RotateGate    = { "M2", "Rotate (R): cylinder turned 30° about a z axis through (1, 0, 0), then 90° about x — vertices exact, volume kept, audit sound", "≤ 10⁻¹² · rel. ≤ 10⁻⁹" };
    GateTally ScaleGate     = { "M3", "Scale (S): uniform ×2 about a centre (volume ×8); per-axis (1, 2, 1) on a cylinder (exact NURBS promotion, volume ×2), audit sound", "rel. ≤ 10⁻⁹" };
    GateTally NamingGate    = { "M4", "Names survive: a fillet authored by edge name after a move resolves exactly; move∘fillet volume = fillet∘move volume", "rel. ≤ 10⁻⁹" };
    GateTally DuplicateGate = { "M5", "Duplicate: a moved copy is a new active body keyed by its step, faces renamed Duplicate#k.Copy(…), no shared names, equal volume", "rel. ≤ 10⁻¹²" };
    GateTally SnapshotGate  = { "M6", "Paste after delete: Duplicate with Source = the copy-time step still copies a body removed meanwhile; replay bit-identical", "exact" };
    GateTally ExactPipeGate = { "M7", "Pipe (P) on a sketch line / arc: cylinder π r² L and torus segment π r² R θ exactly, faces Side(Path) / Cap(Start) / Cap(End); pipe subtracted from a box", "rel. ≤ 10⁻⁹" };
    GateTally FreePipeGate  = { "M8", "Pipe on a free-form SpaceCurve: rotation-minimising NURBS tube within 10⁻⁴·r of the true canal surface, plane caps, volume vs π r² L (tube formula), audit sound", "≤ 10⁻⁴·r · rel. ≤ 3·10⁻⁴" };
    GateTally RefuseGate    = { "M9", "Refusals name the cause: unknown body, singular map, copy before the body existed, pipe radius beyond the bend, closed path, multi-curve sketch, zero radius", "all named" };

    std::vector<GateTally*> AllGates() { return { &MoveGate, &RotateGate, &ScaleGate, &NamingGate, &DuplicateGate, &SnapshotGate, &ExactPipeGate, &FreePipeGate, &RefuseGate }; }

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

    [[nodiscard]] std::string Scientific(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.3e", Value);
        return Text;
    }

    [[nodiscard]] std::string Fixed(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.12f", Value);
        return Text;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                TREE BUILDING
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] FeatureStep SketchStep(const std::string& Label, SketchModel Model, OrthonormalFrame Plane = {})
    {
        FeatureStep Step;
        Step.Kind   = FeatureStepKind::Sketch;
        Step.Label  = Label;
        Step.Sketch = std::move(Model);
        Step.Plane  = Plane;
        return Step;
    }

    [[nodiscard]] SketchModel PolylineSketch(const std::vector<Vector2>& Points, bool Closed)
    {
        SketchModel Model;
        for (const Vector2& Point : Points) AppendSketchPoint(Model, Point);
        const int Count = static_cast<int>(Points.size());
        for (int Index = 0; Index + 1 < Count; ++Index) AppendSketchLine(Model, Index, Index + 1, Index + 1);
        if (Closed) AppendSketchLine(Model, Count - 1, 0, Count);
        return Model;
    }

    [[nodiscard]] SketchModel CircleSketch(Vector2 Centre, double Radius)
    {
        SketchModel Model;
        AppendSketchCircle(Model, AppendSketchPoint(Model, Centre), Radius, 1);
        return Model;
    }

    [[nodiscard]] FeatureStep ExtrudeStep(const std::string& Label, const std::string& Profile, double Extent, FeatureCombination Combination = FeatureCombination::New)
    {
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Extrude;
        Step.Label       = Label;
        Step.Profile     = Profile;
        Step.Extent      = FeatureValue{ Extent };
        Step.Combination = Combination;
        return Step;
    }

    [[nodiscard]] FeatureStep TransformStep(const std::string& Label, const std::string& Target, const AffineMap& Map)
    {
        FeatureStep Step;
        Step.Kind      = FeatureStepKind::Transform;
        Step.Label     = Label;
        Step.Target    = Target;
        Step.Placement = Map;
        return Step;
    }

    [[nodiscard]] FeatureStep DuplicateStep(const std::string& Label, const std::string& Target, const AffineMap& Map, const std::string& Source = {})
    {
        FeatureStep Step;
        Step.Kind      = FeatureStepKind::Duplicate;
        Step.Label     = Label;
        Step.Target    = Target;
        Step.Placement = Map;
        Step.Source    = Source;
        return Step;
    }

    [[nodiscard]] FeatureStep PipeStep(const std::string& Label, const std::string& Profile, const std::string& Curve, double Radius,
                                       FeatureCombination Combination = FeatureCombination::New, const std::string& Target = {})
    {
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Pipe;
        Step.Label       = Label;
        Step.Profile     = Profile;
        if (!Curve.empty()) Step.Sections = { Curve };
        Step.Size        = FeatureValue{ Radius };
        Step.Combination = Combination;
        Step.Target      = Target;
        return Step;
    }

    [[nodiscard]] FeatureStep CurveStep(const std::string& Label, const std::vector<Vector3>& Points, bool Closed = false)
    {
        FeatureStep Step;
        Step.Kind            = FeatureStepKind::SpaceCurve;
        Step.Label           = Label;
        Step.Points          = Points;
        Step.ClosedCondition = Closed;
        return Step;
    }

    // Box [−1, 1] × [−½, ½] × [0, 1]: Sketch#1 + Extrude#2.
    [[nodiscard]] FeatureTree BoxTree()
    {
        FeatureTree Tree;
        Tree.Name = "EditBox";
        Tree.Steps.push_back(SketchStep("Sketch#1", PolylineSketch({ { -1.0, -0.5 }, { 1.0, -0.5 }, { 1.0, 0.5 }, { -1.0, 0.5 } }, true)));
        Tree.Steps.push_back(ExtrudeStep("Extrude#2", "Sketch#1", 1.0));
        return Tree;
    }

    // Cylinder r 0.4, height 1.2 on (0.5, 0.2): Sketch#1 + Extrude#2.
    [[nodiscard]] FeatureTree CylinderTree()
    {
        FeatureTree Tree;
        Tree.Name = "EditCylinder";
        Tree.Steps.push_back(SketchStep("Sketch#1", CircleSketch({ 0.5, 0.2 }, 0.4)));
        Tree.Steps.push_back(ExtrudeStep("Extrude#2", "Sketch#1", 1.2));
        return Tree;
    }

    struct EditRun
    {
        ModelPartition      Partition = {};
        RegenerationOutcome Outcome   = {};
        std::string         Refusals  = {};
    };

    void Regenerate(EditRun& Run, FeatureTree Tree)
    {
        Run.Outcome = RegenerateFeatureTree(Run.Partition, Tree, true);
        for (const FeatureStepReport& Step : Run.Outcome.Steps) if (Step.Status == FeatureStepStatus::Refused) Run.Refusals += (Run.Refusals.empty() ? "" : " | ") + Step.Label + ": " + Step.Refusal;
    }

    [[nodiscard]] const RegeneratedBody* FindBody(const RegenerationOutcome& Outcome, const std::string& Key)
    {
        for (const RegeneratedBody& Body : Outcome.Bodies) if (Body.Key == Key) return &Body;
        return nullptr;
    }

    [[nodiscard]] double BodyVolume(const EditRun& Run, const std::string& Key)
    {
        const RegeneratedBody* Body = FindBody(Run.Outcome, Key);
        return Body ? IntegrateBodyVolume(Run.Partition, Body->Body) : -1.0;
    }

    [[nodiscard]] bool SoundCondition(const EditRun& Run, const std::string& Key, std::string& Text)
    {
        const RegeneratedBody* Body = FindBody(Run.Outcome, Key);
        if (!Body) { Text = "no body " + Key; return false; }
        const AuditReport Audit = AuditBody(Run.Partition, Body->Body);
        Text = Audit.SoundCondition() ? std::string("sound") : "UNSOUND " + std::string(Audit.Findings.front().Code) + " " + Audit.Findings.front().Detail;
        return Audit.SoundCondition();
    }

    [[nodiscard]] std::vector<Vector3> SortedVertices(const EditRun& Run, const std::string& Key)
    {
        std::vector<Vector3> Points;
        const RegeneratedBody* Body = FindBody(Run.Outcome, Key);
        if (!Body) return Points;
        for (VertexToken Vertex : Run.Partition.CollectInventory(Body->Body).Vertices) Points.push_back(Run.Partition.Vertices.Resolve(Vertex).Position);
        std::sort(Points.begin(), Points.end(), [](Vector3 A, Vector3 B) { return A.X != B.X ? A.X < B.X : A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z; });
        return Points;
    }

    [[nodiscard]] std::set<std::string> FaceNames(const EditRun& Run, const std::string& Key)
    {
        std::set<std::string> Names;
        if (const RegeneratedBody* Body = FindBody(Run.Outcome, Key)) for (const auto& [Slot, Name] : Body->Naming.Faces) Names.insert(Name);
        return Names;
    }

    [[nodiscard]] Vector3 ApplyMap(const AffineMap& Map, Vector3 Point) { return Map.ColumnX * Point.X + Map.ColumnY * Point.Y + Map.ColumnZ * Point.Z + Map.Shift; }

    [[nodiscard]] AffineMap ComposeMaps(const AffineMap& Outer, const AffineMap& Inner)
    {
        const auto Linear = [&](Vector3 Vector) { return Outer.ColumnX * Vector.X + Outer.ColumnY * Vector.Y + Outer.ColumnZ * Vector.Z; };
        return { Linear(Inner.ColumnX), Linear(Inner.ColumnY), Linear(Inner.ColumnZ), ApplyMap(Outer, Inner.Shift) };
    }

    // Largest distance between each mapped source vertex and its nearest image vertex (and counts must agree).
    [[nodiscard]] double VertexMismatch(const std::vector<Vector3>& Source, const std::vector<Vector3>& Image, const AffineMap& Map)
    {
        if (Source.size() != Image.size() || Source.empty()) return 1.0;
        double Worst = 0.0;
        for (const Vector3& Point : Source)
        {
            const Vector3 Mapped = ApplyMap(Map, Point);
            double Nearest = 1.0e300;
            for (const Vector3& Candidate : Image) Nearest = std::min(Nearest, Distance(Mapped, Candidate));
            Worst = std::max(Worst, Nearest);
        }
        return Worst;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                GATES
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateMove()
    {
        EditRun Source, Moved;
        Regenerate(Source, BoxTree());
        FeatureTree Tree = BoxTree();
        const AffineMap Map = TranslationMap({ 0.3, -0.2, 0.5 });
        Tree.Steps.push_back(TransformStep("Transform#3", "Extrude#2", Map));
        Regenerate(Moved, Tree);
        const double Mismatch = VertexMismatch(SortedVertices(Source, "Extrude#2"), SortedVertices(Moved, "Extrude#2"), Map);
        const double Before = BodyVolume(Source, "Extrude#2"), After = BodyVolume(Moved, "Extrude#2");
        const double Relative = std::fabs(After - Before) / Before;
        std::string Audit;
        const bool Sound = SoundCondition(Moved, "Extrude#2", Audit);
        const bool Names = FaceNames(Source, "Extrude#2") == FaceNames(Moved, "Extrude#2");
        Report(MoveGate, "Box moved", "vertex error " + Scientific(Mismatch) + " · volume " + Fixed(After) + " (rel. " + Scientific(Relative) + ") · names " + (Names ? "kept" : "CHANGED") + " · " + Audit + (Moved.Refusals.empty() ? "" : " · " + Moved.Refusals),
               std::max(Mismatch, Relative), Moved.Refusals.empty() && Mismatch <= 1.0e-12 && Relative <= 1.0e-12 && Names && Sound && Moved.Outcome.Bodies.size() == 1 && Moved.Outcome.Active == "Extrude#2");
    }

    void ValidateRotate()
    {
        EditRun Source, Turned;
        Regenerate(Source, CylinderTree());
        FeatureTree Tree = CylinderTree();
        const AffineMap First  = RotationMap({ 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, Pi / 6.0);
        const AffineMap Second = RotationMap({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, Pi / 2.0);
        Tree.Steps.push_back(TransformStep("Transform#3", "Extrude#2", First));
        Tree.Steps.push_back(TransformStep("Transform#4", "", Second));                    // Empty target: the active body
        Regenerate(Turned, Tree);
        const double Mismatch = VertexMismatch(SortedVertices(Source, "Extrude#2"), SortedVertices(Turned, "Extrude#2"), ComposeMaps(Second, First));
        const double Before = BodyVolume(Source, "Extrude#2"), After = BodyVolume(Turned, "Extrude#2");
        const double Relative = std::fabs(After - Before) / Before;
        std::string Audit;
        const bool Sound = SoundCondition(Turned, "Extrude#2", Audit);
        Report(RotateGate, "Cylinder turned twice", "vertex error " + Scientific(Mismatch) + " · volume " + Fixed(After) + " vs " + Fixed(Before) + " (rel. " + Scientific(Relative) + ") · " + Audit + (Turned.Refusals.empty() ? "" : " · " + Turned.Refusals),
               std::max(Mismatch, Relative), Turned.Refusals.empty() && Mismatch <= 1.0e-12 && Relative <= 1.0e-9 && Sound);
    }

    void ValidateScale()
    {
        struct Case { const char* Name; std::function<FeatureTree()> Build; Vector3 Factors; double Ratio; };
        const Case Cases[] = {
            { "Box ×2 about (1, 1, 1)", BoxTree, { 2.0, 2.0, 2.0 }, 8.0 },
            { "Cylinder × (1, 2, 1) (elliptic, NURBS)", CylinderTree, { 1.0, 2.0, 1.0 }, 2.0 },
            { "Box × (0.5, 1, 3)", BoxTree, { 0.5, 1.0, 3.0 }, 1.5 },
        };
        for (const Case& Each : Cases)
        {
            EditRun Source, Scaled;
            Regenerate(Source, Each.Build());
            FeatureTree Tree = Each.Build();
            Tree.Steps.push_back(TransformStep("Transform#3", "Extrude#2", ScalingMap({ 1.0, 1.0, 1.0 }, Each.Factors)));
            Regenerate(Scaled, Tree);
            const double Before = BodyVolume(Source, "Extrude#2"), After = BodyVolume(Scaled, "Extrude#2");
            const double Relative = std::fabs(After / Before - Each.Ratio) / Each.Ratio;
            std::string Audit;
            const bool Sound = SoundCondition(Scaled, "Extrude#2", Audit);
            Report(ScaleGate, Each.Name, "volume " + Fixed(After) + " = " + Fixed(After / Before) + " × source (rel. " + Scientific(Relative) + ") · " + Audit + (Scaled.Refusals.empty() ? "" : " · " + Scaled.Refusals),
                   Relative, Scaled.Refusals.empty() && Relative <= 1.0e-9 && Sound);
        }
    }

    void ValidateNaming()
    {
        // An edge on the top cap, named on the source box; the fillet is authored before / after a move.
        EditRun Probe;
        Regenerate(Probe, BoxTree());
        std::string EdgeName;
        for (const auto& [Slot, Name] : FindBody(Probe.Outcome, "Extrude#2")->Naming.Edges)
            if (Name.find("Cap(End)") != std::string::npos && Name.find("Line1") != std::string::npos) EdgeName = Name;
        FeatureStep Fillet;
        Fillet.Kind       = FeatureStepKind::Blend;
        Fillet.Label      = "Blend#4";
        Fillet.Shape      = BlendProfile::Fillet;
        Fillet.Size       = FeatureValue{ 0.2 };
        Fillet.References = { TopologyReference{ ReferenceKind::Edge, EdgeName, {} } };
        const AffineMap Map = ComposeMaps(TranslationMap({ 0.4, 0.1, -0.3 }), RotationMap({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, 0.7));
        FeatureTree MoveFirst = BoxTree(), FilletFirst = BoxTree();
        MoveFirst.Steps.push_back(TransformStep("Transform#3", "Extrude#2", Map));
        MoveFirst.Steps.push_back(Fillet);
        FilletFirst.Steps.push_back(Fillet);
        FilletFirst.Steps.push_back(TransformStep("Transform#3", "Extrude#2", Map));
        EditRun First, Second;
        Regenerate(First, MoveFirst);
        Regenerate(Second, FilletFirst);
        const FeatureStepReport& Blend = First.Outcome.Steps[3];
        const bool Exact = !Blend.Resolutions.empty() && Blend.Resolutions.front().Method == ResolutionMethod::Exact;
        const double A = BodyVolume(First, "Extrude#2"), B = BodyVolume(Second, "Extrude#2");
        const double Relative = std::fabs(A - B) / B;
        Report(NamingGate, "Fillet " + EdgeName + " after a move", std::string("reference resolved ") + (Exact ? "exactly" : "NOT exactly") + " · volumes " + Fixed(A) + " / " + Fixed(B) + " (rel. " + Scientific(Relative) + ")"
               + (First.Refusals.empty() && Second.Refusals.empty() ? "" : " · " + First.Refusals + Second.Refusals), Relative,
               !EdgeName.empty() && First.Refusals.empty() && Second.Refusals.empty() && Exact && Relative <= 1.0e-9 && A < 2.0);
    }

    void ValidateDuplicate()
    {
        FeatureTree Tree = BoxTree();
        Tree.Steps.push_back(DuplicateStep("Duplicate#3", "Extrude#2", TranslationMap({ 0.0, 2.0, 0.0 })));
        EditRun Run;
        Regenerate(Run, Tree);
        const double Original = BodyVolume(Run, "Extrude#2"), Copy = BodyVolume(Run, "Duplicate#3");
        const double Relative = std::fabs(Copy - Original) / Original;
        const std::set<std::string> First = FaceNames(Run, "Extrude#2"), Second = FaceNames(Run, "Duplicate#3");
        size_t Shared = 0, Prefixed = 0;
        for (const std::string& Name : Second) { Shared += First.count(Name); Prefixed += Name.rfind("Duplicate#3.Copy(Extrude#2.", 0) == 0 ? 1 : 0; }
        std::string Audit;
        const bool Sound = SoundCondition(Run, "Duplicate#3", Audit);
        Report(DuplicateGate, "Box duplicated 2 along y", std::to_string(Run.Outcome.Bodies.size()) + " bodies · active " + Run.Outcome.Active + " · volumes " + Fixed(Original) + " / " + Fixed(Copy) + " · " + std::to_string(Prefixed) + "/"
               + std::to_string(Second.size()) + " faces renamed · " + std::to_string(Shared) + " shared · " + Audit + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
               Relative, Run.Refusals.empty() && Run.Outcome.Bodies.size() == 2 && Run.Outcome.Active == "Duplicate#3" && Relative <= 1.0e-12 && Shared == 0 && Prefixed == Second.size() && Second.size() == 6 && Sound);
    }

    void ValidateSnapshot()
    {
        // Ctrl+C after Extrude#2, X on the box, Ctrl+V: the paste copies the box as it stood at copy time.
        FeatureTree Tree = BoxTree();
        FeatureStep Remove;
        Remove.Kind   = FeatureStepKind::Remove;
        Remove.Label  = "Remove#3";
        Remove.Target = "Extrude#2";
        Tree.Steps.push_back(Remove);
        Tree.Steps.push_back(DuplicateStep("Duplicate#4", "Extrude#2", TranslationMap({}), "Extrude#2"));
        EditRun First, Second;
        Regenerate(First, Tree);
        Regenerate(Second, Tree);
        const double Volume = BodyVolume(First, "Duplicate#4"), Again = BodyVolume(Second, "Duplicate#4");
        const bool Identical = std::memcmp(&Volume, &Again, sizeof Volume) == 0;
        Report(SnapshotGate, "Copy → X → paste", std::to_string(First.Outcome.Bodies.size()) + " body · " + First.Outcome.Active + " · volume " + Fixed(Volume) + " · replay " + (Identical ? "bit-identical" : "DIFFERS") + (First.Refusals.empty() ? "" : " · " + First.Refusals),
               std::fabs(Volume - 2.0), First.Refusals.empty() && First.Outcome.Bodies.size() == 1 && std::fabs(Volume - 2.0) <= 1.0e-12 && Identical);
    }

    void ValidateExactPipe()
    {
        struct Case { const char* Name; SketchModel Sketch; double Radius; double Expected; };
        SketchModel Arc;
        AppendSketchArc(Arc, AppendSketchPoint(Arc, { 0.0, 0.0 }), AppendSketchPoint(Arc, { 1.0, 0.0 }), AppendSketchPoint(Arc, { 0.0, 1.0 }), 1);
        const Case Cases[] = {
            { "Line 2, r 0.25 → cylinder", PolylineSketch({ { 0.0, 0.0 }, { 2.0, 0.0 } }, false), 0.25, Pi * 0.0625 * 2.0 },
            { "Quarter arc R 1, r 0.2 → torus segment", Arc, 0.2, Pi * 0.04 * 1.0 * Pi / 2.0 },
        };
        for (const Case& Each : Cases)
        {
            FeatureTree Tree;
            Tree.Steps.push_back(SketchStep("Sketch#1", Each.Sketch));
            Tree.Steps.push_back(PipeStep("Pipe#2", "Sketch#1", {}, Each.Radius));
            EditRun Run;
            Regenerate(Run, Tree);
            const double Volume = BodyVolume(Run, "Pipe#2");
            const double Relative = std::fabs(Volume - Each.Expected) / Each.Expected;
            const std::set<std::string> Names = FaceNames(Run, "Pipe#2");
            const bool Named = Names.count("Pipe#2.Side(Path)") == 1 && Names.count("Pipe#2.Cap(Start)") == 1 && Names.count("Pipe#2.Cap(End)") == 1 && Names.size() == 3;
            std::string Audit;
            const bool Sound = SoundCondition(Run, "Pipe#2", Audit);
            Report(ExactPipeGate, Each.Name, "volume " + Fixed(Volume) + " vs " + Fixed(Each.Expected) + " (rel. " + Scientific(Relative) + ") · " + std::to_string(Names.size()) + " named faces · " + Audit + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
                   Relative, Run.Refusals.empty() && Relative <= 1.0e-9 && Named && Sound);
        }
        // A bore: pipe along x at z = 0.5 through the box, subtracted (Plasticity P then W).
        FeatureTree Tree = BoxTree();
        OrthonormalFrame Plane;
        Plane.Origin = { 0.0, 0.0, 0.5 };
        Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ { -1.5, 0.0 }, { 1.5, 0.0 } }, false), Plane));
        Tree.Steps.push_back(PipeStep("Pipe#4", "Sketch#3", {}, 0.2, FeatureCombination::Difference, "Extrude#2"));
        EditRun Run;
        Regenerate(Run, Tree);
        const double Volume = BodyVolume(Run, "Extrude#2"), Expected = 2.0 - Pi * 0.04 * 2.0;
        const double Relative = std::fabs(Volume - Expected) / Expected;
        std::string Audit;
        const bool Sound = SoundCondition(Run, "Extrude#2", Audit);
        Report(ExactPipeGate, "Bore r 0.2 through the box (difference)", "volume " + Fixed(Volume) + " vs 2 − 0.08π = " + Fixed(Expected) + " (rel. " + Scientific(Relative) + ") · " + Audit + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
               Relative, Run.Refusals.empty() && Run.Outcome.Bodies.size() == 1 && Relative <= 1.0e-9 && Sound);
    }

    [[nodiscard]] std::vector<Vector3> WavePoints()
    {
        std::vector<Vector3> Points;
        for (int Index = 0; Index <= 40; ++Index)
        {
            const double Along = 2.0 * Index / 40.0;
            Points.push_back({ Along, 0.3 * std::sin(Pi * Along), 0.2 * std::cos(Pi * Along) });
        }
        return Points;
    }

    void ValidateFreePipe()
    {
        const double Radius = 0.08;
        FeatureTree Tree;
        Tree.Steps.push_back(CurveStep("Curve#1", WavePoints()));
        Tree.Steps.push_back(PipeStep("Pipe#2", {}, "Curve#1", Radius));
        EditRun Run;
        Regenerate(Run, Tree);
        const auto Fit = Run.Outcome.Curves.find("Curve#1");
        if (Fit == Run.Outcome.Curves.end()) { Report(FreePipeGate, "Wave curve", "no fitted curve · " + Run.Refusals, 1.0, false); return; }
        // Direct construction for the measured deviation and station count.
        ModelPartition Scratch;
        const PipeOutcome Direct = PipeAlongCurve(Scratch, Fit->second.Curve, Radius);
        const double Expected = Pi * Radius * Radius * Direct.Length;
        const double Volume   = BodyVolume(Run, "Pipe#2");
        const double Relative = std::fabs(Volume - Expected) / Expected;
        const std::set<std::string> Names = FaceNames(Run, "Pipe#2");
        std::string Audit;
        const bool Sound = SoundCondition(Run, "Pipe#2", Audit);
        Report(FreePipeGate, "Wave curve L " + Fixed(Direct.Length).substr(0, 6) + ", r 0.08",
               std::to_string(Direct.Sections) + " sections · radial deviation " + Scientific(Direct.Deviation) + " (≤ " + Scientific(1.0e-4 * Radius) + ") · r·κmax " + Fixed(Direct.Bend).substr(0, 5) + " · volume " + Fixed(Volume) + " vs π r² L " + Fixed(Expected)
               + " (rel. " + Scientific(Relative) + ") · " + std::to_string(Names.size()) + " faces · " + Audit + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
               std::max(Direct.Deviation / Radius, Relative), Run.Refusals.empty() && Direct.Refusal.empty() && Direct.Deviation <= 1.0e-4 * Radius && Relative <= 3.0e-4 && Names.size() == 3 && Sound);
    }

    void ValidateRefusals()
    {
        std::vector<Vector3> Ring;
        for (int Index = 0; Index < 48; ++Index) Ring.push_back({ std::cos(2.0 * Pi * Index / 48.0), std::sin(2.0 * Pi * Index / 48.0), 0.0 });
        struct Case { const char* Name; std::function<FeatureTree()> Build; const char* Expected; };
        const std::vector<Case> Cases = {
            { "Transform an unknown body", [] { FeatureTree Tree = BoxTree(); Tree.Steps.push_back(TransformStep("Transform#3", "Ghost", TranslationMap({ 1.0, 0.0, 0.0 }))); return Tree; }, "Transform: no body named Ghost" },
            { "Singular map", [] { FeatureTree Tree = BoxTree(); AffineMap Flat; Flat.ColumnZ = {}; Tree.Steps.push_back(TransformStep("Transform#3", "Extrude#2", Flat)); return Tree; }, "singular" },
            { "Copy before the body existed", [] { FeatureTree Tree = BoxTree(); Tree.Steps.push_back(DuplicateStep("Duplicate#3", "Extrude#2", TranslationMap({}), "Sketch#1")); return Tree; }, "did not exist after Sketch#1" },
            { "Pipe radius beyond the bend", [] { FeatureTree Tree; Tree.Steps.push_back(CurveStep("Curve#1", WavePoints())); Tree.Steps.push_back(PipeStep("Pipe#2", {}, "Curve#1", 0.5)); return Tree; }, "tightest bend" },
            { "Closed path", [Ring] { FeatureTree Tree; Tree.Steps.push_back(CurveStep("Curve#1", Ring, true)); Tree.Steps.push_back(PipeStep("Pipe#2", {}, "Curve#1", 0.05)); return Tree; }, "closed" },
            { "Multi-curve sketch path", [] { FeatureTree Tree; Tree.Steps.push_back(SketchStep("Sketch#1", PolylineSketch({ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 } }, false))); Tree.Steps.push_back(PipeStep("Pipe#2", "Sketch#1", {}, 0.1)); return Tree; }, "has 2 curves" },
            { "Zero radius", [] { FeatureTree Tree; Tree.Steps.push_back(SketchStep("Sketch#1", PolylineSketch({ { 0.0, 0.0 }, { 1.0, 0.0 } }, false))); Tree.Steps.push_back(PipeStep("Pipe#2", "Sketch#1", {}, 0.0)); return Tree; }, "radius must be positive" },
        };
        for (const Case& Each : Cases)
        {
            EditRun Run;
            Regenerate(Run, Each.Build());
            const bool Named = Run.Refusals.find(Each.Expected) != std::string::npos;
            Report(RefuseGate, Each.Name, Run.Refusals.empty() ? "accepted!" : Run.Refusals, Named ? 0.0 : 1.0, Named);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ValidateMove();
    ValidateRotate();
    ValidateScale();
    ValidateNaming();
    ValidateDuplicate();
    ValidateSnapshot();
    ValidateExactPipe();
    ValidateFreePipe();
    ValidateRefusals();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Body editing validation\n\n%s Editing E1: Transform (G / R / S), Duplicate (Shift+D, Ctrl+C / Ctrl+V) and Pipe (P) as feature steps\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
