/*==========================================================================================================================================
                                                           REFRESHENCODING.H
==========================================================================================================================================*/
// 🧩 The 1072-byte RefreshConstants every program reads: camera, jitter, light, toggles, focus (IlluminationHost.EncodeFrameRecord).

#pragma once

#include "AtmosphereIntegrator.h"
#include "ShadowProjection.h"

#include <array>
#include <optional>
#include <vector>

namespace Frontier::GlobalIllumination
{

// 📝 The demo Settings that reach the refresh constants, with the demo's defaults.
struct RefreshToggles
{
    bool     Indirect             = true;
    bool     TemporalReuse        = true;
    bool     SpatialReuse         = true;
    bool     VisibilityReuse      = true;
    bool     VarianceFilter       = true;
    bool     Reflection           = true;
    bool     CardRadiosity        = true;
    bool     Antialias            = true;
    bool     DirectShadow         = true;
    bool     AutoExposure         = true;
    bool     RadianceRefresh      = true;
    bool     RayTracing           = true;
    bool     RayTracedShadows     = true;
    bool     RussianRoulette      = true;
    bool     FireflyFilter        = true;
    bool     MultiBounceNee       = true;
    bool     RayTracedReflections = true;
    uint32_t MaxBounces           = 3;
    uint32_t InitialCandidates    = 2;
    uint32_t ReflectionBounces    = 2;
    uint32_t Inspection           = 0;
    double   TemporalTallyLimit   = 20.0;
    double   SpatialRadius        = 20.0;   // [px]
    uint32_t SpatialCount         = 5;
    double   SkyScale             = 1.0;
    double   IncandescenceScale   = 1.0;
    double   ExposureCompensation = 0.0;    // [EV]
    double   IndirectScale        = 1.0;
    uint32_t DistanceInspection   = 1;
    double   ReflectionRoughness  = 0.80;
    double   ReflectionScale      = 1.0;
    uint32_t Refinements          = 0;      // [-] RefinementBit mask (SceneLayouts.slang); 0 reproduces the demo
};

// 📝 Options beyond the demo, each measured against it (Exhibits/RayTracedLighting).
namespace RefinementBit
{
constexpr uint32_t SolarDisc      = 1u;
constexpr uint32_t StratifiedSun  = 2u;
constexpr uint32_t PenumbraFilter = 4u;
constexpr uint32_t MirrorFilter   = 8u;
constexpr uint32_t MirrorTrace    = 16u;
constexpr uint32_t ShadowReference = 64u;    // [-] measurement only: 64 stratified sun rays per pixel
constexpr uint32_t MirrorReference = 128u;   // [-] measurement only: every reflective pixel traced 16 times at screen resolution
} // namespace RefinementBit

// 📝 Inputs owned by systems outside the port (HMI brightness word, fire light, environment block, elapsed time):
//    the editor supplies its own, a replay supplies the demo's.
struct RefreshExternalInputs
{
    double                 ElapsedSeconds  = 0.0;    // [s]
    uint32_t               PresentationControl         = 0;      // [-] scalar 119: brightness << 8 | scale << 20
    std::array<float, 8>   FireBlock{};              // [-] scalars 120..127
    std::vector<float>     EnvironmentBlock{};       // [-] scalars 128..267 (EncodeEnvironment)
    bool                   SmokeResident   = false;
};

class RefreshEncoder
{
  public:
    // 📝 Returns the 1072 bytes; retains the unjittered view-projection as the next refresh's "previous".
    std::vector<uint8_t> Encode(const CameraPose& Camera, const LightSolution& Light, const RefreshToggles& Toggles,
                                const RefreshExternalInputs& External, std::array<uint32_t, 2> ScreenExtent, std::array<uint32_t, 2> TraceExtent,
                                uint32_t TraceScale, uint32_t RefreshIndex, const Vector3& Focus, bool PriorValid);

    std::optional<Transform> PreviousUnjittered{};
};

} // namespace Frontier::GlobalIllumination
