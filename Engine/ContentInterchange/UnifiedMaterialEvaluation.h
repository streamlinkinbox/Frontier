//============================================================================================================================================
//                                              UNIFIEDMATERIALEVALUATION.H
//============================================================================================================================================
// 🧩 ONE material model, for every CPU render path. Include this and you get the engine's shipped OpenPBR lobe set
//    (Engine/Shaders/MaterialEvaluation.slang compiled 1:1 as C++) plus the slab → ShadingRecord transcription that
//    feeds it. Nothing else in the tree may define a second one.
//
//    WHY THIS FILE EXISTS.  The three CPU render paths used to evaluate three different material models:
//
//       VisibilityRaster    a hand-rolled Lambert + single GGX lobe off MaterialRecord's flattened 64-byte header
//                           (AlbedoR/G/B, Roughness, Metalness). No coat, no flakes, no transmission, no fuzz,
//                           no thin film, no energy compensation.
//       SurfelReference     a local MaterialAlbedo() switch returning one RGB constant per family.
//       MaterialLevelViewport  the real thing — MaterialEvaluation.slang through a private TranscribeShadingRecord.
//
//    That made the "GI off / GI on / RT on" comparison meaningless: the columns differed by MATERIAL MODEL as much
//    as by light transport, which is the one thing such a comparison must hold fixed. Worse, VisibilityRaster's own
//    header documented the reduction as if it were a property of rasterisation ("direct-only lookdev PBR — Lambert
//    plus a GGX"). It never was. Rasterisation determines how VISIBILITY is resolved; it says nothing whatsoever
//    about which BSDF you evaluate once a pixel knows which triangle it sees. A raster path can evaluate the full
//    lobe set, and now does.
//
//    The precedent is already in VisibilityRaster.cpp, for the tone curve: "This path used to apply plain Reinhard
//    with no exposure ... while the ReSTIR kernel applied ACES with both, so the same radiance reached the screen up
//    to 49/255 apart depending on which path drew it. That was survivable while the two drew different things; it is
//    not, now that one sky feeds both." The same argument applies to the BSDF, and this file finishes the job.
//
//    WHAT STILL LEGITIMATELY DIFFERS between the paths — and must, because it is the whole point:
//       * transport: raster has no ray queries at all (its quarantine gate greps for them), Surfel carries a bounced
//         term through a world-space surfel field, ReSTIR resamples reservoirs of traced direct light.
//       * visibility: shadow maps vs traced occlusion.
//       * sampling: one shading point per pixel vs many.
//    None of those are reasons to evaluate a different BRDF.
//
//    BINDING.  MaterialEvaluation.slang's CPU branch needs the energy/sheen LUTs supplied by the host. Call
//    Frontier::UnifiedMaterial::BindTables once before shading; the Fetch* functions below are the shim the shader
//    text calls by unqualified name, exactly as the GPU branch calls its samplers.

#pragma once

#include "DisplayPresentation/ShadingTableCodec.h"
#include "ContentInterchange/MaterialIndex.h"
#include "Shaders/SlangCpuShim.h"

namespace Frontier::UnifiedMaterial {

// The energy-compensation and LTC-sheen tables, bound once by whichever host owns them. A null pointer here is a
//    programming error, not a recoverable state: shading cannot proceed without the tables the lobe set indexes.
inline const Frontier::ShadingTableSet* g_Tables = nullptr;

inline void BindTables(const Frontier::ShadingTableSet& Tables) noexcept { g_Tables = &Tables; }
[[nodiscard]] inline bool TablesBound() noexcept { return g_Tables != nullptr; }

// Bake-on-first-use, so a host that never heard of the LUTs still shades correctly instead of silently taking a
//    fallback path. A host that owns its own tables (MaterialLevelViewport does) calls BindTables first and this
//    never bakes. 1024 samples/cell matches what that host asks for. Function-local static init is thread-safe.
inline const Frontier::ShadingTableSet& EnsureTables() noexcept
{
    static const Frontier::ShadingTableSet Baked = Frontier::ShadingTableCodec::Bake(1024u);
    if (g_Tables == nullptr) g_Tables = &Baked;
    return *g_Tables;
}

} // namespace Frontier::UnifiedMaterial

// ── the shader text's LUT fetches, at global scope because that is how the .slang calls them ─────────────────────
inline vec3 FetchEnergy(float mu, float alpha)
{
    float Out[3] = { 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleEnergy(*Frontier::UnifiedMaterial::g_Tables, mu, alpha, Out);
    return vec3(Out[0], Out[1], Out[2]);
}

inline vec3 FetchSheen(float mu, float alpha)
{
    float Out[3] = { 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleSheen(*Frontier::UnifiedMaterial::g_Tables, mu, alpha, Out);
    return vec3(Out[0], Out[1], Out[2]);
}

inline vec4 FetchSheenFull(float mu, float alpha)
{
    float Out[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    Frontier::ShadingTableCodec::SampleSheenFull(*Frontier::UnifiedMaterial::g_Tables, mu, alpha, Out);
    return vec4(Out[0], Out[1], Out[2], Out[3]);
}

#define FRONTIER_AUTOMOTIVE_SHOWCASE 1
// INTERNAL LINKAGE. MaterialEvaluation.slang is shader text: its functions are written at namespace scope
//    with external linkage, which is fine while exactly one translation unit in a binary includes it. The
//    moment two do -- here, VisibilityRaster and SurfelReference in the same link -- the linker reports
//    multiple definition of EvaluateBsdf, SampleBsdf, PdfBsdf and the rest. An anonymous namespace gives each
//    TU its own copy, which is what an inline function would have done had the shader been written as one.
//    Nothing crosses a TU boundary here: every caller evaluates the lobe set locally.
namespace {
#include "Shaders/MaterialEvaluation.slang"   // the shipped OpenPBR lobe set, compiled 1:1 as C++
}

namespace Frontier::UnifiedMaterial {

// A transcription of ReSTIRViewport.slang's `ResolveMaterial` for the constants-only case (no textures bound).
//    Every line corresponds to a line of that function; the channel fetches all resolve to their documented default
//    (`SampleChannel*` with no slot returns vec4(1.0) / vec4(1.0, 0.5, 1.0, 1.0) for anisotropy), which is why the
//    folds collapse to plain multiplies. Keeping the SHAPE of the kernel's code — rather than writing "the obvious
//    fold" — is what makes this the engine's material model instead of a lookalike.
//
//    Lifted verbatim out of Projects/Project-Zero/Host/MaterialLevelViewport.cpp, where it was a private host
//    detail. A per-host copy of the material resolve is precisely how the three paths drifted apart.
[[nodiscard]] inline ShadingRecord Transcribe(const Frontier::MaterialSlabRecord& S, uint32_t Selection) noexcept
{
    ShadingRecord m;
    m.AutomotiveData     = vec4(S.Reserved0, S.Reserved1, 0.0f, 0.0f);
    m.AutomotiveDensity  = S.SlateGlintDensity;
    m.BaseColor          = vec3(S.BaseWeight * S.BaseColorR, S.BaseWeight * S.BaseColorG, S.BaseWeight * S.BaseColorB);
    m.Metalness          = S.BaseMetalness;
    m.DiffuseRoughness   = S.BaseDiffuseRoughness;
    m.SpecularWeight     = S.SpecularWeight;
    m.SpecularColor      = vec3(S.SpecularColorR, S.SpecularColorG, S.SpecularColorB);
    m.SpecularRoughness  = S.SpecularRoughness;
    m.SpecularAnisotropy = S.SpecularRoughnessAnisotropy;
    m.AnisotropyAngle    = S.AnisotropyRotation;
    m.SpecularIor        = S.SpecularIor;
    m.ThinFilmWeight     = S.ThinFilmWeight;
    m.ThinFilmThickness  = S.ThinFilmThickness;
    m.ThinFilmIor        = S.ThinFilmIor;
    m.HazinessWeight     = S.SlateHazinessWeight;
    m.HazinessRoughness  = S.SlateHazinessRoughness;
    m.CoatWeight         = S.CoatWeight;
    m.CoatColor          = vec3(S.CoatColorR, S.CoatColorG, S.CoatColorB);
    m.CoatRoughness      = S.CoatRoughness;
    m.CoatAnisotropy     = S.CoatRoughnessAnisotropy;
    m.CoatIor            = S.CoatIor;
    m.CoatDarkening      = S.CoatDarkening;
    m.CoatTangent        = vec3(1.0f, 0.0f, 0.0f);
    m.CoatNormal         = vec3(0.0f, 0.0f, 1.0f);
    m.FuzzWeight         = S.FuzzWeight;
    m.FuzzColor          = vec3(S.FuzzColorR, S.FuzzColorG, S.FuzzColorB);
    m.FuzzRoughness      = S.FuzzRoughness;
    m.Emission           = vec3(S.EmissionLuminance * S.EmissionColorR, S.EmissionLuminance * S.EmissionColorG,
                                S.EmissionLuminance * S.EmissionColorB);
    m.TransmissionWeight = S.TransmissionWeight;
    m.TransmissionColor  = vec3(S.TransmissionColorR, S.TransmissionColorG, S.TransmissionColorB);
    m.TransmissionDepth  = S.TransmissionDepth;
    m.TransmissionThickness = 0.0f;
    m.Selection          = Selection;
    m.SssWeight          = S.SubsurfaceWeight;
    m.SssColor           = vec3(S.SubsurfaceColorR, S.SubsurfaceColorG, S.SubsurfaceColorB);
    m.SssRadius          = S.SubsurfaceRadius;
    m.SssRadiusScale     = vec3(S.SubsurfaceRadiusScaleR, S.SubsurfaceRadiusScaleG, S.SubsurfaceRadiusScaleB);
    m.SssThickness       = 0.0f;
    return m;
}

// The automotive flake lobe is the one part of the lobe set that needs PER-HIT data: the glint cell lookup is
//    driven by a scaled UV and by the ray footprint on the surface. A ShadingRecord that has been transcribed but
//    not bound carries AutomotiveData.zw = 0 and AutomotiveFootprint = 0, and the flake evaluation then collapses
//    -- which renders a metallic-flake car essentially black rather than merely flake-less. Any path that shades an
//    automotive slab MUST call this; it early-returns for every other material, exactly as the host's copy does.
//
//    Lifted from MaterialLevelViewport::BindAutomotiveHit so there is one definition rather than one per renderer.
inline void BindAutomotiveHit(ShadingRecord& m,
                              const vec3& P0, const vec3& P1, const vec3& P2,
                              float U0, float V0, float U1, float V1, float U2, float V2,
                              float u, float v, float distance,
                              const vec3& direction, const vec3& normal,
                              float raySpread = 0.001f) noexcept
{
    if (m.AutomotiveData.x < 1.0f || m.AutomotiveData.x > 5.0f) return;
    m.AutomotiveData.z = (U0 * (1.0f - u - v) + U1 * u + U2 * v) * 3.4557519f;
    m.AutomotiveData.w = (V0 * (1.0f - u - v) + V1 * u + V2 * v) * 1.7278760f;
    const float worldArea = length(cross(P1 - P0, P2 - P0));
    const float uvArea = std::abs((U1 - U0) * (V2 - V0) - (V1 - V0) * (U2 - U0));
    m.AutomotiveFootprint = 3.4557519f * std::sqrt(uvArea / std::max(worldArea, 1e-12f))
                          * distance * raySpread / std::max(std::abs(dot(direction, normal)), 0.001f);
}

// Build an orthonormal shading frame about N. EvaluateBsdf works in the local frame with +Z = shading normal, so
//    every caller needs this; duplicating it per renderer is how the paths drift.
inline void ShadingFrame(const vec3& N, vec3& T, vec3& B) noexcept
{
    // Duff et al. 2017 branchless ONB — stable for N.z near -1, where the naive cross-with-up degenerates.
    const float Sign = (N.z >= 0.0f) ? 1.0f : -1.0f;
    const float a = -1.0f / (Sign + N.z);
    const float b = N.x * N.y * a;
    T = vec3(1.0f + Sign * N.x * N.x * a, Sign * b, -Sign * N.x);
    B = vec3(b, Sign + N.y * N.y * a, -N.y);
}

inline vec3 ToLocal(const vec3& V, const vec3& T, const vec3& B, const vec3& N) noexcept
{
    return vec3(V.x * T.x + V.y * T.y + V.z * T.z,
                V.x * B.x + V.y * B.y + V.z * B.z,
                V.x * N.x + V.y * N.y + V.z * N.z);
}

// The one call every renderer makes. `wo` and `wi` are WORLD-space and both point away from the surface.
//    Returns f (no cosine), exactly as EvaluateBsdf does.
[[nodiscard]] inline vec3 EvaluateWorld(const ShadingRecord& m, const vec3& N,
                                        const vec3& woWorld, const vec3& wiWorld) noexcept
{
    vec3 T, B;
    ShadingFrame(N, T, B);
    const vec3 wo = ToLocal(woWorld, T, B, N);
    const vec3 wi = ToLocal(wiWorld, T, B, N);
    if (wo.z <= 0.0f) return vec3(0.0f, 0.0f, 0.0f);
    ResolvedLayers L = ResolveLayers(m, wo);
    return EvaluateBsdf(m, L, wo, wi);
}

} // namespace Frontier::UnifiedMaterial

// ── ambient / environment response ──────────────────────────────────────────────────────────────────────────
// The GI-off paths need "what fraction of a uniform environment does this material send to the eye", i.e.
//     integral over the hemisphere of f(wo, wi) cos(theta) dwi
// and they need it WITHOUT casting a ray. The obvious shortcut -- evaluate f once along the normal and multiply
// by pi -- is correct only for a Lambert lobe. On a clearcoated or metallic slab f(N, N) sits on the specular
// peak, so pi * f is enormous and the material renders blown-out white. (It did exactly that here.) Clamping the
// result does not help: the peak is orders of magnitude over unity, so the clamp just paints it white instead.
//
// This is a small fixed cosine-weighted quadrature instead: 8 stratified directions, estimator (2*pi/N) sum f cos.
// Deterministic, no rays, ~8 BSDF evaluations per shaded pixel, and it cannot exceed the material's true
// hemispherical reflectance because it IS an estimate of that integral.
namespace Frontier::UnifiedMaterial {

//------------------------------------------------------------------------------------------------------------------------
//                                           SLAB RECORD → SHADING RECORD
//------------------------------------------------------------------------------------------------------------------------
// The device-side MaterialSlabRecord unpacked into the evaluator's ShadingRecord. Lifted out of
//    MaterialLevelViewport::TranscribeShadingRecord so there is ONE transcription rather than one per host —
//    it was the third hand-written copy of this mapping, after the kernel's ResolveMaterial and the surfel
//    passes' (which did not have one at all, and approximated instead).
//
//    ⚠️ This is the no-texture case: a slab's constant channels only. ResolveMaterial on the device also folds
//    in the bound textures and the surface frame; a host that has textures must do the same after calling this.
//    CoatTangent / CoatNormal are the identity frame for the same reason.

[[nodiscard]] inline ShadingRecord MakeShadingRecord(const MaterialSlabRecord& S, uint32_t Selection) noexcept
{
    ShadingRecord m;
    m.AutomotiveData=vec4(S.Reserved0,S.Reserved1,0.0f,0.0f);m.AutomotiveDensity=S.SlateGlintDensity;
    m.BaseColor          = vec3(S.BaseWeight * S.BaseColorR, S.BaseWeight * S.BaseColorG, S.BaseWeight * S.BaseColorB);
    m.Metalness          = S.BaseMetalness;
    m.DiffuseRoughness   = S.BaseDiffuseRoughness;
    m.SpecularWeight     = S.SpecularWeight;
    m.SpecularColor      = vec3(S.SpecularColorR, S.SpecularColorG, S.SpecularColorB);
    m.SpecularRoughness  = S.SpecularRoughness;
    m.SpecularAnisotropy = S.SpecularRoughnessAnisotropy;                 // × anisoTex.z (=1 unbound)
    m.AnisotropyAngle    = S.AnisotropyRotation;                          // atan(1,0)=0 + Slate2.x
    m.SpecularIor        = S.SpecularIor;
    m.ThinFilmWeight     = S.ThinFilmWeight;
    m.ThinFilmThickness  = S.ThinFilmThickness;
    m.ThinFilmIor        = S.ThinFilmIor;
    m.HazinessWeight     = S.SlateHazinessWeight;
    m.HazinessRoughness  = S.SlateHazinessRoughness;
    m.CoatWeight         = S.CoatWeight;
    m.CoatColor          = vec3(S.CoatColorR, S.CoatColorG, S.CoatColorB);
    m.CoatRoughness      = S.CoatRoughness;
    m.CoatAnisotropy     = S.CoatRoughnessAnisotropy;
    m.CoatIor            = S.CoatIor;
    m.CoatDarkening      = S.CoatDarkening;
    m.CoatTangent        = vec3(1.0f, 0.0f, 0.0f);                        // identity (no coat normal texture bound)
    m.CoatNormal         = vec3(0.0f, 0.0f, 1.0f);
    m.FuzzWeight         = S.FuzzWeight;
    m.FuzzColor          = vec3(S.FuzzColorR, S.FuzzColorG, S.FuzzColorB);
    m.FuzzRoughness      = S.FuzzRoughness;
    m.Emission           = vec3(S.EmissionLuminance * S.EmissionColorR, S.EmissionLuminance * S.EmissionColorG,
                                S.EmissionLuminance * S.EmissionColorB);
    m.TransmissionWeight = S.TransmissionWeight;
    m.TransmissionColor  = vec3(S.TransmissionColorR, S.TransmissionColorG, S.TransmissionColorB);
    m.TransmissionDepth  = S.TransmissionDepth;
    m.TransmissionThickness = 0.0f;                                       // tracer-side: the true chord, or the no-exit fallback
    m.Selection          = Selection;
    m.SssWeight          = S.SubsurfaceWeight;
    m.SssColor           = vec3(S.SubsurfaceColorR, S.SubsurfaceColorG, S.SubsurfaceColorB);
    m.SssRadius          = S.SubsurfaceRadius;
    m.SssRadiusScale     = vec3(S.SubsurfaceRadiusScaleR, S.SubsurfaceRadiusScaleG, S.SubsurfaceRadiusScaleB);
    m.SssThickness       = 0.0f;                                          // filled per hit by SssChord
    return m;
}


[[nodiscard]] inline vec3 AmbientResponse(const ShadingRecord& m, const vec3& N, const vec3& woWorld) noexcept
{
    vec3 T, B;
    ShadingFrame(N, T, B);
    const vec3 wo = ToLocal(woWorld, T, B, N);
    if (wo.z <= 0.0f) return vec3(0.0f, 0.0f, 0.0f);
    ResolvedLayers L = ResolveLayers(m, wo);

    constexpr int kSamples = 8;
    // Fixed low-discrepancy pairs: cosine-weighted hemisphere, so the cosine cancels and the estimator is
    //    (1/N) sum f * pi. Stratified in a spiral to avoid clumping on the specular lobe.
    vec3 Sum(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < kSamples; ++i)
    {
        const float u1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSamples);
        const float u2 = 0.618033988f * static_cast<float>(i);
        const float r = std::sqrt(u1);
        const float phi = 2.0f * kPi * (u2 - std::floor(u2));
        const vec3 wi(r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u1)));
        const vec3 f = EvaluateBsdf(m, L, wo, wi);
        Sum = Sum + f;   // cosine-weighted pdf = cos/pi cancels the cosine, leaving f * pi / N
    }
    const float k = kPi / static_cast<float>(kSamples);
    return vec3(Sum.x * k, Sum.y * k, Sum.z * k);
}

} // namespace Frontier::UnifiedMaterial
