/*==========================================================================================================================================
                                                PATHREPLAYGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare dynamic BVH path replay against independent area integrals and bounded multihit scenes.
#pragma once

namespace
{
LightComputeArchive ReplayOcclusionFixture(float BlockStart)
{
    auto Scene = DirectGpuFixture(false);
    Scene.Lights.clear();
    for (auto& R : Scene.Receivers) if (R.PositionHit[3] != 0) R.AlbedoIor = {.5f,.5f,.5f,1.5f};
    const SpatialVector A{-.5,-.5,1}, B{.5,-.5,1}, C{.5,.5,1}, D{-.5,.5,1};
    const SpatialVector E{BlockStart,-5,.5}, F{5,-5,.5}, G{5,5,.5}, H{BlockStart,5,.5};
    TriangleVisibilitySpace Space;
    Space.Build({{A,B,C}, {A,C,D}, {E,F,G}, {E,G,H}});
    Scene.Visibility = Space.PackFloat32();
    for (size_t I = 0; I < Scene.Visibility.Corners.size(); ++I)
    {
        const auto& T = Scene.Visibility.Corners[I];
        const bool Source = Scene.Visibility.OriginalFacets[I] < 2;
        LightComputeFacet Facet;
        Facet.A = T.A; Facet.B = T.B; Facet.C = T.C;
        Facet.NormalA = Facet.NormalB = Facet.NormalC = {0,0,-1,0};
        Facet.AlbedoRoughness = {0,0,0,1};
        Facet.OpticalToken = {0,1.5f,0,1};
        if (Source) Facet.Emissive = {2,2,2,0};
        Scene.Surfaces.push_back(Facet);
    }
    return Scene;
}
LightComputeArchive ReplayMultibounceFixture(bool Covered, bool LowerReflective = false)
{
    auto Scene = DirectGpuFixture(false);
    Scene.Lights.clear();
    for (auto& R : Scene.Receivers) if (R.PositionHit[3] != 0) R.AlbedoIor = {.5f,.5f,.5f,1.5f};
    constexpr double W = 1000;
    const SpatialVector A{-W,-W,1}, B{W,-W,1}, C{W,W,1}, D{-W,W,1};
    const SpatialVector E{-W,-W,-1}, F{W,-W,-1}, G{W,W,-1}, H{-W,W,-1};
    const double X0 = Covered ? -10000 : 100000, X1 = Covered ? 10000 : 120000;
    const SpatialVector I{X0,-10000,-.5}, J{X1,-10000,-.5}, K{X1,10000,-.5}, L{X0,10000,-.5};
    TriangleVisibilitySpace Space;
    Space.Build({{A,B,C},{A,C,D},{E,F,G},{E,G,H},{I,J,K},{I,K,L}});
    Scene.Visibility = Space.PackFloat32();
    for (size_t Index = 0; Index < Scene.Visibility.Corners.size(); ++Index)
    {
        const auto& T = Scene.Visibility.Corners[Index];
        const uint32_t Original = Scene.Visibility.OriginalFacets[Index];
        LightComputeFacet Facet;
        Facet.A = T.A; Facet.B = T.B; Facet.C = T.C;
        Facet.NormalA = Facet.NormalB = Facet.NormalC = Original < 2 ? std::array<float,4>{0,0,-1,0}
                                                                      : std::array<float,4>{0,0,1,0};
        Facet.AlbedoRoughness = (Original < 2 || (LowerReflective && Original < 4))
                                    ? std::array<float,4>{.5f,.5f,.5f,1} : std::array<float,4>{0,0,0,1};
        Facet.OpticalToken = {0,1.5f,0,1};
        if (Original >= 2 && Original < 4) Facet.Emissive = {2,2,2,0};
        Scene.Surfaces.push_back(Facet);
    }
    return Scene;
}
double ReplayOccludedAreaIntegral(double ReceiverX, double BlockStart, uint32_t Steps = 1000)
{
    const double H = .999, T = .499 / H, Step = 1.0 / Steps;
    double Sum = 0;
    for (uint32_t Y = 0; Y < Steps; ++Y)
        for (uint32_t X = 0; X < Steps; ++X)
        {
            const double Px = (X + .5) * Step - .5, Py = (Y + .5) * Step - .5;
            const double BlockX = ReceiverX + (Px - ReceiverX) * T, BlockY = Py * T;
            if (BlockX >= BlockStart && BlockX <= 5 && std::abs(BlockY) <= 5) continue;
            const double D2 = H * H + (Px - ReceiverX) * (Px - ReceiverX) + Py * Py;
            Sum += H * H / (D2 * D2);
        }
    return Sum * Step * Step / 3.14159265358979323846;
}
void AccumulateReplayCentres(DirectGpuMoments& Moments, const std::vector<std::array<float,4>>& Image, uint32_t Width)
{
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
        {
            const auto& Colour = Image[size_t(3 * Y + 1) * Width + 3 * X + 1];
            Require(std::isfinite(Colour[0]) && std::isfinite(Colour[1]) && std::isfinite(Colour[2]),
                    "Nonfinite replay output");
            Moments.Accumulate(Colour[0]);
        }
}
void ValidatePathReplayGpu(const char* Driver, const std::string& Shaders)
{
    const auto Covered = ReplayOcclusionFixture(-5);
    const auto Half = ReplayOcclusionFixture(0);
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY preparing Vulkan pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Covered, Shaders, false, false, false, false, true);
    const double Reference = ReplayOccludedAreaIntegral(0, 0);
    const double Coarse = ReplayOccludedAreaIntegral(0, 0, 500);
    DirectGpuMoments Fresh, Reused, LongHistory;
    uint32_t Accepted = 0;
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 1000 + 100 * Batch;
        AccumulateReplayCentres(Fresh, Compute.ExecuteReplay(Half, 1, Seed + 40, false, false, true), Half.Width);
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
        {
            const auto Dark = Compute.ExecuteReplay(Covered, 1, Seed + Frame, true, false, Frame == 0);
            Require(DirectGpuCentres(Dark, Covered.Width).Mean() == 0, "Full occlusion should be dark");
        }
        AccumulateReplayCentres(Reused, Compute.ExecuteReplay(Half, 1, Seed + 9, true, false), Half.Width);
        Accepted += Compute.ExtendedStatistics()[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Changed occluder unexpectedly reset history or failed to cap it");
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(Covered, 1, Seed + Frame, true, false, Frame == 0);
        AccumulateReplayCentres(LongHistory, Compute.ExecuteReplay(Half, 1, Seed + 9, true, false, false,
                                                                   {0,0,0}, .01f, false, true), Half.Width);
        Require(Compute.ReplayLastHistoryCap == 64, "Long-history control did not retain its count");
    }
    std::printf("REPLAY_DISOCCLUSION exact=%.12g quadrature_delta=%.9g fresh=%.12g se=%.12g "
                "capped=%.12g se=%.12g long=%.12g se=%.12g capped_vs_long_std=%.9g accepted=%u n=%u\n", Reference,
                std::abs(Reference-Coarse), Fresh.Mean(), Fresh.StandardError(), Reused.Mean(), Reused.StandardError(),
                LongHistory.Mean(), LongHistory.StandardError(), Reused.Deviation()/LongHistory.Deviation(),
                Accepted, Reused.Count);
    Require(Accepted == 4 * 5120 && std::abs(Reference-Coarse) < 1e-6 &&
                std::abs(Fresh.Mean()-Reference) < 6*Fresh.StandardError()+.002 &&
                std::abs(Reused.Mean()-Reference) < 6*Reused.StandardError()+.002 &&
                std::abs(LongHistory.Mean()-Reference) < 6*LongHistory.StandardError()+.002 &&
                Reused.Deviation() < .9 * LongHistory.Deviation(),
            "Replay after disocclusion failed the area integral or long-history variance control");
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
        (void)Compute.ExecuteReplay(Half, 1, 2000 + Frame, true, false, Frame == 0);
    const auto Darkened = Compute.ExecuteReplay(Covered, 1, 2010, true, false);
    const auto DarkCounts = Compute.ExtendedStatistics();
    std::printf("REPLAY_OCCLUSION mean=%.12g accepted=%u reset=%u\n",
                DirectGpuCentres(Darkened, Covered.Width).Mean(), DarkCounts[20], Compute.ReplayLastReset ? 1u : 0u);
    Require(DarkCounts[20] == 5120 && !Compute.ReplayLastReset && DirectGpuCentres(Darkened, Covered.Width).Mean() == 0,
            "Moved occluder retained stale light");
    auto Shifted = ReplayOcclusionFixture(.1f);
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
        {
            const size_t Centre = size_t(3 * Y + 1) * Shifted.Width + 3 * X + 1;
            Shifted.Receivers[Centre].PositionHit[0] = .5f;
            Shifted.Receivers[Centre-1].PositionHit[0] = -.2f;
            Shifted.Receivers[Centre+1].PositionHit[0] = .2f;
            Shifted.Receivers[Centre-Shifted.Width].PositionHit[0] = .1f;
            Shifted.Receivers[Centre+Shifted.Width].PositionHit[0] = .3f;
        }
    const double ShiftReference = ReplayOccludedAreaIntegral(.5, .1);
    DirectGpuMoments ShiftedMoments;
    uint32_t ShiftAccepted = 0, SpatialLinks = 0;
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 3000 + 100 * Batch;
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(Covered, 1, Seed + Frame, true, false, Frame == 0);
        AccumulateReplayCentres(ShiftedMoments, Compute.ExecuteReplay(Shifted, 1, Seed + 9, true, true), Shifted.Width);
        const auto Counts = Compute.ExtendedStatistics();
        ShiftAccepted += Counts[20]; SpatialLinks += Counts[22];
    }
    std::printf("REPLAY_MOVING_SPATIAL exact=%.12g mean=%.12g se=%.12g accepted=%u links=%u n=%u\n",
                ShiftReference, ShiftedMoments.Mean(), ShiftedMoments.StandardError(), ShiftAccepted, SpatialLinks,
                ShiftedMoments.Count);
    Require(ShiftAccepted == 4 * 5120 && SpatialLinks > 0 &&
                std::abs(ShiftedMoments.Mean()-ShiftReference) < 6*ShiftedMoments.StandardError()+.002,
            "Moving receiver, occluder and spatial replay failed independent area integral");
    // Change source radiance without changing triangle positions or throwing away dark history.
    auto Unlit = Half;
    for (auto& F : Unlit.Surfaces) F.Emissive = {0,0,0,0};
    DirectGpuMoments Relit;
    uint32_t RelitAccepted = 0;
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 3500 + 100 * Batch;
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(Unlit, 1, Seed + Frame, true, false, Frame == 0);
        AccumulateReplayCentres(Relit, Compute.ExecuteReplay(Half, 1, Seed + 9, true, false), Half.Width);
        RelitAccepted += Compute.ExtendedStatistics()[20];
        Require(!Compute.ReplayLastReset, "Changed source radiance unexpectedly reset replay history");
    }
    std::printf("REPLAY_RELIGHT exact=%.12g mean=%.12g se=%.12g accepted=%u n=%u\n", Reference,
                Relit.Mean(), Relit.StandardError(), RelitAccepted, Relit.Count);
    Require(RelitAccepted == 4 * 5120 && std::abs(Relit.Mean()-Reference) < 6*Relit.StandardError()+.002,
            "Changed source radiance failed independent area integral");
    // The same prepared buffers have room for six-facet tests only after a separate submission is prepared.
    const auto MultiDark = ReplayMultibounceFixture(true), MultiOpen = ReplayMultibounceFixture(false);
    LightComputeSubmission Multi(Driver);
    Multi.Prepare(MultiDark, Shaders, false, false, false, false, true);
    const auto OneHit = Multi.ExecuteReplay(MultiOpen, 1, 4000, false, false, true);
    const auto TwoHits = Multi.ExecuteReplay(MultiOpen, 2, 4000, false, false, true);
    const double OneMean = DirectGpuCentres(OneHit, MultiOpen.Width).Mean();
    const double TwoMean = DirectGpuCentres(TwoHits, MultiOpen.Width).Mean();
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
        (void)Multi.ExecuteReplay(MultiDark, 2, 4100 + Frame, true, false, Frame == 0);
    const auto MultiRevealed = Multi.ExecuteReplay(MultiOpen, 2, 4110, true, false);
    const auto MultiCounts = Multi.ExtendedStatistics();
    const auto MultiMoments = DirectGpuCentres(MultiRevealed, MultiOpen.Width);
    std::printf("REPLAY_MULTIBOUNCE one_hit=%.12g two_hits=%.12g revealed=%.12g se=%.12g "
                "accepted=%u reset=%u expected_nearly=.5\n", OneMean, TwoMean, MultiMoments.Mean(),
                MultiMoments.StandardError(), MultiCounts[20], Multi.ReplayLastReset ? 1u : 0u);
    Require(OneMean == 0 && std::abs(TwoMean-.5) < .002 && std::abs(MultiMoments.Mean()-.5) < .002 &&
                MultiCounts[20] == 5120 && !Multi.ReplayLastReset,
            "Moving blocker with two-hit diffuse replay failed");
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Multi.ExecuteReplay(MultiOpen, 1, 4200 + Frame, true, false, Frame == 0);
    const auto Deeper = Multi.ExecuteReplay(MultiOpen, 2, 4210, true, false);
    const auto DepthCounts = Multi.ExtendedStatistics();
    std::printf("REPLAY_DEPTH_CHANGE mean=%.12g accepted=%u reset=%u\n",
                DirectGpuCentres(Deeper, MultiOpen.Width).Mean(), DepthCounts[20], Multi.ReplayLastReset ? 1u : 0u);
    Require(DepthCounts[20] == 5120 && !Multi.ReplayLastReset &&
                std::abs(DirectGpuCentres(Deeper, MultiOpen.Width).Mean()-.5) < .002,
            "Changing bounce budget did not replay source sequences");
    const auto Reflective = ReplayMultibounceFixture(false, true);
    const auto FourHits = Multi.ExecuteReplay(Reflective, 4, 4300, false, false, true);
    const double FourMean = DirectGpuCentres(FourHits, Reflective.Width).Mean();
    std::printf("REPLAY_FOUR_HITS mean=%.12g expected_nearly=.625\n", FourMean);
    Require(std::abs(FourMean-.625) < .002, "Four-hit parallel-plane diffuse transport failed");
    auto Colour = MultiOpen;
    for (auto& F : Colour.Surfaces)
        if (F.Emissive[0] > 0) F.Emissive = {2,1,.5f,0};
    const auto Coloured = Multi.ExecuteReplay(Colour, 2, 4400, true, true, true);
    DirectGpuMoments Channels[3];
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
        {
            const auto& Pixel = Coloured[size_t(3*Y+1)*Colour.Width+3*X+1];
            for (uint32_t C = 0; C < 3; ++C) Channels[C].Accumulate(Pixel[C]);
        }
    std::printf("REPLAY_RGB expected=(.5,.25,.125) mean=(%.12g,%.12g,%.12g)\n",
                Channels[0].Mean(), Channels[1].Mean(), Channels[2].Mean());
    Require(std::abs(Channels[0].Mean()-.5) < .002 && std::abs(Channels[1].Mean()-.25) < .002 &&
                std::abs(Channels[2].Mean()-.125) < .002, "RGB replay transport failed");
    bool RejectedFloor = false, RejectedMaterial = false, RejectedBvh = false;
    try { (void)Compute.ExecuteReplay(Half, 1, 1, true, false, true, {0,0,0}, 0); }
    catch (const std::exception&) { RejectedFloor = true; }
    auto Unsupported = Half;
    Unsupported.Surfaces[0].OpticalToken[0] = 1;
    try { (void)Compute.ExecuteReplay(Unsupported, 1, 1, true, false); }
    catch (const std::exception&) { RejectedMaterial = true; }
    auto Broken = Half;
    Broken.Visibility.Enclosures[0].Links[0] = 999;
    try { (void)Compute.ExecuteReplay(Broken, 1, 1, true, false); }
    catch (const std::exception&) { RejectedBvh = true; }
    std::printf("REPLAY_GUARDS zero_floor=%u metallic=%u corrupt_bvh=%u\n", RejectedFloor ? 1u : 0u,
                RejectedMaterial ? 1u : 0u, RejectedBvh ? 1u : 0u);
    Require(RejectedFloor && RejectedMaterial && RejectedBvh, "Replay domain guards did not reject invalid input");
    ReplayCaptureResult PreviewFixture;
    PreviewFixture.Pixels = {{1,0,0,1},{3,0,0,1},{0,0,3,1},{0,1,0,1}};
    PreviewFixture.Guides.resize(4);
    for (uint32_t I = 0; I < 3; ++I)
    {
        PreviewFixture.Guides[I].PositionHit[3] = 1;
        PreviewFixture.Guides[I].NormalRoughness = {0,0,1,0};
        PreviewFixture.Guides[I].MotionPositionToken[3] = I == 2 ? 2 : 1;
    }
    const auto Preview = FilterReplayPreview(PreviewFixture, 4, 1);
    Require(Preview[0][0] > 1 && Preview[0][0] < 3 && Preview[1][0] > 1 && Preview[1][0] < 3 &&
                Preview[2][2] == 3 && Preview[2][0] == 0 && Preview[3][1] == 1,
            "Display filter mixed surface tokens or missed its smoothing range");
    PreviewFixture.Guides[0].AlbedoIor = {1,0,0,1};
    PreviewFixture.Guides[1].AlbedoIor = {0,0,1,1};
    const auto AlbedoPreview = FilterReplayPreview(PreviewFixture, 4, 1, true);
    Require(AlbedoPreview[0][0] == 1 && AlbedoPreview[1][0] == 3,
            "Texture preview blurred across a large albedo edge");
    std::printf("REPLAY_PREVIEW_FILTER same_surface_smoothed=1 cross_surface_rejected=1 "
                "background_preserved=1 albedo_edge_rejected=1\n");
    std::printf("PASS bounded diffuse replay with moving occluder; general materials and production frame remain open\n");
}
} // namespace
