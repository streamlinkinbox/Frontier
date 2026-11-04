/*====================================================================================================================================
                                                    SKETCHEDITINGVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Editing phase E1: offset-curve (Plasticity O) exact on line / arc / circle chains, join-curves (Plasticity J) merging

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Sketch/SketchEditing.h"
#include "Engine/GeometryProcessing/Sketch/SketchProfile.h"
#include "Engine/GeometryProcessing/Sketch/SketchSolver.h"

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
        const char* Code     = "";                                                           // [-] - O1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally RectangleGate = { "O1", "Rectangle 2 × 1 offset ±0.25: mitred corners at (±1.25, ±0.75) / (±0.75, ±0.25), areas 3.75 / 0.75, drawn clockwise too", "≤ 10⁻¹²" };
    GateTally SlotGate      = { "O2", "Slot (2 lines + 2 tangent arcs, L 2, r 0.3) offset ±0.1: area 2L(r ± d) + π(r ± d)², joins stay tangent", "≤ 10⁻¹²" };
    GateTally CircleGate    = { "O3", "Circle r 0.5 → r 0.7 / 0.3 (concentric, area π r²); a hole circle inside a rectangle shrinks while the rectangle grows", "≤ 10⁻¹²" };
    GateTally OpenGate      = { "O4", "Open L chain offset 0.2 to the left: every offset piece at distance 0.2 from its source, inner corner mitred at (0.8, 0.2)", "≤ 10⁻¹²" };
    GateTally ExactGate     = { "O5", "Line–arc corner (not tangent) and arc–arc chain: dense samples of every offset piece at |d| from its source carrier, profile valid, solve keeps it", "≤ 10⁻¹²" };
    GateTally JoinGate      = { "O6", "Join: square split in two L sketches, the second rotated 90° / mirrored by its placement → one closed profile of area 1, ends welded, frame constraints re-expressed or dropped", "≤ 10⁻¹²" };
    GateTally ContactGate   = { "O7", "Contact: crossing, end-touching and disjoint sketches → yes, yes, no", "exact" };
    GateTally RefuseGate    = { "O8", "Refusals: spline, branching junction, zero distance, circle radius vanishing, rectangle turned inside out, skewed placement", "all named" };

    std::vector<GateTally*> AllGates() { return { &RectangleGate, &SlotGate, &CircleGate, &OpenGate, &ExactGate, &JoinGate, &ContactGate, &RefuseGate }; }

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

    [[nodiscard]] SketchModel Polygon(const std::vector<Vector2>& Points, bool Closed)
    {
        SketchModel Model;
        for (const Vector2& Point : Points) AppendSketchPoint(Model, Point);
        const int Count = static_cast<int>(Points.size());
        for (int Index = 0; Index + 1 < Count; ++Index) AppendSketchLine(Model, Index, Index + 1, Index + 1);
        if (Closed) AppendSketchLine(Model, Count - 1, 0, Count);
        return Model;
    }

    // Slot of length Span between cap centres (0, 0) and (Span, 0), radius Radius.
    [[nodiscard]] SketchModel Slot(double Span, double Radius)
    {
        SketchModel Model;
        const int A = AppendSketchPoint(Model, { 0.0, -Radius }), B = AppendSketchPoint(Model, { Span, -Radius });
        const int C = AppendSketchPoint(Model, { Span, Radius }),  D = AppendSketchPoint(Model, { 0.0, Radius });
        const int Left = AppendSketchPoint(Model, { 0.0, 0.0 }),   Right = AppendSketchPoint(Model, { Span, 0.0 });
        AppendSketchLine(Model, A, B, 1);
        AppendSketchArc(Model, Right, B, C, 2);
        AppendSketchLine(Model, C, D, 3);
        AppendSketchArc(Model, Left, D, A, 4);
        return Model;
    }

    [[nodiscard]] double ProfileArea(const SketchModel& Sketch, std::string& Refusal)
    {
        const SketchProfile Profile = ExtractSketchProfile(Sketch);
        Refusal = Profile.Refusal;
        return Profile.Area;
    }

    // Largest | distance to Source curve − Expected | over dense samples of every offset piece (interior only).
    [[nodiscard]] double WorstOffsetGap(const SketchModel& Source, const SketchModel& Offset, double Expected)
    {
        std::vector<ProfileSegment> Sources;
        for (size_t Index = 0; Index < Source.Curves.size(); ++Index)
        {
            std::string Refusal;
            Sources.push_back(ResolveSketchCurveSegment(Source, static_cast<int>(Index), 1.0e-12, Refusal));
        }
        // 📝 Mitred corners extend pieces past their source ends, so the measure is the distance to the source carriers
        //    (infinite line / full circle): the best |distance − Expected| over them.
        auto Gap = [&](Vector2 Point) -> double
        {
            double Best = 1.0e300;
            for (const ProfileSegment& Piece : Sources)
            {
                if (Piece.Kind == ProfileSegmentKind::Line)
                {
                    const Vector2 Direction = Piece.End - Piece.Start;
                    Best = std::min(Best, std::fabs(std::fabs(Cross(Direction, Point - Piece.Start)) / Length(Direction) - Expected));
                    continue;
                }
                Best = std::min(Best, std::fabs(std::fabs(Length(Point - Piece.Centre) - Piece.Radius) - Expected));
            }
            return Best;
        };
        double Worst = 0.0;
        for (size_t Index = 0; Index < Offset.Curves.size(); ++Index)
        {
            std::string Refusal;
            const ProfileSegment Piece = ResolveSketchCurveSegment(Offset, static_cast<int>(Index), 1.0e-12, Refusal);
            if (!Refusal.empty()) return 1.0;
            for (int Sample = 1; Sample < 64; ++Sample) Worst = std::max(Worst, Gap(EvaluateProfileSegment(Piece, Sample / 64.0)));
        }
        return Worst;
    }

    [[nodiscard]] double NearestPoint(const SketchModel& Sketch, Vector2 Target)
    {
        double Best = 1.0e300;
        for (const SketchPoint& Point : Sketch.Points) Best = std::min(Best, Distance(Point.Position, Target));
        return Best;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                GATES
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateRectangle()
    {
        const std::vector<Vector2> Corners = { { -1.0, -0.5 }, { 1.0, -0.5 }, { 1.0, 0.5 }, { -1.0, 0.5 } };
        for (int Direction = 0; Direction < 2; ++Direction)
        {
            std::vector<Vector2> Drawn = Corners;
            if (Direction == 1) std::reverse(Drawn.begin(), Drawn.end());                    // Clockwise drawing: offset still grows outward
            const SketchModel Rectangle = Polygon(Drawn, true);
            for (double Offset : { 0.25, -0.25 })
            {
                const SketchOffsetOutcome Outcome = OffsetSketchCurves(Rectangle, Offset);
                std::string Refusal;
                const double Area     = Outcome.Refusal.empty() ? ProfileArea(Outcome.Sketch, Refusal) : 0.0;
                const double Expected = (2.0 + 2.0 * Offset) * (1.0 + 2.0 * Offset);
                const double HalfX = 1.0 + Offset, HalfY = 0.5 + Offset;
                double Corner = 0.0;
                for (Vector2 Target : { Vector2{ -HalfX, -HalfY }, Vector2{ HalfX, -HalfY }, Vector2{ HalfX, HalfY }, Vector2{ -HalfX, HalfY } }) Corner = std::max(Corner, NearestPoint(Outcome.Sketch, Target));
                const double Error = std::max(std::fabs(Area - Expected), Corner);
                Report(RectangleGate, std::string(Direction ? "Clockwise" : "Counter-clockwise") + " rectangle, d " + Fixed(Offset).substr(0, 5),
                       "area " + Fixed(Area) + " vs " + Fixed(Expected) + " · corner error " + Scientific(Corner) + " · " + std::to_string(Outcome.Sketch.Curves.size()) + " lines" + (Outcome.Refusal.empty() ? "" : " · " + Outcome.Refusal),
                       Error, Outcome.Refusal.empty() && Refusal.empty() && Error <= 1.0e-12 && Outcome.Sketch.Curves.size() == 4 && Outcome.Closed == 1);
            }
        }
    }

    void ValidateSlot()
    {
        const double Span = 2.0, Radius = 0.3;
        const SketchModel Source = Slot(Span, Radius);
        for (double Offset : { 0.1, -0.1 })
        {
            const SketchOffsetOutcome Outcome = OffsetSketchCurves(Source, Offset);
            std::string Refusal;
            const double Area     = Outcome.Refusal.empty() ? ProfileArea(Outcome.Sketch, Refusal) : 0.0;
            const double Grown    = Radius + Offset;
            const double Expected = 2.0 * Span * Grown + Pi * Grown * Grown;
            const double Gap      = Outcome.Refusal.empty() ? WorstOffsetGap(Source, Outcome.Sketch, std::fabs(Offset)) : 1.0;
            const double Error    = std::max(std::fabs(Area - Expected), Gap);
            Report(SlotGate, "Slot, d " + Fixed(Offset).substr(0, 5), "area " + Fixed(Area) + " vs " + Fixed(Expected) + " · distance error " + Scientific(Gap) + (Outcome.Refusal.empty() ? "" : " · " + Outcome.Refusal),
                   Error, Outcome.Refusal.empty() && Refusal.empty() && Error <= 1.0e-12 && Outcome.Sketch.Curves.size() == 4);
        }
    }

    void ValidateCircle()
    {
        SketchModel Circle;
        AppendSketchCircle(Circle, AppendSketchPoint(Circle, { 0.2, -0.1 }), 0.5, 1);
        for (double Offset : { 0.2, -0.2 })
        {
            const SketchOffsetOutcome Outcome = OffsetSketchCurves(Circle, Offset);
            std::string Refusal;
            const double Area     = Outcome.Refusal.empty() ? ProfileArea(Outcome.Sketch, Refusal) : 0.0;
            const double Expected = Pi * (0.5 + Offset) * (0.5 + Offset);
            const double Centre   = Outcome.Sketch.Points.empty() ? 1.0 : Distance(Outcome.Sketch.Points.front().Position, { 0.2, -0.1 });
            const double Error    = std::max(std::fabs(Area - Expected), Centre);
            Report(CircleGate, "Circle r 0.5, d " + Fixed(Offset).substr(0, 5), "area " + Fixed(Area) + " vs " + Fixed(Expected) + " · centre moved " + Scientific(Centre), Error,
                   Outcome.Refusal.empty() && Refusal.empty() && Error <= 1.0e-12);
        }
        // A plate with a hole: the outer rectangle grows, the hole (a closed chain of its own) shrinks → area grows by both.
        SketchModel Plate = Polygon({ { -1.0, -1.0 }, { 1.0, -1.0 }, { 1.0, 1.0 }, { -1.0, 1.0 } }, true);
        AppendSketchCircle(Plate, AppendSketchPoint(Plate, { 0.0, 0.0 }), 0.5, 5);
        const SketchOffsetOutcome Outcome = OffsetSketchCurves(Plate, 0.1);
        std::string Refusal;
        const double Area     = Outcome.Refusal.empty() ? ProfileArea(Outcome.Sketch, Refusal) : 0.0;
        const double Expected = 2.2 * 2.2 - Pi * 0.6 * 0.6;
        const double Error    = std::fabs(Area - Expected);
        Report(CircleGate, "Plate with hole, d 0.1", "net area " + Fixed(Area) + " vs 2.2² − π·0.6² = " + Fixed(Expected) + " · " + std::to_string(Outcome.Chains) + " chains" + (Outcome.Refusal.empty() ? "" : " · " + Outcome.Refusal),
               Error, Outcome.Refusal.empty() && Refusal.empty() && Error <= 1.0e-12 && Outcome.Chains == 2);
    }

    void ValidateOpen()
    {
        // L: (0, 0) → (1, 0) → (1, 1); left of the first piece is +y, so the offset runs inside the corner.
        const SketchModel Chain = Polygon({ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 } }, false);
        const SketchOffsetOutcome Outcome = OffsetSketchCurves(Chain, 0.2);
        const double Gap    = Outcome.Refusal.empty() ? WorstOffsetGap(Chain, Outcome.Sketch, 0.2) : 1.0;
        const double Corner = NearestPoint(Outcome.Sketch, { 0.8, 0.2 });
        const double Ends   = std::max(NearestPoint(Outcome.Sketch, { 0.0, 0.2 }), NearestPoint(Outcome.Sketch, { 0.8, 1.0 }));
        const double Error  = std::max({ Gap, Corner, Ends });
        Report(OpenGate, "L chain, d 0.2", "distance error " + Scientific(Gap) + " · corner " + Scientific(Corner) + " · ends " + Scientific(Ends) + " · closed " + std::to_string(Outcome.Closed), Error,
               Outcome.Refusal.empty() && Error <= 1.0e-12 && Outcome.Closed == 0 && Outcome.Sketch.Curves.size() == 2);
        const SketchOffsetOutcome Right = OffsetSketchCurves(Chain, -0.2);
        const double Outside = std::max(NearestPoint(Right.Sketch, { 1.2, -0.2 }), WorstOffsetGap(Chain, Right.Sketch, 0.2));
        Report(OpenGate, "L chain, d −0.2", "outer mitre (1.2, −0.2) error " + Scientific(Outside), Outside, Right.Refusal.empty() && Outside <= 1.0e-12);
    }

    void ValidateExact()
    {
        // D shape: a chord line closed by an arc meeting it at 60° (non-tangent corners: line–circle intersection).
        SketchModel Shape;
        const double Half = std::sqrt(3.0) / 2.0;
        const int Centre = AppendSketchPoint(Shape, { 0.0, 0.0 });
        const int Start  = AppendSketchPoint(Shape, { Half, -0.5 });
        const int End    = AppendSketchPoint(Shape, { Half, 0.5 });
        AppendSketchArc(Shape, Centre, End, Start, 1);                                       // Long way round: 300°
        AppendSketchLine(Shape, Start, End, 2);
        for (double Offset : { 0.15, -0.15 })
        {
            const SketchOffsetOutcome Outcome = OffsetSketchCurves(Shape, Offset);
            SketchModel Solved = Outcome.Sketch;
            const SketchModel Before = Solved;
            const SketchSolveOutcome Solve = SolveSketch(Solved);
            double Moved = 0.0;
            for (size_t Index = 0; Index < Before.Points.size(); ++Index) Moved = std::max(Moved, Distance(Before.Points[Index].Position, Solved.Points[Index].Position));
            std::string Refusal;
            const double Area = Outcome.Refusal.empty() ? ProfileArea(Outcome.Sketch, Refusal) : 0.0;
            const double Gap  = Outcome.Refusal.empty() ? WorstOffsetGap(Shape, Outcome.Sketch, std::fabs(Offset)) : 1.0;
            const double Error = std::max(Gap, Moved);
            Report(ExactGate, "Arc 300° + chord, d " + Fixed(Offset).substr(0, 5), "distance error " + Scientific(Gap) + " · area " + Fixed(Area) + " · solve moved " + Scientific(Moved) + (Outcome.Refusal.empty() ? "" : " · " + Outcome.Refusal),
                   Error, Outcome.Refusal.empty() && Refusal.empty() && Solve.Converged && Error <= 1.0e-12 && Area > 0.0);
        }
        // S chain of two arcs meeting tangentially (reverse curvature) — the inner one shrinks, the outer one grows.
        SketchModel Wave;
        const int A = AppendSketchPoint(Wave, { -1.0, 0.0 }), B = AppendSketchPoint(Wave, { 0.0, 0.0 }), C = AppendSketchPoint(Wave, { 1.0, 0.0 });
        const int First = AppendSketchPoint(Wave, { -0.5, 0.0 }), Second = AppendSketchPoint(Wave, { 0.5, 0.0 });
        AppendSketchArc(Wave, First, B, A, 1);                                               // Upper half from B to A (CCW)
        AppendSketchArc(Wave, Second, B, C, 2);                                              // Lower half from B to C (CCW)
        const SketchOffsetOutcome Outcome = OffsetSketchCurves(Wave, 0.1);
        const double Gap = Outcome.Refusal.empty() ? WorstOffsetGap(Wave, Outcome.Sketch, 0.1) : 1.0;
        Report(ExactGate, "S wave of two tangent arcs, d 0.1", "distance error " + Scientific(Gap) + " · " + std::to_string(Outcome.Sketch.Curves.size()) + " arcs" + (Outcome.Refusal.empty() ? "" : " · " + Outcome.Refusal), Gap,
               Outcome.Refusal.empty() && Gap <= 1.0e-12 && Outcome.Sketch.Curves.size() == 2);
    }

    void ValidateJoin()
    {
        // Unit square (0, 0) – (1, 1): bottom + right edges in the base sketch; top + left in a second sketch whose own
        // coordinates are rotated 90° (its x̂ lands on the base ŷ) or mirrored, with Horizontal / Vertical constraints.
        SketchModel Base = Polygon({ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 } }, false);
        AppendSketchConstraint(Base, SketchConstraintKind::Horizontal, 0);
        struct Case { const char* Name; SketchPlacement Placement; };
        const Case Cases[] = {
            { "Identity", { { 0.0, 0.0 }, { 1.0, 0.0 }, { 0.0, 1.0 } } },
            { "Rotated 90°", { { 0.0, 0.0 }, { 0.0, 1.0 }, { -1.0, 0.0 } } },
            { "Mirrored (x ↔ y)", { { 0.0, 0.0 }, { 0.0, 1.0 }, { 1.0, 0.0 } } },
            { "Rotated 30°", { { 0.0, 0.0 }, { std::cos(Pi / 6.0), std::sin(Pi / 6.0) }, { -std::sin(Pi / 6.0), std::cos(Pi / 6.0) } } },
        };
        for (const Case& Each : Cases)
        {
            // Build the second sketch in its own coordinates so that, once placed, it is (1, 1) → (0, 1) → (0, 0).
            const Vector2 X = Each.Placement.XAxis, Y = Each.Placement.YAxis;
            auto Local = [&](Vector2 World) { return Vector2{ Dot(World - Each.Placement.Origin, X), Dot(World - Each.Placement.Origin, Y) }; };
            SketchModel Second = Polygon({ Local({ 1.0, 1.0 }), Local({ 0.0, 1.0 }), Local({ 0.0, 0.0 }) }, false);
            // In world terms line 0 is horizontal and line 1 vertical; express that in the second sketch's frame.
            const bool Swapped = std::fabs(X.Y) > 0.5;
            const bool Aligned = std::fabs(std::fabs(X.X) - 1.0) < 1.0e-12 || std::fabs(std::fabs(X.Y) - 1.0) < 1.0e-12;
            if (Aligned)
            {
                AppendSketchConstraint(Second, Swapped ? SketchConstraintKind::Vertical : SketchConstraintKind::Horizontal, 0);
                AppendSketchConstraint(Second, Swapped ? SketchConstraintKind::Horizontal : SketchConstraintKind::Vertical, 1);
            }
            else AppendSketchConstraint(Second, SketchConstraintKind::Horizontal, 0);       // Not horizontal once rotated 30°: must be dropped
            const SketchJoinOutcome Joined = JoinSketchModels(Base, Second, Each.Placement, 1.0e-9);
            std::string Refusal;
            const double Area  = Joined.Refusal.empty() ? ProfileArea(Joined.Sketch, Refusal) : 0.0;
            SketchModel  Solved = Joined.Sketch;
            const SketchSolveOutcome Solve = SolveSketch(Solved);
            const double Error = std::fabs(Area - 1.0);
            const int    ExpectedDropped = Aligned ? 0 : 1;
            Report(JoinGate, Each.Name, "area " + Fixed(Area) + " · welded " + std::to_string(Joined.Welded) + " · dropped " + std::to_string(Joined.Dropped) + " · " + std::to_string(Joined.Sketch.Points.size()) + " points · solve " + (Solve.Converged ? "converged" : "failed") + (Refusal.empty() ? "" : " · " + Refusal),
                   Error, Joined.Refusal.empty() && Refusal.empty() && Error <= 1.0e-12 && Joined.Welded == 2 && Joined.Sketch.Points.size() == 4 && Joined.Dropped == ExpectedDropped && Solve.Converged);
        }
        // A mirrored arc keeps running counter-clockwise: half disc joined through a mirror closes with its diameter.
        SketchModel Diameter = Polygon({ { -1.0, 0.0 }, { 1.0, 0.0 } }, false);
        SketchModel Half;
        const int Centre = AppendSketchPoint(Half, { 0.0, 0.0 }), From = AppendSketchPoint(Half, { 1.0, 0.0 }), To = AppendSketchPoint(Half, { -1.0, 0.0 });
        AppendSketchArc(Half, Centre, From, To, 1);                                          // Upper half in its own frame
        const SketchJoinOutcome Mirrored = JoinSketchModels(Diameter, Half, { { 0.0, 0.0 }, { 1.0, 0.0 }, { 0.0, -1.0 } }, 1.0e-9);   // Lands as the lower half
        std::string Refusal;
        const double Area  = ProfileArea(Mirrored.Sketch, Refusal);
        double Lowest = 0.0;
        for (int Sample = 1; Sample < 8; ++Sample)
        {
            std::string Problem;
            Lowest = std::min(Lowest, EvaluateProfileSegment(ResolveSketchCurveSegment(Mirrored.Sketch, 1, 1.0e-12, Problem), Sample / 8.0).Y);
        }
        Report(JoinGate, "Mirrored arc", "area " + Fixed(Area) + " vs π/2 · arc runs below the diameter (lowest y " + Fixed(Lowest).substr(0, 6) + ")", std::fabs(Area - Pi / 2.0),
               Refusal.empty() && std::fabs(Area - Pi / 2.0) <= 1.0e-12 && Lowest < -0.5);
    }

    void ValidateContact()
    {
        const SketchModel Square  = Polygon({ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 } }, true);
        const SketchModel Across  = Polygon({ { 0.5, -0.5 }, { 0.5, 1.5 } }, false);
        const SketchModel Touch   = Polygon({ { 1.0, 1.0 }, { 2.0, 2.0 } }, false);
        const SketchModel Apart   = Polygon({ { 3.0, 3.0 }, { 4.0, 3.0 } }, false);
        const bool First = SketchContactCondition(Square, Across, 1.0e-9), Second = SketchContactCondition(Square, Touch, 1.0e-9), Third = SketchContactCondition(Square, Apart, 1.0e-9);
        Report(ContactGate, "Crossing · touching · apart", std::string(First ? "yes" : "no") + " · " + (Second ? "yes" : "no") + " · " + (Third ? "yes" : "no"), First && Second && !Third ? 0.0 : 1.0, First && Second && !Third);
    }

    void ValidateRefusals()
    {
        struct Case { const char* Name; SketchModel Sketch; double Distance; const char* Expected; };
        SketchModel Spline;
        std::vector<int> Points;
        for (Vector2 Point : { Vector2{ 0.0, 0.0 }, Vector2{ 1.0, 1.0 }, Vector2{ 2.0, 0.0 } }) Points.push_back(AppendSketchPoint(Spline, Point));
        AppendSketchSpline(Spline, Points, SketchSplineForm::CubicSpline, 1);
        SketchModel Branch = Polygon({ { 0.0, 0.0 }, { 1.0, 0.0 } }, false);
        AppendSketchLine(Branch, 1, AppendSketchPoint(Branch, { 1.0, 1.0 }), 2);
        AppendSketchLine(Branch, 1, AppendSketchPoint(Branch, { 2.0, 0.0 }), 3);
        SketchModel Circle;
        AppendSketchCircle(Circle, AppendSketchPoint(Circle, { 0.0, 0.0 }), 0.3, 1);
        const std::vector<Case> Cases = {
            { "Spline", Spline, 0.1, "spline" },
            { "Branching junction", Branch, 0.1, "branching" },
            { "Zero distance", Polygon({ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 } }, true), 0.0, "zero" },
            { "Circle radius vanishes", Circle, -0.3, "vanish" },
            { "Rectangle turned inside out", Polygon({ { 0.0, 0.0 }, { 2.0, 0.0 }, { 2.0, 1.0 }, { 0.0, 1.0 } }, true), -0.6, "inside out" },
        };
        for (const Case& Each : Cases)
        {
            const SketchOffsetOutcome Outcome = OffsetSketchCurves(Each.Sketch, Each.Distance);
            const bool Named = Outcome.Refusal.find(Each.Expected) != std::string::npos && Outcome.Sketch.Curves.empty();
            Report(RefuseGate, Each.Name, Outcome.Refusal.empty() ? "accepted!" : Outcome.Refusal, Named ? 0.0 : 1.0, Named);
        }
        const SketchJoinOutcome Skewed = JoinSketchModels(Circle, Circle, { { 0.0, 0.0 }, { 1.0, 0.0 }, { 0.5, 1.0 } }, 1.0e-9);
        const bool Named = Skewed.Refusal.find("orthonormal") != std::string::npos;
        Report(RefuseGate, "Skewed placement", Skewed.Refusal.empty() ? "accepted!" : Skewed.Refusal, Named ? 0.0 : 1.0, Named);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ValidateRectangle();
    ValidateSlot();
    ValidateCircle();
    ValidateOpen();
    ValidateExact();
    ValidateJoin();
    ValidateContact();
    ValidateRefusals();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Sketch editing validation\n\n%s Editing E1: offset-curve (Plasticity O) and join-curves (Plasticity J) on solved sketches\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
