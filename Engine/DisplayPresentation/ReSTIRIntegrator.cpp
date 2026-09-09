//============================================================================================================================================
//                                                     RESTIRINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 Accumulates ReSTIR DI+GI radiance by numerically integrating light transport paths on the GPU compute pipeline.

#include "ReSTIRIntegrator.h"
#include <algorithm>
#include <string>
#include <cmath>

namespace Frontier {

//============================================================================================================================================
//                                                     LIFECYCLE
//============================================================================================================================================

ReSTIRIntegrator::ReSTIRIntegrator(ReSTIRIntegratorConfiguration InitialConfiguration) noexcept
    : ActiveConfiguration(InitialConfiguration)
    , AccumulationIndex(0u)
    , HistoryOrigin{ 0.0f, 0.0f, 0.0f }
    , HistoryForward{ 0.0f, 0.0f, 0.0f }
    , HistoryWidth(0u)
    , HistoryHeight(0u)
{
}

//============================================================================================================================================
//                                                OBSERVE CAMERA
//============================================================================================================================================

void ReSTIRIntegrator::ObserveDaylight() noexcept
{
    // 🔴 The sun's own elevation and the air it shines through — nothing about where the camera is. That is the
    //    property being bought: an exposure derived from these cannot move when the camera does.
    const HorizonDirection Sun = Sky.QuerySunDirection();
    const float Turbidity = QueryDiurnalTurbidity(ActiveConfiguration.SkyTurbidity,
                                                  ActiveConfiguration.TurbiditySwing,
                                                  Sky.QuerySolarDayFraction(Sky.QueryTime()));

    // Sky off means there is no incident reading to give, and the exposure falls back to metering the frame —
    //    which is right, because then the only light in the world is whatever the scene itself carries.
    const bool SkyOn = ActiveConfiguration.SunIlluminance > 0.0f
                    && ActiveConfiguration.SkyQuality != SkyQualityCategory::Off;
    Adaptation.ObserveIlluminance(SkyOn
        ? Daylight.QueryAnchorLuminance(ActiveConfiguration.SunIlluminance,
                                        static_cast<float>(Sun.Elevation), Turbidity)
        : 0.0f);
}

void ReSTIRIntegrator::ObserveCamera(const ProjectZero::FlyThroughSolver& Camera,
                                     uint32_t ViewportWidth, uint32_t ViewportHeight) noexcept
{
    const Vector3& Origin  = Camera.QuerySpatialLocation();
    const Vector3& Forward = Camera.QueryForwardVector();

    constexpr float PositionTolerance  = 1e-5f;   // [m]
    constexpr float DirectionTolerance = 1e-6f;   // [-]

    const Vector3 OriginDelta  = Origin  - HistoryOrigin;
    const Vector3 ForwardDelta = Forward - HistoryForward;

    const bool Moved   = OriginDelta.LengthSquared()  > PositionTolerance  * PositionTolerance;
    const bool Turned  = ForwardDelta.LengthSquared() > DirectionTolerance * DirectionTolerance;
    const bool Resized = ViewportWidth != HistoryWidth || ViewportHeight != HistoryHeight;

    // A3 ⚠️ A MOVING SUN INVALIDATES THE HISTORY TOO. The camera can be perfectly still while the sky changes
    //    underneath it, and accumulating across that blends two different skies — a sunset would smear into a
    //    long grey dissolve and read as a denoiser fault rather than a stale history. The threshold is angular
    //    so a paused clock costs nothing: 0.0001 rad is roughly a twentieth of the sun's own diameter, well
    //    below what a frame can show, so a slow day/night cycle still accumulates between steps.
    const HorizonDirection Sun = Sky.QuerySunDirection();
    const float SunX = static_cast<float>(Sun.East), SunY = static_cast<float>(Sun.North), SunZ = static_cast<float>(Sun.Zenith);
    const float SunDelta = (SunX - HistorySunX) * (SunX - HistorySunX)
                         + (SunY - HistorySunY) * (SunY - HistorySunY)
                         + (SunZ - HistorySunZ) * (SunZ - HistorySunZ);
    constexpr float SunTolerance = 1e-4f;   // [rad] chord ≈ angle for small angles
    const bool SunMoved = SunDelta > SunTolerance * SunTolerance;

    if (Moved || Turned || Resized || SunMoved)
    {
        HistorySunX = SunX; HistorySunY = SunY; HistorySunZ = SunZ;
        HistoryOrigin  = Origin;
        HistoryForward = Forward;
        HistoryWidth   = ViewportWidth;
        HistoryHeight  = ViewportHeight;
        ResetAccumulation();
    }
}

//============================================================================================================================================
//                                                  BUILD DISPATCH
//============================================================================================================================================

DispatchConfiguration ReSTIRIntegrator::BuildDispatch(
    const ProjectZero::FlyThroughSolver& Camera,
    uint32_t                             ViewportWidth,
    uint32_t                             ViewportHeight,
    uint32_t                             AlphaMaskedMaterialCount,
    uint32_t                             LuminaireTriangleCount) const noexcept
{
    const Vector3& Origin  = Camera.QuerySpatialLocation();
    const Vector3& Forward = Camera.QueryForwardVector();
    const Vector3& Right   = Camera.QueryRightVector();
    const Vector3& Up      = Camera.QueryUpwardVector();

    const float TanHalf = std::tan(Camera.QueryFieldOfViewRadians() * 0.5f);

    DispatchConfiguration Dispatch{};
    Dispatch.CameraOriginX         = Origin.x;
    Dispatch.CameraOriginY         = Origin.y;
    Dispatch.CameraOriginZ         = Origin.z;
    Dispatch.FieldOfViewTanHalf    = TanHalf;
    Dispatch.CameraForwardX        = Forward.x;
    Dispatch.CameraForwardY        = Forward.y;
    Dispatch.CameraForwardZ        = Forward.z;
    Dispatch.AspectRatio           = Camera.QueryAspectRatio();
    Dispatch.CameraRightX          = Right.x;
    Dispatch.CameraRightY          = Right.y;
    Dispatch.CameraRightZ          = Right.z;
    // A6b. ONE exposure value reaches the shader, whether it came from the slider or from adaptation. Manual
    //    mode returns the configured value unchanged, so every pre-A6b image is still reproducible, and the two
    //    modes cannot become two code paths that disagree about what the tone map receives.
    Dispatch.Exposure              = Adaptation.QueryExposure();
    // A7d. The eye's remaining colour at this adapted level. Taken from the same integrator as the exposure so
    //    the two can never describe different light.
    Dispatch.ColourSaturation      = Adaptation.QueryColourSaturation();
    Dispatch.CameraUpX             = Up.x;
    Dispatch.CameraUpY             = Up.y;
    Dispatch.CameraUpZ             = Up.z;
    Dispatch.AmbientStrength       = ActiveConfiguration.AmbientStrength;
    Dispatch.ViewportWidth         = ViewportWidth;
    Dispatch.ViewportHeight        = ViewportHeight;
    Dispatch.AccumulationIndex     = AccumulationIndex;
    Dispatch.ExtraCandidateCount      = ActiveConfiguration.ExtraCandidateCount;
    Dispatch.CandidatesPerPixel    = ActiveConfiguration.CandidatesPerPixel;
    Dispatch.AlphaMaskedMaterialCount = AlphaMaskedMaterialCount;   // R4b: 0 keeps the any-hit shadow path
    Dispatch.LuminaireTriangleCount = LuminaireTriangleCount;
    Dispatch.FeatureFlags          = (ActiveConfiguration.GlobalIllumination ? DispatchFeatureGlobalIllumination : 0u)
                                   | (ActiveConfiguration.AntiAliasing       ? DispatchFeatureAntiAliasing       : 0u)
                                   | (ActiveConfiguration.AmbientFloor       ? DispatchFeatureAmbientFloor       : 0u)
                                   | (ActiveConfiguration.TemporalReuse      ? DispatchFeatureTemporalReuse      : 0u)
                                   | (ActiveConfiguration.SpatialReuse       ? DispatchFeatureSpatialReuse       : 0u)
                                   | (ActiveConfiguration.AliasPick          ? DispatchFeatureAliasPick          : 0u)
                                   | (ActiveConfiguration.TemporalReprojection ? DispatchFeatureTemporalReprojection : 0u)
                                   | (ActiveConfiguration.Denoise            ? DispatchFeatureDenoise            : 0u)
                                   | (ActiveConfiguration.SkyLighting        ? DispatchFeatureSkyLighting        : 0u)
                                   | (ActiveConfiguration.NightSky           ? DispatchFeatureNightSky           : 0u);

    for (uint32_t& Reserve : Dispatch.PushReserve) Reserve = 0u;

    return Dispatch;
}

//============================================================================================================================================
//                                                  A7 — THE SKY RECORD
//============================================================================================================================================

SkyRecord ReSTIRIntegrator::BuildSkyRecord() const noexcept
{
    SkyRecord Record{};

    // Every direction comes from the celestial clock, which is the single authoritative source. The integrator
    //    never stores a sun or a moon of its own, or two copies would eventually disagree about the time.
    const HorizonDirection Sun  = Sky.QuerySunDirection();
    const HorizonDirection Moon = Sky.QueryMoonDirection();

    Record.SunDirectionX = static_cast<float>(Sun.East);
    Record.SunDirectionY = static_cast<float>(Sun.North);
    Record.SunDirectionZ = static_cast<float>(Sun.Zenith);

    Record.MoonDirectionX = static_cast<float>(Moon.East);
    Record.MoonDirectionY = static_cast<float>(Moon.North);
    Record.MoonDirectionZ = static_cast<float>(Moon.Zenith);
    Record.MoonPhase      = static_cast<float>(Sky.QueryMoonPhase());

    Record.CameraAltitude = ActiveConfiguration.CameraAltitude;
    Record.StarRotation   = static_cast<float>(Sky.QuerySiderealAngle(Sky.QueryTime()));

    const SkyStepCounts Steps = QuerySkySteps(ActiveConfiguration.SkyQuality);
    Record.SkyViewSteps  = Steps.View;
    Record.SkyLightSteps = Steps.Light;

    // A7b. The aerosol load for this hour. Taken from the SAME clock as the sun above, so the air and the sun
    //    can never describe different times of day — which would show as a clean-air glow under a low evening
    //    sun, i.e. exactly the symptom this parameter exists to fix, but at the wrong end of the day.
    Record.SkyTurbidity = QueryDiurnalTurbidity(ActiveConfiguration.SkyTurbidity,
                                                ActiveConfiguration.TurbiditySwing,
                                                Sky.QuerySolarDayFraction(Sky.QueryTime()));

    // Quality Off zeroes the illuminance, which is the single condition the shader tests. Two conditions could
    //    disagree about whether the sky is on.
    const bool SkyOn = ActiveConfiguration.SkyQuality != SkyQualityCategory::Off;
    Record.SunIlluminance = SkyOn ? ActiveConfiguration.SunIlluminance : 0.0f;

    // The moon and the stars are only offered when they could actually be seen. Below the horizon the moon is
    //    not drawn at all, and with the sun well up the stars are washed out anyway — but they are faded rather
    //    than switched, so nothing pops at dawn.
    Record.MoonIlluminance = (SkyOn && ActiveConfiguration.NightSky && Moon.Zenith > -0.05)
                           ? ActiveConfiguration.SunIlluminance : 0.0f;
    Record.StarBrightness  = (SkyOn && ActiveConfiguration.NightSky)
                           ? ActiveConfiguration.StarBrightness : 0.0f;

    Record.MoonAngularScale = ActiveConfiguration.MoonAngularScale;

    return Record;
}

//============================================================================================================================================
//                                               SCENE RECORD BUILDERS
//============================================================================================================================================

uint32_t ReSTIRIntegrator::CountLuminaireTriangles(const ProjectZero::RayTracingSolver& Scene) noexcept
{
    const auto& Triangles = Scene.QueryTriangles();
    const auto& Materials = Scene.QueryMaterials();

    uint32_t Count = 0u;
    for (const auto& Triangle : Triangles)
    {
        if (Triangle.MaterialIndex < Materials.size())
        {
            const auto& Material = Materials[Triangle.MaterialIndex];
            const float EmissiveMagnitude = Material.EmissiveRadiance.x
                                          + Material.EmissiveRadiance.y
                                          + Material.EmissiveRadiance.z;
            if (EmissiveMagnitude > 0.0f) ++Count;
        }
    }
    return Count;
}

std::vector<TriangleIndex> ReSTIRIntegrator::BuildTriangleIndex(
    const ProjectZero::RayTracingSolver& Scene) noexcept
{
    const auto& Triangles = Scene.QueryTriangles();

    std::vector<TriangleIndex> Records;
    Records.reserve(Triangles.size());

    for (const auto& Triangle : Triangles)
    {
        TriangleIndex Record{};
        Record.VertexAlphaX  = Triangle.VertexAlpha.x;
        Record.VertexAlphaY  = Triangle.VertexAlpha.y;
        Record.VertexAlphaZ  = Triangle.VertexAlpha.z;
        Record.MaterialSlot  = *reinterpret_cast<const float*>(&Triangle.MaterialIndex);
        Record.VertexBetaX   = Triangle.VertexBeta.x;
        Record.VertexBetaY   = Triangle.VertexBeta.y;
        Record.VertexBetaZ   = Triangle.VertexBeta.z;
        Record.VertexGammaX  = Triangle.VertexGamma.x;
        Record.VertexGammaY  = Triangle.VertexGamma.y;
        Record.VertexGammaZ  = Triangle.VertexGamma.z;
        // R4a: no per-face normal or UVs in the analytical Cornell soup (flat-shaded, untextured)
        Records.push_back(Record);
    }
    return Records;
}

std::vector<MaterialDescriptor> ReSTIRIntegrator::BuildMaterialDescriptors(
    const ProjectZero::RayTracingSolver& Scene) noexcept
{
    const auto& Materials = Scene.QueryMaterials();

    std::vector<MaterialDescriptor> Records;
    Records.reserve(Materials.size());

    for (const auto& Material : Materials)
    {
        MaterialDescriptor D;
        D.Name = "material_" + std::to_string(Material.MaterialIdentifier);
        D.Slabs.emplace_back();
        MaterialSlabDescriptor& S = D.Slabs.back();
        S.BaseColor[0] = Material.AlbedoColor.x; S.BaseColor[1] = Material.AlbedoColor.y; S.BaseColor[2] = Material.AlbedoColor.z;
        S.SpecularRoughness = Material.RoughnessValue;
        S.BaseMetalness     = Material.MetallicValue;
        S.SpecularWeight    = 0.0f;   // R4b pin (approved): the analytical Cornell box is Lambertian — no dielectric lobe, so R3/R4a images stay the reference
        const float E[3] = { Material.EmissiveRadiance.x, Material.EmissiveRadiance.y, Material.EmissiveRadiance.z };
        const float Peak = std::max({ E[0], E[1], E[2], 0.0f });
        if (Peak > 0.0f) { S.EmissionLuminance = Peak; S.EmissionColor[0] = E[0] / Peak; S.EmissionColor[1] = E[1] / Peak; S.EmissionColor[2] = E[2] / Peak; }
        Records.push_back(std::move(D));
    }
    return Records;
}

} // namespace Frontier
