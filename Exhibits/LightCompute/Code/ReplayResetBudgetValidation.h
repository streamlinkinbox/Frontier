/*==========================================================================================================================================
                                                 REPLAYRESETBUDGETVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify Fresh8 reset output, persistent seed-measure invariance and unchanged non-reset temporal replay.

#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                RESET AND CONTINUATION CONTRACT
//------------------------------------------------------------------------------------------------------------------------

uint64_t ValidateResetBudgetCalls(const LightComputeSubmission& Compute)
{
    if (!Compute.ReplayLastSampleHistoryReset) return ValidateBoundedPathCount(Compute, false);
    uint64_t Expected = 0;
    for (uint32_t Pixel = 0; Pixel < Compute.ReceiverCount; ++Pixel)
    {
        const auto Source = ReplaySampleSlot(Compute, 2, Pixel);
        if (Source[3] == 0) continue;
        Require(Source[2] == 1 && Source[3] == 1, "Reset source is not a unit-weight uniform seed");
        Expected += 8;
    }
    const auto Counts = Compute.ExtendedStatistics();
    Require(Counts[16] == Expected && Counts[20] == 0 && Counts[22] == 0 && Counts[23] == 0,
            "Fresh reset did not execute eight paths without spatial/temporal/bank reuse");
    return Expected;
}

void ValidateResetBudgetPersistence(const LightComputeSubmission& Fixed, const LightComputeSubmission& Previous)
{
    Require(Fixed.ReplayLastSampleHistoryReset == Previous.ReplayLastSampleHistoryReset,
            "Reset correction changed host invalidation decisions");
    for (uint32_t Pixel = 0; Pixel < Fixed.ReceiverCount; ++Pixel)
        for (uint32_t Slot : {0u, 1u, 2u, 3u, 4u, 11u, 12u, 13u, 14u})
        {
            if (Fixed.ReplayLastSampleHistoryReset && Slot == 3) continue;
            const auto A = ReplaySampleSlot(Fixed, Slot, Pixel);
            const auto B = ReplaySampleSlot(Previous, Slot, Pixel);
            Require(std::memcmp(A.data(), B.data(), sizeof(A)) == 0,
                    "Reset correction changed persisted seeds, normalization or continuation slabs");
        }
}

void ValidateReplayResetBudget(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, WarmSteps = 8, CpuSamples = 4096;
    const auto PreviousScene = ReplayCameraPlane(0, 0);
    const auto CurrentScene = ReplayCameraPlane(.0625f, 0, .25f);
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    std::filesystem::create_directories(Directory);
    LightComputeSubmission Fixed(Driver), Previous(Driver), Fresh(Driver);
    Fixed.Prepare(PreviousScene, Shaders, false, false, false, false, true, false, true);
    Previous.Prepare(PreviousScene, Shaders + "/previous", false, false, false, false, true, false, true);
    Fresh.Prepare(PreviousScene, Shaders + "/previous", false, false, false, false, true, false, true);
    ReplayFreshBudgetPipeline FreshAccess(Fresh, Shaders + "/previous");
    std::mt19937_64 Seeds(0x416ce537ull);
    std::vector<uint32_t> Pixels;
    std::vector<double> Reference;
    std::array<DirectGpuMoments, 2> Means;
    std::array<double, 2> Squared{};
    DirectGpuMoments PairedMse;
    double ReferenceMean = 0, ReferenceVariance = 0;
    uint64_t FixedCalls = 0, PreviousCalls = 0, FixedWarmCalls = 0, PreviousWarmCalls = 0;
    uint64_t FixedQueries = 0, PreviousQueries = 0;
    uint32_t Continuations = 0, ResetMatches = 0;
    std::printf("RESET_DEVICE name=%s type=%u output_bytes=%llu\n", Fixed.Properties.deviceName,
                unsigned(Fixed.Properties.deviceType),
                static_cast<unsigned long long>(Fixed.ReplayOutputDescriptorBytes));
    std::fflush(stdout);
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        for (uint32_t Step = 0; Step < WarmSteps; ++Step)
        {
            const uint32_t Seed = uint32_t(Seeds());
            const auto A = ExecuteReplaySeedReservoir(Fixed, PreviousScene, Seed, Step == 0,
                                                       true, 4, true, .01f, Environment);
            const auto B = ExecuteReplaySeedReservoir(Previous, PreviousScene, Seed, Step == 0,
                                                       true, 4, true, .01f, Environment);
            FixedWarmCalls += ValidateResetBudgetCalls(Fixed);
            PreviousWarmCalls += ValidateBoundedPathCount(Previous, false);
            ValidateResetBudgetPersistence(Fixed, Previous);
            if (Step == 0)
            {
                const auto Control = ExecuteFreshBudget(FreshAccess, PreviousScene, Seed, 4, Environment, true);
                Require(ReplayCandidateImagesEqual(A, Control), "Initial reset differs from Fresh8");
                ++ResetMatches;
            }
            else
            {
                Require(ReplayCandidateImagesEqual(A, B), "Non-reset continuation output changed");
                ++Continuations;
            }
        }
        const uint32_t Seed = uint32_t(Seeds());
        const auto A = ExecuteReplaySeedReservoir(Fixed, CurrentScene, Seed, false, true, 4, true, .01f, Environment);
        const auto B = ExecuteReplaySeedReservoir(Previous, CurrentScene, Seed, false, true, 4, true, .01f, Environment);
        const auto Control = ExecuteFreshBudget(FreshAccess, CurrentScene, Seed, 4, Environment, true);
        Require(Fixed.ReplayLastSampleHistoryReset && ReplayCandidateImagesEqual(A, Control),
                "Moving-blocker reset differs from Fresh8");
        ValidateResetBudgetPersistence(Fixed, Previous);
        ++ResetMatches;
        FixedCalls += ValidateResetBudgetCalls(Fixed);
        PreviousCalls += ValidateBoundedPathCount(Previous, false);
        FixedQueries += Fixed.ExtendedStatistics()[17] + Fixed.ExtendedStatistics()[2];
        PreviousQueries += Previous.ExtendedStatistics()[17] + Previous.ExtendedStatistics()[2];
        const auto Guides = Fixed.PrimaryReceivers();
        if (Batch == 0)
        {
            std::vector<std::array<float, 4>> Mask(Guides.size(), {.5f, .02f, .02f, 1});
            for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
            {
                if (Guides[Pixel].MotionPositionToken[3] != 1) continue;
                Pixels.push_back(Pixel);
                Mask[Pixel] = {.05f, 1, .05f, 1};
                std::mt19937_64 Random(0x7bea14f9ull + uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                DirectGpuMoments Cpu;
                for (uint32_t Sample = 0; Sample < CpuSamples; ++Sample)
                    Cpu.Accumulate(ReplayReferencePath(CurrentScene, Guides[Pixel], 4, Random)[0]);
                Reference.push_back(Cpu.Mean());
                ReferenceMean += Cpu.Mean();
                const double Error = Cpu.StandardError() * std::sqrt(double(CpuSamples) / (CpuSamples - 1));
                ReferenceVariance += Error * Error;
            }
            Require(Pixels.size() > 500, "Reset measurement plane is too small");
            ReferenceMean /= Pixels.size();
            WriteImage(Directory + "/ResetPrevious.ppm", B, 32, 32, 3);
            WriteImage(Directory + "/ResetFixed.ppm", A, 32, 32, 3);
            WriteImage(Directory + "/ResetFresh.ppm", Control, 32, 32, 3);
            WriteImage(Directory + "/ResetMask.ppm", Mask, 32, 32, 3);
            std::printf("RESET_REFERENCE points=%zu cpu_paths=%zu mean=%.9g nominal_se=%.9g\n", Pixels.size(),
                        Pixels.size() * CpuSamples, ReferenceMean, std::sqrt(ReferenceVariance) / Pixels.size());
        }
        std::array<double, 2> Sum{}, BatchSquared{};
        for (size_t Index = 0; Index < Pixels.size(); ++Index)
            for (uint32_t Method = 0; Method < 2; ++Method)
            {
                const double Red = Method == 0 ? B[Pixels[Index]][0] : A[Pixels[Index]][0];
                Require(std::isfinite(Red) && Red >= 0, "Reset radiance is invalid");
                const double Error = Red - Reference[Index];
                Sum[Method] += Red;
                BatchSquared[Method] += Error * Error;
            }
        for (uint32_t Method = 0; Method < 2; ++Method)
        {
            Means[Method].Accumulate(Sum[Method] / Pixels.size());
            Squared[Method] += BatchSquared[Method];
        }
        PairedMse.Accumulate((BatchSquared[1] - BatchSquared[0]) / Pixels.size());
        if ((Batch + 1) % 8 == 0)
        {
            std::printf("RESET_PROGRESS completed=%u/%u\n", Batch + 1, Batches);
            std::fflush(stdout);
        }
    }
    const double Correction = std::sqrt(double(Batches) / (Batches - 1));
    for (uint32_t Method = 0; Method < 2; ++Method)
    {
        const double Error = Means[Method].StandardError() * Correction;
        std::printf("RESET_RESULT method=%s batches=%u points=%zu mean=%.9g nominal_se=%.9g rmse=%.9g\n",
                    Method == 0 ? "previous" : "fresh_reset", Batches, Pixels.size(), Means[Method].Mean(), Error,
                    std::sqrt(Squared[Method] / (Batches * Pixels.size())));
        Require(std::abs(Means[Method].Mean() - ReferenceMean) <
                    4 * std::hypot(Error, std::sqrt(ReferenceVariance) / Pixels.size()) + .001,
                "Reset mean disagrees with independent CPU reference");
    }
    std::printf("RESET_PAIRED mse_difference=%.9g nominal_se=%.9g\n", PairedMse.Mean(),
                PairedMse.StandardError() * Correction);
    std::printf("RESET_WORK previous_current=%llu fixed_current=%llu previous_warm=%llu fixed_warm=%llu "
                "previous_queries=%llu fixed_queries=%llu\n", static_cast<unsigned long long>(PreviousCalls),
                static_cast<unsigned long long>(FixedCalls), static_cast<unsigned long long>(PreviousWarmCalls),
                static_cast<unsigned long long>(FixedWarmCalls), static_cast<unsigned long long>(PreviousQueries),
                static_cast<unsigned long long>(FixedQueries));

    //--------------------------------------------------------------------------------------------------------------------
    //                                              REQUEST INVALIDATION AND RELIGHT
    //--------------------------------------------------------------------------------------------------------------------

    uint32_t ExtraResets = 0;
    for (uint32_t Step = 0; Step < 5; ++Step)
    {
        const bool Dark = Step == 2;
        const bool Facets = Step >= 2;
        const bool ExplicitReset = Step == 0 || Step == 4;
        const auto Scene = ReplayCameraPlane(Step == 1 ? .125f : 0, 0, 0, Dark ? 0.f : 2.f);
        const std::array<float, 3> Ambient = Dark ? std::array<float, 3>{0, 0, 0} : Environment;
        const uint32_t Seed = 760001u + Step * 7919u;
        const auto A = ExecuteReplaySeedReservoir(Fixed, Scene, Seed, ExplicitReset, true, 4, Facets, .01f, Ambient);
        const auto B = ExecuteReplaySeedReservoir(Previous, Scene, Seed, ExplicitReset, true, 4, Facets, .01f, Ambient);
        ValidateResetBudgetPersistence(Fixed, Previous);
        (void)ValidateResetBudgetCalls(Fixed);
        if (Fixed.ReplayLastSampleHistoryReset)
        {
            const auto Control = ExecuteFreshBudget(FreshAccess, Scene, Seed, 4, Ambient, Facets);
            Require(ReplayCandidateImagesEqual(A, Control), "Request reset, dark source or relight differs from Fresh8");
            ++ExtraResets;
        }
        else
        {
            Require(ReplayCandidateImagesEqual(A, B), "Camera-only continuation changed");
            ++Continuations;
        }
        if (Dark)
            for (const auto& Pixel : A)
                Require(Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 0, "Reset source-off produced stale light");
    }
    Require(ExtraResets >= 4, "Request invalidation controls did not exercise reset fallback");
    std::printf("RESET_PARITY fresh8_exact=%u extra_reset_exact=%u nonreset_output_and_nine_slabs_exact=%u "
                "persistent_eight_slabs_exact_all=1\n", ResetMatches, ExtraResets, Continuations);
    std::puts("PASS metadata-only fresh reset budget and unchanged temporal continuation; no physical GPU timing");
}
} // namespace
