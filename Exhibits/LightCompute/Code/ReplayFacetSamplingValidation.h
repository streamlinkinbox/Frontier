/*==========================================================================================================================================
                                             REPLAYFACETSAMPLINGVALIDATION.H
==========================================================================================================================================*/
// 🧩 Validate explicit emissive-facet sampling, moving occluders and double-count controls.
#pragma once

namespace
{
LightComputeArchive ReplayFacetFixture(float HalfSide, float BlockStart, bool ExtraSource = false)
{
    auto Scene = DirectGpuFixture(false);
    Scene.Lights.clear();
    for (auto& Receiver : Scene.Receivers)
        if (Receiver.PositionHit[3] != 0) Receiver.AlbedoIor = {.5f,.5f,.5f,1.5f};
    const double H = HalfSide;
    const SpatialVector A{-H,-H,1}, B{H,-H,1}, C{H,H,1}, D{-H,H,1};
    const SpatialVector E{BlockStart,-5,.5}, F{5,-5,.5}, G{5,5,.5}, J{BlockStart,5,.5};
    std::vector<std::array<SpatialVector,3>> Geometry{{A,B,C},{A,C,D},{E,F,G},{E,G,J}};
    if (ExtraSource) Geometry.push_back({SpatialVector{.7,-.5,1},SpatialVector{1.3,-.5,1},SpatialVector{.7,.5,1}});
    TriangleVisibilitySpace Space;
    Space.Build(std::move(Geometry));
    Scene.Visibility = Space.PackFloat32();
    for (size_t Index = 0; Index < Scene.Visibility.Corners.size(); ++Index)
    {
        const auto& T = Scene.Visibility.Corners[Index];
        const uint32_t Original = Scene.Visibility.OriginalFacets[Index];
        LightComputeFacet Facet;
        Facet.A = T.A; Facet.B = T.B; Facet.C = T.C;
        Facet.NormalA = Facet.NormalB = Facet.NormalC = {0,0,-1,0};
        Facet.AlbedoRoughness = {0,0,0,1};
        Facet.OpticalToken = {0,1.5f,0,1};
        if (Original < 2 || Original == 4) Facet.Emissive = {2,2,2,0};
        Scene.Surfaces.push_back(Facet);
    }
    return Scene;
}
double ReplayFacetAreaIntegral(double HalfSide, double BlockStart, bool ExtraSource, uint32_t Steps = 1000)
{
    constexpr double Height = .999, BlockHeight = .499, Pi = 3.14159265358979323846;
    const double Step = 2*HalfSide/Steps;
    double Sum = 0;
    for (uint32_t Y = 0; Y < Steps; ++Y)
        for (uint32_t X = 0; X < Steps; ++X)
        {
            const double Px = -HalfSide+(X+.5)*Step, Py = -HalfSide+(Y+.5)*Step;
            if (Px * BlockHeight / Height >= BlockStart) continue;
            const double Squared = Height*Height+Px*Px+Py*Py;
            Sum += Height*Height/(Squared*Squared);
        }
    double Integral = Sum*Step*Step/Pi;
    if (ExtraSource)
    {
        double TriangleSum = 0;
        for (uint32_t U = 0; U < Steps; ++U)
            for (uint32_t V = 0; V < Steps; ++V)
            {
                const double Root = std::sqrt((U+.5)/Steps), Along = (V+.5)/Steps;
                const double Px = .7 + .6*Root*(1-Along), Py = -.5 + Root*Along;
                if (Px * BlockHeight / Height >= BlockStart) continue;
                const double Squared = Height*Height+Px*Px+Py*Py;
                TriangleSum += Height*Height/(Squared*Squared);
            }
        Integral += TriangleSum * .3 / (double(Steps)*Steps*Pi);
    }
    return Integral;
}
std::array<double,3> ReplayRoughAreaIntegral(const LightComputeReceiver& Receiver, double HalfSide,
                                              double BlockStart, uint32_t Steps = 1000)
{
    constexpr double Height = .999, BlockHeight = .499;
    const double Step = 2*HalfSide/Steps;
    std::array<double,3> Sum{};
    for (uint32_t Y = 0; Y < Steps; ++Y)
        for (uint32_t X = 0; X < Steps; ++X)
        {
            const double Px = -HalfSide+(X+.5)*Step, Py = -HalfSide+(Y+.5)*Step;
            if (Px * BlockHeight / Height >= BlockStart) continue;
            const double Squared = Height*Height+Px*Px+Py*Py, Distance = std::sqrt(Squared);
            const auto Value = RoughReplayBrdf(Receiver, {Px/Distance,Py/Distance,Height/Distance});
            for (uint32_t C = 0; C < 3; ++C) Sum[C] += Value[C] * 2 * Height*Height/(Squared*Squared);
        }
    for (double& Value : Sum) Value *= Step*Step;
    return Sum;
}
void ValidateReplayFacetSampling(const char* Driver, const std::string& Shaders)
{
    const auto Small = ReplayFacetFixture(.05f, 2), Open = ReplayFacetFixture(.5f, 2),
               Half = ReplayFacetFixture(.5f, 0), Covered = ReplayFacetFixture(.5f, -5),
               Three = ReplayFacetFixture(.5f, 2, true);
    LightComputeSubmission Compute(Driver);
    std::printf("FACET_REPLAY preparing Vulkan on %s\n", Compute.Properties.deviceName); std::fflush(stdout);
    Compute.Prepare(Three, Shaders, false, false, false, false, true);
    const double SmallExact = ReplayFacetAreaIntegral(.05, 2, false);
    const double OpenExact = ReplayFacetAreaIntegral(.5, 2, false);
    const double HalfExact = ReplayFacetAreaIntegral(.5, 0, false);
    const double ThreeExact = ReplayFacetAreaIntegral(.5, 2, true);
    Require(std::abs(ThreeExact-ReplayFacetAreaIntegral(.5,2,true,500)) < 2e-6,
            "Three-facet area quadrature did not converge");
    auto UnlitScene = Open;
    for (auto& Facet : UnlitScene.Surfaces) Facet.Emissive = {0,0,0,0};
    DirectGpuMoments SmallBsdf, SmallExplicit, OpenExplicit, DoubleCount, ThreeExplicit, Dynamic, Relit;
    uint32_t Accepted = 0, Links = 0, RelitAccepted = 0;
    const std::array<float,3> DarkEnvironment{0,0,0};
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 22000+100*Batch;
        AccumulateReplayCentres(SmallBsdf, Compute.ExecuteReplay(Small, 1, Seed, false, false, true), Small.Width);
        AccumulateReplayCentres(SmallExplicit, Compute.ExecuteReplay(Small, 1, Seed+1, false, false, true,
                                   DarkEnvironment, .01f, false, false, false, true), Small.Width);
        AccumulateReplayCentres(OpenExplicit, Compute.ExecuteReplay(Open, 1, Seed+2, false, false, true,
                                   DarkEnvironment, .01f, false, false, false, true), Open.Width);
        AccumulateReplayCentres(DoubleCount, Compute.ExecuteReplay(Open, 1, Seed+3, false, false, true,
                                   DarkEnvironment, .01f, false, false, false, true, true), Open.Width);
        AccumulateReplayCentres(ThreeExplicit, Compute.ExecuteReplay(Three, 1, Seed+4, false, false, true,
                                   DarkEnvironment, .01f, false, false, false, true), Three.Width);
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(Covered, 1, Seed+10+Frame, true, false, Frame == 0,
                                        DarkEnvironment, .01f, false, false, false, true);
        AccumulateReplayCentres(Dynamic, Compute.ExecuteReplay(Half, 1, Seed+19, true, true, false,
                                   DarkEnvironment, .01f, false, false, false, true), Half.Width);
        const auto Counts = Compute.ExtendedStatistics();
        Accepted += Counts[20]; Links += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Facet-sampling disocclusion did not retain and cap temporal history");
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(UnlitScene, 1, Seed+30+Frame, true, false, Frame == 0,
                                        DarkEnvironment, .01f, false, false, false, true);
        AccumulateReplayCentres(Relit, Compute.ExecuteReplay(Open, 1, Seed+39, true, true, false,
                                 DarkEnvironment, .01f, false, false, false, true), Open.Width);
        RelitAccepted += Compute.ExtendedStatistics()[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Changing the emissive-facet list did not retain and cap history");
    }
    auto Report = [](const char* Name, double Expected, const DirectGpuMoments& Result)
    {
        std::printf("FACET_REPLAY %s exact=%.12g mean=%.12g se=%.12g std=%.12g n=%u\n", Name, Expected,
                    Result.Mean(), Result.StandardError(), Result.Deviation(), Result.Count);
        Require(std::abs(Result.Mean()-Expected) < 6*Result.StandardError()+std::max(1e-6, .005*Expected),
                "Facet sampling disagrees with independent area integral");
    };
    Report("small_bsdf", SmallExact, SmallBsdf);
    Report("small_explicit", SmallExact, SmallExplicit);
    Report("open_explicit", OpenExact, OpenExplicit);
    Report("three_sources", ThreeExact, ThreeExplicit);
    Report("disocclusion", HalfExact, Dynamic);
    Report("source_appears", OpenExact, Relit);
    Report("double_count_control", 2*OpenExact, DoubleCount);
    const double Ratio = SmallExplicit.Deviation()/SmallBsdf.Deviation();
    std::printf("FACET_REPLAY variance_control small_std_ratio=%.9g double_count_vs_exact=%.9g "
                "accepted=%u relit_accepted=%u links=%u\n", Ratio, DoubleCount.Mean()/OpenExact, Accepted, RelitAccepted, Links);
    Require(Ratio < .1 && DoubleCount.Mean() > OpenExact + 5*DoubleCount.StandardError() &&
                Accepted == 8*5120 && RelitAccepted == 8*5120 && Links > 0,
            "Facet sampling variance, negative control or reuse did not behave as expected");
    auto Empty = DirectGpuFixture(false);
    Empty.Lights.clear();
    const std::array<float,3> WhiteEnvironment{1,1,1};
    const auto Unlit = Compute.ExecuteReplay(Empty, 1, 23980, false, false, true,
                                             WhiteEnvironment, .01f, false, false, false, true);
    const double EnvironmentMean = DirectGpuCentres(Unlit, Empty.Width).Mean();
    const auto Dark = Compute.ExecuteReplay(Covered, 1, 23981, false, false, true,
                                            DarkEnvironment, .01f, false, false, false, true);
    Require(std::abs(EnvironmentMean-.6) < 1e-6 && DirectGpuCentres(Dark,Covered.Width).Mean() == 0,
            "Explicit sampling changed an empty-source environment or a fully blocked source");
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
    try { (void)Compute.ExecuteReplay(Degenerate, 1, 23982, false, false, true,
                                      DarkEnvironment, .01f, false, false, false, true); }
    catch (const std::exception&) { RejectedDegenerate = true; }
    Require(RejectedDegenerate, "Explicit sampling accepted a degenerate emissive facet");
    std::printf("FACET_REPLAY empty_source_environment=%.9g fully_blocked=0 degenerate_rejected=1\n", EnvironmentMean);
    auto Rough = Half;
    for (auto& Receiver : Rough.Receivers)
        if (Receiver.PositionHit[3] != 0)
        {
            Receiver.NormalRoughness = {0,0,1,.45f};
            Receiver.ViewMetallic = {std::sin(.9f),0,std::cos(.9f),.9f};
            Receiver.AlbedoIor = {.8f,.3f,.1f,1.5f};
            Receiver.ResidualSpecular = {0,0,0,.5f};
        }
    const auto RoughReference = ReplayRoughAreaIntegral(Rough.Receivers[size_t(Rough.Width)+1], .5, 0);
    ReplayGroupedMoments RoughValues;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
        RoughValues.Accumulate(Compute.ExecuteReplay(Rough, 1, 24000+100*Batch, false, false, true,
                                DarkEnvironment, .01f, false, false, true, true), Rough.Width);
    for (uint32_t Channel = 0; Channel < 3; ++Channel)
    {
        const auto& Pixels = RoughValues.Pixels[Channel];
        const double Se = std::max(Pixels.StandardError(), RoughValues.Batches[Channel].StandardError());
        std::printf("FACET_ROUGH channel=%u quadrature=%.12g mean=%.12g se=%.12g n=%u\n", Channel,
                    RoughReference[Channel], Pixels.Mean(), Se, Pixels.Count);
        Require(std::abs(Pixels.Mean()-RoughReference[Channel]) < 6*Se+.002,
                "Rough facet sampling disagrees with area quadrature");
    }
    std::printf("PASS explicit facet sampling on bounded opaque paths; not general materials or analytic lights\n");
}

void ValidateReplayFacetRoom(const char* Driver, const std::string& Shaders)
{
    auto Open = ReplayRoom(7), Blocked = ReplayRoom(0);
    auto Receivers = DirectGpuFixture(false);
    for (auto& Receiver : Receivers.Receivers)
        if (Receiver.PositionHit[3] != 0)
        {
            Receiver.PositionHit = {1.4f,2,0,1}; Receiver.MotionPositionToken = {1.4f,2,0,1};
            Receiver.AlbedoIor = {.7f,.7f,.7f,1.5f};
        }
    Open.Width = Blocked.Width = Receivers.Width; Open.Height = Blocked.Height = Receivers.Height;
    Open.Receivers = Blocked.Receivers = Receivers.Receivers;
    std::printf("FACET_ROOM tracing two 400000-path independent CPU references\n"); std::fflush(stdout);
    const auto CpuOpen = ReplayReferenceIntegrate(Open, 4, 349155);
    const auto CpuBlocked = ReplayReferenceIntegrate(Blocked, 4, 819432);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    ReplayGroupedMoments FreshOpen, FreshBlocked, ReusedBlocked;
    uint32_t Accepted = 0, Links = 0;
    const std::array<float,3> Environment{.025f,.025f,.025f};
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 27000 + 100*Batch;
        FreshOpen.Accumulate(Compute.ExecuteReplay(Open, 4, Seed, false, false, true,
                             Environment, .01f, false, false, false, true), Open.Width);
        FreshBlocked.Accumulate(Compute.ExecuteReplay(Blocked, 4, Seed+1, false, false, true,
                                Environment, .01f, false, false, false, true), Open.Width);
        // 📝 Change both geometry and the path proposal while retaining pre-spatial random-domain history.
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(Open, 4, Seed+10+Frame, true, false, Frame == 0,
                                        Environment, .01f, false, false, false, false);
        ReusedBlocked.Accumulate(Compute.ExecuteReplay(Blocked, 4, Seed+19, true, true, false,
                                 Environment, .01f, false, false, false, true), Open.Width);
        const auto Counts = Compute.ExtendedStatistics();
        Accepted += Counts[20]; Links += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Changed geometry and light-sampling mode lost temporal history");
    }
    ReplayCompareRgb("facet_open_four", CpuOpen, FreshOpen);
    ReplayCompareRgb("facet_blocked_fresh_four", CpuBlocked, FreshBlocked);
    ReplayCompareRgb("facet_blocked_reused_four", CpuBlocked, ReusedBlocked);
    Require(Accepted == 8*5120 && Links > 0, "Facet-sampled room did not exercise dynamic and spatial reuse");
    std::printf("FACET_ROOM accepted=%u spatial_links=%u cpu_rng=mt19937_64 brute_force=1\n", Accepted, Links);
    auto RoughOpen = Open, RoughBlocked = Blocked;
    for (auto* Scene : {&RoughOpen, &RoughBlocked})
    {
        for (auto& Facet : Scene->Surfaces)
        {
            const int Token = int(Facet.OpticalToken[3]);
            Facet.OpticalToken[2] = Token == 6 ? 0 : .5f;
            if (Token == 2 || Token == 4)
            {
                Facet.OpticalToken[0] = Token == 4 ? .7f : .35f;
                Facet.AlbedoRoughness[3] = .6f;
            }
        }
        for (auto& Receiver : Scene->Receivers)
            if (Receiver.PositionHit[3] != 0)
            {
                Receiver.NormalRoughness = {0,0,1,.7f}; Receiver.ViewMetallic = {0,0,1,.2f};
                Receiver.ResidualSpecular = {0,0,0,.5f};
            }
    }
    std::printf("FACET_ROUGH_ROOM tracing two 400000-path CPU references\n"); std::fflush(stdout);
    const auto CpuRoughOpen = ReplayReferenceIntegrate(RoughOpen, 4, 689134, true);
    const auto CpuRoughBlocked = ReplayReferenceIntegrate(RoughBlocked, 4, 594617, true);
    ReplayGroupedMoments RoughFresh, RoughReused;
    uint32_t RoughAccepted = 0, RoughLinks = 0;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 28500 + 100*Batch;
        RoughFresh.Accumulate(Compute.ExecuteReplay(RoughOpen, 4, Seed, false, false, true,
                              Environment, .01f, false, false, true, true), Open.Width);
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(RoughOpen, 4, Seed+10+Frame, true, false, Frame == 0,
                                        Environment, .01f, false, false, true, true);
        RoughReused.Accumulate(Compute.ExecuteReplay(RoughBlocked, 4, Seed+19, true, true, false,
                               Environment, .01f, false, false, true, true), Open.Width);
        const auto Counts = Compute.ExtendedStatistics();
        RoughAccepted += Counts[20]; RoughLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Rough facet-sampled room lost temporal history");
    }
    ReplayCompareRgb("facet_rough_open_four", CpuRoughOpen, RoughFresh);
    ReplayCompareRgb("facet_rough_blocked_reused_four", CpuRoughBlocked, RoughReused);
    Require(RoughAccepted == 8*5120 && RoughLinks > 0, "Rough facet-sampled room did not exercise reuse");
    std::printf("FACET_ROUGH_ROOM accepted=%u spatial_links=%u cpu_rng=mt19937_64 brute_force=1\n",
                RoughAccepted, RoughLinks);
    std::printf("PASS four-hit diffuse and rough facet sampling against independent path references\n");
}
} // namespace
