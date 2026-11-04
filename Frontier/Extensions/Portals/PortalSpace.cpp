/*==========================================================================================================================================
                                                             PORTALSPACE.CPP
==========================================================================================================================================*/
// 🧩 The portal transit: resolve apertures from the level's Portal records, pair them by name, and carry a crossing body through
//    the pair's transform with the aperture ratio applied to its offsets and velocities. The flip is a half turn about the
//    aperture's up axis, so a body enters one face and leaves the peer's face pointing outward.

#include "Frontier/Extensions/Portals/PortalSpace.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/TransformProjection.h"
#include "Frontier/Engine/Scene/WorldSpace.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

std::array<double, 3> Normalize(const std::array<double, 3>& Vector) noexcept
{
    const double Length = std::sqrt(Vector[0] * Vector[0] + Vector[1] * Vector[1] + Vector[2] * Vector[2]);
    if (Length < 1.0e-12) return { 0.0, 0.0, 1.0 };
    return { Vector[0] / Length, Vector[1] / Length, Vector[2] / Length };
}

double Dot(const std::array<double, 3>& A, const std::array<double, 3>& B) noexcept
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

std::array<double, 3> Subtract(const std::array<double, 3>& A, const std::array<double, 3>& B) noexcept
{
    return { A[0] - B[0], A[1] - B[1], A[2] - B[2] };
}

// 📝 Quaternion product (x, y, z, w).
std::array<double, 4> MultiplyQuaternion(const std::array<double, 4>& A, const std::array<double, 4>& B) noexcept
{
    return {
        A[3] * B[0] + A[0] * B[3] + A[1] * B[2] - A[2] * B[1],
        A[3] * B[1] - A[0] * B[2] + A[1] * B[3] + A[2] * B[0],
        A[3] * B[2] + A[0] * B[1] - A[1] * B[0] + A[2] * B[3],
        A[3] * B[3] - A[0] * B[0] - A[1] * B[1] - A[2] * B[2],
    };
}

std::array<double, 4> Conjugate(const std::array<double, 4>& Q) noexcept
{
    return { -Q[0], -Q[1], -Q[2], Q[3] };
}

std::array<double, 3> Rotate(const std::array<double, 4>& Q, const std::array<double, 3>& V) noexcept
{
    const std::array<double, 4> P{ V[0], V[1], V[2], 0.0 };
    const std::array<double, 4> R = MultiplyQuaternion(MultiplyQuaternion(Q, P), Conjugate(Q));
    return { R[0], R[1], R[2] };
}

// 📝 The rotation of an aperture as a quaternion, from its normalized world axes (right, up, normal as the matrix columns).
std::array<double, 4> ApertureRotation(const PortalAperture& Aperture) noexcept
{
    const auto& R = Aperture.Right;
    const auto& U = Aperture.Up;
    const auto& N = Aperture.Normal;
    // Rotation matrix rows: [R.x U.x N.x; R.y U.y N.y; R.z U.z N.z].
    const double Trace = R[0] + U[1] + N[2];
    std::array<double, 4> Q{};
    if (Trace > 0.0)
    {
        const double S = std::sqrt(Trace + 1.0) * 2.0;
        Q = { (U[2] - N[1]) / S, (N[0] - R[2]) / S, (R[1] - U[0]) / S, 0.25 * S };
    }
    else if (R[0] > U[1] && R[0] > N[2])
    {
        const double S = std::sqrt(1.0 + R[0] - U[1] - N[2]) * 2.0;
        Q = { 0.25 * S, (U[0] + R[1]) / S, (N[0] + R[2]) / S, (U[2] - N[1]) / S };
    }
    else if (U[1] > N[2])
    {
        const double S = std::sqrt(1.0 + U[1] - R[0] - N[2]) * 2.0;
        Q = { (U[0] + R[1]) / S, 0.25 * S, (N[1] + U[2]) / S, (N[0] - R[2]) / S };
    }
    else
    {
        const double S = std::sqrt(1.0 + N[2] - R[0] - U[1]) * 2.0;
        Q = { (N[0] + R[2]) / S, (N[1] + U[2]) / S, 0.25 * S, (R[1] - U[0]) / S };
    }
    const double Length = std::sqrt(Q[0] * Q[0] + Q[1] * Q[1] + Q[2] * Q[2] + Q[3] * Q[3]);
    if (Length < 1.0e-12) return { 0.0, 0.0, 0.0, 1.0 };
    return { Q[0] / Length, Q[1] / Length, Q[2] / Length, Q[3] / Length };
}

// 📝 A half turn about the aperture's up axis: the flip that turns "into A" into "out of B".
constexpr std::array<double, 4> FlipQuaternion{ 0.0, 1.0, 0.0, 0.0 };   // [-] Ry(180°) = (0, 1, 0, 0)

} // namespace

double ResolvePortalScale(const PortalAperture& From, const PortalAperture& To) noexcept
{
    if (From.HalfWidth < 1.0e-6) return 1.0;
    return std::clamp(To.HalfWidth / From.HalfWidth, 0.05, 20.0);
}

void PortalSpace::Configure(const WorldSpace& World)
{
    Apertures_.clear();
    Pairs_.clear();
    for (const SceneRecordBase* Record : World.FindAllByType("Portal"))
    {
        const auto* Portal = dynamic_cast<const PortalRecord*>(Record);
        if (Portal == nullptr) continue;
        const PortalConfiguration& Configuration = Portal->QueryConfiguration();
        if (!Configuration.Enabled) continue;
        PortalAperture Aperture;
        Aperture.Record     = Portal->QueryToken().Value;
        Aperture.Name       = Portal->QueryRecordName();
        Aperture.LinkedName = Configuration.LinkedPortalName;
        Aperture.Placement  = ResolveSpatialPlacement(Configuration.Spatial);
        Aperture.Position   = { Aperture.Placement[12], Aperture.Placement[13], Aperture.Placement[14] };
        Aperture.Right      = Normalize({ Aperture.Placement[0], Aperture.Placement[1], Aperture.Placement[2] });
        Aperture.Up         = Normalize({ Aperture.Placement[4], Aperture.Placement[5], Aperture.Placement[6] });
        Aperture.Normal     = Normalize({ Aperture.Placement[8], Aperture.Placement[9], Aperture.Placement[10] });
        Aperture.HalfWidth  = std::max(0.05, 0.5 * Configuration.ApertureWidthMetres * std::abs(Configuration.Spatial.Scale[0]));
        Aperture.HalfHeight = std::max(0.05, 0.5 * Configuration.ApertureHeightMetres * std::abs(Configuration.Spatial.Scale[1]));
        Apertures_.push_back(std::move(Aperture));
    }
    // 📝 Pair by name both ways: A links to B and B links to A.
    for (const PortalAperture& Aperture : Apertures_)
    {
        if (Aperture.LinkedName.empty()) continue;
        for (const PortalAperture& Peer : Apertures_)
        {
            if (Peer.Record == Aperture.Record || Peer.Name != Aperture.LinkedName) continue;
            if (Peer.LinkedName == Aperture.Name) Pairs_[Aperture.Record] = Peer.Record;
            break;
        }
    }
}

const PortalAperture* PortalSpace::QueryPeer(const PortalAperture& Aperture) const noexcept
{
    const auto Found = Pairs_.find(Aperture.Record);
    if (Found == Pairs_.end()) return nullptr;
    for (const PortalAperture& Candidate : Apertures_)
        if (Candidate.Record == Found->second) return &Candidate;
    return nullptr;
}

PortalTransit PortalSpace::ResolveTransit(const PortalBodyPose& Pose, const PortalAperture& From, const PortalAperture& To) const
{
    using namespace GlobalIllumination;
    const double Scale = ResolvePortalScale(From, To);
    // 📝 World → From-local, flip through the aperture (half turn about up), scale, then To-local → world.
    const Vector3 Local      = TransformPoint(InvertPlacement(From.Placement), Vector3{ Pose.Position[0], Pose.Position[1], Pose.Position[2] });
    const Vector3 Flipped    = { -Local[0] * Scale, Local[1] * Scale, -Local[2] * Scale };
    const Vector3 Emerging   = TransformPoint(To.Placement, Flipped);

    const std::array<double, 4> FromRotation = ApertureRotation(From);
    const std::array<double, 4> ToRotation   = ApertureRotation(To);
    const std::array<double, 4> Carry        = MultiplyQuaternion(ToRotation, MultiplyQuaternion(FlipQuaternion, Conjugate(FromRotation)));

    PortalTransit Transit;
    Transit.Scale                  = Scale;
    Transit.Pose.Position          = { Emerging[0], Emerging[1], Emerging[2] };
    Transit.Pose.Orientation       = MultiplyQuaternion(Carry, Pose.Orientation);
    Transit.Pose.LinearVelocity    = Rotate(Carry, Pose.LinearVelocity);
    Transit.Pose.AngularVelocity   = Rotate(Carry, Pose.AngularVelocity);
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Transit.Pose.LinearVelocity[Axis]  *= Scale;
        Transit.Pose.AngularVelocity[Axis] *= Scale;
    }
    return Transit;
}

PortalCrossing PortalSpace::DetectCrossing(const std::array<double, 3>& Before, const std::array<double, 3>& After,
                                           const PortalAperture& Aperture) const
{
    PortalCrossing Crossing;
    Crossing.SignedBefore = Dot(Subtract(Before, Aperture.Position), Aperture.Normal);
    Crossing.SignedAfter  = Dot(Subtract(After, Aperture.Position), Aperture.Normal);
    // 📝 A front-to-back crossing: the signed distance goes from positive to non-positive.
    Crossing.Crossed = Crossing.SignedBefore > 0.0 && Crossing.SignedAfter <= 0.0;
    if (!Crossing.Crossed) return Crossing;
    const double Span = Crossing.SignedBefore - Crossing.SignedAfter;
    const double T    = Span > 1.0e-12 ? Crossing.SignedBefore / Span : 0.0;
    Crossing.Point    = { Before[0] + (After[0] - Before[0]) * T, Before[1] + (After[1] - Before[1]) * T,
                          Before[2] + (After[2] - Before[2]) * T };
    const std::array<double, 3> Offset = Subtract(Crossing.Point, Aperture.Position);
    Crossing.WithinAperture = std::abs(Dot(Offset, Aperture.Right)) <= Aperture.HalfWidth &&
                                std::abs(Dot(Offset, Aperture.Up)) <= Aperture.HalfHeight;
    return Crossing;
}

PortalBodyPose PortalSpace::ResolveClone(const PortalBodyPose& Pose, const PortalAperture& From, const PortalAperture& To) const
{
    return ResolveTransit(Pose, From, To).Pose;
}

} // namespace Frontier
