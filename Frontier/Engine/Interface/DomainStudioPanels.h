/*==========================================================================================================================================
                                                        DOMAIN STUDIO PANELS
                                     ------------------------------------------------------------------
             Tier-3 Dedicated Domain Studios: Fracture Voronoi Shatter, Fluid/Plume Eulerian Lattice, and Particle Emitter.
==========================================================================================================================================*/

// 🧩 Tier-3 Dedicated Domain Studios: Fracture Voronoi Shatter, Fluid/Plume Eulerian Lattice, and Particle Emitter.

#pragma once

#include "Engine/Rendering/PlumeLatticeFootprint.h"

#include <cstdint>

namespace Frontier
{

class PanelDockspace;

enum class DomainStudioCategory : uint8_t
{
    FractureShatter = 0,
    FluidPlume      = 1,
    ParticleEmitter = 2
};

struct FractureStudioSettings
{
    int32_t  VoronoiSiteCount      = 24;      // [cells] - [4..64]
    double   ClusterRadialBias     = 0.42;    // [-] - [0..1] impact-epicentre clustering
    double   ImpactEpicentreU      = 0.52;    // [UV]
    double   ImpactEpicentreV      = 0.46;    // [UV]
    double   ImpulseThresholdKn    = 18.5;    // [kN] - [1..120]
    double   InternalGlueStrength  = 0.65;    // [-] - [0..1]
    double   DebrisLifetimeSeconds = 12.0;    // [s] - [1..60]
    uint32_t PatternSeed           = 104729u;
    bool     AutoSleepFragments    = true;
};

struct FluidPlumeStudioSettings
{
    int32_t LatticeExtentX          = 96;     // [voxels]
    int32_t LatticeExtentY          = 96;     // [voxels]
    int32_t LatticeExtentZ          = 128;    // [voxels]
    double  VoxelSizeMetres         = 0.08;   // [m]
    double  BuoyancyLift            = 2.40;   // [m/s^2]
    double  VorticityConfinement    = 0.58;   // [-]
    double  JacobiPressureSweeps    = 28.0;   // [sweeps] - GasPressureProjection.slang
    double  CombustionKelvin        = 1350.0; // [K]
    double  SmokeExtinction         = 1.25;   // [1/m]
    bool    CoupleSharedWind        = true;
};

struct ParticleStudioSettings
{
    double SpawnRateThousandsPerSec = 14.5;   // [k/s] - [0.5..100]
    double InitialSpeedMps          = 8.4;    // [m/s] - [0.5..35]
    double ConeSpreadDegrees        = 22.0;   // [deg] - [1..90]
    double AerodynamicDrag          = 0.28;   // [1/s] - [0..2]
    double SurfaceRestitution       = 0.36;   // [-] - [0..1]
    double ParticleLifetimeSeconds  = 2.8;    // [s] - [0.2..12]
    bool   CollideWithDepthSdf      = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  DOMAIN STUDIO PANEL
//------------------------------------------------------------------------------------------------------------------------

class DomainStudioPanel
{
  public:
    void RegisterWithDockspace(PanelDockspace& Dockspace);
    void ConstructInterface();

    void ActivateStudio(DomainStudioCategory Category) noexcept
    {
        ActiveCategory = Category;
        FocusRequested = true;
    }

    [[nodiscard]] DomainStudioCategory QueryActiveCategory() const noexcept { return ActiveCategory; }
    [[nodiscard]] bool                 ConsumeFocusRequest() noexcept
    {
        const bool Was = FocusRequested;
        FocusRequested = false;
        return Was;
    }

    [[nodiscard]] PlumeLatticeFootprint BuildValidatedPlumeFootprint(bool& OutValid) const noexcept;

    FractureStudioSettings   Fracture{};
    FluidPlumeStudioSettings FluidPlume{};
    ParticleStudioSettings   Particle{};

  private:
    DomainStudioCategory ActiveCategory = DomainStudioCategory::FractureShatter;
    bool                 FocusRequested = false;
};

} // namespace Frontier
