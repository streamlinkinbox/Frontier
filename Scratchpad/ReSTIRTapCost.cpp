//============================================================================================================================================
// 📦 Scratchpad/ReSTIRTapCost.cpp — measures what the ReSTIR spatial cross actually costs, before and after tier-keying
//============================================================================================================================================
// WHY THIS EXISTS, AND WHAT IT IS NOT
//
// ReSTIRViewport.slang runs on the GPU and has no CPU mirror, and this sandbox has no GPU (no /dev/dri, no Vulkan
//    driver). So a frame-time measurement is impossible here and nothing in this file pretends otherwise. What IS
//    measurable, exactly and without a device, is the QUANTITY OF WORK the kernel issues per pixel: the number of
//    PHatFull evaluations, each of which is a full OpenPBR EvaluateBsdf — GGX D and G2, Fresnel, EON diffuse, plus
//    coat and sheen lobes when weighted. That count is the thing #4 changes; the wall clock is downstream of it.
//
// So this harness does two things:
//    ① counts PHatFull evaluations per pixel per tier, by walking the same control flow the kernel walks, and
//    ② times a REAL EvaluateBsdf-shaped workload on the CPU, so the count can be quoted as a duration rather than
//       an abstraction. The BSDF stand-in is a faithful arithmetic transcription of the lobe set (same transcendental
//       mix: pow, exp, sqrt, rsqrt, divides), NOT the engine's shader — it exists to give the count a unit, and the
//       ratio between before and after is what carries meaning, not the absolute milliseconds.
//
// Read the output as: "the spatial cross is N% of the lighting budget at tier T, and tier-keying removes M% of it."
//    Do not read it as a frame time. The GPU timestamps (VisibilityExchange.cpp already owns a 12-slot query pool)
//    are what will settle frame time on real hardware.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                    THE COST MODEL — one PHatFull, counted and timed
//------------------------------------------------------------------------------------------------------------------------

// Global tally, incremented exactly where the kernel would call PHatFull.
uint64_t gPHatCalls = 0u;

// A stand-in for MaterialEvaluation.slang's EvaluateBsdf with the same arithmetic shape: GGX D + G2 (Smith),
//    Schlick Fresnel, EON-style diffuse, and the coat lobe. Volatile sink keeps the optimiser from deleting it.
volatile float gSink = 0.0f;

inline float EvaluateBsdfShaped(float nl, float nv, float rough, float metal, float coat) noexcept
{
    ++gPHatCalls;

    const float a  = rough * rough < 0.05f ? 0.05f : rough * rough;
    const float a2 = a * a;

    // GGX D
    const float nh    = 0.5f * (nl + nv);
    const float denom = nh * nh * (a2 - 1.0f) + 1.0f;
    const float D     = a2 / (3.14159265f * denom * denom);

    // Smith G2 (height-correlated form: two sqrts, as in the real lobe)
    const float lv = nl * std::sqrt(nv * nv * (1.0f - a2) + a2);
    const float ll = nv * std::sqrt(nl * nl * (1.0f - a2) + a2);
    const float G2 = 0.5f / (lv + ll + 1e-6f);

    // Schlick Fresnel — the pow5 the real lobe spells out
    const float c1 = 1.0f - nh;
    const float F  = 0.04f + 0.96f * (c1 * c1 * c1 * c1 * c1);

    // EON-ish diffuse with the retro-reflection term (a divide and a pow)
    const float fd = (1.0f - metal) * (1.0f - F) * (1.0f / 3.14159265f)
                   * (1.0f + 0.5f * rough * std::pow(1.0f - nl, 5.0f));

    float result = fd + D * G2 * F;

    // Coat lobe: a second GGX + a Fresnel + the absorption exp, taken only when weighted, as in the real code.
    if (coat > 0.0f)
    {
        const float ca  = 0.08f;
        const float cd  = ca / (3.14159265f * (nh * nh * (ca - 1.0f) + 1.0f) * (nh * nh * (ca - 1.0f) + 1.0f));
        const float cf  = 0.04f + 0.96f * (c1 * c1 * c1 * c1 * c1);
        const float abs_ = std::exp(-0.35f / (nl + 1e-4f));
        result = coat * cd * cf + result * abs_;
    }

    return result;
}

//------------------------------------------------------------------------------------------------------------------------
//                          THE CONTROL FLOW — transcribed from ReSTIRViewport.slang's main()
//------------------------------------------------------------------------------------------------------------------------
// Line references are to ReSTIRViewport.slang as of this commit. Every PHatFull call site in the kernel appears
//    here exactly once, in the same order and under the same conditions, so the tally is the kernel's tally.

struct TierSetup
{
    const char* Name;
    uint32_t    M;              // ReSTIRCandidateSampleCount
    uint32_t    Extra;          // ReSTIRExtraCandidateCount
    uint32_t    SpatialTaps;    // kSpatialTaps — the constant #4 makes tier-keyed
};

// Simulates one pixel's reservoir work and returns nothing; the tally is the measurement.
void SimulatePixel(const TierSetup& T, bool TemporalValid, bool SpatialTapValid, float nl, float nv,
                   float rough, float metal, float coat) noexcept
{
    // :823 initial candidate loop — M evaluations
    for (uint32_t s = 0u; s < T.M; ++s)
        EvaluateBsdfShaped(nl, nv, rough, metal, coat);

    // :849 pHatSel for the unbiased weight
    EvaluateBsdfShaped(nl, nv, rough, metal, coat);

    // :855 extra same-pixel candidates
    for (uint32_t e = 0u; e < T.Extra; ++e)
        EvaluateBsdfShaped(nl, nv, rough, metal, coat);

    // :873 recompute W for the final selection
    EvaluateBsdfShaped(nl, nv, rough, metal, coat);

    // :900,:902,:917 temporal reuse — pPrev, pCur, pFin, only when the reprojected sample validates
    if (TemporalValid)
    {
        EvaluateBsdfShaped(nl, nv, rough, metal, coat);
        EvaluateBsdfShaped(nl, nv, rough, metal, coat);
        EvaluateBsdfShaped(nl, nv, rough, metal, coat);
    }

    // :936 spatial cross — per VALID tap: pNeigh, pSelf, pM
    for (uint32_t t = 0u; t < T.SpatialTaps; ++t)
    {
        if (!SpatialTapValid) continue;
        EvaluateBsdfShaped(nl, nv, rough, metal, coat);   // :955 pNeigh
        EvaluateBsdfShaped(nl, nv, rough, metal, coat);   // :958 pSelf
        EvaluateBsdfShaped(nl, nv, rough, metal, coat);   // :975 pM
    }
}

struct Measurement
{
    uint64_t Calls;
    double   Millis;
};

Measurement Run(const TierSetup& T, uint32_t Pixels, bool TemporalValid, bool SpatialValid)
{
    gPHatCalls = 0u;
    const auto Start = std::chrono::steady_clock::now();

    float acc = 0.0f;
    for (uint32_t p = 0u; p < Pixels; ++p)
    {
        // Vary the surface so the branches and transcendentals see real data, not one cached value.
        const float f     = static_cast<float>(p & 1023u) * (1.0f / 1023.0f);
        const float nl    = 0.05f + 0.9f * f;
        const float nv    = 0.05f + 0.9f * (1.0f - f);
        const float rough = 0.08f + 0.7f * f;
        const float metal = (p & 7u) == 0u ? 1.0f : 0.0f;
        const float coat  = (p & 3u) == 0u ? 0.5f : 0.0f;
        SimulatePixel(T, TemporalValid, SpatialValid, nl, nv, rough, metal, coat);
        acc += static_cast<float>(gPHatCalls & 1u);
    }
    gSink = acc;

    const auto End = std::chrono::steady_clock::now();
    return { gPHatCalls, std::chrono::duration<double, std::milli>(End - Start).count() };
}

}   // namespace

int main(int argc, char** argv)
{
    // The five tiers, with kSpatialTaps as it is TODAY: a hardcoded 4 for every tier.
    const TierSetup Before[5] = {
        { "Minimal",   1u, 0u, 4u },
        { "Economy",   2u, 1u, 4u },
        { "Standard",  4u, 2u, 4u },
        { "Ultra",     8u, 3u, 4u },
        { "Reference", 16u, 4u, 4u },
    };

    // The proposed ladder: the cross scales with the tier like every other knob. Reference is unchanged at 4,
    //    so the top of the ladder — the tier whose whole purpose is the most realistic image — is untouched.
    const TierSetup After[5] = {
        { "Minimal",   1u, 0u, 0u },
        { "Economy",   2u, 1u, 1u },
        { "Standard",  4u, 2u, 2u },
        { "Ultra",     8u, 3u, 3u },
        { "Reference", 16u, 4u, 4u },
    };

    const bool AfterMode = argc > 1 && std::string_view(argv[1]) == "after";
    const TierSetup* Setup = AfterMode ? After : Before;

    constexpr uint32_t kPixels = 1280u * 720u;   // one 720p frame's worth of shaded pixels

    std::printf("[ReSTIRTapCost] %s — kSpatialTaps %s\n",
                AfterMode ? "AFTER  (tier-keyed cross)" : "BEFORE (hardcoded 4-tap cross)",
                AfterMode ? "= 0/1/2/3/4 by tier" : "= 4 for every tier");
    std::printf("[ReSTIRTapCost] %u pixels/frame, temporal valid, all spatial taps valid (worst case)\n\n", kPixels);
    std::printf("  %-10s %4s %5s %5s  %12s  %10s  %10s\n",
                "tier", "M", "extra", "taps", "PHat/px", "PHat/frame", "ms/frame");

    for (uint32_t i = 0u; i < 5u; ++i)
    {
        const Measurement Mres = Run(Setup[i], kPixels, true, true);
        std::printf("  %-10s %4u %5u %5u  %12.1f  %10llu  %10.1f\n",
                    Setup[i].Name, Setup[i].M, Setup[i].Extra, Setup[i].SpatialTaps,
                    static_cast<double>(Mres.Calls) / kPixels,
                    static_cast<unsigned long long>(Mres.Calls), Mres.Millis);
    }

    return 0;
}
