//============================================================================================================================================
// 📦 Engine/DisplayPresentation/DenoiseGuide.h — the denoiser DETAIL-GUIDE selector, shared by the Control Centre and the integrator
//============================================================================================================================================
// A "guide" is a cheap, deterministic per-pixel signal that tells the à-trous denoiser "this pixel is genuine
//    high-frequency DETAIL, not Monte-Carlo noise — fade the filtered result back toward the raw sample here".
//    The shipped filter edge-stops on GEOMETRY (normal/depth), so it protects silhouettes but blurs anything flat
//    in geometry yet sharp in SHADING: metallic flakes, mirror/glass reflections, small luminaires, thin rims.
//    Each guide re-injects one of those shading-detail classes. See FlakeVerification/Denoise for the prototype.
//
// The enum value is passed to AtrousDenoise.slang as the GuideMode push constant; Standard is the pre-guide filter,
//    so a build that never assigns a guide renders exactly as before.

#pragma once

#include <cstdint>

namespace Frontier {

enum class DenoiseGuideCategory : uint32_t
{
    Standard  = 0,   // the shipped edge-avoiding à-trous, no detail guide (identity)
    Flakes    = 1,   // preserve metallic flake sparkle (System-B flake coverage mask)
    Reflections = 2, // preserve low effective-roughness reflections (chrome/gold/glass/clearcoat)
    Emissive  = 3,   // preserve luminaires (emissive luminance — they carry no noise)
    Fresnel   = 4,   // preserve grazing-angle rim highlights, pow(1 - N·V, 4)
    Edges     = 5,   // preserve silhouettes / contact shadows (normal + depth gradient)
    Smart     = 6,   // max of all of the above — the recommended all-in-one default
    Count     = 7
};

// The label shown on the Control Centre pill and in the status line.
inline const char* DenoiseGuideLabel(DenoiseGuideCategory Guide) noexcept
{
    switch (Guide)
    {
        case DenoiseGuideCategory::Standard:    return "Standard";
        case DenoiseGuideCategory::Flakes:      return "Flakes";
        case DenoiseGuideCategory::Reflections: return "Reflections";
        case DenoiseGuideCategory::Emissive:    return "Emissive";
        case DenoiseGuideCategory::Fresnel:     return "Fresnel Rim";
        case DenoiseGuideCategory::Edges:       return "Edges";
        case DenoiseGuideCategory::Smart:       return "Smart";
        default:                                return "Standard";
    }
}

// One tap of the Control Centre cycler advances to the next guide, wrapping past Smart back to Standard.
inline DenoiseGuideCategory NextDenoiseGuide(DenoiseGuideCategory Guide) noexcept
{
    return static_cast<DenoiseGuideCategory>(
        (static_cast<uint32_t>(Guide) + 1u) % static_cast<uint32_t>(DenoiseGuideCategory::Count));
}

}  // namespace Frontier
