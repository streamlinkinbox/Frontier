/*==========================================================================================================================================
                                      REPLAYFACETMIXTURECONVERGENCEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Investigate a short-run outlier with colour classification and a disjoint frame-seed sequence.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                           FINITE-SEQUENCE DIAGNOSTICS
//------------------------------------------------------------------------------------------------------------------------

// 📝 This copies the shader's generator only to diagnose the observed source choices, not as an independent oracle.
uint32_t ReplayAuditRandom(uint32_t& Bits)
{
    Bits = Bits * 747796405u + 2891336453u;
    const uint32_t Word = ((Bits >> ((Bits >> 28u) + 4u)) ^ Bits) * 277803737u;
    return (Word >> 22u) ^ Word;
}

double ReplayMixtureBrightProbability(const LightComputeArchive& Scene)
{
    const auto Distribution = CompileRadiantFacetDistribution(Scene.Surfaces);
    Require(Distribution[0] == 2, "Convergence fixture needs two emitters");
    for (uint32_t Index = 0; Index < 2; ++Index)
    {
        const uint32_t Facet = uint32_t(Distribution[1 + 4 * Index]);
        if (Scene.Surfaces[Facet].Emissive[0] > 1)
            return .5 * Distribution[2 + 4 * Index] + .25;
    }
    throw std::runtime_error("Bright convergence emitter is missing");
}

void ValidateReplayFacetMixtureConvergence(const char* Driver, const std::string& Shaders)
{
    const auto Open = ReplayPowerFacetFixture(2), Blocked = ReplayPowerFacetFixture(0);
    const double OpenExact = ReplayPowerAreaIntegral(2, false, 1024);
    const double BlockedExact = ReplayPowerAreaIntegral(0, false, 1024);
    const auto Sources = CompilePathReplayFacetSources(Open.Surfaces);
    Require(Sources.size() == 2, "Convergence fixture has an unexpected emitter count");
    const uint32_t First = uint32_t(Sources[0]), Second = uint32_t(Sources[1]);
    Require((Open.Surfaces[First].Emissive[0] > 1) != (Open.Surfaces[Second].Emissive[0] > 1),
            "Convergence fixture does not have one bright emitter");
    const uint32_t BrightIndex = Open.Surfaces[First].Emissive[0] > 1 ? 0u : 1u;
    LightComputeSubmission Compute(Driver);
    std::printf("MIXTURE_REPEAT preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);

    // 📝 Reproduce the original 24-batch uniform sequence. Values > .01 are the bright source;
    //    every dim-source contribution is below .01 in this fixed two-emitter setup.
    constexpr uint32_t Centres = 1024, OriginalBatches = 24;
    ReplayGroupedMoments Original;
    DirectGpuMoments OriginalFrequency;
    uint32_t OriginalBright = 0, GeneratorMismatches = 0;
    for (uint32_t Batch = 0; Batch < OriginalBatches; ++Batch)
    {
        const uint32_t Seed = 840001 + 103 * Batch;
        const auto Image = TraceReplayPower(Compute, Open, 1, Seed, false, false, true, false);
        Original.Accumulate(Image, Open.Width);
        uint32_t BatchBright = 0;
        for (uint32_t Y = 0; Y < 32; ++Y)
            for (uint32_t X = 0; X < 32; ++X)
            {
                const uint32_t Pixel = (3 * Y + 1) * Open.Width + 3 * X + 1;
                const float Value = Image[Pixel][0];
                Require(std::isfinite(Value) && Value > 0, "Original visible-source sample was invalid");
                const bool ActualBright = Value > .01f;
                uint32_t SeedBits = Seed ^ (Pixel * 747796405u + 2891336453u);
                uint32_t PathBits = ReplayAuditRandom(SeedBits);
                const bool ExpectedBright = ReplayAuditRandom(PathBits) % 2u == BrightIndex;
                GeneratorMismatches += ActualBright != ExpectedBright ? 1u : 0u;
                BatchBright += ActualBright ? 1u : 0u;
            }
        OriginalBright += BatchBright;
        OriginalFrequency.Accumulate(double(BatchBright) / Centres);
    }
    const double OriginalSe = ReplaySampleStandardError(Original.Batches[0]);
    const double FrequencySe = ReplaySampleStandardError(OriginalFrequency);
    std::printf("MIXTURE_REPEAT original_uniform exact=%.9g mean=%.9g batch_se=%.9g bright=%u/%u "
                "frequency=%.9g frequency_batch_se=%.9g generator_mismatches=%u\n", OpenExact,
                Original.Pixels[0].Mean(), OriginalSe, OriginalBright, OriginalBatches * Centres,
                OriginalFrequency.Mean(), FrequencySe, GeneratorMismatches);
    Require(GeneratorMismatches == 0 && OriginalBright > 0 && OriginalBright < OriginalBatches * Centres &&
                std::abs(Original.Pixels[0].Mean() - OpenExact) < 6 * OriginalSe + .003 * OpenExact,
            "Original short-run source-choice diagnostic did not reproduce the area fixture");

    // 📝 A fixed MT sequence supplies different frame seeds; reuse within a batch makes the four comparisons paired,
    //    but does not establish independence of GPU pixels, frame streams or adaptive reservoirs.
    constexpr uint32_t Batches = 128;
    std::mt19937 FrameRandom(0x715b2e31u);
    std::vector<uint32_t> Seeds;
    ReplayGroupedMoments OpenUniform, OpenMixture, BlockedUniform, BlockedMixture;
    DirectGpuMoments UniformBright, MixtureBright, UniformVisible, MixtureVisible;
    const double MixtureBrightProbability = ReplayMixtureBrightProbability(Open);
    const double MixtureVisibleProbability = 1 - ReplayMixtureBrightProbability(Blocked);
    auto CaptureFrequency = [&](DirectGpuMoments& M, const std::vector<std::array<float, 4>>& Image, bool IsOpen)
    {
        uint32_t Count = 0;
        for (uint32_t Y = 0; Y < 32; ++Y)
            for (uint32_t X = 0; X < 32; ++X)
            {
                const float Value = Image[size_t(3 * Y + 1) * Open.Width + 3 * X + 1][0];
                Require(std::isfinite(Value) && Value >= 0 && (!IsOpen || Value > 0),
                        "Convergence source classification encountered an invalid sample");
                Count += IsOpen ? (Value > .01f ? 1u : 0u) : (Value > 0 ? 1u : 0u);
            }
        M.Accumulate(double(Count) / Centres);
    };
    auto Report = [](const char* Label, const ReplayGroupedMoments& M, double Exact)
    {
        std::printf("MIXTURE_REPEAT batches=%u case=%s exact=%.9g mean=%.9g batch_se=%.9g sample_sd=%.9g\n",
                    M.Batches[0].Count, Label, Exact, M.Pixels[0].Mean(), ReplaySampleStandardError(M.Batches[0]),
                    M.Pixels[0].Deviation());
    };
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = FrameRandom();
        Require(std::find(Seeds.begin(), Seeds.end(), Seed) == Seeds.end(), "Repeated convergence frame seed");
        for (uint32_t I = 0; I < OriginalBatches; ++I)
            Require(Seed != 840001 + 103 * I, "Follow-up frame seed overlaps the original sequence");
        Seeds.push_back(Seed);
        const auto UOpen = TraceReplayPower(Compute, Open, 1, Seed, false, false, true, false);
        const auto MOpen = TraceReplayPower(Compute, Open, 1, Seed, false, false, true, true,
                                             false, false, false, false, {0, 0, 0}, true);
        const auto UBlocked = TraceReplayPower(Compute, Blocked, 1, Seed, false, false, true, false);
        const auto MBlocked = TraceReplayPower(Compute, Blocked, 1, Seed, false, false, true, true,
                                                false, false, false, false, {0, 0, 0}, true);
        OpenUniform.Accumulate(UOpen, Open.Width); OpenMixture.Accumulate(MOpen, Open.Width);
        BlockedUniform.Accumulate(UBlocked, Open.Width); BlockedMixture.Accumulate(MBlocked, Open.Width);
        CaptureFrequency(UniformBright, UOpen, true); CaptureFrequency(MixtureBright, MOpen, true);
        CaptureFrequency(UniformVisible, UBlocked, false); CaptureFrequency(MixtureVisible, MBlocked, false);
        if (Batch == 31 || Batch == 63 || Batch == 127)
        {
            Report("open_uniform", OpenUniform, OpenExact); Report("open_mixture", OpenMixture, OpenExact);
            Report("blocked_uniform", BlockedUniform, BlockedExact); Report("blocked_mixture", BlockedMixture, BlockedExact);
            std::fflush(stdout);
        }
    }
    auto Check = [](const ReplayGroupedMoments& M, double Exact)
    {
        const double Se = std::max(ReplaySampleStandardError(M.Batches[0]), ReplaySampleStandardError(M.Pixels[0]));
        Require(std::abs(M.Pixels[0].Mean() - Exact) < 6 * Se + .003 * Exact + 1e-6,
                "Disjoint convergence repeat disagrees with the area integral");
    };
    Check(OpenUniform, OpenExact); Check(OpenMixture, OpenExact);
    Check(BlockedUniform, BlockedExact); Check(BlockedMixture, BlockedExact);
    auto CheckFrequency = [](const char* Label, const DirectGpuMoments& M, double Expected)
    {
        const double BatchSe = ReplaySampleStandardError(M);
        const double NominalSe = std::sqrt(Expected * (1 - Expected) / (Batches * Centres));
        std::printf("MIXTURE_REPEAT_FREQUENCY case=%s expected=%.9g observed=%.9g batch_se=%.9g\n",
                    Label, Expected, M.Mean(), BatchSe);
        Require(std::abs(M.Mean() - Expected) < 6 * std::max(BatchSe, NominalSe) + .002,
                "Disjoint convergence source frequency disagrees with the proposal");
    };
    CheckFrequency("open_uniform_bright", UniformBright, .5);
    CheckFrequency("open_mixture_bright", MixtureBright, MixtureBrightProbability);
    CheckFrequency("blocked_uniform_visible", UniformVisible, .5);
    CheckFrequency("blocked_mixture_visible", MixtureVisible, MixtureVisibleProbability);
    std::printf("MIXTURE_REPEAT unique_frame_seeds=%zu original_seed_overlap=0 centres_per_batch=%u\n", Seeds.size(), Centres);
    std::printf("PASS original outlier diagnosis and disjoint uniform/mixture convergence repeat\n");
}
} // namespace
