/*==========================================================================================================================================
                                                   REPLAYCORRELATIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Isolate fresh D/I seed correlation and compare fixed Fresh6 against temporal-only reuse at measured cost.
#pragma once

namespace
{
struct ReplayCorrelationMoments
{
    double Direct = 0, Indirect = 0, DirectSquared = 0, IndirectSquared = 0, Product = 0;
    void Accumulate(double D, double I)
    {
        Direct += D; Indirect += I; DirectSquared += D * D; IndirectSquared += I * I; Product += D * I;
    }
};

void ValidateReplayCorrelation(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, WarmSteps = 8;
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    std::filesystem::create_directories(Directory);
    bool Correctness = true;
    for (uint32_t Condition = 0; Condition < 2; ++Condition)
    {
        const auto Previous = Condition == 0 ? ReplayCameraPlane(0, 0) : ReplaySplitRoom();
        const auto Current = Condition == 0 ? ReplayCameraPlane(.03125f, 0) : ReplaySplitRoom();
        const auto Facets = ReplayConvergenceFacets(Current);
        std::vector<size_t> Sources;
        for (size_t Index = 0; Index < Facets.size(); ++Index)
            if (Facets[Index].Radiance[0] > 0) Sources.push_back(Index);
        Require(!Sources.empty(), "Correlation fixture has no emissive sources");
        LightComputeSubmission Whole(Driver), Split(Driver), Temporal(Driver);
        Whole.Prepare(Previous, Shaders + "/whole", false, false, false, false, true, false, true);
        Split.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
        Temporal.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
        ReplayFreshBudgetPipeline WholeAccess(Whole, Shaders + "/whole");
        ReplayFreshBudgetPipeline SixAccess(Whole, Shaders + "/six");
        Whole.ReplayGeneratePipeline = WholeAccess.Fresh;
        const auto Legacy = ExecuteFirstHopMIS(Split, Current, 319841, true, false, 4, true, .01f, Environment);
        std::vector<std::array<float, 4>> Slabs;
        for (uint32_t Pixel = 0; Pixel < Split.ReceiverCount; ++Pixel)
            for (uint32_t Slot : {0u, 1u, 2u, 3u, 4u, 11u, 12u, 13u, 14u})
                Slabs.push_back(ReplaySampleSlot(Split, Slot, Pixel));
        Split.ReplayIndependentIndirectSeedForAudit = true;
        Split.ReplayIndirectFrameSeedForAudit = 319841;
        const auto Matched = ExecuteFirstHopMIS(Split, Current, 319841, true, false, 4, true, .01f, Environment);
        Require(ReplayCandidateImagesEqual(Legacy, Matched), "Matched seed override changed output bits");
        size_t SlabIndex = 0;
        for (uint32_t Pixel = 0; Pixel < Split.ReceiverCount; ++Pixel)
            for (uint32_t Slot : {0u, 1u, 2u, 3u, 4u, 11u, 12u, 13u, 14u})
            {
                const auto Actual = ReplaySampleSlot(Split, Slot, Pixel);
                Require(std::memcmp(Actual.data(), Slabs[SlabIndex++].data(), sizeof(Actual)) == 0,
                        "Matched seed override changed history bits");
            }
        const auto OneA = ExecuteFirstHopMIS(Split, Current, 319841, true, false, 1, true, .01f, Environment);
        Split.ReplayIndirectFrameSeedForAudit = 857219;
        const auto OneB = ExecuteFirstHopMIS(Split, Current, 319841, true, false, 1, true, .01f, Environment);
        Require(ReplayCandidateImagesEqual(OneA, OneB), "Independent indirect seed changed one-bounce direct output");
        std::printf("CORRELATION_PREFLIGHT condition=%u matched_output_nine_slabs_exact=1 one_bounce_direct_exact=1 "
                    "device=%s type=%u\n", Condition, Whole.Properties.deviceName, unsigned(Whole.Properties.deviceType));
        std::fflush(stdout);
        std::mt19937_64 Seeds(0x7b2e6139ull + Condition * 0x1000193ull);
        std::mt19937_64 IndirectSeeds(0x15ce49a7ull + Condition * 0x1000193ull);
        std::array<DirectGpuMoments, 5> Means;
        std::array<double, 5> Squared{};
        std::array<uint64_t, 5> Queries{}, Nearest{}, Shadow{}, Calls{};
        std::array<DirectGpuMoments, 4> Paired;
        uint64_t WarmQueries = 0, Accepted = 0, IndependentClosest = 0;
        std::vector<uint32_t> Pixels;
        std::vector<double> References;
        std::array<std::vector<ReplayCorrelationMoments>, 2> Components;
        double ReferenceMean = 0, ReferenceVariance = 0, MaximumFresh = 0, MaximumIndependent = 0, MaximumDirect = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            {
                (void)ExecuteFirstHopMIS(Temporal, Previous, uint32_t(Seeds()), Step == 0, false, 4, true, .01f, Environment);
                ValidateSplitCalls(Temporal);
                const auto Counts = Temporal.ExtendedStatistics();
                Require(Counts[22] == 0, "Temporal-only warm-up used spatial links");
                WarmQueries += ReplayAblationQueries(Counts);
            }
            const uint32_t Seed = uint32_t(Seeds()), IndirectSeed = uint32_t(IndirectSeeds());
            Whole.ReplayGeneratePipeline = WholeAccess.Fresh;
            const auto A = ExecuteFirstHopWhole(WholeAccess, Current, Seed, Environment, true);
            const auto CountA = Whole.ExtendedStatistics();
            Split.ReplayIndirectFrameSeedForAudit = Seed;
            const auto B = ExecuteFirstHopMIS(Split, Current, Seed, true, false, 4, true, .01f, Environment);
            const auto CountB = Split.ExtendedStatistics();
            std::vector<float> CoupledIndirect(Split.ReceiverCount);
            for (uint32_t Pixel = 0; Pixel < Split.ReceiverCount; ++Pixel)
                CoupledIndirect[Pixel] = ReplaySampleSlot(Split, 3, Pixel)[0];
            Split.ReplayIndirectFrameSeedForAudit = IndirectSeed;
            const auto C = ExecuteFirstHopMIS(Split, Current, Seed, true, false, 4, true, .01f, Environment);
            const auto CountC = Split.ExtendedStatistics();
            std::vector<float> IndependentIndirect(Split.ReceiverCount);
            for (uint32_t Pixel = 0; Pixel < Split.ReceiverCount; ++Pixel)
                IndependentIndirect[Pixel] = ReplaySampleSlot(Split, 3, Pixel)[0];
            Whole.ReplayGeneratePipeline = SixAccess.Fresh;
            const auto D = ExecuteFirstHopWhole(SixAccess, Current, Seed, Environment, true);
            const auto CountD = Whole.ExtendedStatistics();
            Whole.ReplayGeneratePipeline = WholeAccess.Fresh;
            const auto E = ExecuteFirstHopMIS(Temporal, Current, Seed, false, false, 4, true, .01f, Environment);
            const auto CountE = Temporal.ExtendedStatistics();
            ValidateSplitCalls(Temporal);
            Require(CountE[22] == 0 && CountB[20] == 0 && CountC[20] == 0, "Correlation controls consumed unintended history");
            Accepted += CountE[20];
            Require(CountD[16] * 8u == CountA[16] * 6u, "Whole6 did not trace exactly six paths per eligible pixel");
            Require(ReplayAblationQueries(CountA) == ReplayAblationQueries(CountB), "Coupled split changed query budget");
            Require(CountB[12] == CountC[12] && CountB[13] == CountC[13] && CountB[14] == CountC[14],
                    "Independent indirect seed changed direct work");
            Require(CountC[17] >= CountB[13], "Independent stream did not trace first hops");
            IndependentClosest += CountC[17];
            for (size_t Pixel = 0; Pixel < A.size(); ++Pixel)
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                {
                    const double Difference = std::abs(double(A[Pixel][Channel]) - B[Pixel][Channel]);
                    MaximumFresh = std::max(MaximumFresh, Difference);
                    Require(Difference <= 4e-6 * (1 + std::abs(A[Pixel][Channel])), "Coupled split changed whole radiance");
                    MaximumIndependent = std::max(MaximumIndependent,
                        std::abs(double(B[Pixel][Channel]) - C[Pixel][Channel]));
                }
            const std::array<std::array<uint32_t, 24>, 5> Counts{CountA, CountB, CountC, CountD, CountE};
            for (uint32_t Method = 0; Method < 5; ++Method)
            {
                Queries[Method] += ReplayAblationQueries(Counts[Method]);
                Nearest[Method] += uint64_t(Counts[Method][13]) + Counts[Method][17];
                Shadow[Method] += uint64_t(Counts[Method][14]) + Counts[Method][2];
                Calls[Method] += uint64_t(Counts[Method][12]) + Counts[Method][16];
            }
            if (Batch == 0)
            {
                const auto Guides = Split.PrimaryReceivers();
                const uint32_t Samples = Condition == 0 ? 4096u : 2048u;
                for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                {
                    const auto& R = Guides[Pixel];
                    if (R.PositionHit[3] == 0 || R.MotionPositionToken[3] <= 0 ||
                        std::max({R.AlbedoIor[0], R.AlbedoIor[1], R.AlbedoIor[2]}) <= 0) continue;
                    Require(R.ResidualSpecular[0] == 0, "D/I decomposition requires zero primary residual on measured points");
                    Pixels.push_back(Pixel);
                    std::mt19937_64 Random(0x46d9a173ull + Condition * 0x1000193ull +
                                           uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                    DirectGpuMoments Cpu;
                    for (uint32_t Sample = 0; Sample < Samples; ++Sample)
                        Cpu.Accumulate(ReplayConvergencePath(Facets, Sources, R, 4, Random, true));
                    References.push_back(Cpu.Mean()); ReferenceMean += Cpu.Mean();
                    const double Error = Cpu.StandardError() * std::sqrt(double(Samples) / (Samples - 1));
                    ReferenceVariance += Error * Error;
                }
                Require(Pixels.size() == (Condition == 0 ? 1024u : 528u), "Correlation measured mask changed");
                for (auto& Arm : Components) Arm.resize(Pixels.size());
                ReferenceMean /= Pixels.size();
                const std::array<const std::vector<std::array<float, 4>>*, 5> Images{&A, &B, &C, &D, &E};
                for (uint32_t Method = 0; Method < 5; ++Method)
                    WriteImage(Directory + "/Correlation" + std::to_string(Condition) + std::to_string(Method) + ".ppm",
                               *Images[Method], Current.Width, Current.Height, 3);
                std::printf("CORRELATION_REFERENCE condition=%u points=%zu paths=%zu mean=%.9g nominal_se=%.9g\n", Condition,
                            Pixels.size(), Pixels.size() * Samples, ReferenceMean, std::sqrt(ReferenceVariance) / Pixels.size());
            }
            const std::array<const std::vector<std::array<float, 4>>*, 5> Images{&A, &B, &C, &D, &E};
            std::array<double, 5> Sum{}, Errors{};
            for (size_t Index = 0; Index < Pixels.size(); ++Index)
            {
                const uint32_t Pixel = Pixels[Index];
                const double I0 = CoupledIndirect[Pixel], I1 = IndependentIndirect[Pixel];
                const double D0 = double(B[Pixel][0]) - I0, D1 = double(C[Pixel][0]) - I1;
                MaximumDirect = std::max(MaximumDirect, std::abs(D0 - D1));
                Require(std::abs(D0 - D1) < 4e-6 * (1 + std::abs(D0)), "Independent indirect seed changed reconstructed direct term");
                Components[0][Index].Accumulate(D0, I0); Components[1][Index].Accumulate(D1, I1);
                for (uint32_t Method = 0; Method < 5; ++Method)
                {
                    const double Red = (*Images[Method])[Pixel][0];
                    Require(std::isfinite(Red) && Red >= 0, "Correlation radiance is invalid");
                    Sum[Method] += Red;
                    const double Error = Red - References[Index]; Errors[Method] += Error * Error;
                }
            }
            for (uint32_t Method = 0; Method < 5; ++Method)
            { Means[Method].Accumulate(Sum[Method] / Pixels.size()); Squared[Method] += Errors[Method]; }
            Paired[0].Accumulate((Errors[2] - Errors[1]) / Pixels.size());
            Paired[1].Accumulate((Errors[4] - Errors[3]) / Pixels.size());
            Paired[2].Accumulate((Errors[4] - Errors[0]) / Pixels.size());
            Paired[3].Accumulate((Errors[4] - Errors[2]) / Pixels.size());
            if ((Batch + 1) % 8 == 0) std::printf("CORRELATION_PROGRESS condition=%u completed=%u/%u\n", Condition, Batch + 1, Batches);
            std::fflush(stdout);
        }
        Require(Accepted > 0 && MaximumIndependent > 1e-7 && IndependentClosest > 0, "Correlation feature control was inactive");
        const double Correction = std::sqrt(double(Batches) / (Batches - 1));
        for (uint32_t Method = 0; Method < 5; ++Method)
        {
            const double Error = Means[Method].StandardError() * Correction;
            const bool Agrees = std::abs(Means[Method].Mean() - ReferenceMean) <
                4 * std::hypot(Error, std::sqrt(ReferenceVariance) / Pixels.size()) + .001;
            Correctness = Correctness && Agrees;
            std::printf("CORRELATION_RESULT condition=%u method=%s mean=%.9g nominal_se=%.9g rmse=%.9g mean_gate=%u "
                        "queries=%llu nearest=%llu shadow=%llu calls=%llu\n", Condition,
                        Method == 0 ? "whole8" : Method == 1 ? "coupled8" : Method == 2 ? "independent8" : Method == 3 ? "whole6" : "temporal",
                        Means[Method].Mean(), Error, std::sqrt(Squared[Method] / (Batches * Pixels.size())), unsigned(Agrees),
                        static_cast<unsigned long long>(Queries[Method]), static_cast<unsigned long long>(Nearest[Method]),
                        static_cast<unsigned long long>(Shadow[Method]), static_cast<unsigned long long>(Calls[Method]));
        }
        for (uint32_t Arm = 0; Arm < 2; ++Arm)
        {
            double VD = 0, VI = 0, Covariance = 0, BiasSquared = 0;
            for (size_t Index = 0; Index < Pixels.size(); ++Index)
            {
                const auto& M = Components[Arm][Index];
                const double D = M.Direct / Batches, I = M.Indirect / Batches;
                VD += M.DirectSquared / Batches - D * D; VI += M.IndirectSquared / Batches - I * I;
                Covariance += M.Product / Batches - D * I;
                const double Error = D + I - References[Index]; BiasSquared += Error * Error;
            }
            VD /= Pixels.size(); VI /= Pixels.size(); Covariance /= Pixels.size(); BiasSquared /= Pixels.size();
            const double Mse = VD + VI + 2 * Covariance + BiasSquared;
            Require(std::abs(Mse - Squared[Arm + 1] / (Batches * Pixels.size())) < 1e-9, "D/I covariance does not rederive MSE");
            std::printf("CORRELATION_COMPONENTS condition=%u arm=%s direct_variance=%.9g indirect_variance=%.9g "
                        "covariance=%.9g mean_error_squared=%.9g rederived_mse=%.9g\n", Condition,
                        Arm == 0 ? "coupled" : "independent", VD, VI, Covariance, BiasSquared, Mse);
        }
        for (uint32_t Pair = 0; Pair < 4; ++Pair)
            std::printf("CORRELATION_PAIRED condition=%u pair=%s mse_difference=%.9g nominal_se=%.9g\n", Condition,
                        Pair == 0 ? "independent_minus_coupled" : Pair == 1 ? "temporal_minus_whole6" :
                        Pair == 2 ? "temporal_minus_whole8" : "temporal_minus_independent",
                        Paired[Pair].Mean(), Paired[Pair].StandardError() * Correction);
        std::printf("CORRELATION_CHECKS condition=%u maximum_fresh_difference=%.9g maximum_independent_difference=%.9g "
                    "maximum_direct_difference=%.9g temporal_accepted=%llu spatial_links=0 warm_submissions=%u warm_queries=%llu\n",
                    Condition, MaximumFresh, MaximumIndependent, MaximumDirect, static_cast<unsigned long long>(Accepted),
                    Batches * WarmSteps, static_cast<unsigned long long>(WarmQueries));
        std::fflush(stdout);
    }
    std::printf("CORRELATION_DECISION correctness=%u promote=0 physical_gpu_timing=0\n", unsigned(Correctness));
    Require(Correctness, "Correlation experiment failed fixed mean/reference gates");
}
}
