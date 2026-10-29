/*==========================================================================================================================================
                                                 REPLAYREUSEABLATIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Isolate temporal bank reuse from spatial replay against unchanged whole-power Fresh8 transport.
#pragma once

namespace
{
uint64_t ReplayAblationQueries(const std::array<uint32_t, 24>& Counts)
{
    return uint64_t(Counts[13]) + Counts[14] + Counts[17] + Counts[2];
}

void ValidateReplayReuseAblation(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, WarmSteps = 8;
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    std::filesystem::create_directories(Directory);
    bool Correctness = true;
    std::array<bool, 2> Quality{true, true};
    for (uint32_t Condition = 0; Condition < 2; ++Condition)
    {
        const auto Previous = Condition == 0 ? ReplayCameraPlane(0, 0) : ReplaySplitRoom();
        const auto Current = Condition == 0 ? ReplayCameraPlane(.03125f, 0) : ReplaySplitRoom();
        const auto Facets = ReplayConvergenceFacets(Current);
        std::vector<size_t> Sources;
        for (size_t Index = 0; Index < Facets.size(); ++Index)
            if (Facets[Index].Radiance[0] > 0) Sources.push_back(Index);
        Require(!Sources.empty(), "Ablation fixture has no emissive source");
        LightComputeSubmission Whole(Driver), Fresh(Driver), Temporal(Driver), Spatial(Driver);
        Whole.Prepare(Previous, Shaders + "/whole", false, false, false, false, true, false, true);
        Fresh.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
        Temporal.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
        Spatial.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
        ReplayFreshBudgetPipeline WholeAccess(Whole, Shaders + "/whole");
        // 📝 On a cold reset the spatial toggle must not change the Fresh8 proposal or its persisted slabs.
        const auto ColdA = ExecuteFirstHopMIS(Temporal, Previous, 219743, true, false, 4, true, .01f, Environment);
        const auto ColdB = ExecuteFirstHopMIS(Spatial, Previous, 219743, true, true, 4, true, .01f, Environment);
        Require(ReplayCandidateImagesEqual(ColdA, ColdB), "Spatial toggle changed cold output bits");
        for (uint32_t Pixel = 0; Pixel < Temporal.ReceiverCount; ++Pixel)
            for (uint32_t Slot : {0u, 1u, 2u, 3u, 4u, 11u, 12u, 13u, 14u})
            {
                const auto A = ReplaySampleSlot(Temporal, Slot, Pixel), B = ReplaySampleSlot(Spatial, Slot, Pixel);
                Require(std::memcmp(A.data(), B.data(), sizeof(A)) == 0, "Spatial toggle changed cold history bits");
            }
        std::printf("ABLATION_COLD condition=%u output_nine_slabs_exact=1 device=%s type=%u\n", Condition,
                    Whole.Properties.deviceName, unsigned(Whole.Properties.deviceType));
        std::fflush(stdout);
        std::array<DirectGpuMoments, 4> Means;
        std::array<double, 4> Squared{};
        std::array<uint64_t, 4> Queries{}, Nearest{}, Shadow{}, Accepted{}, Links{};
        std::array<uint64_t, 2> WarmQueries{}, WarmAccepted{}, WarmLinks{};
        std::array<DirectGpuMoments, 3> Paired;
        std::vector<uint32_t> Pixels;
        std::vector<double> References;
        double ReferenceMean = 0, ReferenceVariance = 0, MaximumFreshDifference = 0;
        std::mt19937_64 Seeds(0x62be3197ull + Condition * 0x1000193ull);
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            {
                const uint32_t Seed = uint32_t(Seeds());
                (void)ExecuteFirstHopMIS(Temporal, Previous, Seed, Step == 0, false, 4, true, .01f, Environment);
                (void)ExecuteFirstHopMIS(Spatial, Previous, Seed, Step == 0, true, 4, true, .01f, Environment);
                ValidateSplitCalls(Temporal); ValidateSplitCalls(Spatial);
                const auto A = Temporal.ExtendedStatistics(), B = Spatial.ExtendedStatistics();
                Require(A[22] == 0, "Temporal-only warm-up consumed spatial links");
                for (uint32_t Method = 0; Method < 2; ++Method)
                {
                    const auto& Counts = Method == 0 ? A : B;
                    WarmQueries[Method] += ReplayAblationQueries(Counts);
                    WarmAccepted[Method] += Counts[20]; WarmLinks[Method] += Counts[22];
                }
            }
            const uint32_t Seed = uint32_t(Seeds());
            const auto A = ExecuteFirstHopWhole(WholeAccess, Current, Seed, Environment, true);
            const auto B = ExecuteFirstHopMIS(Fresh, Current, Seed, true, true, 4, true, .01f, Environment);
            const auto C = ExecuteFirstHopMIS(Temporal, Current, Seed, false, false, 4, true, .01f, Environment);
            const auto D = ExecuteFirstHopMIS(Spatial, Current, Seed, false, true, 4, true, .01f, Environment);
            ValidateSplitCalls(Fresh); ValidateSplitCalls(Temporal); ValidateSplitCalls(Spatial);
            const auto W = Whole.ExtendedStatistics(), F = Fresh.ExtendedStatistics(), T = Temporal.ExtendedStatistics();
            Require(ReplayAblationQueries(W) == ReplayAblationQueries(F), "Fresh fusion changed query budget");
            Require(T[22] == 0, "Temporal-only measurement consumed spatial links");
            for (size_t Pixel = 0; Pixel < A.size(); ++Pixel)
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                {
                    const double Difference = std::abs(double(A[Pixel][Channel]) - B[Pixel][Channel]);
                    MaximumFreshDifference = std::max(MaximumFreshDifference, Difference);
                    Require(Difference <= 4e-6 * (1 + std::abs(A[Pixel][Channel])), "Fresh split changed transport");
                }
            const std::array<LightComputeSubmission*, 4> Submissions{&Whole, &Fresh, &Temporal, &Spatial};
            for (uint32_t Method = 0; Method < 4; ++Method)
            {
                const auto Counts = Submissions[Method]->ExtendedStatistics();
                Queries[Method] += ReplayAblationQueries(Counts);
                Nearest[Method] += uint64_t(Counts[13]) + Counts[17];
                Shadow[Method] += uint64_t(Counts[14]) + Counts[2];
                Accepted[Method] += Counts[20]; Links[Method] += Counts[22];
            }
            if (Batch == 0)
            {
                const auto Guides = Whole.PrimaryReceivers();
                const uint32_t Samples = Condition == 0 ? 4096u : 2048u;
                for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                {
                    const auto& R = Guides[Pixel];
                    if (R.PositionHit[3] == 0 || R.MotionPositionToken[3] <= 0 ||
                        std::max({R.AlbedoIor[0], R.AlbedoIor[1], R.AlbedoIor[2]}) <= 0) continue;
                    Pixels.push_back(Pixel);
                    std::mt19937_64 Random(0x39a6fc21ull + Condition * 0x1000193ull +
                                           uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                    DirectGpuMoments Cpu;
                    for (uint32_t Sample = 0; Sample < Samples; ++Sample)
                        Cpu.Accumulate(ReplayConvergencePath(Facets, Sources, R, 4, Random, true));
                    References.push_back(Cpu.Mean()); ReferenceMean += Cpu.Mean();
                    const double Error = Cpu.StandardError() * std::sqrt(double(Samples) / (Samples - 1));
                    ReferenceVariance += Error * Error;
                }
                Require(Pixels.size() == (Condition == 0 ? 1024u : 528u), "Ablation measured mask changed");
                ReferenceMean /= Pixels.size();
                const std::string Prefix = Directory + "/Ablation" + std::to_string(Condition);
                WriteImage(Prefix + "Whole.ppm", A, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Fresh.ppm", B, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Temporal.ppm", C, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Spatial.ppm", D, Current.Width, Current.Height, 3);
                std::printf("ABLATION_REFERENCE condition=%u points=%zu paths=%zu mean=%.9g nominal_se=%.9g\n",
                            Condition, Pixels.size(), Pixels.size() * Samples, ReferenceMean,
                            std::sqrt(ReferenceVariance) / Pixels.size());
            }
            const std::array<const std::vector<std::array<float, 4>>*, 4> Images{&A, &B, &C, &D};
            std::array<double, 4> Sum{}, ErrorSquared{};
            for (size_t Index = 0; Index < Pixels.size(); ++Index)
                for (uint32_t Method = 0; Method < 4; ++Method)
                {
                    const double Red = (*Images[Method])[Pixels[Index]][0];
                    Require(std::isfinite(Red) && Red >= 0, "Ablation radiance is invalid");
                    Sum[Method] += Red;
                    const double Error = Red - References[Index];
                    ErrorSquared[Method] += Error * Error;
                }
            for (uint32_t Method = 0; Method < 4; ++Method)
            {
                Means[Method].Accumulate(Sum[Method] / Pixels.size()); Squared[Method] += ErrorSquared[Method];
            }
            Paired[0].Accumulate((ErrorSquared[2] - ErrorSquared[0]) / Pixels.size());
            Paired[1].Accumulate((ErrorSquared[3] - ErrorSquared[0]) / Pixels.size());
            Paired[2].Accumulate((ErrorSquared[3] - ErrorSquared[2]) / Pixels.size());
            if ((Batch + 1) % 8 == 0) std::printf("ABLATION_PROGRESS condition=%u completed=%u/%u\n", Condition, Batch + 1, Batches);
            std::fflush(stdout);
        }
        Require(Accepted[2] > 0 && Accepted[3] > 0 && Links[2] == 0 && Links[3] > 0,
                "Reuse ablation did not exercise isolated temporal/spatial features");
        const double Correction = std::sqrt(double(Batches) / (Batches - 1));
        for (uint32_t Method = 0; Method < 4; ++Method)
        {
            const double Error = Means[Method].StandardError() * Correction;
            const bool Agrees = std::abs(Means[Method].Mean() - ReferenceMean) <
                4 * std::hypot(Error, std::sqrt(ReferenceVariance) / Pixels.size()) + .001;
            Correctness = Correctness && Agrees;
            std::printf("ABLATION_RESULT condition=%u method=%s mean=%.9g nominal_se=%.9g rmse=%.9g mean_gate=%u "
                        "queries=%llu nearest=%llu shadow=%llu accepted=%llu spatial_links=%llu\n", Condition,
                        Method == 0 ? "whole" : Method == 1 ? "fresh" : Method == 2 ? "temporal" : "spatial",
                        Means[Method].Mean(), Error, std::sqrt(Squared[Method] / (Batches * Pixels.size())), unsigned(Agrees),
                        static_cast<unsigned long long>(Queries[Method]), static_cast<unsigned long long>(Nearest[Method]),
                        static_cast<unsigned long long>(Shadow[Method]), static_cast<unsigned long long>(Accepted[Method]),
                        static_cast<unsigned long long>(Links[Method]));
        }
        for (uint32_t Pair = 0; Pair < 3; ++Pair)
        {
            const double Error = Paired[Pair].StandardError() * Correction;
            const bool Improves = Paired[Pair].Mean() + 3 * Error < 0;
            if (Pair < 2) Quality[Pair] = Quality[Pair] && Improves;
            std::printf("ABLATION_PAIRED condition=%u pair=%s mse_difference=%.9g nominal_se=%.9g improvement_gate=%u\n",
                        Condition, Pair == 0 ? "temporal_minus_whole" : Pair == 1 ? "spatial_minus_whole" : "spatial_minus_temporal",
                        Paired[Pair].Mean(), Error, unsigned(Improves));
        }
        for (uint32_t Method = 0; Method < 2; ++Method)
            std::printf("ABLATION_WARM condition=%u method=%s submissions=%u queries=%llu accepted=%llu spatial_links=%llu\n",
                        Condition, Method == 0 ? "temporal" : "spatial", Batches * WarmSteps,
                        static_cast<unsigned long long>(WarmQueries[Method]), static_cast<unsigned long long>(WarmAccepted[Method]),
                        static_cast<unsigned long long>(WarmLinks[Method]));
        std::printf("ABLATION_FRESH condition=%u maximum_difference=%.9g matched_queries=1\n", Condition, MaximumFreshDifference);
    }
    std::printf("ABLATION_DECISION correctness=%u temporal_quality=%u spatial_quality=%u promote=0 physical_gpu_timing=0\n",
                unsigned(Correctness), unsigned(Quality[0]), unsigned(Quality[1]));
    Require(Correctness, "Reuse ablation failed fixed mean gates");
}
}
