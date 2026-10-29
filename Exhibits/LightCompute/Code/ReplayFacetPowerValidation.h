/*==========================================================================================================================================
                                            REPLAYFACETPOWERVALIDATION.H
==========================================================================================================================================*/
// 🧩 Check optional power-weighted emissive replay against independent area and multibounce references.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                              DEFAULT-OFF READBACK PARITY
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayPowerDefaultParity(const char* Driver, const std::string& CurrentShaders,
                                      const std::string& PreviousShaders)
{
    ValidateReplayImportanceDefaultParity(Driver, CurrentShaders, PreviousShaders);
    const auto Previous = ReadReplayParityCases(Driver, PreviousShaders, true);
    const auto Current = ReadReplayParityCases(Driver, CurrentShaders, true);
    Require(Previous.Images.size() == Current.Images.size() && Previous.History == Current.History,
            "Power-weighting changed default-off importance history");
    uint64_t Components = 0, Different = 0;
    for (size_t Image = 0; Image < Previous.Images.size(); ++Image)
    {
        Require(Previous.Images[Image].size() == Current.Images[Image].size(), "Parity image extent changed");
        for (size_t Pixel = 0; Pixel < Previous.Images[Image].size(); ++Pixel)
            for (uint32_t Channel = 0; Channel < 4; ++Channel)
            {
                const float A = Previous.Images[Image][Pixel][Channel], B = Current.Images[Image][Pixel][Channel];
                Different += std::memcmp(&A, &B, sizeof(float)) != 0 ? 1u : 0u;
                ++Components;
            }
    }
    std::printf("POWER_PARITY importance_images=%zu components=%llu differing_float_bits=%llu history=%zu\n",
                Previous.Images.size(), (unsigned long long)Components, (unsigned long long)Different,
                Previous.History.size());
    Require(Different == 0, "Power-weighting changed default-off GGX replay readbacks");
    std::printf("PASS default-off cosine and GGX replay readbacks and history\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                              UNEQUAL EMITTERS AND OCCLUSION
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive ReplayPowerFacetFixture(float BlockStart, bool Reverse = false)
{
    auto Scene = DirectGpuFixture(false);
    Scene.Lights.clear();
    for (auto& Receiver : Scene.Receivers)
        if (Receiver.PositionHit[3] != 0) Receiver.AlbedoIor = {.5f, .5f, .5f, 1.5f};
    const std::vector<std::array<SpatialVector, 3>> Geometry{
        {{{.2, -.4, 1}, {.8, -.4, 1}, {.5, .4, 1}}},
        {{{-.8, -.4, 1}, {-.2, -.4, 1}, {-.5, .4, 1}}},
        {{{BlockStart, -5, .5}, {5, -5, .5}, {5, 5, .5}}},
        {{{BlockStart, -5, .5}, {5, 5, .5}, {BlockStart, 5, .5}}},
    };
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    for (size_t Index = 0; Index < Scene.Visibility.Corners.size(); ++Index)
    {
        const auto& T = Scene.Visibility.Corners[Index];
        const uint32_t Original = Scene.Visibility.OriginalFacets[Index];
        LightComputeFacet Facet;
        Facet.A = T.A; Facet.B = T.B; Facet.C = T.C;
        Facet.NormalA = Facet.NormalB = Facet.NormalC = {0, 0, -1, 0};
        Facet.AlbedoRoughness = {0, 0, 0, 1};
        Facet.OpticalToken = {0, 1.5f, 0, 1};
        if (Original < 2)
        {
            const float Power = (Original == 0) != Reverse ? 24.0f : .05f;
            Facet.Emissive = {Power, Power, Power, 0};
        }
        Scene.Surfaces.push_back(Facet);
    }
    return Scene;
}

double ReplayPowerAreaIntegral(double BlockStart, bool Reverse, uint32_t Steps)
{
    constexpr double Pi = 3.14159265358979323846, Height = .999, BlockHeight = .499, Area = .24;
    double Integral = 0;
    for (uint32_t Triangle = 0; Triangle < 2; ++Triangle)
    {
        const double X0 = Triangle == 0 ? .2 : -.8, X1 = Triangle == 0 ? .8 : -.2;
        const double Power = (Triangle == 0) != Reverse ? 24.0 : double(.05f);
        double Sum = 0;
        for (uint32_t U = 0; U < Steps; ++U)
            for (uint32_t V = 0; V < Steps; ++V)
            {
                const double Root = std::sqrt((U + .5) / Steps), Along = (V + .5) / Steps;
                const double X = X0 * (1 - Root) + X1 * Root * (1 - Along) + (X0 + X1) * .5 * Root * Along;
                const double Y = -.4 * (1 - Root * Along) + .4 * Root * Along;
                if (X * BlockHeight / Height >= BlockStart) continue;
                const double Squared = Height * Height + X * X + Y * Y;
                Sum += Height * Height / (Squared * Squared);
            }
        Integral += .5 / Pi * Power * Area * Sum / (double(Steps) * Steps);
    }
    return Integral;
}

std::vector<std::array<float, 4>> TraceReplayPower(LightComputeSubmission& Compute, const LightComputeArchive& Scene,
                                                    uint32_t Bounces, uint32_t Seed, bool Temporal, bool Spatial,
                                                    bool Reset, bool Weighted, bool Balance = false, bool Rough = false,
                                                    bool Textured = false, bool Importance = false,
                                                    const std::array<float, 3>& Environment = {0, 0, 0},
                                                    bool UniformMixture = false)
{
    return Compute.ExecuteReplay(Scene, Bounces, Seed, Temporal, Spatial, Reset, Environment, .01f,
                                 false, false, Rough, true, false, false, 64, Balance, Textured, Importance, Weighted,
                                 UniformMixture);
}

void ValidateReplayPowerFacets(const char* Driver, const std::string& Shaders)
{
    const auto Open = ReplayPowerFacetFixture(2), Blocked = ReplayPowerFacetFixture(0);
    const double OpenExact = ReplayPowerAreaIntegral(2, false, 1024), BlockedExact = ReplayPowerAreaIntegral(0, false, 1024);
    Require(std::abs(OpenExact - ReplayPowerAreaIntegral(2, false, 512)) < 1e-5 &&
                std::abs(BlockedExact - ReplayPowerAreaIntegral(0, false, 512)) < 1e-5,
            "Weighted-emitter area quadrature did not converge");
    const auto Distribution = CompileRadiantFacetDistribution(Open.Surfaces);
    ValidateRadiantFacetDistribution(Distribution, 0, Open.Surfaces.size());
    Require(Distribution[0] == 2 && Distribution[2] > 0 && Distribution[6] > 0,
            "Weighted fixture did not retain both light sources");
    LightComputeSubmission Compute(Driver);
    std::printf("POWER_FACET preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    constexpr uint32_t Batches = 24;
    ReplayGroupedMoments UniformOpen, WeightedOpen, BalancedOpen, UniformBlocked, WeightedBlocked, BalancedBlocked,
                         Switched, Moved;
    uint32_t SwitchedLinks = 0, MovedLinks = 0, SpatialLinks = 0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 805001 + Batch * 101;
        UniformOpen.Accumulate(TraceReplayPower(Compute, Open, 1, Seed, false, false, true, false), Open.Width);
        WeightedOpen.Accumulate(TraceReplayPower(Compute, Open, 1, Seed + 1, false, false, true, true), Open.Width);
        BalancedOpen.Accumulate(TraceReplayPower(Compute, Open, 1, Seed + 2, false, false, true, true, true), Open.Width);
        UniformBlocked.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 3, false, false, true, false), Open.Width);
        WeightedBlocked.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 4, false, false, true, true), Open.Width);
        BalancedBlocked.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 5, false, false, true, true, true), Open.Width);
        for (uint32_t Frame = 0; Frame < 3; ++Frame)
            (void)TraceReplayPower(Compute, Open, 1, Seed + 10 + Frame, true, false, Frame == 0, false, true);
        Switched.Accumulate(TraceReplayPower(Compute, Open, 1, Seed + 20, true, true, false, true, true), Open.Width);
        auto Counts = Compute.ExtendedStatistics(); SwitchedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Changing the facet proposal did not retain capped history");
        Moved.Accumulate(TraceReplayPower(Compute, Blocked, 1, Seed + 21, true, true, false, true, true), Open.Width);
        Counts = Compute.ExtendedStatistics(); MovedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving the blocker did not retain capped weighted history");
    }
    auto Check = [](const char* Label, const ReplayGroupedMoments& Result, double Exact)
    {
        const auto& Pixels = Result.Pixels[0];
        const double BatchSe = ReplaySampleStandardError(Result.Batches[0]);
        const double Se = std::max(ReplaySampleStandardError(Pixels), BatchSe);
        std::printf("POWER_FACET %s exact=%.9g mean=%.9g batch_se=%.9g sample_sd=%.9g batches=%u\n",
                    Label, Exact, Pixels.Mean(), BatchSe, Pixels.Deviation(), Result.Batches[0].Count);
        Require(std::abs(Pixels.Mean() - Exact) < 6 * Se + .003 * Exact + 1e-6,
                "Weighted facet estimator disagrees with area integration");
    };
    Check("open_uniform", UniformOpen, OpenExact);
    Check("open_weighted", WeightedOpen, OpenExact);
    Check("open_weighted_mis", BalancedOpen, OpenExact);
    Check("blocked_uniform", UniformBlocked, BlockedExact);
    Check("blocked_weighted", WeightedBlocked, BlockedExact);
    Check("blocked_weighted_mis", BalancedBlocked, BlockedExact);
    Check("proposal_switch", Switched, OpenExact);
    Check("blocker_transition", Moved, BlockedExact);
    const double OpenRatio = WeightedOpen.Pixels[0].Deviation() / UniformOpen.Pixels[0].Deviation();
    const double BlockedRatio = WeightedBlocked.Pixels[0].Deviation() / UniformBlocked.Pixels[0].Deviation();
    std::printf("POWER_FACET_VARIANCE open_sd_ratio=%.9g blocked_sd_ratio=%.9g probabilities=%.9g,%.9g "
                "proposal_accepts=%u blocker_accepts=%u spatial=%u\n",
                OpenRatio, BlockedRatio, Distribution[2], Distribution[6], SwitchedLinks, MovedLinks, SpatialLinks);
    Require(OpenRatio < .7 && BlockedRatio > 2 && SwitchedLinks == Batches * 5120 &&
                MovedLinks == Batches * 5120 && SpatialLinks > 0,
            "Variance counterexample or dynamic weighted reuse was not exercised");

    // 📝 A separate seed sequence exposes convergence in the rare-visible-emitter counterexample.
    ReplayGroupedMoments RareVisible;
    std::mt19937 FrameRandom(0x8a34c791u);
    std::vector<uint32_t> FrameSeeds;
    uint32_t Nonzero = 0;
    for (uint32_t Batch = 0; Batch < 256; ++Batch)
    {
        const uint32_t Seed = FrameRandom();
        Require(std::find(FrameSeeds.begin(), FrameSeeds.end(), Seed) == FrameSeeds.end(),
                "Fresh convergence sequence repeated a frame seed");
        FrameSeeds.push_back(Seed);
        const auto Image = TraceReplayPower(Compute, Blocked, 1, Seed, false, false, true, true);
        RareVisible.Accumulate(Image, Open.Width);
        for (uint32_t Y = 0; Y < 32; ++Y)
            for (uint32_t X = 0; X < 32; ++X)
                Nonzero += Image[size_t(3 * Y + 1) * Open.Width + 3 * X + 1][0] > 0 ? 1u : 0u;
        if (Batch == 31 || Batch == 63 || Batch == 127 || Batch == 255)
        {
            std::printf("POWER_FACET_CONVERGENCE batches=%u exact=%.9g mean=%.9g batch_se=%.9g "
                        "nonzero_centres=%u unique_frame_seeds=1\n", Batch + 1, BlockedExact,
                        RareVisible.Pixels[0].Mean(), ReplaySampleStandardError(RareVisible.Batches[0]), Nonzero);
            std::fflush(stdout);
        }
    }
    Check("blocked_weighted_256", RareVisible, BlockedExact);
    Require(Nonzero > 0, "Rare-visible convergence check did not draw the dim source");
    bool MissingSampling = false;
    try
    {
        (void)Compute.ExecuteReplay(Open, 1, 1, false, false, true, {0,0,0}, .01f,
                                    false, false, false, false, false, false, 64, false, false, false, true);
    }
    catch (const std::exception&) { MissingSampling = true; }
    auto Degenerate = Open;
    for (size_t Index = 0; Index < Degenerate.Surfaces.size(); ++Index)
        if (Degenerate.Surfaces[Index].Emissive[0] > 0)
        {
            auto& Facet = Degenerate.Surfaces[Index];
            Facet.B = Facet.C = Facet.A;
            Degenerate.Visibility.Corners[Index].B = Degenerate.Visibility.Corners[Index].C = Facet.A;
            break;
        }
    bool RejectedDegenerate = false;
    try { (void)TraceReplayPower(Compute, Degenerate, 1, 7, false, false, true, true); }
    catch (const std::exception&) { RejectedDegenerate = true; }
    auto Empty = DirectGpuFixture(false);
    Empty.Lights.clear();
    const auto Environment = TraceReplayPower(Compute, Empty, 1, 19, false, false, true, true,
                                               true, false, false, false, {.6f, .6f, .6f});
    const double EmptyMean = DirectGpuCentres(Environment, Empty.Width).Mean();
    Require(MissingSampling && RejectedDegenerate && std::abs(EmptyMean - .36) < 1e-6,
            "Weighted facet guards or empty-source fallback failed");
    std::printf("POWER_FACET_GUARDS missing_sampling=%u degenerate=%u empty_environment=%.9g\n",
                MissingSampling, RejectedDegenerate, EmptyMean);
    std::printf("PASS weighted facet area, MIS, dynamic occlusion and variance counterexample\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                            MULTIBOUNCE TEXTURED ROOM
//------------------------------------------------------------------------------------------------------------------------

void AdjustReplayRoomPower(LightComputeArchive& Scene, bool Reverse)
{
    uint32_t Changed = 0;
    for (size_t Index = 0; Index < Scene.Surfaces.size(); ++Index)
    {
        const uint32_t Original = Scene.Visibility.OriginalFacets[Index];
        if (Original != 10 && Original != 11) continue;
        const float Scale = (Original == 10) != Reverse ? 1.0f : .1f;
        for (uint32_t C = 0; C < 3; ++C) Scene.Surfaces[Index].Emissive[C] *= Scale;
        ++Changed;
    }
    Require(Changed == 2, "Unequal-power room emitters are missing");
}

void ValidateReplayPowerRoom(const char* Driver, const std::string& Shaders)
{
    auto Open = RoughTexturedRoom(1, 7, .48f, .7f);
    auto Relit = Open;
    auto Blocked = RoughTexturedRoom(1, 0, .48f, .7f);
    AdjustReplayRoomPower(Open, false);
    AdjustReplayRoomPower(Relit, true);
    AdjustReplayRoomPower(Blocked, true);
    std::printf("POWER_ROOM tracing three independent 400000-path four-hit CPU references\n");
    std::fflush(stdout);
    const auto CpuOpen = ReplayReferenceIntegrate(Open, 4, 810001, true, true);
    const auto CpuRelit = ReplayReferenceIntegrate(Relit, 4, 810101, true, true);
    const auto CpuBlocked = ReplayReferenceIntegrate(Blocked, 4, 810201, true, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    constexpr uint32_t Batches = 12;
    ReplayGroupedMoments Fresh, Switched, Reweighted, Occluded;
    uint32_t SwitchLinks = 0, RelitLinks = 0, BlockedLinks = 0, SpatialLinks = 0;
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 811001 + 101 * Batch;
        Fresh.Accumulate(TraceReplayPower(Compute, Open, 4, Seed, false, false, true, true,
                                           true, true, true, true, Environment), Open.Width);
        for (uint32_t Frame = 0; Frame < 4; ++Frame)
            (void)TraceReplayPower(Compute, Open, 4, Seed + 10 + Frame, true, false, Frame == 0, false,
                                   true, true, true, true, Environment);
        Switched.Accumulate(TraceReplayPower(Compute, Open, 4, Seed + 20, true, true, false, true,
                                              true, true, true, true, Environment), Open.Width);
        auto Counts = Compute.ExtendedStatistics(); SwitchLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Weighted room proposal switch did not cap history");
        Reweighted.Accumulate(TraceReplayPower(Compute, Relit, 4, Seed + 21, true, true, false, true,
                                                true, true, true, true, Environment), Open.Width);
        Counts = Compute.ExtendedStatistics(); RelitLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Changed room source power did not cap history");
        Occluded.Accumulate(TraceReplayPower(Compute, Blocked, 4, Seed + 22, true, true, false, true,
                                              true, true, true, true, Environment), Open.Width);
        Counts = Compute.ExtendedStatistics(); BlockedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Weighted room blocker did not cap history");
    }
    ReplayCompareRgb("power_room_fresh", CpuOpen, Fresh);
    ReplayCompareRgb("power_room_switched", CpuOpen, Switched);
    ReplayCompareRgb("power_room_relit", CpuRelit, Reweighted);
    ReplayCompareRgb("power_room_blocked", CpuBlocked, Occluded);
    std::printf("POWER_ROOM_LINKS switch=%u relit=%u blocked=%u spatial=%u batches=%u\n",
                SwitchLinks, RelitLinks, BlockedLinks, SpatialLinks, Batches);
    Require(SwitchLinks == Batches * 5120 && RelitLinks == Batches * 5120 && BlockedLinks == Batches * 5120 &&
                SpatialLinks > 0, "Weighted multibounce history was not exercised");
    std::printf("PASS weighted four-hit textured room, relighting and moving blocker\n");
}
} // namespace
