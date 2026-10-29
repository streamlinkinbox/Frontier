/*==========================================================================================================================================
                                               REPLAYBALANCEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare balanced facet/BSDF proposals with area quadrature and independent multibounce paths.
#pragma once

namespace
{
std::vector<std::array<float,4>> TraceBalancedReplay(LightComputeSubmission& Compute, const LightComputeArchive& Scene,
                                                      uint32_t Hits, uint32_t Seed, bool Temporal, bool Spatial,
                                                      bool Reset, bool Rough = false,
                                                      const std::array<float,3>& Environment = {0,0,0})
{
    return Compute.ExecuteReplay(Scene, Hits, Seed, Temporal, Spatial, Reset, Environment, .01f,
                                 false, false, Rough, true, false, false, 64, true);
}
void ValidateReplayBalance(const char* Driver, const std::string& Shaders)
{
    const auto Small = ReplayFacetFixture(.05f, 100), Large = ReplayFacetFixture(4, 100),
               Open = ReplayFacetFixture(.5f, 100), Half = ReplayFacetFixture(.5f, 0),
               Covered = ReplayFacetFixture(.5f, -5), Three = ReplayFacetFixture(.5f, 100, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Three, Shaders, false, false, false, false, true);
    const double SmallIntegral = ReplayFacetAreaIntegral(.05, 100, false);
    const double LargeIntegral = ReplayFacetAreaIntegral(4, 100, false);
    const double OpenIntegral = ReplayFacetAreaIntegral(.5, 100, false);
    const double HalfIntegral = ReplayFacetAreaIntegral(.5, 0, false);
    const double ThreeIntegral = ReplayFacetAreaIntegral(.5, 100, true);
    Require(std::abs(LargeIntegral - ReplayFacetAreaIntegral(4,100,false,500)) < 2e-6,
            "Large-source quadrature did not converge");
    ReplayGroupedMoments SmallFacet, SmallBalance, LargeFacet, LargeBalance, HalfBalance, ThreeBalance, ModeChange;
    uint32_t Accepted = 0, Spatial = 0, ChangedAccepted = 0;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 60000 + 100*Batch;
        SmallFacet.Accumulate(Compute.ExecuteReplay(Small, 1, Seed, false, false, true,
                              {0,0,0}, .01f, false, false, false, true), Small.Width);
        SmallBalance.Accumulate(TraceBalancedReplay(Compute, Small, 1, Seed, false, false, true), Small.Width);
        LargeFacet.Accumulate(Compute.ExecuteReplay(Large, 1, Seed+1, false, false, true,
                              {0,0,0}, .01f, false, false, false, true), Large.Width);
        LargeBalance.Accumulate(TraceBalancedReplay(Compute, Large, 1, Seed+2, false, false, true), Large.Width);
        ThreeBalance.Accumulate(TraceBalancedReplay(Compute, Three, 1, Seed+3, false, false, true), Three.Width);
        for (uint32_t Frame = 0; Frame < 6; ++Frame)
            (void)TraceBalancedReplay(Compute, Covered, 1, Seed+10+Frame, true, false, Frame == 0);
        HalfBalance.Accumulate(TraceBalancedReplay(Compute, Half, 1, Seed+19, true, true, false), Half.Width);
        auto Counts = Compute.ExtendedStatistics();
        Accepted += Counts[20]; Spatial += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving blocker did not retain capped balanced history");
        for (uint32_t Frame = 0; Frame < 6; ++Frame)
            (void)Compute.ExecuteReplay(Open, 1, Seed+30+Frame, true, false, Frame == 0,
                                        {0,0,0}, .01f, false, false, false, true);
        ModeChange.Accumulate(TraceBalancedReplay(Compute, Open, 1, Seed+39, true, true, false), Open.Width);
        Counts = Compute.ExtendedStatistics();
        ChangedAccepted += Counts[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Changing the sampling estimator did not cap retained history");
    }
    auto Check = [](const char* Label, const ReplayGroupedMoments& Moments, double Reference)
    {
        const auto& M = Moments.Pixels[0];
        const double Se = std::max(M.StandardError(), Moments.Batches[0].StandardError());
        std::printf("BALANCE_%s exact=%.12g mean=%.12g pixel_se=%.12g batch_se=%.12g std=%.12g n=%u\n",
                    Label, Reference, M.Mean(), M.StandardError(), Moments.Batches[0].StandardError(), M.Deviation(), M.Count);
        Require(std::abs(M.Mean()-Reference) < 6*Se + .002, "Balanced proposal disagrees with area quadrature");
    };
    Check("small_facet", SmallFacet, SmallIntegral);
    Check("small_combined", SmallBalance, SmallIntegral);
    Check("large_facet", LargeFacet, LargeIntegral);
    Check("large_combined", LargeBalance, LargeIntegral);
    Check("half_reused", HalfBalance, HalfIntegral);
    Check("three", ThreeBalance, ThreeIntegral);
    Check("mode_change", ModeChange, OpenIntegral);
    const double LargeRatio = LargeBalance.Pixels[0].Deviation() / LargeFacet.Pixels[0].Deviation();
    const double SmallRatio = SmallBalance.Pixels[0].Deviation() / SmallFacet.Pixels[0].Deviation();
    std::printf("BALANCE_COMPARISON large_sample_std_ratio=%.9g small_sample_std_ratio=%.9g "
                "accepted=%u changed_accepted=%u spatial=%u\n", LargeRatio, SmallRatio, Accepted, ChangedAccepted, Spatial);
    Require(LargeRatio < .8 && SmallRatio > 2 && Accepted == 8*5120 && ChangedAccepted == 8*5120 && Spatial > 0,
            "Balanced large-light variance or dynamic proposal checks failed");
    bool MissingFacet = false, DoubleCount = false;
    try { (void)Compute.ExecuteReplay(Open, 1, 1, false, false, true, {0,0,0}, .01f,
                                      false, false, false, false, false, false, 64, true); }
    catch (const std::exception&) { MissingFacet = true; }
    try { (void)Compute.ExecuteReplay(Open, 1, 1, false, false, true, {0,0,0}, .01f,
                                      false, false, false, true, true, false, 64, true); }
    catch (const std::exception&) { DoubleCount = true; }
    std::printf("BALANCE_GUARDS missing_facet=%u double_count=%u\n", MissingFacet ? 1u : 0u, DoubleCount ? 1u : 0u);
    Require(MissingFacet && DoubleCount, "Incompatible balanced-sampling modes were not rejected");
    auto Empty = Open;
    Empty.Surfaces.clear(); Empty.Visibility = {};
    const auto Environment = TraceBalancedReplay(Compute, Empty, 1, 1, false, false, true, false, {.6f,.6f,.6f});
    const auto EnvironmentMean = DirectGpuCentres(Environment, Empty.Width).Mean();
    const auto Near = ReplayEndpointFixture(.999995f, -5);
    const auto NearImage = TraceBalancedReplay(Compute, Near, 1, 2, false, false, true);
    const auto NearMean = DirectGpuCentres(NearImage, Near.Width).Mean();
    std::printf("BALANCE_EMPTY_ENVIRONMENT mean=%.9g near_blocked=%.9g\n", EnvironmentMean, NearMean);
    Require(std::abs(EnvironmentMean-.3) < 1e-6 && NearMean == 0,
            "Balanced environment fallback or near-source blocker failed");

    auto Rough = Half;
    for (auto& Receiver : Rough.Receivers)
        if (Receiver.PositionHit[3] != 0)
        {
            Receiver.NormalRoughness = {0,0,1,.45f};
            Receiver.ViewMetallic = {std::sin(.9f),0,std::cos(.9f),.9f};
            Receiver.AlbedoIor = {.8f,.3f,.1f,1.5f};
            Receiver.ResidualSpecular = {0,0,0,.5f};
        }
    const auto RoughIntegral = ReplayRoughAreaIntegral(Rough.Receivers[size_t(Rough.Width)+1], .5, 0);
    ReplayGroupedMoments RoughBalance;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
        RoughBalance.Accumulate(TraceBalancedReplay(Compute, Rough, 1, 62000+100*Batch, false, false, true, true), Rough.Width);
    for (uint32_t Channel = 0; Channel < 3; ++Channel)
    {
        const auto& M = RoughBalance.Pixels[Channel];
        const double Se = std::max(M.StandardError(), RoughBalance.Batches[Channel].StandardError());
        std::printf("BALANCE_ROUGH channel=%u exact=%.9g mean=%.9g se=%.9g n=%u\n",
                    Channel, RoughIntegral[Channel], M.Mean(), Se, M.Count);
        Require(std::abs(M.Mean()-RoughIntegral[Channel]) < 6*Se + .002,
                "Balanced rough proposal disagrees with area quadrature");
    }
    std::printf("PASS balanced direct sampling against independent area integrals\n");
}

void ValidateReplayBalanceRoom(const char* Driver, const std::string& Shaders)
{
    auto Open = ReplayRoom(7), Blocked = ReplayRoom(0);
    auto Receivers = DirectGpuFixture(false);
    for (auto& R : Receivers.Receivers)
        if (R.PositionHit[3] != 0)
        {
            R.PositionHit = {1.4f,2,0,1}; R.MotionPositionToken = {1.4f,2,0,1};
            R.AlbedoIor = {.7f,.7f,.7f,1.5f};
        }
    Open.Width = Blocked.Width = Receivers.Width; Open.Height = Blocked.Height = Receivers.Height;
    Open.Receivers = Blocked.Receivers = Receivers.Receivers;
    std::printf("BALANCE_ROOM tracing two independent 400000-path CPU references\n"); std::fflush(stdout);
    const auto CpuOpen = ReplayReferenceIntegrate(Open, 4, 752531);
    const auto CpuBlocked = ReplayReferenceIntegrate(Blocked, 4, 953451);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    const std::array<float,3> Environment{.025f,.025f,.025f};
    ReplayGroupedMoments Fresh, Reused;
    uint32_t Accepted = 0, Spatial = 0;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 63000 + Batch*100;
        Fresh.Accumulate(TraceBalancedReplay(Compute, Open, 4, Seed, false, false, true, false, Environment), Open.Width);
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)TraceBalancedReplay(Compute, Open, 4, Seed+10+Frame, true, false, Frame == 0, false, Environment);
        Reused.Accumulate(TraceBalancedReplay(Compute, Blocked, 4, Seed+19, true, true, false, false, Environment), Open.Width);
        const auto Counts = Compute.ExtendedStatistics();
        Accepted += Counts[20]; Spatial += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Balanced multibounce blocker motion lost capped history");
    }
    ReplayCompareRgb("balance_open_four", CpuOpen, Fresh);
    ReplayCompareRgb("balance_blocked_reused_four", CpuBlocked, Reused);
    Require(Accepted == 8*5120 && Spatial > 0, "Balanced multibounce reuse was not exercised");
    std::printf("BALANCE_ROOM accepted=%u spatial_links=%u cpu_rng=mt19937_64 brute_force=1\n", Accepted, Spatial);
    for (auto* Scene : {&Open, &Blocked})
    {
        for (auto& F : Scene->Surfaces)
        {
            const int Token = int(F.OpticalToken[3]);
            F.OpticalToken[2] = Token == 6 ? 0 : .5f;
            if (Token == 2 || Token == 4)
            {
                F.OpticalToken[0] = Token == 4 ? .7f : .35f;
                F.AlbedoRoughness[3] = .6f;
            }
        }
        for (auto& R : Scene->Receivers)
            if (R.PositionHit[3] != 0)
            {
                R.NormalRoughness = {0,0,1,.7f}; R.ViewMetallic = {0,0,1,.2f};
                R.ResidualSpecular = {0,0,0,.5f};
            }
    }
    std::printf("BALANCE_ROUGH_ROOM tracing two independent 400000-path CPU references\n"); std::fflush(stdout);
    const auto CpuRoughOpen = ReplayReferenceIntegrate(Open, 4, 267531, true);
    const auto CpuRoughBlocked = ReplayReferenceIntegrate(Blocked, 4, 479531, true);
    ReplayGroupedMoments RoughFresh, RoughReused;
    uint32_t RoughAccepted = 0, RoughSpatial = 0;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 65000 + Batch*100;
        RoughFresh.Accumulate(TraceBalancedReplay(Compute, Open, 4, Seed, false, false, true, true, Environment), Open.Width);
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)TraceBalancedReplay(Compute, Open, 4, Seed+10+Frame, true, false, Frame == 0, true, Environment);
        RoughReused.Accumulate(TraceBalancedReplay(Compute, Blocked, 4, Seed+19, true, true, false, true, Environment), Open.Width);
        const auto Counts = Compute.ExtendedStatistics();
        RoughAccepted += Counts[20]; RoughSpatial += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Balanced rough blocker motion lost capped history");
    }
    ReplayCompareRgb("balance_rough_open_four", CpuRoughOpen, RoughFresh);
    ReplayCompareRgb("balance_rough_blocked_reused_four", CpuRoughBlocked, RoughReused);
    Require(RoughAccepted == 8*5120 && RoughSpatial > 0, "Balanced rough multibounce reuse was not exercised");
    std::printf("BALANCE_ROUGH_ROOM accepted=%u spatial_links=%u cpu_rng=mt19937_64 brute_force=1\n",
                RoughAccepted, RoughSpatial);
    std::printf("PASS balanced diffuse and rough four-hit replay against independent CPU paths\n");
}
} // namespace
