/*====================================================================================================================================
                                                     SKETCHTRIMVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Trim / split phase T4: curve trim (Plasticity T) — exact crossings, the picked piece comes off, constraints follow

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Sketch/SketchProfile.h"
#include "Engine/GeometryProcessing/Sketch/SketchSolver.h"
#include "Engine/GeometryProcessing/Sketch/SketchSpline.h"
#include "Engine/GeometryProcessing/Sketch/SketchTrim.h"

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

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - R1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally AnalyticGate   = { "R1", "Crossings of lines, circles and arcs vs closed form (line–line, line–circle, circle–circle, arc ranges, touching ends)", "≤ 10⁻¹²" };
    GateTally SplineGate     = { "R2", "Line × cubic spline crossings: count vs dense sign changes, both curves pass through each crossing", "≤ 10⁻¹²" };
    GateTally RectangleGate  = { "R3", "Rectangle 4 × 2 crossed by x = 1: five T picks leave the closed 3 × 2 profile; area exact, solve keeps it", "≤ 10⁻¹²" };
    GateTally CircleGate     = { "R4", "Circle r 1 crossed by y = 0: T on the top half → lower arc, T on the overhangs → D profile of area π/2", "≤ 10⁻¹²" };
    GateTally ConstraintGate = { "R5", "Constraints: split line copies Horizontal, drops Length; new ends pinned PointOnLine; erase shifts curve references", "exact" };
    GateTally ForeignGate    = { "R6", "Foreign cutter (coplanar sketch) splits a line: middle piece removed, no pin (another sketch)", "≤ 10⁻¹²" };
    GateTally RefuseGate     = { "R7", "Whole-curve erase without crossings (line, once-crossed circle); spline split refused; bad index refused; untouched on refusal", "all named" };

    std::vector<GateTally*> AllGates() { return { &AnalyticGate, &SplineGate, &RectangleGate, &CircleGate, &ConstraintGate, &ForeignGate, &RefuseGate }; }

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

    [[nodiscard]] ProfileSegment Segment(const SketchModel& Sketch, int Curve)
    {
        std::string Refusal;
        return ResolveSketchCurveSegment(Sketch, Curve, 1.0e-12, Refusal);
    }

    [[nodiscard]] double WorstMatch(const std::vector<SegmentCrossing>& Found, const std::vector<Vector2>& Expected)
    {
        if (Found.size() != Expected.size()) return 1.0;
        double Worst = 0.0;
        for (const Vector2& Point : Expected)
        {
            double Nearest = 1.0;
            for (const SegmentCrossing& Crossing : Found) Nearest = std::min(Nearest, Distance(Crossing.Position, Point));
            Worst = std::max(Worst, Nearest);
        }
        return Worst;
    }

    [[nodiscard]] std::string Points(const std::vector<SegmentCrossing>& Found)
    {
        std::string Text;
        for (const SegmentCrossing& Crossing : Found)
        {
            char Piece[64];
            std::snprintf(Piece, sizeof Piece, "%s(%.6g, %.6g)", Text.empty() ? "" : " ", Crossing.Position.X, Crossing.Position.Y);
            Text += Piece;
        }
        return Text.empty() ? "none" : Text;
    }

    [[nodiscard]] int CountKind(const SketchModel& Sketch, SketchConstraintKind Kind)
    {
        return static_cast<int>(std::count_if(Sketch.Constraints.begin(), Sketch.Constraints.end(), [&](const SketchConstraint& Constraint) { return Constraint.Kind == Kind; }));
    }

    // Largest move of any point between two sketches with the same points.
    [[nodiscard]] double LargestMove(const SketchModel& Before, const SketchModel& After)
    {
        double Worst = 0.0;
        for (size_t Index = 0; Index < Before.Points.size() && Index < After.Points.size(); ++Index)
            Worst = std::max(Worst, Distance(Before.Points[Index].Position, After.Points[Index].Position));
        return Worst;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                GATES
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateAnalytic()
    {
        SketchModel Sketch;
        const int Origin = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        const int A      = AppendSketchPoint(Sketch, { -2.0, 0.5 });
        const int B      = AppendSketchPoint(Sketch, { 2.0, 0.5 });
        const int C      = AppendSketchPoint(Sketch, { 0.3, -2.0 });
        const int D      = AppendSketchPoint(Sketch, { 0.3, 2.0 });
        const int Shift  = AppendSketchPoint(Sketch, { 1.2, 0.0 });
        const int ArcStart = AppendSketchPoint(Sketch, { 1.0, 0.0 });
        const int ArcEnd   = AppendSketchPoint(Sketch, { -1.0, 0.0 });
        const int T1 = AppendSketchPoint(Sketch, { 2.0, -1.0 });
        const int T2 = AppendSketchPoint(Sketch, { 1.0, 0.5 });
        AppendSketchLine(Sketch, A, B, 1);                                                   // 0: y = 0.5
        AppendSketchLine(Sketch, C, D, 2);                                                   // 1: x = 0.3
        AppendSketchCircle(Sketch, Origin, 1.0, 1);                                          // 2: unit circle
        AppendSketchCircle(Sketch, Shift, 0.8, 2);                                           // 3: r 0.8 at (1.2, 0)
        AppendSketchArc(Sketch, Origin, ArcStart, ArcEnd, 1);                                // 4: upper half circle (CCW 0 → π)
        AppendSketchLine(Sketch, T1, T2, 3);                                                 // 5: ends on y = 0.5 at x = 1

        const double Root = std::sqrt(1.0 - 0.25);
        const double Along = (1.0 + 1.44 - 0.64) / (2.0 * 1.2);                              // Circle–circle chord x
        const double Rise  = std::sqrt(1.0 - Along * Along);
        struct Case { const char* Name; int Target; int Cutter; std::vector<Vector2> Expected; };
        const std::vector<Case> Cases = {
            { "line × line (y = 0.5 · x = 0.3)", 0, 1, { { 0.3, 0.5 } } },
            { "line × circle (y = 0.5 · r 1)", 0, 2, { { -Root, 0.5 }, { Root, 0.5 } } },
            { "circle × line (r 1 · y = 0.5)", 2, 0, { { -Root, 0.5 }, { Root, 0.5 } } },
            { "circle × circle (r 1 · r 0.8 at 1.2)", 2, 3, { { Along, Rise }, { Along, -Rise } } },
            { "upper arc × circle (only y > 0 counts)", 4, 3, { { Along, Rise } } },
            { "upper arc × line x = 0.3", 4, 1, { { 0.3, std::sqrt(1.0 - 0.09) } } },
            { "touching end (line ending on y = 0.5)", 0, 5, { { 1.0, 0.5 } } },
        };
        for (const Case& Entry : Cases)
        {
            const std::vector<SegmentCrossing> Found = IntersectProfileSegments(Segment(Sketch, Entry.Target), Segment(Sketch, Entry.Cutter), 1.0e-9);
            const double Worst = WorstMatch(Found, Entry.Expected);
            Report(AnalyticGate, Entry.Name, std::to_string(Found.size()) + " crossing(s) " + Points(Found) + " · worst " + Scientific(Worst), Worst, Worst <= 1.0e-12);
        }
    }

    void ValidateSpline()
    {
        SketchModel Sketch;
        std::vector<int> Through;
        for (Vector2 Point : { Vector2{ 0.0, 0.0 }, Vector2{ 1.0, 1.0 }, Vector2{ 2.0, -0.6 }, Vector2{ 3.0, 0.8 }, Vector2{ 4.0, 0.1 } }) Through.push_back(AppendSketchPoint(Sketch, Point));
        AppendSketchSpline(Sketch, Through, SketchSplineForm::CubicSpline, 1);
        const int Left = AppendSketchPoint(Sketch, { -0.5, 0.3 });
        const int Right = AppendSketchPoint(Sketch, { 4.5, 0.3 });
        AppendSketchLine(Sketch, Left, Right, 1);
        const ProfileSegment Spline = Segment(Sketch, 0), Line = Segment(Sketch, 1);
        int SignChanges = 0;
        double Previous = EvaluateProfileSegment(Spline, 0.0).Y - 0.3;
        for (int Step = 1; Step <= 20000; ++Step)
        {
            const double Value = EvaluateProfileSegment(Spline, Step / 20000.0).Y - 0.3;
            if ((Value > 0.0) != (Previous > 0.0)) ++SignChanges;
            Previous = Value;
        }
        for (int Pass = 0; Pass < 2; ++Pass)
        {
            const bool LineFirst = Pass == 1;
            const std::vector<SegmentCrossing> Found = LineFirst ? IntersectProfileSegments(Line, Spline, 1.0e-9) : IntersectProfileSegments(Spline, Line, 1.0e-9);
            double Worst = 0.0;
            for (const SegmentCrossing& Crossing : Found)
            {
                const Vector2 OnSpline = EvaluateProfileSegment(Spline, LineFirst ? Crossing.CutterFraction : Crossing.TargetFraction);
                Worst = std::max({ Worst, std::fabs(OnSpline.Y - 0.3), Distance(OnSpline, Crossing.Position) });
            }
            const bool Passed = static_cast<int>(Found.size()) == SignChanges && SignChanges == 4 && Worst <= 1.0e-12;
            Report(SplineGate, LineFirst ? "line × spline" : "spline × line", std::to_string(Found.size()) + " crossing(s) vs " + std::to_string(SignChanges) + " sign changes · " + Points(Found)
                   + " · off y = 0.3 " + Scientific(Worst), Worst, Passed);
        }
    }

    // Rectangle (0,0)–(4,2) as four lines + a vertical line x = 1 from y = −1 to 3.
    [[nodiscard]] SketchModel CrossedRectangle()
    {
        SketchModel Sketch;
        const int P0 = AppendSketchPoint(Sketch, { 0.0, 0.0 }), P1 = AppendSketchPoint(Sketch, { 4.0, 0.0 });
        const int P2 = AppendSketchPoint(Sketch, { 4.0, 2.0 }), P3 = AppendSketchPoint(Sketch, { 0.0, 2.0 });
        AppendSketchLine(Sketch, P0, P1, 1);                                                 // 0 bottom
        AppendSketchLine(Sketch, P1, P2, 2);                                                 // 1 right
        AppendSketchLine(Sketch, P2, P3, 3);                                                 // 2 top (right → left)
        AppendSketchLine(Sketch, P3, P0, 4);                                                 // 3 left
        const int Low = AppendSketchPoint(Sketch, { 1.0, -1.0 }), High = AppendSketchPoint(Sketch, { 1.0, 3.0 });
        AppendSketchLine(Sketch, Low, High, 5);                                              // 4 divider
        return Sketch;
    }

    [[nodiscard]] int FindCurve(const SketchModel& Sketch, int Identifier)
    {
        for (int Index = 0; Index < static_cast<int>(Sketch.Curves.size()); ++Index)
            if (Sketch.Curves[Index].Kind == SketchCurveKind::Line && Sketch.Curves[Index].Identifier == Identifier) return Index;
        return -1;
    }

    void ValidateRectangle()
    {
        SketchModel Sketch = CrossedRectangle();
        struct Pick { const char* Name; int Identifier; Vector2 Point; int Pieces; Vector2 From; Vector2 To; };
        const std::vector<Pick> Picks = {
            { "divider overhang above (1, 2.5)", 5, { 1.0, 2.5 }, 1, { 1.0, 2.0 }, { 1.0, 3.0 } },
            { "divider overhang below (1, −0.5)", 5, { 1.0, -0.5 }, 1, { 1.0, -1.0 }, { 1.0, 0.0 } },
            { "top side left of the divider (0.5, 2)", 3, { 0.5, 2.0 }, 1, { 1.0, 2.0 }, { 0.0, 2.0 } },
            { "bottom side left of the divider (0.5, 0)", 1, { 0.5, 0.0 }, 1, { 0.0, 0.0 }, { 1.0, 0.0 } },
            { "left side, no crossing left (0, 1)", 4, { 0.0, 1.0 }, 0, { 0.0, 2.0 }, { 0.0, 0.0 } },
        };
        for (const Pick& Entry : Picks)
        {
            const int Curve = FindCurve(Sketch, Entry.Identifier);
            const SketchTrimOutcome Outcome = TrimSketchCurve(Sketch, Curve, Entry.Point);
            const double Ends = Outcome.Removed.empty() ? 1.0 : std::max(Distance(Outcome.Removed.front(), Entry.From), Distance(Outcome.Removed.back(), Entry.To));
            const bool Passed = Curve >= 0 && Outcome.Refusal.empty() && Outcome.Pieces == Entry.Pieces && Ends <= 1.0e-12;
            Report(RectangleGate, std::string("T on ") + Entry.Name, "Line" + std::to_string(Entry.Identifier) + " · " + std::to_string(Outcome.Crossings) + " crossing(s) · pieces left "
                   + std::to_string(Outcome.Pieces) + " · removed ends off " + Scientific(Ends) + (Outcome.Refusal.empty() ? "" : " · " + Outcome.Refusal), Ends, Passed);
            Sketch = Outcome.Sketch;
        }
        const SketchProfile Profile = ExtractSketchProfile(Sketch);
        const double AreaError = std::fabs(Profile.Area - 6.0);
        Report(RectangleGate, "Profile after the picks", std::to_string(Sketch.Curves.size()) + " curves · " + std::to_string(Profile.Loops.size()) + " loop · area " + Fixed(Profile.Area) + " (3 × 2)"
               + (Profile.Refusal.empty() ? "" : " · " + Profile.Refusal), AreaError, Profile.Refusal.empty() && Profile.Loops.size() == 1 && Sketch.Curves.size() == 4 && AreaError <= 1.0e-12);
        SketchModel Solved = Sketch;
        const SketchSolveOutcome Solve = SolveSketch(Solved);
        const double Move = LargestMove(Sketch, Solved);
        Report(RectangleGate, "Solve after the trims", std::string(Solve.Converged ? "converged" : "not converged") + " · " + std::to_string(CountKind(Sketch, SketchConstraintKind::PointOnLine))
               + " PointOnLine pins · largest move " + Scientific(Move), Move, Solve.Converged && Move <= 1.0e-12 && CountKind(Sketch, SketchConstraintKind::PointOnLine) == 4);
    }

    void ValidateCircle()
    {
        SketchModel Sketch;
        const int Centre = AppendSketchPoint(Sketch, { 0.0, 0.0 });
        AppendSketchCircle(Sketch, Centre, 1.0, 1);
        const int Left = AppendSketchPoint(Sketch, { -2.0, 0.0 }), Right = AppendSketchPoint(Sketch, { 2.0, 0.0 });
        AppendSketchLine(Sketch, Left, Right, 1);
        AppendSketchConstraint(Sketch, SketchConstraintKind::Radius, 0, -1, -1, 1.0, "R");
        const SketchTrimOutcome Top = TrimSketchCurve(Sketch, 0, { 0.0, 1.0 });
        const SketchCurve& Arc = Top.Sketch.Curves[0];
        const double ArcEnds = Arc.Kind == SketchCurveKind::Arc ? std::max(Distance(Top.Sketch.Points[Arc.Start].Position, { -1.0, 0.0 }), Distance(Top.Sketch.Points[Arc.End].Position, { 1.0, 0.0 })) : 1.0;
        const ProfileSegment Lower = Segment(Top.Sketch, 0);
        const double Bottom = Distance(EvaluateProfileSegment(Lower, 0.5), { 0.0, -1.0 });
        Report(CircleGate, "T on the top half (0, 1)", std::string(Arc.Kind == SketchCurveKind::Arc ? "arc" : "not an arc") + " from (−1, 0) to (1, 0) counter-clockwise · ends off " + Scientific(ArcEnds)
               + " · midpoint at (0, −1) off " + Scientific(Bottom) + " · removed sweep " + Fixed(Top.ToFraction - Top.FromFraction) + " turn · Radius kept " + std::to_string(CountKind(Top.Sketch, SketchConstraintKind::Radius)),
               std::max(ArcEnds, Bottom), Top.Refusal.empty() && Top.Pieces == 1 && std::max(ArcEnds, Bottom) <= 1.0e-12 && std::fabs(Top.ToFraction - Top.FromFraction - 0.5) <= 1.0e-12
               && CountKind(Top.Sketch, SketchConstraintKind::Radius) == 1);
        const SketchTrimOutcome LeftEnd  = TrimSketchCurve(Top.Sketch, 1, { -1.5, 0.0 });
        const SketchTrimOutcome RightEnd = TrimSketchCurve(LeftEnd.Sketch, 1, { 1.5, 0.0 });
        const SketchProfile Profile = ExtractSketchProfile(RightEnd.Sketch);
        const double AreaError = std::fabs(Profile.Area - Pi / 2.0);
        Report(CircleGate, "T on both overhangs → D profile", std::to_string(LeftEnd.Crossings) + " + " + std::to_string(RightEnd.Crossings) + " crossings · area " + Fixed(Profile.Area) + " vs π/2 "
               + Fixed(Pi / 2.0) + (Profile.Refusal.empty() ? "" : " · " + Profile.Refusal), AreaError, LeftEnd.Refusal.empty() && RightEnd.Refusal.empty() && Profile.Refusal.empty() && AreaError <= 1.0e-12);
        SketchModel Solved = RightEnd.Sketch;
        const SketchSolveOutcome Solve = SolveSketch(Solved);
        const double Move = LargestMove(RightEnd.Sketch, Solved);
        Report(CircleGate, "Solve after the trims", std::string(Solve.Converged ? "converged" : "not converged") + " · PointOnCircle pins " + std::to_string(CountKind(RightEnd.Sketch, SketchConstraintKind::PointOnCircle))
               + " · PointOnLine pins " + std::to_string(CountKind(RightEnd.Sketch, SketchConstraintKind::PointOnLine)) + " · largest move " + Scientific(Move), Move, Solve.Converged && Move <= 1.0e-12);
    }

    void ValidateConstraints()
    {
        SketchModel Sketch;
        const int A = AppendSketchPoint(Sketch, { 0.0, 0.0 }), B = AppendSketchPoint(Sketch, { 6.0, 0.0 });
        const int C = AppendSketchPoint(Sketch, { 2.0, -1.0 }), D = AppendSketchPoint(Sketch, { 2.0, 1.0 });
        const int E = AppendSketchPoint(Sketch, { 4.0, -1.0 }), F = AppendSketchPoint(Sketch, { 4.0, 1.0 });
        AppendSketchLine(Sketch, A, B, 1);                                                   // 0: horizontal, length 6
        AppendSketchLine(Sketch, C, D, 2);                                                   // 1: x = 2
        AppendSketchLine(Sketch, E, F, 3);                                                   // 2: x = 4
        AppendSketchConstraint(Sketch, SketchConstraintKind::Horizontal, 0);
        AppendSketchConstraint(Sketch, SketchConstraintKind::Length, 0, -1, -1, 6.0, "Span");
        AppendSketchConstraint(Sketch, SketchConstraintKind::Vertical, 2);
        const SketchTrimOutcome Split = TrimSketchCurve(Sketch, 0, { 3.0, 0.0 });
        const int Lines = static_cast<int>(Split.Sketch.Curves.size());
        const bool Copied = CountKind(Split.Sketch, SketchConstraintKind::Horizontal) == 2 && CountKind(Split.Sketch, SketchConstraintKind::Length) == 0;
        const bool Pinned = CountKind(Split.Sketch, SketchConstraintKind::PointOnLine) == 2;
        const bool Named  = Lines == 4 && Split.Sketch.Curves[3].Identifier == 4 && SketchCurveName(Split.Sketch.Curves[3]) == "Line4";
        const double Ends = Lines == 4 ? std::max(Distance(Split.Sketch.Points[Split.Sketch.Curves[0].End].Position, { 2.0, 0.0 }), Distance(Split.Sketch.Points[Split.Sketch.Curves[3].Start].Position, { 4.0, 0.0 })) : 1.0;
        Report(ConstraintGate, "Split Line1 between x = 2 and x = 4", std::to_string(Split.Pieces) + " pieces · new curve " + (Lines == 4 ? SketchCurveName(Split.Sketch.Curves[3]) : std::string("–"))
               + " · Horizontal " + std::to_string(CountKind(Split.Sketch, SketchConstraintKind::Horizontal)) + " · Length " + std::to_string(CountKind(Split.Sketch, SketchConstraintKind::Length))
               + " · PointOnLine " + std::to_string(CountKind(Split.Sketch, SketchConstraintKind::PointOnLine)) + " · ends off " + Scientific(Ends), Ends,
               Split.Pieces == 2 && Copied && Pinned && Named && Ends <= 1.0e-12);
        SketchModel Erased = Split.Sketch;
        EraseSketchCurve(Erased, 1);                                                         // Line2 goes: its pin drops, Line3 moves to 1
        bool Shifted = Erased.Curves.size() == 3 && CountKind(Erased, SketchConstraintKind::PointOnLine) == 1;
        for (const SketchConstraint& Constraint : Erased.Constraints)
        {
            if (Constraint.Kind == SketchConstraintKind::Vertical) Shifted = Shifted && Constraint.First == 1 && Erased.Curves[1].Identifier == 3;
            if (Constraint.Kind == SketchConstraintKind::PointOnLine) Shifted = Shifted && Constraint.Second == 1;
            if (Constraint.Kind == SketchConstraintKind::Horizontal) Shifted = Shifted && (Constraint.First == 0 || Constraint.First == 2);
        }
        Report(ConstraintGate, "Erase Line2: its pin drops, later references shift", std::to_string(Erased.Curves.size()) + " curves · " + std::to_string(Erased.Constraints.size()) + " constraints", Shifted ? 0.0 : 1.0, Shifted);
        SketchModel Solved = Split.Sketch;
        const SketchSolveOutcome Solve = SolveSketch(Solved);
        const double Move = LargestMove(Split.Sketch, Solved);
        Report(ConstraintGate, "Solve after the split", std::string(Solve.Converged ? "converged" : "not converged") + " · largest move " + Scientific(Move), Move, Solve.Converged && Move <= 1.0e-12);
    }

    void ValidateForeign()
    {
        SketchModel Sketch;
        const int A = AppendSketchPoint(Sketch, { 0.0, 0.0 }), B = AppendSketchPoint(Sketch, { 3.0, 0.0 });
        AppendSketchLine(Sketch, A, B, 1);
        SketchModel Other;
        const int Centre = AppendSketchPoint(Other, { 1.5, 0.0 });
        AppendSketchCircle(Other, Centre, 0.5, 1);
        const SketchTrimOutcome Outcome = TrimSketchCurve(Sketch, 0, { 1.5, 0.0 }, { Segment(Other, 0) });
        const double Ends = Outcome.Sketch.Curves.size() == 2 ? std::max(Distance(Outcome.Sketch.Points[Outcome.Sketch.Curves[0].End].Position, { 1.0, 0.0 }),
                                                                         Distance(Outcome.Sketch.Points[Outcome.Sketch.Curves[1].Start].Position, { 2.0, 0.0 })) : 1.0;
        Report(ForeignGate, "Line (0,0)–(3,0) cut by a circle of another sketch", std::to_string(Outcome.Crossings) + " crossings · " + std::to_string(Outcome.Pieces) + " pieces · ends off " + Scientific(Ends)
               + " · pins " + std::to_string(Outcome.Sketch.Constraints.size()), Ends, Outcome.Pieces == 2 && Ends <= 1.0e-12 && Outcome.Sketch.Constraints.empty());
    }

    void ValidateRefusals()
    {
        SketchModel Lone;
        const int A = AppendSketchPoint(Lone, { 0.0, 0.0 }), B = AppendSketchPoint(Lone, { 1.0, 0.0 });
        AppendSketchLine(Lone, A, B, 1);
        const SketchTrimOutcome Whole = TrimSketchCurve(Lone, 0, { 0.5, 0.0 });
        Report(RefuseGate, "Lone line: erased whole", std::to_string(Whole.Sketch.Curves.size()) + " curves left · pieces " + std::to_string(Whole.Pieces), 0.0, Whole.Refusal.empty() && Whole.Sketch.Curves.empty() && Whole.Pieces == 0);

        SketchModel Once;
        const int Centre = AppendSketchPoint(Once, { 0.0, 0.0 });
        AppendSketchCircle(Once, Centre, 1.0, 1);
        const int Inner = AppendSketchPoint(Once, { 0.0, 0.0 }), Outer = AppendSketchPoint(Once, { 2.0, 0.0 });
        AppendSketchLine(Once, Inner, Outer, 1);
        const SketchTrimOutcome Single = TrimSketchCurve(Once, 0, { 0.0, 1.0 });
        Report(RefuseGate, "Circle crossed once: erased whole", std::to_string(Single.Crossings) + " crossing · " + std::to_string(Single.Sketch.Curves.size()) + " curve left ("
               + (Single.Sketch.Curves.empty() ? "–" : SketchCurveName(Single.Sketch.Curves[0])) + ")", 0.0, Single.Refusal.empty() && Single.Crossings == 1 && Single.Sketch.Curves.size() == 1
               && Single.Sketch.Curves[0].Kind == SketchCurveKind::Line);

        SketchModel Wavy;
        std::vector<int> Through;
        for (Vector2 Point : { Vector2{ 0.0, 0.0 }, Vector2{ 1.0, 1.0 }, Vector2{ 2.0, -1.0 }, Vector2{ 3.0, 0.0 } }) Through.push_back(AppendSketchPoint(Wavy, Point));
        AppendSketchSpline(Wavy, Through, SketchSplineForm::Hermite, 1);
        const int Left = AppendSketchPoint(Wavy, { -1.0, 0.2 }), Right = AppendSketchPoint(Wavy, { 4.0, 0.2 });
        AppendSketchLine(Wavy, Left, Right, 1);
        const SketchTrimOutcome Spline = TrimSketchCurve(Wavy, 0, { 1.0, 1.0 });
        Report(RefuseGate, "Spline crossed: middle piece trimmed cleanly",
               std::to_string(Spline.Crossings) + " crossings · pieces " + std::to_string(Spline.Pieces), 0.0,
               Spline.Refusal.empty() && Spline.Crossings >= 2 && Spline.Pieces >= 1);
        const SketchTrimOutcome Line = TrimSketchCurve(Wavy, 1, { -0.5, 0.2 });
        Report(RefuseGate, "Line crossing the spline: end piece off", std::to_string(Line.Crossings) + " crossings · pieces " + std::to_string(Line.Pieces), 0.0, Line.Refusal.empty() && Line.Pieces == 1 && Line.Crossings >= 2);

        const SketchTrimOutcome Missing = TrimSketchCurve(Lone, 7, { 0.0, 0.0 });
        Report(RefuseGate, "Unknown curve index: refused", Missing.Refusal, 0.0, !Missing.Refusal.empty() && Missing.Sketch.Curves.size() == 1);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ValidateAnalytic();
    ValidateSpline();
    ValidateRectangle();
    ValidateCircle();
    ValidateConstraints();
    ValidateForeign();
    ValidateRefusals();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Sketch trim validation\n\n%s Trim / split T4: curve trim (Plasticity T) — the piece under the pick, between neighbouring crossings, comes off\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
