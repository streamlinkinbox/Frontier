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
    uint32_t LightTaps       = 4u;    // FidelityCriteria::CloudLightTapCount
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

        // Advection. ⚠️ SampleStep only — this runs at every march step, and the swirl belongs once per pixel
        //    (WindField.h, and the regression the source branch's last commit fixed).
        float Drift[3] = { 0.0f, 0.0f, 0.0f };
        if (Cloud.FollowWind)
        {
            WindField::SampleStep(Wind, Altitude, Drift);
            Drift[0] *= Time * 0.8f;
            Drift[1] *= Time * 0.8f;
        }

        const float Inverse = 1.0f / std::fmax(Cloud.Scale * 900.0f, 1.0f);
        const float S[3] = { (Position[0] + Drift[0]) * Inverse,
                             (Position[1] + Drift[1]) * Inverse,
                             Position[2] * Inverse };

        float Shape = Noise(S[0], S[1], S[2]) * 0.5f
                    + Noise(S[0] * 2.02f + 3.1f, S[1] * 2.02f + 1.7f, S[2] * 2.02f + 9.2f) * 0.25f
                    + Noise(S[0] * 4.10f + 7.7f, S[1] * 4.10f + 2.2f, S[2] * 4.10f + 1.1f) * 0.125f
                    + Noise(S[0] * 8.30f + 1.3f, S[1] * 8.30f + 8.8f, S[2] * 8.30f + 4.4f) * 0.0625f;
        Shape /= 0.9375f;
        Shape = Shape * 0.5f + 0.5f;

        const float Threshold = 1.0f - Clamp(Cloud.Coverage, 0.0f, 1.0f);
        const float Body = Clamp((Shape - Threshold) / std::fmax(1.0f - Threshold, 1e-3f), 0.0f, 1.0f);
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

        float Drift[3] = { 0.0f, 0.0f, 0.0f };
        if (Volume.FollowWind)
        {
            WindField::SampleStep(Wind, Position[2], Drift);
            Drift[0] *= Time * 0.6f;
            Drift[1] *= Time * 0.6f;
        }

        const float Inverse = 1.0f / std::fmax(Volume.Scale, 1.0f);
        const float S[3] = { (Position[0] + Drift[0]) * Inverse,
                             (Position[1] + Drift[1]) * Inverse,
                             Position[2] * Inverse };
        float Shape = Noise(S[0], S[1], S[2]) * 0.5f
                    + Noise(S[0] * 2.02f + 3.1f, S[1] * 2.02f + 1.7f, S[2] * 2.02f + 9.2f) * 0.25f
                    + Noise(S[0] * 4.10f + 7.7f, S[1] * 4.10f + 2.2f, S[2] * 4.10f + 1.1f) * 0.125f;
        Shape /= 0.875f;
        Shape = Shape * 0.5f + 0.5f;

        const float Threshold = 1.0f - Clamp(Volume.Coverage, 0.0f, 1.0f);
        const float Body = Clamp((Shape - Threshold) / std::fmax(1.0f - Threshold, 1e-3f), 0.0f, 1.0f);
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

    // ⚠️ ONE loop over the union of every enabled volume's interval. Each medium contributes its own density and
    //    albedo; the extinction, the sun-shadow march and the light loop are SHARED, so fog shadows cloud and
    //    cloud shadows fog for free and the per-pixel cost is one march instead of one per volume (73737b6).
    //
    //    A second march would look almost identical in a still frame and cost double, which is why the structure
    //    is asserted by the gate rather than trusted to review.
    static VolumetricSample March(const CloudLayerSettings& Cloud, const LocalVolumeSettings& LocalCloud,
                                  const LocalVolumeSettings& LocalFog, const WindSettings& Wind,
                                  const VolumetricBudget& Budget,
                                  const float Origin[3], const float Direction[3], float MaximumDistance,
                                  const float SunDirection[3], const float SunRadiance[3],
                                  const float AmbientRadiance[3], float Time) noexcept
    {
        VolumetricSample Result{};

        // ── The union interval ─────────────────────────────────────────────────────────────────────────────────
        float Near = 1e30f, Far = -1e30f;
        bool  Any  = false;

        if (Cloud.Enabled)
        {
            float Base = 0.0f, Top = 0.0f;
            if (SlabExtent(Cloud, Base, Top))
            {
                // The slab is horizontal, so its interval is where the ray crosses the two altitudes.
                float SlabNear = 0.0f, SlabFar = 0.0f;
                if (SlabInterval(Origin[2], Direction[2], Base, Top, MaximumDistance, SlabNear, SlabFar))
                {
                    Near = std::fmin(Near, SlabNear); Far = std::fmax(Far, SlabFar); Any = true;
                }
            }
        }
        float BoxNear = 0.0f, BoxFar = 0.0f;
        if (LocalCloud.Enabled && IntersectBox(LocalCloud.Centre, LocalCloud.HalfSize, Origin, Direction, BoxNear, BoxFar))
        {
            Near = std::fmin(Near, BoxNear); Far = std::fmax(Far, BoxFar); Any = true;
        }
        if (LocalFog.Enabled && IntersectBox(LocalFog.Centre, LocalFog.HalfSize, Origin, Direction, BoxNear, BoxFar))
        {
            Near = std::fmin(Near, BoxNear); Far = std::fmax(Far, BoxFar); Any = true;
        }
        if (!Any) return Result;

        Near = std::fmax(Near, 0.0f);
        Far  = std::fmin(Far, MaximumDistance);
        if (Far <= Near) return Result;

        // ⚠️ The step SIZE is bounded, not the step COUNT, and the difference is a correctness matter rather than
        //    a quality one. Fixing the count means the size grows with the union interval, so enabling a distant
        //    fog volume silently coarsens the sampling of a near cloud — measured, adding fog RAISED
        //    transmittance from 0.2954 to 0.2963, i.e. more medium let more light through, which is impossible.
        //
        //    The budget therefore sets the step size for a reference span, and a longer interval takes more
        //    steps rather than coarser ones. The cap keeps a pathological interval (a grazing ray through the
        //    whole slab) from running away.
        const uint32_t Budgeted = (Cloud.Enabled ? Budget.CloudSteps : Budget.LocalSteps);
        const uint32_t Reference = Budgeted == 0u ? 1u : Budgeted;
        constexpr float kReferenceSpan = 4000.0f;   // [m] the span the tier's step count is quoted against
        const float StepSize = kReferenceSpan / static_cast<float>(Reference);
        const uint32_t Cap = Reference * 4u;        // never more than 4x the tier's budget
        uint32_t Count = static_cast<uint32_t>(std::ceil((Far - Near) / std::fmax(StepSize, 1e-3f)));
        if (Count < 1u) Count = 1u;
        if (Count > Cap) Count = Cap;
        const float ActualStep = (Far - Near) / static_cast<float>(Count);

        const float CosTheta = Direction[0] * SunDirection[0] + Direction[1] * SunDirection[1]
                             + Direction[2] * SunDirection[2];

        float Transmittance = 1.0f;
        for (uint32_t I = 0; I < Count; ++I)
        {
            const float T = Near + (static_cast<float>(I) + 0.5f) * ActualStep;
            const float P[3] = { Origin[0] + Direction[0] * T,
                                 Origin[1] + Direction[1] * T,
                                 Origin[2] + Direction[2] * T };
            ++Result.StepsTaken;

            // Each medium's own density, summed into one extinction — this is what makes the march shared.
            const float CloudPart = Cloud.Enabled ? CloudDensity(Cloud, Wind, P, Time) : 0.0f;
            const float LocalCloudPart = LocalDensity(LocalCloud, Wind, P, Time);
            const float LocalFogPart   = LocalDensity(LocalFog, Wind, P, Time);
            const float Density = CloudPart + LocalCloudPart + LocalFogPart;
            if (Density <= 1e-5f) continue;

            // Sun visibility, marched once for the combined medium rather than per volume.
            const float SunTransmittance = ShadowMarch(Cloud, LocalCloud, LocalFog, Wind, P, SunDirection,
                                                       ActualStep, Budget.LightTaps, Time);
            ++Result.ShadowMarches;

            const float Phase = HenyeyGreenstein(CosTheta, LocalCloud.Anisotropy);
            const float Extinction = Density * ActualStep * 0.01f;
            const float StepTransmittance = std::exp(-Extinction);

            // Albedo blended by which medium dominates here.
            const float Total = std::fmax(Density, 1e-6f);
            const float CloudWeight = (CloudPart + LocalCloudPart) / Total;
            for (int C = 0; C < 3; ++C)
            {
                const float Albedo = Lerp(0.88f, LocalCloud.Albedo[C], CloudWeight);
                const float In = (SunRadiance[C] * SunTransmittance * Phase + AmbientRadiance[C]) * Albedo;
                Result.Scatter[C] += In * (1.0f - StepTransmittance) * Transmittance;
            }
            Transmittance *= StepTransmittance;
            if (Transmittance < 0.005f) break;      // fully occluded; nothing behind matters
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
