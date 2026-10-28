/*==========================================================================================================================================
                                                  REPLAYFACETCAPTURE.H
==========================================================================================================================================*/
// 🧩 Capture dynamic multibounce rooms with explicit emissive-facet sampling.
#pragma once

namespace
{
void CaptureReplayFacetRooms(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    const auto Open = ReplayRoom(7), Blocked = ReplayRoom(0);
    LightComputeSubmission Compute(Driver);
    std::printf("FACET_CAPTURE preparing Vulkan on %s (%ux%u facets=%zu)\n", Compute.Properties.deviceName,
                Open.Width, Open.Height, Open.Surfaces.size());
    std::fflush(stdout);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    constexpr uint32_t Frames = 24;
    const auto One = CaptureReplaySequence(Compute, Open, 1, 31000, true, Frames, true);
    std::printf("FACET_CAPTURE one-hit open sequence complete\n"); std::fflush(stdout);
    const auto Four = CaptureReplaySequence(Compute, Open, 4, 32000, true, Frames, true);
    std::printf("FACET_CAPTURE four-hit open sequence complete\n"); std::fflush(stdout);
    const auto Occluded = CaptureReplaySequence(Compute, Blocked, 4, 33000, false, Frames, true);
    std::printf("FACET_CAPTURE blocked sequence complete\n"); std::fflush(stdout);
    const auto Reopened = CaptureReplaySequence(Compute, Open, 4, 34000, false, Frames, true);
    std::printf("FACET_CAPTURE reopened sequence complete\n"); std::fflush(stdout);
    Require(!Occluded.Reset && !Reopened.Reset && Occluded.Accepted > 0 && Reopened.Accepted > 0 &&
                Occluded.SpatialLinks > 0 && Reopened.SpatialLinks > 0 && Occluded.HistoryCap == 1 &&
                Reopened.HistoryCap == 1, "Facet room capture did not retain and cap histories");
    std::filesystem::create_directories(Directory);
    auto Store = [&](const char* Name, const ReplayCaptureResult& Capture)
    {
        WriteImage(Directory + "/" + Name + ".ppm", Capture.Pixels, Open.Width, Open.Height);
        WriteImage(Directory + "/" + Name + "Preview.ppm", FilterReplayPreview(Capture, Open.Width, Open.Height),
                   Open.Width, Open.Height);
    };
    Store("FacetRoomOne", One); Store("FacetRoomOpen", Four);
    Store("FacetRoomBlocked", Occluded); Store("FacetRoomReopened", Reopened);
    std::printf("FACET_CAPTURE frames_each=%u averaged_last=%u one_floor=%.9g open_floor=%.9g "
                "blocked_floor=%.9g reopened_floor=%.9g\n", Frames, Frames/2, One.FloorMean, Four.FloorMean,
                Occluded.FloorMean, Reopened.FloorMean);
    std::printf("FACET_CAPTURE_MOTION blocked_accepted=%u reopened_accepted=%u spatial_links=%u/%u "
                "reset=%u/%u cap=%u/%u\n", Occluded.Accepted, Reopened.Accepted, Occluded.SpatialLinks,
                Reopened.SpatialLinks, Occluded.Reset ? 1u : 0u, Reopened.Reset ? 1u : 0u,
                Occluded.HistoryCap, Reopened.HistoryCap);
    Require(Four.FloorMean > One.FloorMean && Four.FloorMean > Occluded.FloorMean &&
                Reopened.FloorMean > Occluded.FloorMean, "Facet-sampled floor did not respond to depth or blocker");
    std::printf("PASS four facet-sampled Vulkan readbacks; display exposure=.45 gamma=2.2\n");
}
} // namespace
