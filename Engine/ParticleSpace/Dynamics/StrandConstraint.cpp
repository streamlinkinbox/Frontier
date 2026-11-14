/*============================================================================================================================================
                                                          STRANDCONSTRAINT.CPP
============================================================================================================================================*/
// 🧩 XPBD distance-constraint strand relaxation, wind/flutter waves, ball push-out & tear threshold

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/StrandConstraint.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

void InitializeStrandVertices(
    std::span<StrandVertex>       Vertices,
    std::span<StrandVertex>       PrevVertices,
    const StructuralStrandAnchor& Anchor,
    float                         NominalWidthMeters) noexcept
{
    const uint32_t Count = std::min<uint32_t>(
        static_cast<uint32_t>(std::min(Vertices.size(), PrevVertices.size())),
        std::max(2u, Anchor.VertexCount));
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float Dist = static_cast<float>(I) * Anchor.RestSegmentLength;
        const float Px   = Anchor.RootPosition.X + Anchor.TipDirection.X * Dist;
        const float Py   = Anchor.RootPosition.Y + Anchor.TipDirection.Y * Dist;
        const float Pz   = Anchor.RootPosition.Z + Anchor.TipDirection.Z * Dist;
        Vertices[I].PositionRadius[0]     = Px;
        Vertices[I].PositionRadius[1]     = Py;
        Vertices[I].PositionRadius[2]     = Pz;
        Vertices[I].PositionRadius[3]     = NominalWidthMeters;
        PrevVertices[I].PositionRadius[0] = Px;
        PrevVertices[I].PositionRadius[1] = Py;
        PrevVertices[I].PositionRadius[2] = Pz;
        PrevVertices[I].PositionRadius[3] = NominalWidthMeters;
    }
}

bool StepStrandXpbd(
    std::span<StrandVertex>            Vertices,
    std::span<StrandVertex>            PrevVertices,
    const StrandConstraintSpec&        Spec,
    const StructuralStrandAnchor&      Anchor,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                AmbientWindMps,
    float                              DeltaSeconds,
    float                              ElapsedSeconds) noexcept
{
    const uint32_t Count = std::min<uint32_t>(
        static_cast<uint32_t>(std::min(Vertices.size(), PrevVertices.size())),
        std::max(2u, Anchor.VertexCount));
    if (Count < 2u || DeltaSeconds <= 0.0f)
    {
        return true;
    }

    const float Damping = std::exp(-1.8f * DeltaSeconds);
    const float GravityZ = (Spec.Mode == StrandDynamicsMode::UnderwaterKelp) ? 2.4f : -9.81f;
    bool Intact = true;

    // 1. Verlet predict + external forces (gravity + wind wave)
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const bool IsRoot = (I == 0u && Anchor.RootPinned);
        const bool IsTip  = (I + 1u == Count && Anchor.TipPinned);
        if (IsRoot || IsTip)
        {
            PrevVertices[I].PositionRadius[0] = Vertices[I].PositionRadius[0];
            PrevVertices[I].PositionRadius[1] = Vertices[I].PositionRadius[1];
            PrevVertices[I].PositionRadius[2] = Vertices[I].PositionRadius[2];
            continue;
        }

        const float X = Vertices[I].PositionRadius[0];
        const float Y = Vertices[I].PositionRadius[1];
        const float Z = Vertices[I].PositionRadius[2];

        const float Vx = (X - PrevVertices[I].PositionRadius[0]) * Damping;
        const float Vy = (Y - PrevVertices[I].PositionRadius[1]) * Damping;
        const float Vz = (Z - PrevVertices[I].PositionRadius[2]) * Damping;

        PrevVertices[I].PositionRadius[0] = X;
        PrevVertices[I].PositionRadius[1] = Y;
        PrevVertices[I].PositionRadius[2] = Z;

        const float WavePhase = ElapsedSeconds * Spec.WaveSpeedHz * 3.14159f + static_cast<float>(I) * 0.45f + static_cast<float>(Anchor.StrandIndex) * 0.31f;
        const float WaveAmp   = 1.6f * std::max(0.2f, Spec.StiffnessRate);
        const float Ax        = AmbientWindMps.X * 2.5f + std::sin(WavePhase) * WaveAmp;
        const float Ay        = AmbientWindMps.Y * 2.5f + std::cos(WavePhase * 0.87f) * WaveAmp;
        const float Az        = GravityZ + AmbientWindMps.Z * 1.5f;

        Vertices[I].PositionRadius[0] = X + Vx + Ax * DeltaSeconds * DeltaSeconds;
        Vertices[I].PositionRadius[1] = Y + Vy + Ay * DeltaSeconds * DeltaSeconds;
        Vertices[I].PositionRadius[2] = Z + Vz + Az * DeltaSeconds * DeltaSeconds;
    }

    // 2. XPBD distance constraint iterations
    const float RestLen = std::max(1.0e-3f, Anchor.RestSegmentLength);
    constexpr uint32_t SolverIterations = 4u;
    for (uint32_t Iter = 0u; Iter < SolverIterations; ++Iter)
    {
        for (uint32_t I = 0u; I + 1u < Count; ++I)
        {
            const bool PinA = (I == 0u && Anchor.RootPinned);
            const bool PinB = (I + 1u == Count - 1u && Anchor.TipPinned);
            if (PinA && PinB)
            {
                continue;
            }
            const float Dx   = Vertices[I + 1u].PositionRadius[0] - Vertices[I].PositionRadius[0];
            const float Dy   = Vertices[I + 1u].PositionRadius[1] - Vertices[I].PositionRadius[1];
            const float Dz   = Vertices[I + 1u].PositionRadius[2] - Vertices[I].PositionRadius[2];
            const float Dist = std::sqrt(std::max(1.0e-8f, Dx * Dx + Dy * Dy + Dz * Dz));
            const float Diff = (Dist - RestLen) / Dist;
            if (Spec.TearThresholdSpeed > 0.0f && (Dist - RestLen) / DeltaSeconds > Spec.TearThresholdSpeed)
            {
                Intact = false;
            }

            if (PinA)
            {
                Vertices[I + 1u].PositionRadius[0] -= Dx * Diff;
                Vertices[I + 1u].PositionRadius[1] -= Dy * Diff;
                Vertices[I + 1u].PositionRadius[2] -= Dz * Diff;
            }
            else if (PinB)
            {
                Vertices[I].PositionRadius[0] += Dx * Diff;
                Vertices[I].PositionRadius[1] += Dy * Diff;
                Vertices[I].PositionRadius[2] += Dz * Diff;
            }
            else
            {
                Vertices[I].PositionRadius[0]      += Dx * Diff * 0.5f;
                Vertices[I].PositionRadius[1]      += Dy * Diff * 0.5f;
                Vertices[I].PositionRadius[2]      += Dz * Diff * 0.5f;
                Vertices[I + 1u].PositionRadius[0] -= Dx * Diff * 0.5f;
                Vertices[I + 1u].PositionRadius[1] -= Dy * Diff * 0.5f;
                Vertices[I + 1u].PositionRadius[2] -= Dz * Diff * 0.5f;
            }
        }

        // Interactive ball collider push-out & floor clamp
        for (uint32_t I = 0u; I < Count; ++I)
        {
            const bool IsRoot = (I == 0u && Anchor.RootPinned);
            const bool IsTip  = (I + 1u == Count && Anchor.TipPinned);
            if (IsRoot || IsTip)
            {
                continue;
            }

            if (Vertices[I].PositionRadius[2] < Collider.FloorElevationMeters + 0.02f)
            {
                Vertices[I].PositionRadius[2] = Collider.FloorElevationMeters + 0.02f;
            }

            if (Collider.BallRadiusMeters > 0.01f)
            {
                const float Dx = Vertices[I].PositionRadius[0] - Collider.BallPositionMeters.X;
                const float Dy = Vertices[I].PositionRadius[1] - Collider.BallPositionMeters.Y;
                const float Dz = Vertices[I].PositionRadius[2] - Collider.BallPositionMeters.Z;
                const float MinR = Collider.BallRadiusMeters + 0.03f;
                const float D2 = Dx * Dx + Dy * Dy + Dz * Dz;
                if (D2 < MinR * MinR)
                {
                    const float D = std::sqrt(std::max(1.0e-6f, D2));
                    Vertices[I].PositionRadius[0] = Collider.BallPositionMeters.X + (Dx / D) * MinR;
                    Vertices[I].PositionRadius[1] = Collider.BallPositionMeters.Y + (Dy / D) * MinR;
                    Vertices[I].PositionRadius[2] = Collider.BallPositionMeters.Z + (Dz / D) * MinR;
                }
            }
        }
    }

    return Intact;
}

} // namespace Frontier::ParticleSpace
