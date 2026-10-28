/*==========================================================================================================================================
                                               REPLAYTEXTUREMOTIONCAPTURE.H
==========================================================================================================================================*/
// 🧩 Capture actual textured camera, rigid-object and blocker transitions from Vulkan replay.
#pragma once

namespace
{
void CaptureReplayTextureMotion(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Width = 256, Height = 160, Frames = 16;
    auto Before = ReplayTexturedMotionRigid(0, 0, 0, 7, Width, Height);
    const auto Transition = ReplayTexturedMotionRigid(.35f, .28f, 1, 7, Width, Height);
    auto Moved = Transition;
    SettleReplayMotion(Moved);
    auto Blocked = ReplayTexturedMotionRigid(.35f, .28f, 1, 0, Width, Height);
    SettleReplayMotion(Blocked);
    auto Reopened = ReplayTexturedMotionRigid(.35f, .28f, 0, 7, Width, Height);
    SettleReplayMotion(Reopened);
    LightComputeSubmission Compute(Driver);
    std::printf("TEXTURE_MOTION_CAPTURE preparing Vulkan on %s (%ux%u facets=%zu)\n", Compute.Properties.deviceName,
                Width, Height, Before.Surfaces.size());
    std::fflush(stdout);
    Compute.Prepare(Before, Shaders, false, false, false, false, true);
    const auto Initial = CaptureReplaySequence(Compute, Before, 4, 181000, true, Frames,
                                                true, nullptr, true, true, true);
    std::printf("TEXTURE_MOTION_CAPTURE initial sequence complete\n"); std::fflush(stdout);
    const auto Translated = CaptureReplaySequence(Compute, Transition, 4, 182000, false, Frames,
                                                   true, &Moved, true, true, true);
    std::printf("TEXTURE_MOTION_CAPTURE camera, object and atlas transition complete\n"); std::fflush(stdout);
    const auto Occluded = CaptureReplaySequence(Compute, Blocked, 4, 183000, false, Frames,
                                                 true, nullptr, true, true, true);
    std::printf("TEXTURE_MOTION_CAPTURE blocker transition complete\n"); std::fflush(stdout);
    const auto Opened = CaptureReplaySequence(Compute, Reopened, 4, 184000, false, Frames,
                                               true, nullptr, true, true, true);
    std::printf("TEXTURE_MOTION_CAPTURE reopening and atlas transition complete\n"); std::fflush(stdout);
    for (const auto* Sequence : {&Translated, &Occluded, &Opened})
        Require(!Sequence->Reset && Sequence->Accepted > 0 && Sequence->SpatialLinks > 0 &&
                    Sequence->HistoryCap == 1 && Sequence->FirstPixels.size() == Before.Receivers.size() &&
                    Sequence->FirstGuides.size() == Before.Receivers.size(),
                "Textured motion capture did not retain capped history and the first transition");
    std::filesystem::create_directories(Directory);
    auto Store = [&](const char* Name, const ReplayCaptureResult& Capture)
    {
        WriteImage(Directory + "/" + Name + ".ppm", Capture.Pixels, Width, Height);
        WriteImage(Directory + "/" + Name + "Preview.ppm",
                   FilterReplayPreview(Capture, Width, Height, true), Width, Height);
    };
    auto StoreFirst = [&](const char* Name, const ReplayCaptureResult& Capture)
    {
        ReplayCaptureResult First;
        First.Pixels = Capture.FirstPixels;
        First.Guides = Capture.FirstGuides;
        Store(Name, First);
    };
    Store("TexturedMotionBefore", Initial);
    Store("TexturedMotionMoved", Translated);
    Store("TexturedMotionBlocked", Occluded);
    Store("TexturedMotionReopened", Opened);
    StoreFirst("TexturedMotionMovedFirst", Translated);
    StoreFirst("TexturedMotionBlockedFirst", Occluded);
    StoreFirst("TexturedMotionReopenedFirst", Opened);
    std::printf("TEXTURE_MOTION_CAPTURE frames_each=%u averaged_last=%u camera_shift=.35 object_angle=.28 "
                "floor_before=%.9g floor_moved=%.9g floor_blocked=%.9g floor_reopened=%.9g\n",
                Frames, Frames / 2, Initial.FloorMean, Translated.FloorMean, Occluded.FloorMean, Opened.FloorMean);
    std::printf("TEXTURE_MOTION_CAPTURE_LINKS accepted=%u/%u/%u spatial=%u/%u/%u reset=%u/%u/%u cap=%u/%u/%u\n",
                Translated.Accepted, Occluded.Accepted, Opened.Accepted, Translated.SpatialLinks, Occluded.SpatialLinks,
                Opened.SpatialLinks, Translated.Reset ? 1u : 0u, Occluded.Reset ? 1u : 0u, Opened.Reset ? 1u : 0u,
                Translated.HistoryCap, Occluded.HistoryCap, Opened.HistoryCap);
    Require(Translated.FloorMean > Occluded.FloorMean && Opened.FloorMean > Occluded.FloorMean,
            "The moving blocker did not reduce lower-room illumination");
    std::printf("PASS four settled and three first-transition textured-motion Vulkan readbacks\n");
}
} // namespace
