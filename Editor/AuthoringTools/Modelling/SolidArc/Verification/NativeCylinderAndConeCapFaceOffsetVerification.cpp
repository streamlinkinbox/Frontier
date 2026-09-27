//=============================================================================================================================================
// SolidArc · Batch 56 · exact native-cylinder and native-cone upper-cap face offsets
//=============================================================================================================================================
#include "Kernel/FaceEditSolver.h"
#include "Console/ConsoleHost.h"
#include "VerificationPanel.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using namespace Frontier;

namespace
{
constexpr double CylinderRadius = 5.0;
constexpr double CylinderHeight = 6.0;
constexpr double ConeFootRadius = 8.0;
constexpr double ConeTopRadius = 3.0;
constexpr double ConeHeight = 6.0;
constexpr double DistanceA = 0.5;
constexpr double DistanceB = 1.5;
constexpr double DistanceC = 3.0;

[[nodiscard]] int FaceToward(const BrepBody& Body, Vec3 Direction) noexcept
{
    int Best = -1; double BestDot = -2.0;
    for (int I = 0; I < static_cast<int>(Body.Faces.size()); ++I)
    {
        const BrepFace& F = Body.Faces[I];
        const Vec3 N = Body.FaceNormal(I, 0.5 * (F.Surface.DomainStartU() + F.Surface.DomainEndU()),
                                       0.5 * (F.Surface.DomainStartV() + F.Surface.DomainEndV())).Normalised();
        const double D = N.Dot(Direction.Normalised());
        if (D > BestDot) { BestDot = D; Best = I; }
    }
    return Best;
}

[[nodiscard]] bool SameSource(const BrepBody& Body, const BrepBody& Snapshot, const BodyReport& Before) noexcept
{
    const BodyReport After = Body.Validate();
    return After.Vertices == Before.Vertices && After.Edges == Before.Edges && After.Faces == Before.Faces &&
           After.Loops == Before.Loops && After.EulerCharacteristic == Before.EulerCharacteristic &&
           After.Genus == Before.Genus && After.OpenEdges == Before.OpenEdges &&
           After.NonManifoldEdges == Before.NonManifoldEdges && After.MisorientedEdges == Before.MisorientedEdges &&
           std::fabs(After.Volume - Before.Volume) <= 1e-12 && Body.Vertices.size() == Snapshot.Vertices.size() &&
           Body.Edges.size() == Snapshot.Edges.size() && Body.Coedges.size() == Snapshot.Coedges.size() &&
           Body.Loops.size() == Snapshot.Loops.size() && Body.Faces.size() == Snapshot.Faces.size();
}

[[nodiscard]] bool ExactTopology(const BrepBody& Body) noexcept
{
    const BodyReport R = Body.Validate();
    return R.Solid() && R.Hulls == 1 && R.Genus == 0 && R.OpenEdges == 0 && R.NonManifoldEdges == 0 &&
           R.MisorientedEdges == 0 && Body.Vertices.size() == 2 && Body.Edges.size() == 3 &&
           Body.Coedges.size() == 6 && Body.Loops.size() == 3 && Body.Faces.size() == 3;
}

[[nodiscard]] bool ExactCylinder(const BrepBody& Body, double Radius, double Height) noexcept
{
    if (!ExactTopology(Body)) return false;
    const Box3 B = Body.Bounds();
    if (std::fabs(B.Low.X + Radius) > 1e-8 || std::fabs(B.High.X - Radius) > 1e-8 ||
        std::fabs(B.Low.Y + Radius) > 1e-8 || std::fabs(B.High.Y - Radius) > 1e-8 ||
        std::fabs(B.Low.Z) > 1e-8 || std::fabs(B.High.Z - Height) > 1e-8) return false;
    int Cylinders = 0, Planes = 0;
    for (const BrepFace& F : Body.Faces)
    {
        if (F.Surface.Classification == SurfaceClassification::Cylinder)
        {
            ++Cylinders;
            if (F.Surface.Origin.Distance({ 0, 0, 0 }) > 1e-8 || F.Surface.Axis.Normalised().Dot(Vec3::UnitZ()) < 1.0 - 1e-10 ||
                std::fabs(F.Surface.RadiusMajor - Radius) > 1e-8 || std::fabs(F.Surface.RadiusMinor - Radius) > 1e-8) return false;
        }
        else if (F.Surface.Classification == SurfaceClassification::Plane) ++Planes;
        else return false;
    }
    return Cylinders == 1 && Planes == 2;
}

[[nodiscard]] bool ExactCone(const BrepBody& Body, double Foot, double Top, double Height) noexcept
{
    if (!ExactTopology(Body)) return false;
    const Box3 B = Body.Bounds();
    if (std::fabs(B.Low.Z) > 1e-8 || std::fabs(B.High.Z - Height) > 1e-8) return false;
    int Cones = 0, Planes = 0;
    for (const BrepFace& F : Body.Faces)
    {
        if (F.Surface.Classification == SurfaceClassification::Cone)
        {
            ++Cones;
            if (F.Surface.Origin.Distance({ 0, 0, 0 }) > 1e-8 || F.Surface.Axis.Normalised().Dot(Vec3::UnitZ()) < 1.0 - 1e-10 ||
                std::fabs(F.Surface.RadiusMajor - Foot) > 1e-8 || std::fabs(F.Surface.RadiusMinor - Top) > 1e-8) return false;
        }
        else if (F.Surface.Classification == SurfaceClassification::Plane) ++Planes;
        else return false;
    }
    return Cones == 1 && Planes == 2;
}
}

int main()
{
    VerificationPanel Panel("SolidArc · Batch 56 · native-cylinder and native-cone upper-cap face offsets");
    const auto CylinderDeliver = BrepBody::Cylinder({ 0, 0, 0 }, Vec3::UnitZ(), CylinderRadius, CylinderHeight);
    const auto ConeDeliver = BrepBody::Cone({ 0, 0, 0 }, Vec3::UnitZ(), ConeFootRadius, ConeTopRadius, ConeHeight);
    Panel.Expect("Both exact native analytic sources are constructed", CylinderDeliver && ConeDeliver);
    if (!CylinderDeliver || !ConeDeliver) return Panel.Conclude();

    const BrepBody Cylinder = CylinderDeliver.Payload, CylinderSnapshot = Cylinder;
    const BrepBody Cone = ConeDeliver.Payload, ConeSnapshot = Cone;
    const BodyReport CylinderBefore = Cylinder.Validate(), ConeBefore = Cone.Validate();
    const int CylinderTop = FaceToward(Cylinder, Vec3::UnitZ()), CylinderBottom = FaceToward(Cylinder, -Vec3::UnitZ()), CylinderSide = FaceToward(Cylinder, Vec3::UnitX());
    const int ConeTop = FaceToward(Cone, Vec3::UnitZ()), ConeBottom = FaceToward(Cone, -Vec3::UnitZ()), ConeSide = FaceToward(Cone, Vec3::UnitX());

    Panel.Section("Exact native source recognition");
    Panel.Expect("The cylinder is closed V2/E3/C6/L3/F3 with one analytic side and two planar caps", ExactCylinder(Cylinder, CylinderRadius, CylinderHeight));
    Panel.Expect("The cone frustum is closed V2/E3/C6/L3/F3 with one analytic side and two planar caps", ExactCone(Cone, ConeFootRadius, ConeTopRadius, ConeHeight));
    Panel.Expect("Each route selects distinct upper, lower, and side faces", CylinderTop != CylinderBottom && CylinderTop != CylinderSide &&
                 ConeTop != ConeBottom && ConeTop != ConeSide);

    Panel.Section("Multiple positive upper-cap distances");
    for (const double Distance : { DistanceA, DistanceB, DistanceC })
    {
        const auto CylinderResult = FaceEditSolver::OffsetCylinderCap(Cylinder, CylinderTop, Distance);
        const auto ConeResult = FaceEditSolver::OffsetConeCap(Cone, ConeTop, Distance);
        const double NewConeTop = ConeTopRadius + (ConeTopRadius - ConeFootRadius) * Distance / ConeHeight;
        const double CylinderExpectedVolume = ScalarCriteria::Pi * CylinderRadius * CylinderRadius * (CylinderHeight + Distance);
        const double ConeExpectedVolume = ScalarCriteria::Pi * (ConeHeight + Distance) *
            (ConeFootRadius * ConeFootRadius + ConeFootRadius * NewConeTop + NewConeTop * NewConeTop) / 3.0;
        Panel.Expect("Cylinder cap offset retains closed native topology", CylinderResult && ExactCylinder(CylinderResult.Payload, CylinderRadius, CylinderHeight + Distance));
        Panel.Expect("Cone cap offset retains closed native topology", ConeResult && ExactCone(ConeResult.Payload, ConeFootRadius, NewConeTop, ConeHeight + Distance));
        if (CylinderResult) Panel.Within("Cylinder volume follows pi r squared times extended height", std::fabs(CylinderResult.Payload.Validate().Volume - CylinderExpectedVolume) / CylinderExpectedVolume, 1e-3);
        if (ConeResult) Panel.Within("Cone volume follows the frustum identity", std::fabs(ConeResult.Payload.Validate().Volume - ConeExpectedVolume) / ConeExpectedVolume, 1e-3);
    }
    Panel.Expect("The public dispatcher reaches both native upper-cap routes", FaceEditSolver::OffsetFace(Cylinder, CylinderTop, DistanceB) &&
                 FaceEditSolver::OffsetFace(Cone, ConeTop, DistanceB));
    Panel.Expect("Both native reconstructions preserve their sources", SameSource(Cylinder, CylinderSnapshot, CylinderBefore) &&
                 SameSource(Cone, ConeSnapshot, ConeBefore));

    Panel.Section("Transactional refusal boundaries");
    Panel.Expect("Only the cylinder upper planar cap is supported", !FaceEditSolver::OffsetCylinderCap(Cylinder, CylinderBottom, DistanceB) &&
                 !FaceEditSolver::OffsetCylinderCap(Cylinder, CylinderSide, DistanceB));
    Panel.Expect("Only the cone upper planar cap is supported", !FaceEditSolver::OffsetConeCap(Cone, ConeBottom, DistanceB) &&
                 !FaceEditSolver::OffsetConeCap(Cone, ConeSide, DistanceB));
    Panel.Expect("Zero, negative, and non-finite distances refuse for both routes", !FaceEditSolver::OffsetCylinderCap(Cylinder, CylinderTop, 0.0) &&
                 !FaceEditSolver::OffsetCylinderCap(Cylinder, CylinderTop, -0.1) &&
                 !FaceEditSolver::OffsetCylinderCap(Cylinder, CylinderTop, std::numeric_limits<double>::infinity()) &&
                 !FaceEditSolver::OffsetConeCap(Cone, ConeTop, 0.0) && !FaceEditSolver::OffsetConeCap(Cone, ConeTop, -0.1) &&
                 !FaceEditSolver::OffsetConeCap(Cone, ConeTop, std::numeric_limits<double>::infinity()));
    const auto TranslatedCylinder = BrepBody::Cylinder({ 2, 0, 0 }, Vec3::UnitZ(), CylinderRadius, CylinderHeight);
    const auto TranslatedCone = BrepBody::Cone({ 2, 0, 0 }, Vec3::UnitZ(), ConeFootRadius, ConeTopRadius, ConeHeight);
    Panel.Expect("Translated native supports refuse the origin-anchored routes", TranslatedCylinder && TranslatedCone &&
                 !FaceEditSolver::OffsetCylinderCap(TranslatedCylinder.Payload, FaceToward(TranslatedCylinder.Payload, Vec3::UnitZ()), DistanceB) &&
                 !FaceEditSolver::OffsetConeCap(TranslatedCone.Payload, FaceToward(TranslatedCone.Payload, Vec3::UnitZ()), DistanceB));
    Panel.Expect("The routes refuse each other's analytic support", !FaceEditSolver::OffsetCylinderCap(Cone, ConeTop, DistanceB) &&
                 !FaceEditSolver::OffsetConeCap(Cylinder, CylinderTop, DistanceB));
    const auto Apex = BrepBody::Cone({ 0, 0, 0 }, Vec3::UnitZ(), ConeFootRadius, 0.0, ConeHeight);
    Panel.Expect("An apex cone remains outside the full-frustum cone-cap route", Apex &&
                 !FaceEditSolver::OffsetConeCap(Apex.Payload, FaceToward(Apex.Payload, Vec3::UnitZ()), DistanceB));
    const auto Crossing = FaceEditSolver::OffsetConeCap(Cone, ConeTop, 4.0);
    Panel.Expect("A tapering cone refuses a positive extension that crosses the apex", !Crossing);
    BrepBody MalformedCylinder = Cylinder; MalformedCylinder.Edges.front().Coedges.push_back(MalformedCylinder.Edges.front().Coedges.front());
    BrepBody MalformedCone = Cone; MalformedCone.Edges.front().Coedges.push_back(MalformedCone.Edges.front().Coedges.front());
    Panel.Expect("Malformed native supports refuse without healing", !FaceEditSolver::OffsetCylinderCap(MalformedCylinder, CylinderTop, DistanceB) &&
                 !FaceEditSolver::OffsetConeCap(MalformedCone, ConeTop, DistanceB));
    Panel.Expect("All refusal paths preserve both valid sources", SameSource(Cylinder, CylinderSnapshot, CylinderBefore) &&
                 SameSource(Cone, ConeSnapshot, ConeBefore));

    Panel.Section("Durable native-cap contact sheet");
    const std::filesystem::path Proof = std::filesystem::path(SOLIDARC_PROOF_FOLDER) / "Batch56_NativeCylinderAndConeCapFaceOffset.png";
    std::error_code Error; std::filesystem::remove(Proof, Error);
    const auto CylinderResult = FaceEditSolver::OffsetCylinderCap(Cylinder, CylinderTop, DistanceB);
    const auto ConeResult = FaceEditSolver::OffsetConeCap(Cone, ConeTop, DistanceB);
    ConsoleHost Host(SOLIDARC_PROOF_FOLDER, 1800, 900);
    const bool Added = CylinderResult && ConeResult &&
        Host.Document().AddBody("SharpCylinderCap", Cylinder.Transformed(Mat4::Translation({ -12, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetCylinderCap", CylinderResult.Payload.Transformed(Mat4::Translation({ -4, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("SharpConeCap", Cone.Transformed(Mat4::Translation({ 5, -4, 0 }))).Identity > 0 &&
        Host.Document().AddBody("OffsetConeCap", ConeResult.Payload.Transformed(Mat4::Translation({ 13, -4, 0 }))).Identity > 0;
    const bool Rendered = Added && Host.Execute("show shading flat") && Host.Execute("view iso") && Host.Execute("view orbit 25 -18") &&
        Host.Execute("view fit") && Host.Execute("render Batch56_NativeCylinderAndConeCapFaceOffset");
    Panel.Expect("The consolidated native-cap proof render completes", Rendered);
    Panel.Expect("The durable native-cap proof PNG is visible", std::filesystem::exists(Proof) && std::filesystem::file_size(Proof, Error) > 100000);
    return Panel.Conclude();
}
