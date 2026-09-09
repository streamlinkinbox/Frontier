//============================================================================================================================================
//                                                       DAYLIGHTSOLVER.CPP
//============================================================================================================================================

#include "DaylightSolver.h"

#include "AtmosphereModel.h"

#include <cmath>

namespace Frontier {

namespace {

using namespace Frontier::AtmosphereModel;

constexpr float kPi = 3.14159265358979f;

// Rec. 709, the same weighting every other luminance in the renderer uses. A different one here would make the
//    exposure disagree with the tone map about what "bright" means.
float Luma(Vec3 C) noexcept { return 0.2126f * C.x + 0.7152f * C.y + 0.0722f * C.z; }

// ⚠️ Ring counts, not a single number of samples. The sky's radiance varies far more steeply with ELEVATION than
//    with azimuth — the whole interesting structure is a bright band a few degrees above the horizon — so the
//    rings are spaced in a way that puts samples where the variation is, and each ring is averaged over azimuth.
constexpr int kElevationRings = 24;
constexpr int kAzimuthSamples = 8;

} // namespace

float DaylightSolver::QueryIlluminance(float SunIlluminance, float SunElevationRadians, float Turbidity) noexcept
{
    // The cache is the reason this can be called every frame. The integral below is a few hundred thousand
    //    operations; the sun moves 0.2° in about fifty seconds of a real day, and far less than the eye can
    //    follow between one frame and the next.
    if (std::fabs(SunElevationRadians - CachedElevation) <= kElevationTolerance
     && std::fabs(Turbidity - CachedTurbidity) <= kTurbidityTolerance
     && std::fabs(SunIlluminance - CachedSun) <= 1.0f)
        return CachedIlluminance;

    CachedElevation = SunElevationRadians;
    CachedTurbidity = Turbidity;
    CachedSun       = SunIlluminance;

    gAerosolTurbidity = Turbidity > 0.0f ? Turbidity : 1.0f;

    const float SinElevation = std::sin(SunElevationRadians);
    const Vec3  SunDirection = Normalize(Vec3{ 0.0f, std::cos(SunElevationRadians), SinElevation });
    const Vec3  Ground{ 0.0f, 0.0f, kPlanetRadius + 2.0f };

    // ── The direct beam ──────────────────────────────────────────────────────────────────────────────────────
    // Illuminance on a HORIZONTAL surface, so the cosine is the sun's own elevation. Below the horizon there is
    //    no direct term at all — everything at twilight comes from the sky.
    CachedDirect = 0.0f;
    if (SinElevation > 0.0f)
        CachedDirect = SunIlluminance * SinElevation * Luma(SunTransmittance(Ground, SunDirection, 32));

    // ── The sky ──────────────────────────────────────────────────────────────────────────────────────────────
    // ∫ L(ω)·cosθ dω over the upper hemisphere. Sampled on rings of equal solid angle in cos θ so the cosine
    //    weighting is carried by the sampling rather than applied afterwards, which keeps the estimator unbiased
    //    without needing more rings near the zenith where nothing happens.
    double Diffuse = 0.0;
    double Uniform = 0.0;   // the same samples, weighted by solid angle rather than by the cosine
    for (int Ring = 0; Ring < kElevationRings; ++Ring)
    {
        // Uniform in sin²θ gives rings of equal cosine-weighted solid angle: the classic cosine-hemisphere
        //    stratification, so every sample carries the same weight and the sum is a plain mean.
        const float U        = (static_cast<float>(Ring) + 0.5f) / static_cast<float>(kElevationRings);
        const float SinTheta = std::sqrt(U);                       // θ from the zenith
        const float CosTheta = std::sqrt(std::fmax(0.0f, 1.0f - SinTheta * SinTheta));

        for (int Step = 0; Step < kAzimuthSamples; ++Step)
        {
            const float Phi = (static_cast<float>(Step) + 0.5f) / static_cast<float>(kAzimuthSamples) * 2.0f * kPi;
            const Vec3  View{ SinTheta * std::sin(Phi), SinTheta * std::cos(Phi), CosTheta };
            const float Radiance = Luma(SkyRadiance(2.0f, Normalize(View), SunDirection,
                                                    Vec3(SunIlluminance), 24, 8));
            Diffuse += static_cast<double>(Radiance);
            // ⚠️ The samples are cosine-distributed, so undoing that weight is what turns this into a plain
            //    solid-angle mean. Without the division the horizon band — which is where a low sun puts all of
            //    its light, and where the camera is pointed — would count for almost nothing here too.
            Uniform += static_cast<double>(Radiance) / std::fmax(CosTheta, 0.02f);
        }
    }
    // Cosine-weighted hemisphere integral of a constant L is πL, and the samples are already cosine
    //    distributed, so the mean times π is the irradiance.
    CachedDiffuse = static_cast<float>(Diffuse / (kElevationRings * kAzimuthSamples)) * kPi;

    // Normalised by the same undone weights, so a uniform sky of radiance L returns exactly L.
    double Weight = 0.0;
    for (int Ring = 0; Ring < kElevationRings; ++Ring)
    {
        const float U = (static_cast<float>(Ring) + 0.5f) / static_cast<float>(kElevationRings);
        const float CosTheta = std::sqrt(std::fmax(0.0f, 1.0f - U));
        Weight += static_cast<double>(kAzimuthSamples) / std::fmax(CosTheta, 0.02f);
    }
    CachedMeanSky = Weight > 0.0 ? static_cast<float>(Uniform / Weight) : 0.0f;

    gAerosolTurbidity = 1.0f;   // leave the shared model on its reference atmosphere

    CachedIlluminance = CachedDirect + CachedDiffuse;
    return CachedIlluminance;
}

float DaylightSolver::QueryAnchorLuminance(float SunIlluminance, float SunElevationRadians, float Turbidity) noexcept
{
    const float Illuminance = QueryIlluminance(SunIlluminance, SunElevationRadians, Turbidity);

    // The lower half of what a camera can point at is the lit ground, whose radiance is the planet's albedo
    //    against the illuminance the sky and sun deliver — the cosine-weighted figure, correctly, because that
    //    is what actually lands on it.
    const float GroundRadiance = kGroundAlbedo * Illuminance / kPi;

    // Half the sphere is sky and half is ground. Not a physical average of anything — an estimate of what the
    //    camera will be shown, which is what the exposure has to be right about.
    return 0.5f * (CachedMeanSky + GroundRadiance);
}

} // namespace Frontier
