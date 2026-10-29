/*==========================================================================================================================================
                                       REPLAYFACETMIXTUREMOTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare moving rough-textured mixture replay with independent primary rays and multibounce paths.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            MOVING RADIANT FACETS
//------------------------------------------------------------------------------------------------------------------------

void TranslateReplayRadiantFacets(LightComputeArchive& Scene, float Translation)
{
    uint32_t RadiantCount = 0;
    double OriginalSum = 0;
    for (auto& Facet : Scene.Surfaces)
    {
        if (!(Facet.Emissive[0] > 0)) continue;
        ++RadiantCount;
        for (auto* Position : {&Facet.A, &Facet.B, &Facet.C})
        {
            OriginalSum += (*Position)[0];
            (*Position)[0] += Translation;
        }
    }
    Require(RadiantCount == 2, "Radiant translation fixture requires two facets");
    const auto Unordered = Scene.Surfaces;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    Geometry.reserve(Unordered.size());
    for (const auto& Facet : Unordered)
        Geometry.push_back({SpatialVector{Facet.A[0], Facet.A[1], Facet.A[2]},
                            SpatialVector{Facet.B[0], Facet.B[1], Facet.B[2]},
                            SpatialVector{Facet.C[0], Facet.C[1], Facet.C[2]}});
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    Require(Scene.Visibility.Corners.size() == Unordered.size(), "Radiant translation changed the facet count");
    double TranslatedSum = 0;
    for (size_t Index = 0; Index < Unordered.size(); ++Index)
    {
        auto Facet = Unordered[Scene.Visibility.OriginalFacets[Index]];
        const auto& Triangle = Scene.Visibility.Corners[Index];
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            Facet.A[Axis] = Triangle.A[Axis];
            Facet.B[Axis] = Triangle.B[Axis];
            Facet.C[Axis] = Triangle.C[Axis];
        }
        if (Facet.Emissive[0] > 0) TranslatedSum += Facet.A[0] + Facet.B[0] + Facet.C[0];
        Scene.Surfaces[Index] = Facet;
    }
    const double Observed = (TranslatedSum - OriginalSum) / (RadiantCount * 3);
    std::printf("MIXTURE_MOTION_SOURCE translation=%.9g observed=%.9g radiant_facets=%u\n",
                Translation, Observed, RadiantCount);
    Require(std::abs(Observed - Translation) < 1e-6, "Radiant geometry translation was not preserved");
}

//------------------------------------------------------------------------------------------------------------------------
//                                      MOVING PRIMARY, LIGHT AND OCCLUDER
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayFacetMixtureMotion(const char* Driver, const std::string& Shaders,
                                      uint32_t CpuPaths = 240000, uint32_t Batches = 96)
{
    Require(CpuPaths >= 10000 && Batches >= 8, "Motion validation needs at least eight batches and 10000 CPU paths");
    constexpr uint32_t Width = 64, Height = 64;
    auto Previous = ReplayTexturedMotionRigid(0, 0, 0, 7, Width, Height);
    auto Current = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    auto Covered = ReplayTexturedMotionRigid(.07f, .12f, 1, 0, Width, Height);
    auto Reopened = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    SettleReplayMotion(Covered);
    SettleReplayMotion(Reopened);
    SetRoughTextureReplayMaterials(Previous, .65f, .35f);
    for (auto* Scene : {&Current, &Covered, &Reopened}) SetRoughTextureReplayMaterials(*Scene, .48f, .7f);
    TranslateReplayRadiantFacets(Previous, -.12f);
    TranslateReplayRadiantFacets(Current, .18f);
    TranslateReplayRadiantFacets(Covered, .18f);
    TranslateReplayRadiantFacets(Reopened, -.12f);
    SetReplayCaptureEmitterPowers(Previous, false);
    SetReplayCaptureEmitterPowers(Current, true);
    SetReplayCaptureEmitterPowers(Covered, true);
    SetReplayCaptureEmitterPowers(Reopened, false);
    ValidateReplayRigidTextureCoordinates(Previous, Current, .12f);
    LightComputeSubmission Compute(Driver);
    std::printf("MIXTURE_MOTION preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    (void)TraceRoughTextureReplay(Compute, Previous, 1, 940001, false, false, true, true);
    const auto PreviousGuides = Compute.PrimaryReceivers();
    const auto PreviousReference = ValidateReplayPrimaryReference(Previous, PreviousGuides, "mixture_previous");
    ValidateRoughMotionCoefficients(Previous, PreviousReference, PreviousGuides, "mixture_previous");
    (void)TraceRoughTextureReplay(Compute, Current, 1, 940002, false, false, true, true);
    const auto CurrentGuides = Compute.PrimaryReceivers();
    const auto CurrentReference = ValidateReplayPrimaryReference(Current, CurrentGuides, "mixture_moved");
    ValidateRoughMotionCoefficients(Current, CurrentReference, CurrentGuides, "mixture_moved");
    (void)TraceRoughTextureReplay(Compute, Covered, 1, 940003, false, false, true, true);
    const auto CoveredGuides = Compute.PrimaryReceivers();
    const auto CoveredReference = ValidateReplayPrimaryReference(Covered, CoveredGuides, "mixture_blocked");
    ValidateRoughMotionCoefficients(Covered, CoveredReference, CoveredGuides, "mixture_blocked");
    (void)TraceRoughTextureReplay(Compute, Reopened, 1, 940004, false, false, true, true);
    const auto ReopenedGuides = Compute.PrimaryReceivers();
    const auto ReopenedReference = ValidateReplayPrimaryReference(Reopened, ReopenedGuides, "mixture_reopened");
    ValidateRoughMotionCoefficients(Reopened, ReopenedReference, ReopenedGuides, "mixture_reopened");
    const std::array<uint32_t, 2> Pixels{
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 8, 28, 38),
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 1, 46, 50)};
    std::array<ReplayRgbMoments, 2> CpuCurrent, CpuCovered, CpuReopened, UniformFresh, MixtureFresh,
                                     Moved, Blocked, Reweighted;
    std::printf("MIXTURE_MOTION tracing %u independent CPU paths for each of six primary probes\n", CpuPaths);
    std::fflush(stdout);
    for (uint32_t Probe = 0; Probe < Pixels.size(); ++Probe)
    {
        CpuCurrent[Probe] = TraceRoughMotionReference(Current, CurrentReference[Pixels[Probe]], 941001 + Probe, CpuPaths);
        CpuCovered[Probe] = TraceRoughMotionReference(Covered, CoveredReference[Pixels[Probe]], 942001 + Probe, CpuPaths);
        CpuReopened[Probe] = TraceRoughMotionReference(Reopened, ReopenedReference[Pixels[Probe]], 943001 + Probe, CpuPaths);
    }
    uint64_t MovedLinks = 0, BlockedLinks = 0, ReopenedLinks = 0, SpatialLinks = 0;
    uint32_t MovedProbeAccepts = 0, BlockedProbeAccepts = 0, ReopenedProbeAccepts = 0;
    auto RepresentedCount = [&](uint32_t Pixel)
    {
        const size_t Start = size_t(Width) * Height * 27 + 13889 + SolarSourceCount;
        std::array<float, 4> Packed{};
        std::memcpy(Packed.data(), static_cast<const char*>(Compute.Buffers[6].Address) + (Start + Pixel) * 16, 16);
        return Packed[3];
    };
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 944001 + 103 * Batch;
        const auto UniformImage = TraceRoughTextureReplay(Compute, Current, 4, Seed, false, true, true,
                                                           true, true, true, true);
        const auto MixtureImage = TraceRoughTextureReplay(Compute, Current, 4, Seed, false, true, true,
                                                           true, true, true, true, true, true);
        for (uint32_t Probe = 0; Probe < Pixels.size(); ++Probe)
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                UniformFresh[Probe][Channel].Accumulate(UniformImage[Pixels[Probe]][Channel]);
                MixtureFresh[Probe][Channel].Accumulate(MixtureImage[Pixels[Probe]][Channel]);
            }
        for (uint32_t Iteration = 0; Iteration < 3; ++Iteration)
            (void)TraceRoughTextureReplay(Compute, Previous, 4, Seed + 10 + Iteration, true, true, Iteration == 0,
                                          true, true, true, false);
        const auto MovedImage = TraceRoughTextureReplay(Compute, Current, 4, Seed + 20, true, true, false,
                                                         true, true, true, true, true, true);
        auto Counts = Compute.ExtendedStatistics(); MovedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving camera, rigid surface and radiant proposal lost capped history");
        for (uint32_t Probe = 0; Probe < Pixels.size(); ++Probe)
        {
            MovedProbeAccepts += RepresentedCount(Pixels[Probe]) == 2 ? 1u : 0u;
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
                Moved[Probe][Channel].Accumulate(MovedImage[Pixels[Probe]][Channel]);
        }
        const auto BlockedImage = TraceRoughTextureReplay(Compute, Covered, 4, Seed + 21, true, true, false,
                                                           true, true, true, true, true, true);
        Counts = Compute.ExtendedStatistics(); BlockedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Blocker insertion lost capped mixture history");
        for (uint32_t Probe = 0; Probe < Pixels.size(); ++Probe)
        {
            BlockedProbeAccepts += RepresentedCount(Pixels[Probe]) == 2 ? 1u : 0u;
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
                Blocked[Probe][Channel].Accumulate(BlockedImage[Pixels[Probe]][Channel]);
        }
        const auto ReopenedImage = TraceRoughTextureReplay(Compute, Reopened, 4, Seed + 22, true, true, false,
                                                            true, true, true, true, true, true);
        Counts = Compute.ExtendedStatistics(); ReopenedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Reopened and relocated radiant facets lost capped mixture history");
        for (uint32_t Probe = 0; Probe < Pixels.size(); ++Probe)
        {
            ReopenedProbeAccepts += RepresentedCount(Pixels[Probe]) == 2 ? 1u : 0u;
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
                Reweighted[Probe][Channel].Accumulate(ReopenedImage[Pixels[Probe]][Channel]);
        }
        if ((Batch + 1) % 32 == 0 || Batch + 1 == Batches)
        {
            const auto& Floor = MixtureFresh[1][2];
            std::printf("MIXTURE_MOTION_PROGRESS batches=%u/%u floor_blue_mean=%.9g floor_blue_se=%.9g\n",
                        Batch + 1, Batches, Floor.Mean(), ReplaySampleStandardError(Floor));
            std::fflush(stdout);
        }
    }
    bool OcclusionDetected = false, RelightingDetected = false;
    for (uint32_t Probe = 0; Probe < Pixels.size(); ++Probe)
    {
        const std::string Name = Probe == 0 ? "mixture_object" : "mixture_floor";
        CompareReplayMotionProbe((Name + "_uniform_fresh").c_str(), CpuCurrent[Probe], UniformFresh[Probe]);
        CompareReplayMotionProbe((Name + "_mixture_fresh").c_str(), CpuCurrent[Probe], MixtureFresh[Probe]);
        CompareReplayMotionProbe((Name + "_moved_reused").c_str(), CpuCurrent[Probe], Moved[Probe]);
        CompareReplayMotionProbe((Name + "_blocked_reused").c_str(), CpuCovered[Probe], Blocked[Probe]);
        CompareReplayMotionProbe((Name + "_reopened_reused").c_str(), CpuReopened[Probe], Reweighted[Probe]);
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            OcclusionDetected |= CpuCurrent[Probe][Channel].Mean() - CpuCovered[Probe][Channel].Mean() >
                6 * std::hypot(ReplaySampleStandardError(CpuCurrent[Probe][Channel]),
                               ReplaySampleStandardError(CpuCovered[Probe][Channel]));
            RelightingDetected |= std::abs(CpuCurrent[Probe][Channel].Mean() - CpuReopened[Probe][Channel].Mean()) >
                6 * std::hypot(ReplaySampleStandardError(CpuCurrent[Probe][Channel]),
                               ReplaySampleStandardError(CpuReopened[Probe][Channel]));
            const double UniformDeviation = UniformFresh[Probe][Channel].Deviation();
            Require(UniformDeviation > 0, "Uniform motion control did not vary");
            std::printf("MIXTURE_MOTION_VARIANCE probe=%s channel=%u uniform_sd=%.9g mixture_sd=%.9g "
                        "ratio=%.9g batches=%u\n", Name.c_str(), Channel, UniformDeviation,
                        MixtureFresh[Probe][Channel].Deviation(), MixtureFresh[Probe][Channel].Deviation() /
                        UniformDeviation, Batches);
        }
    }
    std::printf("MIXTURE_MOTION_LINKS moved=%llu blocked=%llu reopened=%llu spatial=%llu "
                "probe_accepts=%u/%u/%u occlusion=%u relighting=%u batches=%u\n",
                (unsigned long long)MovedLinks, (unsigned long long)BlockedLinks, (unsigned long long)ReopenedLinks,
                (unsigned long long)SpatialLinks, MovedProbeAccepts, BlockedProbeAccepts, ReopenedProbeAccepts,
                OcclusionDetected ? 1u : 0u, RelightingDetected ? 1u : 0u, Batches);
    Require(OcclusionDetected && RelightingDetected && MovedProbeAccepts == 2 * Batches &&
                BlockedProbeAccepts == 2 * Batches && ReopenedProbeAccepts == 2 * Batches &&
                MovedLinks > Batches * 500 && BlockedLinks > Batches * 500 && ReopenedLinks > Batches * 500 &&
                SpatialLinks > 0, "Motion probes did not exercise all radiant and occlusion transitions");
    std::printf("PASS moving rough-textured mixture, radiant translation, source-power swap and blocker transitions\n");
}
} // namespace
