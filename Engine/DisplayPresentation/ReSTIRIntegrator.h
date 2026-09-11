//============================================================================================================================================
//                                                      RESTIRINTEGRATOR.H
//============================================================================================================================================
// 🧩 Drives the interim progressive path-tracing kernel (RIS direct lighting + one NEE bounce, running-mean accumulation).
//    🚧 Not yet ReSTIR proper — see the status block at the top of Engine/Shaders/ReSTIRViewport.slang and plan v2.1.

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)
#endif

#include "../DeviceExchange/SwapchainExchange.h"
#include "../ContentInterchange/MaterialDescriptor.h"
#include "ExposureIntegrator.h"
#include "../../Projects/Project-Zero/Source/RayTracingSolver.h"
#include "../../Projects/Project-Zero/Source/FlyThroughSolver.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           RESTIR INTEGRATOR CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct ReSTIRIntegratorConfiguration
{
    uint32_t    CandidatesPerPixel;         // [-]   primary DI candidates per pixel
    uint32_t    ExtraCandidateCount;      // [-]   extra same-pixel RIS candidates (R6 row 3: renamed)
    uint32_t    SpatialTapCount    = 4;   // [-]   R10: spatial-reuse neighbours per pixel, tier-keyed (0 = cross off).
                                          //       Defaults to the pre-R10 hardcoded 4, so a caller that never assigns
                                          //       it renders exactly as before rather than silently losing the cross.
    uint32_t    DenoiseLevelCount = 5;    // [-]   R10 #8: a-trous levels dispatched, tier-keyed. Defaults to the
                                          //       pre-R10 fixed 5 for the same reason: never silently filter less.
    float       Exposure;                   // [-]   ACES tone-map exposure scalar
    float       AmbientStrength;            // [-]   ambient fallback contribution
    bool        GlobalIllumination = true;  // [-]   secondary bounce on/off
    bool        AntiAliasing       = true;  // [-]   sub-pixel jitter on/off
    bool        AmbientFloor       = false; // [-]   debug fill light (albedo × AmbientStrength); off by default since R0
    bool        TemporalReuse      = true;  // [-]   R6 row 2: temporal reservoir reuse (back-projection + validation)
    bool        SpatialReuse       = true;  // [-]   R6 row 3: spatial neighbour reuse (pairwise MIS)
    bool        AliasPick          = true;  // [-]   R6 row 3: Walker-alias light pick (false = uniform, R0 identity; F5)
    bool        Denoise            = true;  // [-]   R7: edge-avoiding à-trous filter (false = the raw accumulated image)
    bool        TemporalReprojection = true; // [-]   R7a: back-project the running mean through the motion vectors
                                             //       (false = the pre-R7a same-pixel accumulator, kept as an identity switch)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RESTIR INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class ReSTIRIntegrator
{
public:
    explicit ReSTIRIntegrator(ReSTIRIntegratorConfiguration InitialConfiguration) noexcept;
    ~ReSTIRIntegrator() noexcept = default;

    ReSTIRIntegrator(const ReSTIRIntegrator&)            = delete;
    ReSTIRIntegrator& operator=(const ReSTIRIntegrator&) = delete;

    // Construct the DispatchConfiguration from live camera state and scene counts
    [[nodiscard]] DispatchConfiguration
    BuildDispatch(const ProjectZero::FlyThroughSolver& Camera,
                  uint32_t                             ViewportWidth,
                  uint32_t                             ViewportHeight,
                  uint32_t                             AlphaMaskedMaterialCount,   // R4b: materials flagged MaterialFlagAlphaMask
                  uint32_t                             LuminaireTriangleCount) const noexcept;

    // Count emissive triangles in the scene (used to set LuminaireTriangleCount each frame)
    [[nodiscard]] static uint32_t
    CountLuminaireTriangles(const ProjectZero::RayTracingSolver& Scene) noexcept;

    // Build GPU triangle and material records from the CPU scene
    [[nodiscard]] static std::vector<TriangleIndex>
    BuildTriangleIndex(const ProjectZero::RayTracingSolver& Scene) noexcept;

    [[nodiscard]] static std::vector<MaterialDescriptor>
    BuildMaterialDescriptors(const ProjectZero::RayTracingSolver& Scene) noexcept;

    // Mutable configuration — updated live by RenderScheduler
    // Any parameter change invalidates the temporal history; the accumulation restarts at index 0.
    void AssignCandidatesPerPixel(uint32_t Count) noexcept { if (ActiveConfiguration.CandidatesPerPixel != Count) { ActiveConfiguration.CandidatesPerPixel = Count; ResetAccumulation(); } }
    void AssignExtraCandidateCount  (uint32_t Count) noexcept { if (ActiveConfiguration.ExtraCandidateCount   != Count) { ActiveConfiguration.ExtraCandidateCount   = Count; ResetAccumulation(); } }
    void AssignSpatialTapCount      (uint32_t Count) noexcept { if (ActiveConfiguration.SpatialTapCount       != Count) { ActiveConfiguration.SpatialTapCount       = Count; ResetAccumulation(); } }
    void AssignDenoiseLevelCount    (uint32_t Count) noexcept { if (ActiveConfiguration.DenoiseLevelCount     != Count) { ActiveConfiguration.DenoiseLevelCount     = Count; ResetAccumulation(); } }
    // A6b ⚠️ The slider writes BOTH the configuration and the exposure integrator's manual value. Keeping two
    //    copies and hoping they agree is exactly how a control ends up doing nothing in one mode.
    void AssignExposure          (float    Value) noexcept
    {
        if (ActiveConfiguration.Exposure != Value)
        {
            ActiveConfiguration.Exposure = Value;
            ExposureConfiguration Adapt = Adaptation.QueryConfiguration();
            Adapt.ManualExposure = Value;
            Adaptation.AssignConfiguration(Adapt);
            ResetAccumulation();
        }
    }
    void AssignGlobalIllumination(bool     On)    noexcept { if (ActiveConfiguration.GlobalIllumination  != On)    { ActiveConfiguration.GlobalIllumination  = On;    ResetAccumulation(); } }
    void AssignAntiAliasing      (bool     On)    noexcept { if (ActiveConfiguration.AntiAliasing        != On)    { ActiveConfiguration.AntiAliasing        = On;    ResetAccumulation(); } }
    void AssignTemporalReuse     (bool     On)    noexcept { if (ActiveConfiguration.TemporalReuse       != On)    { ActiveConfiguration.TemporalReuse       = On;    ResetAccumulation(); } }
    void AssignSpatialReuse      (bool     On)    noexcept { if (ActiveConfiguration.SpatialReuse        != On)    { ActiveConfiguration.SpatialReuse        = On;    ResetAccumulation(); } }
    void AssignAliasPick         (bool     On)    noexcept { if (ActiveConfiguration.AliasPick           != On)    { ActiveConfiguration.AliasPick           = On;    ResetAccumulation(); } }
    // R7. Toggling the filter does not change what is SAMPLED, only how the accumulated image is presented, so it
    //    deliberately does NOT reset accumulation — restarting would throw away a converged history to change a
    //    post-process, and the A/B comparison the switch exists for would be impossible.
    void AssignDenoise           (bool     On)    noexcept { ActiveConfiguration.Denoise = On; }

    // ⚠️ THE INCREMENT MUST NOT SWALLOW THE RESET. The frame loop reads the index for the dispatch,
    //    the §8 record comparisons reset it when the sky changes, and the loop unconditionally increments it
    //    after presenting. Without the flag, a reset-to-zero was incremented back to one before the next frame
    //    read it — every slider reset dispatched with FrameIndex ≥ 1, the kernel kept blending at 1/n, and
    //    panel edits only became visible when a camera move failed reprojection geometrically. The flag spends
    //    one increment, so the frame after a reset dispatches with FrameIndex 0 and starts genuinely fresh.
    void ResetAccumulation() noexcept { AccumulationIndex = 0u; ResetPending = true; }

    // A6b. Adaptive exposure. Held here because BuildDispatch is what fills the Exposure push constant, so the
    //    measured value and the value the shader receives cannot drift apart.
    [[nodiscard]] ExposureIntegrator&       Exposure()       noexcept { return Adaptation; }
    [[nodiscard]] const ExposureIntegrator& Exposure() const noexcept { return Adaptation; }

    // Compares the camera pose against the one used for the running history; a moved or turned camera
    //    (or a resized viewport) restarts accumulation so no stale radiance is blended in.
    void ObserveCamera(const ProjectZero::FlyThroughSolver& Camera, uint32_t ViewportWidth, uint32_t ViewportHeight) noexcept;

    [[nodiscard]] const ReSTIRIntegratorConfiguration& QueryConfiguration() const noexcept
    {
        return ActiveConfiguration;
    }

    void IncrementAccumulationIndex() noexcept { if (ResetPending) ResetPending = false; else AccumulationIndex++; }
    [[nodiscard]] uint32_t QueryAccumulationIndex() const noexcept { return AccumulationIndex; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    ReSTIRIntegratorConfiguration ActiveConfiguration;  // [-]  live-tunable parameters
    ExposureIntegrator Adaptation{};  // A6b: adaptive exposure
    uint32_t                      AccumulationIndex;    // [-]  temporal frame counter (incremented per frame)
    bool                          ResetPending = false; // [-]  a reset landed after the dispatch read the index

    Vector3                       HistoryOrigin;        // [m]   camera position the history was accumulated from
    Vector3                       HistoryForward;       // [-]   camera forward the history was accumulated from
    uint32_t                      HistoryWidth;         // [px]  viewport width of the history
    uint32_t                      HistoryHeight;        // [px]  viewport height of the history
};

template<>
inline uint32_t ReSTIRIntegrator::Convert<uint32_t>() const noexcept
{
    return AccumulationIndex;
}

template<>
inline float ReSTIRIntegrator::Convert<float>() const noexcept
{
    return ActiveConfiguration.Exposure;
}

} // namespace Frontier
