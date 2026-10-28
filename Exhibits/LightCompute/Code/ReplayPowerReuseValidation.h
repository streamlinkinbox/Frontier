/*==========================================================================================================================================
                                                   REPLAYPOWERREUSEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify same-seed first-hop sharing and power MIS against fused, whole and no-MIS controls.

#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MIS SUBMISSIONS
//------------------------------------------------------------------------------------------------------------------------

void ValidatePowerReuseEquivalence(const char* Driver, const std::string& Shaders)
{
    uint64_t Saved = 0;
    uint32_t Submissions = 0;
    for (uint32_t Condition = 0; Condition < 2; ++Condition)
    {
        const auto Scene = Condition == 0 ? ReplayCameraPlane(0, 0) : ReplaySplitRoom();
        LightComputeSubmission Fused(Driver), Unfused(Driver);
        Fused.Prepare(Scene, Shaders, false, false, false, false, true, false, true);
        Unfused.Prepare(Scene, Shaders + "/unfused", false, false, false, false, true, false, true);
        for (uint32_t Bounces : {1u, 4u})
            for (uint32_t Step = 0; Step < 9; ++Step)
            {
                const uint32_t Seed = 460003u + Step * 7919u + Bounces * 1009u;
                const auto A = ExecuteFirstHopMIS(Fused, Scene, Seed, Step == 0, true, Bounces, true,
                                                  .01f, {.025f, .025f, .025f});
                const auto B = ExecuteFirstHopMIS(Unfused, Scene, Seed, Step == 0, true, Bounces, true,
                                                  .01f, {.025f, .025f, .025f});
                Require(ReplayCandidateImagesEqual(A, B), "First-hop cache changed output bits");
                for (uint32_t Pixel = 0; Pixel < Fused.ReceiverCount; ++Pixel)
                    for (uint32_t Slot : {0u, 1u, 2u, 3u, 4u, 11u, 12u, 13u, 14u})
                    {
                        const auto X = ReplaySampleSlot(Fused, Slot, Pixel);
                        const auto Y = ReplaySampleSlot(Unfused, Slot, Pixel);
                        Require(std::memcmp(X.data(), Y.data(), sizeof(X)) == 0,
                                "First-hop cache changed a reservoir or bank slab");
                    }
                Require(ReplayCandidateImagesEqual(Fused.PrimaryStrikes(), Unfused.PrimaryStrikes()),
                        "First-hop cache overwrote primary diagnostics");
                const auto X = Fused.ExtendedStatistics(), Y = Unfused.ExtendedStatistics();
                for (uint32_t Counter : {2u, 12u, 13u, 14u, 15u, 16u, 20u, 21u, 22u, 23u})
                    Require(X[Counter] == Y[Counter], "First-hop cache changed sampling or shadow work");
                Require(X[17] <= Y[17], "First-hop cache increased replay closest-hit queries");
                Saved += Y[17] - X[17];
                ++Submissions;
            }
    }
    Require(Saved > 0, "First-hop cache did not save any trace queries");
    std::printf("POWER_REUSE_EQUIVALENCE submissions=%u output_nine_slabs_primary_exact=1 saved_closest_queries=%llu\n",
                Submissions, static_cast<unsigned long long>(Saved));
    std::fflush(stdout);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               INDEPENDENT FIXED COMPARISON
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayPowerReuse(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, WarmSteps = 8;
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    std::filesystem::create_directories(Directory);
    ValidatePowerReuseEquivalence(Driver, Shaders);
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
        std::printf("POWER_REUSE_DEVICE condition=%u name=%s type=%u compact_output_bytes=%llu\n", Condition,
                    Reused.Properties.deviceName, unsigned(Reused.Properties.deviceType),
                    static_cast<unsigned long long>(Reused.ReplayOutputDescriptorBytes));
        std::fflush(stdout);
        double MaximumIdentityError = 0;
        {
            ReplaySplitDiagnostic Identity(Diagnostic, Shaders, true);
            for (uint32_t Bounces : {1u, 4u})
            {
                const auto Errors = ExecuteFirstHopMIS(Diagnostic, Current, 930007u + Bounces * 7919u,
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
        std::printf("POWER_REUSE_IDENTITY condition=%u seed_paths=%u maximum_error=%.9g one_bounce_indirect_zero=1\n",
                    Condition, Current.Width * Current.Height * 16, MaximumIdentityError);
        std::mt19937_64 Seeds(0x94cde17bull + Condition * 0x1000193ull);
        std::vector<uint32_t> Pixels;
        std::vector<double> References;
        std::array<DirectGpuMoments, 4> Means;
        std::array<double, 4> Squared{};
        DirectGpuMoments PairedMse, MisMse;
        double ReferenceMean = 0, ReferenceVariance = 0, MaximumFreshDifference = 0, MaximumMisDifference = 0;
        std::array<uint64_t, 4> DirectCalls{}, IndirectCalls{}, Queries{};
        std::array<uint64_t, 4> DirectNearest{}, DirectShadow{}, ReplayNearest{}, ReplayShadow{};
        uint64_t WarmDirect = 0, WarmIndirect = 0, WarmQueries = 0;
        uint64_t WarmDirectNearest = 0, WarmDirectShadow = 0, WarmReplayNearest = 0, WarmReplayShadow = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            {
                const uint32_t Seed = uint32_t(Seeds());
                (void)ExecuteFirstHopMIS(Reused, Previous, Seed, Step == 0, true, 4, true, .01f, Environment);
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
                    (void)ExecuteFirstHopMIS(Diagnostic, Previous, Seed, Step == 0, true, 4, true, .01f, Environment);
            }
            const uint32_t Seed = uint32_t(Seeds());
            const auto WholeImage = ExecuteFirstHopWhole(WholeAccess, Current, Seed, Environment, true);
            const auto FreshImage = ExecuteFirstHopMIS(FreshSplit, Current, Seed, true, true, 4, true, .01f, Environment);
            const auto ReusedImage = ExecuteFirstHopMIS(Reused, Current, Seed, false, true, 4, true, .01f, Environment);
            ValidateSplitCalls(FreshSplit);
            ValidateSplitCalls(Reused);
            const auto WholeCounts = Whole.ExtendedStatistics();
            Require(WholeCounts[12] == 0 && WholeCounts[13] == 0 && WholeCounts[14] == 0,
                    "Private direct counters collide with whole-path transport");
            (void)ValidateBoundedPathCount(Whole, true);
            const auto SplitCounts = FreshSplit.ExtendedStatistics();
            Require(SplitCounts[16] == WholeCounts[16] && SplitCounts[17] + SplitCounts[13] == WholeCounts[17] &&
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
            const auto NoMisImage = ExecuteFirstHopWhole(WholeAccess, Current, Seed, Environment, false);
            const auto NoMisCounts = Whole.ExtendedStatistics();
            for (size_t Pixel = 0; Pixel < WholeImage.size(); ++Pixel)
                MaximumMisDifference = std::max(MaximumMisDifference,
                    std::abs(double(WholeImage[Pixel][0]) - NoMisImage[Pixel][0]));
            IndirectCalls[3] += NoMisCounts[16];
            ReplayNearest[3] += NoMisCounts[17];
            ReplayShadow[3] += NoMisCounts[2];
            Queries[3] += uint64_t(NoMisCounts[17]) + NoMisCounts[2];
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
                    const auto DirectReused = ExecuteFirstHopMIS(Diagnostic, Current, Seed, false,
                                                                         true, 4, true, .01f, Environment);
                    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                        for (uint32_t Slot : {0u, 1u, 2u, 3u, 4u, 11u, 12u, 13u, 14u})
                        {
                            const auto A = ReplaySampleSlot(Diagnostic, Slot, Pixel);
                            const auto B = ReplaySampleSlot(Reused, Slot, Pixel);
                            Require(std::memcmp(A.data(), B.data(), sizeof(A)) == 0,
                                    "Direct-only diagnostic changed indirect history");
                        }
                    const auto DirectFresh = ExecuteFirstHopMIS(Diagnostic, Current, Seed, true,
                                                                         true, 4, true, .01f, Environment);
                    Require(ReplayCandidateImagesEqual(DirectReused, DirectFresh),
                            "Temporal or spatial selection changed the direct-light estimate");
                    std::printf("POWER_REUSE_DIRECT_PARITY condition=%u all_pixels_exact=1 indirect_nine_slabs_exact=1\n", Condition);
                }
                std::vector<std::array<float, 4>> Mask(Guides.size(), {.5f, .02f, .02f, 1});
                for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                {
                    const auto& Receiver = Guides[Pixel];
                    if (Receiver.PositionHit[3] == 0 || Receiver.MotionPositionToken[3] <= 0 ||
                        std::max({Receiver.AlbedoIor[0], Receiver.AlbedoIor[1], Receiver.AlbedoIor[2]}) <= 0) continue;
                    Pixels.push_back(Pixel);
                    Mask[Pixel] = {.05f, 1, .05f, 1};
                    std::mt19937_64 Random(0x1c57ea39ull + Condition * 0x1000193ull +
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
                WriteImage(Prefix + "NoMIS.ppm", NoMisImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Whole.ppm", WholeImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Fresh.ppm", FreshImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Reuse.ppm", ReusedImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Mask.ppm", Mask, Current.Width, Current.Height, 3);
                std::printf("POWER_REUSE_REFERENCE condition=%u points=%zu cpu_paths=%zu mean=%.9g nominal_se=%.9g\n",
                            Condition, Pixels.size(), Pixels.size() * CpuSamples, ReferenceMean,
                            std::sqrt(ReferenceVariance) / Pixels.size());
                std::fflush(stdout);
            }
            std::array<double, 4> Sum{}, BatchSquared{};
            const std::array<const std::vector<std::array<float, 4>>*, 4> Images{&WholeImage, &FreshImage, &ReusedImage, &NoMisImage};
            for (size_t Index = 0; Index < Pixels.size(); ++Index)
                for (uint32_t Method = 0; Method < 4; ++Method)
                {
                    const double Red = (*Images[Method])[Pixels[Index]][0];
                    Require(std::isfinite(Red) && Red >= 0, "Indirect split radiance is invalid");
                    const double Error = Red - References[Index];
                    Sum[Method] += Red;
                    BatchSquared[Method] += Error * Error;
                }
            for (uint32_t Method = 0; Method < 4; ++Method)
            {
                Means[Method].Accumulate(Sum[Method] / Pixels.size());
                Squared[Method] += BatchSquared[Method];
            }
            PairedMse.Accumulate((BatchSquared[2] - BatchSquared[0]) / Pixels.size());
            MisMse.Accumulate((BatchSquared[0] - BatchSquared[3]) / Pixels.size());
            if ((Batch + 1) % 8 == 0)
            {
                std::printf("POWER_REUSE_PROGRESS condition=%u completed=%u/%u\n", Condition, Batch + 1, Batches);
                std::fflush(stdout);
            }
        }
        Require(MaximumMisDifference > 1e-7, "Power MIS control was inactive");
        std::printf("POWER_REUSE_ACTIVE condition=%u maximum_disabled_difference=%.9g\n",
                    Condition, MaximumMisDifference);
        const double Correction = std::sqrt(double(Batches) / (Batches - 1));
        for (uint32_t Method = 0; Method < 4; ++Method)
        {
            const double Error = Means[Method].StandardError() * Correction;
            const bool MeanAgrees = std::abs(Means[Method].Mean() - ReferenceMean) <
                4 * std::hypot(Error, std::sqrt(ReferenceVariance) / Pixels.size()) + .001;
            Correctness = Correctness && MeanAgrees;
            const char* Name = Method == 0 ? "whole_power" : Method == 1 ? "fused_fresh" : Method == 2 ? "fused_reuse" : "whole_no_mis";
            std::printf("POWER_REUSE_RESULT condition=%u method=%s batches=%u points=%zu mean=%.9g nominal_se=%.9g "
                        "rmse=%.9g mean_gate=%u\n", Condition, Name, Batches, Pixels.size(), Means[Method].Mean(),
                        Error, std::sqrt(Squared[Method] / (Batches * Pixels.size())), unsigned(MeanAgrees));
            std::printf("POWER_REUSE_WORK condition=%u method=%s direct_calls=%llu indirect_or_whole_calls=%llu "
                        "transport_queries=%llu direct_nearest=%llu direct_shadow=%llu replay_nearest=%llu replay_shadow=%llu\n",
                        Condition, Name, static_cast<unsigned long long>(DirectCalls[Method]),
                        static_cast<unsigned long long>(IndirectCalls[Method]), static_cast<unsigned long long>(Queries[Method]),
                        static_cast<unsigned long long>(DirectNearest[Method]), static_cast<unsigned long long>(DirectShadow[Method]),
                        static_cast<unsigned long long>(ReplayNearest[Method]), static_cast<unsigned long long>(ReplayShadow[Method]));
        }
        const bool Quality = PairedMse.Mean() + 3 * PairedMse.StandardError() * Correction < 0;
        Promotion = Promotion && Quality;
        std::printf("POWER_REUSE_PAIRED condition=%u mse_difference=%.9g nominal_se=%.9g quality_gate=%u "
                    "fresh_split_maximum_difference=%.9g\n", Condition, PairedMse.Mean(),
                    PairedMse.StandardError() * Correction, unsigned(Quality), MaximumFreshDifference);
        std::printf("POWER_REUSE_MIS_PAIRED condition=%u mse_difference=%.9g nominal_se=%.9g\n", Condition,
                    MisMse.Mean(), MisMse.StandardError() * Correction);
        std::printf("POWER_REUSE_WARM condition=%u submissions=%u direct_calls=%llu indirect_calls=%llu transport_queries=%llu "
                    "direct_nearest=%llu direct_shadow=%llu replay_nearest=%llu replay_shadow=%llu\n",
                    Condition, Batches * WarmSteps, static_cast<unsigned long long>(WarmDirect),
                    static_cast<unsigned long long>(WarmIndirect), static_cast<unsigned long long>(WarmQueries),
                    static_cast<unsigned long long>(WarmDirectNearest), static_cast<unsigned long long>(WarmDirectShadow),
                    static_cast<unsigned long long>(WarmReplayNearest), static_cast<unsigned long long>(WarmReplayShadow));
        std::fflush(stdout);
        const auto DarkScene = ReplayCameraPlane(0, 0, 0, 0, Current.Width);
        const auto Dark = ExecuteFirstHopMIS(Reused, DarkScene, 830003, false, true, 4, true, .01f, {0, 0, 0});
        Require(Reused.ReplayLastSampleHistoryReset, "Split source-off did not invalidate history");
        ValidateSplitCalls(Reused);
        for (const auto& Pixel : Dark)
            Require(Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 0, "Split source-off leaked stale lighting");
        std::printf("POWER_REUSE_RESET condition=%u source_off_black=1\n", Condition);
    }
    std::printf("POWER_REUSE_DECISION correctness=%u promote=%u physical_gpu_timing=0\n",
                unsigned(Correctness), unsigned(Correctness && Promotion));
    Require(Correctness, "Indirect split failed its fixed mean gates");
}
} // namespace
