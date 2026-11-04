/*====================================================================================================================================
                                                         SWEEPVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Revolve / sweep R1–R2: spline revolutions (exact NURBS), axis without a line, Evolution along lines, arcs and splines

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Feature/FeatureTree.h"
#include "Engine/GeometryProcessing/Feature/PathSweep.h"
#include "Engine/GeometryProcessing/Geometry/NurbsCarrier.h"
#include "Engine/GeometryProcessing/Sketch/SketchProfile.h"

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

    GateTally SplineRevolveGate = { "R1", "Spline revolve: a vase wall (NURBS spline) turned a full turn about a profile edge — every sampled point of the NURBS face sits at the spline's (r, z), volume = 2π·A·r̄ (Pappus, exact profile area / centroid), audit sound", "≤ 10⁻¹² · rel. ≤ 10⁻⁹" };
    GateTally PartialGate       = { "R2", "Partial spline revolve 120° about the sketch x-axis given as point + direction (no axis line): volume = θ·A·ȳ, two plane caps, far cap = the profile turned by θ", "≤ 10⁻¹² · rel. ≤ 10⁻⁹" };
    GateTally DomeGate          = { "R3", "Spline ending on the axis (dome): the pole collapses to one vertex, volume by Pappus, audit sound", "rel. ≤ 10⁻⁹" };
    GateTally StraightGate      = { "R4", "Evolution along an oblique line: square with a round hole, every side a NURBS skin of the exact carried section, volume A·Δz, end cap = start cap + d", "≤ 10⁻¹² · rel. ≤ 10⁻⁹" };
    GateTally ArcGate           = { "R5", "Evolution along a quarter arc: rectangle ⟂ the path sweeps the revolve solid — volume A·R̄·θ, end vertices = start vertices turned 90° about the arc axis", "≤ 10⁻⁵·scale · rel. ≤ 10⁻⁶" };
    GateTally SpaceGate         = { "R6", "Evolution along a non-planar spline: profile ⟂ the path with its centroid on it — volume A·L (rotation-minimising frames), circle profile = pipe volume, audit sound", "≤ 10⁻⁵·scale · rel. ≤ 10⁻⁵" };
    GateTally TreeGate          = { "R7", "Feature steps: Revolve with AxisOrigin / AxisDirection, Evolution from a profile sketch along a path sketch (spline), Evolution subtracted from a box; face names Side(curve) / Cap(Start) / Cap(End)", "rel. ≤ 10⁻⁶" };
    GateTally RefuseGate        = { "R8", "Refusals name the cause: spline crossing the axis, axis without direction, path along the profile plane, profile beyond the bend, closed path, path = profile, multi-curve path", "all named" };

    std::vector<GateTally*> AllGates() { return { &SplineRevolveGate, &PartialGate, &DomeGate, &StraightGate, &ArcGate, &SpaceGate, &TreeGate, &RefuseGate }; }

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
        std::snprintf(Text, sizeof Text, "%.9f", Value);
        return Text;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                PROFILES
    //--------------------------------------------------------------------------------------------------------------------

    // Closed chain: lines between consecutive corners, except the run [SplineFirst, SplineLast] which is one NURBS spline.
    [[nodiscard]] SketchModel ChainSketch(const std::vector<Vector2>& Corners, int SplineFirst, int SplineLast)
    {
        SketchModel Model;
        for (const Vector2& Corner : Corners) AppendSketchPoint(Model, Corner);
        const int Count = static_cast<int>(Corners.size());
        int Identifier = 1;
        for (int Index = 0; Index < Count; )
        {
            if (Index == SplineFirst)
            {
                std::vector<int> Poles;
                for (int Pole = SplineFirst; Pole <= SplineLast; ++Pole) Poles.push_back(Pole % Count);
                AppendSketchSpline(Model, Poles, SketchSplineForm::Nurbs, Identifier++);
                Index = SplineLast;
                if (Index >= Count) break;
                continue;
            }
            AppendSketchLine(Model, Index, (Index + 1) % Count, Identifier++);
            ++Index;
        }
        return Model;
    }

    [[nodiscard]] SketchModel RectangleSketch(Vector2 Low, Vector2 High)
    {
        SketchModel Model;
        AppendSketchPoint(Model, Low); AppendSketchPoint(Model, { High.X, Low.Y }); AppendSketchPoint(Model, High); AppendSketchPoint(Model, { Low.X, High.Y });
        for (int Index = 0; Index < 4; ++Index) AppendSketchLine(Model, Index, (Index + 1) % 4, Index + 1);
        return Model;
    }

    [[nodiscard]] Vector3 Lift(const OrthonormalFrame& Plane, Vector2 Point) { return Plane.Origin + Plane.XAxis * Point.X + Plane.YAxis * Point.Y; }

    struct BodyRun
    {
        ModelPartition Partition = {};
        BodyToken      Body      = {};
        std::string    Refusal   = {};
    };

    [[nodiscard]] std::string AuditText(const ModelPartition& Partition, BodyToken Body, bool& Sound)
    {
        const AuditReport Audit = AuditBody(Partition, Body);
        Sound = Audit.SoundCondition();
        return Sound ? std::string("sound") : "UNSOUND " + std::string(Audit.Findings.front().Code) + " " + Audit.Findings.front().Detail;
    }

    [[nodiscard]] std::vector<Vector3> BodyVertices(const ModelPartition& Partition, BodyToken Body)
    {
        std::vector<Vector3> Points;
        for (VertexToken Vertex : Partition.CollectInventory(Body).Vertices) Points.push_back(Partition.Vertices.Resolve(Vertex).Position);
        return Points;
    }

    // Largest distance from each expected point to its nearest body vertex.
    [[nodiscard]] double NearestGap(const std::vector<Vector3>& Expected, const std::vector<Vector3>& Actual)
    {
        double Worst = 0.0;
        for (const Vector3& Point : Expected)
        {
            double Best = 1e300;
            for (const Vector3& Other : Actual) Best = std::min(Best, Distance(Point, Other));
            Worst = std::max(Worst, Best);
        }
        return Worst;
    }

    // Worst (r, z) gap of every NURBS face against the spline segment it revolves (𝑣 = the spline's τ).
    [[nodiscard]] double RevolutionGap(const ModelPartition& Partition, BodyToken Body, const ProfileSegment& Spline, Vector3 Origin, Vector3 Axis, Vector3 Radial, Vector2 AxisPoint, Vector2 Along)
    {
        double Worst = 0.0;
        const Vector2 Side = { -Along.Y, Along.X };
        const double Sign = Dot(Spline.Start - AxisPoint, Side) + Dot(Spline.End - AxisPoint, Side) >= 0.0 ? 1.0 : -1.0;
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
        {
            const FaceProfile& Profile = Partition.Faces.Resolve(Face);
            const auto* Surface = std::get_if<NurbsSurface>(&Partition.Surfaces.Resolve(Profile.Surface));
            if (!Surface) continue;
            const NurbsDomain AlongU = MeasureNurbsSurfaceDomain(*Surface->Definition, true), AlongV = MeasureNurbsSurfaceDomain(*Surface->Definition, false);
            for (int I = 0; I <= 12; ++I)
            {
                for (int J = 0; J <= 12; ++J)
                {
                    const double U = AlongU.Low + (AlongU.High - AlongU.Low) * I / 12.0, V = AlongV.Low + (AlongV.High - AlongV.Low) * J / 12.0;
                    const Vector3 Point  = EvaluateNurbsSurface(*Surface, { U, V }).Position - Origin;
                    const double  Height = Dot(Point, Axis);
                    const double  Radius = Length(Point - Axis * Height);
                    const Vector2 Meridian = EvaluateProfileSegment(Spline, (V - AlongV.Low) / (AlongV.High - AlongV.Low)) - AxisPoint;
                    Worst = std::max(Worst, std::hypot(Radius - Sign * Dot(Meridian, Side), Height - Dot(Meridian, Along)));
                }
            }
        }
        (void)Radial;
        return Worst;
    }

    [[nodiscard]] const ProfileSegment* FirstSpline(const SketchProfile& Profile)
    {
        for (const ProfileLoop& Loop : Profile.Loops) for (const ProfileSegment& Segment : Loop.Segments) if (Segment.Kind == ProfileSegmentKind::Spline) return &Segment;
        return nullptr;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                R1–R3 SPLINE REVOLVE
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateSplineRevolve()
    {
        // Vase half-section on the XZ plane: base line, NURBS wall, rim line, the axis edge x = 0 closing the loop.
        const OrthonormalFrame Plane{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        const SketchModel Model = ChainSketch({ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.4, 0.6 }, { 0.6, 1.2 }, { 0.9, 1.8 }, { 0.0, 1.8 } }, 1, 4);
        const SketchProfile Profile = ExtractSketchProfile(Model);
        BodyRun Run;
        const SweepOutcome Sweep = RevolveProfile(Run.Partition, Profile, Plane, { 0.0, 0.0 }, { 0.0, 1.0 });
        if (!Sweep.Refusal.empty()) { Report(SplineRevolveGate, "Vase", "refused: " + Sweep.Refusal, 1.0, false); return; }
        const double Expected = 2.0 * Pi * std::fabs(Profile.Area) * Profile.Centroid.X;
        const double Volume   = IntegrateBodyVolume(Run.Partition, Sweep.Body);
        const double Relative = std::fabs(Volume - Expected) / Expected;
        const double Gap      = RevolutionGap(Run.Partition, Sweep.Body, *FirstSpline(Profile), {}, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0 }, { 0.0, 1.0 });
        bool Sound = false;
        const std::string Audit = AuditText(Run.Partition, Sweep.Body, Sound);
        Report(SplineRevolveGate, "Vase (line · NURBS wall · line · axis edge), 360°",
               "faces " + std::to_string(Sweep.Faces.size()) + " · (r, z) gap " + Scientific(Gap) + " · volume " + Fixed(Volume) + " vs 2π·A·r̄ " + Fixed(Expected) + " (rel. " + Scientific(Relative) + ") · " + Audit,
               std::max(Gap, Relative), Gap <= 1.0e-12 && Relative <= 1.0e-9 && Sweep.Faces.size() == 3 && Sound);
    }

    void ValidatePartialRevolve()
    {
        const OrthonormalFrame Plane{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        const SketchModel Model = ChainSketch({ { 0.0, 0.3 }, { 2.0, 0.3 }, { 2.0, 0.8 }, { 1.3, 1.1 }, { 0.7, 0.6 }, { 0.0, 0.9 } }, 2, 5);
        const SketchProfile Profile = ExtractSketchProfile(Model);
        const double Angle = 2.0 * Pi / 3.0;
        BodyRun Run;
        const SweepOutcome Sweep = RevolveProfile(Run.Partition, Profile, Plane, { 0.0, 0.0 }, { 1.0, 0.0 }, Angle);
        if (!Sweep.Refusal.empty()) { Report(PartialGate, "120° about x", "refused: " + Sweep.Refusal, 1.0, false); return; }
        const double Expected = Angle * std::fabs(Profile.Area) * Profile.Centroid.Y;
        const double Volume   = IntegrateBodyVolume(Run.Partition, Sweep.Body);
        const double Relative = std::fabs(Volume - Expected) / Expected;
        const double Gap      = RevolutionGap(Run.Partition, Sweep.Body, *FirstSpline(Profile), {}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0 }, { 1.0, 0.0 });
        // Far cap: every profile corner turned by θ about x (y, z) ↦ (y cos θ − z sin θ, y sin θ + z cos θ) — the turn direction is either sense.
        std::vector<Vector3> Turned, Opposed;
        for (const Vector2& Corner : std::vector<Vector2>{ { 0.0, 0.3 }, { 2.0, 0.3 }, { 2.0, 0.8 }, { 0.0, 0.9 } })
        {
            Turned.push_back({ Corner.X, Corner.Y * std::cos(Angle), Corner.Y * std::sin(Angle) });
            Opposed.push_back({ Corner.X, Corner.Y * std::cos(Angle), -Corner.Y * std::sin(Angle) });
        }
        const std::vector<Vector3> Vertices = BodyVertices(Run.Partition, Sweep.Body);
        const double CapGap = std::min(NearestGap(Turned, Vertices), NearestGap(Opposed, Vertices));
        bool Sound = false;
        const std::string Audit = AuditText(Run.Partition, Sweep.Body, Sound);
        int Caps = 0;
        for (const SweepFace& Role : Sweep.Roles) Caps += Role.Role != SweepFaceRole::Side ? 1 : 0;
        Report(PartialGate, "Rectangle with a NURBS top, 120° about the sketch x-axis",
               "faces " + std::to_string(Sweep.Faces.size()) + " (caps " + std::to_string(Caps) + ") · (r, z) gap " + Scientific(Gap) + " · far cap gap " + Scientific(CapGap) + " · volume " + Fixed(Volume) + " vs θ·A·ȳ " + Fixed(Expected)
               + " (rel. " + Scientific(Relative) + ") · " + Audit,
               std::max({ Gap, CapGap, Relative }), Gap <= 1.0e-12 && CapGap <= 1.0e-12 && Relative <= 1.0e-9 && Caps == 2 && Sound);
    }

    void ValidateDome()
    {
        const OrthonormalFrame Plane{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        const SketchModel Model = ChainSketch({ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 0.8 }, { 0.5, 1.2 }, { 0.0, 1.2 } }, 1, 4);
        const SketchProfile Profile = ExtractSketchProfile(Model);
        BodyRun Run;
        const SweepOutcome Sweep = RevolveProfile(Run.Partition, Profile, Plane, { 0.0, 0.0 }, { 0.0, 1.0 });
        if (!Sweep.Refusal.empty()) { Report(DomeGate, "Dome", "refused: " + Sweep.Refusal, 1.0, false); return; }
        const double Expected = 2.0 * Pi * std::fabs(Profile.Area) * Profile.Centroid.X;
        const double Volume   = IntegrateBodyVolume(Run.Partition, Sweep.Body);
        const double Relative = std::fabs(Volume - Expected) / Expected;
        bool Sound = false;
        const std::string Audit = AuditText(Run.Partition, Sweep.Body, Sound);
        Report(DomeGate, "Dome: NURBS wall from (1, 0) up to the axis at (0, 1.2)",
               "faces " + std::to_string(Sweep.Faces.size()) + " · vertices " + std::to_string(BodyVertices(Run.Partition, Sweep.Body).size()) + " · volume " + Fixed(Volume) + " vs 2π·A·r̄ " + Fixed(Expected) + " (rel. " + Scientific(Relative) + ") · " + Audit,
               Relative, Relative <= 1.0e-9 && Sound);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                R4–R6 EVOLUTION
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateStraightEvolution()
    {
        const OrthonormalFrame Plane{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        SketchModel Model = RectangleSketch({ 0.0, 0.0 }, { 1.0, 1.0 });
        AppendSketchCircle(Model, AppendSketchPoint(Model, { 0.5, 0.5 }), 0.25, 5);
        const SketchProfile Profile = ExtractSketchProfile(Model);
        const Vector3 Shift = { 0.6, 0.3, 2.0 };
        const NurbsCurve Path = ConvertLineToNurbs(StraightLine{ { 0.5, 0.5, 0.0 }, Normalize(Shift) }, 0.0, Length(Shift));
        BodyRun Run;
        const EvolutionOutcome Evolved = EvolveProfile(Run.Partition, Profile, Plane, Path);
        if (!Evolved.Refusal.empty()) { Report(StraightGate, "Oblique line", "refused: " + Evolved.Refusal, 1.0, false); return; }
        const double Expected = (1.0 - Pi / 16.0) * Shift.Z;
        const double Volume   = IntegrateBodyVolume(Run.Partition, Evolved.Sweep.Body);
        const double Relative = std::fabs(Volume - Expected) / Expected;
        std::vector<Vector3> Corners;
        for (const Vector2& Corner : std::vector<Vector2>{ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 } }) Corners.push_back(Lift(Plane, Corner) + Shift);
        const double CapGap = NearestGap(Corners, BodyVertices(Run.Partition, Evolved.Sweep.Body));
        bool Sound = false;
        const std::string Audit = AuditText(Run.Partition, Evolved.Sweep.Body, Sound);
        Report(StraightGate, "Square − hole r 0.25 along d = (0.6, 0.3, 2)",
               std::to_string(Evolved.Sections) + " stations · deviation " + Scientific(Evolved.Deviation) + " · faces " + std::to_string(Evolved.Sweep.Faces.size()) + " · end corners gap " + Scientific(CapGap) + " · volume " + Fixed(Volume)
               + " vs A·Δz " + Fixed(Expected) + " (rel. " + Scientific(Relative) + ") · " + Audit,
               std::max({ Evolved.Deviation, CapGap, Relative }), Evolved.Deviation <= 1.0e-12 && CapGap <= 1.0e-12 && Relative <= 1.0e-9 && Evolved.Sweep.Faces.size() == 7 && Sound);
    }

    void ValidateArcEvolution()
    {
        // Rectangle in the plane x–z through (2, 0, 0) (⟂ the arc), arc of radius 2 about z from (2, 0, 0) to (0, 2, 0).
        const OrthonormalFrame Plane{ { 2.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        const SketchProfile Profile = ExtractSketchProfile(RectangleSketch({ -0.2, -0.1 }, { 0.2, 0.1 }));
        const NurbsCurve Path = ConvertConicToNurbs({ {}, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.0, 0.5 * Pi);
        BodyRun Run;
        const EvolutionOutcome Evolved = EvolveProfile(Run.Partition, Profile, Plane, Path);
        if (!Evolved.Refusal.empty()) { Report(ArcGate, "Quarter arc", "refused: " + Evolved.Refusal, 1.0, false); return; }
        const double Expected = 0.08 * 2.0 * 0.5 * Pi;
        const double Volume   = IntegrateBodyVolume(Run.Partition, Evolved.Sweep.Body);
        const double Relative = std::fabs(Volume - Expected) / Expected;
        std::vector<Vector3> Turned;
        for (const Vector2& Corner : std::vector<Vector2>{ { -0.2, -0.1 }, { 0.2, -0.1 }, { 0.2, 0.1 }, { -0.2, 0.1 } })
        {
            const Vector3 Point = Lift(Plane, Corner);
            Turned.push_back({ -Point.Y, Point.X, Point.Z });
        }
        const double CapGap = NearestGap(Turned, BodyVertices(Run.Partition, Evolved.Sweep.Body));
        bool Sound = false;
        const std::string Audit = AuditText(Run.Partition, Evolved.Sweep.Body, Sound);
        const double Limit = 1.0e-5 * Profile.Scale;
        Report(ArcGate, "0.4 × 0.2 rectangle along a quarter arc R 2",
               std::to_string(Evolved.Sections) + " stations · deviation " + Scientific(Evolved.Deviation) + " (≤ " + Scientific(Limit) + ") · end corners gap " + Scientific(CapGap) + " · volume " + Fixed(Volume) + " vs A·R̄·θ " + Fixed(Expected)
               + " (rel. " + Scientific(Relative) + ") · " + Audit,
               std::max(Evolved.Deviation / Profile.Scale, Relative), Evolved.Deviation <= Limit && CapGap <= Limit && Relative <= 1.0e-6 && Evolved.Sweep.Faces.size() == 6 && Sound);
    }

    [[nodiscard]] NurbsCurve SpacePath()
    {
        // Cubic B-spline climbing a twisted ramp: non-planar (torsion ≠ 0), gentle bends.
        const std::vector<Vector3> Poles = { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.8 }, { 0.6, 0.3, 1.5 }, { 1.4, 0.2, 1.9 }, { 2.0, 0.9, 2.4 }, { 2.2, 1.8, 2.6 } };
        std::vector<double> Knots = { 0.0, 0.0, 0.0, 0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0, 1.0, 1.0, 1.0 };
        return *ConstructNurbsCurve(3, Knots, Poles, std::vector<double>(Poles.size(), 1.0));
    }

    void ValidateSpaceEvolution()
    {
        // Profile in the plane ⟂ the path start (tangent +z), centred on the path start.
        const OrthonormalFrame Plane{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
        struct Case { const char* Name; SketchModel Model; };
        SketchModel Circle;
        AppendSketchCircle(Circle, AppendSketchPoint(Circle, { 0.0, 0.0 }), 0.1, 1);
        const std::vector<Case> Cases = { { "Rectangle 0.24 × 0.12, centroid on the path", RectangleSketch({ -0.12, -0.06 }, { 0.12, 0.06 }) }, { "Circle r 0.1 (the pipe)", Circle } };
        for (const Case& Each : Cases)
        {
            const SketchProfile Profile = ExtractSketchProfile(Each.Model);
            BodyRun Run;
            const EvolutionOutcome Evolved = EvolveProfile(Run.Partition, Profile, Plane, SpacePath());
            if (!Evolved.Refusal.empty()) { Report(SpaceGate, Each.Name, "refused: " + Evolved.Refusal, 1.0, false); continue; }
            const double Expected = std::fabs(Profile.Area) * Evolved.Length;
            const double Volume   = IntegrateBodyVolume(Run.Partition, Evolved.Sweep.Body);
            const double Relative = std::fabs(Volume - Expected) / Expected;
            bool Sound = false;
            const std::string Audit = AuditText(Run.Partition, Evolved.Sweep.Body, Sound);
            const double Limit = 1.0e-5 * Profile.Scale;
            Report(SpaceGate, Each.Name,
                   std::to_string(Evolved.Sections) + " stations · deviation " + Scientific(Evolved.Deviation) + " (≤ " + Scientific(Limit) + ") · reach·κmax " + Fixed(Evolved.Bend).substr(0, 5) + " · faces " + std::to_string(Evolved.Sweep.Faces.size())
                   + " · volume " + Fixed(Volume) + " vs A·L " + Fixed(Expected) + " (rel. " + Scientific(Relative) + ") · " + Audit,
                   std::max(Evolved.Deviation / Profile.Scale, Relative), Evolved.Deviation <= Limit && Relative <= 1.0e-5 && Sound);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                R7–R8 FEATURE STEPS
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

    [[nodiscard]] FeatureStep EvolutionStep(const std::string& Label, const std::string& Profile, const std::string& Path, FeatureCombination Combination = FeatureCombination::New)
    {
        FeatureStep Step;
        Step.Kind        = FeatureStepKind::Evolution;
        Step.Label       = Label;
        Step.Profile     = Profile;
        Step.Path        = Path;
        Step.Combination = Combination;
        return Step;
    }

    [[nodiscard]] FeatureStep RevolveStep(const std::string& Label, const std::string& Profile, Vector2 Origin, Vector2 Direction, double Angle = 2.0 * Pi)
    {
        FeatureStep Step;
        Step.Kind          = FeatureStepKind::Revolve;
        Step.Label         = Label;
        Step.Profile       = Profile;
        Step.AxisOrigin    = Origin;
        Step.AxisDirection = Direction;
        Step.Angle         = FeatureValue{ Angle };
        return Step;
    }

    struct TreeRun
    {
        ModelPartition      Partition = {};
        RegenerationOutcome Outcome   = {};
        std::string         Refusals  = {};
    };

    void Regenerate(TreeRun& Run, FeatureTree Tree)
    {
        Run.Outcome = RegenerateFeatureTree(Run.Partition, Tree, true);
        for (const FeatureStepReport& Step : Run.Outcome.Steps) if (Step.Status == FeatureStepStatus::Refused) Run.Refusals += (Run.Refusals.empty() ? "" : " | ") + Step.Label + ": " + Step.Refusal;
    }

    [[nodiscard]] const RegeneratedBody* FindBody(const RegenerationOutcome& Outcome, const std::string& Key)
    {
        for (const RegeneratedBody& Body : Outcome.Bodies) if (Body.Key == Key) return &Body;
        return nullptr;
    }

    [[nodiscard]] std::string NameList(const RegeneratedBody& Body)
    {
        std::set<std::string> Names;
        for (const auto& [Slot, Name] : Body.Naming.Faces) Names.insert(Name);
        std::string Text;
        for (const std::string& Name : Names) Text += (Text.empty() ? "" : ", ") + Name;
        return Text;
    }

    // Path sketch on the XZ plane: one NURBS spline from (0, 0) rising to the right.
    [[nodiscard]] SketchModel PathSketch()
    {
        SketchModel Model;
        const std::vector<Vector2> Points = { { 0.0, 0.0 }, { 0.0, 1.0 }, { 0.6, 1.6 }, { 1.6, 1.9 } };
        std::vector<int> Indices;
        for (const Vector2& Point : Points) Indices.push_back(AppendSketchPoint(Model, Point));
        AppendSketchSpline(Model, Indices, SketchSplineForm::Nurbs, 1);
        return Model;
    }

    void ValidateTree()
    {
        const OrthonormalFrame Front{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        {
            // Ring: rectangle x ∈ [1, 1.5], y ∈ [0, 1] about the sketch y-axis given by point + direction: π (1.5² − 1²) · 1.
            TreeRun Run;
            FeatureTree Tree;
            Tree.Steps.push_back(SketchStep("Sketch#1", RectangleSketch({ 1.0, 0.0 }, { 1.5, 1.0 }), Front));
            Tree.Steps.push_back(RevolveStep("Revolve#2", "Sketch#1", { 0.0, 0.0 }, { 0.0, 1.0 }));
            Regenerate(Run, Tree);
            const RegeneratedBody* Body = FindBody(Run.Outcome, "Revolve#2");
            const double Expected = Pi * (1.5 * 1.5 - 1.0) * 1.0;
            const double Volume   = Body ? IntegrateBodyVolume(Run.Partition, Body->Body) : -1.0;
            const double Relative = std::fabs(Volume - Expected) / Expected;
            Report(TreeGate, "Revolve#2 about AxisOrigin (0, 0) + t·(0, 1)", "volume " + Fixed(Volume) + " vs π(R² − r²)h " + Fixed(Expected) + " (rel. " + Scientific(Relative) + ")" + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
                   Relative, Body && Run.Refusals.empty() && Relative <= 1.0e-9);
        }
        {
            // Evolution: a 0.2 × 0.1 rectangle on the XY plane centred on the path start, along the path sketch's spline.
            TreeRun Run;
            FeatureTree Tree;
            Tree.Steps.push_back(SketchStep("Sketch#1", RectangleSketch({ -0.1, -0.05 }, { 0.1, 0.05 })));
            Tree.Steps.push_back(SketchStep("Sketch#2", PathSketch(), Front));
            Tree.Steps.push_back(EvolutionStep("Evolution#3", "Sketch#1", "Sketch#2"));
            Regenerate(Run, Tree);
            const RegeneratedBody* Body = FindBody(Run.Outcome, "Evolution#3");
            double Expected = -1.0;
            {
                ModelPartition Scratch;
                const std::optional<NurbsCurve> Path = ConstructNurbsCurve(3, { 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0 }, { { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.6, 0.0, 1.6 }, { 1.6, 0.0, 1.9 } }, { 1.0, 1.0, 1.0, 1.0 });
                const EvolutionOutcome Direct = EvolveProfile(Scratch, ExtractSketchProfile(RectangleSketch({ -0.1, -0.05 }, { 0.1, 0.05 })), {}, *Path);
                Expected = 0.02 * Direct.Length;
            }
            const double Volume   = Body ? IntegrateBodyVolume(Run.Partition, Body->Body) : -1.0;
            const double Relative = std::fabs(Volume - Expected) / Expected;
            const std::string Names = Body ? NameList(*Body) : std::string();
            const bool Named = Names.find("Side(Line1)") != std::string::npos && Names.find("Cap(Start)") != std::string::npos && Names.find("Cap(End)") != std::string::npos;
            Report(TreeGate, "Evolution#3: Sketch#1 along Sketch#2 (spline)", "faces " + Names + " · volume " + Fixed(Volume) + " vs A·L " + Fixed(Expected) + " (rel. " + Scientific(Relative) + ")" + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
                   Relative, Body && Run.Refusals.empty() && Named && Relative <= 1.0e-6);
        }
        {
            // Far end first: a line path drawn from (0, 2) down to the profile at (0, 0) on the front plane still sweeps
            // the 0.2 × 0.1 rectangle up from where it lies (the path starts at its end nearer the profile): prism 0.04.
            TreeRun Run;
            FeatureTree Tree;
            SketchModel Rail;
            AppendSketchLine(Rail, AppendSketchPoint(Rail, { 0.0, 2.0 }), AppendSketchPoint(Rail, { 0.0, 0.0 }), 1);
            Tree.Steps.push_back(SketchStep("Sketch#1", RectangleSketch({ -0.1, -0.05 }, { 0.1, 0.05 })));
            Tree.Steps.push_back(SketchStep("Sketch#2", Rail, Front));
            Tree.Steps.push_back(EvolutionStep("Evolution#3", "Sketch#1", "Sketch#2"));
            Regenerate(Run, Tree);
            const RegeneratedBody* Body = FindBody(Run.Outcome, "Evolution#3");
            const double Volume   = Body ? IntegrateBodyVolume(Run.Partition, Body->Body) : -1.0;
            const double Relative = std::fabs(Volume - 0.04) / 0.04;
            Report(TreeGate, "Evolution#3 along a line drawn from its far end", "volume " + Fixed(Volume) + " vs A·L 0.04 (rel. " + Scientific(Relative) + ")" + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
                   Relative, Body && Run.Refusals.empty() && Relative <= 1.0e-9);
        }
        {
            // Groove: the same sweep subtracted from a 2 × 1 × 2.2 block it runs through.
            TreeRun Run;
            FeatureTree Tree;
            Tree.Steps.push_back(SketchStep("Sketch#1", RectangleSketch({ -0.4, -0.5 }, { 1.9, 0.5 })));
            FeatureStep Block;
            Block.Kind = FeatureStepKind::Extrude; Block.Label = "Extrude#2"; Block.Profile = "Sketch#1"; Block.Extent = FeatureValue{ 1.6 };
            Tree.Steps.push_back(Block);
            Tree.Steps.push_back(SketchStep("Sketch#3", RectangleSketch({ -0.1, -0.05 }, { 0.1, 0.05 }), OrthonormalFrame{ { 0.0, 0.0, -0.3 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }));
            Tree.Steps.push_back(SketchStep("Sketch#4", PathSketch(), OrthonormalFrame{ { 0.0, 0.0, -0.3 }, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } }));
            Tree.Steps.push_back(EvolutionStep("Evolution#5", "Sketch#3", "Sketch#4", FeatureCombination::Difference));
            Regenerate(Run, Tree);
            const RegeneratedBody* Body = FindBody(Run.Outcome, "Extrude#2");
            const double Volume = Body ? IntegrateBodyVolume(Run.Partition, Body->Body) : -1.0;
            const double Block3 = 2.3 * 1.0 * 1.6;
            std::string Audit = "no body";
            bool Sound = false;
            if (Body) Audit = AuditText(Run.Partition, Body->Body, Sound);
            Report(TreeGate, "Evolution#5 subtracted from a block", "volume " + Fixed(Volume) + " < block " + Fixed(Block3) + " · " + Audit + (Run.Refusals.empty() ? "" : " · " + Run.Refusals),
                   0.0, Body && Run.Refusals.empty() && Volume < Block3 - 1.0e-3 && Volume > 0.5 * Block3 && Sound);
        }
    }

    void ValidateRefusals()
    {
        const OrthonormalFrame Ground{};
        const OrthonormalFrame Front{ {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
        struct Case { const char* Name; std::function<std::string()> Run; const char* Expected; };
        const std::vector<Case> Cases = {
            { "Spline crossing the axis", [&] { ModelPartition Partition; return RevolveProfile(Partition, ExtractSketchProfile(ChainSketch({ { 1.0, 0.0 }, { 1.5, 0.0 }, { 1.5, 1.5 }, { 0.6, 2.2 }, { -2.0, 1.2 }, { 1.0, 0.6 } }, 2, 5)), Front, { 0.0, 0.0 }, { 0.0, 1.0 }).Refusal; }, "crosses the axis" },
            { "Axis without direction", [&] { TreeRun Run; FeatureTree Tree; Tree.Steps.push_back(SketchStep("Sketch#1", RectangleSketch({ 1.0, 0.0 }, { 2.0, 1.0 }))); Tree.Steps.push_back(RevolveStep("Revolve#2", "Sketch#1", {}, {})); Regenerate(Run, Tree); return Run.Refusals; }, "no direction" },
            { "Path along the profile plane", [&] { ModelPartition Partition; return EvolveProfile(Partition, ExtractSketchProfile(RectangleSketch({ 0.0, 0.0 }, { 1.0, 1.0 })), Ground, ConvertLineToNurbs(StraightLine{ {}, { 1.0, 0.0, 0.0 } }, 0.0, 2.0)).Refusal; }, "along the profile plane" },
            { "Profile beyond the bend", [&] { ModelPartition Partition; return EvolveProfile(Partition, ExtractSketchProfile(RectangleSketch({ -1.0, -1.0 }, { 1.0, 1.0 })), Front, ConvertConicToNurbs({ { -0.5, 0.0, 0.0 }, { 0.5, 0.0, 0.0 }, { 0.0, 0.5, 0.0 }, { 0.0, 0.0, 1.0 } }, 0.0, 1.0)).Refusal; }, "tightest bend" },
            { "Closed path sketch", [&] { TreeRun Run; FeatureTree Tree; SketchModel Ring; AppendSketchCircle(Ring, AppendSketchPoint(Ring, { 0.0, 2.0 }), 1.0, 1); Tree.Steps.push_back(SketchStep("Sketch#1", RectangleSketch({ -0.1, -0.1 }, { 0.1, 0.1 }))); Tree.Steps.push_back(SketchStep("Sketch#2", Ring, Front)); Tree.Steps.push_back(EvolutionStep("Evolution#3", "Sketch#1", "Sketch#2")); Regenerate(Run, Tree); return Run.Refusals; }, "closed" },
            { "Path = profile", [&] { TreeRun Run; FeatureTree Tree; Tree.Steps.push_back(SketchStep("Sketch#1", RectangleSketch({ -0.1, -0.1 }, { 0.1, 0.1 }))); Tree.Steps.push_back(EvolutionStep("Evolution#2", "Sketch#1", "Sketch#1")); Regenerate(Run, Tree); return Run.Refusals; }, "another sketch" },
            { "Multi-curve path", [&] { TreeRun Run; FeatureTree Tree; SketchModel Chain; AppendSketchPoint(Chain, { 0.0, 0.0 }); AppendSketchPoint(Chain, { 0.0, 1.0 }); AppendSketchPoint(Chain, { 1.0, 1.0 }); AppendSketchLine(Chain, 0, 1, 1); AppendSketchLine(Chain, 1, 2, 2); Tree.Steps.push_back(SketchStep("Sketch#1", RectangleSketch({ -0.1, -0.1 }, { 0.1, 0.1 }))); Tree.Steps.push_back(SketchStep("Sketch#2", Chain, Front)); Tree.Steps.push_back(EvolutionStep("Evolution#3", "Sketch#1", "Sketch#2")); Regenerate(Run, Tree); return Run.Refusals; }, "has 2 curves" },
        };
        for (const Case& Each : Cases)
        {
            const std::string Text = Each.Run();
            Report(RefuseGate, Each.Name, Text.empty() ? std::string("not refused") : "refused: " + Text, 0.0, Text.find(Each.Expected) != std::string::npos);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    ValidateSplineRevolve();
    ValidatePartialRevolve();
    ValidateDome();
    ValidateStraightEvolution();
    ValidateArcEvolution();
    ValidateSpaceEvolution();
    ValidateTree();
    ValidateRefusals();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && Gate->Checks > 0;
    std::printf("# Sweep validation\n\n%s Revolve / sweep R1–R2: spline revolutions as exact NURBS, revolve about point + direction, Evolution (profile along a path)\n\n", PassedCondition ? "🟢" : "🔴");
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
