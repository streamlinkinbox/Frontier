/*==========================================================================================================================================
                                       REPLAYFRESHDONORMOTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Test fresh spatial donors through changing visibility and multiple initial candidates.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                               MOVING RECEIVER FIXTURES
//------------------------------------------------------------------------------------------------------------------------

std::array<LightComputeArchive, 4> ConstructReplayFreshMotionScenes(uint32_t Size,
                                                                   const LightComputeArchive& Dim,
                                                                   const LightComputeArchive& Metal,
                                                                   bool Mixed)
{
    auto Initial = Mixed ? ConstructReplayMixedMaterialFixture(Size, Dim, Metal)
                         : ConstructReplayMixedMaterialFixture(Size, Metal, Metal);
    if (!Mixed)
        for (auto& Receiver : Initial.Receivers) Receiver.ExpectedDirect[3] = 1.0f;
    std::array<LightComputeArchive, 4> Scenes;
    for (uint32_t SubmissionIndex = 0; SubmissionIndex < Scenes.size(); ++SubmissionIndex)
    {
        Scenes[SubmissionIndex] = Initial;
        for (auto& Receiver : Scenes[SubmissionIndex].Receivers)
        {
            const float Origin = Receiver.PositionHit[0];
            Receiver.PositionHit[0] = Origin + .2f * float(SubmissionIndex);
            Receiver.MotionPositionToken[0] = Origin + .2f * float(SubmissionIndex == 0 ? 0 : SubmissionIndex - 1);
        }
    }
    return Scenes;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             SEQUENCE INVALIDATION CONTROL
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayFreshMotionSequence(LightComputeSubmission& Compute,
                                       const std::array<LightComputeArchive, 4>& Scenes,
                                       const char* Arm)
{
    const ReplayMaterialReuseMode Mode{"temporal_disk4", 4u, true, true};
    uint32_t ResetCount = 0;
    for (uint32_t SubmissionIndex = 0; SubmissionIndex < Scenes.size(); ++SubmissionIndex)
    {
        const uint32_t Seed = 840001u + 127u * SubmissionIndex;
        const auto Actual = ExecuteReplayMaterialFloor(Compute, Scenes[SubmissionIndex], Mode, Seed, SubmissionIndex == 0, .01f, true);
        const bool Reset = Compute.ReplayLastSequence.Reset;
        const uint32_t Temporal = Compute.ExtendedStatistics()[20];
        const auto Explicit = ExecuteReplayMaterialFloor(Compute, Scenes[SubmissionIndex], Mode, Seed, true, .01f, true);
        Require(Reset && Temporal == 0 && Actual == Explicit,
                "Changed receiver sequence did not match an explicit history reset");
        ++ResetCount;
    }
    auto Stationary = Scenes.back();
    for (auto& Receiver : Stationary.Receivers)
        Receiver.MotionPositionToken[0] = Receiver.PositionHit[0];
    (void)ExecuteReplayMaterialFloor(Compute, Stationary, Mode, 841009u, true, .01f, true);
    (void)ExecuteReplayMaterialFloor(Compute, Stationary, Mode, 841019u, false, .01f, true);
    const bool Continuation = !Compute.ReplayLastSequence.Reset;
    const uint32_t Temporal = Compute.ExtendedStatistics()[20];
    Require(Continuation && Temporal > 0, "Unchanged receiver sequence did not continue temporal history");
    std::printf("REPLAY_FRESH_MOTION_SEQUENCE arm=%s changed_resets=%u explicit_equal=1 changed_temporal=0 "
                "stationary_continuation=%u stationary_temporal=%u\n", Arm, ResetCount,
                unsigned(Continuation), Temporal);
    std::fflush(stdout);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             PAIRED MOVING-RECEIVER STUDY
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayFreshDonorMotion(const char* Driver, const std::string& Shaders, bool Candidate)
{
    constexpr uint32_t Size = 32u, Batches = 48u, SubmissionCount = 4u;
    const std::array<ReplayMaterialReuseMode, 6> Modes{{
        {"fresh1", 1u, false, false}, {"fresh4", 4u, false, false}, {"temporal1", 1u, true, false},
        {"spatial4", 4u, false, true}, {"combined1", 1u, true, true}, {"combined4", 4u, true, true}}};
    constexpr std::array<float, 2> Floors{.01f, .0001f};
    constexpr std::array<const char*, 2> FloorNames{"default", "lower"};
    constexpr std::array<const char*, 2> SceneNames{"mixed", "visibility"};
    const char* Arm = Candidate ? "fresh" : "baseline";
    auto DimSpecification = EnumerateReplayMaterialSpecifications()[4];
    DimSpecification.Coverage = 1e-3f;
    DimSpecification.CoatCoverage = 2e-3f;
    DimSpecification.ReportingScale = 1e3;
    const auto Dim = ConstructReplayMaterialReuseFixture(Size, DimSpecification);
    const auto Metal = ConstructReplayMaterialReuseFixture(Size, EnumerateReplayMaterialSpecifications()[1]);
    const auto Capacity = ConstructReplayMixedMaterialFixture(Size, Dim, Metal);
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_FRESH_MOTION arm=%s preparing Engine-derived pipelines on %s\n", Arm,
                Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    std::printf("REPLAY_FRESH_MOTION arm=%s pipelines_ready=1\n", Arm);
    std::fflush(stdout);
    bool WithinReference = true;
    for (uint32_t SceneIndex = 0; SceneIndex < SceneNames.size(); ++SceneIndex)
    {
        const bool Mixed = SceneIndex == 0;
        const auto Scenes = ConstructReplayFreshMotionScenes(Size, Dim, Metal, Mixed);
        const std::array<const char*, 2> GroupNames = Mixed
            ? std::array<const char*, 2>{"dim", "metal"} : std::array<const char*, 2>{"near", "far"};
        const std::array<const LightComputeArchive*, 2> Materials{Mixed ? &Dim : &Metal, &Metal};
        const std::array<double, 2> Scales{Mixed ? 1e3 : 1.0, 1.0};
        const std::array<size_t, 2> GroupPixels{0u, 1u};
        std::array<double, 2> References{};
        for (uint32_t Group = 0; Group < 2u; ++Group)
        {
            const double X = double(Scenes.back().Receivers[GroupPixels[Group]].PositionHit[0]);
            References[Group] = IntegrateReplayMixedVisibleReference(*Materials[Group], X, 1024u) * Scales[Group];
            const double Coarse = IntegrateReplayMixedVisibleReference(*Materials[Group], X, 512u) * Scales[Group];
            Require(std::isfinite(References[Group]) && References[Group] > 0 &&
                        std::abs(References[Group] - Coarse) < .0002,
                    "Moving-receiver reference quadrature did not converge");
            std::printf("REPLAY_FRESH_MOTION_REFERENCE arm=%s scene=%s group=%s x=%.9g reference=%.9g "
                        "coarse=%.9g scale=%.3g\n", Arm, SceneNames[SceneIndex], GroupNames[Group], X,
                        References[Group], Coarse, Scales[Group]);
        }
        std::array<ReplayMixedMaterialMeasurements, Modes.size() * Floors.size()> Results{};
        std::mt19937_64 Random(0x728fba194607ull + uint64_t(SceneIndex) * 0x1000193ull);
        uint32_t MinimumTemporal = UINT32_MAX, ContinuedSubmissions = 0, CappedSubmissions = 0;
        uint32_t LastSeed = 0, LastResolveLinks = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            std::array<uint32_t, SubmissionCount> Seeds{};
            for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
            for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
                for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
                {
                    const auto& Mode = Modes[ModeIndex];
                    auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
                    std::vector<std::array<float, 4>> Image;
                    const uint32_t FirstSubmission = Mode.Temporal ? 0u : SubmissionCount - 1u;
                    for (uint32_t SubmissionIndex = FirstSubmission; SubmissionIndex < SubmissionCount; ++SubmissionIndex)
                    {
                        Image = ExecuteReplayMaterialFloor(Compute, Scenes[SubmissionIndex], Mode, Seeds[SubmissionIndex],
                                                           SubmissionIndex == FirstSubmission, Floors[FloorIndex]);
                        const auto Counts = Compute.ExtendedStatistics();
                        if (Mode.Temporal && SubmissionIndex > FirstSubmission)
                        {
                            Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1u && Counts[20] > 0,
                                    "Moving direct replay did not continue capped temporal history");
                            MinimumTemporal = std::min(MinimumTemporal, Counts[20]);
                            ++ContinuedSubmissions;
                            ++CappedSubmissions;
                        }
                        if (SubmissionIndex + 1u < SubmissionCount) Measurement.WarmRays += Counts[8];
                        else
                        {
                            Measurement.FinalRays += Counts[8];
                            Measurement.BoxTests += Counts[18];
                            Measurement.TriangleTests += Counts[19];
                            Measurement.TemporalAccepted += Counts[20];
                            Measurement.SpatialLinks += Counts[22];
                            Require((Counts[20] > 0) == Mode.Temporal, "Motion temporal reuse did not match mode");
                            Require((Counts[22] > 0) == Mode.Spatial, "Motion spatial reuse did not match mode");
                            if (ModeIndex + 1u == Modes.size() && FloorIndex + 1u == Floors.size())
                            {
                                LastSeed = Seeds[SubmissionIndex];
                                LastResolveLinks = Counts[22];
                            }
                        }
                    }
                    std::array<double, 2> Sum{}, Squared{}, Maximum{};
                    std::array<uint32_t, 2> Counts{};
                    for (uint32_t Y = 0; Y < Size; ++Y)
                        for (uint32_t X = 0; X < Size; ++X)
                        {
                            const size_t Pixel = size_t(Y) * Size + X;
                            const uint32_t Group = (X + Y) % 2u;
                            const double Sample = double(Image[Pixel][0]) * Scales[Group];
                            Require(std::isfinite(Sample) && Sample >= 0, "Motion replay produced invalid radiance");
                            Sum[Group] += Sample;
                            Squared[Group] += (Sample - References[Group]) * (Sample - References[Group]);
                            Maximum[Group] = std::max(Maximum[Group], Sample);
                            ++Counts[Group];
                        }
                    for (uint32_t Group = 0; Group < 2u; ++Group)
                    {
                        Require(Counts[Group] == Size * Size / 2u, "Motion fixture group size changed");
                        const double Mean = Sum[Group] / double(Counts[Group]);
                        const double Mse = Squared[Group] / double(Counts[Group]);
                        auto& Result = Measurement.Groups[Group];
                        Result.Mean.Accumulate(Mean);
                        Result.Mse.Accumulate(Mse);
                        Result.MaximumPixel = std::max(Result.MaximumPixel, Maximum[Group]);
                        Result.MaximumBatchMean = std::max(Result.MaximumBatchMean, Mean);
                        std::printf("REPLAY_FRESH_MOTION_BATCH arm=%s scene=%s group=%s mode=%s floor=%s batch=%u "
                                    "mean=%.12g mse=%.12g max_pixel=%.12g hash=%016llx\n", Arm,
                                    SceneNames[SceneIndex], GroupNames[Group], Mode.Label, FloorNames[FloorIndex],
                                    Batch, Mean, Mse, Maximum[Group],
                                    static_cast<unsigned long long>(ReplayMaterialImageHash(Image, Size, Group)));
                    }
                }
            std::fflush(stdout);
        }
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
            for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
            {
                const auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
                for (uint32_t Group = 0; Group < 2u; ++Group)
                {
                    const auto& Result = Measurement.Groups[Group];
                    const bool MeanGate = std::abs(Result.Mean.Mean() - References[Group]) <=
                                          5.0 * Result.Mean.StandardError() + .001;
                    WithinReference = WithinReference && MeanGate;
                    std::printf("REPLAY_FRESH_MOTION_RESULT arm=%s scene=%s group=%s mode=%s floor=%s reference=%.9g "
                                "mean=%.9g batch_se=%.9g rmse=%.9g mse=%.9g mse_se=%.9g max_pixel=%.9g "
                                "max_batch_mean=%.9g mean_rays=%.3f warm_rays=%.3f mean_boxes=%.3f "
                                "mean_triangles=%.3f temporal=%llu spatial=%llu mean_gate=%u scale=%.3g "
                                "batches=%u pixels_per_group=%u\n", Arm, SceneNames[SceneIndex], GroupNames[Group],
                                Modes[ModeIndex].Label, FloorNames[FloorIndex], References[Group], Result.Mean.Mean(),
                                Result.Mean.StandardError(), std::sqrt(Result.Mse.Mean()), Result.Mse.Mean(),
                                Result.Mse.StandardError(), Result.MaximumPixel, Result.MaximumBatchMean,
                                double(Measurement.FinalRays) / Batches, double(Measurement.WarmRays) / Batches,
                                double(Measurement.BoxTests) / Batches, double(Measurement.TriangleTests) / Batches,
                                static_cast<unsigned long long>(Measurement.TemporalAccepted),
                                static_cast<unsigned long long>(Measurement.SpatialLinks), unsigned(MeanGate),
                                Scales[Group], Batches, Size * Size / 2u);
                }
            }
        std::printf("REPLAY_FRESH_MOTION_CONTINUATION arm=%s scene=%s continued_frames=%u capped_frames=%u "
                    "minimum_temporal=%u\n", Arm, SceneNames[SceneIndex], ContinuedSubmissions, CappedSubmissions,
                    MinimumTemporal);
        std::fflush(stdout);
        InspectReplayMaterialGuardLinks(Compute, Shaders, Size, LastSeed, LastResolveLinks, Mixed, false, Arm);
        ValidateReplayFreshMotionSequence(Compute, Scenes, Arm);
    }
    Require(WithinReference, "Moving fresh-donor holdout failed the independent mean gates");
    std::printf("PASS ReplayFreshDonorMotionValidation arm=%s\n", Arm);
    std::fflush(stdout);
}
