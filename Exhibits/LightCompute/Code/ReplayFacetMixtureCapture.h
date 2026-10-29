/*==========================================================================================================================================
                                             REPLAYFACETMIXTURECAPTURE.H
==========================================================================================================================================*/
// 🧩 Capture a rough-textured room through unequal-emitter, motion and blocker changes.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            CAPTURE SCENE AND READBACKS
//------------------------------------------------------------------------------------------------------------------------

void SetReplayCaptureEmitterPowers(LightComputeArchive& Scene, bool Swapped)
{
    uint32_t Sources = 0, Bright = 0;
    for (auto& Facet : Scene.Surfaces)
    {
        if (!(Facet.Emissive[0] > 0)) continue;
        const bool PositiveCentroid = Facet.A[0] + Facet.B[0] + Facet.C[0] > 0;
        const bool IsBright = PositiveCentroid != Swapped;
        const float Scale = IsBright ? 1.0f : .08f;
        for (uint32_t Channel = 0; Channel < 3; ++Channel) Facet.Emissive[Channel] *= Scale;
        ++Sources;
        Bright += IsBright ? 1u : 0u;
    }
    Require(Sources == 2 && Bright == 1, "Mixture capture expected two unequal ceiling emitters");
    const auto Cdf = CompileRadiantFacetDistribution(Scene.Surfaces);
    Require(Cdf[0] == 2 && std::max(Cdf[2], Cdf[6]) > .90f,
            "Mixture capture did not exercise an unequal emitter proposal");
}

void CaptureReplayFacetMixture(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Width = 256, Height = 160, Frames = 12;
    auto Before = ReplayTexturedMotionRigid(0, 0, 0, 7, Width, Height);
    auto Transition = ReplayTexturedMotionRigid(.35f, .28f, 1, 7, Width, Height);
    auto Moved = Transition;
    SettleReplayMotion(Moved);
    auto Blocked = ReplayTexturedMotionRigid(.35f, .28f, 1, 0, Width, Height);
    SettleReplayMotion(Blocked);
    auto Reopened = ReplayTexturedMotionRigid(.35f, .28f, 0, 7, Width, Height);
    SettleReplayMotion(Reopened);
    SetRoughTextureReplayMaterials(Before, .65f, .35f);
    SetRoughTextureReplayMaterials(Transition, .48f, .7f);
    SetRoughTextureReplayMaterials(Moved, .48f, .7f);
    SetRoughTextureReplayMaterials(Blocked, .48f, .7f);
    SetRoughTextureReplayMaterials(Reopened, .65f, .35f);
    SetReplayCaptureEmitterPowers(Before, false);
    SetReplayCaptureEmitterPowers(Transition, true);
    SetReplayCaptureEmitterPowers(Moved, true);
    SetReplayCaptureEmitterPowers(Blocked, true);
    SetReplayCaptureEmitterPowers(Reopened, false);
    LightComputeSubmission Compute(Driver);
    std::printf("MIXTURE_CAPTURE preparing Vulkan on %s (%ux%u facets=%zu)\n", Compute.Properties.deviceName,
                Width, Height, Before.Surfaces.size());
    std::fflush(stdout);
    Compute.Prepare(Before, Shaders, false, false, false, false, true);
    const auto Initial = CaptureReplaySequence(Compute, Before, 4, 911000, true, Frames,
                                                true, nullptr, true, true, true, true, true);
    std::printf("MIXTURE_CAPTURE before complete\n"); std::fflush(stdout);
    const auto Translated = CaptureReplaySequence(Compute, Transition, 4, 912000, false, Frames,
                                                   true, &Moved, true, true, true, true, true);
    std::printf("MIXTURE_CAPTURE camera, object, texture and source change complete\n"); std::fflush(stdout);
    const auto Occluded = CaptureReplaySequence(Compute, Blocked, 4, 913000, false, Frames,
                                                 true, nullptr, true, true, true, true, true);
    std::printf("MIXTURE_CAPTURE blocker complete\n"); std::fflush(stdout);
    const auto Opened = CaptureReplaySequence(Compute, Reopened, 4, 914000, false, Frames,
                                               true, nullptr, true, true, true, true, true);
    std::printf("MIXTURE_CAPTURE reopening complete\n"); std::fflush(stdout);
    for (const auto* Sequence : {&Translated, &Occluded, &Opened})
        Require(!Sequence->Reset && Sequence->Accepted > 0 && Sequence->SpatialLinks > 0 &&
                    Sequence->HistoryCap == 1 && Sequence->FirstPixels.size() == Before.Receivers.size() &&
                    Sequence->FirstGuides.size() == Before.Receivers.size(),
                "Mixture capture did not retain capped history and the first transition");
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
    Store("ReplayMixtureBefore", Initial);
    Store("ReplayMixtureMoved", Translated);
    Store("ReplayMixtureBlocked", Occluded);
    Store("ReplayMixtureReopened", Opened);
    StoreFirst("ReplayMixtureMovedFirst", Translated);
    StoreFirst("ReplayMixtureBlockedFirst", Occluded);
    StoreFirst("ReplayMixtureReopenedFirst", Opened);
    std::printf("MIXTURE_CAPTURE frames_each=%u averaged_last=%u camera_shift=.35 object_angle=.28 "
                "dim_source_scale=.08 floor_before=%.9g floor_moved=%.9g floor_blocked=%.9g "
                "floor_reopened=%.9g\n", Frames, Frames / 2, Initial.FloorMean, Translated.FloorMean,
                Occluded.FloorMean, Opened.FloorMean);
    std::printf("MIXTURE_CAPTURE_LINKS accepted=%u/%u/%u spatial=%u/%u/%u reset=%u/%u/%u cap=%u/%u/%u\n",
                Translated.Accepted, Occluded.Accepted, Opened.Accepted, Translated.SpatialLinks,
                Occluded.SpatialLinks, Opened.SpatialLinks, Translated.Reset ? 1u : 0u, Occluded.Reset ? 1u : 0u,
                Opened.Reset ? 1u : 0u, Translated.HistoryCap, Occluded.HistoryCap, Opened.HistoryCap);
    Require(Translated.FloorMean > Occluded.FloorMean && Opened.FloorMean > Occluded.FloorMean,
            "The mixture capture's blocker did not reduce lower-room illumination");
    std::printf("PASS four settled and three first-transition mixture Vulkan readbacks\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                             DEFAULT CAPTURE PARITY
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayCaptureDefaultParity(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Width = 96, Height = 128, FirstSeed = 919000;
    auto Scene = ReplayTexturedMotionRigid(0, 0, 0, 7, Width, Height);
    SetRoughTextureReplayMaterials(Scene, .65f, .35f);
    LightComputeSubmission CaptureCompute(Driver);
    CaptureCompute.Prepare(Scene, Shaders, false, false, false, false, true);
    const auto Captured = CaptureReplaySequence(CaptureCompute, Scene, 4, FirstSeed, true, 2,
                                                 true, nullptr, true, true, true, true);
    const auto CaptureLastCounts = CaptureCompute.ExtendedStatistics();
    const bool CaptureLastReset = CaptureCompute.ReplayLastReset;
    const uint32_t CaptureLastCap = CaptureCompute.ReplayLastHistoryCap;
    LightComputeSubmission LegacyCompute(Driver);
    LegacyCompute.Prepare(Scene, Shaders, false, false, false, false, true);
    const auto First = LegacyCompute.ExecuteReplay(Scene, 4, FirstSeed, true, true, true,
                                                   {.025f, .025f, .025f}, .01f, true, false, true,
                                                   true, false, false, 64, true, true);
    const auto Counts = LegacyCompute.ExtendedStatistics();
    const uint32_t Cap = LegacyCompute.ReplayLastHistoryCap;
    const bool Reset = LegacyCompute.ReplayLastReset;
    const auto Last = LegacyCompute.ExecuteReplay(Scene, 4, FirstSeed + 7919u, true, true, false,
                                                  {.025f, .025f, .025f}, .01f, true, false, true,
                                                  true, false, false, 64, true, true);
    const auto LastCounts = LegacyCompute.ExtendedStatistics();
    Require(First.size() == Captured.FirstPixels.size() && Last.size() == Captured.Pixels.size(),
            "Default capture image extent changed");
    uint64_t Different = 0, Components = 0;
    for (size_t Pixel = 0; Pixel < First.size(); ++Pixel)
        for (uint32_t Channel = 0; Channel < 4; ++Channel)
        {
            Different += std::memcmp(&First[Pixel][Channel], &Captured.FirstPixels[Pixel][Channel], sizeof(float)) != 0;
            Different += std::memcmp(&Last[Pixel][Channel], &Captured.Pixels[Pixel][Channel], sizeof(float)) != 0;
            Components += 2;
        }
    Require(Different == 0 && Captured.Accepted == Counts[20] && Captured.SpatialLinks == Counts[22] &&
                Captured.HistoryCap == Cap && Captured.Reset == Reset && std::isfinite(Captured.FloorMean) &&
                CaptureLastCounts[20] == LastCounts[20] && CaptureLastCounts[22] == LastCounts[22] &&
                LastCounts[20] > 0 && LastCounts[22] > 0 && !CaptureLastReset && !LegacyCompute.ReplayLastReset &&
                CaptureLastCap == LegacyCompute.ReplayLastHistoryCap && CaptureLastCap == 64,
            "Default capture helper changed the legacy two-submission path");
    std::printf("MIXTURE_CAPTURE_DEFAULT_PARITY images=2 components=%llu differing_float_bits=%llu "
                "first_temporal=%u first_spatial=%u first_reset=%u first_cap=%u last_temporal=%u last_spatial=%u "
                "last_reset=%u last_cap=%u\n", (unsigned long long)Components, (unsigned long long)Different,
                Counts[20], Counts[22], Reset ? 1u : 0u, Cap, LastCounts[20], LastCounts[22],
                CaptureLastReset ? 1u : 0u, CaptureLastCap);
    std::printf("PASS default capture helper against the previous submission arguments\n");
}
} // namespace
