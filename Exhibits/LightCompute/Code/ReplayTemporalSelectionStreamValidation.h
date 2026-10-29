/*==========================================================================================================================================
                            REPLAYTEMPORALSELECTIONSTREAMVALIDATION.H
==========================================================================================================================================*/
// 🧩 Isolate reservoir-selection random streams while holding temporal paths and weights fixed.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                             FOCUSED STREAM HOLDOUT
//------------------------------------------------------------------------------------------------------------------------

struct ReplayTemporalSelectionStreamMode
{
    const char* Label;
    uint32_t Candidates;
    bool Temporal;
};

struct ReplayTemporalSelectionStreamResult
{
    ReplayMaterialBatchMoments Mean, Mse;
    double MaximumPixel = 0;
    uint64_t FinalRays = 0, WarmRays = 0, TriangleTests = 0, TemporalAccepted = 0;
};

uint32_t ReplayTemporalExpectedSelectionBits(uint32_t Sampling, uint32_t Pixel, bool Decorrelated)
{
    if (!Decorrelated) return (Sampling * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);
    uint32_t Bits = Sampling ^ (Pixel * 0x9e3779b9u) ^ 0x6a09e667u;
    Bits ^= Bits >> 16u;
    Bits *= 0x7feb352du;
    Bits ^= Bits >> 15u;
    Bits *= 0x846ca68bu;
    Bits ^= Bits >> 16u;
    return Bits;
}

uint64_t ReplayTemporalSelectionHash(uint32_t Sampling, uint32_t Pixels, bool Decorrelated)
{
    uint64_t Hash = 1469598103934665603ull;
    for (uint32_t Pixel = 0; Pixel < Pixels; ++Pixel)
    {
        Hash ^= ReplayTemporalExpectedSelectionBits(Sampling, Pixel, Decorrelated);
        Hash *= 1099511628211ull;
    }
    return Hash;
}

void ValidateReplayTemporalSelectionStream(const char* Driver, const std::string& Shaders, bool Decorrelated)
{
    constexpr uint32_t Size = 32u, Cohorts = 128u, SubmissionCount = 4u;
    constexpr uint32_t Pixels = Size * Size;
    const std::array<ReplayTemporalSelectionStreamMode, 5> Modes{{
        {"fresh1", 1u, false}, {"fresh2", 2u, false}, {"fresh4", 4u, false},
        {"temporal1", 1u, true}, {"temporal4", 4u, true}}};
    constexpr std::array<float, 2> Floors{.01f, .0001f};
    constexpr std::array<const char*, 2> FloorNames{"default", "lower"};
    constexpr const char* Scenario = "motion_visibility_far";
    const char* Arm = Decorrelated ? "decorrelated" : "baseline";
    constexpr uint32_t FarGroup = 1u;
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
            "Selection-stream reference did not converge");

    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_SELECTION_STREAM arm=%s scenario=%s reference=%.12g coarse=%.12g x=%.9g "
                "preparing_on=%s\n", Arm, Scenario, Reference, Coarse, PositionX, Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    std::printf("REPLAY_SELECTION_STREAM arm=%s pipelines_ready=1\n", Arm);
    std::fflush(stdout);

    std::array<ReplayTemporalSelectionStreamResult, Modes.size() * Floors.size()> Results{};
    std::mt19937_64 Random(0xd7a4c1e95b830621ull);
    uint64_t FreshSeedChecks = 0, SelectionSeedChecks = 0, ContinuedFrames = 0;
    uint32_t MinimumAccepted = UINT32_MAX;
    for (uint32_t Cohort = 0; Cohort < Cohorts; ++Cohort)
    {
        std::array<uint32_t, SubmissionCount> Seeds{};
        for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
            for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
            {
                const auto& Mode = Modes[ModeIndex];
                auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
                const uint32_t First = Mode.Temporal ? 0u : SubmissionCount - 1u;
                std::vector<std::array<float, 4>> Image;
                for (uint32_t Frame = First; Frame < SubmissionCount; ++Frame)
                {
                    Image = ExecuteReplayMaterialFloor(Compute, Scenes[Frame],
                        ReplayMaterialReuseMode{Mode.Label, Mode.Candidates, Mode.Temporal, false},
                        Seeds[Frame], Frame == First, Floors[FloorIndex]);
                    const auto Counters = Compute.ExtendedStatistics();
                    if (Frame + 1u < SubmissionCount)
                        Measurement.WarmRays += Counters[8];
                    else
                    {
                        Measurement.FinalRays += Counters[8];
                        Measurement.TriangleTests += Counters[19];
                        Measurement.TemporalAccepted += Counters[20];
                        Require((Counters[20] > 0) == Mode.Temporal,
                                "Selection-stream temporal activation did not match requested mode");
                    }
                    if (Mode.Temporal && Frame > First)
                    {
                        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1u &&
                                    Counters[20] == Pixels,
                                "Selection-stream moving history failed to continue at cap one");
                        MinimumAccepted = std::min(MinimumAccepted, Counters[20]);
                        ++ContinuedFrames;
                    }
                }

                const auto FreshSeeds = Compute.ReadReplayLogicalSlab(5u, false);
                const auto SelectionSeeds = Compute.ReadReplayLogicalSlab(6u, false);
                double Sum = 0, Squared = 0, Maximum = 0;
                uint32_t GroupPixels = 0;
                for (uint32_t Pixel = 0; Pixel < Pixels; ++Pixel)
                {
                    const uint32_t ExpectedSeed = ReplayTemporalFirstSeed(Seeds.back(), Pixel);
                    Require(FreshSeeds[Pixel][0] == float(ExpectedSeed & 65535u) &&
                                FreshSeeds[Pixel][1] == float(ExpectedSeed >> 16u),
                            "Selection-stream variant altered the path-proposal seed stream");
                    ++FreshSeedChecks;
                    const uint32_t ExpectedSelection = ReplayTemporalExpectedSelectionBits(Seeds.back(), Pixel, Decorrelated);
                    Require(SelectionSeeds[Pixel][0] == float(ExpectedSelection & 65535u) &&
                                SelectionSeeds[Pixel][1] == float(ExpectedSelection >> 16u),
                            "Selection stream did not match its specified test hash");
                    ++SelectionSeedChecks;
                    const uint32_t X = Pixel % Size, Y = Pixel / Size;
                    if ((X + Y) % 2u != FarGroup) continue;
                    const double Sample = double(Image[Pixel][0]);
                    Require(std::isfinite(Sample) && Sample >= 0, "Selection-stream replay returned invalid radiance");
                    Sum += Sample;
                    Squared += (Sample - Reference) * (Sample - Reference);
                    Maximum = std::max(Maximum, Sample);
                    ++GroupPixels;
                }
                Require(GroupPixels == Pixels / 2u, "Selection-stream far group size changed");
                Measurement.Mean.Accumulate(Sum / GroupPixels);
                Measurement.Mse.Accumulate(Squared / GroupPixels);
                Measurement.MaximumPixel = std::max(Measurement.MaximumPixel, Maximum);
                std::printf("REPLAY_SELECTION_STREAM_BATCH arm=%s scenario=%s group=far mode=%s floor=%s "
                            "batch=%u mean=%.12g mse=%.12g max_pixel=%.12g hash=%016llx selection_hash=%016llx\n",
                            Arm, Scenario, Mode.Label, FloorNames[FloorIndex], Cohort, Sum / GroupPixels,
                            Squared / GroupPixels, Maximum,
                            static_cast<unsigned long long>(ReplayMaterialImageHash(Image, Size, FarGroup)),
                            static_cast<unsigned long long>(ReplayTemporalSelectionHash(Seeds.back(), Pixels, Decorrelated)));
            }
        std::fflush(stdout);
    }

    bool WithinReference = true;
    for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
        for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
        {
            const auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
            const double Mean = Measurement.Mean.Mean();
            const double MeanSE = Measurement.Mean.StandardError();
            const double Mse = Measurement.Mse.Mean();
            const double MseSE = Measurement.Mse.StandardError();
            const bool Gate = std::abs(Mean - Reference) <= 5.0 * MeanSE + .001;
            WithinReference = WithinReference && Gate;
            std::printf("REPLAY_SELECTION_STREAM_RESULT arm=%s scenario=%s group=far mode=%s floor=%s "
                        "reference=%.12g mean=%.12g mean_se=%.12g rmse=%.12g mse=%.12g mse_se=%.12g "
                        "max_pixel=%.12g final_rays=%.3f warm_rays=%.3f triangles=%.3f temporal=%llu "
                        "mean_gate=%u cohorts=%u pixels_per_cohort=%u\n", Arm, Scenario,
                        Modes[ModeIndex].Label, FloorNames[FloorIndex], Reference, Mean, MeanSE, std::sqrt(Mse),
                        Mse, MseSE, Measurement.MaximumPixel, double(Measurement.FinalRays) / Cohorts,
                        double(Measurement.WarmRays) / Cohorts, double(Measurement.TriangleTests) / Cohorts,
                        static_cast<unsigned long long>(Measurement.TemporalAccepted), unsigned(Gate), Cohorts,
                        Pixels / 2u);
        }
    std::printf("REPLAY_SELECTION_STREAM_CONTROL arm=%s path_seed_checks=%llu selection_seed_checks=%llu expected=%llu "
                "continued_frames=%llu minimum_accepted=%u cap=1\n", Arm,
                static_cast<unsigned long long>(FreshSeedChecks),
                static_cast<unsigned long long>(SelectionSeedChecks),
                static_cast<unsigned long long>(Cohorts * Modes.size() * Floors.size() * Pixels),
                static_cast<unsigned long long>(ContinuedFrames), MinimumAccepted);
    Require(FreshSeedChecks == uint64_t(Cohorts) * Modes.size() * Floors.size() * Pixels &&
                SelectionSeedChecks == FreshSeedChecks &&
                ContinuedFrames == uint64_t(Cohorts) * 2u * Floors.size() * 3u && MinimumAccepted == Pixels,
            "Selection-stream path/selection/continuation control was incomplete");
    Require(WithinReference, "Selection-stream holdout failed an independent mean gate");
    std::printf("PASS ReplayTemporalSelectionStreamValidation arm=%s\n", Arm);
    std::fflush(stdout);
}
