/*==========================================================================================================================================
                                               REPLAYENDPOINTVALIDATION.H
==========================================================================================================================================*/
// 🧩 Reproduce and measure shadow segments near an explicitly sampled emissive triangle.
#pragma once
#include <bit>

namespace
{
LightComputeArchive ReplayEndpointFixture(float BlockHeight, float BlockStart)
{
    auto Scene = ReplayOcclusionFixture(BlockStart);
    std::vector<std::array<SpatialVector,3>> Corners;
    for (auto& Facet : Scene.Surfaces)
    {
        if (Facet.Emissive[0] == 0)
            for (auto* Point : {&Facet.A, &Facet.B, &Facet.C}) (*Point)[2] = BlockHeight;
        Corners.push_back({SpatialVector{Facet.A[0],Facet.A[1],Facet.A[2]},
                           SpatialVector{Facet.B[0],Facet.B[1],Facet.B[2]},
                           SpatialVector{Facet.C[0],Facet.C[1],Facet.C[2]}});
    }
    const auto Prior = Scene.Surfaces;
    TriangleVisibilitySpace Space;
    Space.Build(Corners);
    Scene.Visibility = Space.PackFloat32();
    for (size_t I = 0; I < Scene.Surfaces.size(); ++I)
    {
        Scene.Surfaces[I] = Prior[Scene.Visibility.OriginalFacets[I]];
        Scene.Surfaces[I].A = Scene.Visibility.Corners[I].A;
        Scene.Surfaces[I].B = Scene.Visibility.Corners[I].B;
        Scene.Surfaces[I].C = Scene.Visibility.Corners[I].C;
    }
    return Scene;
}
LightComputeArchive ReplayEndpointOverflow(LightComputeArchive Scene)
{
    for (uint32_t I = 0; I < 8; ++I)
    {
        LightComputeFacet F;
        const float X = 20 + float(I);
        F.A = {X,0,0,0}; F.B = {X + .5f,0,0,0}; F.C = {X,.5f,0,0};
        F.NormalA = F.NormalB = F.NormalC = {0,0,1,0};
        F.AlbedoRoughness = {0,0,0,1}; F.OpticalToken = {0,1.5f,0,2};
        Scene.Surfaces.push_back(F);
    }
    RebuildMaterialVisibility(Scene);
    return Scene;
}
void ValidateReplayEndpoint(const char* Driver, const std::string& Shaders)
{
    const auto Open = ReplayEndpointFixture(.999995f, 7);
    const auto Near = ReplayEndpointFixture(.999995f, -5);
    const auto Far = ReplayEndpointFixture(.9999f, -5);
    const auto Beyond = ReplayEndpointFixture(1.000005f, -5);
    const auto Half = ReplayEndpointFixture(.999995f, 0);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    DirectGpuMoments OpenMoments, NearMoments, FarMoments, BeyondMoments, HalfMoments;
    DirectGpuMoments BlockedReuse, UnblockedReuse;
    uint32_t BlockedAccepted = 0, UnblockedAccepted = 0, SpatialLinks = 0;
    for (uint32_t Batch = 0; Batch < 4; ++Batch)
    {
        const uint32_t Seed = 57000 + Batch * 100;
        auto Trace = [&](const LightComputeArchive& Scene, DirectGpuMoments& Samples, uint32_t Salt)
        {
            const auto Pixels = Compute.ExecuteReplay(Scene, 1, Seed + Salt, false, false, true,
                                                      {0,0,0}, .01f, false, false, false, true);
            AccumulateReplayCentres(Samples, Pixels, Scene.Width);
        };
        Trace(Open, OpenMoments, 1); Trace(Near, NearMoments, 2);
        Trace(Far, FarMoments, 3); Trace(Beyond, BeyondMoments, 4); Trace(Half, HalfMoments, 5);
        for (uint32_t Frame = 0; Frame < 6; ++Frame)
            (void)Compute.ExecuteReplay(Open, 4, Seed + 10 + Frame, true, false, Frame == 0,
                                        {0,0,0}, .01f, false, false, false, true);
        const auto Blocked = Compute.ExecuteReplay(Near, 4, Seed + 20, true, true, false,
                                                   {0,0,0}, .01f, false, false, false, true);
        const auto BlockCounts = Compute.ExtendedStatistics();
        BlockedAccepted += BlockCounts[20]; SpatialLinks += BlockCounts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving near-source blocker did not retain capped history");
        AccumulateReplayCentres(BlockedReuse, Blocked, Near.Width);
        for (uint32_t Frame = 0; Frame < 6; ++Frame)
            (void)Compute.ExecuteReplay(Near, 4, Seed + 30 + Frame, true, false, Frame == 0,
                                        {0,0,0}, .01f, false, false, false, true);
        const auto Unblocked = Compute.ExecuteReplay(Beyond, 4, Seed + 40, true, true, false,
                                                     {0,0,0}, .01f, false, false, false, true);
        const auto OpenCounts = Compute.ExtendedStatistics();
        UnblockedAccepted += OpenCounts[20]; SpatialLinks += OpenCounts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving blocker beyond the source did not retain capped history");
        AccumulateReplayCentres(UnblockedReuse, Unblocked, Beyond.Width);
    }
    const double Integral = ReplayOccludedAreaIntegral(0, 7);
    const float SourceHeight = 1, BlockHeight = .999995f;
    const uint32_t Ulps = std::bit_cast<uint32_t>(SourceHeight) - std::bit_cast<uint32_t>(BlockHeight);
    std::printf("ENDPOINT_VISIBILITY source_z=%.9g near_blocker_z=%.9g gap=%.9g float_ulps=%u reference=%.9g "
                "open=%.9g open_se=%.9g near=%.9g farther=%.9g beyond=%.9g beyond_se=%.9g n=%u\n",
                SourceHeight, BlockHeight, SourceHeight - BlockHeight, Ulps, Integral, OpenMoments.Mean(),
                OpenMoments.StandardError(), NearMoments.Mean(), FarMoments.Mean(), BeyondMoments.Mean(),
                BeyondMoments.StandardError(), NearMoments.Count);
    Require(Ulps > 16 && std::abs(OpenMoments.Mean()-Integral) < 6*OpenMoments.StandardError()+.002 &&
                NearMoments.Mean() == 0 && FarMoments.Mean() == 0 &&
                std::abs(BeyondMoments.Mean()-Integral) < 6*BeyondMoments.StandardError()+.002,
            "Endpoint visibility failed near, far, open or beyond-source reference");
    std::printf("ENDPOINT_PARTIAL exact=%.9g mean=%.9g se=%.9g n=%u\n", Integral / 2, HalfMoments.Mean(),
                HalfMoments.StandardError(), HalfMoments.Count);
    std::printf("ENDPOINT_DYNAMIC four_hits blocked=%.9g revealed=%.9g se=%.9g accepted=%u/%u spatial=%u n=%u\n",
                BlockedReuse.Mean(), UnblockedReuse.Mean(), UnblockedReuse.StandardError(), BlockedAccepted,
                UnblockedAccepted, SpatialLinks, UnblockedReuse.Count);
    Require(std::abs(HalfMoments.Mean() - Integral / 2) < 6*HalfMoments.StandardError()+.002 &&
                BlockedReuse.Mean() == 0 &&
                std::abs(UnblockedReuse.Mean() - Integral) < 6*UnblockedReuse.StandardError()+.002 &&
                BlockedAccepted == 4*5120 && UnblockedAccepted == 4*5120 && SpatialLinks > 0,
            "Near-endpoint partial shadow or moving four-hit reuse failed");
    const auto OverflowNear = ReplayEndpointOverflow(Near), OverflowBeyond = ReplayEndpointOverflow(Beyond);
    LightComputeSubmission Overflow(Driver);
    Overflow.Prepare(OverflowNear, Shaders, false, false, false, false, true);
    const auto FallbackBlocked = Overflow.ExecuteReplay(OverflowNear, 1, 58001, false, false, true,
                                                        {0,0,0}, .01f, false, false, false, true, false, false, 1);
    const auto BlockFallbacks = Overflow.ExtendedStatistics()[3];
    const auto FallbackOpen = Overflow.ExecuteReplay(OverflowBeyond, 1, 58002, false, false, true,
                                                     {0,0,0}, .01f, false, false, false, true, false, false, 1);
    const auto OpenFallbacks = Overflow.ExtendedStatistics()[3];
    const auto FallbackMoments = DirectGpuCentres(FallbackOpen, Beyond.Width);
    std::printf("ENDPOINT_FALLBACK stack_limit=1 blocked=%.9g open=%.9g se=%.9g fallbacks=%u/%u\n",
                DirectGpuCentres(FallbackBlocked, Near.Width).Mean(), FallbackMoments.Mean(),
                FallbackMoments.StandardError(), BlockFallbacks, OpenFallbacks);
    Require(BlockFallbacks > 0 && OpenFallbacks > 0 && DirectGpuCentres(FallbackBlocked, Near.Width).Mean() == 0 &&
                std::abs(FallbackMoments.Mean()-Integral) < 6*FallbackMoments.StandardError()+.002,
            "Near-endpoint brute-force overflow fallback did not preserve segment visibility");
    std::printf("PASS bounded open-segment visibility around a sampled emitter\n");
}
} // namespace
