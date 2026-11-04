/*====================================================================================================================================
                                                    ASSEMBLYVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Assemblies A1 (priority 11): joint origins, the seven Fusion joint kinds, Joint steps in the feature tree, assembly mass and
//    interference — every value against a closed form

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Assembly/AssemblyAnalysis.h"
#include "Engine/GeometryProcessing/Assembly/JointSolver.h"
#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Audit/ModelAudit.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/BodyTransform.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Containment/FaceDomain.h"
#include "Engine/GeometryProcessing/Feature/FeatureTree.h"
#include "Engine/GeometryProcessing/Geometry/CurveGeometry.h"
#include "Engine/GeometryProcessing/Measurement/MassProperties.h"
#include "Engine/GeometryProcessing/Sketch/SketchCatalogue.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace Frontier::GeometryProcessing;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double Pi     = std::numbers::pi;
    constexpr double Degree = Pi / 180.0;

    struct GateTally
    {
        const char* Code     = "";                                                           // [-] - J1 …
        const char* Subject  = "";                                                           // [-] - What it measures
        const char* Limit    = "";                                                           // [-] - Acceptance rule
        double      Worst    = 0.0;                                                          // [-] - Largest measured value
        long        Checks   = 0;                                                            // [-]
        long        Failures = 0;                                                            // [-]
    };

    GateTally ConnectorGate    = { "J1", "Joint origins read off geometry: circle edges (centre, axis out of the adjacent plane face), a line edge (midpoint, direction), plane faces (area centroid, outward normal), cylinder / cone sides (axis point level with the centroid), sphere and torus centres, a vertex — upright and tilted, against closed forms", "≤ 10⁻¹⁰" };
    GateTally KindGate         = { "J2", "The seven Fusion joint kinds (rigid with offset and angle, revolute and flipped revolute, slider, cylindrical, pin-slot, planar, ball): every vertex of the placed block where the scalar closed form puts it, the re-measured moving origin on its target frame, volume kept, audit sound; a pin's circle edge onto a tilted cylinder axis", "≤ 10⁻¹²" };
    GateTally TreeGate         = { "J3", "Joint steps in the feature tree: a two-link arm on a base boss (revolute θ₁, θ₂; the second link jointed first, so it must follow the first) — link centroids = (2,2) + 1.2(cos θ₁, sin θ₁) and (2,2) + 2.4(cos θ₁, sin θ₁) + 0.7(cos(θ₁+θ₂), sin(θ₁+θ₂)); editing θ₁ / θ₂ resumes at its joint and equals a full replay bit for bit; a rigid Transform of the base carries both links; removing a link drops its joints", "≤ 10⁻¹² · resumed step exact · 0 differing values" };
    GateTally MassGate         = { "J4", "Assembly mass properties with densities (steel block, aluminium cylinder, unit sphere): mass, centre of mass, inertia tensor about it by the parallel-axis theorem — against closed forms, and equal to the unit-density mass of their disjoint union", "rel. ≤ 10⁻⁹" };
    GateTally InterferenceGate = { "J5", "Interference: overlapping blocks (0.225), a pin r 0.3 pressed into a hole r 0.25 (π(r₁² − r₀²)·t), a cylinder through a block (πr²h), a sphere inside a block (contained, 4πr³/3), a pin r 0.2 in the hole (clear, gap 0.05), blocks 0.3 apart (clear, gap 0.3), blocks face to face (contact, no volume)", "rel. ≤ 10⁻⁹ · verdict exact" };
    GateTally RefuseGate       = { "J6", "Refusals name the cause: joint to itself, unknown body, a second joint on a placed body, a joint closing a loop, angle and slide outside limits, an unresolvable joint origin, a NURBS face as joint origin, interference with itself, assembly mass without bodies or with a zero density — refused steps keep the previous bodies", "named" };

    std::vector<GateTally*> AllGates() { return { &ConnectorGate, &KindGate, &TreeGate, &MassGate, &InterferenceGate, &RefuseGate }; }

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
        std::fprintf(stderr, "%s %s %s · %s\n", PassedCondition ? "🟢" : "🔴", Gate.Code, Name.c_str(), Detail.c_str());
    }

    [[nodiscard]] std::string Scientific(double Value)
    {
        char Text[32];
        std::snprintf(Text, sizeof Text, "%.2e", Value);
        return Text;
    }

    [[nodiscard]] std::string Fixed(double Value)
    {
        char Text[40];
        std::snprintf(Text, sizeof Text, "%.12g", Value);
        return Text;
    }

    [[nodiscard]] double Deviation(double Measured, double Expected, double Scale) { return std::fabs(Measured - Expected) / std::max(std::fabs(Expected), Scale); }

    [[nodiscard]] OrthonormalFrame Upright(Vector3 Origin) { return ConstructFrame(Origin, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }); }

    // Largest deviation between two frames (origin and the three axes).
    [[nodiscard]] double FrameDeviation(const OrthonormalFrame& First, const OrthonormalFrame& Second)
    {
        return std::max({ Distance(First.Origin, Second.Origin), Distance(First.XAxis, Second.XAxis), Distance(First.YAxis, Second.YAxis), Distance(First.ZAxis, Second.ZAxis) });
    }

    // Origin and 𝑧 only (the reference 𝑥 of a circle or a plane depends on its parameterisation).
    [[nodiscard]] double AxisDeviation(const OrthonormalFrame& Frame, Vector3 Origin, Vector3 Axis)
    {
        return std::max(Distance(Frame.Origin, Origin), Distance(Frame.ZAxis, Normalize(Axis)));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                ENTITY LOOKUP
    //--------------------------------------------------------------------------------------------------------------------

    [[nodiscard]] std::optional<FaceToken> FaceAt(const ModelPartition& Partition, BodyToken Body, Vector3 Probe)
    {
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
        {
            const FacePositionClass Class = ClassifyFacePosition(DescribeFaceDomain(Partition, Face), Probe, 1.0e-9);
            if (Class.Membership != PointMembership::Outside && Class.SurfaceDistance < 1.0e-9) return Face;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<EdgeToken> EdgeAt(const ModelPartition& Partition, BodyToken Body, Vector3 Probe)
    {
        for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
            const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
            const double         τ       = ProjectOntoCurve(Curve, Probe, Profile.IntervalLow);
            if (τ >= Profile.IntervalLow - 1.0e-12 && τ <= Profile.IntervalHigh + 1.0e-12 && Distance(EvaluateCurve(Curve, τ).Position, Probe) < 1.0e-9) return Edge;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<Vector3> VertexPositions(const ModelPartition& Partition, BodyToken Body)
    {
        std::vector<Vector3> Positions;
        for (VertexToken Vertex : Partition.CollectInventory(Body).Vertices) Positions.push_back(Partition.Vertices.Resolve(Vertex).Position);
        return Positions;
    }

    // Largest distance from each expected point to the nearest point of Actual (and back: the sets must match).
    [[nodiscard]] double SetDeviation(const std::vector<Vector3>& Expected, const std::vector<Vector3>& Actual)
    {
        if (Expected.size() != Actual.size()) return std::numeric_limits<double>::infinity();
        double Worst = 0.0;
        for (const std::vector<Vector3>* From : { &Expected, &Actual })
        {
            const std::vector<Vector3>& To = From == &Expected ? Actual : Expected;
            for (const Vector3& Point : *From)
            {
                double Nearest = std::numeric_limits<double>::infinity();
                for (const Vector3& Other : To) Nearest = std::min(Nearest, Distance(Point, Other));
                Worst = std::max(Worst, Nearest);
            }
        }
        return Worst;
    }

    [[nodiscard]] bool SoundCondition(const ModelPartition& Partition, BodyToken Body) { return AuditBody(Partition, Body).SoundCondition(); }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                J1 CONNECTORS
    //--------------------------------------------------------------------------------------------------------------------

    void CheckConnector(const std::string& Name, const ModelPartition& Partition, ReferenceKind Kind, std::optional<uint32_t> Slot, Vector3 Origin, Vector3 Axis, const char* Description)
    {
        if (!Slot) { Report(ConnectorGate, Name, "entity not found", 1.0, false); return; }
        const JointConnector Connector = ResolveJointConnector(Partition, Kind, *Slot);
        if (!Connector.Refusal.empty()) { Report(ConnectorGate, Name, Connector.Refusal, 1.0, false); return; }
        const double Error = AxisDeviation(Connector.Frame, Origin, Axis);
        const double Frame = std::max({ std::fabs(Dot(Connector.Frame.XAxis, Connector.Frame.ZAxis)), std::fabs(Length(Connector.Frame.XAxis) - 1.0), Distance(Cross(Connector.Frame.XAxis, Connector.Frame.YAxis), Connector.Frame.ZAxis) });
        const bool   Named = Connector.Description == Description;
        Report(ConnectorGate, Name, Connector.Description + " · origin / axis " + Scientific(Error) + " · frame orthonormality " + Scientific(Frame), std::max(Error, Frame), Error <= 1.0e-10 && Frame <= 1.0e-12 && Named);
    }

    void ValidateConnectors()
    {
        ModelPartition Partition;
        // ---- upright cylinder r 0.5 h 2 at (1, 2, 3)
        const BodyToken Cylinder = ConstructCylinder(Partition, Upright({ 1.0, 2.0, 3.0 }), 0.5, 2.0).Value;
        const auto Slot = [](auto Token) -> std::optional<uint32_t> { if (!Token) return std::nullopt; return Token->Slot; };
        CheckConnector("Cylinder top circle edge", Partition, ReferenceKind::Edge, Slot(EdgeAt(Partition, Cylinder, { 1.5, 2.0, 5.0 })), { 1.0, 2.0, 5.0 }, { 0.0, 0.0, 1.0 }, "circle centre");
        CheckConnector("Cylinder bottom circle edge (axis out of the bottom face)", Partition, ReferenceKind::Edge, Slot(EdgeAt(Partition, Cylinder, { 1.5, 2.0, 3.0 })), { 1.0, 2.0, 3.0 }, { 0.0, 0.0, -1.0 }, "circle centre");
        CheckConnector("Cylinder side face", Partition, ReferenceKind::Face, Slot(FaceAt(Partition, Cylinder, { 1.5, 2.0, 3.7 })), { 1.0, 2.0, 4.0 }, { 0.0, 0.0, 1.0 }, "cylinder axis");
        CheckConnector("Cylinder top face", Partition, ReferenceKind::Face, Slot(FaceAt(Partition, Cylinder, { 1.1, 2.1, 5.0 })), { 1.0, 2.0, 5.0 }, { 0.0, 0.0, 1.0 }, "plane centroid");

        // ---- block 2 × 1 × 0.5
        const BodyToken Block = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 1.0, 0.5 }).Value;
        CheckConnector("Block top face", Partition, ReferenceKind::Face, Slot(FaceAt(Partition, Block, { 0.3, 0.3, 0.5 })), { 1.0, 0.5, 0.5 }, { 0.0, 0.0, 1.0 }, "plane centroid");
        CheckConnector("Block front face (y = 0)", Partition, ReferenceKind::Face, Slot(FaceAt(Partition, Block, { 0.3, 0.0, 0.2 })), { 1.0, 0.0, 0.25 }, { 0.0, -1.0, 0.0 }, "plane centroid");
        {
            const std::optional<EdgeToken> Edge = EdgeAt(Partition, Block, { 0.7, 0.0, 0.0 });
            std::optional<uint32_t> Found = Slot(Edge);
            Vector3 Axis = { 1.0, 0.0, 0.0 };
            if (Edge)
            {
                const CurveGeometry& Curve = Partition.Curves.Resolve(Partition.Edges.Resolve(*Edge).Curve);
                if (const StraightLine* Line = std::get_if<StraightLine>(&Curve)) Axis = Line->Direction;                // ±x̂, the stored direction
            }
            CheckConnector("Block bottom front line edge", Partition, ReferenceKind::Edge, Found, { 1.0, 0.0, 0.0 }, Axis, "line midpoint");
            const bool AlongX = std::fabs(std::fabs(Axis.X) - 1.0) < 1.0e-15;
            Report(ConnectorGate, "Block line edge direction is ±x̂", AlongX ? "yes" : "no", AlongX ? 0.0 : 1.0, AlongX);
        }
        {
            const std::optional<VertexToken> Corner = [&]() -> std::optional<VertexToken>
            {
                for (VertexToken Vertex : Partition.CollectInventory(Block).Vertices) if (Distance(Partition.Vertices.Resolve(Vertex).Position, { 2.0, 1.0, 0.5 }) < 1.0e-12) return Vertex;
                return std::nullopt;
            }();
            CheckConnector("Block corner vertex (world axes)", Partition, ReferenceKind::Vertex, Slot(Corner), { 2.0, 1.0, 0.5 }, { 0.0, 0.0, 1.0 }, "vertex");
        }

        // ---- tilted cylinder r 0.3 h 1, axis (0, 1, 1)
        {
            const Vector3 Axis = Normalize({ 0.0, 1.0, 1.0 });
            const Vector3 Base = { -2.0, 0.5, 0.25 };
            const BodyToken Tilted = ConstructCylinder(Partition, ConstructFrame(Base, Axis, { 1.0, 0.0, 0.0 }), 0.3, 1.0).Value;
            const Vector3 Top = Base + Axis;
            CheckConnector("Tilted cylinder top circle edge", Partition, ReferenceKind::Edge, Slot(EdgeAt(Partition, Tilted, Top + Vector3{ 0.3, 0.0, 0.0 })), Top, Axis, "circle centre");
            CheckConnector("Tilted cylinder side face", Partition, ReferenceKind::Face, Slot(FaceAt(Partition, Tilted, Base + Axis * 0.3 + Vector3{ 0.3, 0.0, 0.0 })), Base + Axis * 0.5, Axis, "cylinder axis");
        }

        // ---- cone, sphere, torus
        {
            const BodyToken Cone = ConstructCone(Partition, Upright({ 5.0, 0.0, 0.0 }), 1.0, 2.0).Value;
            // Lateral surface of a cone r 1 h 2: area centroid at h/3 above the base.
            CheckConnector("Cone side face", Partition, ReferenceKind::Face, Slot(FaceAt(Partition, Cone, { 5.5, 0.0, 1.0 })), { 5.0, 0.0, 2.0 / 3.0 }, { 0.0, 0.0, 1.0 }, "cone axis");
            const BodyToken Sphere = ConstructSphere(Partition, Upright({ -1.0, -3.0, 0.5 }), 0.7).Value;
            CheckConnector("Sphere face", Partition, ReferenceKind::Face, Slot(FaceAt(Partition, Sphere, { -0.3, -3.0, 0.5 })), { -1.0, -3.0, 0.5 }, { 0.0, 0.0, 1.0 }, "sphere centre");
            const BodyToken Torus = ConstructTorus(Partition, Upright({ 0.0, 6.0, 0.0 }), 2.0, 0.5).Value;
            CheckConnector("Torus face", Partition, ReferenceKind::Face, Slot(FaceAt(Partition, Torus, { 2.5, 6.0, 0.0 })), { 0.0, 6.0, 0.0 }, { 0.0, 0.0, 1.0 }, "torus centre");
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                J2 JOINT KINDS
    //--------------------------------------------------------------------------------------------------------------------

    // Scalar closed form of a joint: the moving origin's local coordinates, flipped (y, z negated) unless Flipped, rotated
    // by the kind's rotation written out in cosines and sines, shifted by its translation, then read in the fixed frame.
    [[nodiscard]] Vector3 ClosedFormImage(Vector3 Point, const OrthonormalFrame& Moving, const OrthonormalFrame& Fixed, JointKind Kind, const JointMotion& Values, bool Flipped)
    {
        const Vector3 Relative = Point - Moving.Origin;
        double x = Dot(Relative, Moving.XAxis), y = Dot(Relative, Moving.YAxis), z = Dot(Relative, Moving.ZAxis);
        if (!Flipped) { y = -y; z = -z; }
        if (Kind == JointKind::Ball)
        {
            // Rz(yaw) · Ry(pitch) · Rx(roll), applied right to left.
            const double cr = std::cos(Values.Roll), sr = std::sin(Values.Roll);
            const double y1 = cr * y - sr * z, z1 = sr * y + cr * z;
            y = y1; z = z1;
            const double cp = std::cos(Values.Pitch), sp = std::sin(Values.Pitch);
            const double x2 = cp * x + sp * z, z2 = -sp * x + cp * z;
            x = x2; z = z2;
        }
        const double ca = std::cos(Values.Angle), sa = std::sin(Values.Angle);
        const double x3 = ca * x - sa * y, y3 = sa * x + ca * y;
        x = x3; y = y3;
        double tx = 0.0, ty = 0.0, tz = Values.Offset;
        if (Kind == JointKind::Slider || Kind == JointKind::Cylindrical) tz += Values.Slide;
        if (Kind == JointKind::PinSlot) tx = Values.Slide;
        if (Kind == JointKind::Planar) { tx = Values.Slide; ty = Values.SecondSlide; }
        return Fixed.Origin + Fixed.XAxis * (x + tx) + Fixed.YAxis * (y + ty) + Fixed.ZAxis * (z + tz);
    }

    struct KindSpecimen
    {
        const char* Name    = "";
        JointKind   Kind    = JointKind::Rigid;
        JointMotion Values  = {};
        bool        Flipped = false;
    };

    void ValidateKinds()
    {
        ModelPartition Partition;
        const BodyToken Base = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 4.0, 4.0, 1.0 }).Value;
        const BodyToken Part = ConstructBlock(Partition, { 10.0, 10.0, 10.0 }, { 1.0, 0.5, 0.25 }).Value;
        const std::optional<FaceToken> Top    = FaceAt(Partition, Base, { 1.0, 1.0, 1.0 });
        const std::optional<FaceToken> Bottom = FaceAt(Partition, Part, { 10.2, 10.1, 10.0 });
        if (!Top || !Bottom) { Report(KindGate, "Specimen faces", "not found", 1.0, false); return; }
        const JointConnector Fixed  = ResolveJointConnector(Partition, ReferenceKind::Face, Top->Slot);
        const JointConnector Moving = ResolveJointConnector(Partition, ReferenceKind::Face, Bottom->Slot);
        const double PartVolume = IntegrateBodyVolume(Partition, Part);
        const std::vector<Vector3> Corners = VertexPositions(Partition, Part);

        JointMotion Rigid;       Rigid.Offset = 0.1; Rigid.Angle = 30.0 * Degree;
        JointMotion Revolute;    Revolute.Angle = 72.0 * Degree;
        JointMotion Slider;      Slider.Slide = 0.35;
        JointMotion Cylindrical; Cylindrical.Angle = 40.0 * Degree; Cylindrical.Slide = -0.2;
        JointMotion PinSlot;     PinSlot.Angle = 15.0 * Degree; PinSlot.Slide = 0.6;
        JointMotion Planar;      Planar.Slide = 0.4; Planar.SecondSlide = -0.3; Planar.Angle = 110.0 * Degree;
        JointMotion Ball;        Ball.Angle = 20.0 * Degree; Ball.Pitch = 35.0 * Degree; Ball.Roll = -50.0 * Degree;
        const std::vector<KindSpecimen> Specimens = {
            { "Rigid · offset 0.1, angle 30°", JointKind::Rigid, Rigid, false },
            { "Revolute · 72°", JointKind::Revolute, Revolute, false },
            { "Revolute · 72° flipped (origins aligned)", JointKind::Revolute, Revolute, true },
            { "Slider · 0.35 along z", JointKind::Slider, Slider, false },
            { "Cylindrical · 40°, −0.2 along z", JointKind::Cylindrical, Cylindrical, false },
            { "Pin-slot · 15°, 0.6 along x", JointKind::PinSlot, PinSlot, false },
            { "Planar · (0.4, −0.3), 110°", JointKind::Planar, Planar, false },
            { "Ball · yaw 20°, pitch 35°, roll −50°", JointKind::Ball, Ball, false },
        };
        for (const KindSpecimen& Specimen : Specimens)
        {
            const JointPlacement Placement = SolveJointPlacement(Moving.Frame, Fixed.Frame, Specimen.Kind, Specimen.Values, Specimen.Flipped);
            if (!Placement.Refusal.empty()) { Report(KindGate, Specimen.Name, Placement.Refusal, 1.0, false); continue; }
            const TransformOutcome Moved = TransformBody(Partition, Part, Placement.Map);
            if (!Moved.Refusal.empty()) { Report(KindGate, Specimen.Name, Moved.Refusal, 1.0, false); continue; }
            std::vector<Vector3> Expected;
            for (const Vector3& Corner : Corners) Expected.push_back(ClosedFormImage(Corner, Moving.Frame, Fixed.Frame, Specimen.Kind, Specimen.Values, Specimen.Flipped));
            const double Vertices = SetDeviation(Expected, VertexPositions(Partition, Moved.Body));
            // The image of the bottom face, re-measured: its joint origin must be the target frame.
            double Landing = 1.0;
            for (size_t Index = 0; Index < Moved.SourceFaces.size(); ++Index)
            {
                if (!(Moved.SourceFaces[Index] == *Bottom)) continue;
                const JointConnector Again = ResolveJointConnector(Partition, ReferenceKind::Face, Moved.Faces[Index].Slot);
                Landing = Again.Refusal.empty() ? FrameDeviation(Again.Frame, Placement.Target) : 1.0;
            }
            const double Volume = Deviation(IntegrateBodyVolume(Partition, Moved.Body), PartVolume, 1.0);
            const bool   Sound  = SoundCondition(Partition, Moved.Body);
            const double Worst  = std::max({ Vertices, Landing, Volume });
            Report(KindGate, Specimen.Name, "8 vertices " + Scientific(Vertices) + " · landing frame " + Scientific(Landing) + " · volume " + Scientific(Volume) + (Sound ? " · audit sound" : " · audit NOT sound"),
                   Worst, Worst <= 1.0e-12 && Sound);
        }

        // ---- a pin's bottom circle edge onto the axis of a tilted cylinder side: the pin's centroid lands half its height
        //      along the target axis (the bottom origin points out of the pin, so the pin stands on the target's 𝑧)
        {
            const Vector3 Axis = Normalize({ 0.0, 1.0, 1.0 });
            const BodyToken Tilted = ConstructCylinder(Partition, ConstructFrame({ -3.0, 0.0, 0.0 }, Axis, { 1.0, 0.0, 0.0 }), 0.4, 2.0).Value;
            const BodyToken Pin    = ConstructCylinder(Partition, Upright({ 8.0, 0.0, 0.0 }), 0.2, 1.0).Value;
            const std::optional<EdgeToken> PinEdge = EdgeAt(Partition, Pin, { 8.2, 0.0, 0.0 });
            const std::optional<FaceToken> Side    = FaceAt(Partition, Tilted, Vector3{ -3.0, 0.0, 0.0 } + Axis * 0.7 + Vector3{ 0.4, 0.0, 0.0 });
            if (!PinEdge || !Side) { Report(KindGate, "Pin on a tilted axis", "entities not found", 1.0, false); return; }
            const JointConnector From = ResolveJointConnector(Partition, ReferenceKind::Edge, PinEdge->Slot);
            const JointConnector Onto = ResolveJointConnector(Partition, ReferenceKind::Face, Side->Slot);
            JointMotion Turn; Turn.Angle = 25.0 * Degree;
            const JointPlacement Placement = SolveJointPlacement(From.Frame, Onto.Frame, JointKind::Revolute, Turn);
            const TransformOutcome Moved = TransformBody(Partition, Pin, Placement.Map);
            const Vector3 Centre = Vector3{ -3.0, 0.0, 0.0 } + Axis * 1.0;                       // Axis point level with the side's centroid
            const Vector3 Expected = Centre + Axis * 0.5;
            const BodyMassProperties Mass = MeasureBodyMass(Partition, Moved.Body);
            const double Error = std::max(Distance(Mass.Centroid, Expected), Distance(Placement.Target.Origin, Centre));
            Report(KindGate, "Revolute 25° · pin circle edge onto a tilted cylinder axis", "centroid " + Scientific(Distance(Mass.Centroid, Expected)) + " (closed form: axis point + 0.5·axis) · origin " + Scientific(Distance(Placement.Target.Origin, Centre)), Error, Error <= 1.0e-12);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                J3 JOINT STEPS IN THE TREE
    //--------------------------------------------------------------------------------------------------------------------

    FeatureStep& Append(FeatureTree& Tree, FeatureStepKind StepKind, const std::string& Label)
    {
        FeatureStep Step;
        Step.Kind  = StepKind;
        Step.Label = Label;
        Tree.Steps.push_back(std::move(Step));
        return Tree.Steps.back();
    }

    FeatureStep& Extrude(FeatureTree& Tree, const std::string& Label, const std::string& Profile, double Extent, FeatureCombination Combination, const std::string& Target = {})
    {
        FeatureStep& Swept = Append(Tree, FeatureStepKind::Extrude, Label);
        Swept.Profile     = Profile;
        Swept.Extent      = LiteralValue(Extent);
        Swept.Combination = Combination;
        Swept.Target      = Target;
        return Swept;
    }

    // A link: a Length × 0.6 × 0.2 bar with two holes r 0.25 at 0.3 from each end, its corner at (0, Y).
    void AppendLink(FeatureTree& Tree, const std::string& SketchLabel, const std::string& ExtrudeLabel, double Length, double Y)
    {
        FeatureStep& Sketch = Append(Tree, FeatureStepKind::Sketch, SketchLabel);
        AppendSketchRectangle(Sketch.Sketch, { 0.0, Y }, { Length, Y + 0.6 }, 1);
        AppendSketchCircle(Sketch.Sketch, AppendSketchPoint(Sketch.Sketch, { 0.3, Y + 0.3 }), 0.25, 5);
        AppendSketchCircle(Sketch.Sketch, AppendSketchPoint(Sketch.Sketch, { Length - 0.3, Y + 0.3 }), 0.25, 6);
        Extrude(Tree, ExtrudeLabel, SketchLabel, 0.2, FeatureCombination::Separate);
    }

    // Base 4 × 4 × 1 with a boss r 0.25 h 0.6 at (2, 2); links 3 and 2 long laid out beside it.
    [[nodiscard]] FeatureTree BuildArmTree()
    {
        FeatureTree Tree;
        Tree.Name = "TwoLinkArm";
        Tree.Parameters.push_back({ "Theta1", 0.0, -Pi, Pi, "first joint angle" });
        Tree.Parameters.push_back({ "Theta2", 0.0, -Pi, Pi, "second joint angle" });
        FeatureStep& Plate = Append(Tree, FeatureStepKind::Sketch, "Sketch#1");
        AppendSketchRectangle(Plate.Sketch, { 0.0, 0.0 }, { 4.0, 4.0 }, 1);
        Extrude(Tree, "Extrude#2", "Sketch#1", 1.0, FeatureCombination::New);
        FeatureStep& Boss = Append(Tree, FeatureStepKind::Sketch, "Sketch#3");
        AppendSketchCircle(Boss.Sketch, AppendSketchPoint(Boss.Sketch, { 2.0, 2.0 }), 0.25, 1);
        Boss.Elevation = LiteralValue(1.0);
        Extrude(Tree, "Extrude#4", "Sketch#3", 0.6, FeatureCombination::Union, "Extrude#2");
        AppendLink(Tree, "Sketch#5", "Extrude#6", 3.0, 10.0);
        AppendLink(Tree, "Sketch#7", "Extrude#8", 2.0, 13.0);
        return Tree;
    }

    struct ArmNames
    {
        std::string BossEdge       = {};                                                     // [-] - Base: boss foot circle (z = 1)
        std::string FirstNearEdge  = {};                                                     // [-] - Link 1: hole 1 bottom circle
        std::string FirstFarEdge   = {};                                                     // [-] - Link 1: hole 2 top circle
        std::string SecondNearEdge = {};                                                     // [-] - Link 2: hole 1 bottom circle
    };

    [[nodiscard]] const RegeneratedBody* FindBody(const RegenerationOutcome& Outcome, const std::string& Key)
    {
        for (const RegeneratedBody& Body : Outcome.Bodies) if (Body.Key == Key) return &Body;
        return nullptr;
    }

    [[nodiscard]] std::string EdgeNameAt(const ModelPartition& Partition, const RegenerationOutcome& Outcome, const std::string& Key, Vector3 Probe)
    {
        const RegeneratedBody* Body = FindBody(Outcome, Key);
        if (!Body) return {};
        const std::optional<EdgeToken> Edge = EdgeAt(Partition, Body->Body, Probe);
        if (!Edge) return {};
        const auto Found = Body->Naming.Edges.find(Edge->Slot);
        return Found == Body->Naming.Edges.end() ? std::string() : Found->second;
    }

    FeatureStep& AppendJoint(FeatureTree& Tree, const std::string& Label, const std::string& Moving, const std::string& Fixed, const std::string& MovingEdge,
                             const std::string& FixedEdge, const std::string& Angle)
    {
        FeatureStep& Joint = Append(Tree, FeatureStepKind::Joint, Label);
        Joint.Target = Moving;
        Joint.Tool   = Fixed;
        Joint.References.push_back({ ReferenceKind::Edge, MovingEdge, {} });
        Joint.References.push_back({ ReferenceKind::Edge, FixedEdge, {} });
        Joint.Joint.Kind  = JointKind::Revolute;
        Joint.Joint.Angle = ParameterValue(Angle);
        return Joint;
    }

    [[nodiscard]] Vector3 BodyCentroid(const ModelPartition& Partition, const RegenerationOutcome& Outcome, const std::string& Key)
    {
        const RegeneratedBody* Body = FindBody(Outcome, Key);
        if (!Body) return { std::nan(""), std::nan(""), std::nan("") };
        return MeasureBodyMass(Partition, Body->Body).Centroid;
    }

    // Every vertex position and volume of every body, for bit-exact comparison of two regenerations.
    [[nodiscard]] std::vector<double> Fingerprint(const ModelPartition& Partition, const RegenerationOutcome& Outcome)
    {
        std::vector<double> Values;
        for (const RegeneratedBody& Body : Outcome.Bodies)
        {
            for (const Vector3& Point : VertexPositions(Partition, Body.Body)) { Values.push_back(Point.X); Values.push_back(Point.Y); Values.push_back(Point.Z); }
            Values.push_back(IntegrateBodyVolume(Partition, Body.Body));
        }
        return Values;
    }

    [[nodiscard]] long DifferingValues(const std::vector<double>& First, const std::vector<double>& Second)
    {
        long Count = static_cast<long>(First.size() > Second.size() ? First.size() - Second.size() : Second.size() - First.size());
        for (size_t Index = 0; Index < std::min(First.size(), Second.size()); ++Index) Count += First[Index] != Second[Index] ? 1 : 0;
        return Count;
    }

    void ValidateTree()
    {
        // ① names of the joint origins, read off a regeneration without joints (probes a quarter turn from the circle seams,
        //    whose vertical seam edges end at angle 0)
        FeatureTree Tree = BuildArmTree();
        ArmNames Names;
        {
            ModelPartition Partition;
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Tree, true);
            if (Outcome.Refused != 0) { Report(TreeGate, "Arm specimen", "refused steps: " + std::to_string(Outcome.Refused), 1.0, false); return; }
            Names.BossEdge       = EdgeNameAt(Partition, Outcome, "Extrude#2", { 2.0, 2.25, 1.0 });
            Names.FirstNearEdge  = EdgeNameAt(Partition, Outcome, "Extrude#6", { 0.3, 10.55, 0.0 });
            Names.FirstFarEdge   = EdgeNameAt(Partition, Outcome, "Extrude#6", { 2.7, 10.55, 0.2 });
            Names.SecondNearEdge = EdgeNameAt(Partition, Outcome, "Extrude#8", { 0.3, 13.55, 0.0 });
            const bool Named = !Names.BossEdge.empty() && !Names.FirstNearEdge.empty() && !Names.FirstFarEdge.empty() && !Names.SecondNearEdge.empty();
            Report(TreeGate, "Joint origins named on the regenerated part", Named ? Names.SecondNearEdge + " → " + Names.FirstFarEdge + " · " + Names.FirstNearEdge + " → " + Names.BossEdge : "missing", Named ? 0.0 : 1.0, Named);
            if (!Named) return;
        }
        // ② the second link is jointed to the first BEFORE the first is jointed to the base: it must follow
        AppendJoint(Tree, "Joint#9", "Extrude#8", "Extrude#6", Names.SecondNearEdge, Names.FirstFarEdge, "Theta2");
        AppendJoint(Tree, "Joint#10", "Extrude#6", "Extrude#2", Names.FirstNearEdge, Names.BossEdge, "Theta1");
        const auto Expect = [](double θ1, double θ2)
        {
            const Vector3 First  = { 2.0 + 1.2 * std::cos(θ1), 2.0 + 1.2 * std::sin(θ1), 1.1 };
            const Vector3 Second = { 2.0 + 2.4 * std::cos(θ1) + 0.7 * std::cos(θ1 + θ2), 2.0 + 2.4 * std::sin(θ1) + 0.7 * std::sin(θ1 + θ2), 1.3 };
            return std::pair{ First, Second };
        };
        ModelPartition     Partition;
        RegenerationCache  Cache;
        const std::vector<std::pair<double, double>> Angles = { { 0.0, 0.0 }, { 30.0, 0.0 }, { 30.0, 45.0 }, { 120.0, -60.0 }, { -90.0, -60.0 }, { -90.0, 180.0 }, { 45.0, 45.0 } };
        for (size_t Index = 0; Index < Angles.size(); ++Index)
        {
            const double θ1 = Angles[Index].first * Degree, θ2 = Angles[Index].second * Degree;
            const bool FirstChanged  = Index == 0 || Angles[Index].first != Angles[Index - 1].first;
            const bool SecondChanged = Index == 0 || Angles[Index].second != Angles[Index - 1].second;
            Tree.Parameters[0].Value = θ1;
            Tree.Parameters[1].Value = θ2;
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Partition, Tree, false, &Cache);
            const auto [First, Second] = Expect(θ1, θ2);
            const Vector3 FirstMeasured = BodyCentroid(Partition, Outcome, "Extrude#6"), SecondMeasured = BodyCentroid(Partition, Outcome, "Extrude#8");
            const double Error = std::max(Distance(FirstMeasured, First), Distance(SecondMeasured, Second));
            char Name[96];
            std::snprintf(Name, sizeof Name, "θ₁ %g°, θ₂ %g°", Angles[Index].first, Angles[Index].second);
            const bool Built = Outcome.Refused == 0 && Outcome.Joints.size() == 2;
            Report(TreeGate, std::string(Name) + " · link centroids", "link 2 centroid (" + Fixed(SecondMeasured.X) + ", " + Fixed(SecondMeasured.Y) + ", " + Fixed(SecondMeasured.Z) + ") · link 1 / 2 vs closed form " + Scientific(Error) + (Built ? " · 2 joints" : " · refused: " + std::to_string(Outcome.Refused)), Error, Built && Error <= 1.0e-12);
            if (Index > 0)
            {
                // Only θ₂ changed → resume at Joint#9 (index 8); θ₁ changed → Joint#10 (index 9) unless θ₂ changed too.
                const int Expected = SecondChanged ? 8 : (FirstChanged ? 9 : 10);
                const bool Resumed = Outcome.Resumed == Expected;
                ModelPartition Fresh;
                FeatureTree    Copy = Tree;
                const RegenerationOutcome Full = RegenerateFeatureTree(Fresh, Copy, false, nullptr);
                const long Differing = DifferingValues(Fingerprint(Partition, Outcome), Fingerprint(Fresh, Full));
                Report(TreeGate, std::string(Name) + " · incremental replay", "resumed at step " + std::to_string(Outcome.Resumed) + " (expected " + std::to_string(Expected) + ") · " + std::to_string(Differing) + " differing values vs a full replay",
                       static_cast<double>(Differing), Resumed && Differing == 0);
            }
        }

        // ③ a rigid Transform of the base carries both links (θ₁ 45°, θ₂ 45° from the last regeneration)
        {
            const RegenerationOutcome Before = RegenerateFeatureTree(Partition, Tree, false, &Cache);
            const Vector3 FirstBefore = BodyCentroid(Partition, Before, "Extrude#6"), SecondBefore = BodyCentroid(Partition, Before, "Extrude#8");
            FeatureStep& Moved = Append(Tree, FeatureStepKind::Transform, "Transform#11");
            Moved.Target    = "Extrude#2";
            Moved.Placement = RotationMap({ 0.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, Pi / 2.0);
            Moved.Placement.Shift = Moved.Placement.Shift + Vector3{ 5.0, 0.0, 0.0 };
            const AffineMap Map = Moved.Placement;
            const RegenerationOutcome After = RegenerateFeatureTree(Partition, Tree, false, &Cache);
            const double Error = std::max(Distance(BodyCentroid(Partition, After, "Extrude#6"), MapPosition(Map, FirstBefore)), Distance(BodyCentroid(Partition, After, "Extrude#8"), MapPosition(Map, SecondBefore)));
            double Origins = 0.0;
            for (const RegeneratedJoint& Joint : After.Joints) Origins = std::max(Origins, Distance(Joint.MovingOrigin.Origin, Joint.FixedOrigin.Origin));
            Report(TreeGate, "Transform of the base (90° about z, then +5 x) · both links follow", "link centroids vs the mapped ones " + Scientific(Error) + " · joint origins still coincide " + Scientific(Origins), std::max(Error, Origins),
                   After.Refused == 0 && Error <= 1.0e-12 && Origins <= 1.0e-12);

            // ④ removing the first link drops both joints; the second link stays where it was
            FeatureStep& Removed = Append(Tree, FeatureStepKind::Remove, "Remove#12");
            Removed.Target = "Extrude#6";
            const RegenerationOutcome Last = RegenerateFeatureTree(Partition, Tree, false, &Cache);
            const double Stay = Distance(BodyCentroid(Partition, Last, "Extrude#8"), BodyCentroid(Partition, After, "Extrude#8"));
            Report(TreeGate, "Remove the first link · its joints are dropped", std::to_string(Last.Joints.size()) + " joints left · second link moved " + Scientific(Stay), Stay, Last.Refused == 0 && Last.Joints.empty() && Stay <= 1.0e-15);
            Tree.Steps.pop_back();
            Tree.Steps.pop_back();
        }

        // ⑤ refusals (J6): each appended to the arm tree, the step refused, the previous bodies kept
        struct TreeRefusal
        {
            const char* Name     = "";
            const char* Expected = "";
            std::function<void(FeatureTree&)> Author;
        };
        const std::vector<TreeRefusal> Refusals = {
            { "Joint a body to itself", "cannot be jointed to itself", [&](FeatureTree& Edited) { AppendJoint(Edited, "Joint#11", "Extrude#6", "Extrude#6", Names.FirstNearEdge, Names.FirstFarEdge, "Theta1"); } },
            { "Joint to an unknown body", "no body named Extrude#99", [&](FeatureTree& Edited) { AppendJoint(Edited, "Joint#11", "Extrude#6", "Extrude#99", Names.FirstNearEdge, Names.BossEdge, "Theta1"); } },
            { "A second joint on a placed body", "is already jointed to Extrude#6", [&](FeatureTree& Edited) { AppendJoint(Edited, "Joint#11", "Extrude#8", "Extrude#2", Names.SecondNearEdge, Names.BossEdge, "Theta1"); } },
            { "A joint that closes a loop (base onto the second link)", "would close a loop", [&](FeatureTree& Edited) { AppendJoint(Edited, "Joint#11", "Extrude#2", "Extrude#8", Names.BossEdge, Names.SecondNearEdge, "Theta1"); } },
            { "Angle 120° outside limits [0°, 90°]", "outside its limits", [&](FeatureTree& Edited)
              {
                  Edited.Parameters[0].Value = 120.0 * Degree;
                  Edited.Steps[9].Joint.AngleRange = JointRange{ 0.0, Pi / 2.0 };
              } },
            { "Slide 0.8 outside limits [0, 0.5]", "slide 0.8 is outside its limits", [&](FeatureTree& Edited)
              {
                  Edited.Steps[9].Joint.Kind       = JointKind::Slider;
                  Edited.Steps[9].Joint.Slide      = LiteralValue(0.8);
                  Edited.Steps[9].Joint.SlideRange = JointRange{ 0.0, 0.5 };
              } },
            { "An unresolvable joint origin", "Joint:", [&](FeatureTree& Edited) { Edited.Steps[9].References[1].Name = "Edge(Nowhere|Nothing)"; } },
        };
        for (const TreeRefusal& Refusal : Refusals)
        {
            FeatureTree Edited = Tree;
            Edited.Parameters[0].Value = 30.0 * Degree;
            Edited.Parameters[1].Value = 0.0;
            Refusal.Author(Edited);
            ModelPartition Scratch;
            const RegenerationOutcome Outcome = RegenerateFeatureTree(Scratch, Edited);
            const FeatureStepReport* Refused = nullptr;
            for (const FeatureStepReport& Step : Outcome.Steps) if (Step.Status == FeatureStepStatus::Refused) { Refused = &Step; break; }
            const bool Named = Refused && Refused->Refusal.find(Refusal.Expected) != std::string::npos;
            const bool Kept  = Outcome.Bodies.size() == 3;
            Report(RefuseGate, Refusal.Name, Refused ? "\"" + Refused->Refusal + "\"" + (Kept ? " · 3 bodies kept" : " · bodies lost") : "no refusal", 0.0, Named && Kept);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                J4 ASSEMBLY MASS
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateMass()
    {
        ModelPartition Partition;
        // Steel block 2 × 1 × 0.5 at the origin, aluminium cylinder r 0.3 h 1 at (4, 0, 0), unit sphere r 0.5 at (0, 3, 1).
        const double a = 2.0, b = 1.0, c = 0.5, r = 0.3, h = 1.0, R = 0.5;
        const double ρ1 = 7.85, ρ2 = 2.7, ρ3 = 1.0;
        const BodyToken Block    = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { a, b, c }).Value;
        const BodyToken Cylinder = ConstructCylinder(Partition, Upright({ 4.0, 0.0, 0.0 }), r, h).Value;
        const BodyToken Sphere   = ConstructSphere(Partition, Upright({ 0.0, 3.0, 1.0 }), R).Value;
        const AssemblyMassProperties Totals = MeasureAssemblyMass(Partition, { { Block, ρ1 }, { Cylinder, ρ2 }, { Sphere, ρ3 } });
        if (!Totals.Refusal.empty()) { Report(MassGate, "Three-body assembly", Totals.Refusal, 1.0, false); return; }

        struct Piece { double Mass; Vector3 Centre; std::array<double, 3> Diagonal; };
        const double m1 = ρ1 * a * b * c, m2 = ρ2 * Pi * r * r * h, m3 = ρ3 * 4.0 / 3.0 * Pi * R * R * R;
        const std::array<Piece, 3> Pieces = { {
            { m1, { a / 2.0, b / 2.0, c / 2.0 }, { m1 * (b * b + c * c) / 12.0, m1 * (a * a + c * c) / 12.0, m1 * (a * a + b * b) / 12.0 } },
            { m2, { 4.0, 0.0, h / 2.0 }, { m2 * (3.0 * r * r + h * h) / 12.0, m2 * (3.0 * r * r + h * h) / 12.0, m2 * r * r / 2.0 } },
            { m3, { 0.0, 3.0, 1.0 }, { 0.4 * m3 * R * R, 0.4 * m3 * R * R, 0.4 * m3 * R * R } },
        } };
        double  Mass = 0.0;
        Vector3 Moment;
        for (const Piece& P : Pieces) { Mass += P.Mass; Moment = Moment + P.Centre * P.Mass; }
        const Vector3 Centre = Moment * (1.0 / Mass);
        std::array<double, 6> Inertia = {};
        for (const Piece& P : Pieces)
        {
            const Vector3 d = P.Centre - Centre;
            Inertia[0] += P.Diagonal[0] + P.Mass * (d.Y * d.Y + d.Z * d.Z);
            Inertia[1] += P.Diagonal[1] + P.Mass * (d.Z * d.Z + d.X * d.X);
            Inertia[2] += P.Diagonal[2] + P.Mass * (d.X * d.X + d.Y * d.Y);
            Inertia[3] -= P.Mass * d.X * d.Y;
            Inertia[4] -= P.Mass * d.Y * d.Z;
            Inertia[5] -= P.Mass * d.Z * d.X;
        }
        const double MassError   = Deviation(Totals.Mass, Mass, 1.0);
        const double CentreError = Distance(Totals.CentreOfMass, Centre) / 4.0;
        double InertiaError = 0.0;
        for (int Index = 0; Index < 6; ++Index) InertiaError = std::max(InertiaError, std::fabs(Totals.Inertia[Index] - Inertia[Index]) / std::fabs(Inertia[0]));
        Report(MassGate, "Mass ρ₁abc + ρ₂πr²h + ρ₃·4πR³/3", Fixed(Totals.Mass) + " vs " + Fixed(Mass) + " → " + Scientific(MassError), MassError, MassError <= 1.0e-9);
        Report(MassGate, "Centre of mass Σmᵢcᵢ / M", "(" + Fixed(Totals.CentreOfMass.X) + ", " + Fixed(Totals.CentreOfMass.Y) + ", " + Fixed(Totals.CentreOfMass.Z) + ") → " + Scientific(CentreError), CentreError, CentreError <= 1.0e-9);
        Report(MassGate, "Inertia about the centre of mass (parallel axis, products included)", "Ixx " + Fixed(Totals.Inertia[0]) + " · Ixy " + Fixed(Totals.Inertia[3]) + " → " + Scientific(InertiaError), InertiaError, InertiaError <= 1.0e-9);

        // Unit densities: the assembly equals the mass properties of the disjoint union (one body, three lumps).
        const AssemblyMassProperties Unit = MeasureAssemblyMass(Partition, { { Block, 1.0 }, { Cylinder, 1.0 }, { Sphere, 1.0 } });
        const BooleanOutcome Pair  = ComputeBoolean(Partition, Block, Cylinder, BooleanOperation::Union);
        const BooleanOutcome Union = Pair.Refusal.empty() ? ComputeBoolean(Partition, Pair.Body, Sphere, BooleanOperation::Union) : Pair;
        if (!Union.Refusal.empty()) { Report(MassGate, "Disjoint union", Union.Refusal, 1.0, false); return; }
        const BodyMassProperties Lumps = MeasureBodyMass(Partition, Union.Body);
        double UnionError = std::max(Deviation(Unit.Volume, Lumps.Volume, 1.0), Distance(Unit.CentreOfMass, Lumps.Centroid) / 4.0);
        for (int Index = 0; Index < 6; ++Index) UnionError = std::max(UnionError, std::fabs(Unit.Inertia[Index] - Lumps.Inertia[Index]) / std::fabs(Lumps.Inertia[0]));
        Report(MassGate, "Unit densities = the disjoint union (" + std::to_string(Union.Solids) + " lumps)", "volume, centroid, inertia → " + Scientific(UnionError), UnionError, Union.Solids == 3 && UnionError <= 1.0e-9);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                J5 INTERFERENCE
    //--------------------------------------------------------------------------------------------------------------------

    void CheckInterference(ModelPartition& Partition, const std::string& Name, BodyToken First, BodyToken Second, InterferenceCondition Condition, double Volume, double Clearance)
    {
        const auto Started = std::chrono::steady_clock::now();
        const InterferenceMeasure Measure = DetectBodyInterference(Partition, First, Second);
        const double Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
        if (!Measure.Refusal.empty()) { Report(InterferenceGate, Name, Measure.Refusal, 1.0, false); return; }
        const bool   Verdict = Measure.Condition == Condition;
        const double Error   = std::max(Deviation(Measure.Volume, Volume, 1.0), Deviation(Measure.Clearance, Clearance, 1.0));
        char Timing[32];
        std::snprintf(Timing, sizeof Timing, " · %.0f ms", Milliseconds);
        Report(InterferenceGate, Name, std::string(InterferenceConditionLabel(Measure.Condition)) + " (expected " + InterferenceConditionLabel(Condition) + ") · V " + Fixed(Measure.Volume) + " vs " + Fixed(Volume)
                   + " · gap " + Fixed(Measure.Clearance) + " vs " + Fixed(Clearance) + " → " + Scientific(Error) + Timing, Error, Verdict && Error <= 1.0e-9);
    }

    void ValidateInterference()
    {
        ModelPartition Partition;
        {
            const BodyToken A = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
            const BodyToken B = ConstructBlock(Partition, { 0.5, 0.25, 0.4 }, { 1.0, 1.0, 1.0 }).Value;
            CheckInterference(Partition, "Overlapping unit blocks", A, B, InterferenceCondition::Overlap, 0.5 * 0.75 * 0.6, 0.0);
        }
        // Plate 2 × 2 × 0.5 with a hole r 0.25 at (1, 1); pins pass through it (no coincident faces).
        const BodyToken Slab  = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 2.0, 2.0, 0.5 }).Value;
        const BodyToken Drill = ConstructCylinder(Partition, Upright({ 1.0, 1.0, -0.5 }), 0.25, 1.5).Value;
        const BooleanOutcome Plate = ComputeBoolean(Partition, Slab, Drill, BooleanOperation::Difference);
        if (!Plate.Refusal.empty()) { Report(InterferenceGate, "Plate with a hole", Plate.Refusal, 1.0, false); return; }
        {
            const BodyToken Pressed = ConstructCylinder(Partition, Upright({ 1.0, 1.0, -0.25 }), 0.3, 1.0).Value;
            CheckInterference(Partition, "Pin r 0.3 pressed into the hole r 0.25 (t 0.5)", Pressed, Plate.Body, InterferenceCondition::Overlap, Pi * (0.09 - 0.0625) * 0.5, 0.0);
            const BodyToken Loose = ConstructCylinder(Partition, Upright({ 1.0, 1.0, -0.25 }), 0.2, 1.0).Value;
            CheckInterference(Partition, "Pin r 0.2 in the hole r 0.25", Loose, Plate.Body, InterferenceCondition::Clear, 0.0, 0.05);
        }
        {
            const BodyToken Block = ConstructBlock(Partition, { 4.0, -1.0, 0.0 }, { 2.0, 2.0, 1.0 }).Value;
            const BodyToken Rod   = ConstructCylinder(Partition, Upright({ 5.0, 0.0, -1.0 }), 0.3, 3.0).Value;
            CheckInterference(Partition, "Cylinder r 0.3 through a block (h 1)", Rod, Block, InterferenceCondition::Overlap, Pi * 0.09 * 1.0, 0.0);
        }
        {
            const BodyToken Block  = ConstructBlock(Partition, { 8.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
            const BodyToken Sphere = ConstructSphere(Partition, Upright({ 8.5, 0.5, 0.5 }), 0.2).Value;
            CheckInterference(Partition, "Sphere r 0.2 inside a unit block", Sphere, Block, InterferenceCondition::Contained, 4.0 / 3.0 * Pi * 0.008, 0.0);
        }
        {
            const BodyToken A = ConstructBlock(Partition, { 0.0, 5.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
            const BodyToken B = ConstructBlock(Partition, { 1.3, 5.2, 0.1 }, { 1.0, 0.5, 0.5 }).Value;
            CheckInterference(Partition, "Blocks 0.3 apart", A, B, InterferenceCondition::Clear, 0.0, 0.3);
            const BodyToken C = ConstructBlock(Partition, { 0.2, 5.1, 1.0 }, { 0.5, 0.5, 0.5 }).Value;
            CheckInterference(Partition, "Block resting on a block (face to face)", C, A, InterferenceCondition::Contact, 0.0, 0.0);
        }
        // ---- refusals (J6)
        {
            const InterferenceMeasure Self = DetectBodyInterference(Partition, Slab, Slab);
            Report(RefuseGate, "Interference of a body with itself", Self.Refusal.empty() ? "no refusal" : "\"" + Self.Refusal + "\"", 0.0, Self.Refusal.find("itself") != std::string::npos);
            const AssemblyMassProperties Empty = MeasureAssemblyMass(Partition, {});
            Report(RefuseGate, "Assembly mass without bodies", Empty.Refusal.empty() ? "no refusal" : "\"" + Empty.Refusal + "\"", 0.0, !Empty.Refusal.empty());
            const AssemblyMassProperties Zero = MeasureAssemblyMass(Partition, { { Slab, 0.0 } });
            Report(RefuseGate, "Assembly mass with a zero density", Zero.Refusal.empty() ? "no refusal" : "\"" + Zero.Refusal + "\"", 0.0, Zero.Refusal.find("positive") != std::string::npos);
            const TransformOutcome Promoted = PromoteBodyToNurbs(Partition, Slab);
            const std::optional<FaceToken> Face = Promoted.Refusal.empty() ? FaceAt(Partition, Promoted.Body, { 0.3, 0.3, 0.5 }) : std::nullopt;
            const JointConnector Nurbs = Face ? ResolveJointConnector(Partition, ReferenceKind::Face, Face->Slot) : JointConnector{};
            Report(RefuseGate, "A NURBS face as joint origin", Nurbs.Refusal.empty() ? "no refusal" : "\"" + Nurbs.Refusal + "\"", 0.0, Face && Nurbs.Refusal.find("analytic faces only") != std::string::npos);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    const std::string Only = Count > 2 && std::string(Arguments[1]) == "--only" ? Arguments[2] : "";
    const auto Start = std::chrono::steady_clock::now();
    if (Only.empty() || Only == "connector") ValidateConnectors();
    if (Only.empty() || Only == "kind") ValidateKinds();
    if (Only.empty() || Only == "tree") ValidateTree();
    if (Only.empty() || Only == "mass") ValidateMass();
    if (Only.empty() || Only == "interference") ValidateInterference();
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !Only.empty());
    std::printf("# Assembly validation\n\n%s Priority 11 A1: joint origins, the seven Fusion joint kinds, Joint steps in the feature tree, assembly mass properties and interference (%.2f s)\n\n", PassedCondition ? "🟢" : "🔴", Seconds);
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    const auto Cell = [](std::string Text)                                                   // Persistent names carry '|'
    {
        for (size_t At = Text.find('|'); At != std::string::npos; At = Text.find('|', At + 2)) Text.replace(At, 1, "\\|");
        return Text;
    };
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Cell(Row.Name).c_str(), Cell(Row.Detail).c_str());
    return PassedCondition ? 0 : 1;
}
