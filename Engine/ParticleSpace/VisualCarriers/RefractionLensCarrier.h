/*============================================================================================================================================
                                                        REFRACTIONLENSCARRIER.H
============================================================================================================================================*/
// 🧩 Screen-space refractive distortion carrier for relativistic gravitational lensing (black hole) & rising thermal heat shimmer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class RefractionLensCategory : uint8_t
{
    GravitationalAccretion = 0u, // [-] - Relativistic Schwarzschild deflection ring & photon sphere
    ThermalHeatShimmer     = 1u, // [-] - Rising convective refractive index turbulence
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct RefractionLensCarrier
{
    RefractionLensCategory LensCategory        = RefractionLensCategory::GravitationalAccretion;
    float                  DistortionStrength  = 1.0f;  // [-] - Deflection amplitude
    float                  EventHorizonMeters  = 0.85f; // [m] - Characteristic gravitational/plume radius
    float                  AccretionSwirlSpeed = 2.4f;  // [rad/s]
};

} // namespace Frontier::ParticleSpace
