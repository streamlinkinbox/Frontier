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
#include "../GeometricRaster/CelestialSolver.h"
#include "DaylightSolver.h"
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

// A3 sky quality. The view march dominates the cost — the light march runs inside it, so the product is what
//    you pay — and the tiers spend their budget accordingly. Measured on the reference CPU port: Low differs
//    from Ultra by under 2 % in the zenith and under 6 % at the horizon, where the path through the air is
//    longest and the march is least able to resolve it.
enum class SkyQualityCategory : uint32_t
{
    Off    = 0u,   // no sky at all: ray misses stay black, reproducing every pre-A3 image exactly
    Low    = 1u,   // 16 × 4   — a GTX 1650 SUPER at 1080p
    Medium = 2u,   // 32 × 8
    High   = 3u,   // 48 × 12
    Ultra  = 4u,   // 64 × 16  — the offline reference
};

struct SkyStepCounts { uint32_t View; uint32_t Light; };

// A2 ⚠️ A Light count of ZERO selects the tabulated path: the transmittance LUT replaces the inner march
//    entirely, so there are no light steps left to take. That is why every tier below reports zero — the tables
//    made the inner loop obsolete, and the tiers now only choose how finely the VIEW ray is sampled.
//
//    A non-zero Light count still runs the A3 per-pixel march. It is kept reachable so the two can be compared
//    on the same frame, which is how the tables were verified to be a speed-up rather than a different sky.
[[nodiscard]] inline SkyStepCounts QuerySkySteps(SkyQualityCategory Quality) noexcept
{
    switch (Quality)
    {
        case SkyQualityCategory::Low:    return { 16u, 0u };
        case SkyQualityCategory::Medium: return { 32u, 0u };
        case SkyQualityCategory::High:   return { 48u, 0u };
        case SkyQualityCategory::Ultra:  return { 64u, 0u };
        default:                         return {  0u, 0u };
    }
}

// The A3 march, for the comparison path only.
[[nodiscard]] inline SkyStepCounts QuerySkyStepsMarched(SkyQualityCategory Quality) noexcept
{
    switch (Quality)
    {
        case SkyQualityCategory::Low:    return { 16u,  4u };
        case SkyQualityCategory::Medium: return { 32u,  8u };
        case SkyQualityCategory::High:   return { 48u, 12u };
        case SkyQualityCategory::Ultra:  return { 64u, 16u };
        default:                         return {  0u,  0u };
    }
}

struct ReSTIRIntegratorConfiguration
{
    uint32_t    CandidatesPerPixel;         // [-]   primary DI candidates per pixel
    uint32_t    ExtraCandidateCount;      // [-]   extra same-pixel RIS candidates (R6 row 3: renamed; true spatial reuse is the fixed kSpatialTaps cross)
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

    // A3 sky. Off restores the black background exactly, which is the identity switch for this phase.
    SkyQualityCategory SkyQuality = SkyQualityCategory::Medium;
    float       SunIlluminance   = 120000.0f;   // [lx]  clear midday sun above the atmosphere
    float       CameraAltitude   = 2.0f;        // [m]   observer height above the planet surface
    // A5. The sky lights the scene: a bounce ray that escapes gathers sky radiance instead of returning nothing.
    //    Off restores the pre-A5 image exactly, which is the identity switch — and the A/B for how much of the
    //    room's light is actually coming through the oculus.
    bool        SkyLighting      = true;
    // A7. The moon disc and the star field. Off restores the pre-A7 image exactly.
    bool        NightSky         = true;
    float       StarBrightness   = 0.4f;        // [cd/m²] a dark-site sky; adaptive exposure is what reveals it
    // A7c. The moon's apparent size, as a multiple of its true 0.259°. An admitted artistic control — the real
    //    disc is about ten pixels at a 55° field of view, which is correct and smaller than anyone expects.
    float       MoonAngularScale = 1.0f;        // [-]

    // A7b. Turbidity — the aerosol load, and the only reason a sunrise can look different from a sunset.
    //    1.0 is the clear reference atmosphere the Mie constants describe; 2.5 is a hazy city afternoon.
    float       SkyTurbidity     = 1.0f;        // [-]   the day's MEAN aerosol load
    // How far the load swings either side of that mean across the day: cleanest at 06:00, dirtiest at 18:00.
    //    ⚠️ 0 is the identity switch and reproduces every pre-A7b image at every hour, because the curve is a
    //    cosine about the mean and the mean IS the reference atmosphere. Even at the default swing, noon and
    //    midnight land exactly on 1.0 — the curve crosses its mean there — so only the mornings and evenings
    //    differ, which is precisely the claim being made.
    float       TurbiditySwing   = 0.35f;       // [-]   dawn 0.65, dusk 1.35 at the defaults
};

//------------------------------------------------------------------------------------------------------------------------
//                                            A7b — THE DAY'S AEROSOL CURVE
//------------------------------------------------------------------------------------------------------------------------
// 🧩 Turbidity as a function of the time of day. This is the entire mechanism by which a sunrise stops looking
//    like a sunset, so it is worth being explicit about what it claims.
//
//    The scattering model itself is SYMMETRIC about the horizon: at equal sun elevation, morning and evening are
//    the same geometry and therefore the same picture. That is physically correct, and it is why no amount of
//    tuning the sky march would ever separate them. The real difference is in the air. Overnight the boundary
//    layer cools, convection stops, and dust and haze settle out; by late afternoon a day of surface heating has
//    stirred them back up. Dawn air is therefore CLEANER, and clean air means less Mie: the horizon glow is
//    paler, whiter and tighter, instead of the broad orange of an evening.
//
//    A cosine with its minimum at 06:00 and maximum at 18:00 — continuous, periodic, and with no discontinuity
//    at midnight. A piecewise curve would be easier to reason about and would step visibly at whatever hour the
//    pieces met, which on a fast clock reads as the sky flickering once per simulated day.
[[nodiscard]] inline float QueryDiurnalTurbidity(float Mean, float Swing, double SolarDayFraction) noexcept
{
    constexpr double kTwoPi = 6.283185307179586;
    const double Hour  = SolarDayFraction * 24.0;
    const double Phase = kTwoPi * (Hour - 18.0) / 24.0;
    const double Value = static_cast<double>(Mean) + static_cast<double>(Swing) * std::cos(Phase);
    // A floor rather than a raw value: turbidity is a multiplier on extinction, and zero or negative air is an
    //    atmosphere that amplifies light. The march would not merely look wrong, it would diverge.
    return static_cast<float>(Value < 0.05 ? 0.05 : Value);
}

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

    // A7. The frame's sky, for the uniform buffer the kernel reads. Separate from BuildDispatch because it goes
    //    to a different destination, but built from the same clock so the two always agree.
    [[nodiscard]] SkyRecord BuildSkyRecord() const noexcept;

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
    // Changing the sky changes what every ray-miss pixel resolves to, so the accumulated history is no longer
    //    of the same image and must be discarded — unlike the denoiser, which only re-presents it.
    void AssignSkyQuality  (SkyQualityCategory Q) noexcept { if (ActiveConfiguration.SkyQuality     != Q) { ActiveConfiguration.SkyQuality     = Q; ResetAccumulation(); } }
    void AssignSunIlluminance    (float    Lux)   noexcept { if (ActiveConfiguration.SunIlluminance != Lux) { ActiveConfiguration.SunIlluminance = Lux; ResetAccumulation(); } }
    void AssignSkyLighting       (bool     On)    noexcept { if (ActiveConfiguration.SkyLighting    != On)  { ActiveConfiguration.SkyLighting    = On;  ResetAccumulation(); } }
    void AssignNightSky          (bool     On)    noexcept { if (ActiveConfiguration.NightSky       != On)    { ActiveConfiguration.NightSky       = On;    ResetAccumulation(); } }
    void AssignStarBrightness    (float    Value) noexcept { if (ActiveConfiguration.StarBrightness != Value) { ActiveConfiguration.StarBrightness = Value; ResetAccumulation(); } }
    // A7b. Turbidity rebuilds the atmosphere tables as well as changing every sky pixel, so the accumulated
    //    history is of a different atmosphere and must go — same reasoning as the sky quality above.
    void AssignSkyTurbidity      (float    Value) noexcept { if (ActiveConfiguration.SkyTurbidity   != Value) { ActiveConfiguration.SkyTurbidity   = Value; ResetAccumulation(); } }
    void AssignMoonAngularScale  (float    Value) noexcept { if (ActiveConfiguration.MoonAngularScale != Value) { ActiveConfiguration.MoonAngularScale = Value; ResetAccumulation(); } }
    void AssignTurbiditySwing    (float    Value) noexcept { if (ActiveConfiguration.TurbiditySwing != Value) { ActiveConfiguration.TurbiditySwing = Value; ResetAccumulation(); } }

    void ResetAccumulation() noexcept { AccumulationIndex = 0u; }

    // A3. The sky's clock, held by value because it IS the authoritative state — handing out a pointer to
    //    someone else's would invite a second clock to exist and the two would eventually disagree.
    //
    // ⚠️ ONE clock, therefore ONE sun and ONE atmosphere. That is correct today: the camera can only be inside
    //    one world, so a second dome would be built and sampled while contributing to no pixel. Portals are the
    //    single thing that changes it — see CLAUDE.md §15b for where the assumption is baked in and what the
    //    change looks like.
    // A6b. Adaptive exposure. Held here because BuildDispatch is what fills the Exposure push constant, so the
    //    measured value and the value the shader receives cannot drift apart.
    [[nodiscard]] ExposureIntegrator&       Exposure()       noexcept { return Adaptation; }
    [[nodiscard]] const ExposureIntegrator& Exposure() const noexcept { return Adaptation; }

    [[nodiscard]] CelestialSolver&       Celestial()       noexcept { return Sky; }
    [[nodiscard]] const CelestialSolver& Celestial() const noexcept { return Sky; }

    // Compares the camera pose against the one used for the running history; a moved or turned camera
    //    (or a resized viewport) restarts accumulation so no stale radiance is blended in.
    // A7e. Tell the exposure how much light the sky is putting on the scene. Camera-independent, so the sky
    //    cannot change brightness when the camera moves — which no amount of metering the frame could achieve.
    void ObserveDaylight() noexcept;

    void ObserveCamera(const ProjectZero::FlyThroughSolver& Camera, uint32_t ViewportWidth, uint32_t ViewportHeight) noexcept;

    [[nodiscard]] const ReSTIRIntegratorConfiguration& QueryConfiguration() const noexcept
    {
        return ActiveConfiguration;
    }

    void IncrementAccumulationIndex() noexcept { AccumulationIndex++; }
    [[nodiscard]] uint32_t QueryAccumulationIndex() const noexcept { return AccumulationIndex; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    ReSTIRIntegratorConfiguration ActiveConfiguration;  // [-]  live-tunable parameters
    CelestialSolver    Sky{};        // A3: the authoritative clock; see CelestialSolver.h
    ExposureIntegrator Adaptation{};  // A6b: adaptive exposure
    DaylightSolver     Daylight{};    // A7e: the incident reading the exposure is anchored to
    // The sun direction the accumulated history was rendered under. A moving sun invalidates it exactly as a
    //    moving camera does — see ObserveCamera.
    mutable float   HistorySunX = 0.0f, HistorySunY = 0.0f, HistorySunZ = 0.0f;
    uint32_t                      AccumulationIndex;    // [-]  temporal frame counter (incremented per frame)

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
