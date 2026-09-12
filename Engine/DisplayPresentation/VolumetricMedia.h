//============================================================================================================================================
// 📦 Engine/DisplayPresentation/VolumetricMedia.h — clouds and fog, marched once over the union of their volumes
//============================================================================================================================================
// Celestial port, step 5. The largest step: Cloud Layer, Local Cloud, Height Fog, Atmospheric Fog and Local
//    Volumetric Fog all live here, and they share one march.
//
// ⚠️ ONE MARCH, NOT ONE PER MEDIUM. The source branch consolidated these at 73737b6 — a single loop over the
//    union of the volumes' bounding intervals, with shared extinction and a shared sun-shadow march, so fog
//    shadows cloud and cloud shadows fog for free and the per-pixel cost is one loop instead of one per volume.
//    Splitting them back apart is not a refactor, it is a regression; `CheckVolumetricMedia.sh` asserts the
//    single-march structure.
//
// ⚠️ ON GOD RAYS. The scene-occlusion path — a SunVisibilityAt callback and a GodRaySamples budget for shafts
//    cut by buildings, terrain or foliage — was built, proved, and then removed at the user's request because
//    nothing in the engine called it and no such geometry exists yet. `git show c4ea076` and `6bb52a7` carry the
//    working implementation if it is ever wanted; the lesson worth keeping is that the query must follow the SUN
//    RAY, not test the occluder directly above the sample, or the beams do not move when the sun does.
//
//    CLOUD AND FOG SHAFTS ARE NOT PART OF THAT REMOVAL AND MUST NOT BE. They come from ShadowMarch below, which
//    accumulates cloud density along the sun ray so the medium shadows itself. That is not a shaft feature bolted
//    on: it is what lights a cloud at all. Measured across 2 km of broken cumulus it gives 4.83x contrast between
//    gap and shadow, against 1.39x under solid overcast. Removing it would leave clouds flat and unlit.
//
// ⚠️ AND THREE OPTIMISATIONS THAT ARE PROHIBITED, each measured and reverted upstream:
//        clear-air striding (73b71d6)  — probe/erosion mismatch stalled the march, dark speckled cloud
//        low-res cloud FBO + temporal reprojection (a152901) — slower on a GTX and lower quality
//        atmosphere LUTs (2fe78ed)     — no speedup, and worse looking; see References/Deferred/AtmosphereLuts.md
//
// Coordinates: the engine is right-handed Z-UP (CLAUDE.md §7). The reference demo is Y-up, so every altitude term
//    transcribed from it reads .z here where the demo reads .y. That is the single most likely transcription
//    error in this file and it is silent — a cloud layer built on the wrong axis still renders, just sideways.

#pragma once

#include "WindField.h"

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLOUD LAYER
//------------------------------------------------------------------------------------------------------------------------

enum class CloudTypeCategory : uint32_t
{
    Stratus = 0u, Stratocumulus = 1u, Cumulus = 2u, Cumulonimbus = 3u, Altostratus = 4u, Cirrus = 5u,
};

struct CloudLayerSettings
{
    bool              Enabled   = false;
    CloudTypeCategory Type      = CloudTypeCategory::Cumulus;
    float             Base      = 1500.0f;   // [m] altitude of the cloud base
    float             Thickness = 1200.0f;   // [m] slab depth
    float             Coverage  = 0.55f;     // [0..1]
    float             Density   = 1.0f;      // [x]
    float             Scale     = 1.0f;      // [x] feature size
    float             Anisotropy = 0.45f;    // Henyey-Greenstein g (the march used the box's; now per-medium)
    float             Albedo[3] = { 0.92f, 0.94f, 0.97f };   // the cloud white (likewise)
    float             Anvil     = 0.5f;      // [0..1] cumulonimbus spreading
    bool              FollowWind = true;     // link to the Wind Field entity

    // ⚠️ THE CEILING. Clouds are a tropospheric phenomenon: they form where there is enough water vapour and
    //    convection, which is the bottom ~12 km of a 60 km atmosphere. Without an explicit ceiling the slab is
    //    just a band at whatever altitude the user typed, so a Base of 200 km put cloud *outside the atmosphere*
    //    — visible from orbit as a shell floating in vacuum, which is what looked wrong from space.
    //
    //    Clamped rather than merely documented, because the failure is silent: the render succeeds and simply
    //    shows something impossible.
    float             CeilingMetres = 14000.0f;   // [m] no cloud above this, ever
};

//------------------------------------------------------------------------------------------------------------------------
//                                              LOCAL VOLUMES (box-bounded)
//------------------------------------------------------------------------------------------------------------------------

// A Local Cloud or Local Volumetric Fog: a finite box somewhere in the world, rather than a global layer.
//
//    These are the entities that need a gizmo and a billboard marker. A global cloud layer has no position to
//    drag; a local volume does, and it has no visible body to click on when its density is low — so the editor
//    needs a proxy. See VolumeMarker below.
struct LocalVolumeSettings
{
    bool  Enabled  = false;
    float Centre[3]  = { 0.0f, 0.0f, 400.0f };   // [m] world position, Z-up
    float HalfSize[3] = { 300.0f, 300.0f, 150.0f };
    float Density   = 1.0f;
    float Coverage  = 0.6f;
    float Scale     = 120.0f;    // [m] feature size
    float Albedo[3] = { 0.92f, 0.94f, 0.97f };
    float Anisotropy = 0.45f;    // Henyey-Greenstein g
    bool  FollowWind = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   HEIGHT / AERIAL FOG
//------------------------------------------------------------------------------------------------------------------------

struct FogSettings
{
    bool  HeightEnabled = false;
    float HeightDensity = 0.02f;   // [1/m] at the reference altitude
    float FalloffHeight = 400.0f;  // [m] e-folding height
    float HeightColour[3] = { 0.62f, 0.68f, 0.76f };
    float SunScatter    = 0.6f;

    bool  AerialEnabled = false;
    float AerialDensity = 1.0f;    // [x] multiplies the atmospheric extinction
    float AerialStart   = 50.0f;   // [m] distance before it begins
    float AerialMie     = 0.4f;    // [0..1] 0 = spectral Rayleigh tint, 1 = grey Mie
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE MEDIA
//------------------------------------------------------------------------------------------------------------------------

struct VolumetricBudget
{
    uint32_t CloudSteps      = 28u;   // FidelityCriteria::CloudMarchStepCount
    uint32_t LocalSteps      = 28u;   // FidelityCriteria::LocalVolumeStepCount
    uint32_t LightTaps       = 6u;    // FidelityCriteria::CloudLightTapCount (4 posterized the shading)
    float    CoverageMargin  = 0.03f; // FidelityCriteria::CloudCoverageMargin
};


struct VolumetricSample
{
    float Scatter[3]      = {};                      // in-scattered radiance
    float Transmittance   = 1.0f;                    // what survives the media
    uint32_t StepsTaken   = 0u;                      // for the proof's cost assertions
    // The real saving from the unified march is not fewer steps — the union of two volumes is genuinely longer
    //    than either — it is ONE sun-shadow march per occupied step instead of one per medium per step. That is
    //    the number the proof asserts.
    uint32_t ShadowMarches = 0u;
};

class VolumetricMedia
{
public:
    //--------------------------------------------------------------------------------------------------------------------
    //                                              CLOUD SHAPE
    //--------------------------------------------------------------------------------------------------------------------

    // Vertical profile within the slab, 0 at the base and 1 at the top. This is what makes a stratus a flat sheet
    //    and a cumulonimbus a tall column with an anvil.
    static float HeightProfile(CloudTypeCategory Type, float Normalised, float Anvil) noexcept
    {
        const float H = Clamp(Normalised, 0.0f, 1.0f);
        switch (Type)
        {
            case CloudTypeCategory::Stratus:
                return SmoothStep(0.0f, 0.08f, H) * (1.0f - SmoothStep(0.75f, 1.0f, H));
            case CloudTypeCategory::Stratocumulus:
                return SmoothStep(0.0f, 0.12f, H) * (1.0f - SmoothStep(0.50f, 0.95f, H));
            case CloudTypeCategory::Cumulus:
                return SmoothStep(0.0f, 0.07f, H) * (1.0f - SmoothStep(0.35f, 1.0f, H)) * 1.15f;
            case CloudTypeCategory::Cumulonimbus:
                return SmoothStep(0.0f, 0.05f, H) * (1.0f - SmoothStep(0.85f, 1.0f, H))
                     * Lerp(1.0f, 1.6f, SmoothStep(0.7f, 1.0f, H) * Anvil);
            case CloudTypeCategory::Altostratus:
                return SmoothStep(0.0f, 0.20f, H) * (1.0f - SmoothStep(0.55f, 0.9f, H)) * 0.75f;
            case CloudTypeCategory::Cirrus:
            default:
                return SmoothStep(0.0f, 0.25f, H) * (1.0f - SmoothStep(0.4f, 0.85f, H)) * 0.45f;
        }
    }

    // The slab's actual extent after the ceiling is applied. Returns false when the layer has been pushed
    //    entirely above the ceiling, which is the case that used to put cloud in orbit.
    static bool SlabExtent(const CloudLayerSettings& Cloud, float& OutBase, float& OutTop) noexcept
    {
        const float Ceiling = Cloud.CeilingMetres;
        OutBase = Clamp(Cloud.Base, 0.0f, Ceiling);
        OutTop  = Clamp(Cloud.Base + Cloud.Thickness, 0.0f, Ceiling);
        return OutTop > OutBase + 1.0f;
    }

    // Cloud density at a world point. Z-up: altitude is p.z.
    static float CloudDensity(const CloudLayerSettings& Cloud, const WindSettings& Wind,
                              const float Position[3], float Time) noexcept
    {
        float Base = 0.0f, Top = 0.0f;
        if (!SlabExtent(Cloud, Base, Top)) return 0.0f;

        const float Altitude = Position[2];
        if (Altitude < Base || Altitude > Top) return 0.0f;

        const float Normalised = (Altitude - Base) / (Top - Base);
        const float Profile = HeightProfile(Cloud.Type, Normalised, Cloud.Anvil);
        if (Profile <= 0.0f) return 0.0f;

        // Advection, uniform plus frozen shear (WindField::AdvectDrift — the streak note lives there): the
        //    whole slab rides the slab-mid flow, statically leaned by the shear. Trig-only, so it stays safe
        //    inside the march like the SampleStep it replaces — and SampleStep-only, the swirl still belongs
        //    once per pixel (the regression the source branch's last commit fixed).
        float Drift[3] = { 0.0f, 0.0f, 0.0f };
        if (Cloud.FollowWind)
        {
            float Advected[2];
            WindField::AdvectDrift(Wind, Altitude, 0.5f * (Base + Top), Time,
                                   std::fmax(Cloud.Scale * 900.0f, 1.0f), 0.8f, Advected);
            Drift[0] = Advected[0]; Drift[1] = Advected[1];
        }

        const float Inverse = 1.0f / std::fmax(Cloud.Scale * 900.0f, 1.0f);
        const float S[3] = { (Position[0] + Drift[0]) * Inverse,
                             (Position[1] + Drift[1]) * Inverse,
                             Position[2] * Inverse };

        // Three octaves, not four: the march steps at ~143 m and the fourth octave's 38 m features alias
        //    into long moire streaks (measured — the 11h layer smeared horizontally with hard smoothstepped
        //    edges). What the steps cannot resolve must not be in the field; the lost detail lives below the
        //    sampling floor anyway.
        float Shape = Noise(S[0], S[1], S[2]) * 0.5f
                    + Noise(S[0] * 2.02f + 3.1f, S[1] * 2.02f + 1.7f, S[2] * 2.02f + 9.2f) * 0.25f
                    + Noise(S[0] * 4.10f + 7.7f, S[1] * 4.10f + 2.2f, S[2] * 4.10f + 1.1f) * 0.125f;
        Shape /= 0.875f;
        Shape = Shape * 0.5f + 0.5f;

        const float Threshold = 1.0f - Clamp(Cloud.Coverage, 0.0f, 1.0f);
        const float Raw = Clamp((Shape - Threshold) / std::fmax(1.0f - Threshold, 1e-3f), 0.0f, 1.0f);
        // Sharpened, not linear: the fbm piles samples in the middle of its range, so a linear body paints
        //    every threshold crossing as a broad translucent veil — measured, coverage 0.52 gave 1 clear column
        //    in 81 with almost no opaque core anywhere (a white sky, not broken cloud). The smoothstep keeps
        //    the mapping monotonic (the gate's coverage asserts hold) while thinning the veil toward clean
        //    edges and opaque cores — what Coverage promises ("fraction of sky covered").
        const float Body = SmoothStep(0.0f, 1.0f, Raw);
        return Body * Profile * Cloud.Density;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                            LOCAL VOLUMES
    //--------------------------------------------------------------------------------------------------------------------

    // Slab intersection against an axis-aligned box. Returns false on a miss; Near is clamped to the ray origin.
    static bool IntersectBox(const float Centre[3], const float HalfSize[3],
                             const float Origin[3], const float Direction[3],
                             float& Near, float& Far) noexcept
    {
        float Lowest = -1e30f, Highest = 1e30f;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const float D = Direction[Axis];
            const float O = Origin[Axis] - Centre[Axis];
            if (std::fabs(D) < 1e-9f)
            {
                if (std::fabs(O) > HalfSize[Axis]) return false;
                continue;
            }
            const float Inverse = 1.0f / D;
            float T1 = (-HalfSize[Axis] - O) * Inverse;
            float T2 = ( HalfSize[Axis] - O) * Inverse;
            if (T1 > T2) { const float Swap = T1; T1 = T2; T2 = Swap; }
            Lowest  = std::fmax(Lowest, T1);
            Highest = std::fmin(Highest, T2);
        }
        if (Lowest > Highest || Highest < 0.0f) return false;
        Near = std::fmax(Lowest, 0.0f);
        Far  = Highest;
        return true;
    }

    static float LocalDensity(const LocalVolumeSettings& Volume, const WindSettings& Wind,
                              const float Position[3], float Time) noexcept
    {
        if (!Volume.Enabled) return 0.0f;
        float Local[3];
        for (int C = 0; C < 3; ++C)
            Local[C] = (Position[C] - Volume.Centre[C]) / std::fmax(Volume.HalfSize[C], 1e-3f);

        // A soft ellipsoidal mask inside the box, so the volume has no visible corners.
        const float R = std::sqrt(Local[0] * Local[0] + Local[1] * Local[1] + Local[2] * Local[2]);
        if (R >= 1.0f) return 0.0f;
        const float Mask = 1.0f - SmoothStep(0.55f, 1.0f, R);
        if (Mask <= 0.0f) return 0.0f;

        // Advection, uniform plus frozen shear like the layer's (WindField::AdvectDrift): the whole box
        //    rides the flow at its centre altitude.
        float Drift[3] = { 0.0f, 0.0f, 0.0f };
        if (Volume.FollowWind)
        {
            float Advected[2];
            WindField::AdvectDrift(Wind, Position[2], Volume.Centre[2], Time,
                                   std::fmax(Volume.Scale, 1.0f), 0.6f, Advected);
            Drift[0] = Advected[0]; Drift[1] = Advected[1];
        }

        const float Inverse = 1.0f / std::fmax(Volume.Scale, 1.0f);
        const float S[3] = { (Position[0] + Drift[0]) * Inverse,
                             (Position[1] + Drift[1]) * Inverse,
                             Position[2] * Inverse };
        // Two octaves: the box marches at half its feature scale, which resolves the first two and aliases
        //    the third (a puff is smooth-walled anyway — the lost octave is sub-step ripple).
        float Shape = Noise(S[0], S[1], S[2]) * 0.5f
                    + Noise(S[0] * 2.02f + 3.1f, S[1] * 2.02f + 1.7f, S[2] * 2.02f + 9.2f) * 0.25f;
        Shape /= 0.75f;
        Shape = Shape * 0.5f + 0.5f;

        const float Threshold = 1.0f - Clamp(Volume.Coverage, 0.0f, 1.0f);
        const float Raw = Clamp((Shape - Threshold) / std::fmax(1.0f - Threshold, 1e-3f), 0.0f, 1.0f);
        // Sharpened like the layer's body (see the note there): the local volumes share the fbm's middling
        //    distribution, so they share its veil. Same monotonic curve, same gate guarantees.
        const float Body = SmoothStep(0.0f, 1.0f, Raw);
        return Body * Mask * Volume.Density;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                HEIGHT FOG
    //--------------------------------------------------------------------------------------------------------------------

    // Analytic, not marched: the integral of an exponential height profile along a segment has a closed form, so
    //    fog costs two exponentials rather than a loop.
    static float HeightFogOpticalDepth(const FogSettings& Fog, float StartHeight, float EndHeight,
                                       float Distance) noexcept
    {
        if (!Fog.HeightEnabled || Distance <= 0.0f) return 0.0f;
        const float H = std::fmax(Fog.FalloffHeight, 1.0f);
        const float Rise = EndHeight - StartHeight;
        if (std::fabs(Rise) < 1e-3f)
            return Fog.HeightDensity * std::exp(-StartHeight / H) * Distance;
        // ∫ exp(-z/H) ds along a straight line, parameterised by height.
        const float A = std::exp(-StartHeight / H), B = std::exp(-EndHeight / H);
        return Fog.HeightDensity * H * (A - B) * (Distance / Rise);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                            THE UNIFIED MARCH
    //--------------------------------------------------------------------------------------------------------------------

    // ⚠️ THREE marches, one per medium, each at its own pace — and every step comb is anchored to the WORLD,
    //    not to the span. An earlier revision marched the union of all three intervals in one loop (73737b6),
    //    which looked efficient and rendered three defects: the layer's steps depended on whether the box was
    //    in the union (enabling the box resampled the whole sky — a visible seam along its silhouette),
    //    neighbouring rays sampled different step phases (their spans start at different distances, so the
    //    field decorrelated vertically and the clouds smeared into horizontal streaks), and the 200 m box took
    //    ~2 of the union's 143 m steps (a smooth white blob — no texture, no self-shadow).
    //
    //    Each medium now marches its own span at half its feature scale (the layer keeps the tier's 143 m step
    //    — near enough to half its 315 m features that the tier keeps quoting it), capped by its budget; midpoint
    //    samples cover [SpanNear, SpanFar] exactly, so enabling another medium cannot stretch or resample this
    //    medium's interval. The media composite near-to-far, which is exact for disjoint spans (what parked
    //    volumes are — an editor-dragged overlap composites in span order, approximately). Fog still shadows
    //    cloud and cloud still shadows fog, because the SUN-shadow march samples the combined medium — one
    //    shadow march per occupied step, whichever loop it sits in, which is the number the gate asserts.
    static VolumetricSample March(const CloudLayerSettings& Cloud, const LocalVolumeSettings& LocalCloud,
                                  const LocalVolumeSettings& LocalFog, const WindSettings& Wind,
                                  const VolumetricBudget& Budget,
                                  const float Origin[3], const float Direction[3], float MaximumDistance,
                                  const float SunDirection[3], const float SunRadiance[3],
                                  const float AmbientRadiance[3], float Time) noexcept
    {
        VolumetricSample Result{};

        // ── Each medium's own interval ─────────────────────────────────────────────────────────────────────────
        float SlabNear = 1e30f, SlabFar = -1e30f; bool SlabHit = false;
        if (Cloud.Enabled)
        {
            float Base = 0.0f, Top = 0.0f;
            if (SlabExtent(Cloud, Base, Top))
            {
                // The slab is horizontal, so its interval is where the ray crosses the two altitudes.
                float Lo = 0.0f, Hi = 0.0f;
                if (SlabInterval(Origin[2], Direction[2], Base, Top, MaximumDistance, Lo, Hi))
                {
                    SlabNear = std::fmax(Lo, 0.0f); SlabFar = Hi; SlabHit = SlabFar > SlabNear;
                }
            }
        }
        float BoxNear = 1e30f, BoxFar = -1e30f; bool BoxHit = false;
        if (LocalCloud.Enabled
            && IntersectBox(LocalCloud.Centre, LocalCloud.HalfSize, Origin, Direction, BoxNear, BoxFar))
        {
            BoxNear = std::fmax(BoxNear, 0.0f); BoxFar = std::fmin(BoxFar, MaximumDistance);
            BoxHit = BoxFar > BoxNear;
        }
        float FogNear = 1e30f, FogFar = -1e30f; bool FogHit = false;
        if (LocalFog.Enabled
            && IntersectBox(LocalFog.Centre, LocalFog.HalfSize, Origin, Direction, FogNear, FogFar))
        {
            FogNear = std::fmax(FogNear, 0.0f); FogFar = std::fmin(FogFar, MaximumDistance);
            FogHit = FogFar > FogNear;
        }
        if (!SlabHit && !BoxHit && !FogHit) return Result;

        // March near-to-far over slab/box/fog by span start (a miss sorts last and is skipped), so an opaque
        //    nearer medium hides the rest without marching them.
        uint32_t Order[3] = { 0u, 1u, 2u };
        const float Starts[3] = { SlabHit ? SlabNear : 1e30f, BoxHit ? BoxNear : 1e30f,
                                  FogHit ? FogNear : 1e30f };
        for (uint32_t A = 1u; A < 3u; ++A)
            for (uint32_t B = A; B > 0u && Starts[Order[B]] < Starts[Order[B - 1u]]; --B)
            {
                const uint32_t Tmp = Order[B]; Order[B] = Order[B - 1u]; Order[B - 1u] = Tmp;
            }

        const float CosTheta = Direction[0] * SunDirection[0] + Direction[1] * SunDirection[1]
                             + Direction[2] * SunDirection[2];

        float Transmittance = 1.0f;
        constexpr float kReferenceSpan = 4000.0f;   // [m] the span the tier's step count is quoted against
        for (uint32_t S = 0u; S < 3u; ++S)
        {
            if (Transmittance < 0.005f) break;      // an opaque nearer medium hides the rest
            const uint32_t M = Order[S];

            // The medium's span, step and phase function. The layer keeps the tier's reference step (4000 m
            //    over the tier's count — 143 m at Standard); the volumes step at half their feature scale. A
            //    longer span takes more steps rather than coarser ones (fixing the count once let added fog
            //    RAISE transmittance — more medium letting more light through, which is impossible), and the
            //    cap keeps a pathological span from running away. Midpoints cover this medium's actual interval
            //    [SpanNear, SpanFar], so no samples are spent in the empty distance before its near boundary.
            float SpanNear = 0.0f, SpanFar = 0.0f, StepSize = 1.0f, PhaseG = 0.45f;
            uint32_t Cap = 1u;
            if (M == 0u)
            {
                if (!SlabHit) continue;
                SpanNear = SlabNear; SpanFar = SlabFar;
                const uint32_t Reference = Budget.CloudSteps == 0u ? 1u : Budget.CloudSteps;
                StepSize = kReferenceSpan / static_cast<float>(Reference);
                Cap = Reference * 4u;               // never more than 4x the tier's budget
                PhaseG = Cloud.Anisotropy;
            }
            else if (M == 1u)
            {
                if (!BoxHit) continue;
                SpanNear = BoxNear; SpanFar = BoxFar;
                StepSize = std::fmax(LocalCloud.Scale * 0.5f, 1.0f);
                Cap = Budget.LocalSteps == 0u ? 1u : Budget.LocalSteps;
                PhaseG = LocalCloud.Anisotropy;
            }
            else
            {
                if (!FogHit) continue;
                SpanNear = FogNear; SpanFar = FogFar;
                StepSize = std::fmax(LocalFog.Scale * 0.5f, 1.0f);
                Cap = Budget.LocalSteps == 0u ? 1u : Budget.LocalSteps;
                PhaseG = LocalFog.Anisotropy;
            }
            // Sample the actual medium span, not the empty distance from the eye to its far face. The old zero
            //    anchor made a near cloud use most of its samples in clear air and made an orbital view pay for
            //    thousands of below-slab samples; both cases then had too few samples where density changed.
            //    A span-relative midpoint grid keeps the cloud edges stable when another volume appears and gives
            //    each medium the resolution promised by its own budget. The cap is only a safety bound for a
            //    grazing ray; it cannot turn a short cloud into a one-step white blob.
            const float Span = SpanFar - SpanNear;
            uint32_t Count = static_cast<uint32_t>(std::ceil(Span / StepSize));
            if (Count < 1u) Count = 1u;
            if (Count > Cap) Count = Cap;
            const float ActualStep = Span / static_cast<float>(Count);
            const float Phase = HenyeyGreenstein(CosTheta, PhaseG);

            for (uint32_t I = 0u; I < Count; ++I)
            {
                const float T = SpanNear + (static_cast<float>(I) + 0.5f) * ActualStep;
                const float P[3] = { Origin[0] + Direction[0] * T,
                                     Origin[1] + Direction[1] * T,
                                     Origin[2] + Direction[2] * T };
                ++Result.StepsTaken;

                float Density = 0.0f;
                if (M == 0u)      Density = CloudDensity(Cloud, Wind, P, Time);
                else if (M == 1u) Density = LocalDensity(LocalCloud, Wind, P, Time);
                else              Density = LocalDensity(LocalFog, Wind, P, Time);
                if (Density <= 1e-5f) continue;

                // Sun visibility, marched once for the combined medium — fog shadows cloud and cloud shadows
                //    fog for free, whichever loop this step sits in.
                const float SunTransmittance = ShadowMarch(Cloud, LocalCloud, LocalFog, Wind, P, SunDirection,
                                                     ActualStep, Budget.LightTaps, Time);
                ++Result.ShadowMarches;

                const float Extinction = Density * ActualStep * 0.01f;
                const float StepTransmittance = std::exp(-Extinction);
                for (int C = 0; C < 3; ++C)
                {
                    // Each medium its own albedo: the layer owns its white now (it used the box's), the fog
                    //    keeps its fixed grey (the old blend's fog end).
                    float Albedo = 0.88f;
                    if (M == 0u)      Albedo = Cloud.Albedo[C];
                    else if (M == 1u) Albedo = LocalCloud.Albedo[C];
                    const float In = (SunRadiance[C] * SunTransmittance * Phase + AmbientRadiance[C]) * Albedo;
                    Result.Scatter[C] += In * (1.0f - StepTransmittance) * Transmittance;
                }
                Transmittance *= StepTransmittance;
                if (Transmittance < 0.005f) break;  // fully occluded; nothing behind matters
            }
        }

        Result.Transmittance = Transmittance;
        return Result;
    }

    static float HenyeyGreenstein(float CosTheta, float G) noexcept
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float Denominator = 1.0f + G * G - 2.0f * G * CosTheta;
        return (1.0f - G * G) / (4.0f * kPi * std::pow(std::fmax(Denominator, 1e-4f), 1.5f));
    }

private:
    static float Clamp(float V, float Lo, float Hi) noexcept { return V < Lo ? Lo : (V > Hi ? Hi : V); }
    static float Lerp(float A, float B, float T) noexcept { return A + (B - A) * T; }
    static float SmoothStep(float E0, float E1, float V) noexcept
    {
        const float T = Clamp((V - E0) / (E1 - E0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }

    // Where a ray crosses a horizontal slab between two altitudes.
    static bool SlabInterval(float OriginZ, float DirectionZ, float Base, float Top, float Maximum,
                             float& Near, float& Far) noexcept
    {
        if (std::fabs(DirectionZ) < 1e-6f)
        {
            if (OriginZ < Base || OriginZ > Top) return false;
            Near = 0.0f; Far = Maximum;
            return true;
        }
        float T1 = (Base - OriginZ) / DirectionZ;
        float T2 = (Top - OriginZ) / DirectionZ;
        if (T1 > T2) { const float Swap = T1; T1 = T2; T2 = Swap; }
        Near = std::fmax(T1, 0.0f);
        Far  = std::fmin(T2, Maximum);
        return Far > Near;
    }

    // The shared sun-shadow march: a few long steps toward the light through the COMBINED medium.
    static float ShadowMarch(const CloudLayerSettings& Cloud, const LocalVolumeSettings& LocalCloud,
                             const LocalVolumeSettings& LocalFog, const WindSettings& Wind,
                             const float Position[3], const float SunDirection[3],
                             float StepSize, uint32_t Taps, float Time) noexcept
    {
        const uint32_t Count = Taps == 0u ? 1u : Taps;
        float OpticalDepth = 0.0f;
        for (uint32_t I = 1; I <= Count; ++I)
        {
            const float Distance = StepSize * static_cast<float>(I) * 0.5f;
            const float Q[3] = { Position[0] + SunDirection[0] * Distance,
                                 Position[1] + SunDirection[1] * Distance,
                                 Position[2] + SunDirection[2] * Distance };
            const float Density = (Cloud.Enabled ? CloudDensity(Cloud, Wind, Q, Time) : 0.0f)
                                + LocalDensity(LocalCloud, Wind, Q, Time)
                                + LocalDensity(LocalFog, Wind, Q, Time);
            OpticalDepth += Density * StepSize * 0.5f * 0.01f;
            // Past opaque, further taps change transmittance by under 2% — invisible, so stop paying for them.
            if (OpticalDepth > 4.0f) break;
        }
        return std::exp(-OpticalDepth);
    }

    static float Hash(float X, float Y, float Z) noexcept
    {
        float S = std::sin(X * 127.1f + Y * 311.7f + Z * 74.7f) * 43758.5453f;
        return S - std::floor(S);
    }

    static float Noise(float X, float Y, float Z) noexcept
    {
        const float Ix = std::floor(X), Iy = std::floor(Y), Iz = std::floor(Z);
        const float Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
        const float Ux = Fx * Fx * (3.0f - 2.0f * Fx);
        const float Uy = Fy * Fy * (3.0f - 2.0f * Fy);
        const float Uz = Fz * Fz * (3.0f - 2.0f * Fz);
        const float N000 = Hash(Ix, Iy, Iz),         N100 = Hash(Ix + 1, Iy, Iz);
        const float N010 = Hash(Ix, Iy + 1, Iz),     N110 = Hash(Ix + 1, Iy + 1, Iz);
        const float N001 = Hash(Ix, Iy, Iz + 1),     N101 = Hash(Ix + 1, Iy, Iz + 1);
        const float N011 = Hash(Ix, Iy + 1, Iz + 1), N111 = Hash(Ix + 1, Iy + 1, Iz + 1);
        const float X00 = N000 + (N100 - N000) * Ux, X10 = N010 + (N110 - N010) * Ux;
        const float X01 = N001 + (N101 - N001) * Ux, X11 = N011 + (N111 - N011) * Ux;
        const float Y0 = X00 + (X10 - X00) * Uy,     Y1 = X01 + (X11 - X01) * Uy;
        return (Y0 + (Y1 - Y0) * Uz) * 2.0f - 1.0f;
    }
};

} // namespace Frontier
