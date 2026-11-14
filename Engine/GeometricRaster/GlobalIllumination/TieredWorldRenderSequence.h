/*==========================================================================================================================================
                                                   TIEREDWORLDRENDERSEQUENCE.H
==========================================================================================================================================*/
// 🧩 Complete Ordered GTX Tier 1 + RTX Tier 2 World Render Sequence bound to ViewportProjectionPane (Visibility -> Direct -> GI -> Specular)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/GlobalIllumination/IrradianceLatticeSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/SpecularReflectionSequence.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/MaterialSurfaceLayer/BounceLayerSummary.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelBank.h"
#include "Engine/SimulationSpace/Particles/ParticleChannelLayouts.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class WorldRenderTier : uint8_t
{
    Tier1GtxCalibrated    = 0u, // [-] - Shared 64-bit Vis + 3.9 mm Contact Clip/SDF Sun + 6-Cone SDF GI + Hi-Z SSR & Glass Peel
    Tier2RtxDeterministic = 1u, // [-] - Shared 64-bit Vis + Vogel Solar BVH4 + 1/pi Octahedral Chebyshev GI + VNDF RT Specular/Glass
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct WorldSceneSpecification
{
    std::vector<RigidClusterInstance>            ClusterInstances {};
    MaterialSpace::CompactMaterialRegister       MaterialRegister {};
    std::vector<MaterialSpace::BounceSlabEntry>  BounceSummaries  {};
    SpatialAcceleration::WideBoundingTree        SceneTree        {};
    DistanceFieldSpace::SignedDistanceVolume     SceneSdf         {};
    SolarDirectionalEmitter                                         Sun                    {};
    std::vector<PolygonalAreaQuadLight>                             AreaLights             {};
    IrradianceLatticeConfiguration                                  LatticeConfig          {};
    Mathematics::LinearRGB                                          SkyRadiance            { 0.15f, 0.21f, 0.32f };
    const SimulationSpace::Particles::ParticleChannelBank*          ParticleBank           = nullptr;
    bool                                                            ParticleAdditiveBlend  = false;
    float                                                           ParticleEmissiveGain   = 1.65f;
    float                                                           ParticleDepthFadeSpanM = 0.16f;
};

struct WorldRenderFrameResult
{
    uint32_t                            WidthPx             = 0u;   // [px]    - Viewport width
    uint32_t                            HeightPx            = 0u;   // [px]    - Viewport height
    WorldRenderTier                     ActiveTier          = WorldRenderTier::Tier2RtxDeterministic;
    std::vector<Mathematics::LinearRGB> CompositeLinearHdr  {};     // [cd/m2] - Final Direct + Diffuse GI + Specular + Glass + Particles HDR
    std::vector<Mathematics::LinearRGB> IndirectDiffuseHdr  {};     // [cd/m2] - Isolated diffuse global illumination buffer
    std::vector<float>                  PrimaryDepthMeters  {};     // [m]     - Per-pixel view-space opaque hit depth (1e6 = sky)
    std::vector<uint8_t>                DisplayRgba8        {};     // [rgba8] - ACES filmic + sRGB8 viewport output (ready for ImGui/Pane!)
    TwoPhaseCullReport                  VisibilityCull      {};     // [-]     - Cluster culling telemetry
    DirectLightingFrameOutput           DirectLighting      {};     // [-]     - Solar shadow, LTC, & chassis AO telemetry
    SpecularReflectionBuffer            SpecularAndGlass    {};     // [-]     - Reflection & glass refraction telemetry
    uint32_t                            CompositedParticles = 0u;   // [cnt]   - Active particles depth-composited into world frame
};

//------------------------------------------------------------------------------------------------------------------------
//                                            TIERED WORLD RENDER SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class TieredWorldRenderSequence
{
public:
    // Precomputes static/cached scene structures (BounceSlabEntries, 2-Cascade Contact Clips, & RTX Octahedral Irradiance Lattice)
    void PrepareWorldScene(WorldSceneSpecification& InOutScene);

    // Executes the complete ordered world render frame for either Tier 1 GTX or Tier 2 RTX
    [[nodiscard]] WorldRenderFrameResult RenderWorldFrame(
        WorldRenderTier                Tier,
        const PerspectiveCameraFrame&  CurrentCamera,
        const PerspectiveCameraFrame&  PreviousCamera,
        const WorldSceneSpecification& PreparedScene);

    // Depth-composites a particle channel bank into a rendered linear HDR world frame & re-runs ACES Filmic sRGB8 resolve
    static uint32_t CompositeParticleBankIntoFrame(
        const SimulationSpace::Particles::ParticleChannelBank& Bank,
        const PerspectiveCameraFrame&                          Camera,
        bool                                                   AdditiveBlend,
        float                                                  EmissiveGain,
        float                                                  DepthFadeSpanMeters,
        WorldRenderFrameResult&                                InOutFrame);

    [[nodiscard]] const IrradianceLatticeSequence& InspectIrradianceLattice() const noexcept { return RtxLattice; }

private:
    OrthographicShadowCascade FineContactClip   {};
    OrthographicShadowCascade CoarseContactClip {};
    IrradianceLatticeSequence RtxLattice        {};
    DepthPyramidSequence      PreviousHiZ       {};
};

} // namespace Frontier::GeometricRaster
