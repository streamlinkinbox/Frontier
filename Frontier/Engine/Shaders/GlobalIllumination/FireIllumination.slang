//============================================================================================================================================
//                                                         FIREILLUMINATION.SLANG
//============================================================================================================================================
// 🧩 A burning tyre as a point light on the scene: inverse-square, windowed to its reach, shadowed through the scene distance field.

#pragma once
#include "IntrinsicParity.slang"

// 📝 Read by the direct dispatch (screen) and the card direct dispatch (the surface store), so a tyre fire lights the course
//    and the car directly and, through the cards the final gather reads, bounces into the indirect term as well.
//    The shadow ray stops short of the light: the light sits inside the burning wheel, which must not shadow itself.

#include "RefreshDescriptor.slang"
#include "DistanceMarch.slang"

struct FireArrival
{
    float3 Direction;      // [-]  toward the fire
    float3 Illuminance;    // [-]  arriving at normal incidence, shadowed and windowed
};

FireArrival ResolveFireArrival(float3 Position, float3 Normal)
{
    var Arrival : FireArrival = {};
    Arrival.Direction = float3(0.0, 0.0, 1.0);
    Arrival.Illuminance = float3(0.0);
    if (Refresh.FireLightIntensity <= 0.0)
    {
        return Arrival;
    }
    let Toward = Refresh.FireLightPosition - Position;
    let Squared = dot(Toward, Toward);
    let Reach = Refresh.FireLightReach;
    if (Squared >= Reach * Reach)
    {
        return Arrival;
    }
    let Range = sqrt(Squared);
    Arrival.Direction = Toward / max(Range, 1.0e-4);
    if (dot(Normal, Arrival.Direction) <= 0.0)
    {
        return Arrival;
    }
    let Ratio = Squared / (Reach * Reach);
    let Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);
    var Visibility = 1.0;
    if (FeatureEnabled(Refresh, FeatureDirectShadow))
    {
        let Bias = ResolveSurfaceBias(Position);
        Visibility = TraverseSceneShadow(Position + Normal * Bias, Arrival.Direction, 0.5 * Bias, max(Range - 0.6, 0.0), 4.0);
    }
    Arrival.Illuminance = Refresh.FireLightColour * Refresh.FireLightIntensity * Window * Visibility / max(Squared, 0.25);
    return Arrival;
}
