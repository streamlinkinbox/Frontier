/*==========================================================================================================================================
                                  REPLAYTEMPORALFRESHHISTORYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Measure selected versus raw-seed temporal histories and their visibility-dependent weights.
#pragma once

//----------------------------------------------------------------------------------------------------------------------
//                                                 HISTORY DIAGNOSTICS
//----------------------------------------------------------------------------------------------------------------------

struct ReplayTemporalHistoryGroupMeasurements
{
    ReplayMaterialBatchMoments Mean, Mse, ConditionalMean, HistoryNormalization;
    double MaximumPixel = 0, MaximumNormalization = 0, MaximumHistoryContribution = 0;
    uint64_t HighNormalization = 0, ReopenedDarkWinners = 0, AuditedPixels = 0;
};

struct ReplayTemporalHistoryMeasurements
{
    std::array<ReplayTemporalHistoryGroupMeasurements, 2> Groups;
    uint64_t FinalRays = 0, WarmRays = 0, TriangleTests = 0, TemporalAccepted = 0;
};

uint32_t ReplayTemporalFirstSeed(uint32_t Sampling, uint32_t Pixel)
{
    uint32_t Bits = Sampling ^ (Pixel * 747796405u + 2891336453u);
    Bits = Bits * 747796405u + 2891336453u;
    const uint32_t Word = ((Bits >> ((Bits >> 28u) + 4u)) ^ Bits) * 277803737u;
    return (Word >> 22u) ^ Word;
}

std::array<LightComputeArchive, 4> ConstructReplayTemporalHistoryScenes(uint32_t Size,
                                                                        const LightComputeArchive& Dim,
                                                                        const LightComputeArchive& Metal,
                                                                        bool Mixed,
                                                                        bool Moving)
{
    auto Scenes = ConstructReplayFreshMotionScenes(Size, Dim, Metal, Mixed);
    if (!Moving)
    {
        auto Stationary = Scenes.back();
        for (auto& Receiver : Stationary.Receivers)
            Receiver.MotionPositionToken[0] = Receiver.PositionHit[0];
        for (auto& Scene : Scenes) Scene = Stationary;
    }
    for (const auto& Scene : Scenes)
        for (const auto& Receiver : Scene.Receivers)
            Require(Receiver.ResidualSpecular[0] == 0 && Receiver.ResidualSpecular[1] == 0 &&
                        Receiver.ResidualSpecular[2] == 0,
                    "Temporal diagnostic requires zero residual colour");
    return Scenes;
}

void ValidateReplayTemporalGuideRejection(LightComputeSubmission& Compute,
                                          const std::array<LightComputeArchive, 4>& Scenes,
                                          const char* Arm)
{
    const ReplayMaterialReuseMode Temporal{"temporal1", 1u, true, false};
    const ReplayMaterialReuseMode Fresh{"fresh1", 1u, false, false};
    (void)ExecuteReplayMaterialFloor(Compute, Scenes[0], Temporal, 910081u, true, .0001f);
    auto Changed = Scenes[1];
    Changed.Receivers[0].MotionPositionToken[3] += 17.0f;
    Changed.Receivers[1].MotionPixel[3] = 0.0f;
    const auto Replayed = ExecuteReplayMaterialFloor(Compute, Changed, Temporal, 910093u, false, .0001f);
    const auto Counts = Compute.ExtendedStatistics();
    Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1u && Counts[20] == 1022u,
            "Changed motion guides did not reject exactly two temporal histories");
    const auto Explicit = ExecuteReplayMaterialFloor(Compute, Changed, Fresh, 910093u, true, .0001f);
    Require(Replayed[0] == Explicit[0] && Replayed[1] == Explicit[1],
            "Rejected temporal guides did not reproduce the fresh pixel");
    std::printf("REPLAY_TEMPORAL_GUIDES arm=%s rejected=2 accepted=%u exact_fresh=2 reset=0 cap=1\n",
                Arm, Counts[20]);
    std::fflush(stdout);
}

void ValidateReplayTemporalHistoricalControls(LightComputeSubmission& Compute,
                                              const LightComputeArchive& Dim,
                                              const LightComputeArchive& Metal,
                                              const char* Arm)
{
    constexpr uint32_t Size = 32u, Cohorts = 8u, SubmissionCount = 4u;
    const std::array<ReplayMaterialReuseMode, 3> Modes{{
        {"fresh1", 1u, false, false}, {"fresh4", 4u, false, false}, {"temporal1", 1u, true, false}}};
    constexpr std::array<float, 2> Floors{.01f, .0001f};
    constexpr std::array<const char*, 2> FloorNames{"default", "lower"};
    constexpr std::array<const char*, 2> SceneNames{"mixed", "visibility"};
    for (uint32_t SceneIndex = 0; SceneIndex < SceneNames.size(); ++SceneIndex)
    {
        const bool Mixed = SceneIndex == 0;
        const auto Scenes = ConstructReplayFreshMotionScenes(Size, Dim, Metal, Mixed);
        const std::array<const char*, 2> GroupNames = Mixed
            ? std::array<const char*, 2>{"dim", "metal"} : std::array<const char*, 2>{"near", "far"};
        std::mt19937_64 Random(0x728fba194607ull + uint64_t(SceneIndex) * 0x1000193ull);
        for (uint32_t Cohort = 0; Cohort < Cohorts; ++Cohort)
        {
            std::array<uint32_t, SubmissionCount> Seeds{};
            for (auto& Seed : Seeds) Seed = uint32_t(Random());
            for (const auto& Mode : Modes)
                for (uint32_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
                {
                    std::vector<std::array<float, 4>> Image;
                    const uint32_t First = Mode.Temporal ? 0u : SubmissionCount - 1u;
                    for (uint32_t Index = First; Index < SubmissionCount; ++Index)
                        Image = ExecuteReplayMaterialFloor(Compute, Scenes[Index], Mode, Seeds[Index], Index == First,
                                                           Floors[FloorIndex]);
                    for (uint32_t Group = 0; Group < 2; ++Group)
                        std::printf("REPLAY_TEMPORAL_HISTORICAL arm=%s scene=%s group=%s mode=%s floor=%s "
                                    "batch=%u hash=%016llx\n", Arm, SceneNames[SceneIndex], GroupNames[Group],
                                    Mode.Label, FloorNames[FloorIndex], Cohort,
                                    static_cast<unsigned long long>(ReplayMaterialImageHash(Image, Size, Group)));
                }
        }
    }
    std::fflush(stdout);
}

//----------------------------------------------------------------------------------------------------------------------
//                                             PAIRED TEMPORAL-HISTORY STUDY
//----------------------------------------------------------------------------------------------------------------------

void ValidateReplayTemporalFreshHistory(const char* Driver, const std::string& Shaders, bool Candidate)
{
    constexpr uint32_t Size = 32u, Cohorts = 48u, SubmissionCount = 4u;
    constexpr uint32_t Pixels = Size * Size;
    const std::array<ReplayMaterialReuseMode, 5> Modes{{
        {"fresh1", 1u, false, false}, {"fresh2", 2u, false, false}, {"fresh4", 4u, false, false},
        {"temporal1", 1u, true, false}, {"temporal4", 4u, true, false}}};
    constexpr std::array<float, 2> Floors{.01f, .0001f};
    constexpr std::array<const char*, 2> FloorNames{"default", "lower"};
    constexpr std::array<const char*, 4> ScenarioNames{
        "motion_mixed", "motion_visibility", "stationary_mixed", "stationary_visibility"};
    const char* Arm = Candidate ? "raw" : "baseline";
    auto DimSpecification = EnumerateReplayMaterialSpecifications()[4];
    DimSpecification.Coverage = 1e-3f;
    DimSpecification.CoatCoverage = 2e-3f;
    DimSpecification.ReportingScale = 1e3;
    const auto Dim = ConstructReplayMaterialReuseFixture(Size, DimSpecification);
    const auto Metal = ConstructReplayMaterialReuseFixture(Size, EnumerateReplayMaterialSpecifications()[1]);
    const auto Capacity = ConstructReplayMixedMaterialFixture(Size, Dim, Metal);
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_TEMPORAL arm=%s preparing Engine-derived pipelines on %s\n", Arm,
                Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    std::printf("REPLAY_TEMPORAL arm=%s pipelines_ready=1\n", Arm);
    std::fflush(stdout);
    ValidateReplayTemporalHistoricalControls(Compute, Dim, Metal, Arm);
    bool WithinReference = true;
    uint64_t TotalOraclePixels = 0, TotalRawSeeds = 0;
    double MaximumOracleError = 0;
    for (uint32_t Scenario = 0; Scenario < ScenarioNames.size(); ++Scenario)
    {
        const bool Moving = Scenario < 2u;
        const bool Mixed = Scenario % 2u == 0;
        const auto Scenes = ConstructReplayTemporalHistoryScenes(Size, Dim, Metal, Mixed, Moving);
        const std::array<const char*, 2> GroupNames = Mixed
            ? std::array<const char*, 2>{"dim", "metal"} : std::array<const char*, 2>{"near", "far"};
        const std::array<const LightComputeArchive*, 2> Materials{Mixed ? &Dim : &Metal, &Metal};
        const std::array<double, 2> Scales{Mixed ? 1e3 : 1.0, 1.0};
        std::array<double, 2> References{};
        for (uint32_t Group = 0; Group < 2; ++Group)
        {
            const double X = double(Scenes.back().Receivers[Group].PositionHit[0]);
            References[Group] = IntegrateReplayMixedVisibleReference(*Materials[Group], X, 1024u) * Scales[Group];
            const double Coarse = IntegrateReplayMixedVisibleReference(*Materials[Group], X, 512u) * Scales[Group];
            Require(std::isfinite(References[Group]) && References[Group] > 0 &&
                        std::abs(References[Group] - Coarse) < .0002,
                    "Temporal-history reference did not converge");
            std::printf("REPLAY_TEMPORAL_REFERENCE arm=%s scenario=%s group=%s x=%.9g reference=%.9g "
                        "coarse=%.9g scale=%.3g\n", Arm, ScenarioNames[Scenario], GroupNames[Group], X,
                        References[Group], Coarse, Scales[Group]);
        }
        std::array<ReplayTemporalHistoryMeasurements, Modes.size() * Floors.size()> Results{};
        std::mt19937_64 Random(0xa92f10d4b731ull + uint64_t(Scenario) * 0x1000193ull);
        uint32_t Continued = 0, MinimumAccepted = UINT32_MAX;
        for (uint32_t Cohort = 0; Cohort < Cohorts; ++Cohort)
        {
            std::array<uint32_t, SubmissionCount> Seeds{};
            for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
            std::array<std::vector<std::array<float, 4>>, Floors.size()> FreshImages{};
            for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
                for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
                {
                    const auto& Mode = Modes[ModeIndex];
                    auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
                    const uint32_t First = Mode.Temporal ? 0u : SubmissionCount - 1u;
                    std::vector<std::array<float, 4>> Image, PreviousSelected, Diagnostic;
                    for (uint32_t Index = First; Index < SubmissionCount; ++Index)
                    {
                        Image = ExecuteReplayMaterialFloor(Compute, Scenes[Index], Mode, Seeds[Index], Index == First,
                                                           Floors[FloorIndex]);
                        const auto Counts = Compute.ExtendedStatistics();
                        if (Mode.Temporal && Index > First)
                        {
                            const uint32_t ExpectedCap = Moving ? 1u : 64u;
                            Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == ExpectedCap &&
                                        Counts[20] == Pixels,
                                    "Temporal history did not continue with the expected cap and acceptance");
                            ++Continued;
                            MinimumAccepted = std::min(MinimumAccepted, Counts[20]);
                        }
                        if (ModeIndex == 3u && Index == SubmissionCount - 2u)
                            PreviousSelected = Compute.ReadReplayLogicalSlab(3u, false);
                        if (Index + 1u < SubmissionCount) Measurement.WarmRays += Counts[8];
                        else
                        {
                            Measurement.FinalRays += Counts[8];
                            Measurement.TriangleTests += Counts[19];
                            Measurement.TemporalAccepted += Counts[20];
                            Require((Counts[20] > 0) == Mode.Temporal && Counts[22] == 0,
                                    "Unexpected temporal/spatial activation");
                            if (ModeIndex == 3u) Diagnostic = Compute.ReadReplayLogicalSlab(5u, false);
                            if (Candidate && Mode.Temporal)
                            {
                                const auto History = Compute.ReadReplayLogicalSlab(0u, false);
                                for (uint32_t Pixel = 0; Pixel < Pixels; ++Pixel)
                                {
                                    const uint32_t Expected = ReplayTemporalFirstSeed(Seeds[Index], Pixel);
                                    Require(History[Pixel][0] == float(Expected & 65535u) &&
                                                History[Pixel][1] == float(Expected >> 16u) &&
                                                History[Pixel][2] == 1.0f && History[Pixel][3] == 1.0f,
                                            "Raw history did not retain the first fresh seed with unit weight/count");
                                    ++TotalRawSeeds;
                                }
                            }
                        }
                    }
                    if (ModeIndex == 0u) FreshImages[FloorIndex] = Image;
                    std::array<double, 2> Sum{}, Squared{}, Maximum{}, Conditional{}, Normalization{};
                    std::array<uint32_t, 2> Counts{};
                    for (uint32_t Y = 0; Y < Size; ++Y)
                        for (uint32_t X = 0; X < Size; ++X)
                        {
                            const uint32_t Pixel = Y * Size + X, Group = (X + Y) % 2u;
                            const double Sample = double(Image[Pixel][0]) * Scales[Group];
                            Require(std::isfinite(Sample) && Sample >= 0, "Invalid temporal-history radiance");
                            Sum[Group] += Sample;
                            Squared[Group] += (Sample - References[Group]) * (Sample - References[Group]);
                            Maximum[Group] = std::max(Maximum[Group], Sample);
                            ++Counts[Group];
                            if (ModeIndex == 3u)
                            {
                                const auto& History = Diagnostic[Pixel];
                                const auto& Fresh = FreshImages[FloorIndex][Pixel];
                                Require(std::isfinite(History[0]) && History[0] >= 0 && std::isfinite(History[1]) &&
                                            History[1] > 0 && std::isfinite(History[2]) && History[2] >= 1 &&
                                            std::isfinite(History[3]) && History[3] > 0,
                                        "Invalid temporal diagnostic or unaccepted history");
                                const double Retained = std::min(double(History[2]), Moving ? 1.0 : 64.0);
                                const double FreshTarget = std::max(0.0, double(Fresh[0]) * .2126 +
                                    double(Fresh[1]) * .7152 + double(Fresh[2]) * .0722) + Floors[FloorIndex];
                                const double Weight = double(History[3]) * double(History[1]) * Retained;
                                const double SumTargets = FreshTarget + Weight;
                                const double FreshOutcome = double(Fresh[0]) * SumTargets /
                                                            ((1.0 + Retained) * FreshTarget);
                                const double HistoryOutcome = double(History[0]) * SumTargets /
                                                              ((1.0 + Retained) * double(History[3]));
                                const double Actual = double(Image[Pixel][0]);
                                const double ErrorFresh = std::abs(Actual - FreshOutcome) /
                                                          (1.0 + std::abs(FreshOutcome));
                                const double ErrorHistory = std::abs(Actual - HistoryOutcome) /
                                                            (1.0 + std::abs(HistoryOutcome));
                                const double Error = std::min(ErrorFresh, ErrorHistory);
                                MaximumOracleError = std::max(MaximumOracleError, Error);
                                Require(Error < 2e-5,
                                        "Temporal output did not match either independent reservoir outcome");
                                ++TotalOraclePixels;
                                const double HistoryContribution = double(History[0]) * double(History[1]) * Retained;
                                Conditional[Group] += (double(Fresh[0]) + HistoryContribution) /
                                                      (1.0 + Retained) * Scales[Group];
                                Normalization[Group] += History[1];
                                auto& GroupResult = Measurement.Groups[Group];
                                GroupResult.MaximumNormalization = std::max(GroupResult.MaximumNormalization,
                                                                            double(History[1]));
                                GroupResult.MaximumHistoryContribution = std::max(
                                    GroupResult.MaximumHistoryContribution, HistoryContribution * Scales[Group]);
                                GroupResult.HighNormalization += History[1] > 4.0f;
                                GroupResult.AuditedPixels++;
                                if (!Candidate && PreviousSelected[Pixel][3] <= Floors[FloorIndex] * 1.01f &&
                                    double(History[0]) * Scales[Group] > .01)
                                    GroupResult.ReopenedDarkWinners++;
                            }
                        }
                    for (uint32_t Group = 0; Group < 2; ++Group)
                    {
                        Require(Counts[Group] == Pixels / 2u, "Temporal fixture group size changed");
                        const double Mean = Sum[Group] / Counts[Group], Mse = Squared[Group] / Counts[Group];
                        auto& Result = Measurement.Groups[Group];
                        Result.Mean.Accumulate(Mean);
                        Result.Mse.Accumulate(Mse);
                        Result.MaximumPixel = std::max(Result.MaximumPixel, Maximum[Group]);
                        if (ModeIndex == 3u)
                        {
                            Result.ConditionalMean.Accumulate(Conditional[Group] / Counts[Group]);
                            Result.HistoryNormalization.Accumulate(Normalization[Group] / Counts[Group]);
                        }
                        std::printf("REPLAY_TEMPORAL_BATCH arm=%s scenario=%s group=%s mode=%s floor=%s batch=%u "
                                    "mean=%.12g mse=%.12g max_pixel=%.12g hash=%016llx\n", Arm,
                                    ScenarioNames[Scenario], GroupNames[Group], Mode.Label, FloorNames[FloorIndex],
                                    Cohort, Mean, Mse, Maximum[Group],
                                    static_cast<unsigned long long>(ReplayMaterialImageHash(Image, Size, Group)));
                    }
                }
            std::fflush(stdout);
        }
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
            for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
            {
                const auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
                for (uint32_t Group = 0; Group < 2; ++Group)
                {
                    const auto& Result = Measurement.Groups[Group];
                    const bool Gate = std::abs(Result.Mean.Mean() - References[Group]) <=
                                      5.0 * Result.Mean.StandardError() + .001;
                    WithinReference = WithinReference && Gate;
                    std::printf("REPLAY_TEMPORAL_RESULT arm=%s scenario=%s group=%s mode=%s floor=%s reference=%.9g "
                                "mean=%.9g batch_se=%.9g rmse=%.9g mse=%.9g mse_se=%.9g max_pixel=%.9g "
                                "mean_rays=%.3f warm_rays=%.3f mean_triangles=%.3f temporal=%llu "
                                "mean_gate=%u scale=%.3g batches=%u pixels_per_group=%u\n", Arm,
                                ScenarioNames[Scenario], GroupNames[Group], Modes[ModeIndex].Label,
                                FloorNames[FloorIndex], References[Group], Result.Mean.Mean(),
                                Result.Mean.StandardError(), std::sqrt(Result.Mse.Mean()), Result.Mse.Mean(),
                                Result.Mse.StandardError(), Result.MaximumPixel,
                                double(Measurement.FinalRays) / Cohorts,
                                double(Measurement.WarmRays) / Cohorts,
                                double(Measurement.TriangleTests) / Cohorts,
                                static_cast<unsigned long long>(Measurement.TemporalAccepted), unsigned(Gate),
                                Scales[Group], Cohorts, Pixels / 2u);
                    if (ModeIndex == 3u)
                        std::printf("REPLAY_TEMPORAL_DIAGNOSTIC arm=%s scenario=%s group=%s floor=%s "
                                    "conditional_mean=%.9g conditional_se=%.9g mean_normalization=%.9g "
                                    "max_normalization=%.9g high_normalization=%llu max_history_contribution=%.9g "
                                    "reopened_dark_winners=%llu audited_pixels=%llu\n", Arm, ScenarioNames[Scenario],
                                    GroupNames[Group], FloorNames[FloorIndex], Result.ConditionalMean.Mean(),
                                    Result.ConditionalMean.StandardError(), Result.HistoryNormalization.Mean(),
                                    Result.MaximumNormalization,
                                    static_cast<unsigned long long>(Result.HighNormalization),
                                    Result.MaximumHistoryContribution,
                                    static_cast<unsigned long long>(Result.ReopenedDarkWinners),
                                    static_cast<unsigned long long>(Result.AuditedPixels));
                }
            }
        std::printf("REPLAY_TEMPORAL_CONTINUATION arm=%s scenario=%s count=%u min_accepted=%u expected_cap=%u\n",
                    Arm, ScenarioNames[Scenario], Continued, MinimumAccepted, Moving ? 1u : 64u);
        std::fflush(stdout);
    }
    ValidateReplayTemporalGuideRejection(Compute, ConstructReplayFreshMotionScenes(Size, Dim, Metal, true), Arm);
    std::printf("REPLAY_TEMPORAL_ORACLE arm=%s pixels=%llu max_relative_error=%.9g raw_seed_checks=%llu\n", Arm,
                static_cast<unsigned long long>(TotalOraclePixels), MaximumOracleError,
                static_cast<unsigned long long>(TotalRawSeeds));
    std::fflush(stdout);
    Require(WithinReference, "Temporal-history holdout failed the independent mean gates");
    std::printf("PASS ReplayTemporalFreshHistoryValidation arm=%s\n", Arm);
    std::fflush(stdout);
}
