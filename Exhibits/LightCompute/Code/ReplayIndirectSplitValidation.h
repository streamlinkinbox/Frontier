/*==========================================================================================================================================
                                                REPLAYINDIRECTSPLITVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify pathwise decomposition, untouched fresh direct lighting, independent quality and explicitly larger trace work.

#pragma once

namespace
{
struct ReplaySplitDiagnostic
{
    LightComputeSubmission& Compute;
    VkPipeline PreviousGeneration, PreviousResolution;
    VkPipeline Generation = VK_NULL_HANDLE, Resolution = VK_NULL_HANDLE;

    ReplaySplitDiagnostic(LightComputeSubmission& Submission, const std::string& Shaders, bool Identity)
        : Compute(Submission), PreviousGeneration(Submission.ReplayGeneratePipeline),
          PreviousResolution(Submission.ReplayResolvePipeline)
    {
        if (Identity)
        {
            Generation = Compute.ConstructPipeline(Shaders + "/SplitIdentityGenerate.spv");
            Compute.ReplayGeneratePipeline = Generation;
        }
        Resolution = Compute.ConstructPipeline(Shaders + (Identity ? "/SplitIdentityResolve.spv" : "/SplitDirectResolve.spv"));
        Compute.ReplayResolvePipeline = Resolution;
    }
    ~ReplaySplitDiagnostic()
    {
        Compute.DeviceWaitIdle(Compute.Device);
        Compute.ReplayGeneratePipeline = PreviousGeneration;
        Compute.ReplayResolvePipeline = PreviousResolution;
        if (Generation) Compute.DestroyPipeline(Compute.Device, Generation, nullptr);
        Compute.DestroyPipeline(Compute.Device, Resolution, nullptr);
    }
    ReplaySplitDiagnostic(const ReplaySplitDiagnostic&) = delete;
    ReplaySplitDiagnostic& operator=(const ReplaySplitDiagnostic&) = delete;
};

LightComputeArchive ReplaySplitRoom()
{
    auto Scene = ReplayRoom(7);
    Scene.Width = Scene.Height = 24;
    Scene.Receivers.assign(24u * 24u, {});
    Scene.Camera[2][3] = Scene.Camera[6][3] = Scene.Camera[3][3];
    return Scene;
}

void ValidateSplitCalls(const LightComputeSubmission& Compute)
{
    uint32_t Visible = 0;
    for (const auto& Receiver : Compute.PrimaryReceivers())
        if (Receiver.PositionHit[3] != 0 && Receiver.MotionPositionToken[3] > 0 && Receiver.ViewMetallic[3] == 0) ++Visible;
    const auto Counts = Compute.ExtendedStatistics();
    Require(Counts[12] == 8u * Visible && Counts[13] <= Counts[12] && Counts[14] <= Counts[12],
            "Fresh direct-light budget or private direct counters are invalid");
    if (Compute.ReplayLastSampleHistoryReset)
    {
        Require(Counts[16] == 8u * Visible && Counts[20] == 0 && Counts[22] == 0 && Counts[23] == 0,
                "Split reset did not use eight fresh indirect samples without reuse");
    }
    else (void)ValidateBoundedPathCount(Compute, false);
}

void ValidateReplayIndirectSplit(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, WarmSteps = 8;
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    std::filesystem::create_directories(Directory);
    bool Correctness = true, Promotion = true;
    for (uint32_t Condition = 0; Condition < 2; ++Condition)
    {
        const auto Previous = Condition == 0 ? ReplayCameraPlane(0, 0) : ReplaySplitRoom();
        const auto Current = Condition == 0 ? ReplayCameraPlane(.03125f, 0) : ReplaySplitRoom();
        const uint32_t CpuSamples = Condition == 0 ? 4096u : 2048u;
        LightComputeSubmission Reused(Driver), FreshSplit(Driver), Whole(Driver), Diagnostic(Driver);
        Reused.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
        FreshSplit.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
        Whole.Prepare(Previous, Shaders + "/whole", false, false, false, false, true, false, true);
        Diagnostic.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
        ReplayFreshBudgetPipeline WholeAccess(Whole, Shaders + "/whole");
        std::printf("SPLIT_DEVICE condition=%u name=%s type=%u compact_output_bytes=%llu\n", Condition,
                    Reused.Properties.deviceName, unsigned(Reused.Properties.deviceType),
                    static_cast<unsigned long long>(Reused.ReplayOutputDescriptorBytes));
        std::fflush(stdout);
        double MaximumIdentityError = 0;
        {
            ReplaySplitDiagnostic Identity(Diagnostic, Shaders, true);
            for (uint32_t Bounces : {1u, 4u})
            {
                const auto Errors = ExecuteReplaySeedReservoir(Diagnostic, Current, 930007u + Bounces * 7919u,
                                                               true, true, Bounces, true, .01f, Environment);
                for (const auto& Error : Errors)
                {
                    Require(std::isfinite(Error[0]) && Error[0] <= 2e-6f * (1 + Error[2]),
                            "Direct plus indirect does not reconstruct the same-seed whole path");
                    Require(Bounces != 1 || Error[1] == 0, "One-bounce indirect contribution is not zero");
                    MaximumIdentityError = std::max(MaximumIdentityError, double(Error[0]));
                }
            }
        }
        std::printf("SPLIT_IDENTITY condition=%u seed_paths=%u maximum_error=%.9g one_bounce_indirect_zero=1\n",
                    Condition, Current.Width * Current.Height * 16, MaximumIdentityError);
        std::mt19937_64 Seeds(0x24c59e71ull + Condition * 0x1000193ull);
        std::vector<uint32_t> Pixels;
        std::vector<double> References;
        std::array<DirectGpuMoments, 3> Means;
        std::array<double, 3> Squared{};
        DirectGpuMoments PairedMse;
        double ReferenceMean = 0, ReferenceVariance = 0, MaximumFreshDifference = 0;
        std::array<uint64_t, 3> DirectCalls{}, IndirectCalls{}, Queries{};
        std::array<uint64_t, 3> DirectNearest{}, DirectShadow{}, ReplayNearest{}, ReplayShadow{};
        uint64_t WarmDirect = 0, WarmIndirect = 0, WarmQueries = 0;
        uint64_t WarmDirectNearest = 0, WarmDirectShadow = 0, WarmReplayNearest = 0, WarmReplayShadow = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            {
                const uint32_t Seed = uint32_t(Seeds());
                (void)ExecuteReplaySeedReservoir(Reused, Previous, Seed, Step == 0, true, 4, true, .01f, Environment);
                ValidateSplitCalls(Reused);
                const auto Counts = Reused.ExtendedStatistics();
                WarmDirectNearest += Counts[13];
                WarmDirectShadow += Counts[14];
                WarmReplayNearest += Counts[17];
                WarmReplayShadow += Counts[2];
                WarmDirect += Counts[12];
                WarmIndirect += Counts[16];
                WarmQueries += uint64_t(Counts[13]) + Counts[14] + Counts[17] + Counts[2];
                if (Batch == 0)
                    (void)ExecuteReplaySeedReservoir(Diagnostic, Previous, Seed, Step == 0, true, 4, true, .01f, Environment);
            }
            const uint32_t Seed = uint32_t(Seeds());
            const auto WholeImage = ExecuteFreshBudget(WholeAccess, Current, Seed, 4, Environment, true);
            const auto FreshImage = ExecuteReplaySeedReservoir(FreshSplit, Current, Seed, true, true, 4, true, .01f, Environment);
            const auto ReusedImage = ExecuteReplaySeedReservoir(Reused, Current, Seed, false, true, 4, true, .01f, Environment);
            ValidateSplitCalls(FreshSplit);
            ValidateSplitCalls(Reused);
            const auto WholeCounts = Whole.ExtendedStatistics();
            Require(WholeCounts[12] == 0 && WholeCounts[13] == 0 && WholeCounts[14] == 0,
                    "Private direct counters collide with whole-path transport");
            (void)ValidateBoundedPathCount(Whole, true);
            const auto SplitCounts = FreshSplit.ExtendedStatistics();
            Require(SplitCounts[16] == WholeCounts[16] && SplitCounts[17] == WholeCounts[17] &&
                        SplitCounts[2] + SplitCounts[14] == WholeCounts[2],
                    "Matched split changed indirect geometry paths or total shadow work");
            const std::array<LightComputeSubmission*, 3> Methods{&Whole, &FreshSplit, &Reused};
            for (uint32_t Method = 0; Method < 3; ++Method)
            {
                const auto Counts = Methods[Method]->ExtendedStatistics();
                DirectNearest[Method] += Counts[13];
                DirectShadow[Method] += Counts[14];
                ReplayNearest[Method] += Counts[17];
                ReplayShadow[Method] += Counts[2];
                DirectCalls[Method] += Counts[12];
                IndirectCalls[Method] += Counts[16];
                Queries[Method] += uint64_t(Counts[13]) + Counts[14] + Counts[17] + Counts[2];
            }
            const auto Guides = Reused.PrimaryReceivers();
            for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                {
                    const double Error = std::abs(double(FreshImage[Pixel][Channel]) - WholeImage[Pixel][Channel]);
                    MaximumFreshDifference = std::max(MaximumFreshDifference, Error);
                    Require(Error <= 4e-6 * (1 + std::abs(WholeImage[Pixel][Channel])),
                            "Matched fresh split differs from whole Fresh8 beyond rounding tolerance");
                }
            if (Batch == 0)
            {
                {
                    ReplaySplitDiagnostic DirectOnly(Diagnostic, Shaders, false);
                    const auto DirectReused = ExecuteReplaySeedReservoir(Diagnostic, Current, Seed, false,
                                                                         true, 4, true, .01f, Environment);
                    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                        for (uint32_t Slot : {0u, 1u, 2u, 3u, 4u, 11u, 12u, 13u, 14u})
                        {
                            const auto A = ReplaySampleSlot(Diagnostic, Slot, Pixel);
                            const auto B = ReplaySampleSlot(Reused, Slot, Pixel);
                            Require(std::memcmp(A.data(), B.data(), sizeof(A)) == 0,
                                    "Direct-only diagnostic changed indirect history");
                        }
                    const auto DirectFresh = ExecuteReplaySeedReservoir(Diagnostic, Current, Seed, true,
                                                                         true, 4, true, .01f, Environment);
                    Require(ReplayCandidateImagesEqual(DirectReused, DirectFresh),
                            "Temporal or spatial selection changed the direct-light estimate");
                    std::printf("SPLIT_DIRECT_PARITY condition=%u all_pixels_exact=1 indirect_nine_slabs_exact=1\n", Condition);
                }
                std::vector<std::array<float, 4>> Mask(Guides.size(), {.5f, .02f, .02f, 1});
                for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                {
                    const auto& Receiver = Guides[Pixel];
                    if (Receiver.PositionHit[3] == 0 || Receiver.MotionPositionToken[3] <= 0 ||
                        std::max({Receiver.AlbedoIor[0], Receiver.AlbedoIor[1], Receiver.AlbedoIor[2]}) <= 0) continue;
                    Pixels.push_back(Pixel);
                    Mask[Pixel] = {.05f, 1, .05f, 1};
                    std::mt19937_64 Random(0x574da921ull + Condition * 0x1000193ull +
                                           uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                    DirectGpuMoments Cpu;
                    for (uint32_t Sample = 0; Sample < CpuSamples; ++Sample)
                        Cpu.Accumulate(ReplayReferencePath(Current, Receiver, 4, Random)[0]);
                    References.push_back(Cpu.Mean());
                    ReferenceMean += Cpu.Mean();
                    const double Error = Cpu.StandardError() * std::sqrt(double(CpuSamples) / (CpuSamples - 1));
                    ReferenceVariance += Error * Error;
                }
                Require(Pixels.size() > 100, "Indirect split primary-geometry mask is too small");
                ReferenceMean /= Pixels.size();
                const std::string Prefix = Directory + "/Split" + std::to_string(Condition);
                WriteImage(Prefix + "Whole.ppm", WholeImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Fresh.ppm", FreshImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Reuse.ppm", ReusedImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Mask.ppm", Mask, Current.Width, Current.Height, 3);
                std::printf("SPLIT_REFERENCE condition=%u points=%zu cpu_paths=%zu mean=%.9g nominal_se=%.9g\n",
                            Condition, Pixels.size(), Pixels.size() * CpuSamples, ReferenceMean,
                            std::sqrt(ReferenceVariance) / Pixels.size());
                std::fflush(stdout);
            }
            std::array<double, 3> Sum{}, BatchSquared{};
            const std::array<const std::vector<std::array<float, 4>>*, 3> Images{&WholeImage, &FreshImage, &ReusedImage};
            for (size_t Index = 0; Index < Pixels.size(); ++Index)
                for (uint32_t Method = 0; Method < 3; ++Method)
                {
                    const double Red = (*Images[Method])[Pixels[Index]][0];
                    Require(std::isfinite(Red) && Red >= 0, "Indirect split radiance is invalid");
                    const double Error = Red - References[Index];
                    Sum[Method] += Red;
                    BatchSquared[Method] += Error * Error;
                }
            for (uint32_t Method = 0; Method < 3; ++Method)
            {
                Means[Method].Accumulate(Sum[Method] / Pixels.size());
                Squared[Method] += BatchSquared[Method];
            }
            PairedMse.Accumulate((BatchSquared[2] - BatchSquared[0]) / Pixels.size());
            if ((Batch + 1) % 8 == 0)
            {
                std::printf("SPLIT_PROGRESS condition=%u completed=%u/%u\n", Condition, Batch + 1, Batches);
                std::fflush(stdout);
            }
        }
        const double Correction = std::sqrt(double(Batches) / (Batches - 1));
        for (uint32_t Method = 0; Method < 3; ++Method)
        {
            const double Error = Means[Method].StandardError() * Correction;
            const bool MeanAgrees = std::abs(Means[Method].Mean() - ReferenceMean) <
                4 * std::hypot(Error, std::sqrt(ReferenceVariance) / Pixels.size()) + .001;
            Correctness = Correctness && MeanAgrees;
            const char* Name = Method == 0 ? "whole_fresh" : Method == 1 ? "split_fresh" : "split_reuse";
            std::printf("SPLIT_RESULT condition=%u method=%s batches=%u points=%zu mean=%.9g nominal_se=%.9g "
                        "rmse=%.9g mean_gate=%u\n", Condition, Name, Batches, Pixels.size(), Means[Method].Mean(),
                        Error, std::sqrt(Squared[Method] / (Batches * Pixels.size())), unsigned(MeanAgrees));
            std::printf("SPLIT_WORK condition=%u method=%s direct_calls=%llu indirect_or_whole_calls=%llu "
                        "transport_queries=%llu direct_nearest=%llu direct_shadow=%llu replay_nearest=%llu replay_shadow=%llu\n",
                        Condition, Name, static_cast<unsigned long long>(DirectCalls[Method]),
                        static_cast<unsigned long long>(IndirectCalls[Method]), static_cast<unsigned long long>(Queries[Method]),
                        static_cast<unsigned long long>(DirectNearest[Method]), static_cast<unsigned long long>(DirectShadow[Method]),
                        static_cast<unsigned long long>(ReplayNearest[Method]), static_cast<unsigned long long>(ReplayShadow[Method]));
        }
        const bool Quality = PairedMse.Mean() + 3 * PairedMse.StandardError() * Correction < 0;
        Promotion = Promotion && Quality;
        std::printf("SPLIT_PAIRED condition=%u mse_difference=%.9g nominal_se=%.9g quality_gate=%u "
                    "fresh_split_maximum_difference=%.9g\n", Condition, PairedMse.Mean(),
                    PairedMse.StandardError() * Correction, unsigned(Quality), MaximumFreshDifference);
        std::printf("SPLIT_WARM condition=%u submissions=%u direct_calls=%llu indirect_calls=%llu transport_queries=%llu "
                    "direct_nearest=%llu direct_shadow=%llu replay_nearest=%llu replay_shadow=%llu\n",
                    Condition, Batches * WarmSteps, static_cast<unsigned long long>(WarmDirect),
                    static_cast<unsigned long long>(WarmIndirect), static_cast<unsigned long long>(WarmQueries),
                    static_cast<unsigned long long>(WarmDirectNearest), static_cast<unsigned long long>(WarmDirectShadow),
                    static_cast<unsigned long long>(WarmReplayNearest), static_cast<unsigned long long>(WarmReplayShadow));
        std::fflush(stdout);
        const auto DarkScene = ReplayCameraPlane(0, 0, 0, 0, Current.Width);
        const auto Dark = ExecuteReplaySeedReservoir(Reused, DarkScene, 830003, false, true, 4, true, .01f, {0, 0, 0});
        Require(Reused.ReplayLastSampleHistoryReset, "Split source-off did not invalidate history");
        ValidateSplitCalls(Reused);
        for (const auto& Pixel : Dark)
            Require(Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 0, "Split source-off leaked stale lighting");
        std::printf("SPLIT_RESET condition=%u source_off_black=1\n", Condition);
    }
    std::printf("SPLIT_DECISION correctness=%u promote=%u physical_gpu_timing=0\n",
                unsigned(Correctness), unsigned(Correctness && Promotion));
    Require(Correctness, "Indirect split failed its fixed mean gates");
}
} // namespace
