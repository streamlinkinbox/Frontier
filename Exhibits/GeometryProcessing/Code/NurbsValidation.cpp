/*====================================================================================================================================
                                                       NURBSVALIDATION.CPP
====================================================================================================================================*/
// 🧩 NURBS carriers: exact conic / quadric reproduction, derivatives, inversion, iso-curves, affine images, promoted bodies, Booleans

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Audit/TessellationAudit.h"
#include "Engine/GeometryProcessing/Blend/EdgeBlend.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/BodyTransform.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Containment/WindingNumber.h"
#include "Engine/GeometryProcessing/Geometry/NurbsCarrier.h"
#include "Engine/GeometryProcessing/Intersection/ImplicitSurface.h"
#include "Engine/GeometryProcessing/Tessellation/BodyTessellation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;
    constexpr double π        = std::numbers::pi;                                            // [-]   - π
    constexpr double FullTurn = 2.0 * π;                                                     // [rad] - 2π

    [[nodiscard]] double Elapsed(Clock::time_point Start) { return std::chrono::duration<double, std::milli>(Clock::now() - Start).count(); }

    // 📝 One gate: worst measured value against its limit over every check.
    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - N1 …
        const char* Subject  = "";                                                           // [-] - What is measured
        const char* Limit    = "";                                                           // [-] - Acceptance, as printed
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-] - Measurements
        long        Failures = 0;                                                            // [-] - Measurements over the limit
    };

    void Tally(GateTally& Gate, double Value, double Limit)
    {
        const bool Finite = std::isfinite(Value);
        Gate.Worst = std::max(Gate.Worst, Finite ? Value : 1.0e300);
        Gate.Checks += 1;
        Gate.Failures += Finite && Value <= Limit ? 0 : 1;
    }

    GateTally ReproductionGate{ "N1", "Exact reproduction: NURBS circle / ellipse / line / plane / cylinder / cone / sphere / torus vs the analytic carrier (distance relative to scale / normal angle; the angle is conditioned by 1/cos 𝑣 beside a sphere pole)", "≤ 1e-14 / 1e-13 rad" };
    GateTally DerivativeGate  { "N2", "Rational derivatives C′, C″, S𝑢, S𝑣, S𝑢𝑢, S𝑢𝑣, S𝑣𝑣 vs Richardson differences inside a knot span (relative)", "≤ 1e-8" };
    GateTally InversionGate   { "N3", "Inversion round trip: on-surface / on-curve points and offset points back to their feet (relative to extent)", "≤ 1e-12" };
    GateTally IsoGate         { "N4", "Iso-curves ≡ surface rows, Bézier pieces ≡ curve, reversal ≡ curve (relative to extent)", "≤ 1e-14" };
    GateTally AffineGate      { "N5", "Affine images: map(S)(𝑢,𝑣) vs M·S(𝑢,𝑣) (relative); bodies under non-uniform scale and shear: V = det·V*, there and back", "≤ 1e-12 / 1e-9" };
    GateTally BodyGate        { "N6", "Promoted bodies: audit sound, closed-form volume (relative), watertight tessellation, containment oracle", "≤ 1e-9" };
    GateTally BooleanGate     { "N7", "Booleans with NURBS operands vs closed form or the all-analytic Boolean (relative volume)", "≤ 1e-9" };

    [[nodiscard]] double Uniform(std::mt19937_64& Generator, double Low, double High) { return std::uniform_real_distribution<double>(Low, High)(Generator); }

    [[nodiscard]] Vector3 RandomDirection(std::mt19937_64& Generator)
    {
        for (;;)
        {
            const Vector3 Candidate{ Uniform(Generator, -1, 1), Uniform(Generator, -1, 1), Uniform(Generator, -1, 1) };
            const double  Size = Length(Candidate);
            if (Size > 0.2 && Size <= 1.0) return Candidate * (1.0 / Size);
        }
    }

    [[nodiscard]] OrthonormalFrame RandomFrame(std::mt19937_64& Generator)
    {
        const Vector3 Origin{ Uniform(Generator, -2, 2), Uniform(Generator, -2, 2), Uniform(Generator, -2, 2) };
        return ConstructFrame(Origin, RandomDirection(Generator), RandomDirection(Generator));
    }

    // Random clamped knots with Count poles: interior knots spread non-uniformly, some doubled.
    [[nodiscard]] std::vector<double> RandomKnots(std::mt19937_64& Generator, int Degree, int Count, double Low, double High)
    {
        std::vector<double> Interior;
        for (int Index = 0; Index < Count - Degree - 1; ++Index) Interior.push_back(Uniform(Generator, Low, High));
        std::sort(Interior.begin(), Interior.end());
        std::vector<double> Knots(Degree + 1, Low);
        Knots.insert(Knots.end(), Interior.begin(), Interior.end());
        Knots.insert(Knots.end(), Degree + 1, High);
        return Knots;
    }

    // Freeform rational test carriers: random poles near a wavy sheet, weights in [0.5, 2], non-uniform knots.
    [[nodiscard]] NurbsSurface RandomSurface(std::mt19937_64& Generator, int DegreeU, int DegreeV, int CountU, int CountV)
    {
        std::vector<Vector3> Poles;
        std::vector<double>  Weights;
        for (int I = 0; I < CountU; ++I)
            for (int J = 0; J < CountV; ++J)
            {
                Poles.push_back({ I * 0.6 + Uniform(Generator, -0.15, 0.15), J * 0.5 + Uniform(Generator, -0.15, 0.15), 0.4 * std::sin(I + 0.7 * J) + Uniform(Generator, -0.3, 0.3) });
                Weights.push_back(Uniform(Generator, 0.5, 2.0));
            }
        return *ConstructNurbsSurface(DegreeU, DegreeV, CountU, CountV, RandomKnots(Generator, DegreeU, CountU, -0.5, 1.7), RandomKnots(Generator, DegreeV, CountV, 0.25, 3.0),
                                      std::move(Poles), std::move(Weights));
    }

    [[nodiscard]] NurbsCurve RandomCurve(std::mt19937_64& Generator, int Degree, int Count)
    {
        std::vector<Vector3> Poles;
        std::vector<double>  Weights;
        for (int Index = 0; Index < Count; ++Index)
        {
            Poles.push_back({ Index * 0.5, Uniform(Generator, -1, 1), Uniform(Generator, -1, 1) });
            Weights.push_back(Uniform(Generator, 0.5, 2.0));
        }
        return *ConstructNurbsCurve(Degree, RandomKnots(Generator, Degree, Count, -1.0, 2.0), std::move(Poles), std::move(Weights));
    }

    // A parameter strictly inside one knot span, at least Margin·span from its ends (Richardson must not straddle a knot).
    [[nodiscard]] double InsideSpan(std::mt19937_64& Generator, const std::vector<double>& Knots, double Margin, double& Width)
    {
        std::vector<std::pair<double, double>> Spans;
        for (size_t Index = 0; Index + 1 < Knots.size(); ++Index) if (Knots[Index + 1] > Knots[Index]) Spans.push_back({ Knots[Index], Knots[Index + 1] });
        const auto& [Low, High] = Spans[std::uniform_int_distribution<size_t>(0, Spans.size() - 1)(Generator)];
        Width = High - Low;
        return Uniform(Generator, Low + Margin * Width, High - Margin * Width);
    }

    // ---- N1 ---------------------------------------------------------------------------------------------------------------

    struct ReproductionRow
    {
        std::string Name     = {};
        double      Distance = 0.0;                                                          // [-] - Worst relative distance
        double      Angle    = 0.0;                                                          // [rad] - Worst normal angle
        int         Samples  = 0;
    };

    [[nodiscard]] double AngleBetween(Vector3 First, Vector3 Second) { return std::atan2(Length(Cross(First, Second)), Dot(First, Second)); }

    [[nodiscard]] ReproductionRow ProbeSurfaceReproduction(const std::string& Name, const SurfaceGeometry& Analytic, Vector2 BoxLow, Vector2 BoxHigh, double Scale)
    {
        ReproductionRow Row;
        Row.Name = Name;
        const NurbsSurface Spline = *ConvertSurfaceToNurbs(Analytic, BoxLow, BoxHigh);
        const NurbsDomain  U = MeasureNurbsSurfaceDomain(*Spline.Definition, true), V = MeasureNurbsSurfaceDomain(*Spline.Definition, false);
        for (int I = 0; I <= 40; ++I)
            for (int J = 0; J <= 40; ++J)
            {
                const Vector2 Parameter{ U.Low + (U.High - U.Low) * I / 40.0, V.Low + (V.High - V.Low) * J / 40.0 };
                const Vector3 Position = EvaluateNurbsSurface(Spline, Parameter).Position;
                const SurfaceDistance Measure = MeasureSurfaceDistance(Analytic, Position);
                Row.Distance = std::max(Row.Distance, std::fabs(Measure.Distance) / Scale);
                // Natural normals must agree in sign as well (same chart orientation); skip the exact poles of a sphere.
                const Vector2 Foot = ProjectOntoSurface(Analytic, Position, { Parameter.X, Parameter.Y });
                const Vector3 Expected = EvaluateSurfaceNormal(Analytic, Foot);
                const bool    PoleRow = std::holds_alternative<SphereSurface>(Analytic) && (J == 0 || J == 40);
                if (!PoleRow) Row.Angle = std::max(Row.Angle, AngleBetween(EvaluateNurbsNormal(Spline, Parameter), Expected));
                ++Row.Samples;
            }
        return Row;
    }

    [[nodiscard]] ReproductionRow ProbeCurveReproduction(const std::string& Name, const CurveGeometry& Analytic, double Low, double High, double Scale)
    {
        ReproductionRow Row;
        Row.Name = Name;
        const NurbsCurve Spline = *ConvertCurveToNurbs(Analytic, Low, High);
        for (int Index = 0; Index <= 400; ++Index)
        {
            const double  τ = Low + (High - Low) * Index / 400.0;
            const Vector3 Position = EvaluateNurbsCurve(*Spline.Definition, τ).Position;
            const Vector3 Foot = EvaluateCurve(Analytic, ProjectOntoCurve(Analytic, Position, Low - 1.0e-9)).Position;
            Row.Distance = std::max(Row.Distance, Distance(Position, Foot) / Scale);
            ++Row.Samples;
        }
        // 📝 τ matches the analytic parameter at every rational-quadratic joint (and everywhere on a line).
        for (double Joint : CollectNurbsCurveKnots(*Spline.Definition, Low - 1.0, High + 1.0))
        {
            if (Joint < Low || Joint > High) continue;
            Row.Distance = std::max(Row.Distance, Distance(EvaluateNurbsCurve(*Spline.Definition, Joint).Position, EvaluateCurve(Analytic, Joint).Position) / Scale);
        }
        Row.Distance = std::max(Row.Distance, Distance(EvaluateNurbsCurve(*Spline.Definition, Low).Position, EvaluateCurve(Analytic, Low).Position) / Scale);
        Row.Distance = std::max(Row.Distance, Distance(EvaluateNurbsCurve(*Spline.Definition, High).Position, EvaluateCurve(Analytic, High).Position) / Scale);
        return Row;
    }

    // ---- N2 ---------------------------------------------------------------------------------------------------------------

    [[nodiscard]] double ProbeSurfaceDerivatives(const NurbsSurface& Spline, std::mt19937_64& Generator, int Samples)
    {
        const NurbsSurfaceDefinition& Definition = *Spline.Definition;
        double Worst = 0.0;
        for (int Sample = 0; Sample < Samples; ++Sample)
        {
            double WidthU = 0.0, WidthV = 0.0;
            const double 𝑢 = InsideSpan(Generator, Definition.KnotsU, 0.1, WidthU), 𝑣 = InsideSpan(Generator, Definition.KnotsV, 0.1, WidthV);
            const double StepU = 2.0e-3 * WidthU, StepV = 2.0e-3 * WidthV;                   // [-] - Richardson base steps
            const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(Definition, { 𝑢, 𝑣 });
            const auto At = [&](double DeltaU, double DeltaV) { return EvaluateNurbsSurfaceDerivatives(Definition, { 𝑢 + DeltaU, 𝑣 + DeltaV }); };
            // Richardson of central differences: R = (4·D(h/2) − D(h))/3, error O(h⁴).
            const auto Richardson = [](const std::function<Vector3(double)>& Difference, double Step) { return (Difference(0.5 * Step) * 4.0 - Difference(Step)) * (1.0 / 3.0); };
            const Vector3 DU  = Richardson([&](double h) { return (At(h, 0).Position - At(-h, 0).Position) * (0.5 / h); }, StepU);
            const Vector3 DV  = Richardson([&](double h) { return (At(0, h).Position - At(0, -h).Position) * (0.5 / h); }, StepV);
            const Vector3 DUU = Richardson([&](double h) { return (At(h, 0).U - At(-h, 0).U) * (0.5 / h); }, StepU);
            const Vector3 DUV = Richardson([&](double h) { return (At(0, h).U - At(0, -h).U) * (0.5 / h); }, StepV);
            const Vector3 DVU = Richardson([&](double h) { return (At(h, 0).V - At(-h, 0).V) * (0.5 / h); }, StepU);
            const Vector3 DVV = Richardson([&](double h) { return (At(0, h).V - At(0, -h).V) * (0.5 / h); }, StepV);
            // Scale-free: first derivatives against Extent/width, second against Extent/width².
            const double E = Definition.Extent;
            Worst = std::max({ Worst, Length(S.U - DU) * WidthU / E, Length(S.V - DV) * WidthV / E, Length(S.UU - DUU) * WidthU * WidthU / E,
                               Length(S.UV - DUV) * WidthU * WidthV / E, Length(S.UV - DVU) * WidthU * WidthV / E, Length(S.VV - DVV) * WidthV * WidthV / E });
        }
        return Worst;
    }

    [[nodiscard]] double ProbeCurveDerivatives(const NurbsCurve& Spline, std::mt19937_64& Generator, int Samples)
    {
        const NurbsCurveDefinition& Definition = *Spline.Definition;
        double Extent = 0.0;
        for (const Vector3& Pole : Definition.Poles) Extent = std::max(Extent, Distance(Pole, Definition.Poles.front()));
        double Worst = 0.0;
        for (int Sample = 0; Sample < Samples; ++Sample)
        {
            double Width = 0.0;
            const double τ = InsideSpan(Generator, Definition.Knots, 0.1, Width);
            const double Step = 2.0e-3 * Width;
            const NurbsCurveDerivatives C = EvaluateNurbsCurveDerivatives(Definition, τ);
            const auto At = [&](double Delta) { return EvaluateNurbsCurveDerivatives(Definition, τ + Delta); };
            const auto First  = [&](double h) { return (At(h).Position - At(-h).Position) * (0.5 / h); };
            const auto Second = [&](double h) { return (At(h).First - At(-h).First) * (0.5 / h); };
            const Vector3 D1 = (First(0.5 * Step) * 4.0 - First(Step)) * (1.0 / 3.0);
            const Vector3 D2 = (Second(0.5 * Step) * 4.0 - Second(Step)) * (1.0 / 3.0);
            Worst = std::max({ Worst, Length(C.First - D1) * Width / Extent, Length(C.Second - D2) * Width * Width / Extent });
        }
        return Worst;
    }

    // ---- N3 ---------------------------------------------------------------------------------------------------------------

    [[nodiscard]] double ProbeSurfaceInversion(const NurbsSurface& Spline, std::mt19937_64& Generator, int Samples)
    {
        const NurbsSurfaceDefinition& Definition = *Spline.Definition;
        const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
        double Worst = 0.0;
        for (int Sample = 0; Sample < Samples; ++Sample)
        {
            const Vector2 Parameter{ Uniform(Generator, U.Low + 0.02 * (U.High - U.Low), U.High - 0.02 * (U.High - U.Low)),
                                     Uniform(Generator, V.Low + 0.02 * (V.High - V.Low), V.High - 0.02 * (V.High - V.Low)) };
            const Vector3 Position = EvaluateNurbsSurface(Spline, Parameter).Position;
            // ① On the surface: the foot must reproduce the point (any reference).
            const Vector2 Foot = ProjectOntoNurbsSurface(Spline, Position, { U.Low, V.Low });
            Worst = std::max(Worst, Distance(EvaluateNurbsSurface(Spline, Foot).Position, Position) / Definition.Extent);
            // ② Off the surface along the normal, well inside the curvature radius: the same foot, signed distance exact.
            const double  κ      = MeasureNurbsCurvature(Spline, Parameter);
            const double  Offset = std::min(0.02 * Definition.Extent, 0.1 / std::max(κ, 1.0e-12)) * (Sample % 2 == 0 ? 1.0 : -1.0);
            const Vector3 Normal = EvaluateNurbsNormal(Spline, Parameter);
            const Vector3 Lifted = Position + Normal * Offset;
            const SurfaceDistance Measure = MeasureSurfaceDistance(Spline, Lifted);
            Worst = std::max({ Worst, Distance(Measure.Foot, Position) / Definition.Extent, std::fabs(Measure.Distance - Offset) / Definition.Extent });
        }
        return Worst;
    }

    [[nodiscard]] double ProbeCurveInversion(const NurbsCurve& Spline, std::mt19937_64& Generator, int Samples)
    {
        const NurbsCurveDefinition& Definition = *Spline.Definition;
        const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
        double Extent = 0.0;
        for (const Vector3& Pole : Definition.Poles) Extent = std::max(Extent, Distance(Pole, Definition.Poles.front()));
        double Worst = 0.0;
        for (int Sample = 0; Sample < Samples; ++Sample)
        {
            const double  τ = Uniform(Generator, Domain.Low, Domain.High);
            const Vector3 Position = EvaluateNurbsCurve(Definition, τ).Position;
            const double  Foot = ProjectOntoNurbsCurve(Definition, Position, Domain.Low);
            Worst = std::max(Worst, Distance(EvaluateNurbsCurve(Definition, Foot).Position, Position) / Extent);
        }
        return Worst;
    }

    // ---- N4 ---------------------------------------------------------------------------------------------------------------

    // Rational de Casteljau in homogeneous coordinates (independent of the B-spline evaluator under test).
    [[nodiscard]] Vector3 EvaluateBezierPiece(const NurbsBezierPiece& Piece, double 𝑡)
    {
        std::vector<Vector3> Points(Piece.Poles.size());
        std::vector<double>  Weights = Piece.Weights;
        for (size_t Index = 0; Index < Points.size(); ++Index) Points[Index] = Piece.Poles[Index] * Piece.Weights[Index];
        for (size_t Level = 1; Level < Points.size(); ++Level)
            for (size_t Index = 0; Index + Level < Points.size(); ++Index)
            {
                Points[Index]  = Points[Index] * (1.0 - 𝑡) + Points[Index + 1] * 𝑡;
                Weights[Index] = Weights[Index] * (1.0 - 𝑡) + Weights[Index + 1] * 𝑡;
            }
        return Points.front() * (1.0 / Weights.front());
    }

    [[nodiscard]] double ProbeIsoCurves(const NurbsSurface& Spline, std::mt19937_64& Generator, int Samples)
    {
        const NurbsSurfaceDefinition& Definition = *Spline.Definition;
        const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
        double Worst = 0.0;
        for (int Sample = 0; Sample < Samples; ++Sample)
        {
            const bool   FixedU = Sample % 2 == 0;
            const double Value  = FixedU ? Uniform(Generator, U.Low, U.High) : Uniform(Generator, V.Low, V.High);
            const NurbsCurve Iso = ExtractNurbsIsoCurve(Spline, FixedU, Value);
            const NurbsDomain Run = FixedU ? V : U;
            for (int Index = 0; Index <= 32; ++Index)
            {
                const double τ = Run.Low + (Run.High - Run.Low) * Index / 32.0;
                const Vector3 Expected = EvaluateNurbsSurface(Spline, FixedU ? Vector2{ Value, τ } : Vector2{ τ, Value }).Position;
                Worst = std::max(Worst, Distance(EvaluateNurbsCurve(*Iso.Definition, τ).Position, Expected) / Definition.Extent);
            }
        }
        return Worst;
    }

    [[nodiscard]] double ProbePieces(const NurbsCurve& Spline, std::mt19937_64& Generator, int Samples)
    {
        const NurbsCurveDefinition& Definition = *Spline.Definition;
        const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
        double Extent = 0.0;
        for (const Vector3& Pole : Definition.Poles) Extent = std::max(Extent, Distance(Pole, Definition.Poles.front()));
        double Worst = 0.0;
        for (int Sample = 0; Sample < Samples; ++Sample)
        {
            double Low = Uniform(Generator, Domain.Low, Domain.High), High = Uniform(Generator, Domain.Low, Domain.High);
            if (Low > High) std::swap(Low, High);
            if (Sample == 0) { Low = Domain.Low; High = Domain.High; }
            for (const NurbsBezierPiece& Piece : DecomposeNurbsCurve(Definition, Low, High))
            {
                for (int Index = 0; Index <= 16; ++Index)
                {
                    const double 𝑡 = Index / 16.0;
                    const double τ = Piece.PieceLow + 𝑡 * (Piece.PieceHigh - Piece.PieceLow);
                    Worst = std::max(Worst, Distance(EvaluateBezierPiece(Piece, 𝑡), EvaluateNurbsCurve(Definition, τ).Position) / Extent);
                }
            }
        }
        // Reversal: R(−τ) ≡ C(τ).
        const NurbsCurve Reversed = ReverseNurbsCurve(Definition);
        for (int Index = 0; Index <= 64; ++Index)
        {
            const double τ = Domain.Low + (Domain.High - Domain.Low) * Index / 64.0;
            Worst = std::max(Worst, Distance(EvaluateNurbsCurve(*Reversed.Definition, -τ).Position, EvaluateNurbsCurve(Definition, τ).Position) / Extent);
        }
        return Worst;
    }

    // ---- N5–N7 bodies -----------------------------------------------------------------------------------------------------

    struct BodyRow
    {
        std::string Name        = {};
        std::string Outcome     = {};
        int         Faces       = 0;
        int         NurbsFaces  = 0;
        double      Volume      = 0.0;                                                       // [len³] - Integrated
        double      Expected    = 0.0;                                                       // [len³] - Closed form / reference
        double      Error       = 0.0;                                                       // [-]    - Relative
        int         Triangles   = 0;
        double      PolygonGap  = 0.0;
        double      PolygonBound = 0.0;
        int         Samples     = 0, Wrong = 0;                                              // [-]    - Oracle
        double      Milliseconds = 0.0;
        bool        PassedCondition = false;
    };

    [[nodiscard]] int CountNurbsFaces(const ModelPartition& Partition, BodyToken Body)
    {
        int Count = 0;
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
            if (std::holds_alternative<NurbsSurface>(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface))) ++Count;
        return Count;
    }

    [[nodiscard]] double VertexHomeGap(const ModelPartition& Partition, BodyToken Original, BodyToken Returned)
    {
        const std::vector<VertexToken> Before = Partition.CollectInventory(Original).Vertices;
        const std::vector<VertexToken> After  = Partition.CollectInventory(Returned).Vertices;
        if (Before.size() != After.size()) return 1.0e300;
        double Worst = 0.0;
        for (VertexToken Vertex : After)
        {
            double Nearest = 1.0e300;
            for (VertexToken Home : Before) Nearest = std::min(Nearest, Distance(Partition.Vertices.Resolve(Vertex).Position, Partition.Vertices.Resolve(Home).Position));
            Worst = std::max(Worst, Nearest);
        }
        return Worst;
    }

    // Audit, volume against Expected, tessellation closure and the polygon-volume bound; the oracle is optional.
    void MeasureBody(ModelPartition& Partition, BodyToken Body, BodyRow& Row, const std::function<int(Vector3)>& Oracle, Vector3 Low, Vector3 High)
    {
        const AuditReport Audit = AuditBody(Partition, Body);
        Row.Faces      = static_cast<int>(Partition.CollectInventory(Body).Faces.size());
        Row.NurbsFaces = CountNurbsFaces(Partition, Body);
        Row.Volume     = IntegrateBodyVolume(Partition, Body);
        Row.Error      = std::fabs(Row.Volume - Row.Expected) / std::fabs(Row.Expected);
        TessellationSettings Fine;
        Fine.SagRatio = 2.0e-4;
        const TessellationOutcome     Complex    = TessellateBody(Partition, Body, Fine);
        const TessellationAuditReport Polygon = Complex.Refusal.empty() ? AuditTessellation(Partition, Body, Complex) : TessellationAuditReport{};
        Row.Triangles    = Polygon.Triangles;
        Row.PolygonGap   = std::fabs(Polygon.ExactVolume - Polygon.ComplexVolume);
        Row.PolygonBound = Polygon.PeakDeviation * Polygon.ComplexArea;
        bool OracleCondition = true;
        if (Oracle)
        {
            const BodyContainment Containment = PrepareBodyContainment(Partition, Body);
            WindingSettings Settings = {};
            Settings.OuterTolerance = 1.0e-6;
            Settings.InnerTolerance = 1.0e-7;
            std::mt19937_64 Generator(0x5EED);
            for (int Attempt = 0; Attempt < 2000 && Row.Samples < 100; ++Attempt)
            {
                const Vector3 Point{ Uniform(Generator, Low.X, High.X), Uniform(Generator, Low.Y, High.Y), Uniform(Generator, Low.Z, High.Z) };
                const int Expected = Oracle(Point);                                          // [-] - 1 inside, 0 outside, −1 too near
                if (Expected < 0) continue;
                ++Row.Samples;
                const PointMembership Membership = ClassifySolidPoint(Containment, Point, Settings).Membership;
                if ((Membership == PointMembership::Inside) != (Expected == 1)) ++Row.Wrong;
            }
            OracleCondition = Row.Wrong == 0 && Row.Samples > 0;
        }
        Row.PassedCondition = Audit.SoundCondition() && Complex.Refusal.empty() && Polygon.UnpairedSides == 0 && Row.PolygonGap <= Row.PolygonBound && OracleCondition && Row.Error <= 1.0e-9;
        if (!Audit.SoundCondition()) Row.Outcome = "audit: " + Audit.Findings.front().Detail;
        else if (!Complex.Refusal.empty()) Row.Outcome = "tessellation: " + Complex.Refusal;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    const auto      Start = Clock::now();
    std::mt19937_64 Generator(20261021);
    std::vector<ReproductionRow> Reproductions;
    std::vector<BodyRow>         AffineRows, PromotedRows, BooleanRows;

    // ---- N1 exact reproduction of conics and quadrics (random frames).
    for (int FrameIndex = 0; FrameIndex < 3; ++FrameIndex)
    {
        const OrthonormalFrame Frame = RandomFrame(Generator);
        const double Scale = Length(Frame.Origin) + 2.0;                                     // [len] - Coordinate magnitude
        const double r = Uniform(Generator, 0.3, 1.5);
        Reproductions.push_back(ProbeCurveReproduction("Circle (full turn)", CircularArc{ Frame, r }, 0.4, 0.4 + FullTurn, Scale));
        Reproductions.push_back(ProbeCurveReproduction("Circle arc (−1, 2)", CircularArc{ Frame, r }, -1.0, 2.0, Scale));
        Reproductions.push_back(ProbeCurveReproduction("Ellipse (full turn)", EllipticalArc{ Frame, 1.7 * r, r }, 0.0, FullTurn, Scale));
        Reproductions.push_back(ProbeCurveReproduction("Line (−3, 4)", StraightLine{ Frame.Origin, Frame.ZAxis }, -3.0, 4.0, Scale));
        Reproductions.push_back(ProbeSurfaceReproduction("Plane", PlaneSurface{ Frame }, { -1.0, -2.0 }, { 3.0, 1.0 }, Scale));
        Reproductions.push_back(ProbeSurfaceReproduction("Cylinder", CylinderSurface{ Frame, r }, { 0.3, -1.0 }, { 0.3 + FullTurn, 2.0 }, Scale));
        Reproductions.push_back(ProbeSurfaceReproduction("Cone", ConeSurface{ Frame, r, -0.6 }, { -1.0, -0.5 }, { -1.0 + FullTurn, 0.4 * r }, Scale));
        Reproductions.push_back(ProbeSurfaceReproduction("Sphere", SphereSurface{ Frame, r }, { 0.0, -0.5 * π }, { FullTurn, 0.5 * π }, Scale));
        Reproductions.push_back(ProbeSurfaceReproduction("Torus", TorusSurface{ Frame, 2.0 * r, 0.6 * r }, { 0.7, -π }, { 0.7 + FullTurn, π }, Scale));
    }
    for (const ReproductionRow& Row : Reproductions)
    {
        Tally(ReproductionGate, Row.Distance, 1.0e-14);
        Tally(ReproductionGate, Row.Angle, 1.0e-13);
    }

    // ---- N2–N4 on freeform carriers (random rational, non-uniform knots, degrees 2…5) and on converted quadrics.
    std::vector<NurbsSurface> Surfaces = { RandomSurface(Generator, 3, 3, 6, 7), RandomSurface(Generator, 2, 4, 5, 6), RandomSurface(Generator, 5, 2, 8, 4) };
    {
        const OrthonormalFrame Frame = RandomFrame(Generator);
        Surfaces.push_back(*ConvertSurfaceToNurbs(SphereSurface{ Frame, 0.9 }, { 0.0, -0.5 * π }, { FullTurn, 0.5 * π }));
        Surfaces.push_back(*ConvertSurfaceToNurbs(TorusSurface{ Frame, 1.5, 0.4 }, { 0.2, 0.0 }, { 0.2 + FullTurn, FullTurn }));
    }
    const std::vector<NurbsCurve> Curves = { RandomCurve(Generator, 3, 9), RandomCurve(Generator, 4, 7), RandomCurve(Generator, 2, 6),
                                             ConvertConicToNurbs(*ResolveConicForm(EllipticalArc{ RandomFrame(Generator), 1.4, 0.6 }), 0.0, FullTurn) };
    for (const NurbsSurface& Spline : Surfaces)
    {
        Tally(DerivativeGate, ProbeSurfaceDerivatives(Spline, Generator, 60), 1.0e-8);
        Tally(InversionGate, ProbeSurfaceInversion(Spline, Generator, 80), 1.0e-12);
        Tally(IsoGate, ProbeIsoCurves(Spline, Generator, 20), 1.0e-14);
    }
    for (const NurbsCurve& Spline : Curves)
    {
        Tally(DerivativeGate, ProbeCurveDerivatives(Spline, Generator, 60), 1.0e-8);
        Tally(InversionGate, ProbeCurveInversion(Spline, Generator, 80), 1.0e-12);
        Tally(IsoGate, ProbePieces(Spline, Generator, 12), 1.0e-14);
    }

    // ---- N5 carrier level: shear + non-uniform scale act on the poles exactly.
    const AffineMap Shear{ { 1.3, 0.2, -0.1 }, { 0.4, 0.8, 0.3 }, { -0.2, 0.1, 1.6 }, { 0.5, -1.0, 2.0 } };
    const auto Apply = [&](Vector3 Point) { return Shear.ColumnX * Point.X + Shear.ColumnY * Point.Y + Shear.ColumnZ * Point.Z + Shear.Shift; };
    for (const NurbsSurface& Spline : Surfaces)
    {
        const NurbsSurface Image = MapNurbsSurface(*Spline.Definition, Apply);
        const NurbsDomain U = MeasureNurbsSurfaceDomain(*Spline.Definition, true), V = MeasureNurbsSurfaceDomain(*Spline.Definition, false);
        double Worst = 0.0;
        for (int I = 0; I <= 20; ++I)
            for (int J = 0; J <= 20; ++J)
            {
                const Vector2 Parameter{ U.Low + (U.High - U.Low) * I / 20.0, V.Low + (V.High - V.Low) * J / 20.0 };
                Worst = std::max(Worst, Distance(EvaluateNurbsSurface(Image, Parameter).Position, Apply(EvaluateNurbsSurface(Spline, Parameter).Position)) / Image.Definition->Extent);
            }
        Tally(AffineGate, Worst, 1.0e-12);
    }

    ModelPartition Partition;
    const double   Determinant = Dot(Shear.ColumnX, Cross(Shear.ColumnY, Shear.ColumnZ));
    // Inverse of the shear (adjugate / det) for the way back.
    const auto Inverse = [&](const AffineMap& Map)
    {
        const double  Det = Dot(Map.ColumnX, Cross(Map.ColumnY, Map.ColumnZ));
        const Vector3 RowX = Cross(Map.ColumnY, Map.ColumnZ) * (1.0 / Det), RowY = Cross(Map.ColumnZ, Map.ColumnX) * (1.0 / Det), RowZ = Cross(Map.ColumnX, Map.ColumnY) * (1.0 / Det);
        AffineMap Result{ { RowX.X, RowY.X, RowZ.X }, { RowX.Y, RowY.Y, RowZ.Y }, { RowX.Z, RowY.Z, RowZ.Z }, {} };
        Result.Shift = -(Result.ColumnX * Map.Shift.X + Result.ColumnY * Map.Shift.Y + Result.ColumnZ * Map.Shift.Z);
        return Result;
    };

    // ---- N5 bodies: non-uniform scale and shear promote curved faces to NURBS (formerly refused), there and back.
    {
        struct AffineSpecimen { std::string Name; std::function<EditOutcome<BodyToken>()> Build; AffineMap Map; };
        const OrthonormalFrame Upright = ConstructFrame({ 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 });
        const AffineSpecimen Specimens[] = {
            { "Cylinder r 1 × 2, scale (2, 1, 1) → elliptic cylinder", [&] { return ConstructCylinder(Partition, Upright, 1.0, 2.0); }, ScalingMap({ 0, 0, 0 }, { 2.0, 1.0, 1.0 }) },
            { "Sphere r 1, general shear → ellipsoid",                   [&] { return ConstructSphere(Partition, Upright, 1.0); }, Shear },
            { "Torus 2 / 0.5, scale (1, 1.5, 0.5)",                      [&] { return ConstructTorus(Partition, Upright, 2.0, 0.5); }, ScalingMap({ 0.3, 0, 0 }, { 1.0, 1.5, 0.5 }) },
            { "Cone r 1 × 1.5, general shear",                           [&] { return ConstructCone(Partition, Upright, 1.0, 1.5); }, Shear },
        };
        for (const AffineSpecimen& Specimen : Specimens)
        {
            const auto Moment = Clock::now();
            BodyRow Row;
            Row.Name = Specimen.Name;
            const EditOutcome<BodyToken> Source = Specimen.Build();
            if (!Source.SucceededCondition()) { Row.Outcome = "primitive refused"; AffineRows.push_back(Row); Tally(AffineGate, 1.0, 0.0); continue; }
            const double Original = IntegrateBodyVolume(Partition, Source.Value);
            const double Det = Dot(Specimen.Map.ColumnX, Cross(Specimen.Map.ColumnY, Specimen.Map.ColumnZ));
            const TransformOutcome There = TransformBody(Partition, Source.Value, Specimen.Map);
            if (!There.Refusal.empty()) { Row.Outcome = There.Refusal; AffineRows.push_back(Row); Tally(AffineGate, 1.0, 0.0); continue; }
            Row.Expected = Original * Det;
            MeasureBody(Partition, There.Body, Row, nullptr, {}, {});
            const TransformOutcome Back = TransformBody(Partition, There.Body, Inverse(Specimen.Map));
            const double Home = Back.Refusal.empty() ? VertexHomeGap(Partition, Source.Value, Back.Body) : 1.0e300;
            const double BackError = Back.Refusal.empty() ? std::fabs(IntegrateBodyVolume(Partition, Back.Body) - Original) / Original : 1.0e300;
            Row.Outcome = Row.Outcome.empty() ? (Back.Refusal.empty() ? "there and back" : "back: " + Back.Refusal) : Row.Outcome;
            Row.PassedCondition = Row.PassedCondition && Home <= 1.0e-9 && BackError <= 1.0e-9 && Back.Refusal.empty() && AuditBody(Partition, Back.Body).SoundCondition();
            Row.Error = std::max({ Row.Error, BackError, Home });
            Row.Milliseconds = Elapsed(Moment);
            Tally(AffineGate, Row.PassedCondition ? Row.Error : 1.0, 1.0e-9);
            AffineRows.push_back(Row);
        }
        (void)Determinant;
    }

    // ---- N6 promoted bodies: every analytic face becomes NURBS; closed-form volume, tessellation, containment oracle.
    {
        struct PromotedSpecimen { std::string Name; std::function<EditOutcome<BodyToken>()> Build; double Expected; std::function<int(Vector3)> Oracle; Vector3 Low, High; };
        const OrthonormalFrame Upright = ConstructFrame({ 0.2, -0.1, 0.3 }, { 0, 0, 1 }, { 1, 0, 0 });
        const Vector3 Centre = Upright.Origin;
        const double  Clear  = 2.0e-3;                                                       // [len] - Oracle clearance
        const auto    Classify = [&](double Signed) { return std::fabs(Signed) < Clear ? -1 : (Signed < 0.0 ? 1 : 0); };   // Signed < 0 inside
        const PromotedSpecimen Specimens[] = {
            { "Block 2 × 1 × 0.5", [&] { return ConstructBlock(Partition, { -1, -0.5, 0 }, { 2, 1, 0.5 }); }, 1.0,
              [&](Vector3 P) { return Classify(std::max({ std::fabs(P.X) - 1.0, std::fabs(P.Y) - 0.5, std::fabs(P.Z - 0.25) - 0.25 })); }, { -1.2, -0.7, -0.2 }, { 1.2, 0.7, 0.7 } },
            { "Cylinder r 0.8 × 1.5", [&] { return ConstructCylinder(Partition, Upright, 0.8, 1.5); }, π * 0.64 * 1.5,
              [&](Vector3 P) { const Vector3 Q = P - Centre; return Classify(std::max(std::hypot(Q.X, Q.Y) - 0.8, std::fabs(Q.Z - 0.75) - 0.75)); }, Centre + Vector3{ -1, -1, -0.2 }, Centre + Vector3{ 1, 1, 1.7 } },
            { "Cone r 0.8 × 1.2", [&] { return ConstructCone(Partition, Upright, 0.8, 1.2); }, π * 0.64 * 1.2 / 3.0,
              [&](Vector3 P) { const Vector3 Q = P - Centre; const double ρ = 0.8 * (1.0 - Q.Z / 1.2); return Classify(std::max({ (std::hypot(Q.X, Q.Y) - ρ) * 0.83205, -Q.Z, Q.Z - 1.2 })); },
              Centre + Vector3{ -1, -1, -0.2 }, Centre + Vector3{ 1, 1, 1.4 } },
            { "Frustum r 0.9 → 0.4 × 1", [&] { return ConstructFrustum(Partition, Upright, 0.9, 0.4, 1.0); }, π * 1.0 * (0.81 + 0.36 + 0.16) / 3.0, nullptr, {}, {} },
            { "Sphere r 0.9", [&] { return ConstructSphere(Partition, Upright, 0.9); }, 4.0 / 3.0 * π * 0.729,
              [&](Vector3 P) { return Classify(Length(P - Centre) - 0.9); }, Centre + Vector3{ -1, -1, -1 }, Centre + Vector3{ 1, 1, 1 } },
            { "Torus 1.2 / 0.35", [&] { return ConstructTorus(Partition, Upright, 1.2, 0.35); }, 2.0 * π * π * 1.2 * 0.35 * 0.35,
              [&](Vector3 P) { const Vector3 Q = P - Centre; return Classify(std::hypot(std::hypot(Q.X, Q.Y) - 1.2, Q.Z) - 0.35); }, Centre + Vector3{ -1.7, -1.7, -0.5 }, Centre + Vector3{ 1.7, 1.7, 0.5 } },
        };
        for (const PromotedSpecimen& Specimen : Specimens)
        {
            const auto Moment = Clock::now();
            BodyRow Row;
            Row.Name = Specimen.Name;
            Row.Expected = Specimen.Expected;
            const EditOutcome<BodyToken> Source = Specimen.Build();
            const TransformOutcome Promoted = Source.SucceededCondition() ? PromoteBodyToNurbs(Partition, Source.Value) : TransformOutcome{ {}, "primitive refused" };
            if (!Promoted.Refusal.empty()) { Row.Outcome = Promoted.Refusal; PromotedRows.push_back(Row); Tally(BodyGate, 1.0, 0.0); continue; }
            MeasureBody(Partition, Promoted.Body, Row, Specimen.Oracle, Specimen.Low, Specimen.High);
            if (Row.Outcome.empty()) Row.Outcome = "promoted";
            Row.Milliseconds = Elapsed(Moment);
            Tally(BodyGate, Row.PassedCondition ? Row.Error : 1.0, 1.0e-9);
            PromotedRows.push_back(Row);
        }
        // Composites: a filleted block (cylinder + sphere blend faces) and a bored disc (traced intersection edges kept).
        {
            const auto Moment = Clock::now();
            BodyRow Row;
            Row.Name = "Filleted block 2 × 1.5 × 1, r 0.2 on every edge";
            const EditOutcome<BodyToken> Block = ConstructBlock(Partition, { 0, 0, 0 }, { 2, 1.5, 1 });
            BlendSettings Settings;
            Settings.Profile = BlendProfile::Fillet;
            Settings.Size    = 0.2;
            const BlendOutcome Blended = BlendEdges(Partition, Block.Value, Partition.CollectInventory(Block.Value).Edges, Settings);
            const TransformOutcome Promoted = Blended.Refusal.empty() ? PromoteBodyToNurbs(Partition, Blended.Body) : TransformOutcome{ {}, "blend: " + Blended.Refusal };
            if (Promoted.Refusal.empty())
            {
                // 📝 Closed form: box − (edge strips)·(1 − π/4)·r² per unit length + corners (sphere octants vs cubes).
                const double r = 0.2, Edges = 4.0 * ((2.0 - 2 * r) + (1.5 - 2 * r) + (1.0 - 2 * r));
                Row.Expected = 2.0 * 1.5 * 1.0 - Edges * r * r * (1.0 - π / 4.0) - 8.0 * (r * r * r - π * r * r * r / 6.0) - 0.0;
                // Each corner: cube r³ minus the octant of the ball; the three edge strips through it stop at the corner cube.
                MeasureBody(Partition, Promoted.Body, Row, nullptr, {}, {});
                if (Row.Outcome.empty()) Row.Outcome = "promoted";
            }
            else Row.Outcome = Promoted.Refusal;
            Row.Milliseconds = Elapsed(Moment);
            Tally(BodyGate, Row.PassedCondition ? Row.Error : 1.0, 1.0e-9);
            PromotedRows.push_back(Row);
        }
        {
            const auto Moment = Clock::now();
            BodyRow Row;
            Row.Name = "Disc r 2 × 1 − radial bore r 0.3 (traced edges on NURBS faces)";
            const auto Disc = ConstructCylinder(Partition, ConstructFrame({ 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }), 2.0, 1.0);
            const auto Bore = ConstructCylinder(Partition, ConstructFrame({ 1.0, 0.0, 0.5 }, { 1, 0, 0 }, { 0, 0, 1 }), 0.3, 2.0);
            const BooleanOutcome Bored = ComputeBoolean(Partition, Disc.Value, Bore.Value, BooleanOperation::Difference);
            const TransformOutcome Promoted = Bored.Refusal.empty() ? PromoteBodyToNurbs(Partition, Bored.Body) : TransformOutcome{ {}, "boolean: " + Bored.Refusal };
            if (Promoted.Refusal.empty())
            {
                Row.Expected = IntegrateBodyVolume(Partition, Bored.Body);                    // The analytic body is the reference
                MeasureBody(Partition, Promoted.Body, Row, nullptr, {}, {});
                if (Row.Outcome.empty()) Row.Outcome = "promoted (reference: analytic body)";
            }
            else Row.Outcome = Promoted.Refusal;
            Row.Milliseconds = Elapsed(Moment);
            Tally(BodyGate, Row.PassedCondition ? Row.Error : 1.0, 1.0e-9);
            PromotedRows.push_back(Row);
        }
    }

    // ---- N7 Booleans with NURBS operands.
    {
        struct BooleanSpecimen
        {
            std::string Name;
            std::function<BodyToken()> First, Second;
            BooleanOperation Operation;
            bool PromoteFirst, PromoteSecond;
            double Expected;                                                                 // [len³] - 0: use the analytic Boolean
        };
        const auto Primitive = [&](EditOutcome<BodyToken> Outcome) { return Outcome.Value; };
        const OrthonormalFrame Upright = ConstructFrame({ 0, 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 });
        const double R = 1.0, a = 0.4;
        const BooleanSpecimen Specimens[] = {
            { "NURBS sphere r 1 − analytic cylinder r 0.4 through the centre", [&] { return Primitive(ConstructSphere(Partition, Upright, R)); },
              [&] { return Primitive(ConstructCylinder(Partition, ConstructFrame({ 0, 0, -1.5 }, { 0, 0, 1 }, { 1, 0, 0 }), a, 3.0)); }, BooleanOperation::Difference, true, false,
              4.0 / 3.0 * π * std::pow(R * R - a * a, 1.5) },
            { "Analytic block 2 × 2 × 1 − NURBS cylinder r 0.5 through", [&] { return Primitive(ConstructBlock(Partition, { -1, -1, 0 }, { 2, 2, 1 })); },
              [&] { return Primitive(ConstructCylinder(Partition, ConstructFrame({ 0.1, -0.2, -0.5 }, { 0, 0, 1 }, { 1, 0, 0 }), 0.5, 2.0)); }, BooleanOperation::Difference, false, true,
              4.0 - π * 0.25 },
            { "NURBS cylinder r 0.6 ∪ analytic block (partial overlap)", [&] { return Primitive(ConstructCylinder(Partition, Upright, 0.6, 1.0)); },
              [&] { return Primitive(ConstructBlock(Partition, { 0.2, -0.3, 0.25 }, { 1.0, 0.6, 0.5 })); }, BooleanOperation::Union, true, false, 0.0 },
            { "NURBS torus 1.2 / 0.4 ∩ analytic block (half)", [&] { return Primitive(ConstructTorus(Partition, Upright, 1.2, 0.4)); },
              [&] { return Primitive(ConstructBlock(Partition, { -2, -2, -1 }, { 4, 2.3, 2 })); }, BooleanOperation::Intersection, true, false, 0.0 },
            { "NURBS sphere r 1 − NURBS cylinder r 0.4 (both NURBS)", [&] { return Primitive(ConstructSphere(Partition, Upright, R)); },
              [&] { return Primitive(ConstructCylinder(Partition, ConstructFrame({ 0, 0, -1.5 }, { 0, 0, 1 }, { 1, 0, 0 }), a, 3.0)); }, BooleanOperation::Difference, true, true,
              4.0 / 3.0 * π * std::pow(R * R - a * a, 1.5) },
        };
        for (const BooleanSpecimen& Specimen : Specimens)
        {
            const auto Moment = Clock::now();
            BodyRow Row;
            Row.Name = Specimen.Name;
            const BodyToken First = Specimen.First(), Second = Specimen.Second();
            if (Specimen.Expected > 0.0) Row.Expected = Specimen.Expected;
            else
            {
                const BooleanOutcome Reference = ComputeBoolean(Partition, First, Second, Specimen.Operation);
                Row.Expected = Reference.Refusal.empty() ? IntegrateBodyVolume(Partition, Reference.Body) : std::nan("");
            }
            const TransformOutcome A = Specimen.PromoteFirst ? PromoteBodyToNurbs(Partition, First) : TransformOutcome{ First, {} };
            const TransformOutcome B = Specimen.PromoteSecond ? PromoteBodyToNurbs(Partition, Second) : TransformOutcome{ Second, {} };
            if (!A.Refusal.empty() || !B.Refusal.empty()) { Row.Outcome = A.Refusal.empty() ? B.Refusal : A.Refusal; BooleanRows.push_back(Row); Tally(BooleanGate, 1.0, 0.0); continue; }
            const BooleanOutcome Result = ComputeBoolean(Partition, A.Body, B.Body, Specimen.Operation);
            if (!Result.Refusal.empty()) Row.Outcome = "boolean: " + Result.Refusal;
            else
            {
                MeasureBody(Partition, Result.Body, Row, nullptr, {}, {});
                if (Row.Outcome.empty()) Row.Outcome = Specimen.Expected > 0.0 ? "closed form" : "reference: analytic Boolean";
            }
            Row.Milliseconds = Elapsed(Moment);
            Tally(BooleanGate, Row.PassedCondition ? Row.Error : 1.0, 1.0e-9);
            BooleanRows.push_back(Row);
        }
    }

    // ---- Report.
    const GateTally* Gates[] = { &ReproductionGate, &DerivativeGate, &InversionGate, &IsoGate, &AffineGate, &BodyGate, &BooleanGate };
    bool PassedCondition = true;
    for (const GateTally* Gate : Gates) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# NURBS carrier validation (rational B-spline curves and surfaces as kernel carriers)\n\n");
    std::printf("**%s** · %zu reproduction probes · %zu surfaces · %zu curves · %zu bodies · %.0f ms\n\n", PassedCondition ? "🟢 PASS" : "🔴 FAIL", Reproductions.size(), Surfaces.size(), Curves.size(),
                AffineRows.size() + PromotedRows.size() + BooleanRows.size(), Elapsed(Start));
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : Gates)
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## N1 reproduction (worst over 41 × 41 or 401 samples per probe, 3 random frames)\n\n| Carrier | Worst distance / scale | Worst normal angle [rad] |\n|---|---|---|\n");
    for (const char* Kind : { "Circle (full turn)", "Circle arc (−1, 2)", "Ellipse (full turn)", "Line (−3, 4)", "Plane", "Cylinder", "Cone", "Sphere", "Torus" })
    {
        double Gap = 0.0, Angle = 0.0;
        for (const ReproductionRow& Row : Reproductions) if (Row.Name == Kind) { Gap = std::max(Gap, Row.Distance); Angle = std::max(Angle, Row.Angle); }
        std::printf("| %s | %.2e | %.2e |\n", Kind, Gap, Angle);
    }
    const auto PrintBodies = [](const char* Title, const std::vector<BodyRow>& Rows)
    {
        std::printf("\n## %s\n\n| | Specimen | Faces | NURBS faces | V | V* | Relative error | Triangles | \\|V − V_polygon\\| | Bound | Oracle | ms | Outcome |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n", Title);
        for (const BodyRow& Row : Rows)
        {
            std::printf("| %s | %s | %d | %d | %.12f | %.12f | %.1e | %d | %.1e | %.1e | %d/%d | %.0f | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Name.c_str(), Row.Faces, Row.NurbsFaces, Row.Volume,
                        Row.Expected, Row.Error, Row.Triangles, Row.PolygonGap, Row.PolygonBound, Row.Wrong, Row.Samples, Row.Milliseconds, Row.Outcome.c_str());
        }
    };
    PrintBodies("N5 affine bodies (V* = det M · V, then the inverse map back home)", AffineRows);
    PrintBodies("N6 promoted bodies (oracle: wrong / samples, closed-form membership)", PromotedRows);
    PrintBodies("N7 Booleans with NURBS operands", BooleanRows);
    return PassedCondition ? 0 : 1;
}
