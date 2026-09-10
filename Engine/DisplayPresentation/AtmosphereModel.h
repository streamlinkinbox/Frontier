//============================================================================================================================================
// 📦 Engine/DisplayPresentation/AtmosphereModel.h — Rayleigh/Mie/ozone sky, one definition for both render paths
//============================================================================================================================================
// Celestial port, step 1. Header-only and dependency-free on purpose: this same integral has to run in three
//    places that cannot share a binary —
//        • the GI-off CPU raster (VisibilityRaster), where it replaces the flat kSky constant,
//        • the GI-on path, where the sky becomes an environment light rather than a background colour,
//        • the headless proofs, which must evaluate it without a device.
//    and later a GLSL transcription in SkyView.slang. Anything that is a shared definition rather than a shared
//    binary is the thing that drifts, so the constants and the integral live here once and everything else reads
//    them. The shadow round already paid for that lesson twice (the PCSS half-angle, the tier ladder).
//
// The model is the reference demo's, transcribed faithfully: single-scattering Rayleigh + Mie with an ozone
//    absorption layer, quadratic sample spacing along the view ray (dense near the camera, where the density is),
//    and a nested light march for sun transmittance. Sample counts come from the tier ladder
//    (FidelityCriteria::AtmosphereSampleCount / AtmosphereLightSampleCount) — they are NOT hardcoded here.
//
// ⚠️ NOT LUT-BASED, deliberately. The source branch built transmittance and sky-view LUTs (f5b5d3d) and then
//    reverted them (2fe78ed): no measurable speedup on a GPU-bound frame, and the analytic path looked better.
//    That verdict was measured on WebGL/GTX so it may not hold for Vulkan compute, but the burden of proof sits
//    with the LUT. Start analytic; measure before replacing.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE MEDIUM
//------------------------------------------------------------------------------------------------------------------------

struct AtmosphereMedium
{
    // Scattering coefficients at sea level [1/m]. The Rayleigh triple is the classic 680/550/440 nm set — the
    //    ratio between the channels is why the sky is blue and the sunset is red, so these are physics, not taste.
    float RayleighScattering[3] = { 5.8e-6f, 13.5e-6f, 33.1e-6f };
    float MieScattering         = 21.0e-6f;
    // Ozone absorbs in the Chappuis band, which is what keeps the twilight sky blue rather than muddy brown after
    //    the Rayleigh term has faded.
    float OzoneAbsorption[3]    = { 0.65e-6f, 1.881e-6f, 0.085e-6f };

    float RayleighStrength = 1.0f;   // [x] artistic multiplier (the panel's Rayleigh axis)
    float MieStrength      = 1.0f;   // [x] haze (the panel's Mie axis)
    float OzoneStrength    = 1.2f;   // [x]

    float RayleighScaleHeight = 8000.0f;   // [m] e-folding height of the molecular density
    float MieScaleHeight      = 1200.0f;   // [m] aerosols hug the ground
    float MieAnisotropy       = 0.78f;     // [-] Henyey-Greenstein g: forward scattering, the glow around the sun

    float PlanetRadius     = 6360000.0f;   // [m]
    float AtmosphereHeight = 60000.0f;     // [m] top of the modelled shell
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SUN
//------------------------------------------------------------------------------------------------------------------------

struct AtmosphereLight
{
    float Direction[3] = { 0.0f, 0.0f, 1.0f };   // [-] unit, toward the sun (CelestialSolver's convention)
    float Colour[3]    = { 1.0f, 1.0f, 1.0f };   // [-] linear tint
    float Intensity    = 22.0f;                  // [x] matches the panel's default
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE INTEGRAL
//------------------------------------------------------------------------------------------------------------------------

struct AtmosphereSample
{
    float Radiance[3]     = {};        // [-] in-scattered light along the ray
    float Transmittance[3] = { 1.0f, 1.0f, 1.0f };   // [-] what survives the medium
    bool  HitGround        = false;
};

class AtmosphereModel
{
public:
    // Ray-sphere intersection about the planet centre. Returns false when the ray misses; otherwise Near/Far are
    //    the two roots and may be negative (the caller clamps).
    static bool IntersectSphere(const float Origin[3], const float Direction[3], float Radius,
                                float& Near, float& Far) noexcept
    {
        const float B = Origin[0] * Direction[0] + Origin[1] * Direction[1] + Origin[2] * Direction[2];
        const float C = Origin[0] * Origin[0] + Origin[1] * Origin[1] + Origin[2] * Origin[2] - Radius * Radius;
        const float D = B * B - C;
        if (D < 0.0f) return false;
        const float S = std::sqrt(D);
        Near = -B - S;
        Far  = -B + S;
        return true;
    }

    // Single-scattering sky radiance along Direction from a camera at Height metres above the surface.
    //
    //    SampleCount / LightSampleCount come from the tier. The quadratic spacing (s*s below) is what makes 16
    //    samples look like far more: the near half of the ray holds nearly all the density, so uniform spacing
    //    wastes most of its samples in near-vacuum.
    static AtmosphereSample Integrate(const AtmosphereMedium& Medium, const AtmosphereLight& Light,
                                      float Height, const float Direction[3],
                                      uint32_t SampleCount, uint32_t LightSampleCount) noexcept
    {
        AtmosphereSample Result{};

        const float Origin[3] = { 0.0f, 0.0f, Medium.PlanetRadius + std::fmax(Height, 0.0f) };
        const float TopRadius = Medium.PlanetRadius + Medium.AtmosphereHeight;

        float Near = 0.0f, Far = 0.0f;
        if (!IntersectSphere(Origin, Direction, TopRadius, Near, Far) || Far < 0.0f) return Result;

        float Start = std::fmax(Near, 0.0f);
        float End   = Far;

        // A ray that meets the ground stops there — everything beyond is rock, not air.
        float GroundNear = 0.0f, GroundFar = 0.0f;
        if (IntersectSphere(Origin, Direction, Medium.PlanetRadius, GroundNear, GroundFar) && GroundNear > 0.0f)
        {
            End = GroundNear;
            Result.HitGround = true;
        }
        if (End <= Start) return Result;

        const float BetaR[3] = { Medium.RayleighScattering[0] * Medium.RayleighStrength,
                                 Medium.RayleighScattering[1] * Medium.RayleighStrength,
                                 Medium.RayleighScattering[2] * Medium.RayleighStrength };
        const float BetaM    = Medium.MieScattering * Medium.MieStrength;
        const float BetaO[3] = { Medium.OzoneAbsorption[0] * Medium.OzoneStrength,
                                 Medium.OzoneAbsorption[1] * Medium.OzoneStrength,
                                 Medium.OzoneAbsorption[2] * Medium.OzoneStrength };

        // Phase functions. Rayleigh is symmetric; Mie's g pushes light forward, which is the halo around the sun.
        const float Mu = Direction[0] * Light.Direction[0] + Direction[1] * Light.Direction[1] + Direction[2] * Light.Direction[2];
        constexpr float kPi = 3.14159265358979323846f;
        const float PhaseR = 3.0f / (16.0f * kPi) * (1.0f + Mu * Mu);
        const float G = Medium.MieAnisotropy;
        const float Denominator = (2.0f + G * G) * std::pow(std::fmax(1.0f + G * G - 2.0f * G * Mu, 1e-6f), 1.5f);
        const float PhaseM = 3.0f / (8.0f * kPi) * ((1.0f - G * G) * (1.0f + Mu * Mu)) / std::fmax(Denominator, 1e-9f);

        const uint32_t N  = SampleCount      == 0u ? 1u : SampleCount;
        const uint32_t NL = LightSampleCount == 0u ? 1u : LightSampleCount;
        const float Length = End - Start;

        float SumR[3] = {}, SumM[3] = {};
        float OpticalR = 0.0f, OpticalM = 0.0f;

        for (uint32_t I = 0; I < N; ++I)
        {
            float S0 = static_cast<float>(I)        / static_cast<float>(N);
            float S1 = static_cast<float>(I + 1u)   / static_cast<float>(N);
            S0 *= S0; S1 *= S1;                       // quadratic: dense near the camera

            const float Ta  = Start + Length * S0;
            const float Tb  = Start + Length * S1;
            const float Seg = Tb - Ta;
            const float Tm  = 0.5f * (Ta + Tb);

            const float P[3] = { Origin[0] + Direction[0] * Tm,
                                 Origin[1] + Direction[1] * Tm,
                                 Origin[2] + Direction[2] * Tm };
            const float R = std::sqrt(P[0] * P[0] + P[1] * P[1] + P[2] * P[2]);
            const float H = R - Medium.PlanetRadius;

            const float DensityR = std::exp(-H / Medium.RayleighScaleHeight) * Seg;
            const float DensityM = std::exp(-H / Medium.MieScaleHeight) * Seg;
            OpticalR += DensityR;
            OpticalM += DensityM;

            // Sun transmittance at this point: march toward the light, and if the path dips below the surface
            //    the sample is in the planet's shadow and contributes nothing.
            float LightNear = 0.0f, LightFar = 0.0f;
            if (!IntersectSphere(P, Light.Direction, TopRadius, LightNear, LightFar)) continue;
            const float LightLength = LightFar;

            float LightR = 0.0f, LightM = 0.0f;
            bool  Lit = true;
            for (uint32_t J = 0; J < NL; ++J)
            {
                float Q0 = static_cast<float>(J)      / static_cast<float>(NL);
                float Q1 = static_cast<float>(J + 1u) / static_cast<float>(NL);
                Q0 *= Q0; Q1 *= Q1;
                const float SegL = LightLength * (Q1 - Q0);
                const float Tq   = LightLength * 0.5f * (Q0 + Q1);
                const float Q[3] = { P[0] + Light.Direction[0] * Tq,
                                     P[1] + Light.Direction[1] * Tq,
                                     P[2] + Light.Direction[2] * Tq };
                const float Hq = std::sqrt(Q[0] * Q[0] + Q[1] * Q[1] + Q[2] * Q[2]) - Medium.PlanetRadius;
                if (Hq < 0.0f) { Lit = false; break; }
                LightR += std::exp(-Hq / Medium.RayleighScaleHeight) * SegL;
                LightM += std::exp(-Hq / Medium.MieScaleHeight) * SegL;
            }
            if (!Lit) continue;

            // Mie extinction runs 1.1x its scattering: real aerosols absorb a little as well as scatter.
            for (int C = 0; C < 3; ++C)
            {
                const float Tau = BetaR[C] * (OpticalR + LightR)
                                + BetaM * 1.1f * (OpticalM + LightM)
                                + BetaO[C] * (OpticalR + LightR);
                const float Attenuation = std::exp(-Tau);
                SumR[C] += Attenuation * DensityR;
                SumM[C] += Attenuation * DensityM;
            }
        }

        for (int C = 0; C < 3; ++C)
        {
            Result.Transmittance[C] = std::exp(-(BetaR[C] * OpticalR + BetaM * 1.1f * OpticalM + BetaO[C] * OpticalR));
            Result.Radiance[C] = (SumR[C] * BetaR[C] * PhaseR + SumM[C] * BetaM * PhaseM)
                               * Light.Intensity * Light.Colour[C];
        }
        return Result;
    }
};

} // namespace Frontier
