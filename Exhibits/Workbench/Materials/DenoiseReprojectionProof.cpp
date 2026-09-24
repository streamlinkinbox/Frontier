//============================================================================================================================================
//                                                DENOISEREPROJECTIONPROOF.CPP
//============================================================================================================================================
// M9 gate — the re-enable milestone, proved without a GPU:
//
//    §A  the live configuration. `Engine/DisplayPresentation/ReSTIRIntegrator.cpp` compiled as a real TU (plus the
//        tier ladder): the denoiser and the motion-vector reprojection default ON, no tier turns them off, and
//        `BuildDispatch` sets exactly the feature bits the shader reads — for every on/off combination.
//    §B  the shipped shaders, audited. ReSTIRViewport.slang's accumulation and R7a reprojection, AtrousDenoise.slang's
//        bindings and weight terms, and the dispatcher's level chain are pinned as TEXT. Pins are deliberate: these
//        two files cannot be compiled here (they are full GLSL compute kernels: bindless tables, BVH traversal), so
//        the honest guard is a no-drift audit that fails the moment a pinned line moves — plus the section that
//        pins the one shader that CAN be compiled.
//    §C  the real à-trous filter, compiled 1:1 as C++ (DenoiseCpuShim.h + AtrousDenoiseMirror). Identity switch, mean
//        preservation, converged pass-through (the presentation A/B), early-out equivalence, silhouette stopping,
//        variance propagation against the closed form, and a Monte-Carlo variance check.
//    §D  the reprojection rule, mirrored from the pinned shader lines: static identity, translation tracking,
//        disocclusion resets, background fallback, and the pre-R7a identity when the feature bit is clear.
//    §E  the A/B over three lobe-like sample streams (Lambertian, glass/BTDF heavy tail, subsurface): the filter must
//        help while unconverged, must not bias the image mean, and must be bit-identical once converged.
//
//    No GPU, no window. Compile + run: Exhibits/Workbench/Materials/CheckMaterialDenoise.sh

#include "ReSTIRIntegrator.h"
#include "FidelityClassifier.h"
#include "AtrousDenoiseMirror.h"
#include "ReprojectionMirror.h"   // the rule itself — shared with the exhibit, so §D and the sheets cannot drift apart
#include "DenoiseStreams.h"       // and the §E measurement, shared with the exhibit's chart of the same numbers

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using DenoiseStreams::EarlyOutAccepts;
using DenoiseStreams::Field;
using DenoiseStreams::MeasureStream;
using DenoiseStreams::Pcg;
using DenoiseStreams::StreamBase;
using DenoiseStreams::StreamCategory;
using DenoiseStreams::StreamMeasurement;
using DenoiseStreams::StreamName;

namespace {

int Passed = 0, Failed = 0, Serial = 0;

void Check(bool Condition, const char* Label)
{
    ++Serial;
    if (Condition) { ++Passed; std::printf("ok %d - %s\n", Serial, Label); }
    else           { ++Failed; std::printf("FAIL %d - %s\n", Serial, Label); }
    std::fflush(stdout);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        TEXT
//------------------------------------------------------------------------------------------------------------------------

bool ReadFile(const char* Path, std::string& Out)
{
    Out.clear();
    FILE* F = std::fopen(Path, "rb");
    if (!F) return false;
    char Buffer[8192];
    size_t Read = 0;
    while ((Read = std::fread(Buffer, 1, sizeof(Buffer), F)) > 0) Out.append(Buffer, Read);
    std::fclose(F);
    return true;
}

size_t CountOccurrences(const std::string& Text, const std::string& Needle)
{
    if (Needle.empty()) return 0u;
    size_t Count = 0u, At = 0u;
    while ((At = Text.find(Needle, At)) != std::string::npos) { ++Count; At += Needle.size(); }
    return Count;
}

// One audit pin: the text must contain `Needle` exactly `Wanted` times (0 = the token must be absent).
struct TextPin { const char* Path; const char* Needle; size_t Wanted; const char* Label; };

void AuditPin(const std::string& Text, const TextPin& Pin)
{
    const size_t Found = CountOccurrences(Text, Pin.Needle);
    char Label[256];
    std::snprintf(Label, sizeof(Label), "%s", Pin.Label);
    if (Found != Pin.Wanted)
    {
        std::printf("[denoise] pin %-58s found %zu, wanted %zu :: %s\n", Pin.Label, Found, Pin.Wanted, Pin.Needle);
        Check(false, Label);
        return;
    }
    Check(true, Label);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FIELD HELPERS
//------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------
//                                              REPROJECTION RULE (SHADER MIRROR)
//------------------------------------------------------------------------------------------------------------------------
// ReSTIRViewport.slang `ResolveSurface`, transcribed: back-project by the R2 motion vector, validate against the
//    history's (normal, depth) with the SAME thresholds the reservoir reuse uses (25°, 10 % relative), and treat every
//    failure as a disocclusion — the mean restarts at n = 1 instead of smearing the surface that used to be here.
//    §B pins the four lines this comes from, so a change there fails the gate.

} // namespace

int main()
{
    std::setlocale(LC_ALL, "C");
    using namespace Frontier;

    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    //  §A  the live configuration: re-enabled by default, wired to the feature bits, toggles keep their semantics.
    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {
        ReSTIRIntegratorConfiguration Defaults{};
        Check(Defaults.Denoise, "A1 denoiser defaults ON (ReSTIRIntegratorConfiguration::Denoise)");
        Check(Defaults.TemporalReprojection, "A2 motion-vector reprojection defaults ON");
        Check(Defaults.TemporalReuse && Defaults.SpatialReuse && Defaults.AliasPick && Defaults.GlobalIllumination && Defaults.AntiAliasing,
              "A3 the R6 reuse switches stay on around them");
        Check(Defaults.DenoiseLevelCount == kDenoiseLevelCount && Defaults.SpatialTapCount == 4u,
              "A4 default denoise chain = the descriptor ceiling (5 levels), 4 spatial taps");

        ReSTIRIntegrator Integrator(Defaults);
        ProjectZero::FlyThroughConfiguration Flight{};
        ProjectZero::FlyThroughSolver Camera(Flight);

        const DispatchConfiguration Dispatch = Integrator.BuildDispatch(Camera, 1280u, 720u, 0u, 4u);
        // kFeatureGiReuse (bit 8, the indirect half's pool) is ON by default since the kernel port landed, so the
        //    default mask carries it too — the check names every bit rather than comparing against "the reuse bits",
        //    which is exactly how this expectation went stale for one commit when the pool arrived.
        const uint32_t Expected = DispatchFeatureGlobalIllumination | DispatchFeatureAntiAliasing | DispatchFeatureTemporalReuse
                                | DispatchFeatureSpatialReuse | DispatchFeatureAliasPick | DispatchFeatureTemporalReprojection
                                | DispatchFeatureDenoise | DispatchFeatureGiReuse;
        Check(Dispatch.FeatureFlags == Expected, "A5 default dispatch carries exactly the R6/R7/GI feature bits (8 bits named)");
        Check((Dispatch.FeatureFlags & DispatchFeatureDenoise) != 0u, "A6 DispatchFeatureDenoise set (kernel defers the tone map)");
        Check((Dispatch.FeatureFlags & DispatchFeatureTemporalReprojection) != 0u, "A7 DispatchFeatureTemporalReprojection set");
        Check(Dispatch.DenoiseLevelCount == kDenoiseLevelCount, "A8 dispatch carries the 5-level chain");

        // The shader reads bits 6 and 7 (pinned in §B); the enum must agree with them numerically.
        Check(static_cast<uint32_t>(DispatchFeatureTemporalReprojection) == 64u &&
              static_cast<uint32_t>(DispatchFeatureDenoise) == 128u,
              "A9 engine feature bits 64/128 are the shader's kFeatureTemporalReprojection/kFeatureDenoise");

        // Every combination of the two toggles must move exactly its own bit.
        bool Combinations = true;
        for (uint32_t Mask = 0u; Mask < 4u; ++Mask)
        {
            const bool Denoise = (Mask & 1u) != 0u;
            const bool Reprojection = (Mask & 2u) != 0u;
            ReSTIRIntegrator Trial(Defaults);
            Trial.AssignDenoise(Denoise);
            Trial.AssignTemporalReprojection(Reprojection);
            const DispatchConfiguration D = Trial.BuildDispatch(Camera, 1280u, 720u, 0u, 4u);
            const bool DenoiseBit = (D.FeatureFlags & DispatchFeatureDenoise) != 0u;
            const bool ReprojectionBit = (D.FeatureFlags & DispatchFeatureTemporalReprojection) != 0u;
            if (DenoiseBit != Denoise || ReprojectionBit != Reprojection) Combinations = false;
            // The four neighbours in the mask must be untouched by either toggle.
            if ((D.FeatureFlags & (DispatchFeatureTemporalReuse | DispatchFeatureSpatialReuse | DispatchFeatureAliasPick))
                != (DispatchFeatureTemporalReuse | DispatchFeatureSpatialReuse | DispatchFeatureAliasPick)) Combinations = false;
        }
        Check(Combinations, "A10 all four denoise × reprojection combinations drive exactly their own bits");

        // Toggle semantics: the filter is a post-process (no reset); reprojection changes what is sampled (reset).
        {
            ReSTIRIntegrator Trial(Defaults);
            Trial.ResetAccumulation();
            Trial.IncrementAccumulationIndex();                     // spends the reset flag
            for (uint32_t I = 0u; I < 10u; ++I) Trial.IncrementAccumulationIndex();
            const uint32_t Before = Trial.QueryAccumulationIndex();
            Trial.AssignDenoise(false);
            const uint32_t AfterDenoise = Trial.QueryAccumulationIndex();
            Trial.IncrementAccumulationIndex();
            const uint32_t AfterIncrement = Trial.QueryAccumulationIndex();
            Check(Before == 10u && AfterDenoise == 10u && AfterIncrement == 11u,
                  "A11 AssignDenoise does not restart accumulation (post-process, so the A/B stays comparable)");

            Trial.AssignTemporalReprojection(false);
            const uint32_t AfterReprojection = Trial.QueryAccumulationIndex();
            Trial.IncrementAccumulationIndex();
            const uint32_t AfterResetSpend = Trial.QueryAccumulationIndex();
            Trial.IncrementAccumulationIndex();
            Check(AfterReprojection == 0u && AfterResetSpend == 0u && Trial.QueryAccumulationIndex() == 1u,
                  "A12 AssignTemporalReprojection restarts accumulation (sampling change), spending the reset flag");
        }

        // The tier ladder scales the chain but never disables it: a tier that silently switched the denoiser off
        //    would leave quality presets meaning something other than their labels claim.
        FidelityClassifier Classifier;
        bool LadderOk = true;
        for (uint32_t Tier = 0u; Tier < static_cast<uint32_t>(FidelityCategory::Count); ++Tier)
        {
            const FidelityCriteria Criteria = Classifier.ConstructCriteria(static_cast<FidelityCategory>(Tier));
            const bool InRange = Criteria.DenoiseLevelCount >= 1u && Criteria.DenoiseLevelCount <= kDenoiseLevelCount;
            std::printf("[denoise] tier %-9s denoise levels %u\n", FidelityLabel(static_cast<FidelityCategory>(Tier)), Criteria.DenoiseLevelCount);
            if (!InRange) LadderOk = false;
        }
        Check(LadderOk, "A13 every tier's à-trous chain sits in 1..kDenoiseLevelCount (never zero, never over the ceiling)");
    }

    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    //  §B  the shaders and the dispatcher, pinned as text (the no-drift audit for what cannot be compiled here).
    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    std::string KernelText, FilterText, DispatchText, IntegratorText;
    if (!ReadFile("Engine/Shaders/ReSTIRViewport.slang", KernelText) ||
        !ReadFile("Engine/Shaders/AtrousDenoise.slang", FilterText) ||
        !ReadFile("Engine/DeviceExchange/SwapchainExchange.cpp", DispatchText) ||
        !ReadFile("Engine/DisplayPresentation/ReSTIRIntegrator.h", IntegratorText))
    {
        std::printf("FAIL 1 - B0 the shader and dispatcher sources are readable\n");
        ++Failed;
    }
    else
    {
        Check(true, "B0 the shader and dispatcher sources are readable");
        const TextPin Pins[] =
        {
            { "ReSTIRViewport", "const uint kFeatureTemporalReprojection = 64u;", 1u, "B1 kernel bit 6 = reprojection (64)" },
            { "ReSTIRViewport", "const uint kFeatureDenoise            = 128u;", 1u, "B2 kernel bit 7 = denoise (128)" },
            { "ReSTIRViewport", "const bool reproject = depth > 0.0 && (FeatureFlags & kFeatureTemporalReprojection) != 0u;", 1u, "B3 reprojection gated by the feature bit and a surface" },
            // Three sites since the GI pool landed: the running-mean accumulator, the DIRECT reservoir and the
            //    indirect (GI) reservoir — the indirect pool reprojects by the same pixel motion the direct one does.
            { "ReSTIRViewport", "vec2 motion = texelFetch(MotionImage, ivec2(pixel), 0).rg;", 3u, "B4 motion vector read at all three reuse sites (accumulator + direct reservoir + GI reservoir)" },
            { "ReSTIRViewport", "ivec2 prevPx = ivec2(floor((cuv - motion) * extent));", 1u, "B5 back-projection rule (§D mirrors this line)" },
            { "ReSTIRViewport", "count      = count + 1.0;", 1u, "B6 running-mean sample count" },
            { "ReSTIRViewport", "vec3 mean  = history.rgb + (radiance - history.rgb) / count;", 1u, "B7 running mean" },
            { "ReSTIRViewport", "float variance = sampleVariance / count;", 1u, "B8 filter input = variance OF THE MEAN" },
            { "ReSTIRViewport", "if (count < 2.0) variance = luma * luma;", 1u, "B9 a first sample stays permissive (disocclusion)" },
            { "ReSTIRViewport", "imageStore(DenoiseImage, ivec2(pixel), vec4(mean, variance));", 1u, "B10 one denoise store site for every material" },
            { "ReSTIRViewport", "imageStore(DenoiseImage", 1u, "B11 the denoise store is not duplicated per lobe (material-agnostic)" },
            { "ReSTIRViewport", "if ((FeatureFlags & kFeatureDenoise) == 0u)", 1u, "B12 with the filter off the kernel tone-maps itself" },
            { "ReSTIRViewport", "imageStore(OutputImage, ivec2(pixel), vec4(ToneMap(mean), 1.0));", 1u, "B13 ...through the same tone map the filter uses" },

            { "AtrousDenoise", "layout(set = 0, binding = 0, rgba32f) uniform readonly  image2D SourceImage;", 1u, "B14 filter binding 0: radiance + variance" },
            { "AtrousDenoise", "layout(set = 0, binding = 3, rgba8)   uniform writeonly image2D OutputImage;", 1u, "B15 filter binding 3: the presentation image" },
            { "AtrousDenoise", "const float kEarlyOutStepFraction = 0.2;", 1u, "B16 early-out is derived from an 8-bit step" },
            { "AtrousDenoise", "const float kEarlyOutVariance     = (kEarlyOutStepFraction / 255.0)", 1u, "B17 ...as a variance, not a tuned constant" },
            { "AtrousDenoise", "float NormalWeight = pow(max(dot(CentreNormal, TapSurface.xyz), 0.0), NormalPower);", 1u, "B18 normal edge stop" },
            { "AtrousDenoise", "float DepthWeight = exp(-DepthDelta / DepthSpan);", 1u, "B19 depth edge stop" },
            { "AtrousDenoise", "float LuminanceWeight = exp(-LuminanceDelta / LuminanceDenominator);", 1u, "B20 luminance edge stop (noise-relative)" },
            { "AtrousDenoise", "VarianceOut += TapColour.a   * Weight * Weight;", 1u, "B21 variance propagates with the SQUARED weights" },
            { "AtrousDenoise", "vec4(ColourSum / WeightSum, VarianceOut / (WeightSum * WeightSum))", 1u, "B22 weighted-mean variance of the filtered value" },

            { "SwapchainExchange", "static constexpr uint32_t kDenoiseGroupSize = 8u;", 1u, "B23 dispatcher uses the filter's 8×8 workgroup" },
            { "SwapchainExchange", "Push.StepSize       = 1u << Level;", 1u, "B24 tap spacing doubles per level" },
            { "SwapchainExchange", "Push.NormalPower    = 64.0f;", 1u, "B25 the engine's σn reaches the shader" },
            { "SwapchainExchange", "Push.DepthScale     = 0.05f;", 1u, "B26 the engine's σz reaches the shader" },
            { "SwapchainExchange", "Push.LuminanceScale = 4.0f;", 1u, "B27 the engine's σl reaches the shader" },
            { "SwapchainExchange", "Push.FinalLevel     = (Level + 1u == LiveDenoiseLevels) ? 1u : 0u;", 1u, "B28 the tone map happens exactly once, at the last live level" },
            { "SwapchainExchange", "1u, kDenoiseLevelCount);", 1u, "B29 the level count is clamped into the allocated sets" },

            { "ReSTIRIntegrator.h", "bool        Denoise            = true;", 1u, "B30 the header's default is ON (as §A asserts by value)" },
            { "ReSTIRIntegrator.h", "bool        TemporalReprojection = true;", 1u, "B31 the header's reprojection default is ON" },
        };
        for (const TextPin& Pin : Pins)
        {
            const std::string& Text = std::strcmp(Pin.Path, "ReSTIRViewport") == 0 ? KernelText
                                    : std::strcmp(Pin.Path, "AtrousDenoise") == 0 ? FilterText
                                    : std::strcmp(Pin.Path, "SwapchainExchange") == 0 ? DispatchText : IntegratorText;
            AuditPin(Text, Pin);
        }

        // The filter must not know what a material is: a lobe-dependent branch inside it would mean the denoiser
        //    treats glass and plastic differently, and no proof of the lobes could ever cover its output.
        const char* LobeTokens[] = { "Material", "Selection", "Sss", "Transmission", "Bsdf", "GGX", "EON", "Coat", "Fuzz" };
        bool Agnostic = true;
        for (const char* Token : LobeTokens)
            if (CountOccurrences(FilterText, Token) != 0u) { Agnostic = false; std::printf("[denoise] lobe token in the filter: %s\n", Token); }
        Check(Agnostic, "B32 the filter carries no lobe or material vocabulary (radiance, variance, normal, depth only)");
    }

    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    //  §C  the real filter, compiled 1:1 as C++.
    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    constexpr uint32_t kExtent = 24u;

    // §C0 — the transform that made the port possible, re-derived from the original text. CheckMaterialDenoise.sh
    //    generates the two halves; this recomputes what they must contain, so a hand-edited stage fails the gate.
    {
        const char* Stage = std::getenv("DO_STAGE");
        Check(Stage != nullptr, "C0.1 the transform stage directory is provided by the gate script");
        if (Stage != nullptr && !FilterText.empty())
        {
            // Split exactly like Python's str.split("\n"): a file ending in a newline yields a final empty line, and
            //    the re-derived text below depends on that (the transform is verified byte for byte).
            std::vector<std::string> Lines;
            std::string Current;
            for (char C : FilterText)
            {
                if (C == '\n') { Lines.push_back(Current); Current.clear(); }
                else Current.push_back(C);
            }
            Lines.push_back(Current);

            uint32_t Prologue = 0u, ArrayCtor = 0u, Closers = 0u, PushBlocks = 0u;
            size_t   ArrayLine = 0u, CloseLine = 0u, PushLine = 0u;
            for (size_t I = 0u; I < Lines.size(); ++I)
            {
                if ((!Lines[I].empty() && Lines[I][0] == '#') || Lines[I].rfind("layout(local_size", 0u) == 0u) { ++Prologue; }
                if (Lines[I].find("float[5](") != std::string::npos) { ++ArrayCtor; ArrayLine = I; }
                if (Lines[I] == "};") { ++Closers; CloseLine = I; }
                if (Lines[I].find("push_constant") != std::string::npos) { ++PushBlocks; PushLine = I; }
            }
            Check(Prologue == 2u && ArrayCtor == 1u && Closers == 1u && PushBlocks == 1u && CloseLine > PushLine,
                  "C0.2 the shader still has exactly 2 prologue lines, 1 array constructor, 1 push-constant block");

            std::string Part1, Part2;
            const bool ReadBoth = ReadFile((std::string(Stage) + "/AtrousDenoise.cpu.1.h").c_str(), Part1)
                               && ReadFile((std::string(Stage) + "/AtrousDenoise.cpu.2.h").c_str(), Part2);
            Check(ReadBoth, "C0.3 the gate script staged both halves of the transformed shader");

            if (ReadBoth)
            {
                // Rebuild the expected text: drop the prologue, rewrite the array constructor, close the push struct
                //    with its instance, split there.
                const auto IsPrologue = [&](size_t Index)
                {
                    return (!Lines[Index].empty() && Lines[Index][0] == '#') || Lines[Index].rfind("layout(local_size", 0u) == 0u;
                };
                std::vector<std::string> Body;
                for (size_t I = 0u; I < Lines.size(); ++I) if (!IsPrologue(I)) Body.push_back(Lines[I]);
                size_t ShiftArray = 0u, ShiftClose = 0u;
                for (size_t I = 0u; I < ArrayLine; ++I) if (IsPrologue(I)) ++ShiftArray;
                for (size_t I = 0u; I < CloseLine;  ++I) if (IsPrologue(I)) ++ShiftClose;

                size_t ShiftPush = 0u;
                for (size_t I = 0u; I < PushLine; ++I) if (IsPrologue(I)) ++ShiftPush;

                const std::string ExpectedArray = "    const float Weights[5] = { 0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f };";
                Body[ArrayLine - ShiftArray] = ExpectedArray;
                Check(Body[PushLine - ShiftPush] == "layout(push_constant) uniform DenoiseConstants",
                      "C0.4 the push-constant opener is what the transform expects");
                Body[PushLine - ShiftPush] = "struct DenoiseConstants";
                Body[CloseLine - ShiftClose] = "} DenoiseParameters;";

                std::string Expected1, Expected2;
                for (size_t I = 0u; I <= CloseLine - ShiftClose; ++I) { Expected1 += Body[I]; Expected1 += "\n"; }
                for (size_t I = CloseLine - ShiftClose + 1u; I < Body.size(); ++I) { Expected2 += Body[I]; Expected2 += (I + 1u < Body.size() ? "\n" : ""); }

                Check(Part1 == Expected1, "C0.5 the staged first half is the shader text with the prologue dropped and the push block closed by its instance");
                Check(Part2 == Expected2, "C0.6 the staged second half is the shader text with only the array constructor rewritten");
                std::printf("[denoise] transform: 2 prologue lines dropped, 1 array constructor rewritten, 1 split at the push-constant block\n");
            }
        }
    }

    const float kNoiseVariance = 1.0e-3f;   // well above the early-out threshold (≈ 6.1e-7)

    // §C1 — the identity switch: `Enabled == 0` copies, whatever the input is.
    {
        Field Source, Surface, Target, Output;
        Source.Resize(kExtent); Surface.Resize(kExtent); Target.Resize(kExtent); Output.Resize(kExtent);
        Pcg Rng(1u);
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                float* S = Source.At(X, Y);
                S[0] = 4.0f * Rng.Uniform(); S[1] = 4.0f * Rng.Uniform(); S[2] = 4.0f * Rng.Uniform(); S[3] = kNoiseVariance;
                float* F = Surface.At(X, Y);
                F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = 1.0f + 0.01f * Rng.Uniform();
            }
        DenoiseMirror::RunConfiguration Config;
        Config.Extent = kExtent; Config.StepSize = 1u; Config.Enabled = false; Config.FinalLevel = true;
        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Target.Values.data(), Output.Values.data());
        bool Identical = true;
        for (size_t I = 0u; I < Source.Values.size(); ++I) Identical = Identical && Source.Values[I] == Target.Values[I];
        Check(Identical, "C1.1 Enabled = 0 is a bit-exact copy (the identity switch)");

        bool Mapped = true;
        for (uint32_t Y = 0u; Y < kExtent && Mapped; ++Y)
            for (uint32_t X = 0u; X < kExtent && Mapped; ++X)
            {
                float Expected[3];
                const float* S = Source.At(X, Y);
                DenoiseMirror::ToneMap(S[0], S[1], S[2], 1.0f, 1.0f, Expected);
                const float* O = Output.At(X, Y);
                Mapped = O[0] == Expected[0] && O[1] == Expected[1] && O[2] == Expected[2];
            }
        Check(Mapped, "C1.2 the disabled pass still writes the presentation image through the shader's own tone map");
    }

    // §C2 — a constant radiance field is returned unchanged (a weighted mean of equals is that value).
    {
        Field Source, Surface, Target, Output;
        Source.Resize(kExtent); Surface.Resize(kExtent); Target.Resize(kExtent); Output.Resize(kExtent);
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                float* S = Source.At(X, Y);
                S[0] = 0.7f; S[1] = 0.6f; S[2] = 0.5f; S[3] = kNoiseVariance;
                float* F = Surface.At(X, Y);
                F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = 1.0f;
            }
        DenoiseMirror::RunConfiguration Config;
        Config.Extent = kExtent; Config.StepSize = 1u; Config.Enabled = true; Config.FinalLevel = true;
        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Target.Values.data(), Output.Values.data());
        float Worst = 0.0f;
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                const float* T = Target.At(X, Y);
                Worst = std::max(Worst, std::fabs(T[0] - 0.7f));
                Worst = std::max(Worst, std::fabs(T[1] - 0.6f));
                Worst = std::max(Worst, std::fabs(T[2] - 0.5f));
            }
        Check(Worst <= 1.0e-6f, "C2 a uniform radiance field survives the 25-tap weighted mean unchanged (no energy added)");
    }

    // §C3 — THE A/B: at convergence the filtered and unfiltered presentation images are bit-identical, because a
    //    converged pixel's variance is below the early-out threshold and the filter passes it through verbatim.
    {
        Field Source, Surface, Target, Output;
        Source.Resize(kExtent); Surface.Resize(kExtent); Target.Resize(kExtent); Output.Resize(kExtent);
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                const float Base = StreamBase(X, Y, kExtent);
                float* S = Source.At(X, Y);
                S[0] = Base; S[1] = Base * 0.9f; S[2] = Base * 0.8f; S[3] = 0.0f;   // converged: zero variance
                float* F = Surface.At(X, Y);
                F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = 1.0f;
            }
        DenoiseMirror::RunConfiguration Config;
        Config.Extent = kExtent; Config.StepSize = 1u; Config.Enabled = true; Config.FinalLevel = true;
        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Target.Values.data(), Output.Values.data());

        bool PassthroughTarget = true, PassthroughOutput = true;
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                const float* S = Source.At(X, Y);
                const float* T = Target.At(X, Y);
                PassthroughTarget = PassthroughTarget && S[0] == T[0] && S[1] == T[1] && S[2] == T[2] && S[3] == T[3];
                float Expected[3];
                DenoiseMirror::ToneMap(S[0], S[1], S[2], Config.Exposure, Config.ColourSaturation, Expected);
                const float* O = Output.At(X, Y);
                PassthroughOutput = PassthroughOutput && O[0] == Expected[0] && O[1] == Expected[1] && O[2] == Expected[2];
            }
        Check(PassthroughTarget, "C3.1 a converged field passes through bit-exactly (early-out, radiance and variance)");
        Check(PassthroughOutput, "C3.2 A/B AT CONVERGENCE: denoise ON == denoise OFF, presentation images bit-identical");

        // ...and the same field with noise must NOT be identical, or C3.2 would be vacuous.
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                const float Base = StreamBase(X, Y, kExtent);
                const float Jitter = ((X * 7u + Y * 13u) % 5u) * 0.02f - 0.04f;
                float* S = Source.At(X, Y);
                S[0] = Base * (1.0f + Jitter); S[1] = Base * 0.9f * (1.0f + Jitter); S[2] = Base * 0.8f; S[3] = kNoiseVariance;
            }
        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Target.Values.data(), Output.Values.data());
        uint32_t Differing = 0u;
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                float Expected[3];
                const float* S = Source.At(X, Y);
                DenoiseMirror::ToneMap(S[0], S[1], S[2], Config.Exposure, Config.ColourSaturation, Expected);
                const float* O = Output.At(X, Y);
                if (O[0] != Expected[0]) ++Differing;
            }
        Check(Differing > kExtent * kExtent / 4u, "C3.3 A/B BEFORE CONVERGENCE: the filtered image really is filtered");
    }

    // §C4 — early-out equivalence: forcing the full tap loop on the same radiance changes nothing.
    {
        Field Source, Surface, Target, Output;
        Source.Resize(kExtent); Surface.Resize(kExtent); Target.Resize(kExtent); Output.Resize(kExtent);
        Field SourceForced = Source;
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                const float Base = 0.6f;   // constant: the tint of the test is the early-out, not the luminance term
                float* A = Source.At(X, Y);
                A[0] = Base; A[1] = Base; A[2] = Base; A[3] = 0.0f;                 // early-out path
                float* B = SourceForced.At(X, Y);
                B[0] = Base; B[1] = Base; B[2] = Base; B[3] = kNoiseVariance;       // same radiance, full tap loop
                float* F = Surface.At(X, Y);
                F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = 1.0f;
            }
        DenoiseMirror::RunConfiguration Config;
        Config.Extent = kExtent; Config.StepSize = 1u; Config.Enabled = true; Config.FinalLevel = true;
        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Target.Values.data(), Output.Values.data());
        Field TargetForced, OutputForced;
        TargetForced.Resize(kExtent); OutputForced.Resize(kExtent);
        DenoiseMirror::Run(Config, SourceForced.Values.data(), Surface.Values.data(), TargetForced.Values.data(), OutputForced.Values.data());
        float Worst = 0.0f;
        for (size_t I = 0u; I < Target.Values.size(); I += 4u)
            for (size_t C = 0u; C < 3u; ++C)
                Worst = std::max(Worst, std::fabs(Target.Values[I + C] - TargetForced.Values[I + C]));
        Check(Worst <= 1.0e-6f, "C4 the early-out and the full tap loop agree on the radiance they hand back (variance differs, radiance does not)");
    }

    // §C5 — silhouette stopping: an orthogonal normal and a 100× depth step must contribute nothing.
    {
        Field Source, Surface, Target, Output;
        Source.Resize(kExtent); Surface.Resize(kExtent); Target.Resize(kExtent); Output.Resize(kExtent);
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                const bool RightHalf = X >= kExtent / 2u;
                float* S = Source.At(X, Y);
                S[0] = S[1] = S[2] = RightHalf ? 4.0f : 1.0f;   // a 4× luminance step across the seam
                S[3] = kNoiseVariance;
                float* F = Surface.At(X, Y);
                F[0] = RightHalf ? 1.0f : 0.0f;                 // ...and an orthogonal normal, so the seam is a silhouette
                F[1] = 0.0f; F[2] = 0.0f; F[3] = 1.0f;
            }
        DenoiseMirror::RunConfiguration Config;
        Config.Extent = kExtent; Config.StepSize = 1u; Config.Enabled = true; Config.FinalLevel = false;
        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Target.Values.data(), nullptr);

        float WorstLeak = 0.0f;
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                const float Expected = X >= kExtent / 2u ? 4.0f : 1.0f;
                WorstLeak = std::max(WorstLeak, std::fabs(Target.At(X, Y)[0] - Expected));
            }
        Check(WorstLeak <= 1.0e-5f, "C5.1 no cross-silhouette leak: 4× radiance step stays at 1.0 / 4.0 (normals orthogonal)");

        // A depth-only silhouette: same radiance, normals aligned, 100× depth step ⇒ the depth weight must kill it.
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                float* S = Source.At(X, Y);
                S[0] = S[1] = S[2] = (X >= kExtent / 2u) ? 4.0f : 1.0f;
                float* F = Surface.At(X, Y);
                F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = (X >= kExtent / 2u) ? 100.0f : 1.0f;
            }
        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Target.Values.data(), nullptr);
        float WorstDepthLeak = 0.0f;
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                const float Expected = X >= kExtent / 2u ? 4.0f : 1.0f;
                WorstDepthLeak = std::max(WorstDepthLeak, std::fabs(Target.At(X, Y)[0] - Expected));
            }
        Check(WorstDepthLeak <= 1.0e-4f, "C5.2 ...nor across a 100× depth step (σz = 0.05 relative)");

        // In-region smoothing on the same field: noise on a uniform patch must come down.
        Field Noisy, PatchSurface, PatchTarget;
        Noisy.Resize(kExtent); PatchSurface.Resize(kExtent); PatchTarget.Resize(kExtent);
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                // One flat surface carrying a fixed +/−3 % noise pattern: the signal is the constant, the pattern is noise.
                const float Noise = (((X + Y) & 1u) != 0u ? 1.0f : -1.0f) * 0.03f;
                float* S = Noisy.At(X, Y);
                S[0] = S[1] = S[2] = 1.0f + Noise; S[3] = kNoiseVariance;
                float* F = PatchSurface.At(X, Y);
                F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = 1.0f;
            }
        DenoiseMirror::RunConfiguration PatchConfig = Config;
        PatchConfig.LuminanceScale = 1.0f;   // default-adjacent: the luminance term is live, the geometry weights dominate the pattern
        DenoiseMirror::Run(PatchConfig, Noisy.Values.data(), PatchSurface.Values.data(), PatchTarget.Values.data(), nullptr);
        float InputDeviation = 0.0f, OutputDeviation = 0.0f;
        for (uint32_t Y = 2u; Y < kExtent - 2u; ++Y)
            for (uint32_t X = 2u; X < kExtent - 2u; ++X)
            {
                InputDeviation  += std::fabs(Noisy.At(X, Y)[0] - 1.0f);
                OutputDeviation += std::fabs(PatchTarget.At(X, Y)[0] - 1.0f);
            }
        std::printf("[denoise] in-region deviation %.5f -> %.5f (%.1f%% removed)\n",
                    InputDeviation, OutputDeviation, 100.0f * (1.0f - OutputDeviation / std::max(InputDeviation, 1.0e-9f)));
        Check(OutputDeviation < 0.75f * InputDeviation, "C5.3 in-region: the filter removes a quarter or more of the noise off its own signal");
    }

    // §C6 — variance propagation against the closed form. With a constant radiance (all luminance weights = 1) and a
    //    constant variance, the weights are the 5×5 B₃ outer product, so the filtered variance must equal
    //    σ² · Σw² / (Σw)² — and the shader's `VarianceOut += a·w²` is exactly what produces it.
    {
        // The weights are read from the SHADER's own KernelWeight (compiled above), not from a table retyped here:
        //    the B₃ set is then pinned by value, and the closed form is derived from the code under test.
        const double Weights[5] = { DenoiseMirror::KernelWeight(-2), DenoiseMirror::KernelWeight(-1), DenoiseMirror::KernelWeight(0),
                                    DenoiseMirror::KernelWeight(1), DenoiseMirror::KernelWeight(2) };
        const bool B3 = Weights[0] == 0.0625 && Weights[1] == 0.25 && Weights[2] == 0.375 && Weights[3] == 0.25 && Weights[4] == 0.0625;
        Check(B3, "C6.0 the shader's KernelWeight is the B₃ spline (1/16, 1/4, 3/8, 1/4, 1/16)");
        double SumW = 0.0, SumW2 = 0.0;
        for (int Y = 0; Y < 5; ++Y)
            for (int X = 0; X < 5; ++X)
            {
                const double W = Weights[X] * Weights[Y];
                SumW  += W;
                SumW2 += W * W;
            }
        Check(std::fabs(SumW - 1.0) < 1.0e-12, "C6.1 the B₃ spline kernel is normalised (Σw = 1)");

        Field Source, Surface, Target;
        Source.Resize(kExtent); Surface.Resize(kExtent); Target.Resize(kExtent);
        for (uint32_t Y = 0u; Y < kExtent; ++Y)
            for (uint32_t X = 0u; X < kExtent; ++X)
            {
                float* S = Source.At(X, Y);
                S[0] = S[1] = S[2] = 0.5f; S[3] = kNoiseVariance;
                float* F = Surface.At(X, Y);
                F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = 1.0f;
            }
        DenoiseMirror::RunConfiguration Config;
        Config.Extent = kExtent; Config.StepSize = 1u; Config.Enabled = true; Config.FinalLevel = false;
        DenoiseMirror::Run(Config, Source.Values.data(), Surface.Values.data(), Target.Values.data(), nullptr);

        const double Expected = static_cast<double>(kNoiseVariance) * SumW2 / (SumW * SumW);
        double Worst = 0.0;
        for (uint32_t Y = 2u; Y < kExtent - 2u; ++Y)
            for (uint32_t X = 2u; X < kExtent - 2u; ++X)
                Worst = std::max(Worst, std::fabs(Target.At(X, Y)[3] - Expected) / Expected);
        char Label[192];
        std::snprintf(Label, sizeof(Label), "C6.2 filtered variance == σ²·Σw²/(Σw)² = %.6g (worst relative error %.2e)", Expected, Worst);
        Check(Worst < 1.0e-4, Label);

        // A Monte-Carlo cross-check: the empirical spread of the filtered value over many independent trials must
        //    match the predicted variance, and be far below the unfiltered spread.
        // The closed form assumes weights that do not depend on the data. The shader's luminance weight DOES depend on it
        //    (that is its whole point: a difference large relative to the noise is an edge), so the propagation check
        //    makes it inert and the data-dependent case is left to §C5.3 and the §E A/B.
        DenoiseMirror::RunConfiguration PropagationConfig = Config;
        PropagationConfig.LuminanceScale = 1.0e6f;
        constexpr uint32_t kTrials = 512u;
        std::vector<float> Filtered(kTrials, 0.0f), Unfiltered(kTrials, 0.0f);
        for (uint32_t Trial = 0u; Trial < kTrials; ++Trial)
        {
            Pcg Rng(1000u + Trial * 7919u);
            Field Sample, TrialSurface, TrialTarget;
            Sample.Resize(kExtent); TrialSurface.Resize(kExtent); TrialTarget.Resize(kExtent);
            for (uint32_t Y = 0u; Y < kExtent; ++Y)
                for (uint32_t X = 0u; X < kExtent; ++X)
                {
                    float* S = Sample.At(X, Y);
                    const float Value = 0.5f + std::sqrt(kNoiseVariance) * (2.0f * Rng.Uniform() - 1.0f) * 1.7320508f;
                    S[0] = S[1] = S[2] = Value; S[3] = kNoiseVariance;
                    float* F = TrialSurface.At(X, Y);
                    F[0] = 0.0f; F[1] = 0.0f; F[2] = 1.0f; F[3] = 1.0f;
                }
            DenoiseMirror::Run(PropagationConfig, Sample.Values.data(), TrialSurface.Values.data(), TrialTarget.Values.data(), nullptr);
            const uint32_t CX = kExtent / 2u, CY = kExtent / 2u;
            Filtered[Trial]   = TrialTarget.At(CX, CY)[0];
            Unfiltered[Trial] = Sample.At(CX, CY)[0];
        }
        const auto VarianceOf = [](const std::vector<float>& Samples)
        {
            double Mean = 0.0;
            for (float V : Samples) Mean += V;
            Mean /= static_cast<double>(Samples.size());
            double Sum = 0.0;
            for (float V : Samples) Sum += (V - Mean) * (V - Mean);
            return Sum / static_cast<double>(Samples.size() - 1u);
        };
        const double FilteredVariance   = VarianceOf(Filtered);
        const double UnfilteredVariance = VarianceOf(Unfiltered);
        const double Ratio = FilteredVariance / UnfilteredVariance;
        const double Theory = SumW2 / (SumW * SumW);
        std::printf("[denoise] variance: unfiltered %.6g, filtered %.6g (ratio %.4f, theory %.4f)\n",
                    UnfilteredVariance, FilteredVariance, Ratio, Theory);
        Check(std::fabs(Ratio - Theory) < 0.20 * Theory, "C6.3 Monte-Carlo: filtered/unfiltered spread = Σw²/(Σw)² with data-independent weights (±20 %, 512 trials)");
        Check(std::fabs(UnfilteredVariance - kNoiseVariance) < 0.20 * kNoiseVariance, "C6.4 Monte-Carlo: the unfiltered spread matches the input variance (±20 %)");
    }

    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    //  §D  the reprojection rule (§B5's line, mirrored).
    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {
        constexpr uint32_t W = 16u, H = 16u;
        std::vector<HistoryTexel> History(W * H);
        for (uint32_t Y = 0u; Y < H; ++Y)
            for (uint32_t X = 0u; X < W; ++X)
            {
                History[static_cast<size_t>(Y) * W + X].Normal[0] = 0.0f;
                History[static_cast<size_t>(Y) * W + X].Normal[1] = 0.0f;
                History[static_cast<size_t>(Y) * W + X].Normal[2] = 1.0f;
                History[static_cast<size_t>(Y) * W + X].Depth = 1.0f;
                History[static_cast<size_t>(Y) * W + X].Count = 64.0f;
            }
        const float Front[3] = { 0.0f, 0.0f, 1.0f };

        const ReprojectionAnswer Still = Reproject(5.0f, 7.0f, W, H, Front, 1.0f, 0.0f, 0.0f, History.data(), true);
        Check(Still.Inherited && Still.PreviousX == 5 && Still.PreviousY == 7,
              "D1 a static frame reprojects onto its own pixel (motion = 0 is the identity)");

        const ReprojectionAnswer Moved = Reproject(5.0f, 7.0f, W, H, Front, 1.0f, 3.0f / float(W), -2.0f / float(H), History.data(), true);
        Check(Moved.Inherited && Moved.PreviousX == 2 && Moved.PreviousY == 9,
              "D2 a surface that moved (+3, −2) px between frames reads the pixel it came from");

        const ReprojectionAnswer Offscreen = Reproject(1.0f, 1.0f, W, H, Front, 1.0f, 8.0f / float(W), 0.0f, History.data(), true);
        Check(!Offscreen.Inherited && Offscreen.Offscreen, "D3 a back-projection off screen is a disocclusion, not a clamp");

        // A translation must be TRACKED: the reprojected history equals the sample, the same-pixel history does not.
        {
            // The surface carries a value that travels with it (a texture riding the motion, exactly what motion vectors
            //    describe). Last frame it stood Shift px to the left, so the value now at X was shown at X − Shift — and
            //    the history image, indexed by LAST frame's pixels, holds it there.
            const uint32_t Shift = 4u;
            double ReprojectedError = 0.0, SamePixelError = 0.0;
            for (uint32_t X = Shift; X < W; ++X)
            {
                const float Sample = static_cast<float>(X) * 0.1f;                    // the surface's value at this pixel
                const ReprojectionAnswer Answer = Reproject(static_cast<float>(X), 0.0f, W, H, Front, 1.0f,
                                                            static_cast<float>(Shift) / float(W), 0.0f, History.data(), true);
                const float ReprojectedHistory = static_cast<float>(Answer.PreviousX + Shift) * 0.1f;
                const float SamePixelHistory   = static_cast<float>(X + Shift) * 0.1f;
                ReprojectedError += std::fabs(ReprojectedHistory - Sample);
                SamePixelError   += std::fabs(SamePixelHistory - Sample);
            }
            std::printf("[denoise] translation: reprojected history error %.6f, same-pixel %.6f\n", ReprojectedError, SamePixelError);
            Check(ReprojectedError < 1.0e-6 && SamePixelError > 1.0,
                  "D4 reprojected history tracks a translation exactly; same-pixel history smears by one step per frame");
        }

        // Validation: a 90° normal and a 2× depth are both disocclusions, and the count restarts (no smear).
        {
            const float Sideways[3] = { 1.0f, 0.0f, 0.0f };
            const ReprojectionAnswer Rotated = Reproject(5.0f, 7.0f, W, H, Sideways, 1.0f, 0.0f, 0.0f, History.data(), true);
            Check(!Rotated.Inherited && !Rotated.Offscreen, "D5 a 90° normal disagreement is rejected (25° rule)");
            std::vector<HistoryTexel> Deep = History;
            Deep[7 * W + 5].Depth = 2.0f;
            const ReprojectionAnswer Thick = Reproject(5.0f, 7.0f, W, H, Front, 1.0f, 0.0f, 0.0f, Deep.data(), true);
            Check(!Thick.Inherited, "D6 a 2× depth disagreement is rejected (10 % rule)");
            std::vector<HistoryTexel> Empty = History;
            Empty[7 * W + 5].Depth = -1.0f;
            const ReprojectionAnswer NoSurface = Reproject(5.0f, 7.0f, W, H, Front, 1.0f, 0.0f, 0.0f, Empty.data(), true);
            Check(!NoSurface.Inherited, "D7 history that held no surface inherits nothing");
            const ReprojectionAnswer Background = Reproject(5.0f, 7.0f, W, H, Front, -1.0f, 0.0f, 0.0f, History.data(), true);
            Check(!Background.Inherited && Background.PreviousX == 5 && Background.PreviousY == 7,
                  "D8 a background pixel (depth ≤ 0) reprojects onto itself — never a source, never a consumer");
            const ReprojectionAnswer Disabled = Reproject(5.0f, 7.0f, W, H, Front, 1.0f, 3.0f / float(W), 0.0f, History.data(), false);
            Check(!Disabled.Inherited && Disabled.PreviousX == 5 && Disabled.PreviousY == 7,
                  "D9 with the feature bit clear the rule is the pre-R7a same-pixel read (byte-identical accumulator)");
        }
    }

    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    //  §E  A/B over three lobe-like streams: helpful before convergence, mean-preserving at it, and bit-identical on
    //      every pixel the shipped early-out judges converged.
    //──────────────────────────────────────────────────────────────────────────────────────────────────────────────
    {
        constexpr uint32_t kFrames = 512u;                    // the reporting mark: "after 512 accumulated samples"
        const uint32_t kHolds[3] = { 512u, 2048u, 8192u };    // the fade-out curve's sampling points

        for (uint32_t Category = 0u; Category < static_cast<uint32_t>(StreamCategory::Count); ++Category)
        {
            const StreamCategory Stream = static_cast<StreamCategory>(Category);
            const char* Name = StreamName(Stream);
            const StreamMeasurement Measured = MeasureStream(Stream, kExtent, kHolds[2], kHolds);

            for (uint32_t M = 0u; M < 3u; ++M)
                std::printf("[denoise] %-12s hold %5u: MSE raw %.4g vs filtered %.4g, early-out accepts %5.1f%%, of those %u differ\n",
                            Name, kHolds[M], Measured.MilestoneMseRaw[M], Measured.MilestoneMseFiltered[M],
                            100.0 * Measured.MilestoneAcceptance[M], Measured.MilestoneBad[M]);
            std::printf("[denoise] %-12s frame %3u: MSE filtered %.6g vs unfiltered %.6g, linear image-mean drift %.4f%%\n",
                        Name, kFrames, Measured.MilestoneMseFiltered[0], Measured.MilestoneMseRaw[0], Measured.FinalFrameDrift * 100.0);

            char Label[224];
            std::snprintf(Label, sizeof(Label), "E1 %s: at 1 sample the filter lowers presentation MSE (%.4g < %.4g)",
                          Name, Measured.FirstFrameFilteredError, Measured.FirstFrameUnfilteredError);
            Check(Measured.FirstFrameFilteredError < Measured.FirstFrameUnfilteredError, Label);
            // E2 is stated about LINEAR radiance and at convergence: a display-space filter redistributes the first
            //    frame's under-sampled energy on purpose (that is what denoising IS — E2b bounds it), and the tone map
            //    is a display transform, not a mean-preserving one. Where the estimate has converged, the filter must
            //    hand the image back: that is the mean-preservation claim that belongs to the denoiser.
            std::snprintf(Label, sizeof(Label), "E2 %s: at convergence the filter preserves the linear image mean (%.4f %% drift)",
                          Name, Measured.FinalFrameDrift * 100.0);
            Check(Measured.FinalFrameDrift < 0.02, Label);
            std::snprintf(Label, sizeof(Label), "E2b %s: the frame-one redistribution is bounded (%.3f %% drift)", Name, Measured.FirstFrameDrift * 100.0);
            Check(Measured.FirstFrameDrift < 0.50, Label);
            std::snprintf(Label, sizeof(Label), "E3 %s: the running mean converges to the analytic truth (%.3f %% after %u frames)",
                          Name, 100.0 * Measured.FinalLinearError / std::max(Measured.FinalTruth, 1.0e-9), kFrames);
            Check(Measured.FinalLinearError / std::max(Measured.FinalTruth, 1.0e-9) < 0.02, Label);
            // E4. Every pixel the shipped convergence test accepts, at EVERY hold, comes back out untouched: that is
            //    the A. It is not a tautology — the check is made from outside the filter, on the presentation image,
            //    against the tone map the kernel would have written itself.
            std::snprintf(Label, sizeof(Label), "E4 %s: every pixel the early-out accepts is bit-identical (%u/%u/%u accepted at the three holds, %u differ)",
                          Name, Measured.MilestoneAccepted[0], Measured.MilestoneAccepted[1], Measured.MilestoneAccepted[2],
                          Measured.MilestoneBad[0] + Measured.MilestoneBad[1] + Measured.MilestoneBad[2]);
            Check(Measured.MilestoneBad[0] + Measured.MilestoneBad[1] + Measured.MilestoneBad[2] == 0u && Measured.MilestoneAccepted[2] > 0u, Label);
            // E4b is the "helps before convergence, then fades" claim as a single measurable: at one sample the test
            //    never fires (nothing is converged); by the longest hold it accepts the majority of the frame. The
            //    curve between the two is the self-gating the shader documents, printed hold by hold above.
            std::snprintf(Label, sizeof(Label), "E4b %s: the early-out engages as the estimate converges (%.1f %% at frame 1, %.1f %% at hold %u)",
                          Name, 100.0 * Measured.AcceptedAtFirstFrame / (kExtent * kExtent), 100.0 * Measured.MilestoneAcceptance[2], kHolds[2]);
            Check(Measured.AcceptedAtFirstFrame == 0u && Measured.MilestoneAcceptance[2] > 0.5, Label);
            // E4c. The curve must not go backwards: as the estimate settles, the shipped test can only accept MORE
            //    pixels, which is what "the filter fades out as the frame converges" means when it is measured.
            std::snprintf(Label, sizeof(Label), "E4c %s: acceptance grows with the hold, it never falls back (%.1f %% -> %.1f %% -> %.1f %%)",
                          Name, 100.0 * Measured.MilestoneAcceptance[0], 100.0 * Measured.MilestoneAcceptance[1], 100.0 * Measured.MilestoneAcceptance[2]);
            Check(Measured.MilestoneAcceptance[1] + 0.02 >= Measured.MilestoneAcceptance[0] &&
                  Measured.MilestoneAcceptance[2] + 0.02 >= Measured.MilestoneAcceptance[1], Label);
        }
    }

    if (Failed == 0) std::printf("MATERIAL DENOISE: PASS (%d/%d)\n", Passed, Passed + Failed);
    else             std::printf("MATERIAL DENOISE: FAIL (%d passed, %d failed)\n", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
