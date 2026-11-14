/*============================================================================================================================================
                                                         SHALLOWWATERSPLASH.CPP
============================================================================================================================================*/
// 🧩 Figure-8 rally vehicle tyre trajectory, puddle ripple rings & heavy mud splatter solver

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/ShallowWaterSplash.h"

#include "Engine/ColourSpace/ColourRampSampler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

RallyVehiclePose EvaluateRallyVehiclePose(
    const ShallowWaterSplashSpec& Spec,
    float                         ElapsedSeconds) noexcept
{
    const float R     = std::max(0.5f, Spec.TrackRadiusMeters);
    const float Omega = Spec.VehicleSpeed / R;
    const float T     = ElapsedSeconds * Omega;
    // Lissajous figure-8 in horizontal XY plane (+Z Up)
    const float X  = R * std::sin(T);
    const float Y  = 0.55f * R * std::sin(2.0f * T);
    const float Vx = R * Omega * std::cos(T);
    const float Vy = 1.10f * R * Omega * std::cos(2.0f * T);
    const float Spd = std::sqrt(std::max(1.0e-5f, Vx * Vx + Vy * Vy));
    return {
        { X, Y, 0.32f },
        { Vx / Spd, Vy / Spd, 0.0f },
        Spd,
    };
}

void StepShallowWaterSplash(
    std::span<ParticleSlot>       Slots,
    const ShallowWaterSplashSpec& Spec,
    float                         DeltaSeconds,
    float                         ElapsedSeconds) noexcept
{
    if (Spec.SplashMode == 0u || Slots.empty() || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const RallyVehiclePose Car = EvaluateRallyVehiclePose(Spec, ElapsedSeconds);
    for (std::size_t I = 0u; I < Slots.size(); ++I)
    {
        ParticleSlot& S = Slots[I];
        S.PositionAge[3] += DeltaSeconds;
        if (S.PositionAge[3] >= S.VelocityLifetime[3])
        {
            const float SeedA = ColourSpace::HashDeterministicFloat(static_cast<float>(I), static_cast<uint32_t>(ElapsedSeconds * 60.0f) + 7u);
            const float SeedB = ColourSpace::HashDeterministicFloat(static_cast<float>(I), static_cast<uint32_t>(ElapsedSeconds * 60.0f) + 19u);
            if (Spec.SplashMode == 2u || Spec.SplashMode == 3u)
            {
                const float SideSign = (I % 2u == 0u) ? 1.0f : -1.0f;
                const float RightX =  Car.ForwardAxis.Y;
                const float RightY = -Car.ForwardAxis.X;
                S.PositionAge[0]      = Car.PositionMeters.X - Car.ForwardAxis.X * 0.65f + RightX * (0.48f * SideSign);
                S.PositionAge[1]      = Car.PositionMeters.Y - Car.ForwardAxis.Y * 0.65f + RightY * (0.48f * SideSign);
                S.PositionAge[2]      = 0.06f;
                S.PositionAge[3]      = 0.0f;
                S.VelocityLifetime[0] = -Car.ForwardAxis.X * (1.8f + 2.4f * SeedA) + RightX * (SideSign * 1.4f * SeedB);
                S.VelocityLifetime[1] = -Car.ForwardAxis.Y * (1.8f + 2.4f * SeedA) + RightY * (SideSign * 1.4f * SeedB);
                S.VelocityLifetime[2] = 1.8f + 2.8f * SeedB;
                S.VelocityLifetime[3] = 0.65f + 0.55f * SeedA;
            }
            else
            {
                const float Ang = SeedA * 6.283185f;
                S.PositionAge[0]      = std::cos(Ang) * 0.25f;
                S.PositionAge[1]      = std::sin(Ang) * 0.25f;
                S.PositionAge[2]      = 0.04f;
                S.PositionAge[3]      = 0.0f;
                S.VelocityLifetime[0] = std::cos(Ang) * (1.2f + 1.6f * SeedB);
                S.VelocityLifetime[1] = std::sin(Ang) * (1.2f + 1.6f * SeedB);
                S.VelocityLifetime[2] = 1.6f + 1.5f * SeedA;
                S.VelocityLifetime[3] = 0.75f + 0.5f * SeedB;
            }
            continue;
        }

        S.VelocityLifetime[2] -= 9.81f * DeltaSeconds;
        S.PositionAge[0]      += S.VelocityLifetime[0] * DeltaSeconds;
        S.PositionAge[1]      += S.VelocityLifetime[1] * DeltaSeconds;
        S.PositionAge[2]      += S.VelocityLifetime[2] * DeltaSeconds;
        if (S.PositionAge[2] < 0.02f)
        {
            S.PositionAge[2]      = 0.02f;
            S.VelocityLifetime[2] = 0.0f;
        }
    }
}

} // namespace Frontier::ParticleSpace
