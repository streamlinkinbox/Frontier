/*====================================================================================================================================
                                                 SKETCHCONSTRAININGVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Constraints C1 + L1: SolveSpace verbs → constraints, dimension measuring, diagnosis, pinned point drag; spline curve
//    constraints (G1 / G2 / on / end direction), reference (driven) dimensions, geometry linked from other sketches

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Feature/FeatureTree.h"
#include "Engine/GeometryProcessing/Sketch/SketchCatalogue.h"
#include "Engine/GeometryProcessing/Sketch/SketchConstraining.h"
#include "Engine/GeometryProcessing/Sketch/SketchSolver.h"
#include "Engine/GeometryProcessing/Sketch/SketchSpline.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double Pi = std::numbers::pi;
    using Verb = SketchConstraintVerb;
    using Kind = SketchConstraintKind;

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - S1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally VerbGate     = { "S1", "Verb table: D N H V O Q M Y L [ ] on lines, points, circles, arcs → the expected kind, references in order, measured value vs closed form; wrong picks refused by name", "kind exact · value ≤ 10⁻¹²" };
    GateTally MeasureGate  = { "S2", "Measuring: length 5 of a 3-4-5 side, distance, signed horizontal / vertical distances, arc radius, angles +30° / −120° / 180°", "≤ 10⁻¹²" };
    GateTally DegreeGate   = { "S3", "Drawn rectangle (H / V, 4 DOF) → ] corner, D width typed 3, D height typed 2: DOF 4 → 2 → 1 → 0, corners exact; a second H is redundant (partner H1), D on the far side 3 redundant / 4 conflicting", "≤ 10⁻¹²" };
    GateTally DragGate     = { "S4", "Drag: free rectangle corner pinned at (4, 3) with the rest following minimally (P0 still), H / V held; the fully constrained one does not move; a point on a fixed circle lands on it nearest the target", "≤ 10⁻⁹" };
    GateTally NameGate     = { "S5", "Names and descriptions: Line1.Start, Circle5.Centre, Arc6.End, Spline7.Point1, Point11; \"Length 3 · Line1\", \"Angle 90° · Line1 · Line2\"", "exact text" };
    GateTally SolveGate    = { "S6", "Solved verbs: tangent line–circle (distance = r), angle typed 30°, equal radius, midpoint, symmetric, concentric, perpendicular", "≤ 10⁻¹²" };

    GateTally SplineVerbGate = { "S7", "Spline verbs: O point → on spline, H / V spline + end → end direction, L / C spline + line / arc / spline meeting at an end (shared or coincident) → G1 / G2; defining point, interior point, apart, closed, cubic-spline G2 and D refused by name", "kind exact" };
    GateTally SplineSolveGate = { "S8", "Spline solves: G1 to a fixed line in all 4 forms, G2 Hermite → NURBS, G2 NURBS → arc (κ = 1 / r), G2 Bezier → line (κ = 0), point on spline, H / V end directions; DOF 6 → 5 (G1) → 4 (G2); G1 + G2 on one joint → redundant (partner named)", "sin, |Δκ|·L, distance ≤ 10⁻¹²" };
    GateTally DrivenGate = { "S9", "Reference (driven) dimensions: on a fully constrained rectangle the diagonal reads √13, then 5 after the height becomes 4; status Driven, DOF unchanged, positions bit-equal with and without it; reference angle; text \"(reference)\"", "≤ 10⁻¹²" };
    GateTally LinkGate = { "S10", "Linked geometry: a projected point is held bit-exact (2 unknowns out), a line end coincident with it lands on it, Fixed on it redundant / conflicting, a linked circle keeps its radius, drag refused; feature tree: Sketch#2 (z = 1) follows Sketch#1 when Width 3 → 4, XZ sketch gets (x, z) = (4, 0), dangling link noted and held", "≤ 10⁻¹²" };

    std::vector<GateTally*> AllGates() { return { &VerbGate, &MeasureGate, &DegreeGate, &DragGate, &NameGate, &SolveGate, &SplineVerbGate, &SplineSolveGate, &DrivenGate, &LinkGate }; }

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
        Gate.Worst = std::max(Gate.Worst, std::isfinite(Value) ? Value : 1.0);
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

    [[nodiscard]] SketchPick PointPick(int Index) { return { SketchPickKind::Point, Index }; }
    [[nodiscard]] SketchPick CurvePick(int Index) { return { SketchPickKind::Curve, Index }; }

    // 📝 Test sketch: rectangle 3 × 2 at the origin (points 0–3, Line1–4 = curves 0–3, H1 V1 H2 V2), circle r 0.5 at
    //    (5, 1) (point 4, Circle5 = curve 4), arc r 1 about (5, 4) from (6, 4) to (4, 4) (points 5–7, Arc6 = curve 5),
    //    spline through 3 points (8–10, Spline7 = curve 6), loose point 11 at (1.5, −1).
    [[nodiscard]] SketchModel BuildTestSketch()
    {
        SketchModel Sketch;
        AppendSketchRectangle(Sketch, { 0.0, 0.0 }, { 3.0, 2.0 }, 1);
        AppendSketchCircle(Sketch, AppendSketchPoint(Sketch, { 5.0, 1.0 }), 0.5, 5);
        const int Centre = AppendSketchPoint(Sketch, { 5.0, 4.0 });
        const int Start  = AppendSketchPoint(Sketch, { 6.0, 4.0 });
        const int End    = AppendSketchPoint(Sketch, { 4.0, 4.0 });
        AppendSketchArc(Sketch, Centre, Start, End, 6);
        std::vector<int> Spline;
        for (const Vector2 Point : { Vector2{ 0.0, 5.0 }, Vector2{ 1.0, 6.0 }, Vector2{ 2.0, 5.0 } }) Spline.push_back(AppendSketchPoint(Sketch, Point));
        AppendSketchSpline(Sketch, Spline, SketchSplineForm::CubicSpline, 7);
        AppendSketchPoint(Sketch, { 1.5, -1.0 });
        return Sketch;
    }

    [[nodiscard]] double MaxResidual(const SketchModel& Sketch)
    {
        SketchModel Copy = Sketch;
        return SolveSketch(Copy).MaxResidual;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S1 VERB TABLE
    //------------------------------------------------------------------------------------------------------------------------

    struct VerbCase
    {
        const char*             Name     = "";
        Verb                    Chosen   = Verb::Dimension;
        std::vector<SketchPick> Picks    = {};
        Kind                    Expected = Kind::Coincident;
        int                     First    = -1, Second = -1, Third = -1;
        double                  Value    = 0.0;
        double                  SecondValue = 0.0;
        const char*             Refusal  = nullptr;                                          // Expected refusal fragment (nullptr: accepted)
    };

    void ValidateVerbs()
    {
        const SketchModel Sketch = BuildTestSketch();
        const std::vector<VerbCase> Cases = {
            { "D line → length", Verb::Dimension, { CurvePick(0) }, Kind::Length, 0, -1, -1, 3.0 },
            { "D 2 points → distance", Verb::Dimension, { PointPick(0), PointPick(2) }, Kind::Distance, 0, 2, -1, std::sqrt(13.0) },
            { "D circle → radius", Verb::Dimension, { CurvePick(4) }, Kind::Radius, 4, -1, -1, 0.5 },
            { "D arc → radius", Verb::Dimension, { CurvePick(5) }, Kind::Radius, 5, -1, -1, 1.0 },
            { "D point + line refused", Verb::Dimension, { PointPick(11), CurvePick(0) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "point-to-line distance" },
            { "D 2 lines refused", Verb::Dimension, { CurvePick(0), CurvePick(1) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "takes one line" },
            { "N 2 lines → angle +90°", Verb::Angle, { CurvePick(0), CurvePick(1) }, Kind::Angle, 0, 1, -1, Pi / 2.0 },
            { "N reversed → angle −90°", Verb::Angle, { CurvePick(1), CurvePick(0) }, Kind::Angle, 1, 0, -1, -Pi / 2.0 },
            { "H line", Verb::Horizontal, { CurvePick(0) }, Kind::Horizontal, 0 },
            { "H 2 points → level", Verb::Horizontal, { PointPick(0), PointPick(11) }, Kind::VerticalDistance, 0, 11, -1, 0.0 },
            { "V 2 points → plumb", Verb::Vertical, { PointPick(11), PointPick(0) }, Kind::HorizontalDistance, 11, 0, -1, 0.0 },
            { "V circle refused", Verb::Vertical, { CurvePick(4) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "takes one line or two points" },
            { "O 2 points → coincident", Verb::On, { PointPick(0), PointPick(11) }, Kind::Coincident, 0, 11 },
            { "O point + line (any order)", Verb::On, { CurvePick(0), PointPick(11) }, Kind::PointOnLine, 11, 0 },
            { "O endpoint + its line refused", Verb::On, { PointPick(0), CurvePick(0) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "already ends Line1" },
            { "O point + circle", Verb::On, { PointPick(11), CurvePick(4) }, Kind::PointOnCircle, 11, 4 },
            { "O 2 circles → concentric", Verb::On, { CurvePick(4), CurvePick(5) }, Kind::Concentric, 4, 5 },
            { "Q 2 lines", Verb::Equal, { CurvePick(0), CurvePick(2) }, Kind::EqualLength, 0, 2 },
            { "Q circle + arc", Verb::Equal, { CurvePick(5), CurvePick(4) }, Kind::EqualRadius, 5, 4 },
            { "Q line + circle refused", Verb::Equal, { CurvePick(0), CurvePick(4) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "two lines or two circles" },
            { "M point + line", Verb::Midpoint, { PointPick(11), CurvePick(0) }, Kind::Midpoint, 11, 0 },
            { "Y 2 points + line", Verb::Symmetric, { PointPick(0), CurvePick(1), PointPick(11) }, Kind::Symmetric, 0, 11, 1 },
            { "L 2 lines → parallel", Verb::Parallel, { CurvePick(0), CurvePick(2) }, Kind::Parallel, 0, 2 },
            { "L circle + line → tangent (line first)", Verb::Parallel, { CurvePick(4), CurvePick(0) }, Kind::Tangent, 0, 4, -1, 0.0, 0.0 },
            { "L 2 circles apart → external tangent", Verb::Parallel, { CurvePick(4), CurvePick(5) }, Kind::Tangent, 4, 5, -1, 0.0, 0.0 },
            { "[ 2 lines → perpendicular", Verb::Perpendicular, { CurvePick(0), CurvePick(1) }, Kind::Perpendicular, 0, 1 },
            { "] point → fixed where it is", Verb::Lock, { PointPick(11) }, Kind::Fixed, 11, -1, -1, 1.5, -1.0 },
            { "spline curve refused", Verb::Equal, { CurvePick(6) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "Spline7 is a spline" },
            { "nothing picked refused", Verb::Lock, {}, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "pick points or curves" },
            { "same point twice refused", Verb::On, { PointPick(3), PointPick(3) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "same point is picked twice" },
            { "point out of range refused", Verb::Lock, { PointPick(40) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "not in the sketch" },
        };
        for (const VerbCase& Case : Cases)
        {
            const SketchConstraintProposal Proposal = ProposeSketchConstraint(Sketch, Case.Picks, Case.Chosen);
            if (Case.Refusal)
            {
                const bool Passed = Proposal.Refusal.find(Case.Refusal) != std::string::npos;
                Report(VerbGate, Case.Name, "refusal: " + Proposal.Refusal, 0.0, Passed);
                continue;
            }
            const SketchConstraint& Made = Proposal.Constraint;
            const double Gap = std::max(std::abs(Made.Value - Case.Value), std::abs(Made.SecondValue - Case.SecondValue));
            const bool Passed = Proposal.Refusal.empty() && Made.Kind == Case.Expected && Made.First == Case.First && Made.Second == Case.Second && Made.Third == Case.Third && Gap <= 1.0e-12;
            Report(VerbGate, Case.Name, DescribeSketchConstraint(Sketch, Made) + (Proposal.Refusal.empty() ? "" : " · refused: " + Proposal.Refusal) + " · |Δvalue| " + Scientific(Gap), Gap, Passed);
        }
        // An arc inside a circle: L picks the internal tangent (SecondValue −1).
        SketchModel Nested;
        AppendSketchCircle(Nested, AppendSketchPoint(Nested, { 0.0, 0.0 }), 2.0, 1);
        AppendSketchCircle(Nested, AppendSketchPoint(Nested, { 0.5, 0.0 }), 0.5, 2);
        const SketchConstraintProposal Inside = ProposeSketchConstraint(Nested, { CurvePick(0), CurvePick(1) }, Verb::Parallel);
        Report(VerbGate, "L nested circles → internal tangent", DescribeSketchConstraint(Nested, Inside.Constraint), 0.0, Inside.Constraint.Kind == Kind::Tangent && Inside.Constraint.SecondValue < 0.0);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S2 MEASURING
    //------------------------------------------------------------------------------------------------------------------------

    void ValidateMeasuring()
    {
        SketchModel Sketch;
        const int A = AppendSketchPoint(Sketch, { 1.0, 1.0 });
        const int B = AppendSketchPoint(Sketch, { 4.0, 5.0 });
        const int C = AppendSketchPoint(Sketch, { 1.0 + std::cos(Pi / 6.0), 1.0 + std::sin(Pi / 6.0) });
        const int D = AppendSketchPoint(Sketch, { 1.0 + std::cos(-Pi / 2.0), 1.0 + std::sin(-Pi / 2.0) });
        const int Base  = AppendSketchLine(Sketch, A, AppendSketchPoint(Sketch, { 2.0, 1.0 }), 1);
        const int Hyp   = AppendSketchLine(Sketch, A, B, 2);
        const int Slant = AppendSketchLine(Sketch, A, C, 3);
        const int Down  = AppendSketchLine(Sketch, A, D, 4);
        const int Back  = AppendSketchLine(Sketch, AppendSketchPoint(Sketch, { 2.0, 1.0 }), A, 5);
        const int ArcCentre = AppendSketchPoint(Sketch, { -3.0, 0.0 });
        const int Arc = AppendSketchArc(Sketch, ArcCentre, AppendSketchPoint(Sketch, { -3.0 + 0.6, 0.8 }), AppendSketchPoint(Sketch, { -4.0, 0.0 }), 6);
        struct Measure { const char* Name; SketchConstraint Constraint; double Expected; };
        const auto Make = [](Kind Constraint, int First, int Second = -1) { SketchConstraint Made; Made.Kind = Constraint; Made.First = First; Made.Second = Second; return Made; };
        const std::vector<Measure> Measures = {
            { "length 3-4-5 side", Make(Kind::Length, Hyp), 5.0 },
            { "distance A → B", Make(Kind::Distance, A, B), 5.0 },
            { "horizontal distance B − A", Make(Kind::HorizontalDistance, A, B), 3.0 },
            { "vertical distance A − B (signed)", Make(Kind::VerticalDistance, B, A), -4.0 },
            { "arc radius |CS|", Make(Kind::Radius, Arc), 1.0 },
            { "angle base → slant +30°", Make(Kind::Angle, Base, Slant), Pi / 6.0 },
            { "angle slant → down −120°", Make(Kind::Angle, Slant, Down), -2.0 * Pi / 3.0 },
            { "angle base → reversed base 180°", Make(Kind::Angle, Base, Back), Pi },
            { "non-dimension kind → 0", Make(Kind::Parallel, Base, Slant), 0.0 },
        };
        for (const Measure& Entry : Measures)
        {
            const double Value = MeasureSketchConstraint(Sketch, Entry.Constraint);
            const double Gap   = std::abs(Value - Entry.Expected);
            Report(MeasureGate, Entry.Name, "measured " + Scientific(Value) + " vs " + Scientific(Entry.Expected), Gap, Gap <= 1.0e-12);
        }
        Report(MeasureGate, "dimension kinds", "Distance, H / V distance, Length, Angle, Radius carry values; Fixed, Parallel do not", 0.0,
               SketchDimensionCondition(Kind::Distance) && SketchDimensionCondition(Kind::Angle) && SketchDimensionCondition(Kind::Radius) && !SketchDimensionCondition(Kind::Fixed) &&
               !SketchDimensionCondition(Kind::Parallel));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S3 DEGREES OF FREEDOM
    //------------------------------------------------------------------------------------------------------------------------

    void ValidateDegrees()
    {
        // A rectangle as the workspace draws it (H / V on its sides), with a sloppy placement.
        SketchModel Sketch;
        AppendSketchRectangle(Sketch, { 0.3, 0.2 }, { 2.1, 1.4 }, 1);
        SketchModel Start = Sketch;
        const int StartDegrees = SolveSketch(Start).DegreesOfFreedom;
        const auto Add = [&](Verb Chosen, std::vector<SketchPick> Picks, double Typed, bool TypedCondition) -> SketchSolveOutcome
        {
            SketchConstraintProposal Proposal = ProposeSketchConstraint(Sketch, Picks, Chosen);
            if (TypedCondition) Proposal.Constraint.Value = Typed;
            Sketch.Constraints.push_back(Proposal.Constraint);
            return SolveSketch(Sketch);
        };
        const SketchSolveOutcome Locked = Add(Verb::Lock, { PointPick(0) }, 0.0, false);
        const SketchSolveOutcome Wide   = Add(Verb::Dimension, { CurvePick(0) }, 3.0, true);
        const SketchSolveOutcome Tall   = Add(Verb::Dimension, { CurvePick(1) }, 2.0, true);
        Report(DegreeGate, "DOF after each verb", "start " + std::to_string(StartDegrees) + " → ] " + std::to_string(Locked.DegreesOfFreedom) + " → D 3 " + std::to_string(Wide.DegreesOfFreedom) +
               " → D 2 " + std::to_string(Tall.DegreesOfFreedom), 0.0,
               StartDegrees == 4 && Locked.DegreesOfFreedom == 2 && Wide.DegreesOfFreedom == 1 && Tall.DegreesOfFreedom == 0 && Tall.Converged && Tall.Redundant.empty());
        const Vector2 Expected[4] = { { 0.3, 0.2 }, { 3.3, 0.2 }, { 3.3, 2.2 }, { 0.3, 2.2 } };
        double Worst = 0.0;
        for (int Corner = 0; Corner < 4; ++Corner) Worst = std::max(Worst, Distance(Sketch.Points[Corner].Position, Expected[Corner]));
        Report(DegreeGate, "corners after D 3 / D 2", "(0.3, 0.2) (3.3, 0.2) (3.3, 2.2) (0.3, 2.2) · worst " + Scientific(Worst), Worst, Worst <= 1.0e-12);
        bool AllFixed = true;
        for (int Corner = 0; Corner < 4; ++Corner) AllFixed = AllFixed && Tall.PointFixed[Corner];
        Report(DegreeGate, "every corner fully constrained", "PointFixed ×4, CurveFixed ×4", 0.0, AllFixed && std::all_of(Tall.CurveFixed.begin(), Tall.CurveFixed.end(), [](bool Fixed) { return Fixed; }));

        // A second H on Line1: redundant, its partner is H1 (index 0).
        SketchModel Again = Sketch;
        Again.Constraints.push_back(ProposeSketchConstraint(Again, { CurvePick(0) }, Verb::Horizontal).Constraint);
        const SketchSolveOutcome Twice = SolveSketch(Again);
        const SketchConstraintReport& Extra = Twice.Constraints.back();
        const bool PartnerCondition = std::find(Extra.Partners.begin(), Extra.Partners.end(), 0) != Extra.Partners.end();
        Report(DegreeGate, "second H redundant", std::string(SketchConstraintStatusLabel(Extra.Status)) + " · partners include H1: " + (PartnerCondition ? "yes" : "no"), 0.0,
               Extra.Status == SketchConstraintStatus::Redundant && PartnerCondition && Twice.ConflictGroups.empty());

        // D on the opposite side: 3 agrees (redundant), 4 contradicts (conflicting group).
        for (const double Value : { 3.0, 4.0 })
        {
            SketchModel Far = Sketch;
            SketchConstraintProposal Proposal = ProposeSketchConstraint(Far, { CurvePick(2) }, Verb::Dimension);
            Proposal.Constraint.Value = Value;
            Far.Constraints.push_back(Proposal.Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Far);
            const SketchConstraintStatus Status = Outcome.Constraints.back().Status;
            const bool Passed = Value == 3.0 ? Status == SketchConstraintStatus::Redundant && Outcome.ConflictGroups.empty()
                                             : Status == SketchConstraintStatus::Conflicting && !Outcome.ConflictGroups.empty();
            Report(DegreeGate, Value == 3.0 ? "D Line3 = 3 redundant" : "D Line3 = 4 conflicting", std::string(SketchConstraintStatusLabel(Status)) + " · " + std::to_string(Outcome.ConflictGroups.size()) + " conflict group(s)", 0.0, Passed);
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S4 DRAG
    //------------------------------------------------------------------------------------------------------------------------

    void ValidateDrag()
    {
        SketchModel Free;
        AppendSketchRectangle(Free, { 0.0, 0.0 }, { 3.0, 2.0 }, 1);
        const SketchDragOutcome Pulled = DragSketchPoint(Free, 2, { 4.0, 3.0 });
        const Vector2 Expected[4] = { { 0.0, 0.0 }, { 4.0, 0.0 }, { 4.0, 3.0 }, { 0.0, 3.0 } };
        double Worst = 0.0;
        for (int Corner = 0; Corner < 4; ++Corner) Worst = std::max(Worst, Distance(Pulled.Sketch.Points[Corner].Position, Expected[Corner]));
        Report(DragGate, "free corner pinned, rest follows minimally", "P0 (0, 0) P1 (4, 0) P2 (4, 3) P3 (0, 3) · worst " + Scientific(Worst) + " · DOF " + std::to_string(Pulled.Solve.DegreesOfFreedom),
               Worst, Pulled.Refusal.empty() && Pulled.PinnedCondition && Worst <= 1.0e-9 && Pulled.Solve.DegreesOfFreedom == 4 && Pulled.Solve.MaxResidual <= 1.0e-12);

        SketchModel Held = Free;
        Held.Constraints.push_back(ProposeSketchConstraint(Held, { PointPick(0) }, Verb::Lock).Constraint);
        Held.Constraints.push_back(ProposeSketchConstraint(Held, { CurvePick(0) }, Verb::Dimension).Constraint);
        Held.Constraints.push_back(ProposeSketchConstraint(Held, { CurvePick(1) }, Verb::Dimension).Constraint);
        const SketchDragOutcome Stuck = DragSketchPoint(Held, 2, { 5.0, 5.0 });
        Report(DragGate, "fully constrained corner stays", "pinned " + std::string(Stuck.PinnedCondition ? "yes" : "no") + " · travel " + Scientific(Stuck.Travel), Stuck.Travel,
               Stuck.Refusal.empty() && !Stuck.PinnedCondition && Stuck.Travel <= 1.0e-9);

        // A point held on a circle of fixed centre and radius: it slides on the circle toward the target.
        SketchModel Rim;
        const int Centre = AppendSketchPoint(Rim, { 0.0, 0.0 });
        const int Circle = AppendSketchCircle(Rim, Centre, 2.0, 1);
        const int Bead   = AppendSketchPoint(Rim, { 0.0, 2.0 });
        Rim.Constraints.push_back(ProposeSketchConstraint(Rim, { PointPick(Centre) }, Verb::Lock).Constraint);
        Rim.Constraints.push_back(ProposeSketchConstraint(Rim, { CurvePick(Circle) }, Verb::Dimension).Constraint);
        Rim.Constraints.push_back(ProposeSketchConstraint(Rim, { PointPick(Bead), CurvePick(Circle) }, Verb::On).Constraint);
        const SketchDragOutcome Slid = DragSketchPoint(Rim, Bead, { 3.0, 0.0 });
        const double OffCircle = std::abs(Length(Slid.Sketch.Points[Bead].Position) - 2.0);
        const double Aim       = Distance(Slid.Sketch.Points[Bead].Position, { 2.0, 0.0 });
        Report(DragGate, "bead slides onto the circle", "|P| − 2 = " + Scientific(OffCircle) + " · |P − (2, 0)| = " + Scientific(Aim) + " · pinned " + (Slid.PinnedCondition ? "yes" : "no"),
               std::max(OffCircle, Aim), Slid.Refusal.empty() && !Slid.PinnedCondition && OffCircle <= 1.0e-12 && Aim <= 1.0e-9);
        const SketchDragOutcome Lost = DragSketchPoint(Rim, 99, { 0.0, 0.0 });
        Report(DragGate, "point out of range refused", Lost.Refusal, 0.0, !Lost.Refusal.empty());
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S5 NAMES
    //------------------------------------------------------------------------------------------------------------------------

    void ValidateNames()
    {
        const SketchModel Sketch = BuildTestSketch();
        struct Named { SketchPick Pick; const char* Expected; };
        const std::vector<Named> Names = {
            { PointPick(0), "Line1.Start" }, { PointPick(1), "Line1.End" }, { PointPick(4), "Circle5.Centre" }, { PointPick(5), "Arc6.Centre" },
            { PointPick(7), "Arc6.End" }, { PointPick(9), "Spline7.Point1" }, { PointPick(11), "Point11" }, { CurvePick(5), "Arc6" },
        };
        for (const Named& Entry : Names)
        {
            const std::string Name = NameSketchPick(Sketch, Entry.Pick);
            Report(NameGate, Entry.Expected, Name, 0.0, Name == Entry.Expected);
        }
        struct Described { SketchConstraint Constraint; const char* Expected; };
        const auto Proposal = [&](Verb Chosen, std::vector<SketchPick> Picks) { return ProposeSketchConstraint(Sketch, Picks, Chosen).Constraint; };
        const std::vector<Described> Descriptions = {
            { Sketch.Constraints[0], "Horizontal · Line1" },
            { Proposal(Verb::Dimension, { CurvePick(0) }), "Length 3 · Line1" },
            { Proposal(Verb::Angle, { CurvePick(0), CurvePick(1) }), "Angle 90° · Line1 · Line2" },
            { Proposal(Verb::On, { PointPick(11), CurvePick(4) }), "Point on circle · Point11 · Circle5" },
            { Proposal(Verb::Lock, { PointPick(11) }), "Fixed (1.5, -1) · Point11" },
            { Proposal(Verb::Symmetric, { PointPick(0), PointPick(11), CurvePick(1) }), "Symmetric · Line1.Start · Point11 · Line2" },
        };
        for (const Described& Entry : Descriptions)
        {
            const std::string Text = DescribeSketchConstraint(Sketch, Entry.Constraint);
            Report(NameGate, Entry.Expected, Text, 0.0, Text == Entry.Expected);
        }
        std::string Keys;
        for (int Index = 0; Index <= static_cast<int>(Verb::Lock); ++Index) Keys += SketchConstraintVerbKey(static_cast<Verb>(Index));
        Report(NameGate, "SolveSpace keys", Keys, 0.0, Keys == "DNHVOQMYL[]");
    }


    //------------------------------------------------------------------------------------------------------------------------
    //                                                    SPLINE HELPERS
    //------------------------------------------------------------------------------------------------------------------------

    // Independent end frame of a spline: C′ and C″ at its first (StartCondition) or last knot.
    struct EndFrame
    {
        Vector2 Tangent = {};                                                                // [len] - C′
        Vector2 Bending = {};                                                                // [len] - C″
    };

    [[nodiscard]] EndFrame MeasureEnd(const SketchModel& Sketch, int Curve, bool StartCondition)
    {
        const SketchSplineOutcome Resolved = ResolveSketchCurveSpline(Sketch, Sketch.Curves[Curve]);
        const double Tau = StartCondition ? Resolved.Curve.Knots.front() : Resolved.Curve.Knots.back();
        const SketchSplineDerivatives At = EvaluateSketchSpline(Resolved.Curve, Tau);
        return { At.First, At.Second };
    }

    [[nodiscard]] double SignedCurvature(const EndFrame& End) { return Cross(End.Tangent, End.Bending) / std::pow(Length(End.Tangent), 3.0); }
    [[nodiscard]] double Sine(Vector2 First, Vector2 Second) { return std::abs(Cross(First, Second)) / (Length(First) * Length(Second)); }

    // Distance from a point to a spline by dense sampling and Newton (independent of the solver's foot point).
    [[nodiscard]] double SplineGap(const SketchModel& Sketch, int Curve, Vector2 Target)
    {
        const SketchSplineOutcome Resolved = ResolveSketchCurveSpline(Sketch, Sketch.Curves[Curve]);
        const double Low = Resolved.Curve.Knots.front(), High = Resolved.Curve.Knots.back();
        double Best = Low, BestGap = 1e300;
        for (int Sample = 0; Sample <= 4000; ++Sample)
        {
            const double Tau = Low + (High - Low) * Sample / 4000.0;
            const double Gap = Distance(EvaluateSketchSpline(Resolved.Curve, Tau).Position, Target);
            if (Gap < BestGap) { BestGap = Gap; Best = Tau; }
        }
        for (int Iteration = 0; Iteration < 60; ++Iteration)
        {
            const SketchSplineDerivatives At = EvaluateSketchSpline(Resolved.Curve, Best);
            const Vector2 Offset = At.Position - Target;
            Best = std::clamp(Best - Dot(Offset, At.First) / (Dot(At.First, At.First) + Dot(Offset, At.Second)), Low, High);
        }
        return Distance(EvaluateSketchSpline(Resolved.Curve, Best).Position, Target);
    }

    void FixPoints(SketchModel& Sketch, const std::vector<int>& Points)
    {
        for (int Point : Points) AppendSketchConstraint(Sketch, Kind::Fixed, Point, -1, -1, Sketch.Points[Point].Position.X, {}, Sketch.Points[Point].Position.Y);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S7 SPLINE VERBS
    //------------------------------------------------------------------------------------------------------------------------

    // 📝 Join sketch: Line1 (−2, 0) → (0, 0) = curve 0 (points 0, 1); Spline2 (Hermite) from point 1 through (1, 1.2),
    //    (2, 0.4) to (3, 1) = curve 1 (points 1–4); Arc3 about (3, 2) r 1 from (3, 1) (own point, coincident with the spline
    //    end) to (4, 2) = curve 2 (points 5–7); Spline4 (NURBS) from (3, 1) (own point 8) to (5, 0) = curve 3 (points
    //    8–10); a closed Spline5 = curve 4 (points 11–13); loose point 14 at (1.5, 1.4); Line6 far away = curve 5 (15, 16).
    [[nodiscard]] SketchModel BuildJoinSketch()
    {
        SketchModel Sketch;
        const int Tail = AppendSketchPoint(Sketch, { -2.0, 0.0 });
        const int Hub  = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        AppendSketchLine(Sketch, Tail, Hub, 1);
        std::vector<int> Spline = { Hub };
        for (const Vector2 Point : { Vector2{ 1.0, 1.2 }, Vector2{ 2.0, 0.4 }, Vector2{ 3.0, 1.0 } }) Spline.push_back(AppendSketchPoint(Sketch, Point));
        AppendSketchSpline(Sketch, Spline, SketchSplineForm::Hermite, 2);
        const int Centre = AppendSketchPoint(Sketch, { 3.0, 2.0 });
        const int Start  = AppendSketchPoint(Sketch, { 3.0, 1.0 });
        const int End    = AppendSketchPoint(Sketch, { 4.0, 2.0 });
        AppendSketchArc(Sketch, Centre, Start, End, 3);
        std::vector<int> Other;
        for (const Vector2 Point : { Vector2{ 3.0, 1.0 }, Vector2{ 4.0, -0.5 }, Vector2{ 5.0, 0.0 } }) Other.push_back(AppendSketchPoint(Sketch, Point));
        AppendSketchSpline(Sketch, Other, SketchSplineForm::Nurbs, 4);
        std::vector<int> Loop;
        for (const Vector2 Point : { Vector2{ 0.0, 4.0 }, Vector2{ 1.0, 5.0 }, Vector2{ 2.0, 4.0 } }) Loop.push_back(AppendSketchPoint(Sketch, Point));
        Loop.push_back(Loop.front());
        AppendSketchSpline(Sketch, Loop, SketchSplineForm::CubicSpline, 5);
        AppendSketchPoint(Sketch, { 1.5, 1.4 });
        AppendSketchLine(Sketch, AppendSketchPoint(Sketch, { 8.0, 8.0 }), AppendSketchPoint(Sketch, { 9.0, 8.0 }), 6);
        return Sketch;
    }

    void ValidateSplineVerbs()
    {
        const SketchModel Sketch = BuildJoinSketch();
        struct SplineCase
        {
            const char*             Name     = "";
            Verb                    Chosen   = Verb::On;
            std::vector<SketchPick> Picks    = {};
            Kind                    Expected = Kind::Coincident;
            int                     First = -1, Second = -1, Third = -1;
            double                  Value = 0.0, SecondValue = 0.0;
            const char*             Refusal  = nullptr;
        };
        const std::vector<SplineCase> Cases =
        {
            { "O point + spline → on spline", Verb::On, { CurvePick(1), PointPick(14) }, Kind::PointOnSpline, 14, 1, -1 },
            { "H spline + start → horizontal end", Verb::Horizontal, { CurvePick(1), PointPick(1) }, Kind::SplineDirection, 1, -1, 1, 0.0 },
            { "V spline + end → vertical end", Verb::Vertical, { PointPick(4), CurvePick(1) }, Kind::SplineDirection, 1, -1, 4, Pi / 2.0 },
            { "L spline + line (shared point) → G1", Verb::Parallel, { CurvePick(0), CurvePick(1) }, Kind::SplineTangent, 1, 0, 1, 0.0, 1.0 },
            { "C spline + line → G2", Verb::Curvature, { CurvePick(1), CurvePick(0) }, Kind::SplineCurvature, 1, 0, 1, 0.0, 1.0 },
            { "L spline + arc (coincident ends) → G1", Verb::Parallel, { CurvePick(1), CurvePick(2) }, Kind::SplineTangent, 1, 2, 4, 0.0, 0.0 },
            { "C spline + spline (coincident ends) → G2", Verb::Curvature, { CurvePick(1), CurvePick(3) }, Kind::SplineCurvature, 1, 3, 4, 0.0, 0.0 },
            { "O defining point refused", Verb::On, { PointPick(2), CurvePick(1) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "Spline2.Point1 defines Spline2" },
            { "H interior point refused", Verb::Horizontal, { CurvePick(1), PointPick(2) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "one of its end points" },
            { "L apart refused", Verb::Parallel, { CurvePick(1), CurvePick(5) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "do not meet at an end" },
            { "L closed spline refused", Verb::Parallel, { CurvePick(4), CurvePick(0) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "Spline5 is closed" },
            { "C closed spline refused", Verb::Curvature, { CurvePick(4), CurvePick(1) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "is closed" },
            { "D on a spline refused", Verb::Dimension, { CurvePick(1) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "it takes O, L, C and H / V" },
            { "C two lines refused", Verb::Curvature, { CurvePick(0), CurvePick(5) }, Kind::Coincident, -1, -1, -1, 0.0, 0.0, "takes a spline" },
        };
        for (const SplineCase& Case : Cases)
        {
            const SketchConstraintProposal Proposal = ProposeSketchConstraint(Sketch, Case.Picks, Case.Chosen);
            if (Case.Refusal)
            {
                Report(SplineVerbGate, Case.Name, "refusal: " + Proposal.Refusal, 0.0, Proposal.Refusal.find(Case.Refusal) != std::string::npos);
                continue;
            }
            const SketchConstraint& Made = Proposal.Constraint;
            const double Gap = std::max(std::abs(Made.Value - Case.Value), std::abs(Made.SecondValue - Case.SecondValue));
            const bool Passed = Proposal.Refusal.empty() && Made.Kind == Case.Expected && Made.First == Case.First && Made.Second == Case.Second && Made.Third == Case.Third && Gap <= 1.0e-12;
            Report(SplineVerbGate, Case.Name, DescribeSketchConstraint(Sketch, Made) + (Proposal.Refusal.empty() ? "" : " · refused: " + Proposal.Refusal), Gap, Passed);
        }
        // A natural cubic spline meeting a line: G2 refused with the reason.
        SketchModel Natural;
        const int Joint = AppendSketchPoint(Natural, { 0.0, 0.0 });
        AppendSketchLine(Natural, AppendSketchPoint(Natural, { -1.0, 0.0 }), Joint, 1);
        AppendSketchSpline(Natural, { Joint, AppendSketchPoint(Natural, { 1.0, 1.0 }), AppendSketchPoint(Natural, { 2.0, 0.0 }) }, SketchSplineForm::CubicSpline, 2);
        const SketchConstraintProposal Refused = ProposeSketchConstraint(Natural, { CurvePick(1), CurvePick(0) }, Verb::Curvature);
        Report(SplineVerbGate, "C natural cubic spline refused", "refusal: " + Refused.Refusal, 0.0, Refused.Refusal.find("zero curvature by construction") != std::string::npos);
        Report(SplineVerbGate, "key C, label", std::string(1, SketchConstraintVerbKey(Verb::Curvature)) + " · " + SketchConstraintVerbLabel(Verb::Curvature), 0.0,
               SketchConstraintVerbKey(Verb::Curvature) == 'C' && std::string(SketchConstraintVerbLabel(Verb::Curvature)) == "Curvature (G2)");
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S8 SPLINE SOLVES
    //------------------------------------------------------------------------------------------------------------------------

    void ValidateSplineSolves()
    {
        // G1 to a fixed line in every form: the spline starts at the line's end.
        for (const SketchSplineForm Form : { SketchSplineForm::Hermite, SketchSplineForm::Bezier, SketchSplineForm::Nurbs, SketchSplineForm::CubicSpline })
        {
            SketchModel Sketch;
            const int Tail = AppendSketchPoint(Sketch, { -2.0, 0.0 });
            const int Hub  = AppendSketchPoint(Sketch, { 0.0, 0.0 });
            const int Line = AppendSketchLine(Sketch, Tail, Hub, 1);
            FixPoints(Sketch, { Tail, Hub });
            std::vector<int> Points = { Hub };
            for (const Vector2 Point : { Vector2{ 1.0, 1.2 }, Vector2{ 2.0, 0.4 }, Vector2{ 3.0, 1.0 } }) Points.push_back(AppendSketchPoint(Sketch, Point));
            const int Spline = AppendSketchSpline(Sketch, Points, Form, 2);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { CurvePick(Spline), CurvePick(Line) }, Verb::Parallel).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const double Gap = Sine(MeasureEnd(Sketch, Spline, true).Tangent, Sketch.Points[Hub].Position - Sketch.Points[Tail].Position);
            Report(SplineSolveGate, std::string("G1 to a line · ") + SketchSplineFormLabel(Form), "|sin| = " + Scientific(Gap) + " · DOF " + std::to_string(Outcome.DegreesOfFreedom), Gap,
                   Outcome.Converged && Gap <= 1.0e-12 && Outcome.DegreesOfFreedom == 5);
        }
        // G2 Hermite (fixed) → NURBS sharing its last point.
        {
            SketchModel Sketch;
            std::vector<int> First;
            for (const Vector2 Point : { Vector2{ -3.0, 0.0 }, Vector2{ -2.0, 0.8 }, Vector2{ -1.0, 0.5 }, Vector2{ 0.0, 0.0 } }) First.push_back(AppendSketchPoint(Sketch, Point));
            const int Lead = AppendSketchSpline(Sketch, First, SketchSplineForm::Hermite, 1);
            FixPoints(Sketch, First);
            std::vector<int> Second = { First.back() };
            for (const Vector2 Point : { Vector2{ 1.0, -0.3 }, Vector2{ 2.0, 0.4 }, Vector2{ 3.0, 0.2 } }) Second.push_back(AppendSketchPoint(Sketch, Point));
            const int Follow = AppendSketchSpline(Sketch, Second, SketchSplineForm::Nurbs, 2);
            const SketchConstraintProposal Proposal = ProposeSketchConstraint(Sketch, { CurvePick(Follow), CurvePick(Lead) }, Verb::Curvature);
            Sketch.Constraints.push_back(Proposal.Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const EndFrame A = MeasureEnd(Sketch, Lead, false), B = MeasureEnd(Sketch, Follow, true);
            const double Turn = Sine(A.Tangent, B.Tangent), Bend = std::abs(SignedCurvature(A) - SignedCurvature(B)) * Outcome.Scale;
            Report(SplineSolveGate, "G2 Hermite → NURBS", "|sin| " + Scientific(Turn) + " · |Δκ|·L " + Scientific(Bend) + " · κ " + Scientific(SignedCurvature(A)), std::max(Turn, Bend),
                   Proposal.Refusal.empty() && Outcome.Converged && std::max(Turn, Bend) <= 1.0e-12 && std::abs(SignedCurvature(A)) > 0.1);
        }
        // G2 NURBS → fixed arc (r 1) sharing the arc's start: κ = +1 / r at the joint.
        {
            SketchModel Sketch;
            const int Centre = AppendSketchPoint(Sketch, { 3.0, 2.0 });
            const int Start  = AppendSketchPoint(Sketch, { 3.0, 1.0 });
            const int End    = AppendSketchPoint(Sketch, { 4.0, 2.0 });
            const int Arc    = AppendSketchArc(Sketch, Centre, Start, End, 1);
            FixPoints(Sketch, { Centre, Start, End });
            std::vector<int> Points;
            for (const Vector2 Point : { Vector2{ 0.0, 0.0 }, Vector2{ 1.0, 1.1 }, Vector2{ 2.0, 0.7 } }) Points.push_back(AppendSketchPoint(Sketch, Point));
            Points.push_back(Start);
            FixPoints(Sketch, { Points.front() });
            const int Spline = AppendSketchSpline(Sketch, Points, SketchSplineForm::Nurbs, 2);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { CurvePick(Spline), CurvePick(Arc) }, Verb::Curvature).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const EndFrame Joint = MeasureEnd(Sketch, Spline, false);
            const Vector2  Radial = Sketch.Points[Start].Position - Sketch.Points[Centre].Position;
            const Vector2  Tangent = { -Radial.Y, Radial.X };
            const double   Orientation = Dot(Joint.Tangent, Tangent) < 0.0 ? -1.0 : 1.0;
            const double   Turn = Sine(Joint.Tangent, Tangent), Bend = std::abs(SignedCurvature(Joint) - Orientation * 1.0) * Outcome.Scale;
            Report(SplineSolveGate, "G2 NURBS → arc", "|sin| " + Scientific(Turn) + " · |κ ∓ 1/r|·L " + Scientific(Bend), std::max(Turn, Bend), Outcome.Converged && std::max(Turn, Bend) <= 1.0e-12);
        }
        // G2 Bezier → fixed line: κ = 0 at the joint (three control points collinear).
        {
            SketchModel Sketch;
            const int Tail = AppendSketchPoint(Sketch, { -2.0, 0.0 });
            const int Hub  = AppendSketchPoint(Sketch, { 0.0, 0.0 });
            const int Line = AppendSketchLine(Sketch, Tail, Hub, 1);
            FixPoints(Sketch, { Tail, Hub });
            std::vector<int> Points = { Hub };
            for (const Vector2 Point : { Vector2{ 1.0, 0.6 }, Vector2{ 2.0, 1.4 }, Vector2{ 3.0, 1.0 } }) Points.push_back(AppendSketchPoint(Sketch, Point));
            const int Spline = AppendSketchSpline(Sketch, Points, SketchSplineForm::Bezier, 2);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { CurvePick(Spline), CurvePick(Line) }, Verb::Curvature).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const EndFrame Joint = MeasureEnd(Sketch, Spline, true);
            const double Turn = Sine(Joint.Tangent, { 1.0, 0.0 }), Bend = std::abs(SignedCurvature(Joint)) * Outcome.Scale;
            Report(SplineSolveGate, "G2 Bezier → line", "|sin| " + Scientific(Turn) + " · |κ|·L " + Scientific(Bend) + " · DOF " + std::to_string(Outcome.DegreesOfFreedom), std::max(Turn, Bend),
                   Outcome.Converged && std::max(Turn, Bend) <= 1.0e-12 && Outcome.DegreesOfFreedom == 4);
        }
        // Point on a fixed spline; H / V end directions on a free one.
        {
            SketchModel Sketch = BuildJoinSketch();
            FixPoints(Sketch, Sketch.Curves[1].Points);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { PointPick(14), CurvePick(1) }, Verb::On).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const double Gap = SplineGap(Sketch, 1, Sketch.Points[14].Position);
            Report(SplineSolveGate, "point on spline", "distance " + Scientific(Gap) + " · moved " + Scientific(Distance(Sketch.Points[14].Position, { 1.5, 1.4 })), Gap,
                   Outcome.Converged && Gap <= 1.0e-12 && Distance(Sketch.Points[14].Position, { 1.5, 1.4 }) > 1.0e-3);
        }
        {
            SketchModel Sketch = BuildJoinSketch();
            FixPoints(Sketch, { 1 });
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { CurvePick(1), PointPick(1) }, Verb::Horizontal).Constraint);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { CurvePick(1), PointPick(4) }, Verb::Vertical).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const EndFrame Head = MeasureEnd(Sketch, 1, true), Tail = MeasureEnd(Sketch, 1, false);
            const double Level = std::abs(Head.Tangent.Y) / Length(Head.Tangent), Plumb = std::abs(Tail.Tangent.X) / Length(Tail.Tangent);
            Report(SplineSolveGate, "H start · V end", "|sin| " + Scientific(Level) + " · " + Scientific(Plumb) + " · " + DescribeSketchConstraint(Sketch, Sketch.Constraints[1]), std::max(Level, Plumb),
                   Outcome.Converged && std::max(Level, Plumb) <= 1.0e-12 && DescribeSketchConstraint(Sketch, Sketch.Constraints[1]) == "Horizontal end · Line1.End");
        }
        // DOF and redundancy: NURBS from a fixed line end (6 DOF) → G1 (5) → G2 replacing it (4); both on one joint → redundant.
        {
            SketchModel Sketch;
            const int Tail = AppendSketchPoint(Sketch, { -2.0, 0.0 });
            const int Hub  = AppendSketchPoint(Sketch, { 0.0, 0.0 });
            const int Line = AppendSketchLine(Sketch, Tail, Hub, 1);
            FixPoints(Sketch, { Tail, Hub });
            std::vector<int> Points = { Hub };
            for (const Vector2 Point : { Vector2{ 1.0, 1.2 }, Vector2{ 2.0, 0.4 }, Vector2{ 3.0, 1.0 } }) Points.push_back(AppendSketchPoint(Sketch, Point));
            const int Spline = AppendSketchSpline(Sketch, Points, SketchSplineForm::Nurbs, 2);
            SketchModel Bare = Sketch;
            const int   Free = SolveSketch(Bare).DegreesOfFreedom;
            SketchModel Smooth = Sketch;
            Smooth.Constraints.push_back(ProposeSketchConstraint(Smooth, { CurvePick(Spline), CurvePick(Line) }, Verb::Parallel).Constraint);
            const int Tangent = SolveSketch(Smooth).DegreesOfFreedom;
            SketchModel Curved = Sketch;
            Curved.Constraints.push_back(ProposeSketchConstraint(Curved, { CurvePick(Spline), CurvePick(Line) }, Verb::Curvature).Constraint);
            const int Curvature = SolveSketch(Curved).DegreesOfFreedom;
            Report(SplineSolveGate, "DOF 6 → 5 → 4", std::to_string(Free) + " → " + std::to_string(Tangent) + " → " + std::to_string(Curvature), 0.0, Free == 6 && Tangent == 5 && Curvature == 4);
            Smooth.Constraints.push_back(ProposeSketchConstraint(Smooth, { CurvePick(Spline), CurvePick(Line) }, Verb::Curvature).Constraint);
            const SketchSolveOutcome Both = SolveSketch(Smooth);
            const SketchConstraintReport& Last = Both.Constraints.back();
            const bool Partnered = std::find(Last.Partners.begin(), Last.Partners.end(), static_cast<int>(Smooth.Constraints.size()) - 2) != Last.Partners.end();
            Report(SplineSolveGate, "G1 + G2 on one joint → redundant", std::string(SketchConstraintStatusLabel(Last.Status)) + " · partner G1 " + (Partnered ? "named" : "missing"), 0.0,
                   Last.Status == SketchConstraintStatus::Redundant && Partnered);
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S9 DRIVEN DIMENSIONS
    //------------------------------------------------------------------------------------------------------------------------

    void ValidateDriven()
    {
        // Rectangle 3 × 2 at the origin, fully constrained: H V H V + Fixed corner + width 3 + height 2.
        SketchModel Sketch;
        AppendSketchRectangle(Sketch, { 0.0, 0.0 }, { 3.0, 2.0 }, 1);
        FixPoints(Sketch, { 0 });
        AppendSketchConstraint(Sketch, Kind::Length, 0, -1, -1, 3.0, "D1");
        AppendSketchConstraint(Sketch, Kind::Length, 1, -1, -1, 2.0, "D2");
        SketchModel Plain = Sketch;
        const SketchSolveOutcome PlainSolve = SolveSketch(Plain);
        const SketchConstraintProposal Diagonal = ProposeSketchReference(Sketch, { PointPick(0), PointPick(2) });
        Sketch.Constraints.push_back(Diagonal.Constraint);
        const SketchSolveOutcome First = SolveSketch(Sketch);
        const SketchConstraint&  Driven = Sketch.Constraints.back();
        const double Root = std::abs(Driven.Value - std::sqrt(13.0));
        Report(DrivenGate, "diagonal reads √13", DescribeSketchConstraint(Sketch, Driven) + " · |Δ| " + Scientific(Root), Root,
               Diagonal.Refusal.empty() && Driven.DrivenCondition && Root <= 1.0e-12 && First.Constraints.back().Status == SketchConstraintStatus::Driven);
        Report(DrivenGate, "DOF unchanged, no conflict", "DOF " + std::to_string(PlainSolve.DegreesOfFreedom) + " → " + std::to_string(First.DegreesOfFreedom) + " · conflicts " + std::to_string(First.ConflictGroups.size()),
               0.0, First.DegreesOfFreedom == 0 && PlainSolve.DegreesOfFreedom == 0 && First.ConflictGroups.empty() && First.Redundant.empty());
        bool Equal = true;
        for (size_t Point = 0; Point < Sketch.Points.size(); ++Point) Equal = Equal && Sketch.Points[Point].Position.X == Plain.Points[Point].Position.X && Sketch.Points[Point].Position.Y == Plain.Points[Point].Position.Y;
        Report(DrivenGate, "positions bit-equal with / without", Equal ? "bit-equal" : "differ", 0.0, Equal);
        Sketch.Constraints[6].Value = 4.0;                                                  // D2: height 2 → 4
        const SketchSolveOutcome Second = SolveSketch(Sketch);
        const double Five = std::abs(Sketch.Constraints.back().Value - 5.0);
        Report(DrivenGate, "height 4 → diagonal 5", DescribeSketchConstraint(Sketch, Sketch.Constraints.back()) + " · |Δ| " + Scientific(Five), Five, Second.Converged && Five <= 1.0e-12);
        Report(DrivenGate, "text", DescribeSketchConstraint(Sketch, Sketch.Constraints.back()), 0.0, DescribeSketchConstraint(Sketch, Sketch.Constraints.back()) == "Distance 5 (reference) · Line1.Start · Line2.End");
        const SketchConstraintProposal Angle = ProposeSketchReference(Sketch, { CurvePick(0), CurvePick(1) });
        Sketch.Constraints.push_back(Angle.Constraint);
        const SketchSolveOutcome Third = SolveSketch(Sketch);
        const double Right = std::abs(Sketch.Constraints.back().Value - Pi / 2.0);
        Report(DrivenGate, "reference angle 90°", DescribeSketchConstraint(Sketch, Sketch.Constraints.back()) + " · |Δ| " + Scientific(Right), Right,
               Angle.Refusal.empty() && Angle.Constraint.Kind == Kind::Angle && Right <= 1.0e-12 && Third.DegreesOfFreedom == 0);
        const SketchConstraintProposal Lone = ProposeSketchReference(Sketch, { PointPick(0) });
        Report(DrivenGate, "one point refused", "refusal: " + Lone.Refusal, 0.0, Lone.Refusal.find("Reference dimension: takes") != std::string::npos);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S10 LINKED GEOMETRY
    //------------------------------------------------------------------------------------------------------------------------

    void ValidateLinks()
    {
        // Solver: a point linked to another sketch at (2, 1); a free line ends coincident with it.
        {
            SketchModel Sketch;
            const int Linked = AppendSketchPoint(Sketch, { 2.0, 1.0 });
            Sketch.Points[Linked].Link = { "Sketch#1", 2 };
            const int Line = AppendSketchLine(Sketch, AppendSketchPoint(Sketch, { 0.0, 0.0 }), AppendSketchPoint(Sketch, { 1.7, 0.8 }), 1);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { PointPick(Sketch.Curves[Line].End), PointPick(Linked) }, Verb::On).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const bool   Held = Sketch.Points[Linked].Position.X == 2.0 && Sketch.Points[Linked].Position.Y == 1.0;
            const double Gap  = Distance(Sketch.Points[Sketch.Curves[Line].End].Position, { 2.0, 1.0 });
            Report(LinkGate, "linked point held, line end lands on it", std::string(Held ? "held bit-exact" : "moved") + " · gap " + Scientific(Gap) + " · DOF " + std::to_string(Outcome.DegreesOfFreedom)
                   + " · linked unknowns " + std::to_string(Outcome.Linked), Gap, Held && Gap <= 1.0e-12 && Outcome.DegreesOfFreedom == 2 && Outcome.Linked == 2 && Outcome.Unknowns == 4);
            Report(LinkGate, "linked point counts as fixed", Outcome.PointFixed[Linked] ? "fixed" : "free", 0.0, Outcome.PointFixed[Linked]);
            SketchModel Again = Sketch;
            AppendSketchConstraint(Again, Kind::Fixed, Linked, -1, -1, 2.0, {}, 1.0);
            const SketchSolveOutcome Redundant = SolveSketch(Again);
            Report(LinkGate, "Fixed at its place → redundant", SketchConstraintStatusLabel(Redundant.Constraints.back().Status), 0.0, Redundant.Constraints.back().Status == SketchConstraintStatus::Redundant);
            Again.Constraints.back().Value = 2.5;
            const SketchSolveOutcome Conflict = SolveSketch(Again);
            Report(LinkGate, "Fixed elsewhere → conflicting", SketchConstraintStatusLabel(Conflict.Constraints.back().Status), 0.0,
                   Conflict.Constraints.back().Status == SketchConstraintStatus::Conflicting && Again.Points[Linked].Position.X == 2.0);
            const SketchDragOutcome Drag = DragSketchPoint(Sketch, Linked, { 3.0, 3.0 });
            Report(LinkGate, "drag refused", "refusal: " + Drag.Refusal, 0.0, Drag.Refusal == "Drag: Point0 is linked to Sketch#1 (move it there)");
        }
        // A linked circle keeps its radius while a line is made tangent to it.
        {
            SketchModel Sketch;
            const int Centre = AppendSketchPoint(Sketch, { 0.0, 0.0 });
            Sketch.Points[Centre].Link = { "Sketch#1", 0 };
            const int Circle = AppendSketchCircle(Sketch, Centre, 1.0, 1);
            Sketch.Curves[Circle].Link = { "Sketch#1", 0 };
            const int Line = AppendSketchLine(Sketch, AppendSketchPoint(Sketch, { -2.0, 1.4 }), AppendSketchPoint(Sketch, { 2.0, 1.6 }), 2);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { CurvePick(Line), CurvePick(Circle) }, Verb::Parallel).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const Vector2 Start = Sketch.Points[Sketch.Curves[Line].Start].Position, End = Sketch.Points[Sketch.Curves[Line].End].Position;
            const double  Gap   = std::abs(std::abs(Cross(End - Start, Start * -1.0)) / Distance(Start, End) - 1.0);
            Report(LinkGate, "linked circle keeps r = 1", "r " + Scientific(Sketch.Curves[Circle].Radius - 1.0) + " · tangent gap " + Scientific(Gap), Gap,
                   Outcome.Converged && Sketch.Curves[Circle].Radius == 1.0 && Gap <= 1.0e-12 && Outcome.CurveFixed[Circle]);
        }
        // Feature tree: Sketch#1 rectangle (Width parameter), Sketch#2 at z = 1 with a line ending on the far corner (linked),
        // Sketch#3 on XZ linking the same corner, Sketch#4 linking a sketch that does not exist.
        {
            FeatureTree Tree;
            Tree.Name = "Links";
            Tree.Parameters.push_back({ "Width", 3.0, 1.0, 6.0, "rectangle width" });
            FeatureStep First;
            First.Kind  = FeatureStepKind::Sketch;
            First.Label = "Sketch#1";
            AppendSketchRectangle(First.Sketch, { 0.0, 0.0 }, { 3.0, 2.0 }, 1);
            FixPoints(First.Sketch, { 0 });
            AppendSketchConstraint(First.Sketch, Kind::Length, 0, -1, -1, 3.0, "Width");
            First.Sketch.Constraints.back().Parameter = "Width";
            AppendSketchConstraint(First.Sketch, Kind::Length, 1, -1, -1, 2.0, "D2");
            Tree.Steps.push_back(First);
            FeatureStep Second;
            Second.Kind      = FeatureStepKind::Sketch;
            Second.Label     = "Sketch#2";
            Second.Elevation = { 1.0 };
            const int Corner = AppendSketchPoint(Second.Sketch, { 0.0, 0.0 });
            Second.Sketch.Points[Corner].Link = { "Sketch#1", 2 };
            const int Line = AppendSketchLine(Second.Sketch, AppendSketchPoint(Second.Sketch, { -1.0, 0.0 }), AppendSketchPoint(Second.Sketch, { 2.5, 1.5 }), 1);
            FixPoints(Second.Sketch, { Second.Sketch.Curves[Line].Start });
            AppendSketchConstraint(Second.Sketch, Kind::Coincident, Second.Sketch.Curves[Line].End, Corner);
            Tree.Steps.push_back(Second);
            FeatureStep Third;
            Third.Kind  = FeatureStepKind::Sketch;
            Third.Label = "Sketch#3";
            Third.Plane = { { 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
            AppendSketchPoint(Third.Sketch, { 9.0, 9.0 });
            Third.Sketch.Points[0].Link = { "Sketch#1", 2 };
            Tree.Steps.push_back(Third);
            FeatureStep Fourth;
            Fourth.Kind  = FeatureStepKind::Sketch;
            Fourth.Label = "Sketch#4";
            AppendSketchPoint(Fourth.Sketch, { 0.5, 0.5 });
            Fourth.Sketch.Points[0].Link = { "Sketch#9", 0 };
            Tree.Steps.push_back(Fourth);

            ModelPartition Partition;
            const RegenerationOutcome Before = RegenerateFeatureTree(Partition, Tree);
            const SketchModel& Linked = Before.Sketches.at("Sketch#2");
            const double Lands = Distance(Linked.Points[Linked.Curves[0].End].Position, { 3.0, 2.0 });
            Report(LinkGate, "Sketch#2 line ends on the corner (3, 2)", "gap " + Scientific(Lands) + " · built " + std::string(Before.Steps[1].Status == FeatureStepStatus::Built ? "yes" : "no"), Lands,
                   Lands <= 1.0e-12 && Before.Steps[1].Status == FeatureStepStatus::Built && Before.Steps[1].Notes.empty());
            Tree.Parameters[0].Value = 4.0;
            ModelPartition Again;
            const RegenerationOutcome After = RegenerateFeatureTree(Again, Tree);
            const SketchModel& Moved = After.Sketches.at("Sketch#2");
            const double Follows = Distance(Moved.Points[Moved.Curves[0].End].Position, { 4.0, 2.0 });
            Report(LinkGate, "Width 3 → 4: Sketch#2 follows to (4, 2)", "gap " + Scientific(Follows), Follows, Follows <= 1.0e-12);
            const Vector2 Side = After.Sketches.at("Sketch#3").Points[0].Position;
            const double  Projected = Distance(Side, { 4.0, 0.0 });
            Report(LinkGate, "XZ sketch gets (x, z) = (4, 0)", "(" + Scientific(Side.X) + ", " + Scientific(Side.Y) + ")", Projected, Projected <= 1.0e-12);
            const FeatureStepReport& Dangling = After.Steps[3];
            const Vector2 Kept = After.Sketches.at("Sketch#4").Points[0].Position;
            Report(LinkGate, "dangling link noted and held", Dangling.Notes.empty() ? "no note" : Dangling.Notes.front(), 0.0,
                   Dangling.Notes.size() == 1 && Dangling.Notes.front() == "Point0: link to Sketch#9 is dangling (held where it was)" && Kept.X == 0.5 && Kept.Y == 0.5);
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    S6 SOLVED VERBS
    //------------------------------------------------------------------------------------------------------------------------

    void ValidateSolved()
    {
        // Tangent: a line and a circle apart; L makes the line touch the circle.
        {
            SketchModel Sketch;
            const int Line   = AppendSketchLine(Sketch, AppendSketchPoint(Sketch, { -2.0, 1.4 }), AppendSketchPoint(Sketch, { 2.0, 1.6 }), 1);
            const int Circle = AppendSketchCircle(Sketch, AppendSketchPoint(Sketch, { 0.0, 0.0 }), 1.0, 2);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { CurvePick(Line), CurvePick(Circle) }, Verb::Parallel).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const Vector2 Start = Sketch.Points[0].Position, End = Sketch.Points[1].Position, Centre = Sketch.Points[2].Position;
            const double  Gap   = std::abs(std::abs(Cross(End - Start, Centre - Start)) / Distance(Start, End) - Sketch.Curves[Circle].Radius);
            Report(SolveGate, "tangent line–circle", "|dist(centre, line) − r| = " + Scientific(Gap), Gap, Outcome.Converged && Gap <= 1.0e-12);
        }
        // Angle typed 30° between two lines from a common point.
        {
            SketchModel Sketch;
            const int Hub   = AppendSketchPoint(Sketch, { 0.0, 0.0 });
            const int First = AppendSketchLine(Sketch, Hub, AppendSketchPoint(Sketch, { 2.0, 0.1 }), 1);
            const int Other = AppendSketchLine(Sketch, Hub, AppendSketchPoint(Sketch, { 1.0, 1.2 }), 2);
            SketchConstraint Angle = ProposeSketchConstraint(Sketch, { CurvePick(First), CurvePick(Other) }, Verb::Angle).Constraint;
            Angle.Value = Pi / 6.0;
            Sketch.Constraints.push_back(Angle);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const double Gap = std::abs(MeasureSketchConstraint(Sketch, Angle) - Pi / 6.0);
            Report(SolveGate, "angle typed 30°", "|θ − 30°| = " + Scientific(Gap) + " rad", Gap, Outcome.Converged && Gap <= 1.0e-12);
        }
        // Equal radius, concentric, midpoint, symmetric, perpendicular on one sketch.
        {
            SketchModel Sketch = BuildTestSketch();
            const auto Push = [&](Verb Chosen, std::vector<SketchPick> Picks) { Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, Picks, Chosen).Constraint); };
            Push(Verb::Equal, { CurvePick(4), CurvePick(5) });
            Push(Verb::Midpoint, { PointPick(11), CurvePick(0) });
            const SketchSolveOutcome First = SolveSketch(Sketch);
            const double Radii   = std::abs(MeasureSketchRadius(Sketch, Sketch.Curves[4]) - MeasureSketchRadius(Sketch, Sketch.Curves[5]));
            const Vector2 Middle = (Sketch.Points[0].Position + Sketch.Points[1].Position) * 0.5;
            const double Midway  = Distance(Sketch.Points[11].Position, Middle);
            Report(SolveGate, "equal radius", "|r₁ − r₂| = " + Scientific(Radii), Radii, First.Converged && Radii <= 1.0e-12);
            Report(SolveGate, "midpoint", "|P11 − mid(Line1)| = " + Scientific(Midway), Midway, First.Converged && Midway <= 1.0e-12);
            Push(Verb::On, { CurvePick(4), CurvePick(5) });
            const SketchSolveOutcome Second = SolveSketch(Sketch);
            const double Centres = Distance(Sketch.Points[4].Position, Sketch.Points[5].Position);
            Report(SolveGate, "concentric", "|C₁ − C₂| = " + Scientific(Centres), Centres, Second.Converged && Centres <= 1.0e-12);
        }
        {
            SketchModel Sketch;
            const int Axis  = AppendSketchLine(Sketch, AppendSketchPoint(Sketch, { 0.0, -1.0 }), AppendSketchPoint(Sketch, { 0.2, 3.0 }), 1);
            const int Left  = AppendSketchPoint(Sketch, { -1.0, 1.0 });
            const int Right = AppendSketchPoint(Sketch, { 1.3, 1.4 });
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { PointPick(Left), PointPick(Right), CurvePick(Axis) }, Verb::Symmetric).Constraint);
            const SketchSolveOutcome Outcome = SolveSketch(Sketch);
            const Vector2 Start = Sketch.Points[0].Position, Direction = Normalize(Sketch.Points[1].Position - Start);
            const Vector2 Middle = (Sketch.Points[Left].Position + Sketch.Points[Right].Position) * 0.5;
            const double Across = std::abs(Cross(Direction, Middle - Start));
            const double Square = std::abs(Dot(Sketch.Points[Right].Position - Sketch.Points[Left].Position, Direction));
            Report(SolveGate, "symmetric", "midpoint on the axis " + Scientific(Across) + " · chord ⟂ axis " + Scientific(Square), std::max(Across, Square), Outcome.Converged && std::max(Across, Square) <= 1.0e-12);
            const int Cross1 = AppendSketchLine(Sketch, Left, Right, 2);
            const int Slant  = AppendSketchLine(Sketch, AppendSketchPoint(Sketch, { 3.0, 0.0 }), AppendSketchPoint(Sketch, { 4.0, 0.7 }), 3);
            Sketch.Constraints.push_back(ProposeSketchConstraint(Sketch, { CurvePick(Cross1), CurvePick(Slant) }, Verb::Perpendicular).Constraint);
            const SketchSolveOutcome Square2 = SolveSketch(Sketch);
            const Vector2 U = Sketch.Points[Sketch.Curves[Cross1].End].Position - Sketch.Points[Sketch.Curves[Cross1].Start].Position;
            const Vector2 V = Sketch.Points[Sketch.Curves[Slant].End].Position - Sketch.Points[Sketch.Curves[Slant].Start].Position;
            const double Cosine = std::abs(Dot(U, V)) / (Length(U) * Length(V));
            Report(SolveGate, "perpendicular", "|cos| = " + Scientific(Cosine), Cosine, Square2.Converged && Cosine <= 1.0e-12);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ValidateVerbs();
    ValidateMeasuring();
    ValidateDegrees();
    ValidateDrag();
    ValidateNames();
    ValidateSolved();
    ValidateSplineVerbs();
    ValidateSplineSolves();
    ValidateDriven();
    ValidateLinks();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Sketch constraining validation\n\n%s Constraints C1 + L1: SolveSpace constraint verbs on picked sketch points / curves, dimensions, diagnosis, point drag; spline G1 / G2 / on / end direction, reference dimensions, linked geometry\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
