/*============================================================================================================================================
                                                    DIAGNOSTICINFOGRAPHICCANVAS.H
============================================================================================================================================*/
// 📊 Reusable overview telemetry & archetype infographic canvas (17 particle/force archetypes + interactive Optical Bokeh Aperture selector)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlAppearance.h"

#include <cstdint>
#include <string_view>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class InfographicArchetype : uint8_t
{
    OpticalBokehAperture = 0u,  // [-] - 4-card interactive aperture selector (Mixed / Sharp / Creamy / Rim Ring)
    BallisticArc         = 1u,  // [-] - Gravity + floor bounce trajectory diagram
    CurlManifold         = 2u,  // [-] - Divergence-free curl streamline ribbons
    StrandVerlet         = 3u,  // [-] - 20-vertex catenary/cloth constraint net
    BotanicalSway        = 4u,  // [-] - Travelling wind phase wave across blades
    SpiderwebWeave       = 5u,  // [-] - Radial + logarithmic spiral orb weave
    FerrofluidCrown      = 6u,  // [-] - Rosensweig magnetic spike meniscus
    VoronoiFracture      = 7u,  // [-] - Cracked slab propagation & fissure vectors
    SplashCrown          = 8u,  // [-] - Concentric puddle ripple rings & droplet arc
    MoGraphEffector      = 9u,  // [-] - Height-extruded lattice modulated by spherical falloff
    FlockMurmuration     = 10u, // [-] - Separation / alignment / cohesion boids neighbourhood
    MolecularLattice     = 11u, // [-] - Lennard-Jones 12-6 potential & bond graph
    DerezTransition      = 12u, // [-] - Progressive sweep plane dissolving voxel/cube lattice
    ShieldBarrier        = 13u, // [-] - Geodesic dome + expanding impact ripple ring
    PyroBouquet          = 14u, // [-] - Multi-stage radial shell burst trajectories
    GravitationalLens    = 15u, // [-] - Relativistic Einstein ring deflection rays
    ForceFieldQuiver     = 16u, // [-] - Bounded directional/vortex vector field arrows
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DiagnosticInfographicSpec
{
    InfographicArchetype Archetype          = InfographicArchetype::OpticalBokehAperture;
    double               AnimationSeconds   = 0.0;   // [s]   - Live phase clock
    uint32_t             ActiveApertureCode = 0u;    // [0-3] - Selected optical bokeh aperture card (0..3)
    float                PrimaryScalar      = 1.0f;  // [-]   - Scale / intensity badge value
    std::string_view     CaptionText        {};      // [-]   - Footer guidance caption
};

struct DiagnosticInfographicReading
{
    uint32_t ChosenApertureCode = 0u;    // [0-3] - Updated aperture card when clicked
    bool     Modified           = false; // [-]   - True if user clicked an interactive card
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace DiagnosticInfographicCanvas
{

[[nodiscard]] DiagnosticInfographicReading Construct(
    PixelSpace&                      Surface,
    Rectangle2D                      Bounds,
    const DiagnosticInfographicSpec& Spec,
    const ControlPointer&            Pointer,
    float                            Opacity = 1.0f) noexcept;

} // namespace DiagnosticInfographicCanvas

} // namespace Frontier
