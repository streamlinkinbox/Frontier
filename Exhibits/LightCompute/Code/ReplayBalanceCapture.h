/*==========================================================================================================================================
                                                  REPLAYBALANCECAPTURE.H
==========================================================================================================================================*/
// 🧩 Capture facet-only and balanced sampling in rooms with differently sized sources and a moving blocker.
#pragma once

namespace
{
LightComputeArchive ReplayScaledSource(float Scale, float Blocker)
{
    auto Scene = ReplayRoom(Blocker);
    for (auto& F : Scene.Surfaces)
        if (F.OpticalToken[3] == 6)
        {
            for (auto* P : {&F.A, &F.B, &F.C})
            {
                (*P)[0] *= Scale;
                (*P)[1] = 3 + ((*P)[1] - 3) * Scale;
            }
            for (uint32_t C = 0; C < 3; ++C) F.Emissive[C] /= Scale * Scale;
        }
    RebuildMaterialVisibility(Scene);
    return Scene;
}
void CaptureReplayBalance(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Frames = 24;
    const auto Small = ReplayScaledSource(.25f, 7), Large = ReplayScaledSource(1.8f, 7),
               Blocked = ReplayScaledSource(1.8f, 0);
    LightComputeSubmission Compute(Driver);
    std::printf("BALANCE_CAPTURE preparing Vulkan on %s (%ux%u facets=%zu)\n", Compute.Properties.deviceName,
                Small.Width, Small.Height, Small.Surfaces.size());
    std::fflush(stdout);
    Compute.Prepare(Small, Shaders, false, false, false, false, true);
    const auto SmallFacet = CaptureReplaySequence(Compute, Small, 4, 71000, true, Frames, true);
    std::printf("BALANCE_CAPTURE small facet-only sequence complete\n"); std::fflush(stdout);
    const auto SmallMixed = CaptureReplaySequence(Compute, Small, 4, 71000, true, Frames, true, nullptr, true);
    std::printf("BALANCE_CAPTURE small balanced sequence complete\n"); std::fflush(stdout);
    const auto LargeFacet = CaptureReplaySequence(Compute, Large, 4, 72000, true, Frames, true);
    std::printf("BALANCE_CAPTURE large facet-only sequence complete\n"); std::fflush(stdout);
    const auto LargeMixed = CaptureReplaySequence(Compute, Large, 4, 72000, true, Frames, true, nullptr, true);
    std::printf("BALANCE_CAPTURE large balanced sequence complete\n"); std::fflush(stdout);
    const auto Occluded = CaptureReplaySequence(Compute, Blocked, 4, 73000, false, Frames, true, nullptr, true);
    std::printf("BALANCE_CAPTURE moving blocker sequence complete\n"); std::fflush(stdout);
    const auto Reopened = CaptureReplaySequence(Compute, Large, 4, 74000, false, Frames, true, nullptr, true);
    std::printf("BALANCE_CAPTURE reopened sequence complete\n"); std::fflush(stdout);
    Require(!Occluded.Reset && !Reopened.Reset && Occluded.Accepted > 0 && Reopened.Accepted > 0 &&
                Occluded.SpatialLinks > 0 && Reopened.SpatialLinks > 0 && Occluded.HistoryCap == 1 &&
                Reopened.HistoryCap == 1, "Balanced motion capture lost capped history");
    std::filesystem::create_directories(Directory);
    auto Store = [&](const char* Name, const ReplayCaptureResult& Image)
    {
        WriteImage(Directory + "/" + Name + ".ppm", Image.Pixels, Small.Width, Small.Height);
        WriteImage(Directory + "/" + Name + "Preview.ppm", FilterReplayPreview(Image, Small.Width, Small.Height),
                   Small.Width, Small.Height);
    };
    Store("BalanceSmallFacet", SmallFacet); Store("BalanceSmallMixed", SmallMixed);
    Store("BalanceLargeFacet", LargeFacet); Store("BalanceLargeMixed", LargeMixed);
    Store("BalanceLargeBlocked", Occluded); Store("BalanceLargeReopened", Reopened);
    std::printf("BALANCE_CAPTURE frames_each=%u averaged_last=%u floor_small=%.9g/%.9g floor_large=%.9g/%.9g "
                "floor_blocked=%.9g floor_reopened=%.9g\n", Frames, Frames/2, SmallFacet.FloorMean,
                SmallMixed.FloorMean, LargeFacet.FloorMean, LargeMixed.FloorMean, Occluded.FloorMean, Reopened.FloorMean);
    std::printf("BALANCE_CAPTURE_LINKS accepted=%u/%u spatial=%u/%u reset=%u/%u cap=%u/%u\n", Occluded.Accepted,
                Reopened.Accepted, Occluded.SpatialLinks, Reopened.SpatialLinks, Occluded.Reset ? 1u : 0u,
                Reopened.Reset ? 1u : 0u, Occluded.HistoryCap, Reopened.HistoryCap);
    Require(Occluded.FloorMean < Reopened.FloorMean, "Moving blocker did not reduce the captured floor luminance");
    std::printf("PASS six balanced-comparison Vulkan readbacks; display exposure=.45 gamma=2.2\n");
}
} // namespace
