/*==========================================================================================================================================
                                           REPLAYFACETMIXTUREVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare an optional half-uniform emitter mixture against analytic and multibounce references.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            DEFAULT-OFF GPU PARITY
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayFacetMixtureParity(const char* Driver, const std::string& CurrentShaders,
                                      const std::string& PreviousShaders)
{
    uint64_t Components = 0, Different = 0;
    uint32_t Images = 0, Histories = 0;
    for (bool Power : {false, true})
        for (bool Importance : {false, true})
        {
            const auto Previous = ReadReplayParityCases(Driver, PreviousShaders, Importance, Power);
            const auto Current = ReadReplayParityCases(Driver, CurrentShaders, Importance, Power);
            Require(Previous.Images.size() == Current.Images.size() && Previous.History == Current.History,
                    "Uniform/power mixture changed a disabled-mode history");
            Images += uint32_t(Previous.Images.size());
            Histories += uint32_t(Previous.History.size());
            for (size_t Image = 0; Image < Previous.Images.size(); ++Image)
            {
                Require(Previous.Images[Image].size() == Current.Images[Image].size(), "Parity image extent changed");
                for (size_t Pixel = 0; Pixel < Previous.Images[Image].size(); ++Pixel)
                    for (uint32_t Channel = 0; Channel < 4; ++Channel)
                    {
                        const float A = Previous.Images[Image][Pixel][Channel];
                        const float B = Current.Images[Image][Pixel][Channel];
                        Different += std::memcmp(&A, &B, sizeof(float)) != 0 ? 1u : 0u;
                        ++Components;
                    }
            }
        }
    std::printf("MIXTURE_PARITY images=%u components=%llu differing_float_bits=%llu histories=%u "
                "uniform_and_power=1 cosine_and_ggx=1\n", Images, (unsigned long long)Components,
                (unsigned long long)Different, Histories);
    Require(Different == 0 && Images == 20, "Disabled mixture changed prior shader readbacks");
    std::printf("PASS bit-identical uniform/power and cosine/GGX readbacks with mixture disabled\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                         FIXED RECEIVER AND OCCLUSION STUDY
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayFacetMixture(const char* Driver, const std::string& Shaders)
{
    const auto Open = ReplayPowerFacetFixture(2), Blocked = ReplayPowerFacetFixture(0);
    const auto Relit = ReplayPowerFacetFixture(2, true);
    const double OpenExact = ReplayPowerAreaIntegral(2, false, 1024);
    const double BlockedExact = ReplayPowerAreaIntegral(0, false, 1024);
    const double RelitExact = ReplayPowerAreaIntegral(2, true, 1024);
    const auto Cdf = CompileRadiantFacetDistribution(Open.Surfaces);
    Require(Cdf[0] == 2, "Expected two sources in the mixture fixture");
    const double P0 = .5 * double(Cdf[2]) + .25, P1 = .5 * double(Cdf[6]) + .25;
    Require(P0 >= .25 && P1 >= .25 && std::abs(P0 + P1 - 1) < 1e-12,
            "Mixture probability floor or normalization is invalid");
    // 📝 For fixed per-source contributions the half-uniform mixture's ideal second moment is at most
    //    twice the uniform second moment. This is not a variance bound or a reservoir-independence claim.
    for (const auto& Contribution : {std::array<double, 2>{1, 0}, {0, 1}, {2, .001}, {1, 1}})
    {
        const double MixedMoment = Contribution[0] * Contribution[0] / P0 + Contribution[1] * Contribution[1] / P1;
        const double UniformMoment = 2 * (Contribution[0] * Contribution[0] + Contribution[1] * Contribution[1]);
        Require(MixedMoment <= 2 * UniformMoment + 1e-12, "Finite-domain second-moment bound failed");
    }
    LightComputeSubmission Compute(Driver);
    std::printf("MIXTURE_FACET preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    constexpr uint32_t Batches = 24;
    ReplayGroupedMoments UniformOpen, PowerOpen, MixtureOpen, UniformBlocked, PowerBlocked, MixtureBlocked,
                         MixtureOpenMis, MixtureBlockedMis, Changed, Occluded, Reweighted;
    uint32_t ChangedLinks = 0, BlockedLinks = 0, RelitLinks = 0, SpatialLinks = 0;
    const std::array<float, 3> Dark{0, 0, 0};
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 840001 + 103 * Batch;
        UniformOpen.Accumulate(TraceReplayPower(Compute, Open, 1, Seed, false, false, true, false), Open.Width);
        PowerOpen.Accumulate(TraceReplayPower(Compute, Open, 1, Seed + 1, false, false, true, true), Open.Width);
        MixtureOpen.Accumulate(TraceReplayPower(Compute, Open, 1, Seed + 2, false, false, true, true,
                                                false, false, false, false, Dark, true), Open.Width);
        UniformBlocked.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 3, false, false, true, false), Open.Width);
        PowerBlocked.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 4, false, false, true, true), Open.Width);
        MixtureBlocked.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 5, false, false, true, true,
                                                   false, false, false, false, Dark, true), Open.Width);
        MixtureOpenMis.Accumulate(TraceReplayPower(Compute, Open, 1, Seed + 6, false, false, true, true,
                                                   true, false, false, false, Dark, true), Open.Width);
        MixtureBlockedMis.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 7, false, false, true, true,
                                                      true, false, false, false, Dark, true), Open.Width);
        for (uint32_t Frame = 0; Frame < 3; ++Frame)
            (void)TraceReplayPower(Compute, Open, 1, Seed + 10 + Frame, true, false, Frame == 0, true, true);
        Changed.Accumulate(TraceReplayPower(Compute, Open, 1, Seed + 20, true, true, false, true,
                                            true, false, false, false, Dark, true), Open.Width);
        auto Counts = Compute.ExtendedStatistics(); ChangedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Mixture transition did not cap retained history");
        Occluded.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 21, true, true, false, true,
                                             true, false, false, false, Dark, true), Open.Width);
        Counts = Compute.ExtendedStatistics(); BlockedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Mixture blocker transition did not cap retained history");
        Reweighted.Accumulate(TraceReplayPower(Compute, Relit, 1, Seed + 22, true, true, false, true,
                                               true, false, false, false, Dark, true), Open.Width);
        Counts = Compute.ExtendedStatistics(); RelitLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Mixture relighting transition did not cap retained history");
    }
    auto Check = [](const char* Label, const ReplayGroupedMoments& Result, double Exact)
    {
        const auto& Pixels = Result.Pixels[0];
        const double BatchSe = ReplaySampleStandardError(Result.Batches[0]);
        const double Se = std::max(ReplaySampleStandardError(Pixels), BatchSe);
        std::printf("MIXTURE_FACET %s exact=%.9g mean=%.9g batch_se=%.9g sample_sd=%.9g batches=%u\n",
                    Label, Exact, Pixels.Mean(), BatchSe, Pixels.Deviation(), Result.Batches[0].Count);
        Require(std::abs(Pixels.Mean() - Exact) < 6 * Se + .003 * Exact + 1e-6,
                "Emitter mixture disagrees with independent area integration");
    };
    Check("open_uniform", UniformOpen, OpenExact);
    Check("open_power", PowerOpen, OpenExact);
    Check("open_mixture", MixtureOpen, OpenExact);
    Check("open_mixture_mis", MixtureOpenMis, OpenExact);
    Check("blocked_uniform", UniformBlocked, BlockedExact);
    Check("blocked_power", PowerBlocked, BlockedExact);
    Check("blocked_mixture", MixtureBlocked, BlockedExact);
    Check("blocked_mixture_mis", MixtureBlockedMis, BlockedExact);
    Check("proposal_transition", Changed, OpenExact);
    Check("blocker_transition", Occluded, BlockedExact);
    Check("relight_transition", Reweighted, RelitExact);
    const double OpenSdRatio = MixtureOpen.Pixels[0].Deviation() / UniformOpen.Pixels[0].Deviation();
    const double BlockedSdRatio = MixtureBlocked.Pixels[0].Deviation() / UniformBlocked.Pixels[0].Deviation();
    const double PowerBlockedRatio = PowerBlocked.Pixels[0].Deviation() / UniformBlocked.Pixels[0].Deviation();
    const auto SecondMoment = [](const DirectGpuMoments& M) { return M.Squares / M.Count; };
    const double OpenSecondRatio = SecondMoment(MixtureOpen.Pixels[0]) / SecondMoment(UniformOpen.Pixels[0]);
    const double BlockedSecondRatio = SecondMoment(MixtureBlocked.Pixels[0]) / SecondMoment(UniformBlocked.Pixels[0]);
    std::printf("MIXTURE_FACET_VARIANCE open_sd_ratio=%.9g blocked_sd_ratio=%.9g blocked_power_sd_ratio=%.9g "
                "open_second_ratio=%.9g blocked_second_ratio=%.9g p=%.9g,%.9g proposal=%u blocked=%u "
                "relit=%u spatial=%u\n", OpenSdRatio, BlockedSdRatio, PowerBlockedRatio, OpenSecondRatio,
                BlockedSecondRatio, P0, P1, ChangedLinks, BlockedLinks, RelitLinks, SpatialLinks);
    Require(OpenSdRatio < .95 && BlockedSdRatio < 3 && PowerBlockedRatio > BlockedSdRatio * 5 &&
                OpenSecondRatio < 2.25 && BlockedSecondRatio < 2.25 &&
                ChangedLinks == Batches * 5120 && BlockedLinks == Batches * 5120 && RelitLinks == Batches * 5120 &&
                SpatialLinks > 0, "Mixture variance controls or dynamic reuse did not exercise the intended cases");
    bool MissingCdf = false;
    try
    {
        (void)Compute.ExecuteReplay(Open, 1, 1, false, false, true, Dark, .01f,
                                    false, false, false, true, false, false, 64, false, false, false, false, true);
    }
    catch (const std::exception&) { MissingCdf = true; }
    Require(MissingCdf, "Uniform/power mixture accepted a missing weighted CDF");
    std::printf("MIXTURE_FACET_GUARD missing_cdf_rejected=1 ideal_second_moment_factor=2\n");
    std::printf("PASS area, MIS, changing proposal, occlusion and source-power mixture controls\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                            FOUR-HIT TEXTURED ROOM
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayFacetMixtureRoom(const char* Driver, const std::string& Shaders)
{
    auto Open = RoughTexturedRoom(1, 7, .48f, .7f);
    auto Relit = Open;
    auto Blocked = RoughTexturedRoom(1, 0, .48f, .7f);
    AdjustReplayRoomPower(Open, false);
    AdjustReplayRoomPower(Relit, true);
    AdjustReplayRoomPower(Blocked, true);
    std::printf("MIXTURE_ROOM tracing three independent 400000-path four-hit CPU references\n");
    std::fflush(stdout);
    const auto CpuOpen = ReplayReferenceIntegrate(Open, 4, 850001, true, true);
    const auto CpuBlocked = ReplayReferenceIntegrate(Blocked, 4, 850101, true, true);
    const auto CpuRelit = ReplayReferenceIntegrate(Relit, 4, 850201, true, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    constexpr uint32_t Batches = 12;
    ReplayGroupedMoments Fresh, Switched, Occluded, Reweighted;
    uint32_t SwitchedLinks = 0, BlockedLinks = 0, RelitLinks = 0, SpatialLinks = 0;
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 851001 + 103 * Batch;
        Fresh.Accumulate(TraceReplayPower(Compute, Open, 4, Seed, false, false, true, true,
                                           true, true, true, true, Environment, true), Open.Width);
        for (uint32_t Frame = 0; Frame < 4; ++Frame)
            (void)TraceReplayPower(Compute, Open, 4, Seed + 10 + Frame, true, false, Frame == 0, true,
                                   true, true, true, true, Environment);
        Switched.Accumulate(TraceReplayPower(Compute, Open, 4, Seed + 20, true, true, false, true,
                                              true, true, true, true, Environment, true), Open.Width);
        auto Counts = Compute.ExtendedStatistics(); SwitchedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Four-hit mixture proposal switch did not cap history");
        Occluded.Accumulate(TraceReplayPower(Compute, Blocked, 4, Seed + 21, true, true, false, true,
                                              true, true, true, true, Environment, true), Open.Width);
        Counts = Compute.ExtendedStatistics(); BlockedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Four-hit mixture blocker and source change did not cap history");
        Reweighted.Accumulate(TraceReplayPower(Compute, Relit, 4, Seed + 22, true, true, false, true,
                                                true, true, true, true, Environment, true), Open.Width);
        Counts = Compute.ExtendedStatistics(); RelitLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Four-hit mixture reopening did not cap history");
    }
    ReplayCompareRgb("mixture_room_fresh", CpuOpen, Fresh);
    ReplayCompareRgb("mixture_room_switched", CpuOpen, Switched);
    ReplayCompareRgb("mixture_room_blocked", CpuBlocked, Occluded);
    ReplayCompareRgb("mixture_room_relit", CpuRelit, Reweighted);
    std::printf("MIXTURE_ROOM_LINKS proposal=%u blocked=%u relit=%u spatial=%u batches=%u\n",
                SwitchedLinks, BlockedLinks, RelitLinks, SpatialLinks, Batches);
    Require(SwitchedLinks == Batches * 5120 && BlockedLinks == Batches * 5120 && RelitLinks == Batches * 5120 &&
                SpatialLinks > 0, "Four-hit mixture transition and spatial links were not exercised");
    std::printf("PASS four-hit rough-textured mixture, changed emitter, occlusion and reopening\n");
}
} // namespace
