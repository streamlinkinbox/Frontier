//============================================================================================================================================
//                                                  DISTANCEFIELDINTEGRATOR.H
//============================================================================================================================================
// 📦 Distance Field Global Illumination integrator with Surface Cache sampling, soft shadows, and ReSTIR GI resampling.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)                              // Disable structure padding alignment warning under /WX
#endif

#include "../GeometricRaster/GlobalDistanceFieldSpace.h"
#include "../GeometricRaster/SurfaceCacheStructure.h"
#include <vector>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           RESERVOIR SPATIO-TEMPORAL SAMPLE
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) DistanceFieldReservoir
{
    Vector3                 SampleRadiance;                     // [W/m²·sr] indirect radiance sample
    Vector3                 SampleDirection;                    // [-] incident direction vector
    float                   WeightSum         = 0.0f;           // [-] running candidate weight accumulator
    float                   SampleCountM      = 0.0f;           // [-] count of candidate samples considered
    float                   UnbiasedWeightW   = 0.0f;           // [-] unbiased contribution multiplier
    float                   TargetProbability = 0.0f;           // [-] target distribution evaluation p_hat
};

//------------------------------------------------------------------------------------------------------------------------
//                                           INTEGRATOR QUALITY CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

enum class DistanceFieldVisualizationMode : uint32_t
{
    FullGlobalIllumination    = 0u,                             // [-] direct sun + soft shadows + surface cache indirect GI
    IndirectIlluminationOnly  = 1u,                             // [-] secondary indirect diffuse GI terms only
    SoftShadowsOnly           = 2u,                             // [-] distance field soft shadow penumbra factor only
    SurfaceCacheAtlasView     = 3u,                             // [-] 2D parameterised surface cache radiance atlas
    DistanceVolumeSlices      = 4u,                             // [-] signed distance field iso-surface and distance slice
    SurfaceNormalGradient     = 5u                              // [-] numerical gradient surface normals
};

struct DistanceFieldIntegratorSettings
{
    uint32_t                MaxRayMarchSteps             = 96u;    // [-] maximum sphere-tracing iterations
    float                   StepRelaxationFactor         = 0.85f;  // [-] step under-relaxation multiplier
    bool                    EnableGlobalDistanceField    = true;   // [-] use two-tier GDF scene acceleration
    bool                    EnableSurfaceCache           = true;   // [-] sample precomputed surface cache for bounces
    bool                    SurfaceCacheMultiBounce      = true;   // [-] propagate multi-bounce irradiance in surface cache
    float                   SoftShadowLightAngleDegrees  = 10.0f;  // [deg] angular diameter of sun illuminant
    uint32_t                SoftShadowSteps              = 24u;    // [-] shadow ray march maximum steps
    uint32_t                GlobalIlluminationRayCount   = 1u;     // [-] indirect rays per pixel (1..4)
    bool                    EnableReSTIR                 = true;   // [-] enable spatiotemporal reservoir resampling
    uint32_t                ReSTIRSpatialRadius          = 3u;     // [px] spatial neighbour reuse pixel radius
    float                   ReSTIRHistoryCeiling         = 24.0f;  // [-] temporal sample count M clamp
    DistanceFieldVisualizationMode VisualizationMode     = DistanceFieldVisualizationMode::FullGlobalIllumination;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             DISTANCE FIELD INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class DistanceFieldIntegrator
{
public:
    DistanceFieldIntegrator() noexcept = default;

    DistanceFieldIntegrator(uint32_t InViewportWidth, uint32_t InViewportHeight) noexcept;

    // Resizes viewport buffers and reservoir history
    void Resize(uint32_t InViewportWidth, uint32_t InViewportHeight) noexcept;

    // Evaluates distance field GI for the current frame
    void ExecuteFrame(const GlobalDistanceFieldSpace& DistanceField,
                      const SurfaceCacheStructure& SurfaceCache,
                      Vector3 CameraPosition,
                      Vector3 CameraTarget,
                      Vector3 SunDirection,
                      Vector3 SunRadiance,
                      const DistanceFieldIntegratorSettings& Settings,
                      std::vector<uint8_t>& OutRgbImage) noexcept;

    // Merges temporal historical reservoir into current reservoir
    static void UpdateReservoir(DistanceFieldReservoir& Receiver,
                                Vector3 CandidateRadiance,
                                Vector3 CandidateDirection,
                                float CandidateWeight,
                                float TargetP) noexcept;

    // Combines two reservoirs
    static void MergeReservoirs(DistanceFieldReservoir& Receiver,
                                const DistanceFieldReservoir& Source,
                                float TargetP) noexcept;

private:
    uint32_t                ViewportWidth  = 320u;              // [px] presentation width
    uint32_t                ViewportHeight = 240u;              // [px] presentation height
    std::vector<DistanceFieldReservoir> TemporalReservoirBuffer; // [-] historical reservoirs per pixel
    std::vector<DistanceFieldReservoir> CurrentReservoirBuffer;  // [-] frame initial reservoirs per pixel
    std::vector<float>      DepthBuffer;                        // [m] primary ray hit distances
    std::vector<Vector3>    NormalBuffer;                       // [-] primary ray hit normals
};

} // namespace Frontier
