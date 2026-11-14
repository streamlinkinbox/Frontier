/*============================================================================================================================================
                                                           SPIDERWEBWEAVE.CPP
============================================================================================================================================*/
// 🧩 Real-time orb-weave spinneret extrusion, catenary wind sag & ball deflection on SpiderwebSilkLattice

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/SpiderwebWeave.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

SpiderwebSilkLattice InitializeSpiderwebLattice(const SpiderwebOriginSpec& OriginSpec)
{
    SpiderwebSilkLattice Out{};
    Out.Bonds             = BuildSpiderwebAnchors(OriginSpec, Out.RestPositions);
    Out.LivePositions     = Out.RestPositions;
    Out.ExtrusionProgress = (OriginSpec.WebMode == 2u) ? 1.0f : 0.05f;
    return Out;
}

void StepSpiderwebWeave(
    SpiderwebSilkLattice&              WebLattice,
    const SpiderwebWeaveSpec&          WeaveSpec,
    const InteractiveColliderSettings& Collider,
    Mathematics::Axis3D                AmbientWindMps,
    float                              DeltaSeconds,
    float                              ElapsedSeconds) noexcept
{
    if (WeaveSpec.WebMode == 0u || WebLattice.RestPositions.empty())
    {
        return;
    }

    if (WeaveSpec.WebMode == 1u)
    {
        WebLattice.ExtrusionProgress = std::min(1.0f, WebLattice.ExtrusionProgress + DeltaSeconds * 0.25f * std::max(0.1f, WeaveSpec.SpinRate));
    }
    else
    {
        WebLattice.ExtrusionProgress = 1.0f;
    }

    const std::size_t Count = WebLattice.RestPositions.size();
    WebLattice.LivePositions.resize(Count);
    const float WindMag = std::sqrt(AmbientWindMps.X * AmbientWindMps.X + AmbientWindMps.Y * AmbientWindMps.Y);

    for (std::size_t I = 0u; I < Count; ++I)
    {
        const Mathematics::Axis3D& Rest = WebLattice.RestPositions[I];
        const float RadialRatio = std::min(1.0f, static_cast<float>(I) / static_cast<float>(std::max<std::size_t>(1u, Count)));
        const float CenterFlex  = std::sin(RadialRatio * 3.14159f);
        const float Flutter     = std::sin(ElapsedSeconds * 3.4f + static_cast<float>(I) * 0.23f) * (0.04f + 0.025f * WindMag);

        Mathematics::Axis3D P = Rest;
        P.X += AmbientWindMps.X * 0.06f * CenterFlex + Flutter;
        P.Y += AmbientWindMps.Y * 0.06f * CenterFlex;
        P.Z -= WeaveSpec.SagMeters * CenterFlex;

        if (Collider.BallRadiusMeters > 0.01f)
        {
            const float Dx = P.X - Collider.BallPositionMeters.X;
            const float Dy = P.Y - Collider.BallPositionMeters.Y;
            const float Dz = P.Z - Collider.BallPositionMeters.Z;
            const float MinR = Collider.BallRadiusMeters + 0.05f;
            const float D2 = Dx * Dx + Dy * Dy + Dz * Dz;
            if (D2 < MinR * MinR)
            {
                const float D = std::sqrt(std::max(1.0e-6f, D2));
                P.X = Collider.BallPositionMeters.X + (Dx / D) * MinR;
                P.Y = Collider.BallPositionMeters.Y + (Dy / D) * MinR;
                P.Z = Collider.BallPositionMeters.Z + (Dz / D) * MinR;
            }
        }
        WebLattice.LivePositions[I] = P;
    }
}

} // namespace Frontier::ParticleSpace
