/*==========================================================================================================================================
                       REPLAYTEMPORALPROVENANCEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Read back per-pixel proposal and temporal-reservoir provenance on the moving-visibility lower-floor holdout.
#pragma once

template <typename Submission>
void PrepareReplayTemporalDiagnostic(Submission& Compute, const LightComputeArchive& Scene,
                                    const std::string& Shaders)
{
    if constexpr (requires { Compute.Prepare(Scene, Shaders, false, false, false, false, true, false, false, true); })
        Compute.Prepare(Scene, Shaders, false, false, false, false, true, false, false, true);
    else
        Compute.Prepare(Scene, Shaders, false, false, false, false, true, false, false);
}

struct ReplayTemporalProvenanceMode
{
    const char* Label;
    uint32_t Candidates;
};

struct ReplayTemporalProvenanceTotals
{
    uint64_t Pixels = 0, Accepted = 0, Selected = 0;
    double FreshWeight = 0, HistoryWeight = 0, HistoryTarget = 0, Retained = 0;
    double ExpectedSelected = 0, ExpectedSelectionVariance = 0;
    ReplayMaterialBatchMoments Mean, Mse;
    double MaximumPixel = 0;
};

[[maybe_unused]] void ValidateReplayTemporalProvenance(const char* Driver, const std::string& Shaders,
                                      bool PathHash, bool SelectionHash)
{
    constexpr uint32_t Size = 32u, Pixels = Size * Size, Cohorts = 128u, SubmissionCount = 4u;
    constexpr uint32_t FarGroup = 1u;
    constexpr float Floor = .0001f;
    constexpr const char* FloorName = "lower";
    constexpr const char* Scenario = "motion_visibility_far";
    const std::array<ReplayTemporalProvenanceMode, 2> Modes{{{"temporal1", 1u}, {"temporal4", 4u}}};
    const char* Arm = PathHash ? "path_hash" : (SelectionHash ? "selection_hash" : "baseline");
    Require(!(PathHash && SelectionHash), "Provenance arm cannot change both RNG streams");

    auto DimSpecification = EnumerateReplayMaterialSpecifications()[4];
    DimSpecification.Coverage = 1e-3f;
    DimSpecification.CoatCoverage = 2e-3f;
    DimSpecification.ReportingScale = 1e3;
    const auto Dim = ConstructReplayMaterialReuseFixture(Size, DimSpecification);
    const auto Metal = ConstructReplayMaterialReuseFixture(Size, EnumerateReplayMaterialSpecifications()[1]);
    const auto Capacity = ConstructReplayMixedMaterialFixture(Size, Dim, Metal);
    const auto Scenes = ConstructReplayFreshMotionScenes(Size, Dim, Metal, false);
    const double PositionX = double(Scenes.back().Receivers[1].PositionHit[0]);
    const double Reference = IntegrateReplayMixedVisibleReference(Metal, PositionX, 1024u);
    const double Coarse = IntegrateReplayMixedVisibleReference(Metal, PositionX, 512u);
    Require(std::isfinite(Reference) && Reference > 0 && std::abs(Reference - Coarse) < .0002,
            "Provenance reference failed its convergence check");

    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_TEMPORAL_PROVENANCE arm=%s scenario=%s reference=%.12g coarse=%.12g x=%.9g "
                "preparing_on=%s\n", Arm, Scenario, Reference, Coarse, PositionX, Compute.Properties.deviceName);
    std::fflush(stdout);
    PrepareReplayTemporalDiagnostic(Compute, Capacity, Shaders);
    std::printf("REPLAY_TEMPORAL_PROVENANCE arm=%s pipelines_ready=1 floor=%s cohorts=%u\n",
                Arm, FloorName, Cohorts);
    std::fflush(stdout);

    std::array<ReplayTemporalProvenanceTotals, Modes.size()> Totals{};
    std::mt19937_64 Random(0xd7a4c1e95b830621ull);
    uint64_t PathSeedChecks = 0, SelectionSeedChecks = 0, ContinuedFrames = 0;
    uint32_t MinimumAccepted = UINT32_MAX;
    for (uint32_t Cohort = 0; Cohort < Cohorts; ++Cohort)
    {
        std::array<uint32_t, SubmissionCount> Seeds{};
        for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
        {
            const auto& Mode = Modes[ModeIndex];
            auto& Total = Totals[ModeIndex];
            std::vector<std::array<float, 4>> Image;
            for (uint32_t Frame = 0; Frame < SubmissionCount; ++Frame)
            {
                Image = ExecuteReplayMaterialFloor(Compute, Scenes[Frame],
                    ReplayMaterialReuseMode{Mode.Label, Mode.Candidates, true, false},
                    Seeds[Frame], Frame == 0u, Floor);
                const auto Counters = Compute.ExtendedStatistics();
                if (Frame == SubmissionCount - 1u)
                    Require(Counters[20] == Pixels,
                            "Provenance moving-visibility temporal history was not accepted for every receiver");
                if (Frame > 0u)
                {
                    Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1u && Counters[20] == Pixels,
                            "Provenance moving history failed to continue at cap one");
                    MinimumAccepted = std::min(MinimumAccepted, Counters[20]);
                    ++ContinuedFrames;
                }
            }

            const auto FreshSeeds = Compute.ReadReplayLogicalSlab(5u, false);
            const auto SelectionSeeds = Compute.ReadReplayLogicalSlab(6u, false);
            const auto FreshHistory = Compute.ReadReplayLogicalSlab(7u, false);
            const auto TemporalChoice = Compute.ReadReplayLogicalSlab(8u, false);
            double Sum = 0, Squared = 0, Maximum = 0;
            uint32_t GroupPixels = 0;
            uint64_t BatchAccepted = 0, BatchSelected = 0;
            double BatchFreshWeight = 0, BatchHistoryWeight = 0, BatchHistoryTarget = 0;
            double BatchRetained = 0, BatchExpectedSelected = 0, BatchExpectedVariance = 0;
            for (uint32_t Pixel = 0; Pixel < Pixels; ++Pixel)
            {
                const uint32_t ExpectedPath = ReplayTemporalPathExpectedFirstSeed(Seeds.back(), Pixel, PathHash);
                Require(FreshSeeds[Pixel][0] == float(ExpectedPath & 65535u) &&
                            FreshSeeds[Pixel][1] == float(ExpectedPath >> 16u),
                        "Provenance path stream failed exact host-seed reconstruction");
                ++PathSeedChecks;
                const uint32_t ExpectedSelection = ReplayTemporalExpectedSelectionBits(Seeds.back(), Pixel, SelectionHash);
                Require(SelectionSeeds[Pixel][0] == float(ExpectedSelection & 65535u) &&
                            SelectionSeeds[Pixel][1] == float(ExpectedSelection >> 16u),
                        "Provenance selection stream failed exact host-seed reconstruction");
                ++SelectionSeedChecks;
                const uint32_t X = Pixel % Size, Y = Pixel / Size;
                if ((X + Y) % 2u != FarGroup) continue;

                const double Fresh = double(FreshHistory[Pixel][0]);
                const double FreshCount = double(FreshHistory[Pixel][1]);
                const double History = double(FreshHistory[Pixel][2]);
                const double Target = double(FreshHistory[Pixel][3]);
                const double Retained = double(TemporalChoice[Pixel][0]);
                const double Accepted = double(TemporalChoice[Pixel][1]);
                const double Selected = double(TemporalChoice[Pixel][2]);
                const double Probability = double(TemporalChoice[Pixel][3]);
                Require(std::isfinite(Fresh) && Fresh >= 0 && std::isfinite(FreshCount) && FreshCount >= 0 &&
                            std::isfinite(History) && History >= 0 && std::isfinite(Target) && Target >= 0 &&
                            std::isfinite(Retained) && Retained >= 0 &&
                            (Accepted == 0 || Accepted == 1) && (Selected == 0 || Selected == 1) &&
                            Selected <= Accepted && std::isfinite(Probability) && Probability >= 0 && Probability <= 1,
                        "Provenance slab contains an invalid reservoir value");
                BatchFreshWeight += Fresh;
                BatchHistoryWeight += History;
                BatchHistoryTarget += Target;
                BatchRetained += Retained;
                BatchAccepted += uint64_t(Accepted);
                BatchSelected += uint64_t(Selected);
                if (Accepted != 0)
                {
                    BatchExpectedSelected += Probability;
                    BatchExpectedVariance += Probability * (1.0 - Probability);
                }
                const double Sample = double(Image[Pixel][0]);
                Require(std::isfinite(Sample) && Sample >= 0, "Provenance replay returned invalid radiance");
                Sum += Sample;
                Squared += (Sample - Reference) * (Sample - Reference);
                Maximum = std::max(Maximum, Sample);
                ++GroupPixels;
            }
            Require(GroupPixels == Pixels / 2u, "Provenance moving-visibility group size changed");
            Total.Pixels += GroupPixels;
            Total.Accepted += BatchAccepted;
            Total.Selected += BatchSelected;
            Total.FreshWeight += BatchFreshWeight;
            Total.HistoryWeight += BatchHistoryWeight;
            Total.HistoryTarget += BatchHistoryTarget;
            Total.Retained += BatchRetained;
            Total.ExpectedSelected += BatchExpectedSelected;
            Total.ExpectedSelectionVariance += BatchExpectedVariance;
            Total.Mean.Accumulate(Sum / GroupPixels);
            Total.Mse.Accumulate(Squared / GroupPixels);
            Total.MaximumPixel = std::max(Total.MaximumPixel, Maximum);
            const double WeightShare = BatchFreshWeight + BatchHistoryWeight > 0
                ? BatchHistoryWeight / (BatchFreshWeight + BatchHistoryWeight) : 0;
            std::printf("REPLAY_TEMPORAL_PROVENANCE_BATCH arm=%s scenario=%s group=far mode=%s floor=%s "
                        "batch=%u pixels=%u mean=%.12g mse=%.12g hash=%016llx path_hash=%016llx selection_hash=%016llx "
                        "accepted=%llu selected=%llu expected_selected=%.9g fresh_weight=%.12g history_weight=%.12g "
                        "history_target=%.12g retained=%.9g weight_share=%.9g\n",
                        Arm, Scenario, Mode.Label, FloorName, Cohort, GroupPixels, Sum / GroupPixels,
                        Squared / GroupPixels,
                        static_cast<unsigned long long>(ReplayMaterialImageHash(Image, Size, FarGroup)),
                        static_cast<unsigned long long>(ReplayTemporalPathSeedHash(Seeds.back(), Pixels, PathHash)),
                        static_cast<unsigned long long>(ReplayTemporalSelectionHash(Seeds.back(), Pixels, SelectionHash)),
                        static_cast<unsigned long long>(BatchAccepted), static_cast<unsigned long long>(BatchSelected),
                        BatchExpectedSelected, BatchFreshWeight, BatchHistoryWeight, BatchHistoryTarget,
                        BatchRetained, WeightShare);
        }
        std::fflush(stdout);
    }

    for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
    {
        const auto& Total = Totals[ModeIndex];
        const double AcceptedRate = Total.Pixels ? double(Total.Accepted) / Total.Pixels : 0;
        const double SelectedRate = Total.Accepted ? double(Total.Selected) / Total.Accepted : 0;
        const double ExpectedRate = Total.Accepted ? Total.ExpectedSelected / Total.Accepted : 0;
        const double ExpectedSE = Total.ExpectedSelectionVariance > 0
            ? std::sqrt(Total.ExpectedSelectionVariance) / Total.Accepted : 0;
        const double SelectionZ = ExpectedSE > 0 ? (SelectedRate - ExpectedRate) / ExpectedSE : 0;
        const double WeightShare = Total.FreshWeight + Total.HistoryWeight > 0
            ? Total.HistoryWeight / (Total.FreshWeight + Total.HistoryWeight) : 0;
        const double Mean = Total.Mean.Mean(), MeanSE = Total.Mean.StandardError();
        const double Mse = Total.Mse.Mean();
        std::printf("REPLAY_TEMPORAL_PROVENANCE_RESULT arm=%s scenario=%s group=far mode=%s floor=%s "
                    "reference=%.12g mean=%.12g mean_se=%.12g rmse=%.12g mse=%.12g "
                    "accepted=%llu selected=%llu accepted_rate=%.9g selected_given_accepted=%.9g "
                    "expected_selected_given_accepted=%.9g selection_residual_se=%.9g selection_z_approx=%.6g "
                    "fresh_weight=%.12g history_weight=%.12g history_weight_share=%.9g "
                    "history_target=%.12g retained=%.9g pixels=%llu cohorts=%u\n",
                    Arm, Scenario, Modes[ModeIndex].Label, FloorName, Reference, Mean, MeanSE, std::sqrt(Mse), Mse,
                    static_cast<unsigned long long>(Total.Accepted), static_cast<unsigned long long>(Total.Selected),
                    AcceptedRate, SelectedRate, ExpectedRate, ExpectedSE, SelectionZ, Total.FreshWeight,
                    Total.HistoryWeight, WeightShare, Total.HistoryTarget, Total.Retained,
                    static_cast<unsigned long long>(Total.Pixels), Cohorts);
    }
    const uint64_t ExpectedChecks = uint64_t(Cohorts) * Modes.size() * Pixels;
    std::printf("REPLAY_TEMPORAL_PROVENANCE_CONTROL arm=%s path_seed_checks=%llu selection_seed_checks=%llu "
                "expected=%llu continued_frames=%llu minimum_accepted=%u cap=1\n", Arm,
                static_cast<unsigned long long>(PathSeedChecks),
                static_cast<unsigned long long>(SelectionSeedChecks),
                static_cast<unsigned long long>(ExpectedChecks),
                static_cast<unsigned long long>(ContinuedFrames), MinimumAccepted);
    Require(PathSeedChecks == ExpectedChecks && SelectionSeedChecks == ExpectedChecks &&
                ContinuedFrames == uint64_t(Cohorts) * Modes.size() * (SubmissionCount - 1u) &&
                MinimumAccepted == Pixels,
            "Provenance seed or temporal-continuation coverage was incomplete");
    std::printf("PASS ReplayTemporalProvenanceValidation arm=%s\n", Arm);
    std::fflush(stdout);
}
