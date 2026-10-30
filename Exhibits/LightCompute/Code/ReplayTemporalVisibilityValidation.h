/*==========================================================================================================================================
                                      REPLAYTEMPORALVISIBILITYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Attribute moving-visibility temporal history weight to the exact replayed path and its visibility rays.
#pragma once

void ValidateReplayTemporalVisibility(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 32u, Pixels = Size * Size, Cohorts = 128u, Frames = 4u;
    constexpr uint32_t FarGroup = 1u;
    constexpr float Floor = .0001f;
    constexpr const char* Scenario = "motion_visibility_far";
    const std::array<ReplayTemporalProvenanceMode, 2> Modes{{{"temporal1", 1u}, {"temporal4", 4u}}};

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
            "Visibility-audit reference failed its convergence check");

    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_TEMPORAL_VISIBILITY scenario=%s floor=lower reference=%.12g coarse=%.12g x=%.9g "
                "preparing_on=%s\n", Scenario, Reference, Coarse, PositionX, Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true, false, false, true);
    std::printf("REPLAY_TEMPORAL_VISIBILITY pipelines_ready=1 cohorts=%u pixels_per_cohort=%u\n", Cohorts, Pixels / 2u);
    std::fflush(stdout);

    struct ModeTotals
    {
        uint64_t Pixels = 0, DirectRays = 0, DirectVisible = 0, EnvironmentEscapes = 0, OpaqueHits = 0;
        uint64_t EmitterHits = 0, Selected = 0, VisibleSamples = 0, OccludedSamples = 0;
        uint64_t PriorLitCurrentBlocked = 0, PriorBlockedCurrentLit = 0, StableLit = 0, StableBlocked = 0;
        double Nee = 0, EmitterContribution = 0, PathRadiance = 0;
        double Target = 0, SourceWeight = 0, Retained = 0, HistoryWeight = 0, FreshWeight = 0;
        ReplayMaterialBatchMoments Mean, Mse;
        std::vector<double> VisibleTargets, OccludedTargets, VisibleWeights, OccludedWeights;
        std::vector<double> VisibleNee, OccludedNee, VisibleEmitter, OccludedEmitter;
    };
    std::array<ModeTotals, Modes.size()> Totals{};
    std::mt19937_64 Random(0xd7a4c1e95b830621ull);
    uint64_t PixelRows = 0;
    for (uint32_t Cohort = 0; Cohort < Cohorts; ++Cohort)
    {
        std::array<uint32_t, Frames> Seeds{};
        for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
        {
            const auto& Mode = Modes[ModeIndex];
            auto& Total = Totals[ModeIndex];
            std::vector<std::array<float, 4>> Image;
            for (uint32_t Frame = 0; Frame < Frames; ++Frame)
            {
                Image = ExecuteReplayMaterialFloor(Compute, Scenes[Frame],
                    ReplayMaterialReuseMode{Mode.Label, Mode.Candidates, true, false},
                    Seeds[Frame], Frame == 0u, Floor);
                Require(Compute.ReplayLastHistoryCap == 1u,
                        "Visibility audit requires a one-entry temporal history cap");
            }

            const auto Visibility = Compute.ReadReplayLogicalSlab(5u, false);
            const auto Contributions = Compute.ReadReplayLogicalSlab(6u, false);
            const auto Source = Compute.ReadReplayLogicalSlab(7u, false);
            const auto Choice = Compute.ReadReplayLogicalSlab(8u, false);
            double Sum = 0, Squared = 0;
            uint32_t GroupPixels = 0;
            uint64_t BatchRays = 0, BatchVisible = 0, BatchMisses = 0, BatchOpaque = 0;
            uint64_t BatchEmitters = 0, BatchSelected = 0;
            double BatchNee = 0, BatchEmitterContribution = 0, BatchHistoryWeight = 0;
            for (uint32_t Pixel = 0; Pixel < Pixels; ++Pixel)
            {
                const uint32_t X = Pixel % Size, Y = Pixel / Size;
                if ((X + Y) % 2u != FarGroup) continue;
                const auto& V = Visibility[Pixel];
                const auto& C = Contributions[Pixel];
                const auto& S = Source[Pixel];
                const auto& Q = Choice[Pixel];
                const double Rays = double(V[0]), Visible = double(V[1]);
                const double EnvironmentMiss = double(V[2]), OpaqueHit = double(V[3]);
                const double Nee = double(C[0]), Emitter = double(C[1]);
                const double Radiance = double(C[2]), EmitterHits = double(C[3]);
                const uint32_t Seed = uint32_t(S[0]) | (uint32_t(S[1]) << 16u);
                const double SourceWeight = double(S[2]), SourceCount = double(S[3]);
                const double Retained = std::min(SourceCount, 1.0);
                const double HistoryWeight = double(Q[0]), Probability = double(Q[1]);
                const double PriorPathRadiance = double(Q[2]), Selected = double(Q[3]);
                const double FreshWeight = Probability > 0 && Probability < 1
                    ? HistoryWeight * (1.0 - Probability) / Probability : 0.0;
                const double Target = std::max(0.0, Radiance) + Floor;
                auto FiniteNonnegative = [](double Value) { return std::isfinite(Value) && Value >= 0; };
                Require(FiniteNonnegative(Rays) && Rays <= 1 && FiniteNonnegative(Visible) && Visible <= Rays &&
                        FiniteNonnegative(EnvironmentMiss) && EnvironmentMiss <= 1 &&
                        FiniteNonnegative(OpaqueHit) && OpaqueHit <= 1 &&
                        EnvironmentMiss + OpaqueHit + EmitterHits <= 1 &&
                        FiniteNonnegative(Nee) && FiniteNonnegative(Emitter) && FiniteNonnegative(Radiance) &&
                        FiniteNonnegative(EmitterHits) && EmitterHits <= 1 &&
                        std::isfinite(SourceWeight) && SourceWeight > 0 && std::isfinite(SourceCount) &&
                        SourceCount >= 1 && SourceCount <= 68 && std::floor(SourceCount) == SourceCount &&
                        std::isfinite(Retained) && Retained >= 1 &&
                        std::isfinite(HistoryWeight) && HistoryWeight > 0 && FiniteNonnegative(FreshWeight) &&
                        FiniteNonnegative(PriorPathRadiance) && (Probability > 0 && Probability < 1) &&
                        (Selected == 0 || Selected == 1), "Per-path visibility audit contains an invalid readback");
                const double ExpectedWeight = Target * SourceWeight * Retained;
                Require(std::abs(ExpectedWeight - HistoryWeight) <= 5e-6 * std::max(1.0, ExpectedWeight),
                        "Temporal candidate weight disagrees with replayed target × stored weight × retained count");
                const double ExpectedProbability = HistoryWeight / (FreshWeight + HistoryWeight);
                Require(std::abs(ExpectedProbability - Probability) <= 5e-6,
                        "Temporal selection probability disagrees with logged fresh and history weights");
                Require(std::abs(Target - (std::max(0.0, Radiance) + Floor)) <= 5e-6 * std::max(1.0, Target),
                        "Per-path target disagrees with replayed radiance and configured floor");

                ++GroupPixels;
                ++Total.Pixels;
                ++PixelRows;
                BatchRays += uint64_t(Rays);
                BatchVisible += uint64_t(Visible);
                BatchMisses += uint64_t(EnvironmentMiss);
                BatchOpaque += uint64_t(OpaqueHit);
                BatchEmitters += uint64_t(EmitterHits);
                BatchSelected += uint64_t(Selected);
                BatchNee += Nee;
                BatchEmitterContribution += Emitter;
                BatchHistoryWeight += HistoryWeight;
                Total.DirectRays += uint64_t(Rays);
                Total.DirectVisible += uint64_t(Visible);
                Total.EnvironmentEscapes += uint64_t(EnvironmentMiss);
                Total.OpaqueHits += uint64_t(OpaqueHit);
                Total.EmitterHits += uint64_t(EmitterHits);
                Total.Selected += uint64_t(Selected);
                Total.Nee += Nee;
                Total.EmitterContribution += Emitter;
                Total.PathRadiance += Radiance;
                Total.Target += Target;
                Total.SourceWeight += SourceWeight;
                Total.Retained += Retained;
                Total.HistoryWeight += HistoryWeight;
                Total.FreshWeight += FreshWeight;
                const bool PriorLit = PriorPathRadiance > 1e-8;
                const bool CurrentLit = Radiance > 1e-8;
                if (PriorLit && !CurrentLit) ++Total.PriorLitCurrentBlocked;
                else if (!PriorLit && CurrentLit) ++Total.PriorBlockedCurrentLit;
                else if (PriorLit) ++Total.StableLit;
                else ++Total.StableBlocked;
                if (EnvironmentMiss > 0 || EmitterHits > 0)
                {
                    ++Total.VisibleSamples;
                    Total.VisibleTargets.push_back(Target);
                    Total.VisibleWeights.push_back(HistoryWeight);
                    Total.VisibleNee.push_back(Nee);
                    Total.VisibleEmitter.push_back(Emitter);
                }
                else if (OpaqueHit > 0)
                {
                    ++Total.OccludedSamples;
                    Total.OccludedTargets.push_back(Target);
                    Total.OccludedWeights.push_back(HistoryWeight);
                    Total.OccludedNee.push_back(Nee);
                    Total.OccludedEmitter.push_back(Emitter);
                }
                const double Sample = double(Image[Pixel][0]);
                Require(std::isfinite(Sample) && Sample >= 0, "Visibility audit replay returned invalid radiance");
                Sum += Sample;
                Squared += (Sample - Reference) * (Sample - Reference);
                std::printf("REPLAY_TEMPORAL_VISIBILITY_PIXEL mode=%s cohort=%u pixel=%u x=%u y=%u seed=%08x "
                            "direct_rays=%.0f direct_visible=%.0f bsdf_miss=%.0f first_opaque_hit=%.0f "
                            "nee_luma=%.9g emitter_hits=%.0f emitter_luma=%.9g path_luma=%.9g target=%.9g "
                            "source_weight=%.9g source_count=%.0f retained=%.9g history_weight=%.9g fresh_weight=%.9g "
                            "previous_path_luma=%.9g selection_probability=%.9g selected=%.0f\n",
                            Mode.Label, Cohort, Pixel, X, Y, Seed, Rays, Visible, EnvironmentMiss, OpaqueHit, Nee,
                            EmitterHits, Emitter, Radiance, Target, SourceWeight, SourceCount, Retained, HistoryWeight,
                            FreshWeight, PriorPathRadiance, Probability, Selected);
            }
            Require(GroupPixels == Pixels / 2u, "Visibility audit far-group coverage changed");
            const double Mean = Sum / GroupPixels, Mse = Squared / GroupPixels;
            Total.Mean.Accumulate(Mean);
            Total.Mse.Accumulate(Mse);
            std::printf("REPLAY_TEMPORAL_VISIBILITY_COHORT mode=%s cohort=%u pixels=%u mean=%.12g mse=%.12g "
                        "hash=%016llx direct_rays=%llu direct_visible=%llu bsdf_miss=%llu first_opaque_hit=%llu "
                        "emitter_hits=%llu nee_luma=%.12g "
                        "emitter_luma=%.12g history_weight=%.12g\n",
                        Mode.Label, Cohort, GroupPixels, Mean, Mse,
                        static_cast<unsigned long long>(ReplayMaterialImageHash(Image, Size, FarGroup)),
                        static_cast<unsigned long long>(BatchRays), static_cast<unsigned long long>(BatchVisible),
                        static_cast<unsigned long long>(BatchMisses), static_cast<unsigned long long>(BatchOpaque),
                        static_cast<unsigned long long>(BatchEmitters), BatchNee, BatchEmitterContribution,
                        BatchHistoryWeight);
            (void)BatchSelected;
        }
        std::fflush(stdout);
    }
    Require(PixelRows == uint64_t(Cohorts) * Modes.size() * Pixels / 2u,
            "Per-pixel visibility audit row coverage incomplete");

    for (size_t Index = 0; Index < Modes.size(); ++Index)
    {
        auto& T = Totals[Index];
        const double VisibilityRate = T.DirectRays ? double(T.DirectVisible) / T.DirectRays : 0;
        std::printf("REPLAY_TEMPORAL_VISIBILITY_RESULT mode=%s scenario=%s reference=%.12g pixels=%llu "
                    "direct_rays=%llu direct_visible=%llu direct_visibility_rate=%.9g bsdf_miss=%llu "
                    "first_opaque_hit=%llu emitter_hits=%llu bsdf_visible_outcome_rate=%.9g "
                    "mean_nee_luma=%.9g mean_emitter_hits=%.9g "
                    "mean_emitter_luma=%.9g mean_path_luma=%.9g mean_target=%.9g mean_source_weight=%.9g "
                    "mean_retained=%.9g mean_history_weight=%.9g mean_fresh_weight=%.9g selected=%llu "
                    "visible_samples=%llu occluded_samples=%llu visible_mean_target=%.9g "
                    "occluded_mean_target=%.9g visible_mean_weight=%.9g occluded_mean_weight=%.9g "
                    "visible_mean_nee=%.9g occluded_mean_nee=%.9g visible_mean_emitter=%.9g "
                    "occluded_mean_emitter=%.9g prior_lit_current_blocked=%llu prior_blocked_current_lit=%llu "
                    "stable_lit=%llu stable_blocked=%llu mean=%.12g mean_se=%.12g rmse=%.12g\n",
                    Modes[Index].Label, Scenario, Reference, static_cast<unsigned long long>(T.Pixels),
                    static_cast<unsigned long long>(T.DirectRays), static_cast<unsigned long long>(T.DirectVisible),
                    VisibilityRate, static_cast<unsigned long long>(T.EnvironmentEscapes),
                    static_cast<unsigned long long>(T.OpaqueHits), static_cast<unsigned long long>(T.EmitterHits),
                    (T.EnvironmentEscapes + T.EmitterHits + T.OpaqueHits) > 0
                        ? double(T.EnvironmentEscapes + T.EmitterHits) /
                          double(T.EnvironmentEscapes + T.EmitterHits + T.OpaqueHits) : 0,
                    T.Nee / T.Pixels, double(T.EmitterHits) / T.Pixels, T.EmitterContribution / T.Pixels,
                    T.PathRadiance / T.Pixels, T.Target / T.Pixels, T.SourceWeight / T.Pixels, T.Retained / T.Pixels,
                    T.HistoryWeight / T.Pixels, T.FreshWeight / T.Pixels,
                    static_cast<unsigned long long>(T.Selected),
                    static_cast<unsigned long long>(T.VisibleSamples),
                    static_cast<unsigned long long>(T.OccludedSamples),
                    T.VisibleTargets.empty() ? 0 : std::accumulate(T.VisibleTargets.begin(), T.VisibleTargets.end(), 0.0) / T.VisibleTargets.size(),
                    T.OccludedTargets.empty() ? 0 : std::accumulate(T.OccludedTargets.begin(), T.OccludedTargets.end(), 0.0) / T.OccludedTargets.size(),
                    T.VisibleWeights.empty() ? 0 : std::accumulate(T.VisibleWeights.begin(), T.VisibleWeights.end(), 0.0) / T.VisibleWeights.size(),
                    T.OccludedWeights.empty() ? 0 : std::accumulate(T.OccludedWeights.begin(), T.OccludedWeights.end(), 0.0) / T.OccludedWeights.size(),
                    T.VisibleNee.empty() ? 0 : std::accumulate(T.VisibleNee.begin(), T.VisibleNee.end(), 0.0) / T.VisibleNee.size(),
                    T.OccludedNee.empty() ? 0 : std::accumulate(T.OccludedNee.begin(), T.OccludedNee.end(), 0.0) / T.OccludedNee.size(),
                    T.VisibleEmitter.empty() ? 0 : std::accumulate(T.VisibleEmitter.begin(), T.VisibleEmitter.end(), 0.0) / T.VisibleEmitter.size(),
                    T.OccludedEmitter.empty() ? 0 : std::accumulate(T.OccludedEmitter.begin(), T.OccludedEmitter.end(), 0.0) / T.OccludedEmitter.size(),
                    static_cast<unsigned long long>(T.PriorLitCurrentBlocked),
                    static_cast<unsigned long long>(T.PriorBlockedCurrentLit),
                    static_cast<unsigned long long>(T.StableLit), static_cast<unsigned long long>(T.StableBlocked),
                    T.Mean.Mean(), T.Mean.StandardError(), std::sqrt(T.Mse.Mean()));
    }
    std::printf("PASS ReplayTemporalVisibilityValidation rows=%llu same_seed_prior_current_pairs=verified estimator_changed=0\n",
                static_cast<unsigned long long>(PixelRows));
    std::printf("REPLAY_TEMPORAL_VISIBILITY_EXIT status=0\n");
    std::fflush(stdout);
}

// Add a uniformly sampled area emitter above the moving receiver/occluder holdout.
// The emitter is test-only and deliberately absent from the original baseline fixture.
LightComputeArchive AddReplayTemporalVisibilityEmitter(LightComputeArchive Scene)
{
    auto Position = [](const std::array<float, 4>& P) { return SpatialVector{P[0], P[1], P[2]}; };
    std::vector<LightComputeFacet> Surfaces = Scene.Surfaces;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    Geometry.reserve(Surfaces.size() + 2u);
    for (const auto& F : Surfaces)
        Geometry.push_back({Position(F.A), Position(F.B), Position(F.C)});
    const SpatialVector Down{0, 0, -1};
    AppendPbrQuad(Surfaces, Geometry, {-.35, -.35, 1.5}, {.35, -.35, 1.5},
                  {.35, .35, 1.5}, {-.35, .35, 1.5}, Down, Down, Down, Down,
                  0u, 4.0f, {0.1f, 0.1f, 0.1f}, .7f, 0);
    AppendPbrQuad(Surfaces, Geometry, {-2, -2, 3}, {2, -2, 3}, {2, 2, 3}, {-2, 2, 3},
                  Down, Down, Down, Down, 0u, 3.0f, {0.5f, 0.5f, 0.5f}, .7f, 0,
                  {10.0f, 8.0f, 5.0f});
    TriangleVisibilitySpace Space;
    Space.BuildSurfaceAreaHierarchy(Geometry, 1u);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.clear();
    Scene.Surfaces.reserve(Scene.Visibility.OriginalFacets.size());
    for (auto Index : Scene.Visibility.OriginalFacets)
        Scene.Surfaces.push_back(Surfaces[Index]);
    const auto EmitterCount = std::count_if(Scene.Surfaces.begin(), Scene.Surfaces.end(),
        [](const auto& F) { return F.Emissive[0] > 0 || F.Emissive[1] > 0 || F.Emissive[2] > 0; });
    Require(EmitterCount == 2 && Scene.Surfaces.size() == Scene.Visibility.Corners.size(),
            "Direct-visibility fixture did not retain its two emitter facets in the visibility geometry");
    return Scene;
}

void ValidateReplayTemporalDirectEmitterEstimate(LightComputeSubmission& Compute,
                                                 const LightComputeArchive& Material,
                                                 const std::array<LightComputeArchive, 4>& Scenes,
                                                 uint32_t Cohorts, uint32_t Size, uint32_t FarGroup, float Floor);

void ValidateReplayTemporalDirectEmitterEstimateOnly(const char* Driver, const std::string& Shaders,
                                                       bool Stationary = false)
{
    constexpr uint32_t Size = 32u, Cohorts = 32u, FarGroup = 1u;
    constexpr float Floor = .0001f;
    auto DimSpecification = EnumerateReplayMaterialSpecifications()[4];
    DimSpecification.Coverage = 1e-3f;
    DimSpecification.CoatCoverage = 2e-3f;
    DimSpecification.ReportingScale = 1e3;
    const auto Dim = ConstructReplayMaterialReuseFixture(Size, DimSpecification);
    const auto Metal = ConstructReplayMaterialReuseFixture(Size, EnumerateReplayMaterialSpecifications()[1]);
    const auto Capacity = AddReplayTemporalVisibilityEmitter(ConstructReplayMixedMaterialFixture(Size, Dim, Metal));
    auto Scenes = ConstructReplayFreshMotionScenes(Size, Dim, Metal, false);
    for (auto& Scene : Scenes) Scene = AddReplayTemporalVisibilityEmitter(std::move(Scene));
    if (Stationary)
    {
        const auto FinalScene = Scenes.back();
        for (auto& Scene : Scenes)
        {
            Scene = FinalScene;
            for (auto& Receiver : Scene.Receivers)
                Receiver.MotionPositionToken[0] = Receiver.PositionHit[0];
        }
    }
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_TEMPORAL_DIRECT_ESTIMATE fixture=overhead_emitter motion=%s receiver_x=%.9g "
                "preparing_on=%s\n", Stationary ? "stationary" : "moving",
                double(Scenes.back().Receivers[1].PositionHit[0]), Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true, false, false, true);
    ValidateReplayTemporalDirectEmitterEstimate(Compute, Metal, Scenes, Cohorts, Size, FarGroup, Floor);
}

void ValidateReplayTemporalDirectVisibility(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 32u, Pixels = Size * Size, Cohorts = 32u, Frames = 4u;
    constexpr uint32_t FarGroup = 1u;
    constexpr float Floor = .0001f;
    const std::array<ReplayMaterialReuseMode, 2> Modes{{
        {"temporal1", 1u, true, false}, {"temporal4", 4u, true, false}}};

    auto DimSpecification = EnumerateReplayMaterialSpecifications()[4];
    DimSpecification.Coverage = 1e-3f;
    DimSpecification.CoatCoverage = 2e-3f;
    DimSpecification.ReportingScale = 1e3;
    const auto Dim = ConstructReplayMaterialReuseFixture(Size, DimSpecification);
    const auto Metal = ConstructReplayMaterialReuseFixture(Size, EnumerateReplayMaterialSpecifications()[1]);
    const auto Capacity = AddReplayTemporalVisibilityEmitter(ConstructReplayMixedMaterialFixture(Size, Dim, Metal));
    auto Scenes = ConstructReplayFreshMotionScenes(Size, Dim, Metal, false);
    for (auto& Scene : Scenes) Scene = AddReplayTemporalVisibilityEmitter(std::move(Scene));

    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_TEMPORAL_VISIBILITY_DIRECT fixture=overhead_emitter moving_receivers=1 facets=2 "
                "emission=10,8,5 preparing_on=%s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true, false, false, true);
    std::printf("REPLAY_TEMPORAL_VISIBILITY_DIRECT pipelines_ready=1 cohorts=%u pixels_per_cohort=%u "
                "bounces=1 source_sampling=uniform\n", Cohorts, Pixels / 2u);
    std::fflush(stdout);

    std::array<std::array<uint64_t, 9>, Modes.size()> Transitions{};
    std::array<std::array<uint64_t, 3>, Modes.size()> OutcomeCounts{};
    std::array<std::array<double, 3>, Modes.size()> NeeSums{}, TargetSums{}, HistoryWeightSums{};
    std::array<uint64_t, Modes.size()> RayAttempts{}, RayVisible{};
    std::mt19937_64 Random(0xd7a4c1e95b830621ull);
    uint64_t Rows = 0;
    for (uint32_t Cohort = 0; Cohort < Cohorts; ++Cohort)
    {
        std::array<uint32_t, Frames> Seeds{};
        for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
        {
            const auto& Mode = Modes[ModeIndex];
            std::vector<std::array<float, 4>> Image;
            for (uint32_t Frame = 0; Frame < Frames; ++Frame)
            {
                Image = ExecuteReplayMaterialFloor(Compute, Scenes[Frame], Mode, Seeds[Frame],
                                                   Frame == 0u, Floor, false, true);
                Require(Compute.ReplayLastHistoryCap == 1u,
                        "Direct visibility audit requires a one-entry temporal history cap");
                if (Frame > 0u)
                    Require(Compute.ExtendedStatistics()[20] == Pixels,
                            "Direct visibility fixture failed to pair every same-seed temporal candidate");
            }

            const auto Visibility = Compute.ReadReplayLogicalSlab(5u, false);
            const auto Contributions = Compute.ReadReplayLogicalSlab(6u, false);
            const auto Source = Compute.ReadReplayLogicalSlab(7u, false);
            const auto Choice = Compute.ReadReplayLogicalSlab(8u, false);
            const auto PreviousDirect = Compute.ReadReplayLogicalSlab(11u, false);
            for (uint32_t Pixel = 0; Pixel < Pixels; ++Pixel)
            {
                const uint32_t X = Pixel % Size, Y = Pixel / Size;
                if ((X + Y) % 2u != FarGroup) continue;
                const auto& V = Visibility[Pixel];
                const auto& C = Contributions[Pixel];
                const auto& S = Source[Pixel];
                const auto& Q = Choice[Pixel];
                const double Rays = V[0], Visible = V[1];
                const double Nee = C[0], Radiance = C[2];
                const double HistoryWeight = Q[0], Probability = Q[1], PreviousPathLuma = Q[2];
                const double SourceWeight = S[2], SourceCount = S[3];
                const double Retained = std::min(SourceCount, 1.0);
                const double Target = std::max(0.0, Radiance) + Floor;
                const double FreshWeight = Probability > 0 && Probability < 1
                    ? HistoryWeight * (1.0 - Probability) / Probability : 0.0;
                const int PreviousCode = int(PreviousDirect[Pixel][0]);
                const int CurrentCode = Rays == 0 ? 0 : (Visible > 0 ? 2 : 1);
                Require(std::isfinite(Rays) && (Rays == 0 || Rays == 1) &&
                            std::isfinite(Visible) && (Visible == 0 || Visible == 1) && Visible <= Rays &&
                            PreviousCode >= 0 && PreviousCode <= 2 && PreviousDirect[Pixel][0] == float(PreviousCode) &&
                            std::isfinite(PreviousPathLuma) && PreviousPathLuma >= 0 &&
                            std::isfinite(Nee) && Nee >= 0 && (CurrentCode == 2 || Nee == 0) &&
                            std::isfinite(Radiance) && Radiance >= 0 &&
                            std::isfinite(HistoryWeight) && HistoryWeight > 0 &&
                            std::isfinite(SourceWeight) && SourceWeight > 0 &&
                            std::isfinite(SourceCount) && SourceCount >= Retained && Retained == 1 &&
                            std::isfinite(Probability) && Probability > 0 && Probability < 1 &&
                            std::isfinite(FreshWeight) && FreshWeight > 0,
                        "Direct visibility per-path readback is malformed or lost its same-seed predecessor");
                Require(std::abs(HistoryWeight - Target * SourceWeight * Retained) <=
                            5e-6 * std::max(1.0, HistoryWeight),
                        "Direct visibility history weight disagrees with current target and stored source weight");
                Require(std::abs(Probability - HistoryWeight / (HistoryWeight +
                            HistoryWeight * (1.0 - Probability) / Probability)) <= 5e-6,
                        "Direct visibility selection probability disagrees with its reservoir weights");
                ++Transitions[size_t(ModeIndex)][size_t(PreviousCode * 3 + CurrentCode)];
                ++OutcomeCounts[size_t(ModeIndex)][size_t(CurrentCode)];
                NeeSums[size_t(ModeIndex)][size_t(CurrentCode)] += Nee;
                TargetSums[size_t(ModeIndex)][size_t(CurrentCode)] += Target;
                HistoryWeightSums[size_t(ModeIndex)][size_t(CurrentCode)] += HistoryWeight;
                RayAttempts[size_t(ModeIndex)] += uint64_t(Rays);
                RayVisible[size_t(ModeIndex)] += uint64_t(Visible);
                ++Rows;
                std::printf("REPLAY_TEMPORAL_VISIBILITY_DIRECT_PIXEL mode=%s cohort=%u pixel=%u x=%u y=%u "
                            "seed=%08x prior_direct_code=%d current_direct_code=%d direct_rays=%.0f "
                            "direct_visible=%.0f nee_luma=%.9g target=%.9g source_weight=%.9g "
                            "source_count=%.9g retained=%.9g history_weight=%.9g fresh_weight=%.9g "
                            "selection_probability=%.9g previous_path_luma=%.9g current_path_luma=%.9g\n",
                            Mode.Label, Cohort, Pixel, X, Y,
                            uint32_t(S[0]) | (uint32_t(S[1]) << 16u), PreviousCode, CurrentCode,
                            Rays, Visible, Nee, Target, SourceWeight, SourceCount, Retained, HistoryWeight,
                            FreshWeight, Probability, PreviousPathLuma, Radiance);
            }
            (void)Image;
        }
        std::fflush(stdout);
    }
    Require(Rows == uint64_t(Cohorts) * Modes.size() * Pixels / 2u,
            "Direct visibility same-seed pixel coverage was incomplete");

    for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
    {
        const auto& Mode = Modes[ModeIndex];
        const auto& T = Transitions[ModeIndex];
        const auto& N = OutcomeCounts[ModeIndex];
        const uint64_t ModeRows = Rows / Modes.size();
        Require(RayAttempts[ModeIndex] == ModeRows && RayVisible[ModeIndex] > 0 &&
                    RayVisible[ModeIndex] < RayAttempts[ModeIndex] && N[1] > 0 && N[2] > 0 &&
                    T[5] + T[7] > 0,
                "Controlled emitter fixture failed to produce blocked/visible direct rays and temporal transitions");
        auto OutcomeMean = [&](const std::array<double, 3>& Values, size_t Outcome)
        { return N[Outcome] ? Values[Outcome] / double(N[Outcome]) : 0.0; };
        std::printf("REPLAY_TEMPORAL_VISIBILITY_DIRECT_RESULT mode=%s pixels=%llu direct_rays=%llu "
                    "direct_visible=%llu no_ray=%llu blocked=%llu visible=%llu "
                    "transition_00=%llu transition_01=%llu transition_02=%llu "
                    "transition_10=%llu transition_11=%llu transition_12=%llu "
                    "transition_20=%llu transition_21=%llu transition_22=%llu "
                    "no_ray_mean_nee=%.9g blocked_mean_nee=%.9g visible_mean_nee=%.9g "
                    "no_ray_mean_target=%.9g blocked_mean_target=%.9g visible_mean_target=%.9g "
                    "no_ray_mean_history_weight=%.9g blocked_mean_history_weight=%.9g "
                    "visible_mean_history_weight=%.9g\n",
                    Mode.Label, static_cast<unsigned long long>(Rows / Modes.size()),
                    static_cast<unsigned long long>(RayAttempts[ModeIndex]),
                    static_cast<unsigned long long>(RayVisible[ModeIndex]),
                    static_cast<unsigned long long>(N[0]), static_cast<unsigned long long>(N[1]),
                    static_cast<unsigned long long>(N[2]),
                    static_cast<unsigned long long>(T[0]), static_cast<unsigned long long>(T[1]),
                    static_cast<unsigned long long>(T[2]), static_cast<unsigned long long>(T[3]),
                    static_cast<unsigned long long>(T[4]), static_cast<unsigned long long>(T[5]),
                    static_cast<unsigned long long>(T[6]), static_cast<unsigned long long>(T[7]),
                    static_cast<unsigned long long>(T[8]),
                    OutcomeMean(NeeSums[ModeIndex], 0), OutcomeMean(NeeSums[ModeIndex], 1),
                    OutcomeMean(NeeSums[ModeIndex], 2), OutcomeMean(TargetSums[ModeIndex], 0),
                    OutcomeMean(TargetSums[ModeIndex], 1), OutcomeMean(TargetSums[ModeIndex], 2),
                    OutcomeMean(HistoryWeightSums[ModeIndex], 0), OutcomeMean(HistoryWeightSums[ModeIndex], 1),
                    OutcomeMean(HistoryWeightSums[ModeIndex], 2));
    }
    std::printf("PASS ReplayTemporalDirectVisibilityValidation rows=%llu same_seed_prior_current_pairs=verified "
                "estimator_changed=0 test_only_emitter=1\n", static_cast<unsigned long long>(Rows));
    std::fflush(stdout);
    ValidateReplayTemporalDirectEmitterEstimate(Compute, Metal, Scenes, Cohorts, Size, FarGroup, Floor);
}

double IntegrateReplayTemporalDirectEmitterReference(const LightComputeArchive& Material,
                                                      double ReceiverX, uint32_t Steps)
{
    constexpr double EmitterHalf = 2.0, EmitterZ = 3.0, OriginOffset = .001;
    constexpr double EmissionRed = 10.0;
    constexpr std::array<double, 2> BlockerZ{1.0, 1.5};
    constexpr std::array<double, 2> BlockerCenterX{-.75, 0.0};
    constexpr std::array<double, 2> BlockerHalfX{.5, .35};
    constexpr std::array<double, 2> BlockerHalfY{.5, .35};
    const auto& Receiver = Material.Receivers[0];
    const double ViewLength = std::sqrt(double(Receiver.ViewMetallic[0]) * Receiver.ViewMetallic[0] +
                                        double(Receiver.ViewMetallic[2]) * Receiver.ViewMetallic[2]);
    const double ViewX = double(Receiver.ViewMetallic[0]) / ViewLength;
    const double ViewZ = double(Receiver.ViewMetallic[2]) / ViewLength;
    const double Cell = 2.0 * EmitterHalf / double(Steps);
    double Integral = 0.0;
    for (uint32_t Y = 0; Y < Steps; ++Y)
        for (uint32_t X = 0; X < Steps; ++X)
        {
            const double SourceX = -EmitterHalf + (double(X) + .5) * Cell;
            const double SourceY = -EmitterHalf + (double(Y) + .5) * Cell;
            const double Dx = SourceX - ReceiverX, Dy = SourceY;
            const double Dz = EmitterZ - OriginOffset;
            const double DistanceSquared = Dx * Dx + Dy * Dy + Dz * Dz;
            const double Distance = std::sqrt(DistanceSquared);
            const double LightX = Dx / Distance, LightY = Dy / Distance, LightZ = Dz / Distance;
            const double CosReceiver = LightZ;
            const double CosEmitter = LightZ;
            const double HalfX = ViewX + LightX, HalfY = LightY, HalfZ = ViewZ + LightZ;
            const double HalfLength = std::sqrt(HalfX * HalfX + HalfY * HalfY + HalfZ * HalfZ);
            const double NormalHalf = HalfZ / HalfLength;
            const double ViewHalf = std::clamp((ViewX * HalfX + ViewZ * HalfZ) / HalfLength, 0.0, 1.0);
            const double Brdf = EvaluateReplayMaterialReference(Material, ViewZ, LightZ, NormalHalf, ViewHalf);
            bool Occluded = false;
            for (size_t Blocker = 0; Blocker < BlockerZ.size(); ++Blocker)
            {
                const double T = (BlockerZ[Blocker] - OriginOffset) / Dz;
                const double HitX = ReceiverX + T * Dx - BlockerCenterX[Blocker];
                const double HitY = T * Dy;
                if (std::abs(HitX) <= BlockerHalfX[Blocker] && std::abs(HitY) <= BlockerHalfY[Blocker])
                {
                    Occluded = true;
                    break;
                }
            }
            if (!Occluded)
                Integral += EmissionRed * Brdf * CosReceiver * CosEmitter / DistanceSquared * Cell * Cell;
        }
    return Integral;
}

void ValidateReplayTemporalDirectEmitterEstimate(LightComputeSubmission& Compute,
                                                 const LightComputeArchive& Material,
                                                 const std::array<LightComputeArchive, 4>& Scenes,
                                                 uint32_t Cohorts, uint32_t Size, uint32_t FarGroup, float Floor)
{
    constexpr uint32_t Frames = 4u;
    constexpr double ReferenceTolerance = .005;
    const uint32_t Pixels = Size * Size;
    const std::array<ReplayMaterialReuseMode, 4> Modes{{
        {"fresh1", 1u, false, false}, {"fresh4", 4u, false, false},
        {"temporal1", 1u, true, false}, {"temporal4", 4u, true, false}}};
    const double ReceiverX = Scenes.back().Receivers[1].PositionHit[0];
    Require(std::abs(ReceiverX - 1.35) < 2e-6,
            "Direct-emitter reference receiver no longer matches the far moving holdout");
    constexpr double EmitterPower = 10.0 * .2126 + 8.0 * .7152 + 5.0 * .0722;
    const double Reference = IntegrateReplayTemporalDirectEmitterReference(Material, ReceiverX, 2048u) *
                             (EmitterPower / 10.0);
    const double Coarse = IntegrateReplayTemporalDirectEmitterReference(Material, ReceiverX, 1024u) *
                          (EmitterPower / 10.0);
    const double ReferenceDelta = std::abs(Reference - Coarse);
    const double GateTolerance = ReferenceTolerance * std::max(1.0, std::abs(Reference));
    Require(std::isfinite(Reference) && Reference > 0 && std::isfinite(Coarse) && Coarse > 0 &&
                ReferenceDelta <= GateTolerance,
            "Independent direct-emitter quadrature failed its predeclared convergence gate");

    const char* Motion = std::abs(double(Scenes.front().Receivers[1].PositionHit[0]) -
                                  double(Scenes.back().Receivers[1].PositionHit[0])) < 2e-6
        ? "stationary" : "moving";
    std::array<ReplayMaterialBatchMoments, Modes.size()> Means{}, Mses{};
    std::mt19937_64 Random(0xd7a4c1e95b830621ull);
    std::printf("REPLAY_TEMPORAL_DIRECT_ESTIMATE pipelines_ready=1 motion=%s scope=nee_only "
                "bsdf_continuation_excluded=1 total_radiance_claim=0 cohorts=%u pixels_per_cohort=%u "
                "reference_luma=%.12g reference_coarse_luma=%.12g reference_delta=%.9g gate_tolerance=%.9g "
                "receiver_x=%.9g emission_red=10 occluders=2\n",
                Motion, Cohorts, Pixels / 2u, Reference, Coarse, ReferenceDelta, GateTolerance, ReceiverX);
    std::fflush(stdout);
    for (uint32_t Cohort = 0; Cohort < Cohorts; ++Cohort)
    {
        std::array<uint32_t, Frames> Seeds{};
        for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
        {
            const auto& Mode = Modes[ModeIndex];
            std::vector<std::array<float, 4>> Image;
            if (Mode.Temporal)
            {
                for (uint32_t Frame = 0; Frame < Frames; ++Frame)
                {
                    Image = ExecuteReplayMaterialFloor(Compute, Scenes[Frame], Mode, Seeds[Frame],
                                                       Frame == 0u, Floor, false, true);
                    if (Frame > 0u)
                        Require(Compute.ExtendedStatistics()[20] == Pixels,
                                "Temporal reference arm failed same-frame candidate coverage");
                }
            }
            else
            {
                Image = ExecuteReplayMaterialFloor(Compute, Scenes.back(), Mode, Seeds.back(),
                                                   true, Floor, false, true);
                Require(Compute.ExtendedStatistics()[20] == 0,
                        "Fresh reference control unexpectedly consumed temporal history");
            }
            Require(Image.size() == Pixels, "Direct-emitter image readback has an unexpected pixel count");
            const auto DirectNee = Compute.ReadReplayLogicalSlab(12u, false);
            const auto Reservoir = Compute.ReadReplayLogicalSlab(2u, false);
            Require(DirectNee.size() == Pixels && Reservoir.size() == Pixels,
                    "Direct-emitter NEE/reservoir readback has an unexpected pixel count");
            double Sum = 0.0, SquaredError = 0.0;
            uint32_t SampleCount = 0;
            for (uint32_t Pixel = 0; Pixel < Pixels; ++Pixel)
            {
                const uint32_t X = Pixel % Size, Y = Pixel / Size;
                if ((X + Y) % 2u != FarGroup) continue;
                const double SelectedNeePower = DirectNee[Pixel][0];
                const double ReservoirFactor = Reservoir[Pixel][2];
                const double Sample = SelectedNeePower * ReservoirFactor;
                Require(std::isfinite(SelectedNeePower) && SelectedNeePower >= 0 &&
                            std::isfinite(ReservoirFactor) && ReservoirFactor >= 0 &&
                            std::isfinite(Sample) && Sample >= 0,
                        "Direct-emitter NEE estimate returned invalid reservoir-weighted luminance");
                Sum += Sample;
                const double Error = Sample - Reference;
                SquaredError += Error * Error;
                ++SampleCount;
            }
            Require(SampleCount == Pixels / 2u,
                    "Direct-emitter reference arm did not cover its checkerboard holdout");
            Means[ModeIndex].Accumulate(Sum / double(SampleCount));
            Mses[ModeIndex].Accumulate(SquaredError / double(SampleCount));
            std::printf("REPLAY_TEMPORAL_DIRECT_ESTIMATE_COHORT mode=%s cohort=%u pixels=%u "
                        "mean_luma=%.12g mse=%.12g\n",
                        Mode.Label, Cohort, SampleCount, Sum / double(SampleCount),
                        SquaredError / double(SampleCount));
        }
        std::fflush(stdout);
    }

    bool AllMeanGates = true;
    for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
    {
        const double Mean = Means[ModeIndex].Mean();
        const double StandardError = Means[ModeIndex].StandardError();
        const double Rmse = std::sqrt(std::max(0.0, Mses[ModeIndex].Mean()));
        const bool MeanGate = std::abs(Mean - Reference) <= 6.0 * StandardError + GateTolerance;
        AllMeanGates = AllMeanGates && MeanGate;
        std::printf("REPLAY_TEMPORAL_DIRECT_ESTIMATE_RESULT mode=%s cohorts=%u pixels_per_cohort=%u "
                    "reference_luma=%.12g reference_coarse_luma=%.12g mean_luma=%.12g batch_se=%.9g "
                    "rmse=%.9g gate_tolerance=%.9g mean_gate=%u\n",
                    Modes[ModeIndex].Label, Cohorts, Pixels / 2u, Reference, Coarse, Mean, StandardError,
                    Rmse, GateTolerance, unsigned(MeanGate));
    }
    std::printf("PASS ReplayTemporalDirectEmitterEstimateValidation cohorts=%u modes=%u scope=nee_only "
                "independent_area_reference=1 estimator_math_changed=0 all_mean_gates=%u\n",
                Cohorts, unsigned(Modes.size()), unsigned(AllMeanGates));
    std::fflush(stdout);
}
