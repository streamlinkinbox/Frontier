/*==========================================================================================================================================
                                                            DENOISESECTION.CPP
==========================================================================================================================================*/
// 🧩 Denoising & antialiasing: the variance filter (SVGF) with its diffuse and glossy lobes, wavelet levels by the pixels they reach,
//    and the temporal antialiasing.

#include "Engine/Interface/Inspectors/RenderSettings/RenderSettingsSections.h"

#if FRONTIER_EDITOR && defined(FRONTIER_WITH_INTERFACE_SDK)

#include "Engine/Interface/Controls/PageControls.h"
#include "Engine/Interface/InspectorControls.h"

#include <algorithm>
#include <cstdio>

namespace Frontier
{

namespace
{

bool ConstructLobe(ControlColumn& Page, const char* Lobe, const char* Prefix, int& Levels, int& Tally, float& Weight, float& Sigma, int TraceScale,
                   const char* Unavailable)
{
    using namespace SectionRows;
    // 📝 À-trous step 2^(n-1) trace pixels at level n: the widest tap reaches 2 · 2^(n-1) trace pixels, × trace scale on screen.
    const int Reach = 2 * (1 << (std::clamp(Levels, 1, 4) - 1)) * std::clamp(TraceScale, 1, 4);
    char Heading[64], Id[64];
    std::snprintf(Heading, sizeof(Heading), "%s lobe \xC2\xB7 reach %d px", Lobe, Reach);
    SubHeading(Page, Heading);
    bool Changed = false;
    std::snprintf(Id, sizeof(Id), "%sWaveletLevels", Prefix);
    Changed |= Count(Page, Id, "\xC3\x80-trous wavelet levels", Levels, 1, 4, "", Unavailable);
    std::snprintf(Id, sizeof(Id), "%sTallyLimit", Prefix);
    Changed |= Count(Page, Id, "History limit", Tally, 1, 64, "", Unavailable, "Low: responsive; high: stable");
    std::snprintf(Id, sizeof(Id), "%sMinimumWeight", Prefix);
    Changed |= Amount(Page, Id, "Minimum blend weight", Weight, 0.01f, 0.5f, 0.01f, "", Unavailable);
    std::snprintf(Id, sizeof(Id), "%sColourSigma", Prefix);
    Changed |= Amount(Page, Id, "Colour edge stop", Sigma, 0.5f, 16.0f, 0.5f, "\xCF\x83", Unavailable);
    return Changed;
}

} // namespace

bool DenoiseSection::Construct(ControlColumn& Page, RenderPipelineConfiguration& P, const RenderSettingsReading& Reading)
{
    using namespace SectionRows;
    char Status[48];
    std::snprintf(Status, sizeof(Status), "%s \xC2\xB7 %d / %d levels", P.VarianceFilter ? "SVGF" : "Off", P.DiffuseWaveletLevels, P.GlossyWaveletLevels);
    BeginSection(Page, "Denoising & antialiasing", "Layers", Status,
                 "Temporal moments, then \xC3\xA0-trous wavelet levels per lobe, then a joint bilateral projection onto every screen pixel.");
    (void)Reading;
    const char* Lighting = RenderAvailability::Lighting(P);
    const char* Filter   = RenderAvailability::Denoiser(P);
    bool Changed = Switch(Page, "VarianceFilter", "Denoiser (SVGF)", P.VarianceFilter, Lighting, "Temporal moments, then \xC3\xA0-trous wavelet iterations per lobe");
    Changed |= ConstructLobe(Page, "Diffuse", "Diffuse", P.DiffuseWaveletLevels, P.DiffuseTallyLimit, P.DiffuseMinimumWeight, P.DiffuseColourSigma, P.TraceScale,
                             Filter);
    Changed |= ConstructLobe(Page, "Glossy", "Glossy", P.GlossyWaveletLevels, P.GlossyTallyLimit, P.GlossyMinimumWeight, P.GlossyColourSigma, P.TraceScale,
                             Filter);
    SubHeading(Page, "Antialiasing");
    Changed |= Switch(Page, "TemporalAntiAliasing", "Temporal antialiasing", P.TemporalAntiAliasing, nullptr,
                      "Sub-pixel jitter resolved over 16 refreshes; also resolves dithered tyre marks");
    EndSection(Page);
    return Changed;
}

} // namespace Frontier

#endif
