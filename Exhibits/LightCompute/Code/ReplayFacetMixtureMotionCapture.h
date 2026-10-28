/*==========================================================================================================================================
                                         REPLAYFACETMIXTUREMOTIONCAPTURE.H
==========================================================================================================================================*/
// 🧩 Capture the moving-radiant mixture fixture before, during and after occlusion.
#pragma once

namespace
{

void CaptureReplayFacetMixtureMotion(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Width = 128, Height = 160, Frames = 8;
    auto Previous = ReplayTexturedMotionRigid(0, 0, 0, 7, Width, Height);
    auto Transition = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    auto Moved = Transition;
    auto Blocked = ReplayTexturedMotionRigid(.07f, .12f, 1, 0, Width, Height);
    auto Reopened = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    SettleReplayMotion(Moved);
    SettleReplayMotion(Blocked);
    SettleReplayMotion(Reopened);
    SetRoughTextureReplayMaterials(Previous, .65f, .35f);
    for (auto* Scene : {&Transition, &Moved, &Blocked, &Reopened}) SetRoughTextureReplayMaterials(*Scene, .48f, .7f);
    TranslateReplayRadiantFacets(Previous, -.12f);
    for (auto* Scene : {&Transition, &Moved, &Blocked}) TranslateReplayRadiantFacets(*Scene, .18f);
    TranslateReplayRadiantFacets(Reopened, -.12f);
    SetReplayCaptureEmitterPowers(Previous, false);
    for (auto* Scene : {&Transition, &Moved, &Blocked}) SetReplayCaptureEmitterPowers(*Scene, true);
    SetReplayCaptureEmitterPowers(Reopened, false);
    ValidateReplayRigidTextureCoordinates(Previous, Transition, .12f);
    LightComputeSubmission Compute(Driver);
    std::printf("MIXTURE_MOTION_CAPTURE preparing Vulkan on %s (%ux%u, frames=%u)\n",
                Compute.Properties.deviceName, Width, Height, Frames);
    std::fflush(stdout);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    // 📝 Begin with uniform facets/cosine directions, then change to mixture facets/GGX directions.
    const auto Initial = CaptureReplaySequence(Compute, Previous, 4, 951000, true, Frames,
                                                true, nullptr, true, true, true, true, false, false);
    std::printf("MIXTURE_MOTION_CAPTURE initial complete\n"); std::fflush(stdout);
    const auto Translated = CaptureReplaySequence(Compute, Transition, 4, 952000, false, Frames,
                                                   true, &Moved, true, true, true, true, true, true);
    std::printf("MIXTURE_MOTION_CAPTURE moved and relit complete\n"); std::fflush(stdout);
    const auto Occluded = CaptureReplaySequence(Compute, Blocked, 4, 953000, false, Frames,
                                                 true, nullptr, true, true, true, true, true, true);
    std::printf("MIXTURE_MOTION_CAPTURE blocked complete\n"); std::fflush(stdout);
    const auto Opened = CaptureReplaySequence(Compute, Reopened, 4, 954000, false, Frames,
                                               true, nullptr, true, true, true, true, true, true);
    std::printf("MIXTURE_MOTION_CAPTURE reopened complete\n"); std::fflush(stdout);
    for (const auto* Sequence : {&Translated, &Occluded, &Opened})
        Require(!Sequence->Reset && Sequence->Accepted > 0 && Sequence->SpatialLinks > 0 &&
                    Sequence->HistoryCap == 1 && Sequence->FirstPixels.size() == Previous.Receivers.size() &&
                    Sequence->FirstGuides.size() == Previous.Receivers.size(),
                "Moving-radiant capture did not retain capped history and first-transition pixels");
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
    Store("ReplayRadiantMotionBefore", Initial);
    Store("ReplayRadiantMotionMoved", Translated);
    Store("ReplayRadiantMotionBlocked", Occluded);
    Store("ReplayRadiantMotionReopened", Opened);
    StoreFirst("ReplayRadiantMotionMovedFirst", Translated);
    StoreFirst("ReplayRadiantMotionBlockedFirst", Occluded);
    StoreFirst("ReplayRadiantMotionReopenedFirst", Opened);
    std::printf("MIXTURE_MOTION_CAPTURE frames_each=%u averaged_last=%u lower_before=%.9g lower_moved=%.9g "
                "lower_blocked=%.9g lower_reopened=%.9g\n", Frames, Frames / 2, Initial.FloorMean,
                Translated.FloorMean, Occluded.FloorMean, Opened.FloorMean);
    std::printf("MIXTURE_MOTION_CAPTURE_LINKS accepted=%u/%u/%u spatial=%u/%u/%u reset=%u/%u/%u cap=%u/%u/%u\n",
                Translated.Accepted, Occluded.Accepted, Opened.Accepted, Translated.SpatialLinks,
                Occluded.SpatialLinks, Opened.SpatialLinks, Translated.Reset ? 1u : 0u, Occluded.Reset ? 1u : 0u,
                Opened.Reset ? 1u : 0u, Translated.HistoryCap, Occluded.HistoryCap, Opened.HistoryCap);
    Require(Translated.FloorMean > Occluded.FloorMean && Opened.FloorMean > Occluded.FloorMean,
            "The moving-radiant blocker did not reduce the lower-image luminance");
    std::printf("PASS moving-radiant mixture settled and first-transition Vulkan readbacks\n");
}
} // namespace
