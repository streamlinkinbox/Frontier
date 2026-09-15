//============================================================================================================================================
//                                                       EXPOSUREINTEGRATOR.H
//============================================================================================================================================
// 🧩 Adaptive exposure — the eye's response to a scene that now spans eight orders of magnitude.
//
//    A3–A5 made the sky real, and with it the dynamic range: a noon sky is ~8 000 cd/m², a moonlit sky ~0.1, a
//    star ~0.001. One fixed exposure slider cannot serve those. Every later phase is blocked on this — aerial
//    perspective is invisible without a correct mid-tone to compare against, and a night sky under a daylight
//    exposure is a black screen with one blown moon disc.
//
//    Named …Integrator because it advances a differential equation over time (CLAUDE.md §2.7): the adapted
//    luminance chases the measured one, and that chase IS the phenomenon. A Solver would be wrong — nothing
//    here is a constraint to satisfy.
//
//    🔴 The measurement is a LOG mean, never a linear one. A linear average is dominated by whatever is
//    brightest, so the sun disc entering frame would drag the mean up by orders of magnitude and black out the
//    whole image. Human brightness perception is roughly logarithmic, and so is this.
//
//    The adaptation is deliberately ASYMMETRIC. Adapting to bright is fast — the iris closes in under a second,
//    and a viewer expects a bright doorway to resolve almost immediately. Adapting to dark is slow, because
//    that is what it is: walking into a dark room, everything is black and then gradually is not. Symmetric
//    rates feel wrong in both directions at once.

#pragma once

#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

enum class ExposureModeCategory : uint32_t
{
    Manual   = 0u,   // the fixed slider — reproduces every pre-A6b image exactly
    Adaptive = 1u,   // measured from the frame and eased over time
};

struct ExposureConfiguration
{
    ExposureModeCategory Mode = ExposureModeCategory::Adaptive;

    float ManualExposure   = 1.05f;    // [-]   used in Manual mode; also the value Adaptive starts from
    float KeyValue         = 0.18f;    // [-]   the mid-grey a correctly exposed scene should average to
    float BrightenSeconds  = 0.40f;    // [s]   time constant when the scene gets BRIGHTER (iris closes fast)
    float DarkenSeconds    = 2.20f;    // [s]   and when it gets DARKER (dark adaptation is genuinely slow)

    // Bounds on the exposure itself. Without a ceiling, a nearly black frame drives exposure toward infinity
    //    and amplifies pure noise into a grey blizzard; without a floor, staring at the sun drives it to zero
    //    and the image never recovers.
    //
    // ⚠️ The floor has to be genuinely tiny. A noon sky at 8 000 cd/m² needs 2.25e-5 to expose correctly, and
    //    the sun's own disc at 1.6e9 needs 1.1e-10 — a floor of 0.02 would clamp daylight and render it at 160,
    //    i.e. pure white. These bounds exist to stop divergence on a degenerate frame, not to express taste.
    float MinimumExposure  = 1.0e-10f;   // [-] below what the sun's disc alone would ask for
    float MaximumExposure  = 4000.0f;    // [-] a starlit sky at 0.001 cd/m² asks for 180

    // 🔴 Numerical epsilon ONLY, and it has to be tiny. This was 1e-2 cd/m² when it doubled as a metering
    //    threshold, and that was a daylight constant standing in a place where every scale of scene passes
    //    through: a night sky at 1e-4 clamped straight up to it, so the whole night metered as though it were
    //    a hundred times brighter than it is and the adaptation curve below never engaged at all. The metering
    //    decision now lives in the histogram, where it is a percentile of the frame and has no absolute value
    //    in it, and this is left doing the one job it should ever have had: keeping log() finite.
    float LuminanceFloor   = 1.0e-6f;  // [cd/m²] a moonless overcast night is around 1e-4

    // ── Dark adaptation ─────────────────────────────────────────────────────────────────────────────────────
    // 🔴 An exposure of Key/L renders EVERY scene at the same mid-grey, which means a starlit field and a
    //    beach at noon arrive at the screen looking identical. That is not what eyes do. Below roughly the
    //    luminance of a dim interior the retina switches from cone to rod vision and stops compensating fully:
    //    a night scene genuinely looks darker, not merely bluer, and that incomplete compensation is the whole
    //    reason a night sky reads as night instead of as grey daylight.
    //
    //    So the target grey itself falls with the adapted luminance, as a power law below the photopic level
    //    and not at all above it. At and above PhotopicLuminance the key is exactly KeyValue, so every image
    //    made before this existed is reproduced unchanged — the curve only does anything in the dark.
    //
    //    ⚠️ This is also what makes stars visible. Metered correctly, a night sky sits near 1e-4 cd/m²; a
    //    full-compensation exposure would render it at 0.18 mid-grey and the stars, at 0.4 cd/m², would be a
    //    barely brighter grey on top of it. With the curve the sky renders near 0.007 and the same stars come
    //    through at several times white — points of light on black, which is what a night sky is.
    float PhotopicLuminance = 5.0f;    // [cd/m²] at and above this the eye is fully light-adapted
    float ScotopicExponent  = 0.30f;   // [-]     0 = no dark adaptation at all, 1 = night renders as day

    // ── Colour at low light ─────────────────────────────────────────────────────────────────────────────────
    // 🔴 Cones stop responding before rods do, so below about 3 cd/m² colour drains out of what you see and by
    //    0.003 it is gone entirely — you can still make out a landscape at midnight, but not what colour it is.
    //    Rendering full saturation down there is what turns a faint pre-dawn glow into a lurid orange band:
    //    measured on the deep-twilight sky, the horizon at 15° below the horizon is 0.078 cd/m² and every bit
    //    of it is red, so the red channel saturates while blue stays black.
    //
    //    The ramp is in LOG luminance, because that span is three orders of magnitude and a linear ramp would
    //    spend almost all of itself in the top decade and switch colour off like a light.
    float ScotopicCeiling   = 3.0f;    // [cd/m²] at and above this, colour is complete
    float ScotopicFloor     = 0.003f;  // [cd/m²] at and below this, vision is achromatic
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class ExposureIntegrator
{
public:
    void AssignConfiguration(const ExposureConfiguration& Value) noexcept { Config = Value; }
    [[nodiscard]] const ExposureConfiguration& QueryConfiguration() const noexcept { return Config; }

    // The frame's measured average log luminance, as produced by the reduction pass. Supplying it separately
    //    from Advance keeps this testable without a GPU: the whole adaptation curve is exercised by feeding a
    //    sequence of measurements.
    void ObserveLuminance(float AverageLogLuminance) noexcept;

    // What the adaptation is actually chasing: the frame reading, eased over time.
    [[nodiscard]] float QueryObservedLuminance() const noexcept { return ObservedLuminance; }

    // Ease the adapted value toward the observed one. Δτ is the frame time.
    void Advance(float DeltaSeconds) noexcept;

    // What the tone map should use this frame. This is the ONLY value the renderer reads, so Manual and
    //    Adaptive cannot diverge into two code paths.
    [[nodiscard]] float QueryExposure() const noexcept;

    // The adapted scene luminance, for display.
    [[nodiscard]] float QueryAdaptedLuminance() const noexcept { return AdaptedLuminance; }

    // How much colour the eye still has at the adapted level: 1 in daylight, 0 under starlight. The tone map
    //    mixes toward grey by this, which is what keeps a faint glow faint instead of lurid.
    [[nodiscard]] float QueryColourSaturation() const noexcept;

    // Jump straight to the measurement, with no easing. For a camera cut or a scene load, where easing would
    //    show the viewer several seconds of the previous scene's exposure.
    void Snap() noexcept { AdaptedLuminance = ObservedLuminance; }

    // Exposure that would render a scene of the given luminance at the key value for that luminance.
    [[nodiscard]] static float ExposureForLuminance(float Luminance, const ExposureConfiguration& Config) noexcept;

    // The mid-grey a scene of this luminance should be rendered to. Constant in daylight, falling in the dark.
    //    Exposed separately because it is the one part of the curve with a claim worth asserting on its own.
    [[nodiscard]] static float KeyForLuminance(float Luminance, const ExposureConfiguration& Config) noexcept;

private:
    ExposureConfiguration Config{};
    float FrameLuminance    = 0.18f;   // [cd/m²] the most recent reading from the histogram
    float ObservedLuminance = 0.18f;   // [cd/m²] what the adaptation chases
    float AdaptedLuminance  = 0.18f;   // [cd/m²] what the eye currently believes
};

} // namespace Frontier
