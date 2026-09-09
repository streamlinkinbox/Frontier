//============================================================================================================================================
//                                                      EXPOSUREINTEGRATOR.CPP
//============================================================================================================================================

#include "ExposureIntegrator.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

namespace {

// Smoothstep, so the handover between the two readings has no corner in it. A hard switch would pop the moment
//    the camera crossed a threshold, which is the very complaint this is answering.
float Ease(float Edge0, float Edge1, float Value) noexcept
{
    if (Edge1 <= Edge0) return Value >= Edge1 ? 1.0f : 0.0f;
    const float T = std::clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

} // namespace

void ExposureIntegrator::ObserveIlluminance(float AnchorLuminance) noexcept
{
    // Used as given. The solver already answers "what will the camera be shown, on average, from here" — the
    //    conversion this used to do turned that into "what does an 18 % card under this light read", which is a
    //    different question and the wrong one whenever the sky is not overhead.
    IncidentLuminance = (std::isfinite(AnchorLuminance) && AnchorLuminance > 0.0f) ? AnchorLuminance : 0.0f;
    Reconcile();
}

void ExposureIntegrator::Reconcile() noexcept
{
    if (!Config.IncidentMetering || IncidentLuminance <= 0.0f)
    {
        ObservedLuminance = FrameLuminance;   // the pre-A7e path, and the identity switch
        return;
    }

    // 🔴 The dead zone is the whole mechanism. While the frame agrees with the incident reading, the incident
    //    reading is used ALONE — so moving the camera about outdoors changes the frame, changes nothing that
    //    reaches the exposure, and the sky holds absolutely still. A blend that always mixed in some frame
    //    would only have made the drift smaller, and smaller was not what was asked for.
    const float Disagreement = std::fabs(std::log2(std::max(FrameLuminance, 1.0e-9f)
                                                 / std::max(IncidentLuminance, 1.0e-9f)));
    const float Weight = Ease(Config.IncidentDeadZoneStops, Config.IncidentHandoverStops, Disagreement);

    // Interpolated in LOG space: these differ by orders of magnitude, and a linear mix of 1e4 and 1e-1 is 1e4.
    const float Blended = std::log(IncidentLuminance) * (1.0f - Weight) + std::log(FrameLuminance) * Weight;
    ObservedLuminance = std::exp(Blended);
}

void ExposureIntegrator::ObserveLuminance(float AverageLogLuminance) noexcept
{
    // The reduction pass reports a LOG mean; converting back here rather than there keeps the shader's job to
    //    one sum and keeps the exponential in the one place that also owns the clamping.
    const float Linear = std::exp(AverageLogLuminance);

    // A NaN would propagate into the adapted value and never leave — every subsequent frame would compare
    //    against it and stay NaN, so the screen would go black permanently rather than for one frame.
    FrameLuminance = (std::isfinite(Linear) && Linear > Config.LuminanceFloor) ? Linear : Config.LuminanceFloor;
    Reconcile();
}

void ExposureIntegrator::Advance(float DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    // Asymmetric: the direction of change picks the time constant. Brightening is the fast one because a viewer
    //    expects a bright doorway to resolve almost at once, while dark adaptation genuinely takes seconds.
    const bool  Brightening = ObservedLuminance > AdaptedLuminance;
    const float TimeConstant = Brightening ? Config.BrightenSeconds : Config.DarkenSeconds;

    // Exponential approach, framed so the result is INDEPENDENT OF FRAME RATE. The naive
    //    `Adapted += (Observed − Adapted) * Rate * Δτ` adapts faster at high frame rates, which means the look
    //    of a transition changes with the hardware it runs on. 1 − e^(−Δτ/τ) does not.
    const float Blend = (TimeConstant > 1.0e-4f)
                      ? 1.0f - std::exp(-DeltaSeconds / TimeConstant)
                      : 1.0f;

    // Interpolate in LOG space. In linear space a move from 0.001 to 1.0 spends almost all its time in the last
    //    few percent of the numeric range while the visible brightness barely moves, so the transition appears
    //    to stall and then snap.
    const float LogAdapted  = std::log(std::max(AdaptedLuminance,  Config.LuminanceFloor));
    const float LogObserved = std::log(std::max(ObservedLuminance, Config.LuminanceFloor));
    AdaptedLuminance = std::exp(LogAdapted + (LogObserved - LogAdapted) * Blend);
}

float ExposureIntegrator::KeyForLuminance(float Luminance, const ExposureConfiguration& Config) noexcept
{
    const float Safe = std::max(Luminance, Config.LuminanceFloor);
    if (Safe >= Config.PhotopicLuminance) return Config.KeyValue;   // full daylight adaptation, unchanged

    // A power law rather than a straight line, because perceived brightness follows the ratio of luminances,
    //    not their difference. With the exponent at 0 this returns the constant key and the whole feature is
    //    off, which is the identity switch back to the pre-A7c curve.
    const float Ratio = Safe / Config.PhotopicLuminance;
    return Config.KeyValue * std::pow(Ratio, Config.ScotopicExponent);
}

float ExposureIntegrator::ExposureForLuminance(float Luminance, const ExposureConfiguration& Config) noexcept
{
    const float Safe = std::max(Luminance, Config.LuminanceFloor);
    return std::clamp(KeyForLuminance(Safe, Config) / Safe, Config.MinimumExposure, Config.MaximumExposure);
}

float ExposureIntegrator::QueryColourSaturation() const noexcept
{
    // Manual mode is an identity switch for the whole adaptive path, and that has to include this: an image
    //    made before the curve existed must still be reproducible exactly.
    if (Config.Mode == ExposureModeCategory::Manual) return 1.0f;

    const float Floor   = std::max(Config.ScotopicFloor, 1.0e-9f);
    const float Ceiling = std::max(Config.ScotopicCeiling, Floor * 1.001f);
    const float Low     = std::log(Floor), High = std::log(Ceiling);
    const float Here    = std::log(std::max(AdaptedLuminance, 1.0e-9f));
    return std::clamp((Here - Low) / (High - Low), 0.0f, 1.0f);
}

float ExposureIntegrator::QueryExposure() const noexcept
{
    if (Config.Mode == ExposureModeCategory::Manual) return Config.ManualExposure;
    return ExposureForLuminance(AdaptedLuminance, Config);
}

} // namespace Frontier
