//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/XPBDTyreSolver.cpp
//============================================================================================================================================

#include "XPBDTyreSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                              local vector helpers
//------------------------------------------------------------------------------------------------------------------------
namespace {

[[nodiscard]] inline float Dot(const Vector3& A, const Vector3& B) noexcept
{
    return A.x * B.x + A.y * B.y + A.z * B.z;
}

[[nodiscard]] inline Vector3 Cross(const Vector3& A, const Vector3& B) noexcept
{
    return { A.y * B.z - A.z * B.y,
             A.z * B.x - A.x * B.z,
             A.x * B.y - A.y * B.x };
}

// Rotate a vector by a (unit) quaternion:  v' = v + 2w(u×v) + 2u×(u×v),  u = (x,y,z).
[[nodiscard]] inline Vector3 Rotate(const Quaternion& Q, const Vector3& V) noexcept
{
    const Vector3 U{ Q.x, Q.y, Q.z };
    const Vector3 T = Cross(U, V) * 2.0f;
    return V + (T * Q.w) + Cross(U, T);
}

// Any unit vector orthogonal to N (for building a stable local ring frame).
[[nodiscard]] inline Vector3 AnyPerpendicular(const Vector3& N) noexcept
{
    const Vector3 Reference = (std::fabs(N.x) < 0.9f) ? Vector3{ 1.0f, 0.0f, 0.0f } : Vector3{ 0.0f, 1.0f, 0.0f };
    return Cross(N, Reference).Normalized();
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    BUILD
//------------------------------------------------------------------------------------------------------------------------

void XPBDTyreSolver::Build(const XPBDTyreParameters& InParameters, const Vector3& HubPosition,
                           const Quaternion& HubOrientation) noexcept
{
    Parameters = InParameters;
    Parameters.RingSegments = std::max<uint32_t>(6u, Parameters.RingSegments);

    const uint32_t Count = Parameters.RingSegments;
    Nodes.assign(Count, XPBDTyreNode{});
    RestHoopLength.assign(Count, 0.0f);

    // Ring lies in the plane perpendicular to the spin axis; (Tangent0, Tangent1) span that plane in local coordinates.
    const Vector3 Axis     = Parameters.SpinAxisLocal.Normalized();
    const Vector3 Tangent0 = AnyPerpendicular(Axis);
    const Vector3 Tangent1 = Cross(Axis, Tangent0).Normalized();

    const float PerNodeMass    = (Parameters.TotalMass > 0.0f) ? Parameters.TotalMass / static_cast<float>(Count) : 0.0f;
    const float InverseNodeMass = (PerNodeMass > 0.0f) ? 1.0f / PerNodeMass : 0.0f;

    for (uint32_t i = 0u; i < Count; ++i)
    {
        const float Angle = (2.0f * 3.14159265358979323846f * static_cast<float>(i)) / static_cast<float>(Count);
        const Vector3 LocalOffset = (Tangent0 * (Parameters.Radius * std::cos(Angle)))
                                  + (Tangent1 * (Parameters.Radius * std::sin(Angle)));

        XPBDTyreNode& Node = Nodes[i];
        Node.RestOffsetLocal = LocalOffset;
        Node.Position        = HubPosition + Rotate(HubOrientation, LocalOffset);
        Node.Previous        = Node.Position;
        Node.Velocity        = Vector3{ 0.0f, 0.0f, 0.0f };
        Node.InverseMass     = InverseNodeMass;
    }

    for (uint32_t i = 0u; i < Count; ++i)
    {
        const uint32_t Next = (i + 1u) % Count;
        RestHoopLength[i] = (Nodes[Next].Position - Nodes[i].Position).Length();
    }

    AccumulatedForce  = Vector3{ 0.0f, 0.0f, 0.0f };
    AccumulatedTorque = Vector3{ 0.0f, 0.0f, 0.0f };
    LastContactCount  = 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STEP
//------------------------------------------------------------------------------------------------------------------------

void XPBDTyreSolver::Step(float DeltaSeconds, uint32_t Substeps, const Vector3& HubPosition,
                          const Quaternion& HubOrientation, const GroundQuery& Ground) noexcept
{
    AccumulatedForce  = Vector3{ 0.0f, 0.0f, 0.0f };
    AccumulatedTorque = Vector3{ 0.0f, 0.0f, 0.0f };
    LastContactCount  = 0u;

    if (Nodes.empty() || !(DeltaSeconds > 0.0f)) return;

    Substeps = std::max<uint32_t>(1u, Substeps);
    const float h  = DeltaSeconds / static_cast<float>(Substeps);
    const float h2 = h * h;

    // Compliance in XPBD is scaled by 1/h² so stiffness is timestep-independent.
    const float AlphaRadial  = Parameters.CarcassComplianceRadial / h2;
    const float AlphaHoop    = Parameters.TreadComplianceHoop     / h2;
    const float AlphaContact = Parameters.ContactCompliance       / h2;

    const uint32_t Count = static_cast<uint32_t>(Nodes.size());
    float ContactHits = 0.0f;

    for (uint32_t s = 0u; s < Substeps; ++s)
    {
        // ── predict ──────────────────────────────────────────────────────────────────────────────────────────────
        for (XPBDTyreNode& Node : Nodes)
        {
            if (Node.InverseMass <= 0.0f) { Node.Previous = Node.Position; continue; }
            Node.Velocity = Node.Velocity + (Parameters.Gravity * h);
            Node.Previous = Node.Position;
            Node.Position = Node.Position + (Node.Velocity * h);
        }

        // ── project: spokes (radial carcass) — node ↔ kinematic hub anchor, rest length = |RestOffset| = Radius ──
        for (uint32_t i = 0u; i < Count; ++i)
        {
            XPBDTyreNode& Node = Nodes[i];
            if (Node.InverseMass <= 0.0f) continue;

            const Vector3 Anchor = HubPosition + Rotate(HubOrientation, Node.RestOffsetLocal);
            Vector3 Delta        = Node.Position - Anchor;
            float   Distance     = Delta.Length();
            if (Distance < 1e-6f) continue;

            const Vector3 Direction = Delta / Distance;
            const float   C         = Distance - Parameters.Radius;               // rest length is the build radius
            const float   Denom     = Node.InverseMass + AlphaRadial;
            if (Denom <= 0.0f) continue;
            const float   Lambda    = -C / Denom;

            Node.Position = Node.Position + (Direction * (Lambda * Node.InverseMass));

            // Reaction on the hub is the negative of the force on the node: f = λ·∇C / h².
            const Vector3 NodeForce = Direction * (Lambda / h2);
            AccumulatedForce  = AccumulatedForce  - NodeForce;
            AccumulatedTorque = AccumulatedTorque - Cross(Anchor - HubPosition, NodeForce);
        }

        // ── project: hoop (tread band) — neighbour ↔ neighbour ───────────────────────────────────────────────────
        for (uint32_t i = 0u; i < Count; ++i)
        {
            const uint32_t j = (i + 1u) % Count;
            XPBDTyreNode& A = Nodes[i];
            XPBDTyreNode& B = Nodes[j];
            const float Wsum = A.InverseMass + B.InverseMass;
            if (Wsum <= 0.0f) continue;

            Vector3 Delta    = B.Position - A.Position;
            float   Distance = Delta.Length();
            if (Distance < 1e-6f) continue;

            const Vector3 Direction = Delta / Distance;
            const float   C         = Distance - RestHoopLength[i];
            const float   Lambda    = -C / (Wsum + AlphaHoop);

            A.Position = A.Position - (Direction * (Lambda * A.InverseMass));
            B.Position = B.Position + (Direction * (Lambda * B.InverseMass));
        }

        // ── project: ground contact (inequality) + Coulomb friction ──────────────────────────────────────────────
        if (Ground)
        {
            for (XPBDTyreNode& Node : Nodes)
            {
                if (Node.InverseMass <= 0.0f) continue;

                float   GroundZ = 0.0f;
                Vector3 Normal{ 0.0f, 0.0f, 1.0f };
                if (!Ground(Node.Position, GroundZ, Normal)) continue;
                Normal = Normal.Normalized();

                // Signed penetration along the surface normal (surface point taken directly below the node at GroundZ).
                const Vector3 SurfacePoint{ Node.Position.x, Node.Position.y, GroundZ };
                const float   Penetration = Dot(SurfacePoint - Node.Position, Normal);
                if (Penetration <= 0.0f) continue;                                  // above ground, no contact

                ContactHits += 1.0f;
                const float Denom  = Node.InverseMass + AlphaContact;
                const float Lambda = Penetration / Denom;                           // push out along +normal
                const Vector3 NormalCorrection = Normal * (Lambda * Node.InverseMass);
                Node.Position = Node.Position + NormalCorrection;

                // The push-out is a normal impulse on the node → equal/opposite on the hub (via the carcass).
                const Vector3 NodeNormalForce = Normal * (Lambda / h2);
                AccumulatedForce  = AccumulatedForce  + NodeNormalForce;
                AccumulatedTorque = AccumulatedTorque + Cross(Node.Position - HubPosition, NodeNormalForce);

                // Friction: cancel this substep's tangential slip, clamped to μ·|normal correction| (Coulomb cone).
                Vector3 Tangential = (Node.Position - Node.Previous);
                Tangential = Tangential - (Normal * Dot(Tangential, Normal));
                const float TangentLength = Tangential.Length();
                if (TangentLength > 1e-6f)
                {
                    const float MaxSlip = Parameters.FrictionCoefficient * NormalCorrection.Length();
                    const float Applied = std::min(TangentLength, MaxSlip);
                    Node.Position = Node.Position - (Tangential / TangentLength) * Applied;
                }
            }
        }

        // ── update velocities from the positional change over the substep ────────────────────────────────────────
        for (XPBDTyreNode& Node : Nodes)
        {
            if (Node.InverseMass <= 0.0f) { Node.Velocity = Vector3{ 0.0f, 0.0f, 0.0f }; continue; }
            Node.Velocity = (Node.Position - Node.Previous) / h;
        }
    }

    // Report the reaction averaged over the substeps (each accumulated an h²-scaled force per substep).
    const float InverseSubsteps = 1.0f / static_cast<float>(Substeps);
    AccumulatedForce  = AccumulatedForce  * InverseSubsteps;
    AccumulatedTorque = AccumulatedTorque * InverseSubsteps;
    LastContactCount  = static_cast<uint32_t>(ContactHits * InverseSubsteps + 0.5f);
}

} // namespace Frontier
