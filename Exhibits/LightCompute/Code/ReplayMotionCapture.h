/*==========================================================================================================================================
                                                  REPLAYMOTIONCAPTURE.H
==========================================================================================================================================*/
// 🧩 Capture primary-camera, rigid-object and blocker transitions using path replay.
#pragma once

namespace
{
void SettleReplayMotion(LightComputeArchive& Scene)
{
    for (uint32_t I = 0; I < 4; ++I) Scene.Camera[4+I] = Scene.Camera[I];
    Scene.Camera[9] = Scene.Camera[8];
    Scene.Camera[11][2] = Scene.Camera[11][0];
    Scene.Camera[11][3] = Scene.Camera[11][1];
}
void CaptureReplayMotion(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Width = 256, Height = 160, Frames = 24;
    const auto Before = ReplayMotionRigid(0, 0, Width, Height);
    const auto Transition = ReplayMotionRigid(.35f, .28f, Width, Height);
    auto Moved = Transition;
    SettleReplayMotion(Moved);
    auto Blocked = ReplayMotionRigid(.35f, .28f, Width, Height, 0);
    SettleReplayMotion(Blocked);
    LightComputeSubmission Compute(Driver);
    std::printf("MOTION_CAPTURE preparing Vulkan on %s (%ux%u facets=%zu)\n", Compute.Properties.deviceName,
                Width, Height, Before.Surfaces.size());
    std::fflush(stdout);
    Compute.Prepare(Before, Shaders, false, false, false, false, true);
    const auto Initial = CaptureReplaySequence(Compute, Before, 4, 51000, true, Frames, true);
    std::printf("MOTION_CAPTURE initial sequence complete\n"); std::fflush(stdout);
    const auto Translated = CaptureReplaySequence(Compute, Transition, 4, 52000, false, Frames, true, &Moved);
    std::printf("MOTION_CAPTURE camera and rigid-object sequence complete\n"); std::fflush(stdout);
    const auto Occluded = CaptureReplaySequence(Compute, Blocked, 4, 53000, false, Frames, true);
    std::printf("MOTION_CAPTURE blocker sequence complete\n"); std::fflush(stdout);
    const auto Reopened = CaptureReplaySequence(Compute, Moved, 4, 54000, false, Frames, true);
    std::printf("MOTION_CAPTURE reopened sequence complete\n"); std::fflush(stdout);
    for (const auto* R : {&Translated, &Occluded, &Reopened})
        Require(!R->Reset && R->Accepted > 0 && R->SpatialLinks > 0 && R->HistoryCap == 1,
                "Motion capture did not retain capped temporal and spatial history");
    std::filesystem::create_directories(Directory);
    auto Store = [&](const char* Name, const ReplayCaptureResult& Capture)
    {
        WriteImage(Directory + "/" + Name + ".ppm", Capture.Pixels, Width, Height);
        WriteImage(Directory + "/" + Name + "Preview.ppm", FilterReplayPreview(Capture, Width, Height), Width, Height);
    };
    Store("MotionRoomBefore", Initial); Store("MotionRoomMoved", Translated);
    Store("MotionRoomBlocked", Occluded); Store("MotionRoomReopened", Reopened);
    std::printf("MOTION_CAPTURE frames_each=%u averaged_last=%u camera_shift=.35 object_angle=.28 "
                "floor_before=%.9g floor_moved=%.9g floor_blocked=%.9g floor_reopened=%.9g\n", Frames, Frames/2,
                Initial.FloorMean, Translated.FloorMean, Occluded.FloorMean, Reopened.FloorMean);
    std::printf("MOTION_CAPTURE_LINKS accepted=%u/%u/%u spatial=%u/%u/%u reset=%u/%u/%u cap=%u/%u/%u\n",
                Translated.Accepted, Occluded.Accepted, Reopened.Accepted, Translated.SpatialLinks,
                Occluded.SpatialLinks, Reopened.SpatialLinks, Translated.Reset ? 1u : 0u, Occluded.Reset ? 1u : 0u,
                Reopened.Reset ? 1u : 0u, Translated.HistoryCap, Occluded.HistoryCap, Reopened.HistoryCap);
    Require(Translated.FloorMean > Occluded.FloorMean && Reopened.FloorMean > Occluded.FloorMean,
            "Motion captures did not respond to blocker changes");
    std::printf("PASS four camera, rigid-object and blocker Vulkan readbacks; display exposure=.45 gamma=2.2\n");
}
} // namespace
