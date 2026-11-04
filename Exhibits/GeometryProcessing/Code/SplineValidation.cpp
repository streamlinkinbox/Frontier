/*====================================================================================================================================
                                                      SPLINEVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Phase B: freeform sketch curves (Hermite, Bezier, NURBS, cubic spline) vs an independent oracle, profiles, extrusion

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Feature/ProfileSweep.h"
#include "Engine/GeometryProcessing/Sketch/SketchProfile.h"
#include "Engine/GeometryProcessing/Sketch/SketchSpline.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <variant>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - C1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Pass rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    void Tally(GateTally& Gate, double Value, bool PassedCondition)
    {
        Gate.Worst = std::max(Gate.Worst, Value);
        ++Gate.Checks;
        if (!PassedCondition) ++Gate.Failures;
    }

    GateTally InterpolationGate = { "C1", "Interpolating forms pass through every point; control forms start and end on the first and last point", "≤ 10⁻¹²·L" };
    GateTally ContinuityGate    = { "C2", "Joint continuity from both sides (left limit = reversed curve): Hermite C¹, cubic spline and cubic NURBS C², closed seams too", "≤ 10⁻¹¹·L" };
    GateTally OracleGate        = { "C3", "Kernel B-spline vs independent oracle (dense tangent solve, de Casteljau, recursive Cox–de Boor): C, C′, C″ at 64 samples per span", "≤ 10⁻¹¹·L" };
    GateTally AreaGate          = { "C4", "Profile area and centroid vs exact power-basis integration of the oracle pieces", "rel. ≤ 10⁻¹²" };
    GateTally ExtrudeGate       = { "C5", "Extrude: audit sound · volume = A·H · one NURBS side per spline · watertight, conforming, outward tessellation", "rel. ≤ 10⁻⁹" };
    GateTally RefusalGate       = { "C6", "Refusals with the expected reason (unknown form, too few points, coincident, degree, open, self-crossing, revolve)", "all named" };
    GateTally SolverGate        = { "C7", "Spline points solve as ordinary unknowns; curve constraints naming a spline are Unsupported and left out", "all equal" };
    GateTally DeterminismGate   = { "C8", "Same input twice → bit-identical poles, area and volume", "equal" };

    //--------------------------------------------------------------------------------------------------------------------
    //                                                ORACLE
    //--------------------------------------------------------------------------------------------------------------------

    // Dense Gaussian elimination with partial pivoting (independent of the kernel's banded solvers).
    [[nodiscard]] std::vector<double> SolveDense(std::vector<std::vector<double>> Matrix, std::vector<double> Right)
    {
        const int Size = static_cast<int>(Right.size());
        for (int Column = 0; Column < Size; ++Column)
        {
            int Pivot = Column;
            for (int Row = Column + 1; Row < Size; ++Row) if (std::fabs(Matrix[Row][Column]) > std::fabs(Matrix[Pivot][Column])) Pivot = Row;
            std::swap(Matrix[Column], Matrix[Pivot]);
            std::swap(Right[Column], Right[Pivot]);
            for (int Row = Column + 1; Row < Size; ++Row)
            {
                const double Factor = Matrix[Row][Column] / Matrix[Column][Column];
                for (int Inner = Column; Inner < Size; ++Inner) Matrix[Row][Inner] -= Factor * Matrix[Column][Inner];
                Right[Row] -= Factor * Right[Column];
            }
        }
        std::vector<double> Solution(Size);
        for (int Row = Size - 1; Row >= 0; --Row)
        {
            double Sum = Right[Row];
            for (int Inner = Row + 1; Inner < Size; ++Inner) Sum -= Matrix[Row][Inner] * Solution[Inner];
            Solution[Row] = Sum / Matrix[Row][Row];
        }
        return Solution;
    }

    // Tangents Tᵢ per unit span: Catmull–Rom (Hermite) or the C² cubic interpolation system (natural or cyclic).
    [[nodiscard]] std::vector<Vector2> OracleTangents(SketchSplineForm Form, const std::vector<Vector2>& Points, bool Closed)
    {
        const int Last = static_cast<int>(Points.size()) - 1;
        std::vector<Vector2> Tangents(Points.size());
        if (Form == SketchSplineForm::Hermite || Last == 1)
        {
            for (int Index = 0; Index <= Last; ++Index)
            {
                const Vector2 Before = Index > 0 ? Points[Index - 1] : (Closed ? Points[Last - 1] : Points[0]);
                const Vector2 After  = Index < Last ? Points[Index + 1] : (Closed ? Points[1] : Points[Last]);
                const double  Span   = (Index > 0 && Index < Last) || Closed ? 2.0 : 1.0;
                Tangents[Index] = (After - Before) * (1.0 / Span);
            }
            return Tangents;
        }
        const int Size = Closed ? Last : Last + 1;
        std::vector<std::vector<double>> Matrix(Size, std::vector<double>(Size, 0.0));
        std::vector<double> RightX(Size), RightY(Size);
        for (int Row = 0; Row < Size; ++Row)
        {
            if (!Closed && (Row == 0 || Row == Last))
            {
                const int Other = Row == 0 ? 1 : Last - 1;
                Matrix[Row][Row] = 2.0; Matrix[Row][Other] = 1.0;
                const Vector2 Chord = Row == 0 ? Points[1] - Points[0] : Points[Last] - Points[Last - 1];
                RightX[Row] = 3.0 * Chord.X; RightY[Row] = 3.0 * Chord.Y;
                continue;
            }
            const int Before = (Row - 1 + Size) % Size, After = (Row + 1) % Size;
            Matrix[Row][Before] += 1.0; Matrix[Row][Row] += 4.0; Matrix[Row][After] += 1.0;
            const Vector2 Chord = Points[Closed ? (Row + 1) % Last : Row + 1] - Points[Closed ? (Row - 1 + Last) % Last : Row - 1];
            RightX[Row] = 3.0 * Chord.X; RightY[Row] = 3.0 * Chord.Y;
        }
        const std::vector<double> X = SolveDense(Matrix, RightX), Y = SolveDense(Matrix, RightY);
        for (int Index = 0; Index < Size; ++Index) Tangents[Index] = { X[Index], Y[Index] };
        if (Closed) Tangents[Last] = Tangents[0];
        return Tangents;
    }

    // Recursive Cox–de Boor basis (the textbook definition, 0/0 = 0; the last knot closes the final span).
    [[nodiscard]] double CoxDeBoor(const std::vector<double>& Knots, int Index, int Degree, double Tau)
    {
        if (Degree == 0)
        {
            if (Knots[Index] <= Tau && Tau < Knots[Index + 1]) return 1.0;
            return Tau == Knots.back() && Knots[Index] < Tau && Knots[Index + 1] == Tau ? 1.0 : 0.0;
        }
        double Value = 0.0;
        const double Left = Knots[Index + Degree] - Knots[Index], Right = Knots[Index + Degree + 1] - Knots[Index + 1];
        if (Left > 0.0)  Value += (Tau - Knots[Index]) / Left * CoxDeBoor(Knots, Index, Degree - 1, Tau);
        if (Right > 0.0) Value += (Knots[Index + Degree + 1] - Tau) / Right * CoxDeBoor(Knots, Index + 1, Degree - 1, Tau);
        return Value;
    }

    // One polynomial piece, power basis on the local t ∈ [0, 1]: C(t) = Σ Coefficients[k]·tᵏ.
    struct PowerPiece
    {
        std::vector<Vector2> Coefficients = {};
    };

    struct OracleCurve
    {
        std::vector<PowerPiece> Pieces = {};                                                 // [-] - Unit τ spans in order
    };

    // Horner for derivatives: Σ_{k ≥ Order} k!/(k−Order)!·cₖ·t^{k−Order}.
    [[nodiscard]] Vector2 EvaluatePieceDerivative(const PowerPiece& Piece, double Local, int Order)
    {
        Vector2 Sum{ 0.0, 0.0 };
        for (int Power = static_cast<int>(Piece.Coefficients.size()) - 1; Power >= Order; --Power)
        {
            double Factor = 1.0;
            for (int Step = 0; Step < Order; ++Step) Factor *= Power - Step;
            Sum = Sum * Local + Piece.Coefficients[Power] * Factor;
        }
        return Sum;
    }

    // Independent curve for one form: Hermite power pieces, Bernstein → power, or Cox–de Boor sampled and interpolated.
    [[nodiscard]] OracleCurve BuildOracle(SketchSplineForm Form, std::vector<Vector2> Points)
    {
        OracleCurve Oracle;
        const int  Last   = static_cast<int>(Points.size()) - 1;
        const bool Closed = Last >= 3 && Distance(Points.front(), Points.back()) == 0.0;
        if (Form == SketchSplineForm::Hermite || Form == SketchSplineForm::CubicSpline)
        {
            const std::vector<Vector2> Tangents = OracleTangents(Form, Points, Closed);
            for (int Span = 0; Span < Last; ++Span)
            {
                const Vector2 Q0 = Points[Span], Q1 = Points[Span + 1], T0 = Tangents[Span], T1 = Tangents[Span + 1];
                Oracle.Pieces.push_back({ { Q0, T0, (Q1 - Q0) * 3.0 - T0 * 2.0 - T1, (Q0 - Q1) * 2.0 + T0 + T1 } });
            }
            return Oracle;
        }
        if (Form == SketchSplineForm::Bezier)
        {
            // Bernstein → power: cₖ = C(n,k) Σⱼ (−1)^{k−j} C(k,j) Pⱼ.
            auto Binomial = [](int Top, int Bottom) { double Value = 1.0; for (int Step = 1; Step <= Bottom; ++Step) Value = Value * (Top - Bottom + Step) / Step; return Value; };
            PowerPiece Piece;
            for (int Power = 0; Power <= Last; ++Power)
            {
                Vector2 Sum{ 0.0, 0.0 };
                for (int Index = 0; Index <= Power; ++Index) Sum = Sum + Points[Index] * (((Power - Index) % 2 == 0 ? 1.0 : -1.0) * Binomial(Power, Index));
                Piece.Coefficients.push_back(Sum * Binomial(Last, Power));
            }
            Oracle.Pieces.push_back(Piece);
            return Oracle;
        }
        // NURBS: clamped uniform knots of degree min(3, n), each unit span interpolated from degree + 1 recursive samples.
        const int Degree = std::min(3, Last), Interior = Last - Degree;
        std::vector<double> Knots(Degree + 1, 0.0);
        for (int Index = 1; Index <= Interior; ++Index) Knots.push_back(Index);
        Knots.insert(Knots.end(), Degree + 1, static_cast<double>(Interior + 1));
        for (int Span = 0; Span <= Interior; ++Span)
        {
            std::vector<std::vector<double>> Matrix(Degree + 1, std::vector<double>(Degree + 1));
            std::vector<double> RightX(Degree + 1), RightY(Degree + 1);
            for (int Sample = 0; Sample <= Degree; ++Sample)
            {
                const double Local = 0.5 - 0.5 * std::cos((2.0 * Sample + 1.0) / (2.0 * Degree + 2.0) * 3.14159265358979323846);
                Vector2 Point{ 0.0, 0.0 };
                for (int Index = 0; Index <= Last; ++Index) Point = Point + Points[Index] * CoxDeBoor(Knots, Index, Degree, Span + Local);
                double Power = 1.0;
                for (int Column = 0; Column <= Degree; ++Column) { Matrix[Sample][Column] = Power; Power *= Local; }
                RightX[Sample] = Point.X; RightY[Sample] = Point.Y;
            }
            const std::vector<double> X = SolveDense(Matrix, RightX), Y = SolveDense(Matrix, RightY);
            PowerPiece Piece;
            for (int Column = 0; Column <= Degree; ++Column) Piece.Coefficients.push_back({ X[Column], Y[Column] });
            Oracle.Pieces.push_back(Piece);
        }
        return Oracle;
    }

    // Exact ∫₀¹ of the Green integrands over one piece by polynomial products: area, x-moment, y-moment.
    void IntegratePiece(const PowerPiece& Piece, double& Area, double& MomentX, double& MomentY)
    {
        const int Count = static_cast<int>(Piece.Coefficients.size());
        std::vector<double> X(Count), Y(Count), DX(Count, 0.0), DY(Count, 0.0);
        for (int Power = 0; Power < Count; ++Power) { X[Power] = Piece.Coefficients[Power].X; Y[Power] = Piece.Coefficients[Power].Y; }
        for (int Power = 1; Power < Count; ++Power) { DX[Power - 1] = Power * X[Power]; DY[Power - 1] = Power * Y[Power]; }
        auto Multiply = [](const std::vector<double>& First, const std::vector<double>& Second)
        {
            std::vector<double> Product(First.size() + Second.size() - 1, 0.0);
            for (size_t Low = 0; Low < First.size(); ++Low) for (size_t High = 0; High < Second.size(); ++High) Product[Low + High] += First[Low] * Second[High];
            return Product;
        };
        auto Integral = [](const std::vector<double>& Polynomial) { double Sum = 0.0; for (size_t Power = 0; Power < Polynomial.size(); ++Power) Sum += Polynomial[Power] / (Power + 1.0); return Sum; };
        Area    += 0.5 * (Integral(Multiply(X, DY)) - Integral(Multiply(Y, DX)));
        MomentX += 0.5 * Integral(Multiply(Multiply(X, X), DY));
        MomentY -= 0.5 * Integral(Multiply(Multiply(Y, Y), DX));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SPECIMENS
    //--------------------------------------------------------------------------------------------------------------------

    struct CurveSpecimen
    {
        const char*          Name   = "";
        SketchSplineForm     Form   = SketchSplineForm::CubicSpline;
        std::vector<Vector2> Points = {};
    };

    const std::vector<Vector2> Wave   = { { 0.0, 0.0 }, { 1.0, 1.4 }, { 2.3, 1.9 }, { 3.1, 0.6 }, { 4.4, 1.1 }, { 5.0, 2.6 } };
    const std::vector<Vector2> Pebble = { { 2.0, 0.0 }, { 3.4, 0.8 }, { 3.6, 2.4 }, { 2.1, 3.3 }, { 0.5, 2.6 }, { 0.3, 1.0 }, { 2.0, 0.0 } };

    [[nodiscard]] std::vector<CurveSpecimen> CurveCorpus()
    {
        std::vector<CurveSpecimen> Corpus;
        const SketchSplineForm Forms[] = { SketchSplineForm::Hermite, SketchSplineForm::Bezier, SketchSplineForm::Nurbs, SketchSplineForm::CubicSpline };
        for (SketchSplineForm Form : Forms)
        {
            Corpus.push_back({ "Wave", Form, Wave });
            Corpus.push_back({ "Pebble (closed)", Form, Pebble });
            Corpus.push_back({ "Chord", Form, { { -1.0, 0.5 }, { 2.0, 1.5 } } });
            Corpus.push_back({ "Hook", Form, { { 0.0, 0.0 }, { 0.2, 2.0 }, { 1.5, 2.2 } } });
        }
        return Corpus;
    }

    struct CurveRow
    {
        std::string Name          = {};
        std::string Form          = {};
        int         Degree        = 0;
        int         Poles         = 0;
        int         Spans         = 0;
        double      Interpolation = 0.0;                                                     // [-] - / L
        double      Continuity    = 0.0;                                                     // [-] - / L
        double      Oracle        = 0.0;                                                     // [-] - / L
        bool        PassedCondition = true;
    };

    struct ProfileSpecimen
    {
        const char* Name = "";
        std::function<void(SketchModel&)> Build;
    };

    int NextIdentifier = 1;

    // Polyline of lines through Corners (closed), returning nothing.
    void AppendPolyline(SketchModel& Sketch, const std::vector<Vector2>& Corners, bool Closed)
    {
        std::vector<int> Indices;
        for (const Vector2& Corner : Corners) Indices.push_back(AppendSketchPoint(Sketch, Corner));
        const size_t Count = Closed ? Indices.size() : Indices.size() - 1;
        for (size_t Index = 0; Index < Count; ++Index) (void)AppendSketchLine(Sketch, Indices[Index], Indices[(Index + 1) % Indices.size()], NextIdentifier++);
    }

    // Spline through Points; when Joins is given the end indices reuse existing sketch points.
    void AppendSpline(SketchModel& Sketch, const std::vector<Vector2>& Points, SketchSplineForm Form)
    {
        std::vector<int> Indices;
        for (size_t Index = 0; Index < Points.size(); ++Index)
        {
            if (Index + 1 == Points.size() && Points.size() > 2 && Distance(Points.front(), Points.back()) == 0.0) Indices.push_back(Indices.front());
            else Indices.push_back(AppendSketchPoint(Sketch, Points[Index]));
        }
        (void)AppendSketchSpline(Sketch, Indices, Form, NextIdentifier++);
    }

    // Line from the spline end back to its start closes an arch.
    void AppendArch(SketchModel& Sketch, const std::vector<Vector2>& Points, SketchSplineForm Form)
    {
        AppendSpline(Sketch, Points, Form);
        (void)AppendSketchLine(Sketch, static_cast<int>(Sketch.Points.size()) - 1, static_cast<int>(Sketch.Points.size()) - static_cast<int>(Points.size()), NextIdentifier++);
    }

    const std::vector<Vector2> ArchPoints = { { 0.0, 0.0 }, { 0.6, 1.8 }, { 2.0, 2.6 }, { 3.3, 1.9 }, { 4.0, 0.0 } };

    [[nodiscard]] std::vector<ProfileSpecimen> ProfileCorpus()
    {
        return {
            { "Arch · cubic spline", [](SketchModel& Sketch) { AppendArch(Sketch, ArchPoints, SketchSplineForm::CubicSpline); } },
            { "Arch · Hermite",      [](SketchModel& Sketch) { AppendArch(Sketch, ArchPoints, SketchSplineForm::Hermite); } },
            { "Arch · Bezier",       [](SketchModel& Sketch) { AppendArch(Sketch, ArchPoints, SketchSplineForm::Bezier); } },
            { "Arch · NURBS",        [](SketchModel& Sketch) { AppendArch(Sketch, { { 0.0, 0.0 }, { 0.0, 2.0 }, { 1.4, 3.0 }, { 2.8, 2.6 }, { 4.0, 1.2 }, { 4.0, 0.0 } }, SketchSplineForm::Nurbs); } },
            { "Pebble · cubic spline (closed)", [](SketchModel& Sketch) { AppendSpline(Sketch, Pebble, SketchSplineForm::CubicSpline); } },
            { "Pebble · Hermite (closed)",      [](SketchModel& Sketch) { AppendSpline(Sketch, Pebble, SketchSplineForm::Hermite); } },
            { "Pebble · NURBS (closed, G⁰ seam)", [](SketchModel& Sketch) { AppendSpline(Sketch, Pebble, SketchSplineForm::Nurbs); } },
            { "Plate with pebble hole", [](SketchModel& Sketch)
              {
                  AppendPolyline(Sketch, { { -2.0, -2.0 }, { 6.0, -2.0 }, { 6.0, 5.0 }, { -2.0, 5.0 } }, true);
                  AppendSpline(Sketch, Pebble, SketchSplineForm::CubicSpline);
              } },
            { "Two splines and two lines", [](SketchModel& Sketch)
              {
                  const int A = AppendSketchPoint(Sketch, { 0.0, 0.0 }), B = AppendSketchPoint(Sketch, { 5.0, 0.0 });
                  const int C = AppendSketchPoint(Sketch, { 5.0, 3.0 }), D = AppendSketchPoint(Sketch, { 0.0, 3.0 });
                  const int E = AppendSketchPoint(Sketch, { 2.5, -0.8 }), F = AppendSketchPoint(Sketch, { 4.2, 1.5 }), G = AppendSketchPoint(Sketch, { 2.5, 3.9 });
                  (void)AppendSketchSpline(Sketch, { A, E, B }, SketchSplineForm::CubicSpline, NextIdentifier++);
                  (void)AppendSketchLine(Sketch, B, C, NextIdentifier++);
                  (void)AppendSketchSpline(Sketch, { C, F, G, D }, SketchSplineForm::Hermite, NextIdentifier++);
                  (void)AppendSketchLine(Sketch, D, A, NextIdentifier++);
                  (void)F;
              } },
        };
    }

    struct ProfileRow
    {
        std::string Name       = {};
        int         Loops      = 0;
        int         Splines    = 0;
        double      Area       = 0.0;
        double      AreaError  = 0.0;
        double      Centroid   = 0.0;
        double      Volume     = 0.0;
        double      VolumeError = 0.0;
        int         Faces      = 0;
        int         NurbsFaces = 0;
        int         Triangles  = 0;
        std::string Problem    = {};
        bool        PassedCondition = true;
    };

    // Oracle area and moments of a profile: every segment rebuilt from the sketch, splines through BuildOracle.
    void IntegrateOracleProfile(const SketchModel& Sketch, double& Area, double& MomentX, double& MomentY, const SketchProfile& Profile)
    {
        for (const ProfileLoop& Loop : Profile.Loops)
        {
            const double Sign = Loop.SignedArea > 0.0 ? 1.0 : -1.0;                           // [-] - Orientation chosen by the profile
            double LoopArea = 0.0, LoopX = 0.0, LoopY = 0.0;
            for (const ProfileSegment& Segment : Loop.Segments)
            {
                const SketchCurve& Curve = Sketch.Curves[Segment.Curve];
                if (Curve.Kind == SketchCurveKind::Line)
                {
                    const Vector2 Start = Sketch.Points[Curve.Start].Position, End = Sketch.Points[Curve.End].Position;
                    IntegratePiece({ { Start, End - Start } }, LoopArea, LoopX, LoopY);
                    continue;
                }
                std::vector<Vector2> Points;
                for (int Index : Curve.Points) Points.push_back(Sketch.Points[Index].Position);
                const OracleCurve Oracle = BuildOracle(Curve.Form, Points);
                for (const PowerPiece& Piece : Oracle.Pieces) IntegratePiece(Piece, LoopArea, LoopX, LoopY);
            }
            // Natural direction integral; flip to the loop's orientation (outer CCW, hole CW).
            const double Flip = (LoopArea > 0.0) == (Sign > 0.0) ? 1.0 : -1.0;
            Area += Flip * LoopArea; MomentX += Flip * LoopX; MomentY += Flip * LoopY;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    // ---- C1 C2 C3 over the curve corpus.
    std::vector<CurveRow> CurveRows;
    for (const CurveSpecimen& Specimen : CurveCorpus())
    {
        CurveRow Row;
        Row.Name = Specimen.Name;
        Row.Form = SketchSplineFormLabel(Specimen.Form);
        double Scale = 0.0;
        for (const Vector2& First : Specimen.Points) for (const Vector2& Second : Specimen.Points) Scale = std::max(Scale, Distance(First, Second));
        const SketchSplineOutcome Resolved = ResolveSketchSpline(Specimen.Form, Specimen.Points, Scale);
        if (!Resolved.Refusal.empty()) { Row.PassedCondition = false; Tally(OracleGate, 1.0, false); CurveRows.push_back(Row); continue; }
        const SketchSplineCurve& Curve = Resolved.Curve;
        Row.Degree = Curve.Degree;
        Row.Poles  = static_cast<int>(Curve.Poles.size());
        const std::vector<double> Breaks = CollectSketchSplineBreaks(Curve);
        Row.Spans  = static_cast<int>(Breaks.size()) - 1;
        const bool Interpolating = Specimen.Form == SketchSplineForm::Hermite || Specimen.Form == SketchSplineForm::CubicSpline;

        // C1
        std::vector<std::pair<double, Vector2>> Targets = { { Breaks.front(), Specimen.Points.front() }, { Breaks.back(), Specimen.Points.back() } };
        if (Interpolating) for (size_t Index = 1; Index + 1 < Specimen.Points.size(); ++Index) Targets.push_back({ static_cast<double>(Index), Specimen.Points[Index] });
        for (const auto& [Tau, Point] : Targets) Row.Interpolation = std::max(Row.Interpolation, Distance(EvaluateSketchSpline(Curve, Tau).Position, Point) / Scale);
        Tally(InterpolationGate, Row.Interpolation, Row.Interpolation <= 1.0e-12);

        // C2: right limit from the curve, left limit from the reversed curve at the mirrored parameter.
        SketchSplineCurve Reversed = Curve;
        ReverseSketchSpline(Reversed);
        const double Mirror = Breaks.front() + Breaks.back();
        const int    Orders = Specimen.Form == SketchSplineForm::Hermite ? 1 : (Specimen.Form == SketchSplineForm::Bezier || Curve.Degree < 3) ? 0 : 2;
        auto Jump = [&](double RightTau, double LeftTau)
        {
            const SketchSplineDerivatives Right = EvaluateSketchSpline(Curve, RightTau), Left = EvaluateSketchSpline(Reversed, Mirror - LeftTau);
            double Worst = Distance(Right.Position, Left.Position);
            if (Orders >= 1) Worst = std::max(Worst, Distance(Right.First, Left.First * -1.0));
            if (Orders >= 2) Worst = std::max(Worst, Distance(Right.Second, Left.Second));
            return Worst / Scale;
        };
        for (size_t Index = 1; Index + 1 < Breaks.size(); ++Index) Row.Continuity = std::max(Row.Continuity, Jump(Breaks[Index], Breaks[Index]));
        if (Resolved.Closed && Specimen.Form != SketchSplineForm::Nurbs && Specimen.Form != SketchSplineForm::Bezier) Row.Continuity = std::max(Row.Continuity, Jump(Breaks.front(), Breaks.back()));
        if (Breaks.size() > 2 || Resolved.Closed) Tally(ContinuityGate, Row.Continuity, Row.Continuity <= 1.0e-11);

        // C3
        const OracleCurve Oracle = BuildOracle(Specimen.Form, Specimen.Points);
        if (static_cast<int>(Oracle.Pieces.size()) != Row.Spans) { Row.Oracle = 1.0; }
        else
        {
            const double Width = (Breaks.back() - Breaks.front()) / Row.Spans;            // [-] - τ per oracle piece
            for (int Span = 0; Span < Row.Spans; ++Span)
            {
                for (int Sample = 0; Sample <= 64; ++Sample)
                {
                    const double Local = Sample / 64.0;
                    const SketchSplineDerivatives Kernel = EvaluateSketchSpline(Curve, Breaks[Span] + Local * Width);
                    const Vector2 Position = EvaluatePieceDerivative(Oracle.Pieces[Span], Local, 0);
                    const Vector2 First    = EvaluatePieceDerivative(Oracle.Pieces[Span], Local, 1) * (1.0 / Width);
                    const Vector2 Second   = EvaluatePieceDerivative(Oracle.Pieces[Span], Local, 2) * (1.0 / (Width * Width));
                    if (Sample == 64 && Span + 1 < Row.Spans) continue;                       // Kernel takes the right piece at a break
                    Row.Oracle = std::max({ Row.Oracle, Distance(Kernel.Position, Position) / Scale, Distance(Kernel.First, First) / Scale, Distance(Kernel.Second, Second) / Scale });
                }
            }
        }
        Tally(OracleGate, Row.Oracle, Row.Oracle <= 1.0e-11);
        Row.PassedCondition = Row.Interpolation <= 1.0e-12 && Row.Continuity <= 1.0e-11 && Row.Oracle <= 1.0e-11;
        CurveRows.push_back(Row);
    }

    // ---- C4 C5 over the profile corpus.
    std::vector<ProfileRow> ProfileRows;
    const double Height = 1.5;                                                               // [len] - Extrusion distance
    for (const ProfileSpecimen& Specimen : ProfileCorpus())
    {
        ProfileRow Row;
        Row.Name = Specimen.Name;
        SketchModel Sketch;
        Specimen.Build(Sketch);
        for (const SketchCurve& Curve : Sketch.Curves) Row.Splines += Curve.Kind == SketchCurveKind::Spline ? 1 : 0;
        const SketchProfile Profile = ExtractSketchProfile(Sketch);
        if (!Profile.Refusal.empty())
        {
            Row.Problem = Profile.Refusal; Row.PassedCondition = false;
            Tally(AreaGate, 1.0, false); Tally(ExtrudeGate, 1.0, false);
            ProfileRows.push_back(Row);
            continue;
        }
        Row.Loops = static_cast<int>(Profile.Loops.size());
        Row.Area  = Profile.Area;
        double Area = 0.0, MomentX = 0.0, MomentY = 0.0;
        IntegrateOracleProfile(Sketch, Area, MomentX, MomentY, Profile);
        Row.AreaError = std::fabs(Profile.Area - Area) / std::fabs(Area);
        const Vector2 Centroid = { MomentX / Area, MomentY / Area };
        Row.Centroid = Distance(Profile.Centroid, Centroid) / Profile.Scale;
        Tally(AreaGate, std::max(Row.AreaError, Row.Centroid), Row.AreaError <= 1.0e-12 && Row.Centroid <= 1.0e-12);

        ModelPartition Partition;
        const OrthonormalFrame Plane = ConstructFrame({ 0.3, -0.2, 0.7 }, Normalize(Vector3{ 0.2, -0.3, 1.0 }), { 1.0, 0.0, 0.0 });
        const SweepOutcome Outcome = ExtrudeProfile(Partition, Profile, Plane, Height);
        bool ExtrudeCondition = Outcome.Refusal.empty();
        if (!ExtrudeCondition) Row.Problem = Outcome.Refusal;
        else
        {
            const AuditReport Audit = AuditBody(Partition, Outcome.Body);
            if (!Audit.SoundCondition()) Row.Problem = std::string("audit ") + Audit.Findings.front().Code + " " + Audit.Findings.front().Subject;
            Row.Volume      = IntegrateBodyVolume(Partition, Outcome.Body);
            Row.VolumeError = std::fabs(Row.Volume - Area * Height) / (Area * Height);
            const auto Faces = Partition.CollectInventory(Outcome.Body).Faces;
            Row.Faces = static_cast<int>(Faces.size());
            for (FaceToken Face : Faces) Row.NurbsFaces += std::holds_alternative<NurbsSurface>(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface)) ? 1 : 0;
            TessellationSettings Fine;
            Fine.SagRatio = 1.0e-3;
            const TessellationOutcome Complex = TessellateBody(Partition, Outcome.Body, Fine);
            bool PolygonCondition = Complex.Refusal.empty();
            if (PolygonCondition)
            {
                const TessellationAuditReport Polygon = AuditTessellation(Partition, Outcome.Body, Complex);
                Row.Triangles = Polygon.Triangles;
                PolygonCondition = Polygon.UnpairedSides == 0 && Polygon.MissingEdgeSides == 0 && Polygon.Inverted == 0 && Polygon.Degenerate == 0;
                if (!PolygonCondition) Row.Problem = "tessellation: unpaired " + std::to_string(Polygon.UnpairedSides) + " · missing " + std::to_string(Polygon.MissingEdgeSides) + " · inverted " + std::to_string(Polygon.Inverted);
            }
            else Row.Problem = "tessellation: " + Complex.Refusal;
            ExtrudeCondition = Audit.SoundCondition() && Row.VolumeError <= 1.0e-9 && Row.NurbsFaces == Row.Splines && PolygonCondition;
        }
        Tally(ExtrudeGate, ExtrudeCondition ? Row.VolumeError : std::max(1.0, Row.VolumeError), ExtrudeCondition);
        Row.PassedCondition = ExtrudeCondition && Row.AreaError <= 1.0e-12 && Row.Centroid <= 1.0e-12;
        ProfileRows.push_back(Row);
    }

    // ---- C6 refusals.
    struct RefusalRow { std::string Name, Expected, Measured; bool PassedCondition = false; };
    std::vector<RefusalRow> RefusalRows;
    auto ExpectRefusal = [&](const char* Name, const std::string& Measured, const char* Expected)
    {
        const bool Condition = Measured.find(Expected) != std::string::npos;
        Tally(RefusalGate, Condition ? 0.0 : 1.0, Condition);
        RefusalRows.push_back({ Name, Expected, Measured.empty() ? "(accepted)" : Measured, Condition });
    };
    ExpectRefusal("Unknown form 7 (hand-edited document)", ResolveSketchSpline(static_cast<SketchSplineForm>(7), Wave, 5.0).Refusal, "unknown spline form 7");
    ExpectRefusal("One point", ResolveSketchSpline(SketchSplineForm::CubicSpline, { { 0.0, 0.0 } }, 1.0).Refusal, "needs at least 2 points");
    ExpectRefusal("Repeated point", ResolveSketchSpline(SketchSplineForm::Hermite, { { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 0.0 }, { 2.0, 1.0 } }, 2.0).Refusal, "points 2 and 3 coincide");
    ExpectRefusal("Closed on 2 distinct points", ResolveSketchSpline(SketchSplineForm::CubicSpline, { { 0.0, 0.0 }, { 1.0, 0.0 }, { 0.0, 0.0 } }, 1.0).Refusal, "needs at least 3 distinct points");
    {
        std::vector<Vector2> Many;
        for (int Index = 0; Index < 17; ++Index) Many.push_back({ static_cast<double>(Index), std::sin(0.5 * Index) });
        ExpectRefusal("Bezier on 17 points", ResolveSketchSpline(SketchSplineForm::Bezier, Many, 16.0).Refusal, "exceeds degree 15");
    }
    {
        SketchModel Sketch;
        AppendSpline(Sketch, Wave, SketchSplineForm::CubicSpline);
        ExpectRefusal("Open spline profile", ExtractSketchProfile(Sketch).Refusal, "open profile");
    }
    {
        SketchModel Eight;                                                                   // Figure eight
        AppendSpline(Eight, { { 0.0, 0.0 }, { 2.0, 1.0 }, { 4.0, -1.0 }, { 4.0, 1.0 }, { 2.0, -1.0 }, { 0.0, 0.0 } }, SketchSplineForm::CubicSpline);
        ExpectRefusal("Self-crossing closed spline", ExtractSketchProfile(Eight).Refusal, "crosses itself: Spline");
    }
    {
        SketchModel Sketch;                                                                  // Arch whose spline dips through its base line
        AppendArch(Sketch, { { 0.0, 0.0 }, { 1.0, 1.5 }, { 2.0, -1.0 }, { 3.0, 1.5 }, { 4.0, 0.0 } }, SketchSplineForm::CubicSpline);
        ExpectRefusal("Spline crossing a line", ExtractSketchProfile(Sketch).Refusal, "crosses itself");
    }
    {
        SketchModel Sketch;
        AppendArch(Sketch, ArchPoints, SketchSplineForm::CubicSpline);
        ModelPartition Partition;
        // Spline revolves are exact now (SweepValidation R1 / R3); an axis through the arch (y = 1) is still refused.
        const SweepOutcome Outcome = RevolveProfile(Partition, ExtractSketchProfile(Sketch), OrthonormalFrame{}, { 0.0, 1.0 }, { 1.0, 0.0 }, 3.0);
        ExpectRefusal("Revolve a spline profile across its axis", Outcome.Refusal, "crosses the axis");
    }

    // ---- C7 solver: spline points are ordinary unknowns; a spline as a line / circle reference is Unsupported.
    struct SolverRow { std::string Name; bool PassedCondition = false; std::string Detail; };
    std::vector<SolverRow> SolverRows;
    {
        SketchModel Sketch;
        std::vector<int> Indices;
        const Vector2 Targets[] = { { 0.0, 0.0 }, { 1.0, 2.0 }, { 3.0, 2.0 }, { 4.0, 0.0 } };
        for (int Index = 0; Index < 4; ++Index) Indices.push_back(AppendSketchPoint(Sketch, Targets[Index] + Vector2{ 0.13 * Index, -0.07 * Index }));
        const int Spline = AppendSketchSpline(Sketch, Indices, SketchSplineForm::CubicSpline, NextIdentifier++);
        const int Line   = AppendSketchLine(Sketch, Indices[3], Indices[0], NextIdentifier++);
        for (int Index = 0; Index < 4; ++Index) (void)AppendSketchConstraint(Sketch, SketchConstraintKind::Fixed, Indices[Index], -1, -1, Targets[Index].X, {}, Targets[Index].Y);
        const int Tangent    = AppendSketchConstraint(Sketch, SketchConstraintKind::Tangent, Line, Spline);
        const int Horizontal = AppendSketchConstraint(Sketch, SketchConstraintKind::Horizontal, Spline);
        const SketchSolveOutcome Outcome = SolveSketch(Sketch);
        double Error = 0.0;
        for (int Index = 0; Index < 4; ++Index) Error = std::max(Error, Distance(Sketch.Points[Indices[Index]].Position, Targets[Index]));
        const bool Placed = Outcome.Converged && Error <= 1.0e-12 * 4.0;
        const bool Free   = Outcome.DegreesOfFreedom == 0 && Outcome.CurveFixed[Spline] && Outcome.CurveFixed[Line];
        const bool Marked = Outcome.Constraints[Tangent].Status == SketchConstraintStatus::Unsupported && Outcome.Constraints[Horizontal].Status == SketchConstraintStatus::Unsupported
                         && Outcome.Constraints[0].Status == SketchConstraintStatus::Satisfied && Outcome.ConflictGroups.empty() && Outcome.Redundant.empty();
        SolverRows.push_back({ "Fixed spline points reach their targets", Placed, "error " + std::to_string(Error) });
        SolverRows.push_back({ "DOF 0 · spline and line flagged fixed", Free, "DOF " + std::to_string(Outcome.DegreesOfFreedom) });
        SolverRows.push_back({ "Tangent(line, spline) and Horizontal(spline) → Unsupported, nothing else disturbed", Marked,
                               std::string(SketchConstraintStatusLabel(Outcome.Constraints[Tangent].Status)) + " / " + SketchConstraintStatusLabel(Outcome.Constraints[Horizontal].Status) });
        for (const SolverRow& Row : SolverRows) Tally(SolverGate, Row.PassedCondition ? 0.0 : 1.0, Row.PassedCondition);
    }

    // ---- C8 determinism.
    {
        auto Run = [&](std::vector<double>& Values)
        {
            SketchModel Sketch;
            AppendPolyline(Sketch, { { -2.0, -2.0 }, { 6.0, -2.0 }, { 6.0, 5.0 }, { -2.0, 5.0 } }, true);
            AppendSpline(Sketch, Pebble, SketchSplineForm::CubicSpline);
            const SketchProfile Profile = ExtractSketchProfile(Sketch);
            for (const ProfileLoop& Loop : Profile.Loops) for (const ProfileSegment& Segment : Loop.Segments) for (const Vector2& Pole : Segment.Spline.Poles) { Values.push_back(Pole.X); Values.push_back(Pole.Y); }
            Values.push_back(Profile.Area);
            ModelPartition Partition;
            const SweepOutcome Outcome = ExtrudeProfile(Partition, Profile, OrthonormalFrame{}, Height);
            Values.push_back(Outcome.Refusal.empty() ? IntegrateBodyVolume(Partition, Outcome.Body) : -1.0);
        };
        std::vector<double> First, Second;
        Run(First); Run(Second);
        const bool Same = First.size() == Second.size() && std::memcmp(First.data(), Second.data(), First.size() * sizeof(double)) == 0;
        Tally(DeterminismGate, Same ? 0.0 : 1.0, Same);
    }

    // ---- Report.
    const GateTally* Gates[] = { &InterpolationGate, &ContinuityGate, &OracleGate, &AreaGate, &ExtrudeGate, &RefusalGate, &SolverGate, &DeterminismGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Spline validation\n\n%s Phase B freeform sketch curves: Hermite · Bezier · NURBS · cubic spline (Plasticity curve keys 1 … 4)\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Curves (C1 C2 C3)\n\n| | Specimen | Form | Degree | Poles | Spans | through points / L | joint jump / L | vs oracle / L |\n|---|---|---|---|---|---|---|---|---|\n");
    for (const CurveRow& Row : CurveRows)
    {
        std::printf("| %s | %s | %s | %d | %d | %d | %.1e | %.1e | %.1e |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Form.c_str(), Row.Degree, Row.Poles, Row.Spans, Row.Interpolation, Row.Continuity, Row.Oracle);
    }
    std::printf("\n## Profiles and extrusion (C4 C5) · H = %.1f on a tilted plane\n\n| | Profile | Loops | Splines | Area | area rel. | centroid / L | Volume | volume rel. | Faces | NURBS faces | Triangles | Note |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n", Height);
    for (const ProfileRow& Row : ProfileRows)
    {
        std::printf("| %s | %s | %d | %d | %.9f | %.1e | %.1e | %.9f | %.1e | %d | %d | %d | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Loops, Row.Splines, Row.Area, Row.AreaError, Row.Centroid,
                    Row.Volume, Row.VolumeError, Row.Faces, Row.NurbsFaces, Row.Triangles, Row.Problem.c_str());
    }
    std::printf("\n## Refusals (C6)\n\n| | Case | Expected | Measured |\n|---|---|---|---|\n");
    for (const RefusalRow& Row : RefusalRows) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Expected.c_str(), Row.Measured.c_str());
    std::printf("\n## Solver (C7)\n\n| | Check | Detail |\n|---|---|---|\n");
    for (const SolverRow& Row : SolverRows) std::printf("| %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
