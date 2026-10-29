/*==========================================================================================================================================
                                                  REPLAYTEXTURECAPTURE.H
==========================================================================================================================================*/
// 🧩 Capture settled and first-transition Vulkan readbacks for atlas changes and moving occlusion.
#pragma once

namespace
{
void CaptureReplayTexture(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Iterations = 24;
    const auto First = ReplayTexturedRoom(0, 7), Relit = ReplayTexturedRoom(1, 7), Covered = ReplayTexturedRoom(1, 0);
    LightComputeSubmission Compute(Driver);
    std::printf("TEXTURE_CAPTURE preparing Vulkan on %s (%ux%u facets=%zu)\n", Compute.Properties.deviceName,
                First.Width, First.Height, First.Surfaces.size());
    std::fflush(stdout);
    Compute.Prepare(First, Shaders, false, false, false, false, true);
    const auto Initial = CaptureReplaySequence(Compute, First, 4, 111000, true, Iterations,
                                                true, nullptr, true, true, true);
    std::printf("TEXTURE_CAPTURE initial sequence complete\n"); std::fflush(stdout);
    const auto Changed = CaptureReplaySequence(Compute, Relit, 4, 112000, false, Iterations,
                                                true, nullptr, true, true, true);
    std::printf("TEXTURE_CAPTURE atlas-change sequence complete\n"); std::fflush(stdout);
    const auto Blocked = CaptureReplaySequence(Compute, Covered, 4, 113000, false, Iterations,
                                                true, nullptr, true, true, true);
    std::printf("TEXTURE_CAPTURE blocker sequence complete\n"); std::fflush(stdout);
    const auto Reopened = CaptureReplaySequence(Compute, First, 4, 114000, false, Iterations,
                                                 true, nullptr, true, true, true);
    std::printf("TEXTURE_CAPTURE simultaneous atlas/reopening sequence complete\n"); std::fflush(stdout);
    for (const auto* Sequence : {&Changed, &Blocked, &Reopened})
        Require(!Sequence->Reset && Sequence->Accepted > 0 && Sequence->SpatialLinks > 0 &&
                    Sequence->HistoryCap == 1 && Sequence->FirstPixels.size() == First.Receivers.size() &&
                    Sequence->FirstGuides.size() == First.Receivers.size(),
                "Textured capture did not retain capped history and its first transition readback");
    std::filesystem::create_directories(Directory);
    auto Store = [&](const char* Name, const ReplayCaptureResult& Capture)
    {
        WriteImage(Directory + "/" + Name + ".ppm", Capture.Pixels, First.Width, First.Height);
        WriteImage(Directory + "/" + Name + "Preview.ppm",
                   FilterReplayPreview(Capture, First.Width, First.Height, true), First.Width, First.Height);
    };
    auto StoreTransition = [&](const char* Name, const ReplayCaptureResult& Capture)
    {
        ReplayCaptureResult FirstReadback;
        FirstReadback.Pixels = Capture.FirstPixels;
        FirstReadback.Guides = Capture.FirstGuides;
        Store(Name, FirstReadback);
    };
    Store("TextureRoomInitial", Initial); Store("TextureRoomChanged", Changed);
    Store("TextureRoomBlocked", Blocked); Store("TextureRoomReopened", Reopened);
    StoreTransition("TextureRoomChangedFirst", Changed); StoreTransition("TextureRoomBlockedFirst", Blocked);
    StoreTransition("TextureRoomReopenedFirst", Reopened);
    std::printf("TEXTURE_CAPTURE frames_each=%u averaged_last=%u floor_initial=%.9g floor_changed=%.9g "
                "floor_blocked=%.9g floor_reopened=%.9g\n", Iterations, Iterations/2, Initial.FloorMean, Changed.FloorMean,
                Blocked.FloorMean, Reopened.FloorMean);
    std::printf("TEXTURE_CAPTURE_LINKS accepted=%u/%u/%u spatial=%u/%u/%u reset=%u/%u/%u cap=%u/%u/%u\n",
                Changed.Accepted, Blocked.Accepted, Reopened.Accepted, Changed.SpatialLinks, Blocked.SpatialLinks,
                Reopened.SpatialLinks, Changed.Reset ? 1u : 0u, Blocked.Reset ? 1u : 0u, Reopened.Reset ? 1u : 0u,
                Changed.HistoryCap, Blocked.HistoryCap, Reopened.HistoryCap);
    Require(Blocked.FloorMean < Changed.FloorMean && Blocked.FloorMean < Reopened.FloorMean,
            "Moving blocker did not reduce the captured lower-room luminance");
    std::printf("PASS four settled and three first-transition Vulkan readbacks; display exposure=.45 gamma=2.2\n");
}
} // namespace
