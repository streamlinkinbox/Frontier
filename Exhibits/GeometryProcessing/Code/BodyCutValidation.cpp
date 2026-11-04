/*====================================================================================================================================
                                                       BODYCUTVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Trim / split phase T1–T2: Cut (Plasticity C) splits solids and sheets by sketch profiles and projected curves; Remove

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Feature/BodyCut.h"
#include "Engine/GeometryProcessing/Feature/FeatureTree.h"
#include "Engine/GeometryProcessing/Sketch/SketchSpline.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - K1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally LineGate      = { "K1", "Box 2 × 1 × 1 cut by straight lines (vertical, oblique, near a corner): both volumes vs the clipped polygon area × height", "rel. ≤ 10⁻⁹" };
    GateTally ClosedGate    = { "K2", "Closed profiles cut out their interior: circle → πr²h inside, box − πr²h outside; rectangle overlapping the box edge", "rel. ≤ 10⁻⁹" };
    GateTally CylinderGate  = { "K3", "Cylinder cut by a chord line at distance d: segment r²·acos(d/r) − d·√(r² − d²) times the height, both sides", "rel. ≤ 10⁻⁹" };
    GateTally SplineGate    = { "K4", "Box cut by a cubic spline: inside volume vs ∫(top − f) dx (Gauss–Legendre on the spline), cut edges on the spline", "rel. ≤ 10⁻⁹ · ≤ 10⁻⁷" };
    GateTally ChainGate     = { "K5", "Chained cutter (3 lines, U notch): the notch comes off as the outside piece, 0.8 × 0.6 × 1", "rel. ≤ 10⁻⁹" };
    GateTally TreeGate      = { "K6", "Tree: Extrude → Sketch → Cut → Remove: body keys, active body, cut faces named Cut#4.Side(Line1), audit sound, bit-identical twice", "exact" };
    GateTally RefuseGate    = { "K7", "Refusals name the cause: cut misses, no such body, two chains, self-crossing chain, zero direction, Remove unknown", "all named" };
    GateTally PlanarGate    = { "S1", "Planar loft sheet (2 × 1) cut by a line: pieces stay sheets, areas 1.3 / 0.7 exactly, audit sound, no tool caps", "≤ 10⁻⁹" };
    GateTally CurvedGate    = { "S2", "Curved bonnet sheet cut by a SpaceCurve projected along −z: projection exact, trim edges on the panel and on the curve, area kept (fine tessellation)", "≤ 10⁻¹² · ≤ 10⁻⁷ · rel. ≤ 5·10⁻⁶" };
    GateTally HoleGate      = { "S3", "Bonnet sheet cut by a closed circle: disc piece + holed sheet (inner loop), trim edges on the circle, area kept (fine tessellation)", "≤ 10⁻⁷ · rel. ≤ 5·10⁻⁶" };

    std::vector<GateTally*> AllGates() { return { &LineGate, &ClosedGate, &CylinderGate, &SplineGate, &ChainGate, &TreeGate, &RefuseGate, &PlanarGate, &CurvedGate, &HoleGate }; }

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

    [[nodiscard]] FeatureStep ExtrudeStep(const std::string& Label, const std::string& Profile, double Extent)
    {
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Extrude;
        Step.Label       = Label;
        Step.Profile     = Profile;
        Step.Extent      = FeatureValue{ Extent };
        Step.Combination = FeatureCombination::New;
        return Step;
    }

    [[nodiscard]] FeatureStep CutStep(const std::string& Label, const std::string& Profile, const std::string& Target = {})
    {
        FeatureStep Step;
        Step.Kind    = FeatureStepKind::Cut;
        Step.Label   = Label;
        Step.Profile = Profile;
        Step.Target  = Target;
        return Step;
    }

    [[nodiscard]] FeatureStep CurveStep(const std::string& Label, const std::vector<Vector3>& Points)
    {
        FeatureStep Step;
        Step.Kind   = FeatureStepKind::SpaceCurve;
        Step.Label  = Label;
        Step.Points = Points;
        return Step;
    }

    [[nodiscard]] FeatureStep LoftStep(const std::string& Label, const std::vector<std::string>& Sections)
    {
        FeatureStep Step;
        Step.Kind     = FeatureStepKind::Loft;
        Step.Label    = Label;
        Step.Sections = Sections;
        return Step;
    }

    // Box [−1, 1] × [−½, ½] × [0, 1]: Sketch#1 + Extrude#2.
    [[nodiscard]] FeatureTree BoxTree()
    {
        FeatureTree Tree;
        Tree.Name = "CutBox";
        Tree.Steps.push_back(SketchStep("Sketch#1", PolylineSketch({ { -1.0, -0.5 }, { 1.0, -0.5 }, { 1.0, 0.5 }, { -1.0, 0.5 } }, true)));
        Tree.Steps.push_back(ExtrudeStep("Extrude#2", "Sketch#1", 1.0));
        return Tree;
    }

    struct CutRun
    {
        ModelPartition      Partition  = {};
        RegenerationOutcome Outcome    = {};
        std::string         Refusals   = {};
    };

    void Regenerate(CutRun& Run, FeatureTree& Tree)
    {
        Run.Outcome = RegenerateFeatureTree(Run.Partition, Tree, false);
        for (const FeatureStepReport& Step : Run.Outcome.Steps) if (Step.Status == FeatureStepStatus::Refused) Run.Refusals += (Run.Refusals.empty() ? "" : " | ") + Step.Label + ": " + Step.Refusal;
    }

    [[nodiscard]] const RegeneratedBody* FindBody(const RegenerationOutcome& Outcome, const std::string& Key)
    {
        for (const RegeneratedBody& Body : Outcome.Bodies) if (Body.Key == Key) return &Body;
        return nullptr;
    }

    [[nodiscard]] double BodyVolume(const CutRun& Run, const std::string& Key)
    {
        const RegeneratedBody* Body = FindBody(Run.Outcome, Key);
        return Body ? IntegrateBodyVolume(Run.Partition, Body->Body) : -1.0;
    }

    [[nodiscard]] std::string AuditText(const ModelPartition& Partition, BodyToken Body)
    {
        const AuditReport Audit = AuditBody(Partition, Body);
        return Audit.SoundCondition() ? std::string("sound") : "UNSOUND " + std::string(Audit.Findings.front().Code) + " " + Audit.Findings.front().Detail;
    }

    // Area of the convex polygon left of the directed line A → B (Sutherland–Hodgman, one half-plane).
    [[nodiscard]] double ClippedArea(const std::vector<Vector2>& Polygon, Vector2 A, Vector2 B)
    {
        std::vector<Vector2> Kept;
        const auto Side = [&](Vector2 Point) { return Cross(B - A, Point - A); };
        for (size_t Index = 0; Index < Polygon.size(); ++Index)
        {
            const Vector2 Here = Polygon[Index], Next = Polygon[(Index + 1) % Polygon.size()];
            const double  SideHere = Side(Here), SideNext = Side(Next);
            if (SideHere >= 0.0) Kept.push_back(Here);
            if ((SideHere >= 0.0) != (SideNext >= 0.0)) Kept.push_back(Here + (Next - Here) * (SideHere / (SideHere - SideNext)));
        }
        double Area = 0.0;
        for (size_t Index = 0; Index < Kept.size(); ++Index) Area += Cross(Kept[Index], Kept[(Index + 1) % Kept.size()]);
        return 0.5 * Area;
    }

    [[nodiscard]] double Relative(double Value, double Expected) { return std::fabs(Value - Expected) / std::max(std::fabs(Expected), 1.0e-300); }

    // Both pieces of a cut tree vs expected volumes (inside = the Cut step's body, outside = the target's key).
    void ReportVolumes(GateTally& Gate, const std::string& Name, FeatureTree& Tree, const std::string& CutLabel, const std::string& TargetKey, double Inside, double Outside)
    {
        CutRun Run;
        Regenerate(Run, Tree);
        if (!Run.Refusals.empty()) { Report(Gate, Name, Run.Refusals, 1.0, false); return; }
        const double InsideVolume = BodyVolume(Run, CutLabel), OutsideVolume = BodyVolume(Run, TargetKey);
        const double Worst = std::max(Relative(InsideVolume, Inside), Relative(OutsideVolume, Outside));
        const std::string Audits = AuditText(Run.Partition, FindBody(Run.Outcome, CutLabel)->Body) + " / " + AuditText(Run.Partition, FindBody(Run.Outcome, TargetKey)->Body);
        Report(Gate, Name, "inside " + Fixed(InsideVolume) + " (exact " + Fixed(Inside) + ") · outside " + Fixed(OutsideVolume) + " (exact " + Fixed(Outside) + ") · " + Audits + " · rel " + Scientific(Worst),
               Worst, Worst <= 1.0e-9 && Audits == "sound / sound");
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                EDGE AND AREA PROBES
    //--------------------------------------------------------------------------------------------------------------------

    // Distance in the plane from Point to a segment (dense samples, then golden-section refinement).
    [[nodiscard]] double SegmentDistance(const ProfileSegment& Segment, Vector2 Point)
    {
        constexpr int Samples = 2000;
        int Best = 0;
        double BestDistance = 1e300;
        for (int Index = 0; Index <= Samples; ++Index)
        {
            const double Here = Distance(EvaluateProfileSegment(Segment, static_cast<double>(Index) / Samples), Point);
            if (Here < BestDistance) { BestDistance = Here; Best = Index; }
        }
        double Low = std::max(0.0, (Best - 1.0) / Samples), High = std::min(1.0, (Best + 1.0) / Samples);
        const double Ratio = 0.5 * (std::sqrt(5.0) - 1.0);
        for (int Iteration = 0; Iteration < 80; ++Iteration)
        {
            const double First = High - Ratio * (High - Low), Second = Low + Ratio * (High - Low);
            if (Distance(EvaluateProfileSegment(Segment, First), Point) < Distance(EvaluateProfileSegment(Segment, Second), Point)) High = Second;
            else Low = First;
        }
        return std::min(BestDistance, Distance(EvaluateProfileSegment(Segment, 0.5 * (Low + High)), Point));
    }

    [[nodiscard]] Vector2 PlaneCoordinates(const OrthonormalFrame& Plane, Vector3 Point)
    {
        const Vector3 Offset = Point - Plane.Origin;
        return { Dot(Offset, Plane.XAxis), Dot(Offset, Plane.YAxis) };
    }

    struct TrimProbe
    {
        int    CutEdges    = 0;                                                              // [-]   - Edges found on the cutter
        int    Samples     = 0;                                                              // [-]   - Points tested on them
        double CurveWorst  = 0.0;                                                            // [len] - In-plane distance to the cutter
        double SurfaceWorst = 0.0;                                                           // [len] - Distance to the reference surface
    };

    // Edges whose midpoint projects onto the cutter (within 10⁻⁵) are trim edges; 33 samples each are measured.
    [[nodiscard]] TrimProbe ProbeTrimEdges(const ModelPartition& Partition, BodyToken Body, const OrthonormalFrame& Plane, const std::function<double(Vector2)>& CutterDistance,
                                           const SurfaceGeometry* Reference)
    {
        TrimProbe Probe;
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        for (EdgeToken Edge : Inventory.Edges)
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
            const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
            const auto At = [&](double Fraction) { return EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Fraction).Position; };
            if (CutterDistance(PlaneCoordinates(Plane, At(0.5))) > 1.0e-5) continue;
            ++Probe.CutEdges;
            for (int Index = 0; Index <= 32; ++Index)
            {
                const Vector3 Point = At(Index / 32.0);
                ++Probe.Samples;
                Probe.CurveWorst = std::max(Probe.CurveWorst, CutterDistance(PlaneCoordinates(Plane, Point)));
                if (Reference != nullptr)
                {
                    double Best = 1e300;
                    for (double SeedU : { 0.1, 0.3, 0.5, 0.7, 0.9 })
                    {
                        for (double SeedV : { 0.1, 0.5, 0.9 })
                        {
                            const Vector2 Foot = ProjectOntoSurface(*Reference, Point, { SeedU, SeedV });
                            Best = std::min(Best, Length(EvaluateSurface(*Reference, Foot).Position - Point));
                        }
                    }
                    Probe.SurfaceWorst = std::max(Probe.SurfaceWorst, Best);
                }
            }
        }
        return Probe;
    }

    // Area of a body from a fine tessellation (exact for planar faces; curved: error ∝ sag, 4× finer sag → ≈ 4× smaller residual).
    [[nodiscard]] double TessellatedArea(const ModelPartition& Partition, BodyToken Body)
    {
        TessellationSettings Settings;
        Settings.SagRatio = 5.0e-6;
        const TessellationOutcome Tessellation = TessellateBody(Partition, Body, Settings);
        if (!Tessellation.Refusal.empty()) return -1.0;
        double Area = 0.0;
        for (const auto& Triangle : Tessellation.Complex.Triangles)
        {
            const Vector3 A = Tessellation.Complex.Positions[Triangle[0]], B = Tessellation.Complex.Positions[Triangle[1]], C = Tessellation.Complex.Positions[Triangle[2]];
            Area += 0.5 * Length(Cross(B - A, C - A));
        }
        return Area;
    }

    [[nodiscard]] int LaminarEdges(const ModelPartition& Partition, BodyToken Body)
    {
        int Count = 0;
        for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            const FinToken Fin = Partition.Edges.Resolve(Edge).RadialFin;
            Count += Partition.RadialMate(Fin) == Fin ? 1 : 0;
        }
        return Count;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                K1–K7 SOLIDS
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateSolids()
    {
        const std::vector<Vector2> Box = { { -1.0, -0.5 }, { 1.0, -0.5 }, { 1.0, 0.5 }, { -1.0, 0.5 } };

        // ---- K1 straight lines: inside = left of the directed line.
        struct LineCase { const char* Name; Vector2 Start; Vector2 End; };
        for (const LineCase& Case : { LineCase{ "Vertical x = 0.3", { 0.3, -3.0 }, { 0.3, 3.0 } },
                                      LineCase{ "Oblique through the box", { -2.5, -1.7 }, { 2.0, 1.4 } },
                                      LineCase{ "Near the corner (1, ½)", { 0.55, 0.9 }, { 1.4, 0.1 } } })
        {
            FeatureTree Tree = BoxTree();
            Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ Case.Start, Case.End }, false)));
            Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
            const double Inside = ClippedArea(Box, Case.Start, Case.End);
            ReportVolumes(LineGate, Case.Name, Tree, "Cut#4", "Extrude#2", Inside, 2.0 - Inside);
        }

        // ---- K2 closed profiles.
        {
            FeatureTree Tree = BoxTree();
            Tree.Steps.push_back(SketchStep("Sketch#3", CircleSketch({ 0.2, 0.0 }, 0.3)));
            Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
            const double Disc = std::numbers::pi * 0.09;
            ReportVolumes(ClosedGate, "Circle r 0.3 inside the box", Tree, "Cut#4", "Extrude#2", Disc, 2.0 - Disc);
        }
        {
            FeatureTree Tree = BoxTree();
            Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ { 0.6, -0.2 }, { 1.5, -0.2 }, { 1.5, 0.3 }, { 0.6, 0.3 } }, true)));
            Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
            const double Overlap = 0.4 * 0.5;
            ReportVolumes(ClosedGate, "Rectangle over the edge x = 1", Tree, "Cut#4", "Extrude#2", Overlap, 2.0 - Overlap);
        }

        // ---- K3 cylinder r ½, height 1, chord y = 0.2 directed +x (inside: y > 0.2).
        {
            FeatureTree Tree;
            Tree.Steps.push_back(SketchStep("Sketch#1", CircleSketch({ 0.0, 0.0 }, 0.5)));
            Tree.Steps.push_back(ExtrudeStep("Extrude#2", "Sketch#1", 1.0));
            Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ { -3.0, 0.2 }, { 3.0, 0.2 } }, false)));
            Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
            const double Radius = 0.5, Offset = 0.2;
            const double Segment = Radius * Radius * std::acos(Offset / Radius) - Offset * std::sqrt(Radius * Radius - Offset * Offset);
            ReportVolumes(CylinderGate, "Chord y = 0.2", Tree, "Cut#4", "Extrude#2", Segment, std::numbers::pi * Radius * Radius - Segment);
        }

        // ---- K4 cubic spline y = f(x) across the box, directed +x (inside: above it, up to y = ½).
        {
            SketchModel Model;
            std::vector<int> Points;
            for (Vector2 Point : { Vector2{ -2.0, -0.2 }, Vector2{ -0.5, 0.3 }, Vector2{ 0.5, -0.25 }, Vector2{ 2.0, 0.1 } }) Points.push_back(AppendSketchPoint(Model, Point));
            AppendSketchSpline(Model, Points, SketchSplineForm::CubicSpline, 1);
            const SketchSplineCurve Spline = ResolveSketchCurveSpline(Model, Model.Curves[0]).Curve;
            // Reference: ∫ y dx over x ∈ [−1, 1] = ∫ y(τ)·x′(τ) dτ, exact per knot span (degree 5 integrand, 8-point Gauss–Legendre).
            const auto TauAt = [&](double X)
            {
                double Low = Spline.Knots.front(), High = Spline.Knots.back();
                for (int Iteration = 0; Iteration < 200; ++Iteration)
                {
                    const double Middle = 0.5 * (Low + High);
                    (EvaluateSketchSpline(Spline, Middle).Position.X < X ? Low : High) = Middle;
                }
                return 0.5 * (Low + High);
            };
            const double TauLow = TauAt(-1.0), TauHigh = TauAt(1.0);
            std::vector<double> Breaks{ TauLow };
            for (double Knot : Spline.Knots) if (Knot > TauLow && Knot < TauHigh && Knot != Breaks.back()) Breaks.push_back(Knot);
            Breaks.push_back(TauHigh);
            constexpr double Nodes[8]   = { -0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498, 0.1834346424956498, 0.5255324099163290, 0.7966664774136267, 0.9602898564975363 };
            constexpr double Weights[8] = { 0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620, 0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763 };
            double Under = 0.0;
            for (size_t Span = 0; Span + 1 < Breaks.size(); ++Span)
            {
                const double Half = 0.5 * (Breaks[Span + 1] - Breaks[Span]), Middle = 0.5 * (Breaks[Span + 1] + Breaks[Span]);
                for (int Node = 0; Node < 8; ++Node)
                {
                    const SketchSplineDerivatives Sample = EvaluateSketchSpline(Spline, Middle + Half * Nodes[Node]);
                    Under += Weights[Node] * Half * Sample.Position.Y * Sample.First.X;
                }
            }
            const double Inside = 1.0 - Under;                                               // ∫ (½ − f) dx over a width of 2
            FeatureTree Tree = BoxTree();
            Tree.Steps.push_back(SketchStep("Sketch#3", Model));
            Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
            ReportVolumes(SplineGate, "Cubic spline through 4 points", Tree, "Cut#4", "Extrude#2", Inside, 2.0 - Inside);

            CutRun Run;
            FeatureTree Again = BoxTree();
            Again.Steps.push_back(SketchStep("Sketch#3", Model));
            Again.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
            Regenerate(Run, Again);
            ProfileSegment Reference;
            Reference.Kind   = ProfileSegmentKind::Spline;
            Reference.Spline = Spline;
            for (const char* Key : { "Cut#4", "Extrude#2" })
            {
                const RegeneratedBody* Body = FindBody(Run.Outcome, Key);
                if (!Body) { Report(SplineGate, std::string("Trim edges of ") + Key, "missing body · " + Run.Refusals, 1.0, false); continue; }
                const TrimProbe Probe = ProbeTrimEdges(Run.Partition, Body->Body, OrthonormalFrame{}, [&](Vector2 Point) { return SegmentDistance(Reference, Point); }, nullptr);
                Report(SplineGate, std::string("Trim edges of ") + Key, std::to_string(Probe.CutEdges) + " edges · " + std::to_string(Probe.Samples) + " samples · worst " + Scientific(Probe.CurveWorst),
                       Probe.CurveWorst, Probe.CutEdges >= 2 && Probe.CurveWorst <= 1.0e-7);
            }
        }

        // ---- K5 U-shaped chain: up x = −0.4, across y = 0.1, down x = 0.4; the notch is on its right (outside).
        {
            FeatureTree Tree = BoxTree();
            Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ { -0.4, -3.0 }, { -0.4, 0.1 }, { 0.4, 0.1 }, { 0.4, -3.0 } }, false)));
            Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
            ReportVolumes(ChainGate, "U notch 0.8 × 0.6", Tree, "Cut#4", "Extrude#2", 2.0 - 0.48, 0.48);
        }

        // ---- K6 tree semantics: keys, active body, names, Remove, determinism.
        {
            std::string Digests[2];
            for (int Pass = 0; Pass < 2; ++Pass)
            {
                FeatureTree Tree = BoxTree();
                Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ { 0.3, -3.0 }, { 0.3, 3.0 } }, false)));
                Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
                CutRun Run;
                Regenerate(Run, Tree);
                std::string Keys;
                for (const RegeneratedBody& Body : Run.Outcome.Bodies) Keys += (Keys.empty() ? "" : ",") + Body.Key;
                bool NamedCondition = true;
                std::string Names;
                for (const RegeneratedBody& Body : Run.Outcome.Bodies)
                {
                    bool Found = false;
                    for (const auto& [Slot, Name] : Body.Naming.Faces) { Found = Found || Name == "Cut#4.Side(Line1)"; Names += Name + ";"; }
                    NamedCondition = NamedCondition && Found;
                }
                const bool Expected = Run.Refusals.empty() && Keys == "Extrude#2,Cut#4" && Run.Outcome.Active == "Cut#4" && NamedCondition;
                char Volumes[96];
                std::snprintf(Volumes, sizeof Volumes, "%.17g/%.17g", BodyVolume(Run, "Cut#4"), BodyVolume(Run, "Extrude#2"));
                Digests[Pass] = Keys + "|" + Names + "|" + Volumes;
                if (Pass == 0) Report(TreeGate, "Cut keeps the key, adds Cut#4", "bodies " + Keys + " · active " + Run.Outcome.Active + " · Cut#4.Side(Line1) on both pieces " + (NamedCondition ? "yes" : "NO") + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
                                      Expected ? 0.0 : 1.0, Expected);

                if (Pass == 0)
                {
                    FeatureStep Remove;
                    Remove.Kind   = FeatureStepKind::Remove;
                    Remove.Label  = "Remove#5";
                    Remove.Target = "Cut#4";
                    Tree.Steps.push_back(Remove);
                    CutRun After;
                    Regenerate(After, Tree);
                    const bool Removed = After.Refusals.empty() && After.Outcome.Bodies.size() == 1 && After.Outcome.Active == "Extrude#2" && Relative(BodyVolume(After, "Extrude#2"), 0.7) <= 1.0e-9;
                    Report(TreeGate, "Remove#5 deletes Cut#4", std::to_string(After.Outcome.Bodies.size()) + " body · active " + After.Outcome.Active + " · volume " + Fixed(BodyVolume(After, "Extrude#2")) + (After.Refusals.empty() ? "" : " · " + After.Refusals),
                           Removed ? 0.0 : 1.0, Removed);
                }
            }
            const bool Same = Digests[0] == Digests[1];
            Report(TreeGate, "Bit-identical twice", Same ? "keys · names · volumes equal" : "DIFFERENT", Same ? 0.0 : 1.0, Same);
        }

        // ---- K7 refusals.
        struct RefuseCase { const char* Name; std::function<void(FeatureTree&)> Edit; const char* Expected; };
        const std::vector<RefuseCase> Cases = {
            { "Line beside the box", [](FeatureTree& Tree) { Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ { 5.0, -3.0 }, { 5.0, 3.0 } }, false))); Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3")); }, "misses the body" },
            { "Unknown target", [](FeatureTree& Tree) { Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ { 0.3, -3.0 }, { 0.3, 3.0 } }, false))); Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3", "Nope")); }, "no body named Nope" },
            { "Two separate chains", [](FeatureTree& Tree)
              {
                  SketchModel Model = PolylineSketch({ { 0.3, -3.0 }, { 0.3, 3.0 } }, false);
                  AppendSketchLine(Model, AppendSketchPoint(Model, { -0.3, -3.0 }), AppendSketchPoint(Model, { -0.3, 3.0 }), 7);
                  Tree.Steps.push_back(SketchStep("Sketch#3", Model));
                  Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3"));
              }, "do not form one chain" },
            { "Self-crossing chain", [](FeatureTree& Tree) { Tree.Steps.push_back(SketchStep("Sketch#3", PolylineSketch({ { -0.5, -3.0 }, { 0.5, 3.0 }, { 0.5, -1.0 }, { -0.8, 1.0 } }, false))); Tree.Steps.push_back(CutStep("Cut#4", "Sketch#3")); }, "cannot be closed on one side" },
            { "Curve cut without a direction", [](FeatureTree& Tree)
              {
                  Tree.Steps.push_back(CurveStep("Curve#3", { { -2.0, 0.0, 2.0 }, { 0.0, 0.1, 2.0 }, { 2.0, 0.0, 2.0 } }));
                  FeatureStep Cut = CutStep("Cut#4", "");
                  Cut.Sections    = { "Curve#3" };
                  Cut.Plane.ZAxis = { 0.0, 0.0, 0.0 };
                  Tree.Steps.push_back(Cut);
              }, "cut direction is zero" },
            { "Remove an unknown body", [](FeatureTree& Tree) { FeatureStep Remove; Remove.Kind = FeatureStepKind::Remove; Remove.Label = "Remove#3"; Remove.Target = "Ghost"; Tree.Steps.push_back(Remove); }, "Remove: no body named Ghost" },
        };
        for (const RefuseCase& Case : Cases)
        {
            FeatureTree Tree = BoxTree();
            Case.Edit(Tree);
            CutRun Run;
            Regenerate(Run, Tree);
            const bool Named = Run.Refusals.find(Case.Expected) != std::string::npos && Run.Outcome.Bodies.size() == 1;
            Report(RefuseGate, Case.Name, Run.Refusals.empty() ? "(built — expected a refusal)" : Run.Refusals, Named ? 0.0 : 1.0, Named);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                S1–S3 SHEETS
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] std::vector<Vector3> Stations(const std::function<Vector3(double)>& Place, int Count)
    {
        std::vector<Vector3> Points;
        for (int Index = 0; Index < Count; ++Index) Points.push_back(Place(-1.0 + 2.0 * Index / (Count - 1)));
        return Points;
    }

    // Bonnet-like sheet over x ∈ [−1, 1], y ∈ [0, 1]: three parabolic sections lofted (Curve#1–3 → Loft#4).
    void AppendBonnet(FeatureTree& Tree)
    {
        for (int Section = 0; Section < 3; ++Section)
        {
            const double Y = 0.5 * Section;
            Tree.Steps.push_back(CurveStep("Curve#" + std::to_string(Section + 1), Stations([&](double X) { return Vector3{ X, Y, 0.25 * (1.0 - X * X) + 0.08 * Y * Y }; }, 11)));
        }
        Tree.Steps.push_back(LoftStep("Loft#4", { "Curve#1", "Curve#2", "Curve#3" }));
    }

    void ValidateSheets()
    {
        // ---- S1 planar sheet z = 0, [−1, 1] × [0, 1], cut by x = 0.3 (inside: x < 0.3).
        {
            FeatureTree Tree;
            Tree.Steps.push_back(CurveStep("Curve#1", Stations([](double X) { return Vector3{ X, 0.0, 0.0 }; }, 9)));
            Tree.Steps.push_back(CurveStep("Curve#2", Stations([](double X) { return Vector3{ X, 1.0, 0.0 }; }, 9)));
            Tree.Steps.push_back(LoftStep("Loft#3", { "Curve#1", "Curve#2" }));
            Tree.Steps.push_back(SketchStep("Sketch#4", PolylineSketch({ { 0.3, -3.0 }, { 0.3, 3.0 } }, false)));
            Tree.Steps.push_back(CutStep("Cut#5", "Sketch#4"));
            CutRun Run;
            Regenerate(Run, Tree);
            const RegeneratedBody* Inside = FindBody(Run.Outcome, "Cut#5");
            const RegeneratedBody* Outside = FindBody(Run.Outcome, "Loft#3");
            if (!Inside || !Outside) Report(PlanarGate, "Planar sheet cut", "missing piece · " + Run.Refusals, 1.0, false);
            else
            {
                const double InsideArea = TessellatedArea(Run.Partition, Inside->Body), OutsideArea = TessellatedArea(Run.Partition, Outside->Body);
                const double Worst = std::max(std::fabs(InsideArea - 1.3), std::fabs(OutsideArea - 0.7));
                const bool Sheets = Run.Partition.Bodies.Resolve(Inside->Body).Category == BodyCategory::Sheet && Run.Partition.Bodies.Resolve(Outside->Body).Category == BodyCategory::Sheet;
                const int  Faces  = static_cast<int>(Run.Partition.CollectInventory(Inside->Body).Faces.size() + Run.Partition.CollectInventory(Outside->Body).Faces.size());
                const std::string Audits = AuditText(Run.Partition, Inside->Body) + " / " + AuditText(Run.Partition, Outside->Body);
                Report(PlanarGate, "Areas and sheets", "areas " + Fixed(InsideArea) + " / " + Fixed(OutsideArea) + " · sheets " + (Sheets ? "yes" : "NO") + " · faces " + std::to_string(Faces) + " · laminar "
                       + std::to_string(LaminarEdges(Run.Partition, Inside->Body)) + " + " + std::to_string(LaminarEdges(Run.Partition, Outside->Body)) + " · " + Audits,
                       Worst, Worst <= 1.0e-9 && Sheets && Faces == 2 && Audits == "sound / sound");
                ProfileSegment Line;
                Line.Start = { 0.3, -3.0 };
                Line.End   = { 0.3, 3.0 };
                const TrimProbe Probe = ProbeTrimEdges(Run.Partition, Inside->Body, OrthonormalFrame{}, [&](Vector2 Point) { return SegmentDistance(Line, Point); }, nullptr);
                Report(PlanarGate, "Trim edge on x = 0.3", std::to_string(Probe.CutEdges) + " edge · worst " + Scientific(Probe.CurveWorst), Probe.CurveWorst, Probe.CutEdges == 1 && Probe.CurveWorst <= 1.0e-9);
            }
        }

        // ---- Reference bonnet (uncut) area.
        double BonnetArea = 0.0;
        {
            FeatureTree Tree;
            AppendBonnet(Tree);
            CutRun Run;
            Regenerate(Run, Tree);
            if (const RegeneratedBody* Body = FindBody(Run.Outcome, "Loft#4")) BonnetArea = TessellatedArea(Run.Partition, Body->Body);
        }

        // ---- S2 character line seen from above: a wavy SpaceCurve at z = 1 projected along −z.
        {
            FeatureTree Tree;
            AppendBonnet(Tree);
            Tree.Steps.push_back(CurveStep("Character#5", Stations([](double X) { return Vector3{ 1.6 * X, 0.5 + 0.18 * std::sin(2.2 * X), 1.0 }; }, 13)));
            FeatureStep Cut = CutStep("Cut#6", "");
            Cut.Sections    = { "Character#5" };
            Cut.Plane.ZAxis = { 0.0, 0.0, -1.0 };
            Tree.Steps.push_back(Cut);
            CutRun Run;
            Regenerate(Run, Tree);
            const RegeneratedBody* Inside = FindBody(Run.Outcome, "Cut#6");
            const RegeneratedBody* Outside = FindBody(Run.Outcome, "Loft#4");
            if (!Inside || !Outside || Run.Outcome.Panels.empty() || !Run.Outcome.Curves.count("Character#5")) Report(CurvedGate, "Bonnet cut by the character line", "missing piece · " + Run.Refusals, 1.0, false);
            else
            {
                // Projection exactness: projected poles vs projected curve points.
                OrthonormalFrame Plane;
                Plane.ZAxis = { 0.0, 0.0, -1.0 };
                Plane.XAxis = { 1.0, 0.0, 0.0 };
                Plane.YAxis = Cross(Plane.ZAxis, Plane.XAxis);
                const NurbsCurve& Character = Run.Outcome.Curves.at("Character#5").Curve;
                const std::optional<ProfileSegment> Projected = ProjectCutCurve(Character, Plane, "Character#5");
                double ProjectionWorst = Projected ? 0.0 : 1.0;
                for (int Index = 0; Projected && Index <= 100; ++Index)
                {
                    const double Tau = Index / 100.0;
                    const Vector2 Direct = PlaneCoordinates(Plane, EvaluateCurve(CurveGeometry{ Character }, Tau).Position);
                    const double  Knot   = Projected->Spline.Knots.front() + (Projected->Spline.Knots.back() - Projected->Spline.Knots.front()) * Tau;
                    ProjectionWorst = std::max(ProjectionWorst, Distance(EvaluateSketchSpline(Projected->Spline, Knot).Position, Direct));
                }
                Report(CurvedGate, "Projected poles = projected curve", "101 samples · worst " + Scientific(ProjectionWorst), ProjectionWorst, ProjectionWorst <= 1.0e-12);

                const SurfaceGeometry Panel = Run.Outcome.Panels.begin()->second;
                for (const RegeneratedBody* Body : { Inside, Outside })
                {
                    const TrimProbe Probe = ProbeTrimEdges(Run.Partition, Body->Body, Plane, [&](Vector2 Point) { return SegmentDistance(*Projected, Point); }, &Panel);
                    const double Worst = std::max(Probe.CurveWorst, Probe.SurfaceWorst);
                    Report(CurvedGate, "Trim edges of " + Body->Key, std::to_string(Probe.CutEdges) + " edges · " + std::to_string(Probe.Samples) + " samples · on the curve " + Scientific(Probe.CurveWorst) + " · on the panel " + Scientific(Probe.SurfaceWorst)
                           + " · " + AuditText(Run.Partition, Body->Body), Worst, Probe.CutEdges >= 1 && Worst <= 1.0e-7 && AuditText(Run.Partition, Body->Body) == "sound");
                }
                const double InsideArea = TessellatedArea(Run.Partition, Inside->Body), OutsideArea = TessellatedArea(Run.Partition, Outside->Body);
                const double Kept = Relative(InsideArea + OutsideArea, BonnetArea);
                Report(CurvedGate, "Area kept", Fixed(InsideArea) + " + " + Fixed(OutsideArea) + " vs " + Fixed(BonnetArea) + " · rel " + Scientific(Kept), Kept, Kept <= 5.0e-6 && InsideArea > 0.1 && OutsideArea > 0.1);
            }
        }

        // ---- S3 closed circle seen from above (sketch on z = 0 cuts through the whole sheet height).
        {
            FeatureTree Tree;
            AppendBonnet(Tree);
            Tree.Steps.push_back(SketchStep("Sketch#5", CircleSketch({ 0.1, 0.45 }, 0.25)));
            Tree.Steps.push_back(CutStep("Cut#6", "Sketch#5"));
            CutRun Run;
            Regenerate(Run, Tree);
            const RegeneratedBody* Inside = FindBody(Run.Outcome, "Cut#6");
            const RegeneratedBody* Outside = FindBody(Run.Outcome, "Loft#4");
            if (!Inside || !Outside) Report(HoleGate, "Bonnet cut by a circle", "missing piece · " + Run.Refusals, 1.0, false);
            else
            {
                const auto CircleDistance = [](Vector2 Point) { return std::fabs(Distance(Point, Vector2{ 0.1, 0.45 }) - 0.25); };
                const SurfaceGeometry Panel = Run.Outcome.Panels.begin()->second;
                for (const RegeneratedBody* Body : { Inside, Outside })
                {
                    const TrimProbe Probe = ProbeTrimEdges(Run.Partition, Body->Body, OrthonormalFrame{}, CircleDistance, &Panel);
                    const double Worst = std::max(Probe.CurveWorst, Probe.SurfaceWorst);
                    const int Loops = [&]
                    {
                        int Count = 0;
                        for (FaceToken Face : Run.Partition.CollectInventory(Body->Body).Faces) Count += static_cast<int>(Run.Partition.Faces.Resolve(Face).Loops.size());
                        return Count;
                    }();
                    Report(HoleGate, "Trim edges of " + Body->Key, std::to_string(Probe.CutEdges) + " edges · loops " + std::to_string(Loops) + " · on the circle " + Scientific(Probe.CurveWorst) + " · on the panel " + Scientific(Probe.SurfaceWorst)
                           + " · " + AuditText(Run.Partition, Body->Body), Worst, Probe.CutEdges >= 1 && Worst <= 1.0e-7 && AuditText(Run.Partition, Body->Body) == "sound" && Loops == (Body == Inside ? 1 : 2));
                }
                const double InsideArea = TessellatedArea(Run.Partition, Inside->Body), OutsideArea = TessellatedArea(Run.Partition, Outside->Body);
                const double Kept = Relative(InsideArea + OutsideArea, BonnetArea);
                Report(HoleGate, "Area kept", Fixed(InsideArea) + " + " + Fixed(OutsideArea) + " vs " + Fixed(BonnetArea) + " · rel " + Scientific(Kept), Kept, Kept <= 5.0e-6 && InsideArea > 0.1);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    const std::string Only = Count > 2 && std::string(Arguments[1]) == "--only" ? Arguments[2] : "";
    if (Only.empty() || Only == "solids") ValidateSolids();
    if (Only.empty() || Only == "sheets") ValidateSheets();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !Only.empty());
    std::printf("# Body cut validation\n\n%s Trim / split T1–T2: Cut (Plasticity C) splits solids and sheets by sketch profiles and projected curves; Remove deletes a body\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
