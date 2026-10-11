//============================================================================================================================================
// 📦 Engine/DisplayPresentation/CloudShadowStaging.h — single-sourced cloud-shadow weather: diorama vs kilometre deck
//============================================================================================================================================
// One staging struct, two weather instants, shared by the CPU showcase and the GPU packers so both stage
//    identically. The ALGORITHM (VolumetricMedia::SunTransmittanceAt / CloudShadow.slang: 8 taps, same FBM, same
//    cumulus profile, extinction 0.01, exp(-OD), OD>4 break) is panel-exact on both sides — this header only stages
//    its INPUTS, the way the sun hour stages the solar input.
//
//    WHY TWO INSTANTS. The panel calibration (Prepare(), CelestialSequence.cpp:269-279: base 1400 m, 1100 m thick,
//    coverage 0.34, density 2.4, scale 0.35) spans 315 m features over a kilometre deck — physically correct for a
//    kilometre world. The Project-Zero showcase is a 60 m diorama whose visible ground is 46 m (rows 240-470,
//    y in [-60,-14], measured by row-dump 2026-09-15): under 2 lobes at kilometre scale, so the panel deck is
//    invisible there by physics, not by bug. The showcase therefore stages a diorama deck (inputs only), while
//    every other level keeps the panel staging. The GPU packer picks by level (see CelestialSequence).
//
//    WHY THESE DIORAMA VALUES (swept, not guessed — standalone trapezoid probe + renderer meter, 2026-09-15):
//    scale 0.05 (22 m features, ~2 lobes across the visible 46 m), coverage 0.55, density 4.0 (the thin 200 m
//    slab needs ~1.7x the kilometre density: OD per tap scales with path length, and 2.4 leaves only veil).
//    t = 40 s: in-trapezoid min 0.072 (dark puff, bottom-centre of frame) / max 0.743 (bright gap, mid-left) /
//    range 0.67; renderer band meter x0.70 mean, +27% spatial std vs OFF. FIN3 frame is the proof.
//
//    TIME BASE (CPU/GPU parity): seconds since local midnight, float32-exact (< 2^24 s). The CPU takes it from
//    --cloudtime; the GPU derives it from its clock (hours * 3600). --cloudtime 40 == GPU 00:00:40. Parity is BY
//    INSTANT (same seconds -> same pattern), not by default: the GPU clock runs free while the CPU defaults to
//    the frozen FIN3 instant below.

#pragma once

#include <cstdint>

namespace Frontier {

// Cloud type category, mirrored as uint (Cumulus = 2, matching CloudTypeCategory's order).
// The full enum lives in VolumetricMedia.h; this header stays dependency-free so Slang-side
// tooling and record packers can include it without pulling the march.

struct CloudShadowStaging
{
    bool     Enabled;        // [-] master switch (false packs Enabled01 = 0, shader early-outs to T = 1)
    uint32_t Type;          // [-] cloud type category (2 = Cumulus)
    float    Base;          // [m] slab bottom altitude
    float    Thickness;     // [m] slab vertical extent
    float    Coverage;      // [-] cloud fraction 0..1 (threshold = 1 - coverage)
    float    Density;       // [-] droplet multiplier inside cloud body
    float    Scale;         // [-] feature scale (features ~ 1/(Scale*900) m)
    float    Anvil;         // [0..1] cumulonimbus spreading (CPU CloudDensity default 0.5)
    float    CeilingMetres;  // [m] no cloud above this, ever (CPU SlabExtent clamp 14000)
    float    WindSpeed;     // [m/s] surface wind speed feeding the drift fold
    float    WindBearing;   // [deg] surface wind bearing feeding the drift fold
    float    TimeSeconds;   // [s] weather instant, seconds since local midnight
};

// Panel kilometre deck, restated (CelestialSequence.cpp Prepare():269-279 — the three-times-swept calibration
//    that keeps 80 of 81 zenith columns out from under cloud at frame-top geometry). Non-showcase levels use this.
inline constexpr CloudShadowStaging kCloudShadowPanelKm{ true, 2u, 1400.0f, 1100.0f, 0.34f, 2.4f, 0.35f, 0.5f, 14000.0f, 7.0f, 250.0f, 0.0f };

// Showcase diorama deck (FIN3-thin, 2026-09-19): visible broken cloud shadow on the ground WITHOUT eating the sun.
//    The original FIN3 staging (coverage 0.55, density 4.0) measured transmittance 0.072..0.743 (mean x0.70) — swept
//    for drama when the panels were the key light. With the sun now the Showcase's key light (the shadow-diagnosis
//    rebalance: panel luminance cut 140/60 → 8/4, SunDirect default 2.5), that deck made sun shadows a 2–3%
//    modulation nobody could see. Thinned to coverage 0.35 / density 2.5 so transmittance stays ≥ ~0.6 under the
//    puffs: the broken-cloud character survives, the sun's shadows read. Same scale/wind/time — the pattern and
//    drift are unchanged, only its optical depth.
inline constexpr CloudShadowStaging kCloudShadowShowcaseDiorama{ true, 2u, 250.0f, 200.0f, 0.35f, 2.5f, 0.05f, 0.5f, 14000.0f, 7.0f, 250.0f, 40.0f };

} // namespace Frontier
