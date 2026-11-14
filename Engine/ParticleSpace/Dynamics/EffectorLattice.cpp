/*============================================================================================================================================
                                                          EFFECTORLATTICE.CPP
============================================================================================================================================*/
// 🧩 MoGraph effector field shapes, drivers, twin 3D effector handles & lattice deformation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/EffectorLattice.h"

#include "Engine/ColourSpace/ColourRampSampler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

float EvaluateEffectorInfluence(
    Mathematics::Axis3D SamplePoint,
    EffectorVolumeSpec  Volume,
    float               FalloffContrast) noexcept
{
    if (Volume.SizeMeters <= 1.0e-4f)
    {
        return 0.0f;
    }

    const float Dx = SamplePoint.X - Volume.CentreMeters.X;
    const float Dy = SamplePoint.Y - Volume.CentreMeters.Y;
    const float Dz = SamplePoint.Z - Volume.CentreMeters.Z;
    const float R  = std::max(0.1f, Volume.SizeMeters);

    float NormDist = 0.0f;
    switch (Volume.Shape)
    {
        case EffectorVolumeShape::Box:
        {
            NormDist = std::max({ std::abs(Dx), std::abs(Dy), std::abs(Dz) }) / R;
            break;
        }
        case EffectorVolumeShape::Torus:
        {
            const float RadialXY = std::sqrt(Dx * Dx + Dy * Dy) - R * 0.72f;
            NormDist = std::sqrt(RadialXY * RadialXY + Dz * Dz) / (R * 0.38f);
            break;
        }
        case EffectorVolumeShape::Disc:
        {
            const float RadialXY = std::sqrt(Dx * Dx + Dy * Dy);
            NormDist = std::max(RadialXY / R, std::abs(Dz) / (R * 0.25f));
            break;
        }
        case EffectorVolumeShape::Cone:
        {
            const float RadialXY = std::sqrt(Dx * Dx + Dy * Dy);
            const float H        = std::clamp((Dz + R * 0.5f) / R, 0.0f, 1.0f);
            NormDist = std::max(RadialXY / std::max(0.1f, R * (1.0f - H * 0.7f)), std::abs(Dz) / R);
            break;
        }
        case EffectorVolumeShape::Sphere:
        default:
        {
            NormDist = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz) / R;
            break;
        }
    }

    const float T = std::clamp(1.0f - NormDist, 0.0f, 1.0f);
    return std::pow(T, std::max(0.25f, FalloffContrast));
}

void StepEffectorLattice(
    std::span<ParticleSlot>    Slots,
    const EffectorLatticeSpec& Spec,
    Mathematics::Axis3D        OriginMeters,
    float                      ElapsedSeconds) noexcept
{
    if (!Spec.Active || Slots.empty())
    {
        return;
    }

    const uint32_t Count = static_cast<uint32_t>(Slots.size());
    const uint32_t Cols  = std::max(2u, Spec.Columns);
    const uint32_t Rows  = std::max(2u, Spec.Rows);

    for (uint32_t I = 0u; I < Count; ++I)
    {
        const uint32_t Ix = I % Cols;
        const uint32_t Iy = (I / Cols) % Rows;
        const uint32_t Iz = I / (Cols * Rows);

        const float U = (static_cast<float>(Ix) / static_cast<float>(Cols - 1u)) * 2.0f - 1.0f;
        const float V = (static_cast<float>(Iy) / static_cast<float>(Rows - 1u)) * 2.0f - 1.0f;
        const float W = static_cast<float>(Iz) * 0.35f;

        const float BaseX = OriginMeters.X + U * 3.0f;
        const float BaseY = OriginMeters.Y + V * 3.0f;
        const float BaseZ = OriginMeters.Z + 0.2f + W;

        const float InfA = EvaluateEffectorInfluence({ BaseX, BaseY, BaseZ }, Spec.PrimaryEffector, Spec.FalloffContrast);
        const float InfB = EvaluateEffectorInfluence({ BaseX, BaseY, BaseZ }, Spec.SecondaryEffector, Spec.FalloffContrast);
        const float Inf  = std::clamp(InfA + InfB, 0.0f, 1.0f);

        float OffsetZ   = 0.0f;
        float SpinAngle = 0.0f;
        float ScaleMul  = 1.0f;

        switch (Spec.Driver)
        {
            case MoGraphEffectorDriver::ExpandingPulseRing:
            {
                const float R = std::sqrt(U * U + V * V);
                OffsetZ  = std::sin(R * 6.0f - ElapsedSeconds * 3.2f) * Spec.MaxHeightMeters * (0.35f + Inf);
                ScaleMul = Spec.MinScale + (1.0f - Spec.MinScale) * (0.5f + 0.5f * Inf);
                break;
            }
            case MoGraphEffectorDriver::OrbitingDualEffector:
            {
                SpinAngle = Inf * 2.4f + ElapsedSeconds * 1.4f;
                OffsetZ   = Inf * Spec.MaxHeightMeters;
                ScaleMul  = Spec.MinScale + (1.0f - Spec.MinScale) * Inf;
                break;
            }
            case MoGraphEffectorDriver::CurlIslands:
            {
                const float N = ColourSpace::SampleLatticeNoise3D(BaseX * 0.8f, BaseY * 0.8f, BaseZ * 0.8f + ElapsedSeconds);
                OffsetZ  = N * Spec.MaxHeightMeters * (0.4f + Inf);
                ScaleMul = Spec.MinScale + (1.0f - Spec.MinScale) * N;
                break;
            }
            case MoGraphEffectorDriver::Placeable3DEffector:
            case MoGraphEffectorDriver::ForceFieldBallOnly:
            {
                ScaleMul = Spec.MinScale + (1.35f - Spec.MinScale) * Inf;
                OffsetZ  = Inf * Spec.MaxHeightMeters;
                break;
            }
            case MoGraphEffectorDriver::FluidSimplexWave:
            default:
            {
                OffsetZ  = (0.5f + 0.5f * std::sin((BaseX + BaseY) * 1.6f + ElapsedSeconds * 2.5f)) * Spec.MaxHeightMeters * (0.35f + Inf);
                ScaleMul = Spec.MinScale + (1.0f - Spec.MinScale) * (0.4f + 0.6f * Inf);
                break;
            }
        }

        ParticleSlot& S = Slots[I];
        S.PositionAge[0]      = BaseX;
        S.PositionAge[1]      = BaseY;
        S.PositionAge[2]      = BaseZ + OffsetZ;
        S.PositionAge[3]      = Inf;
        S.VelocityLifetime[0] = 0.0f;
        S.VelocityLifetime[1] = 0.0f;
        S.VelocityLifetime[2] = OffsetZ;
        S.VelocityLifetime[3] = 99.0f;
        S.Attributes[0]       = (3.0f / static_cast<float>(Cols)) * 0.65f * ScaleMul;
        S.Attributes[1]       = SpinAngle;
        S.PhaseTiming[0]      = Inf;
    }
}

} // namespace Frontier::ParticleSpace
