//============================================================================================================================================
// 📦 Engine/DisplayPresentation/AtmosphericOptics.h — rainbows and aerial perspective, from optics rather than art
//============================================================================================================================================
// Celestial port, step 7. Two effects that people usually fake and that are cheaper to derive than to tune.
//
// The rainbow in particular is worth doing properly. The usual approach is a coloured arc drawn at a fixed 42°
//    with a hand-picked gradient, which gets the position roughly right and everything else wrong: the secondary
//    bow is not at a fixed offset, its colours are REVERSED, and the sky between the two is measurably darker
//    than the sky outside them. All three fall out of the geometry for free, so there is no reason to invent
//    them — see the Descartes derivation in RainbowAngle below.
//
// Aerial perspective is the other half of step 7: distant geometry takes on the colour of the air in front of it.
//    That is the same Rayleigh/Mie extinction AtmosphereModel already integrates, applied over a finite segment
//    rather than out to space, so it shares the medium rather than restating it.

#pragma once

#include "AtmosphereModel.h"

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RAINBOW
//------------------------------------------------------------------------------------------------------------------------

struct RainbowSettings
{
    bool  Enabled       = true;
    float Intensity     = 1.0f;   // [x]
    float Width         = 1.0f;   // [x] multiplies the natural angular width
    float SecondaryGain = 1.0f;   // [0..1] how visible the second bow is
    bool  AlexanderBand = true;   // the darker sky between the bows
};

class AtmosphericOptics
{
public:
    // Refractive index of water by wavelength — Cauchy's approximation. This single line is what makes the bow
    //    spread into colours at all: n falls with wavelength, so red and violet leave the drop at different
    //    angles. n ≈ 1.331 at 700 nm and 1.343 at 400 nm, which matches measured water to three decimals.
    static float WaterIndex(float Nanometres) noexcept
    {
        return 1.3245f + 3000.0f / (Nanometres * Nanometres);
    }

    // The Descartes angle: where light of this wavelength piles up after `Order` internal reflections, measured
    //    from the ANTISOLAR point (the shadow of your own head).
    //
    //    Derived, not tabulated. The rainbow is the caustic where deviation is stationary, so the incidence angle
    //    that produces it comes straight out of dθ/di = 0. Evaluated here, this gives primary red at 42.43° and
    //    violet at 40.61°, secondary red at 50.27° and violet at 53.54° — the published figures, and note the
    //    secondary's order is REVERSED, which is a consequence of the extra reflection rather than a choice.
    static float RainbowAngle(float Nanometres, uint32_t Order) noexcept
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float N = WaterIndex(Nanometres);
        const float K = static_cast<float>(Order);
        const float CosI = std::sqrt((N * N - 1.0f) / (K * K + 2.0f * K));
        const float I = std::acos(std::fmin(1.0f, std::fmax(-1.0f, CosI)));
        const float R = std::asin(std::fmin(1.0f, std::sin(I) / N));
        const float Deviation = 2.0f * I - 2.0f * (K + 1.0f) * R + K * kPi;
        return Order < 2u ? (kPi - Deviation) : (Deviation - kPi);
    }

    // Wavelength to linear RGB. A piecewise fit rather than the CIE curves: the bow is a narrow band of
    //    saturated hues and the difference is not visible, where a full colour-matching integral would be.
    static void WavelengthToRgb(float Nanometres, float OutRgb[3]) noexcept
    {
        float R = 0.0f, G = 0.0f, B = 0.0f;
        if      (Nanometres < 440.0f) { R = -(Nanometres - 440.0f) / 60.0f; B = 1.0f; }
        else if (Nanometres < 490.0f) { G = (Nanometres - 440.0f) / 50.0f;  B = 1.0f; }
        else if (Nanometres < 510.0f) { G = 1.0f; B = -(Nanometres - 510.0f) / 20.0f; }
        else if (Nanometres < 580.0f) { R = (Nanometres - 510.0f) / 70.0f;  G = 1.0f; }
        else if (Nanometres < 645.0f) { R = 1.0f; G = -(Nanometres - 645.0f) / 65.0f; }
        else                          { R = 1.0f; }
        // The eye's response falls away at both ends of the visible band.
        const float Falloff = Nanometres < 420.0f ? 0.3f + 0.7f * (Nanometres - 380.0f) / 40.0f
                            : Nanometres > 680.0f ? 0.3f + 0.7f * (700.0f - Nanometres) / 20.0f
                            : 1.0f;
        OutRgb[0] = R * Falloff; OutRgb[1] = G * Falloff; OutRgb[2] = B * Falloff;
    }

    // The bow along a view direction. RainVisibility is how much falling rain the ray passes through — no rain,
    //    no bow, which is why this takes it rather than assuming.
    static void Rainbow(const RainbowSettings& Settings, const float Direction[3], const float SunDirection[3],
                        float RainVisibility, float OutRgb[3]) noexcept
    {
        OutRgb[0] = OutRgb[1] = OutRgb[2] = 0.0f;
        if (!Settings.Enabled || RainVisibility <= 0.0f) return;

        // A bow needs the sun above the horizon and behind the viewer. Below about -2° the antisolar point is
        //    high enough that the arc is entirely above the sky.
        constexpr float kPi = 3.14159265358979323846f;
        const float SunElevation = std::asin(std::fmax(-1.0f, std::fmin(1.0f, SunDirection[2]))) * 180.0f / kPi;
        const float SunUp = SmoothStep(-2.0f, 3.0f, SunElevation);
        if (SunUp <= 0.0f) return;

        // Angle from the antisolar point.
        const float Dot = -(Direction[0] * SunDirection[0] + Direction[1] * SunDirection[1]
                          + Direction[2] * SunDirection[2]);
        const float Angle = std::acos(std::fmax(-1.0f, std::fmin(1.0f, Dot)));
        // Outside the 34°–57° window there is nothing to compute.
        if (Angle < 0.60f || Angle > 1.00f) return;

        constexpr int kSamples = 14;
        float Sum[3] = { 0.0f, 0.0f, 0.0f };
        const float Width = std::fmax(Settings.Width, 0.05f);
        for (int I = 0; I < kSamples; ++I)
        {
            const float Wavelength = 380.0f + static_cast<float>(I) / static_cast<float>(kSamples - 1) * 320.0f;
            float Colour[3];
            WavelengthToRgb(Wavelength, Colour);

            const float Primary = RainbowAngle(Wavelength, 1u);
            const float D1 = (Angle - Primary) / (0.0035f * Width);
            const float W1 = std::exp(-D1 * D1);

            const float Secondary = RainbowAngle(Wavelength, 2u);
            const float D2 = (Angle - Secondary) / (0.0060f * Width);
            const float W2 = std::exp(-D2 * D2) * 0.42f * Settings.SecondaryGain;

            for (int C = 0; C < 3; ++C) Sum[C] += Colour[C] * (W1 + W2);
        }
        for (int C = 0; C < 3; ++C) Sum[C] /= static_cast<float>(kSamples) * 0.42f;

        // Alexander's dark band. Light cannot leave a drop between the two Descartes angles, so the sky there
        //    really is darker than the sky either side — the band is the ABSENCE of scattering, not a shadow, and
        //    it is one of the strongest cues that a rendered bow is physical.
        float Band = 1.0f;
        if (Settings.AlexanderBand)
        {
            const float PrimaryEdge   = RainbowAngle(400.0f, 1u);
            const float SecondaryEdge = RainbowAngle(700.0f, 2u);
            Band = 1.0f - 0.18f * SmoothStep(PrimaryEdge, PrimaryEdge + 0.02f, Angle)
                                * (1.0f - SmoothStep(SecondaryEdge - 0.02f, SecondaryEdge, Angle));
        }

        for (int C = 0; C < 3; ++C)
            OutRgb[C] = Sum[C] * Settings.Intensity * RainVisibility * SunUp * Band;
    }

    // How much the sky is darkened between the bows, at an angle. Separated so the caller can apply it to the
    //    background rather than only to the bow's own colour.
    static float AlexanderAttenuation(const RainbowSettings& Settings, float AngleFromAntisolar) noexcept
    {
        if (!Settings.Enabled || !Settings.AlexanderBand) return 1.0f;
        const float PrimaryEdge   = RainbowAngle(400.0f, 1u);
        const float SecondaryEdge = RainbowAngle(700.0f, 2u);
        return 1.0f - 0.18f * SmoothStep(PrimaryEdge, PrimaryEdge + 0.02f, AngleFromAntisolar)
                            * (1.0f - SmoothStep(SecondaryEdge - 0.02f, SecondaryEdge, AngleFromAntisolar));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              AERIAL PERSPECTIVE
    //--------------------------------------------------------------------------------------------------------------------

    // Distant geometry takes the colour of the air in front of it: extinction toward the viewer plus in-scatter
    //    from the sky. This shares AtmosphereModel's medium rather than restating the coefficients, so tuning
    //    the sky's haze tunes the distance haze with it — the two cannot drift apart.
    static void AerialPerspective(const AtmosphereMedium& Medium, const AtmosphereLight& Light,
                                  float Distance, float ViewerHeight, float TargetHeight,
                                  const float Direction[3], const float SkyRadiance[3],
                                  float OutTransmittance[3], float OutInScatter[3]) noexcept
    {
        for (int C = 0; C < 3; ++C) { OutTransmittance[C] = 1.0f; OutInScatter[C] = 0.0f; }
        if (Distance <= 0.0f) return;

        const float BetaR[3] = { Medium.RayleighScattering[0] * Medium.RayleighStrength,
                                 Medium.RayleighScattering[1] * Medium.RayleighStrength,
                                 Medium.RayleighScattering[2] * Medium.RayleighStrength };
        const float BetaM = Medium.MieScattering * Medium.MieStrength;

        // Mean density over the segment, from the exponential profile — closed form, no march.
        const float H = Medium.RayleighScaleHeight;
        const float Rise = TargetHeight - ViewerHeight;
        const float MeanR = std::fabs(Rise) < 1e-3f
            ? std::exp(-ViewerHeight / H)
            : H * (std::exp(-ViewerHeight / H) - std::exp(-TargetHeight / H)) / Rise;
        const float Hm = Medium.MieScaleHeight;
        const float MeanM = std::fabs(Rise) < 1e-3f
            ? std::exp(-ViewerHeight / Hm)
            : Hm * (std::exp(-ViewerHeight / Hm) - std::exp(-TargetHeight / Hm)) / Rise;

        constexpr float kPi = 3.14159265358979323846f;
        const float Mu = Direction[0] * Light.Direction[0] + Direction[1] * Light.Direction[1]
                       + Direction[2] * Light.Direction[2];
        const float PhaseR = 3.0f / (16.0f * kPi) * (1.0f + Mu * Mu);
        const float G = Medium.MieAnisotropy;
        const float PhaseM = 3.0f / (8.0f * kPi) * ((1.0f - G * G) * (1.0f + Mu * Mu))
                           / std::fmax((2.0f + G * G) * std::pow(std::fmax(1.0f + G * G - 2.0f * G * Mu, 1e-6f), 1.5f), 1e-9f);

        for (int C = 0; C < 3; ++C)
        {
            const float Tau = (BetaR[C] * MeanR + BetaM * 1.1f * MeanM) * Distance;
            OutTransmittance[C] = std::exp(-Tau);
            // What the air itself contributes over that path. Using the sky's own radiance as the source keeps
            //    the haze the colour of the sky it sits under, at dusk as well as at noon.
            const float Scatter = (BetaR[C] * MeanR * PhaseR + BetaM * MeanM * PhaseM) * Distance;
            OutInScatter[C] = SkyRadiance[C] * (1.0f - OutTransmittance[C]) * std::fmin(1.0f, Scatter * 12.0f + 0.35f);
        }
    }

private:
    static float SmoothStep(float Edge0, float Edge1, float V) noexcept
    {
        const float T = std::fmin(1.0f, std::fmax(0.0f, (V - Edge0) / (Edge1 - Edge0)));
        return T * T * (3.0f - 2.0f * T);
    }
};

} // namespace Frontier
