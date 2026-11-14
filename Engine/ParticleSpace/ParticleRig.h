/*============================================================================================================================================
                                                               PARTICLERIG.H
============================================================================================================================================*/
// 🧩 Single modular particle rig composing ReleaseOriginSpec, VisualCarrierSpec, ParticleTintBinding & SimulationDynamicsProfile

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/SimulationDynamicsTypes.h"
#include "Engine/ParticleSpace/Origins/FireworkPatternSampler.h"
#include "Engine/ParticleSpace/Origins/ReleaseOriginTypes.h"
#include "Engine/ParticleSpace/Origins/SurfaceAnchorSampler.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleTintDriver.h"
#include "Engine/ParticleSpace/VisualCarriers/VisualCarrierTypes.h"

#include <cstdint>
#include <string>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ParticleTintBinding
{
    ColourSpace::ColourSourceProfile SourceProfile {};                                        // [-] - Modular colour source + range + blend
    ParticleScalarChannel            ScalarChannel = ParticleScalarChannel::NormalizedLifetime;// [-] - Particle scalar channel driver (0..14)
};

struct ParticleSizingSpec
{
    uint32_t CapacitySlots     = 4096u; // [count] - Allocated ParticleSlot budget for this rig
    float    ReleaseRatePerSec = 400.0f;// [1/s]   - Continuous particle spawn cadence
    float    LifetimeMinSec    = 1.2f;  // [s]     - Minimum particle lifespan
    float    LifetimeMaxSec    = 2.6f;  // [s]     - Maximum particle lifespan
    float    SizeStartMeters   = 0.05f; // [m]     - Initial sprite / solid scale
    float    SizeEndMeters     = 0.01f; // [m]     - Terminal sprite / solid scale
    float    OpacityPeak       = 1.0f;  // [0-1]   - Peak opacity multiplier
    float    AngularSpinRate   = 0.0f;  // [rad/s] - Intrinsic particle spin speed
    float    TurbulenceAmp     = 0.35f; // [m/s^2] - Per-particle stochastic swirl amplitude
    float    TurbulenceFreq    = 1.1f;  // [1/m]   - Per-particle turbulence spatial frequency
    bool     SizeByVelocity    = false; // [-]     - Stretch length proportional to speed
};

struct ParticleRig
{
    uint32_t                     RigId            = 1u;                                  // [id]    - Unique stable rig id
    std::string                  RigTitle         { "Default Particle Rig" };            // [-]     - Human-readable label
    std::string                  PresetOriginId   {};                                    // [-]     - Optional source preset identifier
    bool                         Enabled          = true;                                // [-]     - Active simulation & render toggle
    bool                         Solo             = false;                               // [-]     - Solo isolate toggle
    bool                         Quarantined      = false;                               // [-]     - Matches HTML QUARANTINE_IDS status

    ParticleSizingSpec           Sizing           {};                                    // [-]     - Budget, rate, lifespan, scale, spin
    ReleaseOriginSpec            Origin           { PointOriginSpec{} };                 // [-]     - Modular release origin (12 variants)
    VisualCarrierSpec            Carrier          { SpriteBillboardCarrier{} };          // [-]     - Modular visual carrier (7 variants)
    ParticleTintBinding          TintBinding      {};                                    // [-]     - ColourSource + 15-channel scalar driver
    SimulationDynamicsProfile    Dynamics         {};                                    // [-]     - Composable physics solver descriptors
    BackingSurfaceCarrier        BackingSurface   {};                                    // [-]     - Surface finish & procedural/custom geometry

    SurfaceAnchorSet             SampledAnchors   {};                                    // [-]     - Cached surface anchors & triangles
    FireworkShellSample          FireworkTargets  {};                                    // [-]     - Cached firework burst stars
    ColourSpace::ColourRampLut64 BakedColourLut   {};                                    // [-]     - Pre-baked 64-entry GPU LUT matching TintBinding

    void RebuildBakedColourLut() noexcept;
    void RebuildOriginCaches(uint32_t Seed = 1337u);

    [[nodiscard]] RigParticleUniform PackGpuUniform(uint32_t SlotOffset, uint32_t SlotCount) const noexcept;
};

} // namespace Frontier::ParticleSpace
