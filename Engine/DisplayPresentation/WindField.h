//============================================================================================================================================
// 📦 Engine/DisplayPresentation/WindField.h — one wind, read by every medium
//============================================================================================================================================
// Celestial port, step 4. Deliberately built before clouds, fog and precipitation, because all of them advect by
//    this field's integral: a cloud that drifts one way while the rain beneath it falls another is the kind of
//    error that no single system owns and nobody can localise afterwards.
//
// The model, transcribed from the reference demo:
//    • a base flow with speed and bearing, sheared and veered with altitude (Ekman-like: faster and turning
//      clockwise as you climb, because friction with the ground falls away),
//    • a gust envelope, three sines at incommensurate rates so it never audibly loops,
//    • curl turbulence — the curl of a low-frequency noise field, which is divergence-free by construction and
//      therefore swirls without compressing the air.
//
// ⚠️ THE PERFORMANCE STRUCTURE IS PART OF THE DESIGN, NOT AN OPTIMISATION TO ADD LATER.
//    The source branch's final commit (6c8b1a8) exists because turbulence was called from inside the advection
//    used at every raymarch step. Six noise evaluations per step, ~250 steps per cloud pixel, is roughly 1500
//    extra noise evaluations per pixel — and the fix was not to make the noise cheaper but to move it: the
//    per-step term became trig-only, and the swirl is evaluated ONCE per pixel and reused.
//
//    So this header exposes the split explicitly rather than leaving it to a caller's discipline:
//        SampleStep()   — trig only. Safe inside a march. No noise, ever.
//        SampleSwirl()  — six noise evaluations. ONCE per pixel, and only when Turbulence > 0.
//        Sample()       — the full field, for CPU-side consumers that call it a handful of times per frame.
//    `CheckWindField.sh` asserts the split structurally, because a comment is not a constraint.
//
// Header-only and dependency-free for the reason AtmosphereModel.h and ColourTransfer.h are: this same field has
//    to run in the CPU raster, in the headless proofs, and be transcribed into GLSL, and none of those can share
//    a binary. One definition is what stops the copies drifting.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE FIELD
//------------------------------------------------------------------------------------------------------------------------

// Named as the reference demo's Wind entity names them, so the eventual panel is a projection of this struct
//    rather than a translation of it.
struct WindSettings
{
    float Speed     = 8.0f;    // [m/s] at the surface — the panel's Force, shown on the Beaufort rose
    float Bearing   = 225.0f;  // [deg] compass bearing the wind blows TOWARD
    float Shear     = 0.35f;   // [x/km] fractional speed gain per kilometre of altitude
    float Veer      = 12.0f;   // [deg/km] clockwise turn per kilometre (Ekman spiral)
    float Gust      = 0.25f;   // [0..1] gust depth; 0 is a steady flow
    float GustPhase = 0.0f;    // [rad] advanced by the frame clock, so the CPU and GPU twins agree
    float Turbulence= 0.30f;   // [0..1] curl-noise swirl
    float Steadiness= 1.0f;    // [0..1] panel's Steadiness — scales gust and turbulence together
};

class WindField
{
public:
    //--------------------------------------------------------------------------------------------------------------------
    //                                          THE CHEAP PART — safe in a march
    //--------------------------------------------------------------------------------------------------------------------

    // The base flow at an altitude. Two trig calls and a multiply; no noise. This is the only wind term a
    //    raymarch step may call.
    static void SampleStep(const WindSettings& Wind, float AltitudeMetres, float OutVelocity[3]) noexcept
    {
        constexpr float kPi = 3.14159265358979323846f;
        const float Kilometres = (AltitudeMetres > 0.0f ? AltitudeMetres : 0.0f) * 0.001f;
        const float Speed   = Wind.Speed * (1.0f + Wind.Shear * Kilometres);
        const float Bearing = (Wind.Bearing + Wind.Veer * Kilometres) * kPi / 180.0f;
        // Bearing is a compass angle: 0 is north (+Y), 90 is east (+X).
        OutVelocity[0] = std::sin(Bearing) * Speed;
        OutVelocity[1] = std::cos(Bearing) * Speed;
        OutVelocity[2] = 0.0f;
    }

    // Uniform advection plus a frozen shear offset: how a cloud field follows the wind without shredding.
    //
    //    ⚠️ NEVER the local flow times time-of-day. That older form drifted every altitude by its own wind over
    //    seconds-since-midnight, and the shear/veer piled 76 km of altitude-dependent offset across the 1.1 km
    //    slab by 7am (measured): adjacent elevation rays sampled field points five cells apart — fully
    //    decorrelated — while adjacent azimuth rays stayed correlated, which is exactly horizontal cloud
    //    streaks that worsen through the day. Physically the error is accumulating shear over hours: real shear
    //    shapes a cloud over its ~10-minute eddy life, not since midnight, and there is no formation term here
    //    to replace the shredded field. So the whole medium advects by the flow at ReferenceAltitudeMetres —
    //    coherent at every hour — and the shear/veer survive as a static lean, frozen at a young cloud's memory
    //    (120 s) and clamped to ±2 cells, so no slider combination can shred the sampling grid again. With shear
    //    and veer at zero the lean vanishes and this equals the old flow-times-time exactly.
    //
    //    Trig-only (two SampleSteps), so it is safe inside a march like SampleStep itself. Transcribed as
    //    CloudDriftAt in Shaders/SkyRecords.slang; the sky-cloud proof renders through the twin.
    static void AdvectDrift(const WindSettings& Wind, float AltitudeMetres, float ReferenceAltitudeMetres,
                            float TimeSeconds, float CellMetres, float ArtFactor, float OutDrift[2]) noexcept
    {
        float Step[3], Reference[3];
        SampleStep(Wind, AltitudeMetres, Step);
        SampleStep(Wind, ReferenceAltitudeMetres, Reference);
        constexpr float kShearMemory = 120.0f;   // [s] the lean a young cloud carries
        const float Cell = Clamp(CellMetres, 1.0f, 1.0e6f);
        for (int C = 0; C < 2; ++C)
        {
            float Shear = (Step[C] - Reference[C]) * kShearMemory * ArtFactor;
            const float Limit = 2.0f * Cell;
            Shear = Shear < -Limit ? -Limit : (Shear > Limit ? Limit : Shear);
            OutDrift[C] = Reference[C] * TimeSeconds * ArtFactor + Shear;
        }
    }

    // The gust envelope. Three sines at deliberately incommensurate rates (1, 2.31, 4.7) so the pattern never
    //    repeats on a period a viewer can notice. Scalar, so it costs nothing to fold into a step.
    static float SampleGust(const WindSettings& Wind) noexcept
    {
        const float Depth = Wind.Gust * Clamp(Wind.Steadiness, 0.0f, 1.0f);
        return 1.0f + Depth * (0.55f * std::sin(Wind.GustPhase)
                             + 0.30f * std::sin(Wind.GustPhase * 2.31f + 1.7f)
                             + 0.15f * std::sin(Wind.GustPhase * 4.7f + 0.4f));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                     THE EXPENSIVE PART — once per pixel
    //--------------------------------------------------------------------------------------------------------------------

    // Curl turbulence: six noise evaluations. ⚠️ ONCE PER PIXEL. Calling this per march step is the exact
    //    regression the source branch's last commit fixed, and it costs ~1500 extra noise evaluations per pixel.
    //
    //    Taking the curl of a noise field rather than sampling the noise directly is what makes the result
    //    divergence-free: air swirls without being created or destroyed. Sampling three noise channels as a
    //    velocity would compress and rarefy the medium, and clouds advected by it would visibly bunch up.
    static void SampleSwirl(const WindSettings& Wind, const float Position[3], float Time,
                            float OutSwirl[3]) noexcept
    {
        OutSwirl[0] = OutSwirl[1] = OutSwirl[2] = 0.0f;
        const float Strength = Wind.Turbulence * Clamp(Wind.Steadiness, 0.0f, 1.0f);
        if (Strength <= 0.0f) return;                    // the early-out that makes the guard cheap

        const float Q[3] = { Position[0] * 0.02f + Time * 0.05f,
                             Position[1] * 0.02f,
                             Position[2] * 0.02f + Time * 0.03f };
        constexpr float E = 0.5f;

        // Central differences along each axis: the six evaluations.
        const float Dx = ValueNoise(Q[0] + E, Q[1], Q[2]) - ValueNoise(Q[0] - E, Q[1], Q[2]);
        const float Dy = ValueNoise(Q[0], Q[1] + E, Q[2]) - ValueNoise(Q[0], Q[1] - E, Q[2]);
        const float Dz = ValueNoise(Q[0], Q[1], Q[2] + E) - ValueNoise(Q[0], Q[1], Q[2] - E);

        const float Scale = Strength * Wind.Speed * 0.9f;
        OutSwirl[0] = (Dy - Dz) * Scale;
        OutSwirl[1] = (Dz - Dx) * Scale;
        OutSwirl[2] = (Dx - Dy) * Scale;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              THE WHOLE FIELD
    //--------------------------------------------------------------------------------------------------------------------

    // Base × gust + swirl. For CPU consumers that call it a handful of times a frame — a particle spawner, a
    //    panel readout, the proofs. A raymarch must NOT call this: it contains SampleSwirl.
    static void Sample(const WindSettings& Wind, const float Position[3], float Time, float OutVelocity[3]) noexcept
    {
        float Base[3];
        SampleStep(Wind, Position[2], Base);
        const float Gust = SampleGust(Wind);
        float Swirl[3];
        SampleSwirl(Wind, Position, Time, Swirl);
        for (int C = 0; C < 3; ++C) OutVelocity[C] = Base[C] * Gust + Swirl[C];
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  READOUTS
    //--------------------------------------------------------------------------------------------------------------------

    // Beaufort force from a speed in m/s. The panel's wind rose shows this beside the number, and it is a real
    //    scale with published thresholds rather than a linear remap.
    static uint32_t BeaufortForce(float MetresPerSecond) noexcept
    {
        static constexpr float kThresholds[12] = { 0.3f, 1.6f, 3.4f, 5.5f, 8.0f, 10.8f,
                                                   13.9f, 17.2f, 20.8f, 24.5f, 28.5f, 32.7f };
        for (uint32_t Force = 0; Force < 12u; ++Force)
            if (MetresPerSecond < kThresholds[Force]) return Force;
        return 12u;
    }

    static const char* BeaufortName(uint32_t Force) noexcept
    {
        static const char* kNames[13] = { "Calm", "Light air", "Light breeze", "Gentle breeze",
                                          "Moderate breeze", "Fresh breeze", "Strong breeze", "Near gale",
                                          "Gale", "Strong gale", "Storm", "Violent storm", "Hurricane" };
        return kNames[Force > 12u ? 12u : Force];
    }

private:
    static float Clamp(float V, float Lo, float Hi) noexcept { return V < Lo ? Lo : (V > Hi ? Hi : V); }

    static float Hash(float X, float Y, float Z) noexcept
    {
        float S = std::sin(X * 127.1f + Y * 311.7f + Z * 74.7f) * 43758.5453f;
        return S - std::floor(S);
    }

    // Trilinear value noise. Matched to the demo's vnoise so the CPU and GPU twins agree; the proof checks the
    //    field itself rather than this helper, but a different noise would drift the two apart.
    static float ValueNoise(float X, float Y, float Z) noexcept
    {
        const float Ix = std::floor(X), Iy = std::floor(Y), Iz = std::floor(Z);
        const float Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
        const float Ux = Fx * Fx * (3.0f - 2.0f * Fx);
        const float Uy = Fy * Fy * (3.0f - 2.0f * Fy);
        const float Uz = Fz * Fz * (3.0f - 2.0f * Fz);

        const float N000 = Hash(Ix, Iy, Iz),             N100 = Hash(Ix + 1, Iy, Iz);
        const float N010 = Hash(Ix, Iy + 1, Iz),         N110 = Hash(Ix + 1, Iy + 1, Iz);
        const float N001 = Hash(Ix, Iy, Iz + 1),         N101 = Hash(Ix + 1, Iy, Iz + 1);
        const float N011 = Hash(Ix, Iy + 1, Iz + 1),     N111 = Hash(Ix + 1, Iy + 1, Iz + 1);

        const float X00 = N000 + (N100 - N000) * Ux, X10 = N010 + (N110 - N010) * Ux;
        const float X01 = N001 + (N101 - N001) * Ux, X11 = N011 + (N111 - N011) * Ux;
        const float Y0  = X00 + (X10 - X00) * Uy,    Y1  = X01 + (X11 - X01) * Uy;
        return (Y0 + (Y1 - Y0) * Uz) * 2.0f - 1.0f;
    }
};

} // namespace Frontier
