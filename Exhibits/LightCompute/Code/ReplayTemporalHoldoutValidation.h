/*==========================================================================================================================================
                                               REPLAYTEMPORALHOLDOUTVALIDATION.H
==========================================================================================================================================*/
// 🧩 Measure fixed emitter-enabled temporal weighting on independent camera, blocker and diffuse-albedo conditions.

#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                 INDEPENDENT QUALITY CONDITIONS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayTemporalHoldout(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, WarmSteps = 8, CpuSamples = 4096;
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    std::filesystem::create_directories(Directory);
    ValidateFreshBudgetRectangle(Driver, Shaders);
    ValidateBoundedTemporalGuards(Driver, Shaders);
    ValidateBoundedEvaluationCache(Driver, Shaders);
    LightComputeSubmission Fresh(Driver), Temporal(Driver);
    const auto Initial = ReplayCameraPlane(0, 0);
    Fresh.Prepare(Initial, Shaders, false, false, false, false, true, false, true);
    Temporal.Prepare(Initial, Shaders, false, false, false, false, true, false, true);
    ReplayFreshBudgetPipeline FreshAccess(Fresh, Shaders);
    bool Correctness = true, Promotion = true;
    std::printf("HOLDOUT_DEVICE name=%s type=%u output_bytes=%llu\n", Temporal.Properties.deviceName,
                unsigned(Temporal.Properties.deviceType),
                static_cast<unsigned long long>(Temporal.ReplayOutputDescriptorBytes));
    for (uint32_t Condition = 0; Condition < 4; ++Condition)
    {
        auto Previous = ReplayCameraPlane(0, 0);
        auto Current = ReplayCameraPlane(Condition == 1 ? .125f : Condition == 2 ? .0625f : 0.f,
                                         0, Condition == 2 ? .25f : 0.f);
        if (Condition == 3)
        {
            for (auto* Scene : {&Previous, &Current})
                for (auto& Facet : Scene->Surfaces)
                    if (Facet.OpticalToken[3] == 1)
                        Facet.AlbedoRoughness = {.8f, .8f, .8f, 1};
        }
        std::mt19937_64 Seeds(0x6d248193ull + Condition * 0x1000193ull);
        std::vector<uint32_t> Pixels, Exposed;
        std::vector<double> Reference;
        std::array<DirectGpuMoments, 2> Means;
        std::array<double, 2> Squared{}, ExposedSquared{};
        DirectGpuMoments PairedMean, PairedMse;
        double ReferenceMean = 0, ReferenceVariance = 0;
        uint64_t FreshCalls = 0, TemporalCalls = 0, WarmCalls = 0, CachedCalls = 0;
        uint64_t FreshQueries = 0, TemporalQueries = 0, WarmQueries = 0;
        uint64_t Accepted = 0, Banks = 0, Rejected = 0;
        uint32_t Resets = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            for (uint32_t Step = 0; Step < WarmSteps; ++Step)
            {
                (void)ExecuteReplaySeedReservoir(Temporal, Previous, uint32_t(Seeds()), Step == 0,
                                                 true, 4, true, .01f, Environment);
                WarmCalls += ValidateBoundedPathCount(Temporal, false);
                WarmQueries += Temporal.ExtendedStatistics()[17] + Temporal.ExtendedStatistics()[2];
            }
            const auto PriorGuides = Temporal.PrimaryReceivers();
            const uint32_t Seed = uint32_t(Seeds());
            const auto FreshImage = ExecuteFreshBudget(FreshAccess, Current, Seed, 4, Environment, true);
            const auto TemporalImage = ExecuteReplaySeedReservoir(Temporal, Current, Seed, false,
                                                                  true, 4, true, .01f, Environment);
            const auto Counts = Temporal.ExtendedStatistics();
            FreshCalls += ValidateBoundedPathCount(Fresh, true);
            TemporalCalls += ValidateBoundedPathCount(Temporal, false);
            CachedCalls += Counts[15];
            FreshQueries += Fresh.ExtendedStatistics()[17] + Fresh.ExtendedStatistics()[2];
            TemporalQueries += Counts[17] + Counts[2];
            Accepted += Counts[20];
            Rejected += Counts[21];
            Banks += Counts[23];
            Resets += Temporal.ReplayLastSampleHistoryReset ? 1u : 0u;
            const auto Guides = Temporal.PrimaryReceivers();
            if (Batch == 0)
            {
                std::vector<std::array<float, 4>> Mask(Guides.size(), {.5f, .02f, .02f, 1});
                for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                {
                    const auto& Receiver = Guides[Pixel];
                    if (Receiver.PositionHit[3] == 0 || Receiver.MotionPositionToken[3] != 1) continue;
                    Pixels.push_back(Pixel);
                    Mask[Pixel] = {.05f, 1, .05f, 1};
                    const int32_t X = int32_t(std::floor(Receiver.MotionPixel[0] + .5f));
                    const int32_t Y = int32_t(std::floor(Receiver.MotionPixel[1] + .5f));
                    if (Receiver.MotionPixel[3] != 0 && X >= 0 && Y >= 0 &&
                        X < int32_t(Current.Width) && Y < int32_t(Current.Height) &&
                        PriorGuides[uint32_t(Y) * Current.Width + uint32_t(X)].MotionPositionToken[3] != 1)
                    {
                        Exposed.push_back(Pixel);
                        Mask[Pixel] = {.05f, .5f, 1, 1};
                    }
                    std::mt19937_64 Random(0x95e24ab3ull + Condition * 0x1000193ull +
                                           uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                    DirectGpuMoments Cpu;
                    for (uint32_t Sample = 0; Sample < CpuSamples; ++Sample)
                        Cpu.Accumulate(ReplayReferencePath(Current, Receiver, 4, Random)[0]);
                    Reference.push_back(Cpu.Mean());
                    ReferenceMean += Cpu.Mean();
                    const double Error = Cpu.StandardError() * std::sqrt(double(CpuSamples) / (CpuSamples - 1));
                    ReferenceVariance += Error * Error;
                }
                Require(Pixels.size() > 500, "Holdout geometric plane mask is too small");
                Require(Condition != 2 || !Exposed.empty(), "Moving blocker did not expose any measured plane pixels");
                ReferenceMean /= Pixels.size();
                const std::string Prefix = Directory + "/Holdout" + std::to_string(Condition);
                WriteImage(Prefix + "Fresh.ppm", FreshImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Temporal.ppm", TemporalImage, Current.Width, Current.Height, 3);
                WriteImage(Prefix + "Mask.ppm", Mask, Current.Width, Current.Height, 3);
                std::printf("HOLDOUT_REFERENCE condition=%u points=%zu exposed=%zu cpu_paths=%zu mean=%.9g nominal_se=%.9g\n",
                            Condition, Pixels.size(), Exposed.size(), Pixels.size() * CpuSamples,
                            ReferenceMean, std::sqrt(ReferenceVariance) / Pixels.size());
                std::fflush(stdout);
            }
            std::array<double, 2> Sum{}, BatchSquared{};
            for (size_t Index = 0; Index < Pixels.size(); ++Index)
            {
                const uint32_t Pixel = Pixels[Index];
                Require(Guides[Pixel].MotionPositionToken[3] == 1, "Holdout geometric mask changed across seed batches");
                const bool NewlyExposed = std::find(Exposed.begin(), Exposed.end(), Pixel) != Exposed.end();
                if (NewlyExposed)
                {
                    const float Local = ReplaySampleSlot(Temporal, 14, Pixel)[2] + 1;
                    Require(ReplaySampleSlot(Temporal, 2, Pixel)[3] == Local,
                            "Cross-token selected history admitted at a newly exposed point");
                }
                for (uint32_t Method = 0; Method < 2; ++Method)
                {
                    const double Red = Method == 0 ? FreshImage[Pixel][0] : TemporalImage[Pixel][0];
                    Require(std::isfinite(Red) && Red >= 0, "Holdout radiance is invalid");
                    const double Error = Red - Reference[Index];
                    Sum[Method] += Red;
                    BatchSquared[Method] += Error * Error;
                    if (NewlyExposed) ExposedSquared[Method] += Error * Error;
                }
            }
            for (uint32_t Method = 0; Method < 2; ++Method)
            {
                Means[Method].Accumulate(Sum[Method] / Pixels.size());
                Squared[Method] += BatchSquared[Method];
            }
            PairedMean.Accumulate((Sum[1] - Sum[0]) / Pixels.size());
            PairedMse.Accumulate((BatchSquared[1] - BatchSquared[0]) / Pixels.size());
            if ((Batch + 1) % 8 == 0)
            {
                std::printf("HOLDOUT_PROGRESS condition=%u completed=%u/%u\n", Condition, Batch + 1, Batches);
                std::fflush(stdout);
            }
        }
        const double Correction = std::sqrt(double(Batches) / (Batches - 1));
        for (uint32_t Method = 0; Method < 2; ++Method)
        {
            const double Error = Means[Method].StandardError() * Correction;
            const bool MeanAgrees = std::abs(Means[Method].Mean() - ReferenceMean) <
                4 * std::hypot(Error, std::sqrt(ReferenceVariance) / Pixels.size()) + .001;
            Correctness = Correctness && MeanAgrees;
            std::printf("HOLDOUT_RESULT condition=%u method=%s batches=%u points=%zu mean=%.9g nominal_se=%.9g "
                        "rmse=%.9g exposed_rmse=%.9g mean_gate=%u\n", Condition,
                        Method == 0 ? "fresh_eight" : "bounded_temporal", Batches, Pixels.size(),
                        Means[Method].Mean(), Error, std::sqrt(Squared[Method] / (Batches * Pixels.size())),
                        Exposed.empty() ? 0 : std::sqrt(ExposedSquared[Method] / (Batches * Exposed.size())),
                        unsigned(MeanAgrees));
        }
        const double MeanError = PairedMean.StandardError() * Correction;
        const double MseError = PairedMse.StandardError() * Correction;
        Correctness = Correctness && std::abs(PairedMean.Mean()) < 4 * MeanError + .001;
        const bool Quality = PairedMse.Mean() + 3 * MseError < 0;
        Promotion = Promotion && Quality;
        std::printf("HOLDOUT_PAIRED condition=%u mean_difference=%.9g nominal_mean_se=%.9g "
                    "mse_difference=%.9g nominal_mse_se=%.9g quality_gate=%u\n", Condition,
                    PairedMean.Mean(), MeanError, PairedMse.Mean(), MseError, unsigned(Quality));
        std::printf("HOLDOUT_WORK condition=%u fresh_calls=%llu temporal_calls=%llu warm_calls=%llu cache_hits=%llu "
                    "fresh_queries=%llu temporal_queries=%llu warm_queries=%llu bank_resets=%u "
                    "accepted=%llu rejected=%llu banks=%llu\n", Condition,
                    static_cast<unsigned long long>(FreshCalls), static_cast<unsigned long long>(TemporalCalls),
                    static_cast<unsigned long long>(WarmCalls), static_cast<unsigned long long>(CachedCalls),
                    static_cast<unsigned long long>(FreshQueries), static_cast<unsigned long long>(TemporalQueries),
                    static_cast<unsigned long long>(WarmQueries), Resets,
                    static_cast<unsigned long long>(Accepted), static_cast<unsigned long long>(Rejected),
                    static_cast<unsigned long long>(Banks));
        std::fflush(stdout);
    }
    std::printf("HOLDOUT_DECISION correctness=%u promote=%u physical_gpu_timing=0\n",
                unsigned(Correctness), unsigned(Correctness && Promotion));
    Require(Correctness, "Emitter-enabled holdout failed its preregistered mean gates");
}
} // namespace
