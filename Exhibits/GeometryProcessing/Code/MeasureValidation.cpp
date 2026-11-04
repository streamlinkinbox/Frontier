/*====================================================================================================================================
                                                     MEASUREVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Measure / section phase M1–M2: exact mass properties, face areas, edge lengths, minimum distances and plane sections

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometryProcessing/Audit/EnclosedVolume.h"
#include "Engine/GeometryProcessing/Boolean/GeneralFuse.h"
#include "Engine/GeometryProcessing/Construction/BodyTransform.h"
#include "Engine/GeometryProcessing/Construction/PrimitiveConstruction.h"
#include "Engine/GeometryProcessing/Containment/FaceDomain.h"
#include "Engine/GeometryProcessing/Geometry/CurveGeometry.h"
#include "Engine/GeometryProcessing/Geometry/SurfaceGeometry.h"
#include "Engine/GeometryProcessing/Measurement/MassProperties.h"
#include "Engine/GeometryProcessing/Measurement/MinimumDistance.h"
#include "Engine/GeometryProcessing/Measurement/PlaneSection.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
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

    GateTally MassGate     = { "M1", "Body mass properties (volume, surface area, centroid, inertia tensor, principal moments) of block, translated / rotated block, cylinder (upright, tilted), cone, frustum, sphere, torus, hollow and perforated blocks, NURBS-promoted cylinder / torus vs closed forms", "rel. ≤ 10⁻⁹" };
    GateTally AreaGate     = { "M2", "Face areas and centroids (cylinder side and caps, cone side, sphere, torus, holed plate face, NURBS cylinder side) and edge lengths (circles, lines) vs closed forms", "rel. ≤ 10⁻⁹" };
    GateTally DistanceGate = { "M3", "Minimum distance: opposite faces, edge–edge (clamped end and interior skew), sphere–sphere, cylinder ∥ plane, vertex–sphere, vertex–torus, vertex–circle edge, vertex–cone, vertex–NURBS cylinder, adjacent faces = 0", "≤ 10⁻⁹" };
    GateTally SectionGate  = { "M4", "Plane sections: rectangle, plate with a round hole, sphere disc, oblique cylinder ellipse (perimeter by AGM), torus meridian discs and annulus, cone disc, NURBS cylinder — area, perimeter, centroid, Iuu / Ivv / Iuv, loops", "rel. ≤ 10⁻⁹" };
    GateTally RefuseGate   = { "M5", "Refusals name the cause: section plane above the body, plane on a body face", "named" };

    std::vector<GateTally*> AllGates() { return { &MassGate, &AreaGate, &DistanceGate, &SectionGate, &RefuseGate }; }

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
        std::fprintf(stderr, "%s %s %s\n", PassedCondition ? "🟢" : "🔴", Gate.Code, Name.c_str());
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

    // |measured − expected| / scale: relative to the expected value, floored by a characteristic size.
    [[nodiscard]] double Deviation(double Measured, double Expected, double Scale) { return std::fabs(Measured - Expected) / std::max(std::fabs(Expected), Scale); }

    [[nodiscard]] OrthonormalFrame Upright(Vector3 Origin) { return ConstructFrame(Origin, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 }); }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                ENTITY LOOKUP
    //--------------------------------------------------------------------------------------------------------------------

    // The face of Body containing Probe (on its surface, inside its trimmed domain).
    [[nodiscard]] std::optional<FaceToken> FaceAt(const ModelPartition& Partition, BodyToken Body, Vector3 Probe)
    {
        for (FaceToken Face : Partition.CollectInventory(Body).Faces)
        {
            const FacePositionClass Class = ClassifyFacePosition(DescribeFaceDomain(Partition, Face), Probe, 1.0e-9);
            if (Class.Membership != PointMembership::Outside && Class.SurfaceDistance < 1.0e-9) return Face;
        }
        return std::nullopt;
    }

    // The edge of Body passing through Probe within its interval.
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

    [[nodiscard]] std::optional<VertexToken> VertexAt(const ModelPartition& Partition, BodyToken Body, Vector3 Probe)
    {
        for (VertexToken Vertex : Partition.CollectInventory(Body).Vertices) if (Distance(Partition.Vertices.Resolve(Vertex).Position, Probe) < 1.0e-12) return Vertex;
        return std::nullopt;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                M1 MASS PROPERTIES
    //--------------------------------------------------------------------------------------------------------------------

    struct MassExpectation
    {
        double                Volume      = 0.0;                                             // [len³]
        double                SurfaceArea = 0.0;                                             // [len²]
        Vector3               Centroid    = {};                                              // [len]
        std::array<double, 6> Inertia     = {};                                              // [len⁵] - Ixx Iyy Izz Ixy Iyz Izx
    };

    void ValidateMass(const ModelPartition& Partition, BodyToken Body, const std::string& Name, const MassExpectation& Expected, double Size)
    {
        const auto               Start   = std::chrono::steady_clock::now();
        const BodyMassProperties Measure = MeasureBodyMass(Partition, Body);
        const double             Time    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        if (!Measure.Refusal.empty()) { Report(MassGate, Name, Measure.Refusal, 1.0, false); return; }
        const double Scale5 = Expected.Volume * Size * Size;                                 // [len⁵] - Inertia floor
        double Worst = std::max(Deviation(Measure.Volume, Expected.Volume, 0.0), Deviation(Measure.SurfaceArea, Expected.SurfaceArea, 0.0));
        Worst = std::max(Worst, Length(Measure.Centroid - Expected.Centroid) / Size);
        for (int Index = 0; Index < 6; ++Index) Worst = std::max(Worst, std::fabs(Measure.Inertia[Index] - Expected.Inertia[Index]) / Scale5);
        const std::array<double, 3> Principal = ResolvePrincipalMoments(Measure.Inertia), Reference = ResolvePrincipalMoments(Expected.Inertia);
        for (int Index = 0; Index < 3; ++Index) Worst = std::max(Worst, std::fabs(Principal[Index] - Reference[Index]) / Scale5);
        Report(MassGate, Name, "V " + Fixed(Measure.Volume) + " · A " + Fixed(Measure.SurfaceArea) + " · C (" + Fixed(Measure.Centroid.X) + ", " + Fixed(Measure.Centroid.Y) + ", " + Fixed(Measure.Centroid.Z)
               + ") · I₁ I₂ I₃ " + Fixed(Principal[0]) + " " + Fixed(Principal[1]) + " " + Fixed(Principal[2]) + " · worst " + Scientific(Worst) + " · " + Fixed(Time) + " ms", Worst, Worst <= 1.0e-9);
    }

    [[nodiscard]] std::array<double, 6> Diagonal(double Ixx, double Iyy, double Izz) { return { Ixx, Iyy, Izz, 0.0, 0.0, 0.0 }; }

    // R · diag · Rᵀ for a rotation by Angle about the unit Axis (Rodrigues).
    [[nodiscard]] std::array<double, 6> RotateTensor(const std::array<double, 6>& Tensor, Vector3 Axis, double Angle)
    {
        const double c = std::cos(Angle), s = std::sin(Angle), t = 1.0 - c;
        const double R[3][3] = { { c + Axis.X * Axis.X * t, Axis.X * Axis.Y * t - Axis.Z * s, Axis.X * Axis.Z * t + Axis.Y * s },
                                 { Axis.Y * Axis.X * t + Axis.Z * s, c + Axis.Y * Axis.Y * t, Axis.Y * Axis.Z * t - Axis.X * s },
                                 { Axis.Z * Axis.X * t - Axis.Y * s, Axis.Z * Axis.Y * t + Axis.X * s, c + Axis.Z * Axis.Z * t } };
        const double M[3][3] = { { Tensor[0], Tensor[3], Tensor[5] }, { Tensor[3], Tensor[1], Tensor[4] }, { Tensor[5], Tensor[4], Tensor[2] } };
        double Out[3][3] = {};
        for (int Row = 0; Row < 3; ++Row)
            for (int Column = 0; Column < 3; ++Column)
                for (int First = 0; First < 3; ++First)
                    for (int Second = 0; Second < 3; ++Second) Out[Row][Column] += R[Row][First] * M[First][Second] * R[Column][Second];
        return { Out[0][0], Out[1][1], Out[2][2], Out[0][1], Out[1][2], Out[2][0] };
    }

    [[nodiscard]] Vector3 Rotate(Vector3 Point, Vector3 Centre, Vector3 Axis, double Angle)
    {
        const Vector3 Offset = Point - Centre;
        return Centre + Offset * std::cos(Angle) + Cross(Axis, Offset) * std::sin(Angle) + Axis * (Dot(Axis, Offset) * (1.0 - std::cos(Angle)));
    }

    void ValidateMassProperties()
    {
        ModelPartition Partition;
        // ---- block 4 × 2 × 1 at (1, 2, 3)
        const BodyToken Block = ConstructBlock(Partition, { 1.0, 2.0, 3.0 }, { 4.0, 2.0, 1.0 }).Value;
        const MassExpectation BlockExpected = { 8.0, 28.0, { 3.0, 3.0, 3.5 }, Diagonal(8.0 * 5.0 / 12.0, 8.0 * 17.0 / 12.0, 8.0 * 20.0 / 12.0) };
        ValidateMass(Partition, Block, "Block 4 × 2 × 1 at (1, 2, 3)", BlockExpected, 4.0);
        const BodyToken Far = TransformBody(Partition, Block, TranslationMap({ 1000.0, -500.0, 300.0 })).Body;
        MassExpectation FarExpected = BlockExpected;
        FarExpected.Centroid = FarExpected.Centroid + Vector3{ 1000.0, -500.0, 300.0 };
        ValidateMass(Partition, Far, "Same block moved by (1000, −500, 300)", FarExpected, 4.0);
        const Vector3 Axis = Normalize(Vector3{ 1.0, 1.0, 1.0 });
        const double  Angle = Pi / 6.0;
        const BodyToken Turned = TransformBody(Partition, Block, RotationMap({ 0.0, 0.0, 0.0 }, Axis, Angle)).Body;
        MassExpectation TurnedExpected = BlockExpected;
        TurnedExpected.Centroid = Rotate(BlockExpected.Centroid, {}, Axis, Angle);
        TurnedExpected.Inertia  = RotateTensor(BlockExpected.Inertia, Axis, Angle);
        ValidateMass(Partition, Turned, "Same block turned 30° about (1, 1, 1): R · I · Rᵀ, products of inertia ≠ 0", TurnedExpected, 4.0);

        // ---- cylinder r 0.75 h 2, upright and tilted
        const double r = 0.75, h = 2.0, CylinderVolume = Pi * r * r * h;
        const BodyToken Cylinder = ConstructCylinder(Partition, Upright({ 0.5, -0.25, 0.1 }), r, h).Value;
        const MassExpectation CylinderExpected = { CylinderVolume, 2.0 * Pi * r * r + 2.0 * Pi * r * h, { 0.5, -0.25, 1.1 },
                                                   Diagonal(CylinderVolume * (3.0 * r * r + h * h) / 12.0, CylinderVolume * (3.0 * r * r + h * h) / 12.0, CylinderVolume * r * r / 2.0) };
        ValidateMass(Partition, Cylinder, "Cylinder r 0.75 h 2", CylinderExpected, h);
        const Vector3 Tilt = Normalize(Vector3{ 0.0, 1.0, 1.0 });
        const BodyToken Tilted = ConstructCylinder(Partition, ConstructFrame({ 0.0, 0.0, 0.0 }, Tilt, { 1.0, 0.0, 0.0 }), r, h).Value;
        MassExpectation TiltedExpected = CylinderExpected;
        TiltedExpected.Centroid = Tilt * (h / 2.0);
        TiltedExpected.Inertia  = RotateTensor(CylinderExpected.Inertia, { 1.0, 0.0, 0.0 }, -Pi / 4.0);
        ValidateMass(Partition, Tilted, "Cylinder on the axis (0, 1, 1) / √2", TiltedExpected, h);
        if (const TransformOutcome Promoted = PromoteBodyToNurbs(Partition, Cylinder); Promoted.Refusal.empty()) ValidateMass(Partition, Promoted.Body, "NURBS-promoted cylinder (rational faces)", CylinderExpected, h);
        else Report(MassGate, "NURBS-promoted cylinder", Promoted.Refusal, 1.0, false);

        // ---- cone r 1 h 3, frustum 1 → 0.5 h 2
        const double ConeVolume = Pi * 1.0 * 3.0 / 3.0;
        const BodyToken Cone = ConstructCone(Partition, Upright({ -3.0, 0.0, 0.0 }), 1.0, 3.0).Value;
        ValidateMass(Partition, Cone, "Cone r 1 h 3", { ConeVolume, Pi + Pi * std::sqrt(10.0), { -3.0, 0.0, 0.75 },
                     Diagonal(ConeVolume * (3.0 / 20.0 + 27.0 / 80.0), ConeVolume * (3.0 / 20.0 + 27.0 / 80.0), 0.3 * ConeVolume) }, 3.0);
        {
            const double R1 = 1.0, R2 = 0.5, H = 2.0;
            const double Volume = Pi * H / 3.0 * (R1 * R1 + R1 * R2 + R2 * R2);
            const double Height = H * (R1 * R1 + 2.0 * R1 * R2 + 3.0 * R2 * R2) / (4.0 * (R1 * R1 + R1 * R2 + R2 * R2));   // [len] - Centroid above the base
            // 📝 Izz = (3/10) m (R1⁵ − R2⁵)/(R1³ − R2³); Ixx about the centroid from ∫ (r²/4 + z²) dm − m z̄².
            const double Izz = 0.3 * Volume * (std::pow(R1, 5) - std::pow(R2, 5)) / (std::pow(R1, 3) - std::pow(R2, 3));
            double Quarter = 0.0, Second = 0.0;                                              // [len⁵] - ∫ πr⁴/4 dz, ∫ πr² z² dz (Gauss, exact for polynomials)
            const double Nodes[5] = { -0.9061798459386640, -0.5384693101056831, 0.0, 0.5384693101056831, 0.9061798459386640 };
            const double Weights[5] = { 0.2369268850561891, 0.4786286704993665, 0.5688888888888889, 0.4786286704993665, 0.2369268850561891 };
            for (int Index = 0; Index < 5; ++Index)
            {
                const double z = 0.5 * H * (Nodes[Index] + 1.0), Radius = R1 + (R2 - R1) * z / H;
                Quarter += 0.5 * H * Weights[Index] * Pi * std::pow(Radius, 4) / 4.0;
                Second  += 0.5 * H * Weights[Index] * Pi * Radius * Radius * z * z;
            }
            const double Ixx = Quarter + Second - Volume * Height * Height;
            const BodyToken Frustum = ConstructFrustum(Partition, Upright({ 0.0, 4.0, 0.0 }), R1, R2, H).Value;
            ValidateMass(Partition, Frustum, "Frustum r 1 → 0.5 h 2", { Volume, Pi * (R1 * R1 + R2 * R2) + Pi * (R1 + R2) * std::sqrt((R1 - R2) * (R1 - R2) + H * H), { 0.0, 4.0, Height }, Diagonal(Ixx, Ixx, Izz) }, 2.0);
        }

        // ---- sphere R 1.3, torus 2 / 0.5
        const double SphereVolume = 4.0 / 3.0 * Pi * std::pow(1.3, 3);
        const BodyToken Sphere = ConstructSphere(Partition, Upright({ 0.2, 0.3, -0.4 }), 1.3).Value;
        const double SphereI = 0.4 * SphereVolume * 1.69;
        ValidateMass(Partition, Sphere, "Sphere R 1.3", { SphereVolume, 4.0 * Pi * 1.69, { 0.2, 0.3, -0.4 }, Diagonal(SphereI, SphereI, SphereI) }, 2.6);
        const double TorusVolume = 2.0 * Pi * Pi * 2.0 * 0.25;
        const BodyToken Torus = ConstructTorus(Partition, Upright({ 0.0, -6.0, 0.0 }), 2.0, 0.5).Value;
        const MassExpectation TorusExpected = { TorusVolume, 4.0 * Pi * Pi * 2.0 * 0.5, { 0.0, -6.0, 0.0 },
                                                Diagonal(TorusVolume * (2.0 + 5.0 * 0.25 / 8.0), TorusVolume * (2.0 + 5.0 * 0.25 / 8.0), TorusVolume * (4.0 + 0.75 * 0.25)) };
        ValidateMass(Partition, Torus, "Torus R 2 r 0.5", TorusExpected, 5.0);
        if (const TransformOutcome Promoted = PromoteBodyToNurbs(Partition, Torus); Promoted.Refusal.empty()) ValidateMass(Partition, Promoted.Body, "NURBS-promoted torus (rational faces)", TorusExpected, 5.0);
        else Report(MassGate, "NURBS-promoted torus", Promoted.Refusal, 1.0, false);

        // ---- hollow block 4 × 3 × 2 with a centred 2 × 1 × 1 cavity; perforated block with a 2 × 1 through hole
        ModelPartition Holes;
        const BodyToken Hollow = ConstructHollowBlock(Holes, { 4.0, 3.0, 2.0 }, { 2.0, 1.0, 1.0 }).Value;
        ValidateMass(Holes, Hollow, "Hollow block (cavity shell inside the solid region)", { 22.0, 52.0 + 10.0, { 2.0, 1.5, 1.0 },
                     Diagonal((24.0 * 13.0 - 2.0 * 2.0) / 12.0, (24.0 * 20.0 - 2.0 * 5.0) / 12.0, (24.0 * 25.0 - 2.0 * 5.0) / 12.0) }, 4.0);
        const BodyToken Perforated = ConstructPerforatedBlock(Holes, { 4.0, 3.0, 2.0 }, { 2.0, 1.0, 0.0 }).Value;
        ValidateMass(Holes, Perforated, "Perforated block (genus 1)", { 20.0, 60.0, { 2.0, 1.5, 1.0 }, Diagonal((312.0 - 20.0) / 12.0, (480.0 - 32.0) / 12.0, (600.0 - 20.0) / 12.0) }, 4.0);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                M2 FACE AREAS AND EDGE LENGTHS
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateFace(const ModelPartition& Partition, BodyToken Body, Vector3 Probe, const std::string& Name, double Area, std::optional<Vector3> Centroid, double Size)
    {
        const std::optional<FaceToken> Face = FaceAt(Partition, Body, Probe);
        if (!Face) { Report(AreaGate, Name, "face not found", 1.0, false); return; }
        const FaceAreaMeasure Measure = MeasureFaceArea(Partition, *Face);
        double Worst = Deviation(Measure.Area, Area, 0.0);
        if (Centroid) Worst = std::max(Worst, Length(Measure.Centroid - *Centroid) / Size);
        Report(AreaGate, Name, "A " + Fixed(Measure.Area) + " vs " + Fixed(Area) + (Centroid ? " · centroid (" + Fixed(Measure.Centroid.X) + ", " + Fixed(Measure.Centroid.Y) + ", " + Fixed(Measure.Centroid.Z) + ")" : std::string())
               + " · " + Scientific(Worst), Worst, Worst <= 1.0e-9);
    }

    void ValidateEdge(const ModelPartition& Partition, BodyToken Body, Vector3 Probe, const std::string& Name, double Expected)
    {
        const std::optional<EdgeToken> Edge = EdgeAt(Partition, Body, Probe);
        if (!Edge) { Report(AreaGate, Name, "edge not found", 1.0, false); return; }
        const double Measured = MeasureEdgeLength(Partition, *Edge), Worst = Deviation(Measured, Expected, 0.0);
        Report(AreaGate, Name, "L " + Fixed(Measured) + " vs " + Fixed(Expected) + " · " + Scientific(Worst), Worst, Worst <= 1.0e-9);
    }

    void ValidateAreas()
    {
        ModelPartition Partition;
        const double r = 0.75, h = 2.0;
        const BodyToken Cylinder = ConstructCylinder(Partition, Upright({ 0.0, 0.0, 0.0 }), r, h).Value;
        ValidateFace(Partition, Cylinder, { r, 0.0, 0.7 }, "Cylinder side 2πrh", 2.0 * Pi * r * h, Vector3{ 0.0, 0.0, 1.0 }, h);
        ValidateFace(Partition, Cylinder, { 0.1, 0.1, h }, "Cylinder top cap πr²", Pi * r * r, Vector3{ 0.0, 0.0, h }, h);
        ValidateEdge(Partition, Cylinder, { 0.0, r, h }, "Cylinder rim 2πr", 2.0 * Pi * r);
        const BodyToken Cone = ConstructCone(Partition, Upright({ 5.0, 0.0, 0.0 }), 1.0, 3.0).Value;
        ValidateFace(Partition, Cone, { 5.5, 0.0, 1.5 }, "Cone side πr√(r² + h²), centroid h/3", Pi * std::sqrt(10.0), Vector3{ 5.0, 0.0, 1.0 }, 3.0);
        ValidateEdge(Partition, Cone, { 4.0, 0.0, 0.0 }, "Cone base circle 2π", 2.0 * Pi);
        const BodyToken Sphere = ConstructSphere(Partition, Upright({ -5.0, 0.0, 0.0 }), 1.3).Value;
        ValidateFace(Partition, Sphere, { -5.0, 1.3, 0.0 }, "Sphere 4πR² (poles, seam)", 4.0 * Pi * 1.69, Vector3{ -5.0, 0.0, 0.0 }, 2.6);
        const BodyToken Torus = ConstructTorus(Partition, Upright({ 0.0, 8.0, 0.0 }), 2.0, 0.5).Value;
        ValidateFace(Partition, Torus, { 0.0, 10.5, 0.0 }, "Torus 4π²Rr", 4.0 * Pi * Pi, Vector3{ 0.0, 8.0, 0.0 }, 5.0);
        ValidateEdge(Partition, Torus, { 0.0, 10.5, 0.0 }, "Torus outer seam circle 2π(R + r)", 2.0 * Pi * 2.5);
        ValidateEdge(Partition, Torus, { 2.0, 8.0, 0.5 }, "Torus meridian seam circle 2πr", 2.0 * Pi * 0.5);
        const BodyToken Perforated = ConstructPerforatedBlock(Partition, { 4.0, 3.0, 2.0 }, { 2.0, 1.0, 0.0 }).Value;
        ValidateFace(Partition, Perforated, { 0.5, 0.5, 2.0 }, "Holed top face 12 − 2 (inner loop)", 10.0, Vector3{ 2.0, 1.5, 2.0 }, 4.0);
        ValidateEdge(Partition, Perforated, { 2.0, 0.0, 2.0 }, "Block edge 4", 4.0);
        if (const TransformOutcome Promoted = PromoteBodyToNurbs(Partition, Cylinder); Promoted.Refusal.empty())
        {
            ValidateFace(Partition, Promoted.Body, { r, 0.0, 0.7 }, "NURBS cylinder side 2πrh", 2.0 * Pi * r * h, Vector3{ 0.0, 0.0, 1.0 }, h);
            ValidateEdge(Partition, Promoted.Body, { 0.0, r, h }, "NURBS rim 2πr (rational quadratic)", 2.0 * Pi * r);
        }
        else Report(AreaGate, "NURBS cylinder", Promoted.Refusal, 1.0, false);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                M3 MINIMUM DISTANCE
    //--------------------------------------------------------------------------------------------------------------------

    void ValidateDistance(const ModelPartition& Partition, std::optional<MeasureTarget> First, std::optional<MeasureTarget> Second, const std::string& Name, double Expected)
    {
        if (!First || !Second) { Report(DistanceGate, Name, "entity not found", 1.0, false); return; }
        const auto            Start   = std::chrono::steady_clock::now();
        const DistanceMeasure Measure = MeasureMinimumDistance(Partition, *First, *Second);
        const double          Time    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        if (!Measure.Refusal.empty()) { Report(DistanceGate, Name, Measure.Refusal, 1.0, false); return; }
        const double Error = std::fabs(Measure.Distance - Expected);
        Report(DistanceGate, Name, "d " + Fixed(Measure.Distance) + " vs " + Fixed(Expected) + " · error " + Scientific(Error) + " · (" + Fixed(Measure.First.X) + ", " + Fixed(Measure.First.Y) + ", " + Fixed(Measure.First.Z) + ") → ("
               + Fixed(Measure.Second.X) + ", " + Fixed(Measure.Second.Y) + ", " + Fixed(Measure.Second.Z) + ") · " + std::to_string(Measure.Seeds) + " seeds, " + std::to_string(Measure.Rounds) + " rounds · " + Fixed(Time) + " ms", Error, Error <= 1.0e-9);
    }

    template <typename Token>
    [[nodiscard]] std::optional<MeasureTarget> Target(std::optional<Token> Value) { return Value ? std::optional<MeasureTarget>(*Value) : std::nullopt; }

    void ValidateDistances()
    {
        ModelPartition Partition;
        const BodyToken Unit = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }).Value;
        ValidateDistance(Partition, Target(FaceAt(Partition, Unit, { 0.0, 0.5, 0.5 })), Target(FaceAt(Partition, Unit, { 1.0, 0.5, 0.5 })), "Opposite faces of a unit block", 1.0);
        ValidateDistance(Partition, Target(FaceAt(Partition, Unit, { 0.0, 0.5, 0.5 })), Target(FaceAt(Partition, Unit, { 0.5, 0.0, 0.5 })), "Adjacent faces (shared edge)", 0.0);
        const BodyToken Clamped = ConstructBlock(Partition, { 0.5, 1.5, 2.0 }, { 0.1, 1.0, 1.0 }).Value;
        ValidateDistance(Partition, Target(EdgeAt(Partition, Unit, { 0.5, 1.0, 1.0 })), Target(EdgeAt(Partition, Clamped, { 0.5, 2.0, 2.0 })), "Edge–edge, clamped at an end: √(0.5² + 1)", std::sqrt(1.25));
        const BodyToken Skew = ConstructBlock(Partition, { 0.2, 0.5, 2.0 }, { 0.1, 1.0, 1.0 }).Value;
        ValidateDistance(Partition, Target(EdgeAt(Partition, Unit, { 0.5, 1.0, 1.0 })), Target(EdgeAt(Partition, Skew, { 0.2, 1.0, 2.0 })), "Edge–edge, interior skew lines", 1.0);
        const BodyToken Large = ConstructSphere(Partition, Upright({ 4.0, 0.0, 0.0 }), 1.0).Value;
        const BodyToken Small = ConstructSphere(Partition, Upright({ 7.0, 1.0, 0.5 }), 0.5).Value;
        ValidateDistance(Partition, Target(FaceAt(Partition, Large, { 5.0, 0.0, 0.0 })), Target(FaceAt(Partition, Small, { 7.5, 1.0, 0.5 })), "Sphere–sphere: dist(C₁, C₂) − R₁ − R₂", std::sqrt(10.25) - 1.5);
        ValidateDistance(Partition, Target(VertexAt(Partition, Unit, { 1.0, 1.0, 1.0 })), Target(FaceAt(Partition, Large, { 5.0, 0.0, 0.0 })), "Vertex–sphere: √11 − 1", std::sqrt(11.0) - 1.0);
        const BodyToken Roller = ConstructCylinder(Partition, ConstructFrame({ 0.0, 5.0, 2.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }), 0.5, 3.0).Value;
        const BodyToken Floor  = ConstructBlock(Partition, { 0.0, 4.0, 0.0 }, { 3.0, 2.0, 1.0 }).Value;
        ValidateDistance(Partition, Target(FaceAt(Partition, Roller, { 1.0, 5.0, 1.5 })), Target(FaceAt(Partition, Floor, { 1.0, 5.0, 1.0 })), "Cylinder side ∥ plane (a line of minima)", 0.5);
        const BodyToken Ring = ConstructTorus(Partition, Upright({ 0.0, -6.0, 0.0 }), 2.0, 0.5).Value;
        const BodyToken Above = ConstructBlock(Partition, { 0.0, -6.0, 3.0 }, { 0.1, 0.1, 0.1 }).Value;
        ValidateDistance(Partition, Target(VertexAt(Partition, Above, { 0.0, -6.0, 3.0 })), Target(FaceAt(Partition, Ring, { 2.5, -6.0, 0.0 })), "Vertex on the torus axis: √(R² + 9) − r", std::sqrt(13.0) - 0.5);
        const BodyToken Drum  = ConstructCylinder(Partition, Upright({ -5.0, 0.0, 0.0 }), 1.0, 1.0).Value;
        const BodyToken Over  = ConstructBlock(Partition, { -5.0, 0.0, 3.0 }, { 0.1, 0.1, 0.1 }).Value;
        ValidateDistance(Partition, Target(VertexAt(Partition, Over, { -5.0, 0.0, 3.0 })), Target(EdgeAt(Partition, Drum, { -4.0, 0.0, 1.0 })), "Vertex on the axis – rim circle: √(1 + 2²)", std::sqrt(5.0));
        const BodyToken Spire = ConstructCone(Partition, Upright({ 6.0, -6.0, 0.0 }), 1.0, 2.0).Value;
        const BodyToken Beside = ConstructBlock(Partition, { 8.0, -6.0, 1.0 }, { 0.1, 0.1, 0.1 }).Value;
        ValidateDistance(Partition, Target(VertexAt(Partition, Beside, { 8.0, -6.0, 1.0 })), Target(FaceAt(Partition, Spire, { 6.5, -6.0, 1.0 })), "Vertex – cone side: 3 / √5", 3.0 / std::sqrt(5.0));
        const BodyToken Plain = ConstructCylinder(Partition, Upright({ 10.0, 0.0, 0.0 }), 1.0, 1.0).Value;
        const BodyToken Rational = PromoteBodyToNurbs(Partition, Plain).Body;
        const BodyToken Outside = ConstructBlock(Partition, { 12.5, 0.0, 0.5 }, { 0.1, 0.1, 0.1 }).Value;
        ValidateDistance(Partition, Target(VertexAt(Partition, Outside, { 12.5, 0.0, 0.5 })), Target(FaceAt(Partition, Rational, { 11.0, 0.0, 0.5 })), "Vertex – NURBS cylinder side: 2.5 − 1", 1.5);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                M4 PLANE SECTIONS
    //--------------------------------------------------------------------------------------------------------------------

    struct SectionExpectation
    {
        double                Area          = 0.0;                                           // [len²]
        double                Perimeter     = 0.0;                                           // [len]
        Vector3               Centroid      = {};                                            // [len]
        std::array<double, 3> SecondMoments = {};                                            // [len⁴] - Iuu Ivv Iuv
        int                   Regions       = 1;                                             // [-]
        int                   Loops         = 1;                                             // [-]
    };

    void ValidateSection(const ModelPartition& Partition, BodyToken Body, const OrthonormalFrame& Plane, const std::string& Name, const SectionExpectation& Expected, double Size)
    {
        const PlaneSectionMeasure Measure = SectionBody(Partition, Body, Plane);
        if (!Measure.Refusal.empty()) { Report(SectionGate, Name, Measure.Refusal, 1.0, false); return; }
        const double Scale4 = Expected.Area * Size * Size;                                   // [len⁴] - Moment floor
        double Worst = std::max(Deviation(Measure.Area, Expected.Area, 0.0), Deviation(Measure.Perimeter, Expected.Perimeter, 0.0));
        Worst = std::max(Worst, Length(Measure.Centroid - Expected.Centroid) / Size);
        for (int Index = 0; Index < 3; ++Index) Worst = std::max(Worst, std::fabs(Measure.SecondMoments[Index] - Expected.SecondMoments[Index]) / Scale4);
        const bool CountCondition = Measure.Regions == Expected.Regions && Measure.Loops == Expected.Loops;
        Report(SectionGate, Name, "A " + Fixed(Measure.Area) + " · P " + Fixed(Measure.Perimeter) + " · C (" + Fixed(Measure.Centroid.X) + ", " + Fixed(Measure.Centroid.Y) + ", " + Fixed(Measure.Centroid.Z) + ") · Iuu Ivv Iuv "
               + Fixed(Measure.SecondMoments[0]) + " " + Fixed(Measure.SecondMoments[1]) + " " + Fixed(Measure.SecondMoments[2]) + " · " + std::to_string(Measure.Regions) + " region(s), " + std::to_string(Measure.Loops) + " loop(s), "
               + std::to_string(Measure.Outline.size()) + " outline edges · worst " + Scientific(Worst) + " · " + Fixed(Measure.Milliseconds) + " ms", Worst, Worst <= 1.0e-9 && CountCondition);
    }

    // Complete elliptic integral of the second kind E(k) by the arithmetic–geometric mean.
    [[nodiscard]] double EllipticSecond(double Modulus)
    {
        double a = 1.0, b = std::sqrt(1.0 - Modulus * Modulus), Sum = 0.5 * Modulus * Modulus, Power = 0.5;
        for (int Iteration = 0; Iteration < 40 && std::fabs(a - b) > 1.0e-17; ++Iteration)
        {
            const double c = 0.5 * (a - b);
            const double Next = 0.5 * (a + b);
            b = std::sqrt(a * b); a = Next; Power *= 2.0;
            Sum += Power * c * c;
        }
        return Pi / (2.0 * a) * (1.0 - Sum);
    }

    void ValidateSections()
    {
        ModelPartition Partition;
        // ---- rectangle: block 4 × 2 × 1 cut at x = 1 (u = y, v = z)
        const BodyToken Block = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 4.0, 2.0, 1.0 }).Value;
        ValidateSection(Partition, Block, ConstructFrame({ 1.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }), "Block 4 × 2 × 1 at x = 1: rectangle 2 × 1", { 2.0, 6.0, { 1.0, 1.0, 0.5 }, { 2.0 / 12.0, 8.0 / 12.0, 0.0 } }, 2.0);

        // ---- plate with a round hole: block − cylinder r 0.4 at (3, 1), cut at z = 0.5
        {
            const double r = 0.4, Area = 8.0 - Pi * r * r, Centre = (16.0 - Pi * r * r * 3.0) / Area;
            const BodyToken Plate = ConstructBlock(Partition, { 0.0, 0.0, 0.0 }, { 4.0, 2.0, 1.0 }).Value;
            const BodyToken Drill = ConstructCylinder(Partition, Upright({ 3.0, 1.0, -0.5 }), r, 2.0).Value;
            const BooleanOutcome Holed = ComputeBoolean(Partition, Plate, Drill, BooleanOperation::Difference);
            if (!Holed.Refusal.empty()) Report(SectionGate, "Plate with a hole", Holed.Refusal, 1.0, false);
            else
            {
                const double Iuu = 8.0 / 3.0 - Pi * std::pow(r, 4) / 4.0;
                const double Ivv = 128.0 / 3.0 - Pi * r * r * (9.0 + r * r / 4.0) - Area * Centre * Centre;
                ValidateSection(Partition, Holed.Body, Upright({ 0.0, 0.0, 0.5 }), "Plate − round hole r 0.4 at z = 0.5: 8 − πr², hole loop", { Area, 12.0 + 2.0 * Pi * r, { Centre, 1.0, 0.5 }, { Iuu, Ivv, 0.0 }, 1, 2 }, 4.0);
            }
        }

        // ---- sphere R 1.3 cut 0.5 above its centre
        {
            const double R = 1.3, a2 = R * R - 0.25, a = std::sqrt(a2);
            const BodyToken Sphere = ConstructSphere(Partition, Upright({ 0.2, 0.3, -0.4 }), R).Value;
            ValidateSection(Partition, Sphere, Upright({ 0.2, 0.3, 0.1 }), "Sphere R 1.3 at d = 0.5: disc π(R² − d²)", { Pi * a2, 2.0 * Pi * a, { 0.2, 0.3, 0.1 }, { Pi * a2 * a2 / 4.0, Pi * a2 * a2 / 4.0, 0.0 } }, 2.6);
        }

        // ---- oblique cylinder: r 0.75 h 4, plane through (0, 0, 2) tilted 35° about x → ellipse r × r / cos θ
        {
            const double r = 0.75, θ = 35.0 * Pi / 180.0, c = std::cos(θ), s = std::sin(θ), Major = r / c;
            const BodyToken Cylinder = ConstructCylinder(Partition, Upright({ 0.0, 0.0, 0.0 }), r, 4.0).Value;
            const OrthonormalFrame Plane = ConstructFrame({ 0.0, 0.0, 2.0 }, { 0.0, -s, c }, { 1.0, 0.0, 0.0 });
            const double Perimeter = 4.0 * Major * EllipticSecond(s);
            ValidateSection(Partition, Cylinder, Plane, "Cylinder cut at 35°: ellipse πr²/cos θ, perimeter 4a·E(sin θ)", { Pi * r * Major, Perimeter, { 0.0, 0.0, 2.0 }, { Pi * r * std::pow(Major, 3) / 4.0, Pi * std::pow(r, 3) * Major / 4.0, 0.0 } }, 2.0);
            const TransformOutcome Promoted = PromoteBodyToNurbs(Partition, Cylinder);
            if (Promoted.Refusal.empty())
                ValidateSection(Partition, Promoted.Body, Upright({ 0.0, 0.0, 1.0 }), "NURBS cylinder at z = 1: disc πr²", { Pi * r * r, 2.0 * Pi * r, { 0.0, 0.0, 1.0 }, { Pi * std::pow(r, 4) / 4.0, Pi * std::pow(r, 4) / 4.0, 0.0 } }, 2.0);
            else Report(SectionGate, "NURBS cylinder", Promoted.Refusal, 1.0, false);
        }

        // ---- torus R 2 r 0.5: meridian plane (two discs) and z = 0.3 (annulus)
        {
            const double R = 2.0, r = 0.5;
            const BodyToken Torus = ConstructTorus(Partition, Upright({ 0.0, 0.0, 0.0 }), R, r).Value;
            ValidateSection(Partition, Torus, ConstructFrame({ 0.0, 0.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }), "Torus meridian plane x = 0: two discs 2πr²",
                            { 2.0 * Pi * r * r, 4.0 * Pi * r, { 0.0, 0.0, 0.0 }, { 2.0 * Pi * std::pow(r, 4) / 4.0, 2.0 * (Pi * std::pow(r, 4) / 4.0 + Pi * r * r * R * R), 0.0 }, 2, 2 }, 5.0);
            const double Half = std::sqrt(r * r - 0.09), Outer = R + Half, Inner = R - Half;
            const double Polar = Pi / 4.0 * (std::pow(Outer, 4) - std::pow(Inner, 4));
            ValidateSection(Partition, Torus, Upright({ 0.0, 0.0, 0.3 }), "Torus at z = 0.3: annulus π(R₊² − R₋²)", { Pi * (Outer * Outer - Inner * Inner), 2.0 * Pi * (Outer + Inner), { 0.0, 0.0, 0.3 }, { Polar, Polar, 0.0 }, 1, 2 }, 5.0);
        }

        // ---- cone r 1 h 2 at mid-height
        {
            const BodyToken Cone = ConstructCone(Partition, Upright({ 0.0, 0.0, 0.0 }), 1.0, 2.0).Value;
            ValidateSection(Partition, Cone, Upright({ 0.0, 0.0, 1.0 }), "Cone r 1 h 2 at z = 1: disc r/2", { Pi / 4.0, Pi, { 0.0, 0.0, 1.0 }, { Pi / 64.0, Pi / 64.0, 0.0 } }, 2.0);
        }

        // ---- refusals
        const PlaneSectionMeasure Missed = SectionBody(Partition, Block, Upright({ 0.0, 0.0, 5.0 }));
        Report(RefuseGate, "Plane z = 5 above the block", Missed.Refusal.empty() ? "no refusal" : Missed.Refusal, 0.0, !Missed.Refusal.empty());
        const PlaneSectionMeasure Touching = SectionBody(Partition, Block, Upright({ 0.0, 0.0, 1.0 }));
        Report(RefuseGate, "Plane z = 1 on the top face", Touching.Refusal.empty() ? "no refusal (A " + Fixed(Touching.Area) + ")" : Touching.Refusal, 0.0, !Touching.Refusal.empty());
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    const std::string Only = Count > 2 && std::string(Arguments[1]) == "--only" ? Arguments[2] : "";
    const auto Start = std::chrono::steady_clock::now();
    if (Only.empty() || Only == "mass") ValidateMassProperties();
    if (Only.empty() || Only == "area") ValidateAreas();
    if (Only.empty() || Only == "distance") ValidateDistances();
    if (Only.empty() || Only == "section") ValidateSections();
    const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();

    bool PassedCondition = true;
    for (const GateTally* Gate : AllGates()) PassedCondition = PassedCondition && Gate->Failures == 0 && (Gate->Checks > 0 || !Only.empty());
    std::printf("# Measure validation\n\n%s Measure / section M1–M2: exact mass properties, face areas, edge lengths, minimum distances and plane sections (%.2f s)\n\n", PassedCondition ? "🟢" : "🔴", Seconds);
    std::printf("## Gates\n\n| | Gate | Measures | Limit | Worst | Checks | Failures |\n|---|---|---|---|---|---|---|\n");
    for (const GateTally* Gate : AllGates())
    {
        std::printf("| %s | %s | %s | %s | %.2e | %ld | %ld |\n", Gate->Failures == 0 && Gate->Checks > 0 ? "🟢" : "🔴", Gate->Code, Gate->Subject, Gate->Limit, Gate->Worst, Gate->Checks, Gate->Failures);
    }
    std::printf("\n## Checks\n\n| | Gate | Check | Detail |\n|---|---|---|---|\n");
    for (const DetailRow& Row : Details) std::printf("| %s | %s | %s | %s |\n", Row.PassedCondition ? "🟢" : "🔴", Row.Gate.c_str(), Row.Name.c_str(), Row.Detail.c_str());
    return PassedCondition ? 0 : 1;
}
