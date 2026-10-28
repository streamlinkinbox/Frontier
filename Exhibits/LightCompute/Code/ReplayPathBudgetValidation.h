/*==========================================================================================================================================
                                              REPLAYPATHBUDGETVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare eight fresh current paths with current-surface spatial replay at a verified eight-call measurement mask.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                         VALIDATION-ONLY CURRENT-PATH CONTROL
//------------------------------------------------------------------------------------------------------------------------

struct ReplayFreshBudgetPipeline
{
    LightComputeSubmission& Compute;
    VkPipeline OriginalGeneration = VK_NULL_HANDLE;
    VkPipeline Fresh              = VK_NULL_HANDLE;

    ReplayFreshBudgetPipeline(LightComputeSubmission& Submission, const std::string& Shaders) : Compute(Submission)
    {
        Require(Compute.CompactReplayStorage && Compute.ReplayGeneratePipeline,
                "Fresh budget control requires prepared compact replay storage");
        OriginalGeneration = Compute.ReplayGeneratePipeline;
        Fresh = Compute.ConstructPipeline(Shaders + "/ReplayFreshBudget.spv");
        Compute.ReplayGeneratePipeline = Fresh;
    }
    ~ReplayFreshBudgetPipeline()
    {
        Compute.DeviceWaitIdle(Compute.Device);
        Compute.ReplayGeneratePipeline = OriginalGeneration;
        Compute.DestroyPipeline(Compute.Device, Fresh, nullptr);
    }
    ReplayFreshBudgetPipeline(const ReplayFreshBudgetPipeline&) = delete;
    ReplayFreshBudgetPipeline& operator=(const ReplayFreshBudgetPipeline&) = delete;
};

std::vector<std::array<float,4>> ExecuteFreshBudget(
    ReplayFreshBudgetPipeline& ControlPipeline,
    const LightComputeArchive& Scene,
    uint32_t                   Seed,
    uint32_t                   Bounces,
    const std::array<float,3>&  Environment,
    bool                       Facets)
{
    // 📝 Always cold, no spatial/temporal selection, no bank contribution. Only generation is replaced.
    const auto Image = ExecuteReplaySampleHistory(ControlPipeline.Compute, Scene, Seed, true, true, Bounces,
        Environment, 1, false, false, false, false, false, true, Facets);
    Require(ControlPipeline.Compute.ReplayLastSampleHistoryReset && ControlPipeline.Compute.ExtendedStatistics()[23] == 0 &&
        ControlPipeline.Compute.ExtendedStatistics()[20] == 0 && ControlPipeline.Compute.ExtendedStatistics()[22] == 0,
        "Fresh budget control unexpectedly consumed history or reused reservoirs");
    return Image;
}

uint64_t ValidateBudgetPathCount(const LightComputeSubmission& Compute, bool Fresh)
{
    uint64_t Expected = 0;
    for (uint32_t Pixel = 0; Pixel < Compute.ReceiverCount; ++Pixel)
    {
        const float Count = ReplaySampleSlot(Compute, 2, Pixel)[3];
        Require(Count >= 0 && Count <= 4 && std::floor(Count) == Count, "Invalid represented candidate count");
        Expected += Fresh ? (Count > 0 ? 8u : 0u) : uint32_t(Count);
    }
    if (!Fresh) Expected += Compute.ExtendedStatistics()[22];
    Require(Expected == Compute.ExtendedStatistics()[16], "Measured path calls differ from generated/replayed work");
    return Expected;
}

bool ValidateBudgetPixelEightCalls(const LightComputeSubmission& Compute, uint32_t Pixel)
{
    const uint32_t Width = Compute.ReceiverWidth, Height = Compute.ReceiverHeight;
    const uint32_t X = Pixel % Width, Y = Pixel / Width;
    if (X == 0 || Y == 0 || X + 1 >= Width || Y + 1 >= Height) return false;
    if (ReplaySampleSlot(Compute, 2, Pixel)[3] != 4) return false;
    for (uint32_t Donor : {Pixel - 1, Pixel + 1, Pixel - Width, Pixel + Width})
        if (ReplaySampleSlot(Compute, 2, Donor)[3] <= 0) return false;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           FRESH-CONTROL RECTANGLE INTERSECTIONS
//------------------------------------------------------------------------------------------------------------------------

void ValidateFreshBudgetRectangle(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayCameraPlane(.03125f, 0);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true, false, true);
    ReplayFreshBudgetPipeline ControlPipeline(Compute, Shaders);
    constexpr uint32_t Seed = 191111;
    const auto Image = ExecuteFreshBudget(ControlPipeline, Scene, Seed, 1, {0,0,0}, false);
    const auto Guides = Compute.PrimaryReceivers();
    uint32_t Compared = 0, Excluded = 0;
    double Maximum = 0;
    for (uint32_t Pixel = 0; Pixel < Image.size(); ++Pixel)
    {
        if (Guides[Pixel].MotionPositionToken[3] != 1) continue;
        uint32_t Bits = Seed ^ (Pixel * 747796405u + 2891336453u);
        float Total = 0;
        bool Boundary = false;
        for (uint32_t Path = 0; Path < 8; ++Path)
        {
            const auto Intersection = ReplaySamplePlaneOracle(Guides[Pixel], NextReplayTailRandom(Bits));
            Total += Intersection.Radiance;
            Boundary = Boundary || Intersection.Boundary;
        }
        if (Boundary) { ++Excluded; continue; }
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
            Maximum = std::max(Maximum, std::abs(double(Image[Pixel][Channel]) - Total / 8));
        ++Compared;
    }
    const auto Calls = ValidateBudgetPathCount(Compute, true);
    Require(Compute.ExtendedStatistics()[17] == Calls && Compute.ExtendedStatistics()[2] == 0,
            "Single-bounce closest-query counting disagrees with path calls");
    Require(Compared > 500 && Maximum < 1e-6 && Calls == 8 * Scene.Receivers.size(),
            "Eight-fresh GPU average disagrees with independent rectangle intersections");
    // 📝 Poison every old output/history slab. FreshImage cold fresh control must be unaffected, bit for bit.
    std::memset(Compute.Buffers[6].Address, 0xff, Compute.Buffers[6].Size);
    const auto Poisoned = ExecuteFreshBudget(ControlPipeline, Scene, Seed, 1, {0,0,0}, false);
    Require(ReplayCandidateImagesEqual(Image, Poisoned), "Fresh control read poisoned old output/history");
    std::printf("BUDGET_FRESH_RECTANGLE compared=%u edge_excluded=%u max_rgb_error=%.9g calls=%llu "
                "paths_each=8 old_history_poison_exact=1\n", Compared, Excluded, Maximum,
                static_cast<unsigned long long>(Calls));
    std::fflush(stdout);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           INSTRUMENTATION IMAGE PARITY
//------------------------------------------------------------------------------------------------------------------------

void ValidateBudgetInstrumentation(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    LightComputeSubmission Instrumented(Driver), Plain(Driver);
    Instrumented.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    Plain.Prepare(Previous, Shaders + "/plain", false, false, false, false, true, false, true);
    for (uint32_t Index = 0; Index < 4; ++Index)
    {
        const auto& Scene = Index == 3 ? Current : Previous;
        const uint32_t Seed = 340001u + Index * 7919u;
        const auto CountedImage = ExecuteReplaySeedReservoir(Instrumented, Scene, Seed, Index == 0, true, 4, true);
        const auto PlainImage = ExecuteReplaySeedReservoir(Plain, Scene, Seed, Index == 0, true, 4, true);
        Require(ReplayCandidateImagesEqual(CountedImage, PlainImage), "Path counting changed rendered output");
        auto Counts = Instrumented.ExtendedStatistics();
        const auto Uncounted = Plain.ExtendedStatistics();
        Require(Counts[16] > 0 && Uncounted[16] == 0, "Path counting instrumentation was not isolated");
        Require(Counts[17] > 0 && Uncounted[17] == 0, "Trace-query counting was not isolated");
        Counts[16] = 0;
        Counts[17] = 0;
        Require(Counts == Uncounted, "Path counting changed other GPU counters");
        for (uint32_t Pixel = 0; Pixel < CountedImage.size(); ++Pixel)
            for (uint32_t Slot : {0u,1u,2u,3u,4u,11u,12u,13u,14u})
                Require(ReplaySampleSlot(Instrumented, Slot, Pixel) == ReplaySampleSlot(Plain, Slot, Pixel),
                        "Path counting changed a logical reservoir/history slab");
    }
    std::printf("BUDGET_INSTRUMENTATION frames=4 output_and_history_exact=1 other_counters_exact=1\n");
    std::fflush(stdout);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          EQUAL CURRENT-PATH-COUNT COMPARISON
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayPathBudget(const char* Driver, const std::string& Shaders)
{
    ValidateFreshBudgetRectangle(Driver, Shaders);
    ValidateBudgetInstrumentation(Driver, Shaders);
    constexpr uint32_t Batches = 16, CpuSamples = 1024;
    const auto Previous = ReplayCameraPlane(0, 0), Current = ReplayCameraPlane(.03125f, 0);
    LightComputeSubmission Fresh(Driver), Reused(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    Reused.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    ReplayFreshBudgetPipeline FreshAccess(Fresh, Shaders);
    std::vector<uint32_t> Pixels;
    std::array<std::vector<double>,2> References;
    std::array<double,2> ReferenceMean{}, ReferenceSe{};
    std::printf("BUDGET_DEVICE name=%s type=%u api=%u compact_output_bytes=%llu\n",
        Reused.Properties.deviceName, uint32_t(Reused.Properties.deviceType), Reused.Properties.apiVersion,
        static_cast<unsigned long long>(Reused.ReplayOutputDescriptorBytes));
    std::fflush(stdout);
    for (uint32_t BounceIndex = 0; BounceIndex < 2; ++BounceIndex)
        for (bool Facets : {false, true})
        {
            const uint32_t Bounces = BounceIndex == 0 ? 1 : 4;
            const float Ambient = Bounces == 1 ? 0.f : .025f;
            const std::array<float,3> Environment{Ambient, Ambient, Ambient};
            std::mt19937_64 Seeds(0xb51d4a79ull);
            std::array<DirectGpuMoments,2> Means;
            DirectGpuMoments PairedMean, PairedMse;
            std::array<double,2> Squared{};
            uint64_t FreshCalls = 0, ReusedCalls = 0, WarmCalls = 0;
            uint64_t FreshQueries = 0, ReusedQueries = 0, WarmQueries = 0;
            for (uint32_t Batch = 0; Batch < Batches; ++Batch)
            {
                for (uint32_t Index = 0; Index < 3; ++Index)
                {
                    (void)ExecuteReplaySeedReservoir(Reused, Previous, uint32_t(Seeds()), Index == 0,
                                                     true, Bounces, Facets, .01f, Environment);
                    WarmCalls += ValidateBudgetPathCount(Reused, false);
                    WarmQueries += Reused.ExtendedStatistics()[17] + Reused.ExtendedStatistics()[2];
                }
                const uint32_t Seed = uint32_t(Seeds());
                const auto FreshImage = ExecuteFreshBudget(FreshAccess, Current, Seed, Bounces, Environment, Facets);
                const auto ReusedImage = ExecuteReplaySeedReservoir(Reused, Current, Seed, false, true,
                                                         Bounces, Facets, .01f, Environment);
                FreshCalls += ValidateBudgetPathCount(Fresh, true);
                ReusedCalls += ValidateBudgetPathCount(Reused, false);
                FreshQueries += Fresh.ExtendedStatistics()[17] + Fresh.ExtendedStatistics()[2];
                ReusedQueries += Reused.ExtendedStatistics()[17] + Reused.ExtendedStatistics()[2];
                const auto Guides = Reused.PrimaryReceivers();
                if (Pixels.empty())
                    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                        if (Guides[Pixel].MotionPositionToken[3] == 1 && ValidateBudgetPixelEightCalls(Reused, Pixel))
                            Pixels.push_back(Pixel);
                Require(Pixels.size() > 500, "Equal-budget geometric measurement mask is too small");
                if (References[BounceIndex].empty())
                {
                    double Sum = 0, Variance = 0, QuadratureSquared = 0;
                    for (uint32_t Pixel : Pixels)
                    {
                        double Reference = 0;
                        if (Bounces == 1)
                        {
                            Reference = ReplayCameraPlaneArea(Guides[Pixel], 128);
                            const double Coarse = ReplayCameraPlaneArea(Guides[Pixel], 64);
                            QuadratureSquared += (Reference - Coarse) * (Reference - Coarse);
                        }
                        else
                        {
                            std::mt19937_64 Random(0xe51b1c39ull + uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                            DirectGpuMoments Cpu;
                            for (uint32_t Index = 0; Index < CpuSamples; ++Index)
                                Cpu.Accumulate(ReplayReferencePath(Current, Guides[Pixel], Bounces, Random)[0]);
                            Reference = Cpu.Mean();
                            const double Error = Cpu.StandardError() * std::sqrt(double(CpuSamples) / (CpuSamples - 1));
                            Variance += Error * Error;
                        }
                        References[BounceIndex].push_back(Reference);
                        Sum += Reference;
                    }
                    ReferenceMean[BounceIndex] = Sum / Pixels.size();
                    ReferenceSe[BounceIndex] = std::sqrt(Variance) / Pixels.size();
                    Require(Bounces != 1 || std::sqrt(QuadratureSquared / Pixels.size()) < .002,
                            "Area-integral finite quadrature disagrees");
                    std::printf("BUDGET_REFERENCE bounces=%u points=%zu mean=%.9g nominal_se=%.9g "
                        "cpu_paths=%zu quadrature_rmse=%.9g\n", Bounces, Pixels.size(), ReferenceMean[BounceIndex],
                        ReferenceSe[BounceIndex], Bounces == 4 ? Pixels.size() * CpuSamples : 0,
                        std::sqrt(QuadratureSquared / Pixels.size()));
                    std::fflush(stdout);
                }
                std::array<double,2> BatchSum{}, BatchSquared{};
                for (size_t Index = 0; Index < Pixels.size(); ++Index)
                {
                    const uint32_t Pixel = Pixels[Index];
                    Require(ValidateBudgetPixelEightCalls(Reused, Pixel) && ReplaySampleSlot(Reused, 14, Pixel)[2] == 3,
                            "Measurement pixel lost its eight-current-path contract");
                    for (uint32_t Mode = 0; Mode < 2; ++Mode)
                    {
                        const double Red = Mode == 0 ? FreshImage[Pixel][0] : ReusedImage[Pixel][0];
                        Require(std::isfinite(Red) && Red >= 0, "Invalid equal-budget radiance");
                        const double Error = Red - References[BounceIndex][Index];
                        BatchSum[Mode] += Red;
                        BatchSquared[Mode] += Error * Error;
                    }
                }
                for (uint32_t Mode = 0; Mode < 2; ++Mode)
                {
                    Means[Mode].Accumulate(BatchSum[Mode] / Pixels.size());
                    Squared[Mode] += BatchSquared[Mode];
                }
                PairedMean.Accumulate((BatchSum[1] - BatchSum[0]) / Pixels.size());
                PairedMse.Accumulate((BatchSquared[1] - BatchSquared[0]) / Pixels.size());
                if ((Batch + 1) % 4 == 0)
                {
                    std::printf("BUDGET_PROGRESS bounces=%u facets=%u completed=%u/%u\n",
                                Bounces, unsigned(Facets), Batch + 1, Batches);
                    std::fflush(stdout);
                }
            }
            const double Correction = std::sqrt(double(Batches) / (Batches - 1));
            for (uint32_t Mode = 0; Mode < 2; ++Mode)
            {
                const double Error = Means[Mode].StandardError() * Correction;
                std::printf("BUDGET_RESULT bounces=%u facets=%u mode=%s batches=%u points=%zu "
                    "paths_at_measured_pixel=8 mean=%.9g nominal_batch_se=%.9g rmse=%.9g reference=%.9g\n",
                    Bounces, unsigned(Facets), Mode == 0 ? "fresh_eight" : "spatial_seed_ris", Batches, Pixels.size(),
                    Means[Mode].Mean(), Error, std::sqrt(Squared[Mode] / (Batches * Pixels.size())),
                    ReferenceMean[BounceIndex]);
                Require(std::abs(Means[Mode].Mean() - ReferenceMean[BounceIndex]) <
                    4 * std::hypot(Error, ReferenceSe[BounceIndex]) + .001,
                    "Equal-budget mean disagrees with the independent reference");
            }
            const double DifferenceError = PairedMean.StandardError() * Correction;
            std::printf("BUDGET_PAIRED bounces=%u facets=%u mean_difference=%.9g nominal_mean_se=%.9g "
                "mse_difference=%.9g nominal_mse_se=%.9g\n", Bounces, unsigned(Facets), PairedMean.Mean(),
                DifferenceError, PairedMse.Mean(), PairedMse.StandardError() * Correction);
            Require(std::abs(PairedMean.Mean()) < 4 * DifferenceError + .001,
                    "Equal-budget paired means disagree beyond the declared gate");
            std::printf("BUDGET_WORK bounces=%u facets=%u current_submissions_each=%u warm_submissions_reuse=%u "
                "fresh_current_calls=%llu reuse_current_calls=%llu reuse_warm_calls=%llu "
                "fresh_indirect_queries=%llu reuse_indirect_queries=%llu reuse_warm_indirect_queries=%llu\n",
                Bounces, unsigned(Facets), Batches, Batches * 3,
                static_cast<unsigned long long>(FreshCalls), static_cast<unsigned long long>(ReusedCalls),
                static_cast<unsigned long long>(WarmCalls), static_cast<unsigned long long>(FreshQueries),
                static_cast<unsigned long long>(ReusedQueries), static_cast<unsigned long long>(WarmQueries));
            std::fflush(stdout);
        }
    std::printf("PASS eight-current-path comparison and reference gates; quality advantage is not required to pass\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ACTUAL READBACKS
//------------------------------------------------------------------------------------------------------------------------

void CaptureReplayPathBudget(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Size = 64;
    const auto Previous = ReplayCameraPlane(0, 0, 0, 2, Size);
    const auto Current = ReplayCameraPlane(1.f / Size, 0, 0, 2, Size);
    LightComputeSubmission Fresh(Driver), Reused(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    Reused.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    ReplayFreshBudgetPipeline ControlPipeline(Fresh, Shaders);
    std::filesystem::create_directories(Directory);
    for (bool Facets : {false, true})
    {
        uint64_t Warm = 0;
        for (uint32_t Index = 0; Index < 3; ++Index)
        {
            (void)ExecuteReplaySeedReservoir(Reused, Previous, 340001u + Index * 7919u, Index == 0,
                                             true, 4, Facets, .01f, {.025f,.025f,.025f});
            Warm += ValidateBudgetPathCount(Reused, false);
        }
        const auto FreshImage = ExecuteFreshBudget(ControlPipeline, Current, 380011, 4, {.025f,.025f,.025f}, Facets);
        const auto ReusedImage = ExecuteReplaySeedReservoir(Reused, Current, 380011, false, true, 4, Facets,
                                                  .01f, {.025f,.025f,.025f});
        std::vector<std::array<float,4>> Mask(ReusedImage.size());
        const auto Guides = Reused.PrimaryReceivers();
        uint32_t Measured = 0;
        for (uint32_t Pixel = 0; Pixel < ReusedImage.size(); ++Pixel)
        {
            const bool Accepted = Guides[Pixel].MotionPositionToken[3] == 1 && ValidateBudgetPixelEightCalls(Reused, Pixel);
            Mask[Pixel] = Accepted ? std::array<float,4>{.05f,1,.05f,1} : std::array<float,4>{.5f,.02f,.02f,1};
            Measured += Accepted ? 1u : 0u;
        }
        const std::string Suffix = Facets ? "Facet" : "Cosine";
        WriteImage(Directory + "/BudgetFresh" + Suffix + ".ppm", FreshImage, Size, Size, 3);
        WriteImage(Directory + "/BudgetReuse" + Suffix + ".ppm", ReusedImage, Size, Size, 3);
        WriteImage(Directory + "/BudgetMask" + Suffix + ".ppm", Mask, Size, Size, 3);
        std::printf("BUDGET_CAPTURE size=64 bounces=4 facets=%u measured_points=%u fresh_calls=%llu "
            "reuse_calls=%llu warm_reuse_calls=%llu\n", unsigned(Facets), Measured,
            static_cast<unsigned long long>(ValidateBudgetPathCount(Fresh, true)),
            static_cast<unsigned long long>(ValidateBudgetPathCount(Reused, false)),
            static_cast<unsigned long long>(Warm));
    }
}
} // namespace
