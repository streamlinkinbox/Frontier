//============================================================================================================================================
// 📦 Engine/DisplayPresentation/SkyDomeSheet.h — the baked sky dome, and the rule for when it may stand in for the march
//============================================================================================================================================
// Roadmap #26 stage A, taken from Exhibits/Workbench/Sky/SkyProbeProof.cpp where the split was measured (six
//    times of day, mean relative error 0.014-0.123 %, tone-mapped panel RMSE ≤ 0.13 of 255, fetch 67-135x the
//    march). The realism rule the proof engineered in, restated as the contract of this file:
//
//    WHAT BAKES: the smooth single-scatter dome — SkyRecords.slang's SkyRadiance integral and nothing else —
//    as radiance AND transmittance, both spectral, in one RGBA16F sheet of two stacked octahedral squares
//    (radiance on top, transmittance below).
//
//    WHAT NEVER BAKES: every point and animated feature. The sun's disc and aureole (sub-texel; cone texels
//    are inpainted from the cone's boundary so no disc energy smears into the bilinear neighbourhood), the
//    stars (binding 23's own pass), the moons (their texture slots), the twilight approximation and the
//    cirrus (it animates). SkyAlong adds all of these ANALYTICALLY on top of the fetched dome, so the fetch
//    changes where the smooth dome comes from and nothing else.
//
//    WHERE THE MARCH KEEPS THE PIXEL: inside the solar cone (4°) and inside the horizon band (±1.5° of the
//    level line). The planet's edge is a measured radiance discontinuity — 1.2e-7 against 3.4e-4 half a
//    degree apart at 21 h — and no texel sheet straddles it honestly; the band is ~2.6 % of the sphere and
//    wider than a 256² texel diagonal, so a fetch outside it never blends sky into ground.
//
//    The kernel's copy of these three constants lives in SkyRecords.slang (kSkyDomeSide / kSkyDomeHorizonSine
//    / kSkyDomeSunConeCos) and the sky proofs pin the two copies against each other — the same arrangement as
//    kAureoleKnee/Slope/Sigma above.
//
//    WHO USES IT: the game build's kernel, when SkyControl.w carries a slot + 1. The editor stays analytic
//    always (the owner's call), which the host enforces by never packing the slot in development builds
//    unless the toggle is deliberately flipped for an A/B.

#pragma once

#include "AtmosphereModel.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

inline constexpr uint32_t kSkyDomeSide        = 256u;       // octahedral square side; the sheet is Side x 2·Side
inline constexpr float    kSkyDomeHorizonSine = 0.02618f;   // sin(1.5°): |Direction.z| under this marches analytically
inline constexpr float    kSkyDomeSunConeCos  = 0.99756f;   // cos(4°): closer to the sun than this marches analytically
inline constexpr float    kSkyDomeCameraHeight = 2.0f;      // [m] the kernel's one observer (PackSkyRecord's own figure)

//------------------------------------------------------------------------------------------------------------------------
//                                              OCTAHEDRAL SEATING (Z-up)
//------------------------------------------------------------------------------------------------------------------------
// One square holds the sphere: upper hemisphere in the diamond, lower folded into the corners. Z is up — the
//    engine's frame, and SkyRecords.slang carries the same fold in GLSL.

inline float SkyDomeSignNotZero(float V) noexcept { return V >= 0.0f ? 1.0f : -1.0f; }

inline void SkyDomeOctFromDirection(const float Direction[3], float& U, float& V) noexcept
{
    const float Den = std::fabs(Direction[0]) + std::fabs(Direction[1]) + std::fabs(Direction[2]);
    float Px = Direction[0] / Den;
    float Py = Direction[1] / Den;
    if (Direction[2] < 0.0f)
    {
        const float Qx = (1.0f - std::fabs(Py)) * SkyDomeSignNotZero(Px);
        const float Qy = (1.0f - std::fabs(Px)) * SkyDomeSignNotZero(Py);
        Px = Qx; Py = Qy;
    }
    U = Px * 0.5f + 0.5f;
    V = Py * 0.5f + 0.5f;
}

inline void SkyDomeDirectionFromOct(float U, float V, float Direction[3]) noexcept
{
    const float Fx = U * 2.0f - 1.0f;
    const float Fy = V * 2.0f - 1.0f;
    float X = Fx, Y = Fy;
    float Z = 1.0f - std::fabs(Fx) - std::fabs(Fy);
    if (Z < 0.0f)
    {
        X = (1.0f - std::fabs(Fy)) * SkyDomeSignNotZero(Fx);
        Y = (1.0f - std::fabs(Fx)) * SkyDomeSignNotZero(Fy);
    }
    const float L = std::sqrt(X * X + Y * Y + Z * Z);
    Direction[0] = X / L; Direction[1] = Y / L; Direction[2] = Z / L;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE BAKE
//------------------------------------------------------------------------------------------------------------------------

// IEEE half, round-to-nearest — what an RGBA16F texel holds. Radiance and transmittance both sit well inside
//    half range (the dome peaks around 1.5 linear; transmittance is [0,1]), so overflow clamps to half-max.
inline uint16_t SkyDomeHalfFromFloat(float Value) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Value, 4u);
    const uint32_t Sign = (Bits >> 16u) & 0x8000u;
    const int32_t  Exponent = static_cast<int32_t>((Bits >> 23u) & 0xFFu) - 127 + 15;
    const uint32_t Mantissa = Bits & 0x7FFFFFu;
    if (Exponent <= 0)  return static_cast<uint16_t>(Sign);              // flushes denormals: the dome never needs them
    if (Exponent >= 31) return static_cast<uint16_t>(Sign | 0x7BFFu);    // clamps to half-max rather than inf
    uint32_t Half = (static_cast<uint32_t>(Exponent) << 10u) | (Mantissa >> 13u);
    if (Mantissa & 0x1000u) ++Half;                                      // round to nearest; a carry ripples into the exponent correctly
    if (Half >= 0x7C00u) Half = 0x7BFFu;
    return static_cast<uint16_t>(Sign | Half);
}

// Bakes the dome for one staging into RGBA16F texels: Side x 2·Side, radiance square first, transmittance
//    square below it, 4 halves per texel (alpha carries 1). Texels inside the solar cone are inpainted from
//    the cone's boundary — the sun's energy never enters the sheet, which is what keeps the circumsolar
//    gradient honest when the analytic disc lands on top. 2x2 supersampled: bake-time cost only, measured at
//    ~2 s for the 256² sheet at the product's 16x6 sample staging.
inline void BakeSkyDomeSheet(const AtmosphereMedium& Medium, const AtmosphereLight& Light,
                             uint32_t ViewSamples, uint32_t LightSamples,
                             std::vector<uint16_t>& OutHalves) noexcept
{
    const uint32_t Side = kSkyDomeSide;
    OutHalves.assign(static_cast<size_t>(Side) * Side * 2u * 4u, 0u);

    const float* Sun = Light.Direction;
    for (uint32_t Y = 0u; Y < Side; ++Y)
        for (uint32_t X = 0u; X < Side; ++X)
        {
            float RadianceSum[3] = {}, TransmittanceSum[3] = {};
            for (uint32_t S = 0u; S < 4u; ++S)
            {
                const float U = (static_cast<float>(X) + 0.25f + 0.5f * static_cast<float>(S % 2u)) / static_cast<float>(Side);
                const float V = (static_cast<float>(Y) + 0.25f + 0.5f * static_cast<float>(S / 2u)) / static_cast<float>(Side);
                float Direction[3];
                SkyDomeDirectionFromOct(U, V, Direction);

                // The inpaint: a sample inside the solar cone slides to the cone's boundary, so the texel
                //    carries boundary sky rather than disc-adjacent energy. (Fetches inside the cone never
                //    happen — the kernel marches there — this only protects the bilinear neighbourhood.)
                const float Along = Direction[0] * Sun[0] + Direction[1] * Sun[1] + Direction[2] * Sun[2];
                if (Along > kSkyDomeSunConeCos)
                {
                    float Perp[3] = { Direction[0] - Sun[0] * Along, Direction[1] - Sun[1] * Along, Direction[2] - Sun[2] * Along };
                    float L = std::sqrt(Perp[0] * Perp[0] + Perp[1] * Perp[1] + Perp[2] * Perp[2]);
                    if (L < 1e-6f)
                    {
                        Perp[0] = Sun[1]; Perp[1] = -Sun[0]; Perp[2] = 0.0f;
                        L = std::sqrt(Perp[0] * Perp[0] + Perp[1] * Perp[1]);
                        if (L < 1e-6f) { Perp[0] = 0.0f; Perp[1] = Sun[2]; Perp[2] = -Sun[1]; L = std::sqrt(Perp[1] * Perp[1] + Perp[2] * Perp[2]); }
                    }
                    const float ConeAngle = std::acos(kSkyDomeSunConeCos) * 1.05f;
                    const float C = std::cos(ConeAngle), Sn = std::sin(ConeAngle);
                    for (int A = 0; A < 3; ++A) Direction[A] = Sun[A] * C + Perp[A] / L * Sn;
                }

                const AtmosphereSample Sample = AtmosphereModel::Integrate(Medium, Light, kSkyDomeCameraHeight,
                                                                           Direction, ViewSamples, LightSamples);
                for (int C = 0; C < 3; ++C)
                {
                    RadianceSum[C]      += Sample.Radiance[C];
                    TransmittanceSum[C] += Sample.Transmittance[C];
                }
            }

            uint16_t* Radiance      = OutHalves.data() + (static_cast<size_t>(Y) * Side + X) * 4u;
            uint16_t* Transmittance = OutHalves.data() + (static_cast<size_t>(Y + Side) * Side + X) * 4u;
            for (int C = 0; C < 3; ++C)
            {
                Radiance[C]      = SkyDomeHalfFromFloat(RadianceSum[C] * 0.25f);
                Transmittance[C] = SkyDomeHalfFromFloat(TransmittanceSum[C] * 0.25f);
            }
            Radiance[3] = Transmittance[3] = SkyDomeHalfFromFloat(1.0f);
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                THE STALENESS RULE
//------------------------------------------------------------------------------------------------------------------------

// True when two packed sky records agree on every figure the DOME depends on: sun direction and radiance, the
//    three media, the planet, and the sample counts (a march at different counts is a different dome). The
//    shadow lanes (Mie.w, Ozone.w, Planet.w) and the twilight are deliberately outside the compare — they
//    never enter the integral. PackSkyRecord drops to the analytic march the moment this returns false, so a
//    re-staged sun can never render against yesterday's bake. (Templated on the record shape so this header
//    needs no include of SkyConstantRecord.h; the one instantiation is with SkyConstantRecord.)
template <typename RecordShape>
[[nodiscard]] inline bool SkyDomeStagingMatches(const RecordShape& Baked, const RecordShape& Current) noexcept
{
    const auto Near = [](float A, float B) { return std::fabs(A - B) <= 1e-6f * std::fmax(1.0f, std::fabs(A)); };
    for (int C = 0; C < 3; ++C)
    {
        if (!Near(Baked.SunDirection[C], Current.SunDirection[C])) return false;
        if (!Near(Baked.SunRadiance[C],  Current.SunRadiance[C]))  return false;
        if (!Near(Baked.Rayleigh[C],     Current.Rayleigh[C]))     return false;
        if (!Near(Baked.Ozone[C],        Current.Ozone[C]))        return false;
    }
    if (!Near(Baked.SunRadiance[3], Current.SunRadiance[3])) return false;
    if (!Near(Baked.Rayleigh[3],    Current.Rayleigh[3]))    return false;
    for (int C = 0; C < 3; ++C)
        if (!Near(Baked.Mie[C], Current.Mie[C])) return false;
    for (int C = 0; C < 3; ++C)
        if (!Near(Baked.Planet[C], Current.Planet[C])) return false;
    return Baked.Control[0] == Current.Control[0] && Baked.Control[1] == Current.Control[1];
}

} // namespace Frontier
