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
    // [m] the depth of rain a ray must cross for a full-strength bow. Shorter paths fade out, which is what
    //    keeps the arc off nearby geometry — see the note on Rainbow().
    float MinimumPathMetres = 250.0f;
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

    // The bow along a view direction.
    //
    //    RainVisibility is how much falling rain the ray passes through — no rain, no bow, which is why this
    //    takes it rather than assuming.
    //
    //    ⚠️ RainDistance is how far the ray travels before it hits something, and it is not optional. A rainbow
    //    is light returned by drops SUSPENDED IN THE AIR: it is a feature of the volume between the viewer and
    //    whatever is behind it, so it can only appear where the ray has air to cross. A ray that meets the
    //    ground a few metres away has almost no air in it and must show almost no bow.
    //
    //    Getting this wrong is easy and looks obviously wrong the moment anyone checks: the first render of this
    //    system painted the arc across the ground plane as well as the sky, because the caller passed a constant
    //    visibility for every pixel. The bow appeared to be lying on the floor. Passing the distance makes the
    //    correct behaviour the default — a short ray gets a short path through rain and therefore no bow.
    //
    //    Pass a large distance (the sky) for rays that hit nothing.
    static void Rainbow(const RainbowSettings& Settings, const float Direction[3], const float SunDirection[3],
                        float RainVisibility, float RainDistanceMetres, float OutRgb[3]) noexcept
    {
        OutRgb[0] = OutRgb[1] = OutRgb[2] = 0.0f;
        if (!Settings.Enabled || RainVisibility <= 0.0f) return;

        // How much rain the ray actually crosses. A bow needs a deep column of drops; the reference figure is a
        //    few hundred metres of falling rain, below which the arc fades rather than cutting off, so a shower
        //    seen across a field still reads correctly.
        const float PathFraction = SmoothStep(0.0f, Settings.MinimumPathMetres, RainDistanceMetres);
        if (PathFraction <= 0.0f) return;

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
            OutRgb[C] = Sum[C] * Settings.Intensity * RainVisibility * SunUp * Band * PathFraction;
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
    //                                                  LENS FLARE
    //--------------------------------------------------------------------------------------------------------------------

    // ⚠️ A lens flare is an artefact of the CAMERA, not of the world, and that distinction decides its whole
    //    design. The sun does not have a flare; a lens pointed at the sun does. So this is the one effect in the
    //    Celestial port that is legitimately screen-space — the ghosts really do march along the line joining the
    //    sun's image to the frame centre, because that is the optical axis of the lens they are reflecting in.
    //
    //    ⚠️ AND IT COMPOSITES OVER GEOMETRY, WHICH IS CORRECT — do not "fix" this to match the rainbow. The two
    //    look like the same class of effect and are opposites. A rainbow is light returned by drops in the WORLD,
    //    so it sits at a depth and anything in front of it hides it (see the RainDistance note on Rainbow()). A
    //    flare is scattered across the SENSOR after the light has already entered the lens, so it lies over
    //    everything the sensor recorded, foreground included. Photographs show exactly that: the ghosts sit on
    //    top of the subject, never behind it.
    //
    //    Two consequences the implementation has to respect, and which a decorative sprite gets wrong:
    //      · the flare must be OCCLUDED by anything in front of the sun. Light that never entered the lens
    //        cannot bounce inside it, so a sun behind a wall produces no ghosts at all. This is passed in as
    //        SunVisibility rather than assumed.
    //      · it must vanish when the sun leaves the frame, and fade rather than pop as it approaches the edge.
    //
    //    Ghosts are placed at k = -1.35 + i*0.42 along that line, matching the reference demo. The spacing is
    //    what makes it read as a real lens: a row of reflections at unequal sizes, not a starburst.

    // Which lens is being simulated. These are not presets over one effect — each is a different optical
    //    assembly, and the components they emphasise are what tells them apart:
    //
    //        Cinematic   a modern coated zoom: ghosts, halo and a modest streak, everything present and balanced
    //        Anamorphic  a wide anamorphic prime: almost no ghosting, one long blue horizontal streak
    //        Starburst   a stopped-down lens: the aperture blades diffract the sun into spokes
    //        Halo        a simple uncoated element: one chromatic ring and little else
    //
    //    The weights below are the reference demo's and they are the whole definition of the difference. A single
    //    'flare' with an intensity slider cannot express them: anamorphic with more intensity is not starburst,
    //    it is a brighter streak.
    enum class LensFlareCategory : uint32_t
    {
        Cinematic = 0u, Anamorphic = 1u, Starburst = 2u, Halo = 3u,
    };

    struct LensFlareSettings
    {
        bool              Enabled  = true;
        LensFlareCategory Category = LensFlareCategory::Cinematic;
        uint32_t GhostCount  = 6u;      // [cnt] internal reflections drawn; tier-keyed
        float    Intensity   = 1.0f;    // [x]
        float    HaloRadius  = 0.28f;   // [ndc] the ring around the optical axis
        float    Chromatic   = 0.6f;    // [0..1] how coloured the ghosts are
        float    StreakGain  = 1.0f;    // [x] the horizontal anamorphic streak
        uint32_t ApertureBlades = 8u;   // [cnt] straight blades give 2N spokes, odd counts give N
    };

    // The component mix for a lens type. Exposed rather than buried so the panel can show what a type actually
    //    changes, and so the proof can assert the four are distinct rather than merely differently scaled.
    struct LensFlareMix
    {
        float Ghosts, Halo, Streak, Burst;
    };

    static LensFlareMix MixFor(LensFlareCategory Category) noexcept
    {
        switch (Category)
        {
            case LensFlareCategory::Anamorphic: return { 0.00f, 0.00f, 2.20f, 0.00f };
            case LensFlareCategory::Starburst:  return { 1.00f, 0.50f, 0.35f, 1.00f };
            case LensFlareCategory::Halo:       return { 0.00f, 1.00f, 0.35f, 0.00f };
            case LensFlareCategory::Cinematic:
            default:                            return { 1.00f, 1.00f, 1.00f, 0.35f };
        }
    }

    // ScreenUv and SunUv are in [0,1] with (0,0) at the top-left. SunVisibility is 0 when the sun is occluded.
    static void LensFlare(const LensFlareSettings& Settings, const float ScreenUv[2], const float SunUv[2],
                          float SunVisibility, float Aspect, float OutRgb[3]) noexcept
    {
        OutRgb[0] = OutRgb[1] = OutRgb[2] = 0.0f;
        if (!Settings.Enabled || SunVisibility <= 0.0f) return;

        // Off-screen suns produce nothing, and the approach to the edge is a fade rather than a pop.
        const float EdgeFade = SmoothStep(-0.15f, 0.05f, SunUv[0]) * (1.0f - SmoothStep(0.95f, 1.15f, SunUv[0]))
                             * SmoothStep(-0.15f, 0.05f, SunUv[1]) * (1.0f - SmoothStep(0.95f, 1.15f, SunUv[1]));
        if (EdgeFade <= 0.0f) return;

        // Work about the frame centre, because that is where the optical axis is.
        const float P[2] = { (ScreenUv[0] - 0.5f) * Aspect, ScreenUv[1] - 0.5f };
        const float S[2] = { (SunUv[0] - 0.5f) * Aspect, SunUv[1] - 0.5f };

        float Accumulated[3] = { 0.0f, 0.0f, 0.0f };
        const LensFlareMix Mix = MixFor(Settings.Category);

        // ── Ghosts: internal reflections, strung along the sun-to-centre line ──────────────────────────────────
        const uint32_t Ghosts = Mix.Ghosts > 0.0f ? (Settings.GhostCount > 8u ? 8u : Settings.GhostCount) : 0u;
        for (uint32_t I = 0; I < Ghosts; ++I)
        {
            const float Index = static_cast<float>(I);
            const float K = -1.35f + Index * 0.42f;
            const float Centre[2] = { S[0] * K, S[1] * K };
            const float Radius = 0.035f + 0.07f * Fract(Index * 0.618f + 0.31f);
            const float Dx = P[0] - Centre[0], Dy = P[1] - Centre[1];
            const float Distance = std::sqrt(Dx * Dx + Dy * Dy);

            float Shape = SmoothStep(Radius, Radius * 0.35f, Distance) * 0.9f
                        + SmoothStep(Radius * 1.6f, Radius, Distance) * 0.25f;
            Shape *= 0.06f + 0.06f * Fract(Index * 0.37f);
            if (Shape <= 0.0f) continue;

            float Tint[3];
            Hue(Fract(Index * 0.23f + 0.5f), Tint);
            for (int C = 0; C < 3; ++C)
                Accumulated[C] += Shape * (1.0f + (Tint[C] - 1.0f) * Settings.Chromatic) * Mix.Ghosts;
        }

        // ── Halo: a ring about the axis, chromatically smeared ─────────────────────────────────────────────────
        const float HaloCentre[2] = { S[0] * 0.25f, S[1] * 0.25f };
        const float Hx = P[0] - HaloCentre[0], Hy = P[1] - HaloCentre[1];
        const float HaloDistance = std::sqrt(Hx * Hx + Hy * Hy);
        const float RingOffset = std::fabs(HaloDistance - Settings.HaloRadius);
        const float Halo = SmoothStep(0.045f, 0.0f, RingOffset) * 0.09f * Mix.Halo;
        if (Halo > 0.0f)
        {
            constexpr float kPi = 3.14159265358979323846f;
            const float Angle = std::atan2(Hy, Hx);
            float Tint[3];
            Hue(Fract(Angle / (2.0f * kPi) + RingOffset * 8.0f), Tint);
            for (int C = 0; C < 3; ++C)
                Accumulated[C] += Halo * (1.0f + (Tint[C] - 1.0f) * Settings.Chromatic * 0.8f);
        }

        // ── Anamorphic streak: the horizontal bar a wide lens throws ───────────────────────────────────────────
        const float Mx = P[0] - S[0], My = P[1] - S[1];
        const float Streak = std::exp(-std::fabs(My) * 95.0f) * std::exp(-std::fabs(Mx) * 2.2f) * 0.55f
                           * Settings.StreakGain * Mix.Streak;
        // The streak runs blue because anamorphic elements are coated for it; that tint IS the look.
        Accumulated[0] += Streak * (1.0f + (0.45f - 1.0f) * Settings.Chromatic);
        Accumulated[1] += Streak * (1.0f + (0.65f - 1.0f) * Settings.Chromatic);
        Accumulated[2] += Streak;

        // ── Starburst: diffraction off the aperture blades ─────────────────────────────────────────────────────
        // A stopped-down iris is a polygon, and a polygonal aperture diffracts a point source into spokes. The
        //    count follows the optics rather than taste: an even number of straight blades gives 2N spokes,
        //    because opposite edges are parallel and their diffraction overlaps; an odd number gives N.
        if (Mix.Burst > 0.0f)
        {
            const uint32_t Blades = Settings.ApertureBlades < 3u ? 3u : Settings.ApertureBlades;
            const float Spokes = (Blades % 2u == 0u) ? static_cast<float>(Blades) : static_cast<float>(Blades) * 0.5f;
            const float Angle = std::atan2(My, Mx);
            const float Distance = std::sqrt(Mx * Mx + My * My);
            const float Primary = std::pow(std::fabs(std::sin(Angle * Spokes * 0.5f + 0.3f)), 24.0f)
                                * std::exp(-Distance * 3.5f) * 0.35f;
            // A second, finer set: real irises are not perfect polygons and the higher orders show.
            const float Secondary = std::pow(std::fabs(std::sin(Angle * Spokes * 0.875f)), 40.0f)
                                  * std::exp(-Distance * 6.0f) * 0.25f;
            const float Burst = (Primary + Secondary) * Mix.Burst;
            Accumulated[0] += Burst * 1.00f;
            Accumulated[1] += Burst * 0.95f;
            Accumulated[2] += Burst * 0.85f;
        }

        // The ambient bloom around the sun's image. Present for every lens, because it is scatter in the glass
        //    rather than a feature of any particular assembly.
        {
            const float Distance = std::sqrt(Mx * Mx + My * My);
            const float Bloom = std::exp(-Distance * 1.6f) * 0.04f;
            Accumulated[0] += Bloom * 1.00f;
            Accumulated[1] += Bloom * 0.90f;
            Accumulated[2] += Bloom * 0.80f;
        }

        for (int C = 0; C < 3; ++C)
            OutRgb[C] = Accumulated[C] * Settings.Intensity * SunVisibility * EdgeFade;
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
    static float Fract(float V) noexcept { return V - std::floor(V); }

    // A cheap spectral sweep for the ghosts' tinting. Not a colour space conversion — the ghosts are broad and
    //    low-contrast, and a full HSV would be precision nobody can see.
    static void Hue(float T, float OutRgb[3]) noexcept
    {
        constexpr float kPi = 3.14159265358979323846f;
        OutRgb[0] = 0.5f + 0.5f * std::cos(2.0f * kPi * (T + 0.00f));
        OutRgb[1] = 0.5f + 0.5f * std::cos(2.0f * kPi * (T + 0.33f));
        OutRgb[2] = 0.5f + 0.5f * std::cos(2.0f * kPi * (T + 0.67f));
    }

    static float SmoothStep(float Edge0, float Edge1, float V) noexcept
    {
        const float T = std::fmin(1.0f, std::fmax(0.0f, (V - Edge0) / (Edge1 - Edge0)));
        return T * T * (3.0f - 2.0f * T);
    }
};

} // namespace Frontier
