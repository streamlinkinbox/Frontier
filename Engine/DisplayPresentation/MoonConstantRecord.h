//============================================================================================================================================
// 📦 Engine/DisplayPresentation/MoonConstantRecord.h — the moon atlas, its GPU record, and the CPU evaluator
//============================================================================================================================================
// The GPU seam for the Celestial moons, mirroring SkyConstantRecord.h. One header, three consumers:
//
//    · the ATLAS — the six bodies from the reference panel's MOON_ATLAS, transcribed field for field, so the
//      project registers the same files the panel renders;
//    · the RECORD — the CPU mirror of Shaders/MoonRecords.slang's uniform block at binding 22, with the same
//      static_assert discipline as the sky record: a layout mismatch is a compile error, not a wrong picture;
//    · the EVALUATOR — the C++ transcription of the panel's moons(), which the CPU raster calls per miss pixel
//      and the headless proofs compare against a re-transcription of the shader (the SkyKernelParityProof shape).
//
// ⚠️ THREE CONVENTIONS CHANGE AT THIS FILE'S BORDER, and each is marked where it happens:
//
//    ① UP IS +Z. The panel is Y-up: its disc frame crosses the moon direction with (0,1,0) and its ambient reads
//       dir.y. This engine is right-handed Z-up (CLAUDE.md §7), so both read .z — and the disc frame takes a
//       fallback axis within a degree of the zenith, where the panel's unguarded cross product goes NaN.
//    ② PHASE 0 IS NEW. The panel's phase lights the disc fully at 0 (lit = +z, toward the viewer). The engine's
//       ephemeris reports 0 = new, 0.5 = full (CelestialSolver.h), and the roster state and the inspector keep
//       that convention; MoonPhaseToReference converts once, at pack time, for both consumers.
//    ③ ALBEDOS ARE LINEAR. The panel samples raw sRGB bytes. Ours upload as SRGB (TextureIndex, Linear=false) so
//       the kernel's texture() returns linear, and SampleMoonAlbedo applies the same piecewise EOTF on the CPU —
//       because the two pipelines must agree with each other first, and the panel second.
//
// The record carries bindless texture SLOTS rather than pixels: the six albedos ride the shared TextureIndex and
//    land in the sampler2D[] table with the scene, so no second upload path exists to drift.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE ATLAS
//------------------------------------------------------------------------------------------------------------------------

// One body from the reference panel's MOON_ATLAS (Diagnostics/CelestialPanel.html). Size is the angular DIAMETER in
//    degrees — art-directed, like the panel's: the real moon is ~0.5°, Luna's default is 0.9° so it reads. Position
//    and phase are the true ephemeris for a linked Luna; every other slot is placed by hand.
struct MoonAtlasPreset
{
    const char* Id;          // "luna" — the roster key
    const char* Name;        // "Luna" — the inspector's select option
    const char* File;        // "luna_2k.jpg" — under kMoonTextureDirectory
    float       Tint[3];     // all white in the reference; the slot tints, not the preset
    float       Haze;        // [0..1] atmosphere: limb softening, rim, phase wrap
    float       SizeDegrees; // [deg] angular diameter default. Luna's is the real 0.52 — the panel's own
                             // hint ('real moon ≈ 0.5°') and the kernel's 0.53° sun disc agree — while the fantasy
                             // moons keep their stylised sizes.
    float       Gamma;       // contrast on the albedo (Shard's 0.9 deepens it)
    float       TiltDegrees; // [deg] axial tilt applied before the UV lookup
};

inline constexpr const char* kMoonTextureDirectory = "EngineContent/CelestialTextures/";
inline constexpr uint32_t    kMoonAtlasCount       = 6u;   // bodies on disk
inline constexpr uint32_t    kMoonDrawCount        = 4u;   // MAXM: bodies drawn at once

inline constexpr MoonAtlasPreset kMoonAtlas[kMoonAtlasCount] = {
    { "luna",    "Luna",    "luna_2k.jpg",    { 1.0f, 1.0f, 1.0f }, 0.00f, 0.52f, 1.0f,  6.7f },
    { "ember",   "Ember",   "ember_2k.jpg",   { 1.0f, 1.0f, 1.0f }, 0.08f, 1.6f, 1.0f, 25.0f },
    { "glacier", "Glacier", "glacier_2k.jpg", { 1.0f, 1.0f, 1.0f }, 0.05f, 1.2f, 1.0f,  3.0f },
    { "sulfur",  "Sulfur",  "sulfur_2k.jpg",  { 1.0f, 1.0f, 1.0f }, 0.00f, 1.1f, 1.0f,  2.0f },
    { "shroud",  "Shroud",  "shroud_2k.jpg",  { 1.0f, 1.0f, 1.0f }, 1.00f, 2.4f, 1.0f, 27.0f },
    { "shard",   "Shard",   "shard_2k.jpg",   { 1.0f, 1.0f, 1.0f }, 0.00f, 0.6f, 0.9f,  0.0f },
};

// Engine phase (0 = new, 0.5 = full) to reference phase (0 = full): a half-turn. Self-inverse, so the same call
//    converts back — the inspector shows engine convention, the record and the draw list carry reference.
inline float MoonPhaseToReference(float EnginePhase) noexcept
{
    float R = EnginePhase + 0.5f;
    R -= std::floor(R);
    return R;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE RECORD
//------------------------------------------------------------------------------------------------------------------------

// Binding 22, std140. Eighteen rows: the control word, the four bindless slots, then the four moons as the panel
//    holds them (direction, tint, P = radius/brightness/phase/glow, Surf = spin/tilt/haze/gamma).
//
//        offset    0   MoonControl      uvec4   x = moons drawn [0..4]
//        offset   16   MoonSlots        uvec4   bindless sampler2D[] slot per moon
//        offset   32   MoonDirection    vec4[4] xyz = unit toward the moon, Z-up; w unused
//        offset   96   MoonTint         vec4[4] rgb tint; w unused
//        offset  160   MoonParams       vec4[4] x = angular RADIUS [rad], y = brightness,
//                                              z = phase (REFERENCE convention), w = glow
//        offset  224   MoonSurface      vec4[4] x = spin [rad], y = tilt [rad], z = haze, w = gamma
//        block size = 288 B
struct MoonConstantRecord
{
    uint32_t Control[4];
    uint32_t Slots[4];
    float    Direction[4][4];
    float    Tint[4][4];
    float    Params[4][4];
    float    Surface[4][4];
};

static_assert(sizeof(MoonConstantRecord) == 288u, "MoonConstants must match the shader's std140 block exactly");
static_assert(sizeof(MoonConstantRecord) % 16u == 0u, "std140 blocks are 16-B aligned");
static_assert(offsetof(MoonConstantRecord, Control)   == 0u,   "MoonControl sits at offset 0");
static_assert(offsetof(MoonConstantRecord, Slots)     == 16u,  "MoonSlots sits at offset 16");
static_assert(offsetof(MoonConstantRecord, Direction) == 32u,  "MoonDirection sits at offset 32");
static_assert(offsetof(MoonConstantRecord, Tint)      == 96u,  "MoonTint sits at offset 96");
static_assert(offsetof(MoonConstantRecord, Params)    == 160u, "MoonParams sits at offset 160");
static_assert(offsetof(MoonConstantRecord, Surface)   == 224u, "MoonSurface sits at offset 224");

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DRAW LIST
//------------------------------------------------------------------------------------------------------------------------

// One resolved moon: everything both consumers need, in one place, so the raster and the kernel cannot be handed
//    different moons. The project fills kMoonDrawCount of these from its roster plus the solved frame; PackMoon-
//    Constants keeps the slots and drops the pixels, ApplyTo keeps the pixels and drops the slots.
struct MoonAlbedoView
{
    const uint8_t* Texels     = nullptr;   // level-0 RGBA8 sRGB bytes, row-major, top-down
    uint32_t       Width      = 0u;
    uint32_t       Height     = 0u;
    uint32_t       TexelBytes = 0u;        // 4 expected; anything else reads as white
};

struct MoonDrawEntry
{
    float          Direction[3] = { 0.0f, 0.0f, 1.0f };
    float          Tint[3]      = { 1.0f, 1.0f, 1.0f };
    float          AngularRadius = 0.0f;   // [rad]
    float          Brightness    = 1.6f;   // panel default
    float          Phase         = 0.0f;   // REFERENCE convention (0 = full) — converted at fill time
    float          Glow          = 0.8f;   // panel default
    float          Spin          = 0.0f;   // [rad]
    float          Tilt          = 0.0f;   // [rad]
    float          Haze          = 0.0f;
    float          Gamma         = 1.0f;
    MoonAlbedoView Albedo;                 // CPU only
    uint32_t       TextureSlot   = 0u;     // GPU only
};

struct MoonDrawList
{
    uint32_t     Count = 0u;
    MoonDrawEntry Entries[kMoonDrawCount];
};

// Reshapes the draw list into the block. The ONLY place moon state becomes GPU bytes. A null or empty list packs
//    a zero count, which is the kernel's early-out — never a sampled slot 0.
inline MoonConstantRecord PackMoonConstants(const MoonDrawEntry* Entries, uint32_t Count) noexcept
{
    MoonConstantRecord R{};
    if (Entries == nullptr) return R;
    if (Count > kMoonDrawCount) Count = kMoonDrawCount;
    R.Control[0] = Count;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const MoonDrawEntry& E = Entries[I];
        R.Slots[I] = E.TextureSlot;
        R.Direction[I][0] = E.Direction[0]; R.Direction[I][1] = E.Direction[1]; R.Direction[I][2] = E.Direction[2];
        R.Tint[I][0] = E.Tint[0];           R.Tint[I][1] = E.Tint[1];           R.Tint[I][2] = E.Tint[2];
        R.Params[I][0] = E.AngularRadius;   R.Params[I][1] = E.Brightness;
        R.Params[I][2] = E.Phase;           R.Params[I][3] = E.Glow;
        R.Surface[I][0] = E.Spin;           R.Surface[I][1] = E.Tilt;
        R.Surface[I][2] = E.Haze;           R.Surface[I][3] = E.Gamma;
    }
    return R;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE EVALUATOR
//------------------------------------------------------------------------------------------------------------------------

// The sRGB electro-optical transfer function, piecewise — what the GPU's SRGB hardware decode computes, so the CPU
//    raster samples the same linear albedo the kernel's texture() returns. NOT a 2.2 power: the toe differs by up
//    to 20% down where crater shadows live.
inline float MoonSrgbToLinear(float U) noexcept
{
    if (U <= 0.04045f) return U / 12.92f;
    return std::pow((U + 0.055f) / 1.055f, 2.4f);
}

namespace Detail {

inline float MoonClamp01(float V) noexcept { return V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V); }
inline float MoonMix(float A, float B, float T) noexcept { return A + (B - A) * T; }
inline float MoonSmoothstep(float E0, float E1, float X) noexcept
{
    const float T = MoonClamp01((X - E0) / (E1 - E0));
    return T * T * (3.0f - 2.0f * T);
}

} // namespace Detail

// Bilinear sample of an equirectangular albedo. U wraps (the GPU sampler is REPEAT on every axis), V clamps, and a
//    missing or misshapen view reads as white — the same 1x1 the TextureIndex placeholder decodes to, so a lost
//    file degrades to a pale disc on both pipelines rather than a crash on one.
inline void SampleMoonAlbedo(const MoonAlbedoView& View, float U, float V, float OutRgb[3]) noexcept
{
    OutRgb[0] = OutRgb[1] = OutRgb[2] = 1.0f;
    if (View.Texels == nullptr || View.Width == 0u || View.Height == 0u || View.TexelBytes != 4u) return;
    float W = U - std::floor(U);
    float C = Detail::MoonClamp01(V) * static_cast<float>(View.Height - 1u);
    float R = W * static_cast<float>(View.Width);
    uint32_t X0 = static_cast<uint32_t>(R) % View.Width;
    uint32_t Y0 = static_cast<uint32_t>(C);
    uint32_t X1 = (X0 + 1u) % View.Width;
    uint32_t Y1 = Y0 + 1u < View.Height ? Y0 + 1u : Y0;
    const float Fx = R - std::floor(R);
    const float Fy = C - std::floor(C);
    const uint8_t* T = View.Texels;
    const uint32_t S = View.TexelBytes;
    for (int Ch = 0; Ch < 3; ++Ch)
    {
        const float A = static_cast<float>(T[(static_cast<size_t>(Y0) * View.Width + X0) * S + Ch]) / 255.0f;
        const float B = static_cast<float>(T[(static_cast<size_t>(Y0) * View.Width + X1) * S + Ch]) / 255.0f;
        const float Cc = static_cast<float>(T[(static_cast<size_t>(Y1) * View.Width + X0) * S + Ch]) / 255.0f;
        const float D = static_cast<float>(T[(static_cast<size_t>(Y1) * View.Width + X1) * S + Ch]) / 255.0f;
        // Bilinear in display space, then one decode: the GPU filters post-decode instead, and the two differ by
        //    the EOTF's curvature across a texel — under 2% on moon albedos, and the raster is a proof, not a lab.
        OutRgb[Ch] = MoonSrgbToLinear(Detail::MoonMix(Detail::MoonMix(A, B, Fx), Detail::MoonMix(Cc, D, Fx), Fy));
    }
}

// The panel's moons(), transcribed: textured UV-sphere discs plus halo glow, summed over the list and multiplied
//    by the transmittance the caller integrated — the moon shines THROUGH the air, not in front of it. See the
//    header note for the three conventions that change at this file's border.
inline void EvaluateMoons(const MoonDrawEntry* Entries, uint32_t Count, const float Direction[3],
                          const float Transmittance[3], float OutRgb[3]) noexcept
{
    OutRgb[0] = OutRgb[1] = OutRgb[2] = 0.0f;
    if (Entries == nullptr) return;
    if (Count > kMoonDrawCount) Count = kMoonDrawCount;
    constexpr float kPi = 3.14159265358979323846f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const MoonDrawEntry& E = Entries[I];
        const float* Md = E.Direction;
        const float Ang = E.AngularRadius, Bright = E.Brightness, Phase = E.Phase, GlowAmt = E.Glow;
        const float Spin = E.Spin, TiltA = E.Tilt, Haze = E.Haze, Gam = E.Gamma;
        // A zero-size moon is invisible, not a NaN: the glow's exp(-a/(ang*1.4)) divides by the radius, and at
        //    the exact disc centre that is 0/0. The panel's size slider bottoms at 0.1° so it never meets this;
        //    the pack path clamps too, and this is the backstop for a hand-filled list.
        if (!(Ang > 1e-6f)) continue;
        float Dot = Direction[0] * Md[0] + Direction[1] * Md[1] + Direction[2] * Md[2];
        Dot = Dot < -1.0f ? -1.0f : (Dot > 1.0f ? 1.0f : Dot);
        const float A = std::acos(Dot);
        // The disc frame: the panel crosses with (0,1,0); Z-up crosses with (0,0,1) — except within a degree of
        //    the zenith, where any fixed axis goes degenerate and the panel goes NaN with it.
        const bool NearPole = Md[2] > 0.9998477f || Md[2] < -0.9998477f;
        const float Ax = 0.0f, Ay = NearPole ? 1.0f : 0.0f, Az = NearPole ? 0.0f : 1.0f;
        float Ux = Md[1] * Az - Md[2] * Ay, Uy = Md[2] * Ax - Md[0] * Az, Uz = Md[0] * Ay - Md[1] * Ax;
        const float Ul = std::sqrt(Ux * Ux + Uy * Uy + Uz * Uz);
        if (Ul > 0.0f) { Ux /= Ul; Uy /= Ul; Uz /= Ul; }
        const float Vx = Uy * Md[2] - Uz * Md[1], Vy = Uz * Md[0] - Ux * Md[2], Vz = Ux * Md[1] - Uy * Md[0];
        if (A < Ang * 1.06f)
        {
            const float SinA = std::sin(Ang);
            const float Lx = (Direction[0] * Ux + Direction[1] * Uy + Direction[2] * Uz) / SinA;
            const float Ly = (Direction[0] * Vx + Direction[1] * Vy + Direction[2] * Vz) / SinA;
            const float R2 = Lx * Lx + Ly * Ly;
            const float Z = std::sqrt(R2 < 1.0f ? 1.0f - R2 : 0.0f);
            const float Nx = Lx, Ny = Ly, Nz = Z;
            const float Ct = std::cos(TiltA), St = std::sin(TiltA);
            const float Ntx = Nx, Nty = Ny * Ct - Nz * St, Ntz = Ny * St + Nz * Ct;
            const float Lon = std::atan2(Ntx, Ntz) + Spin;
            float NyC = Nty < -1.0f ? -1.0f : (Nty > 1.0f ? 1.0f : Nty);
            const float Lat = std::asin(NyC);
            float UvU = Lon / (2.0f * kPi) + 0.5f;
            UvU -= std::floor(UvU);
            const float UvV = 0.5f - Lat / kPi;
            float Alb[3];
            SampleMoonAlbedo(E.Albedo, UvU, UvV, Alb);
            Alb[0] = std::pow(Alb[0], Gam) * E.Tint[0];
            Alb[1] = std::pow(Alb[1], Gam) * E.Tint[1];
            Alb[2] = std::pow(Alb[2], Gam) * E.Tint[2];
            const float Ph = Phase * 2.0f * kPi;
            const float LitX = std::sin(Ph), LitY = 0.0f, LitZ = std::cos(Ph);
            const float Wrap = Haze * 0.3f;
            float Ndl = (Nx * LitX + Ny * LitY + Nz * LitZ + Wrap) / (1.0f + Wrap);
            Ndl = Ndl > 0.0f ? Ndl : 0.0f;
            const float EdgeW = Detail::MoonMix(0.975f, 0.88f, Haze);
            const float Edge = 1.0f - Detail::MoonSmoothstep(Ang * EdgeW, Ang * (1.0f + Haze * 0.06f), A);
            const float HzMax = Haze > 0.35f ? Haze : 0.35f;
            const float Limb = Detail::MoonMix(1.0f, 0.5f, (1.0f - Z) * (1.0f - Z) * HzMax);
            const float Disc = Bright * (std::pow(Ndl, 0.8f) * Limb + 0.012f) * Edge;
            OutRgb[0] += Alb[0] * Disc; OutRgb[1] += Alb[1] * Disc; OutRgb[2] += Alb[2] * Disc;
            const float RimA = Detail::MoonSmoothstep(Ang * 0.8f, Ang, A)
                             * (1.0f - Detail::MoonSmoothstep(Ang, Ang * 1.06f, A)) * Haze;
            const float Rim = RimA * Bright * 0.35f * (0.3f + 0.7f * Ndl);
            OutRgb[0] += Rim * E.Tint[0]; OutRgb[1] += Rim * E.Tint[1]; OutRgb[2] += Rim * E.Tint[2];
        }
        const float Lum = 0.4f + 0.6f * (0.5f + 0.5f * std::cos(Phase * 2.0f * kPi));
        const float G = GlowAmt * Bright * (std::exp(-A / (Ang * 1.4f)) * 0.09f + std::exp(-A * 3.5f) * 0.005f) * Lum;
        OutRgb[0] += G * E.Tint[0]; OutRgb[1] += G * E.Tint[1]; OutRgb[2] += G * E.Tint[2];
    }
    OutRgb[0] *= Transmittance[0]; OutRgb[1] *= Transmittance[1]; OutRgb[2] *= Transmittance[2];
}

// Moonlight as the surfaces see it: the panel's ambient loop, Z-up. Tiny (.0025) and phase-gated — a full moon
//    overhead against dark ground, not a second sun. Both pipelines add the same term, so a moonlit frame cannot
//    be bright on one path and black on the other (the lesson the sky ambient's comment records).
inline void EvaluateMoonAmbient(const MoonDrawEntry* Entries, uint32_t Count, float OutRgb[3]) noexcept
{
    OutRgb[0] = OutRgb[1] = OutRgb[2] = 0.0f;
    if (Entries == nullptr) return;
    if (Count > kMoonDrawCount) Count = kMoonDrawCount;
    constexpr float kPi = 3.14159265358979323846f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const MoonDrawEntry& E = Entries[I];
        const float Up = E.Direction[2] > 0.0f ? E.Direction[2] : 0.0f;
        const float Term = E.Brightness * Up * 0.0025f * (0.5f + 0.5f * std::cos(E.Phase * 2.0f * kPi));
        OutRgb[0] += E.Tint[0] * Term; OutRgb[1] += E.Tint[1] * Term; OutRgb[2] += E.Tint[2] * Term;
    }
}

} // namespace Frontier
