/*==========================================================================================================================================
                                         REPLAYIMPORTANCEMOTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare moving rough-textured primary probes using the optional GGX mixture and independent CPU paths.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                        MOVING TEXTURED PRIMARY AND BLOCKER
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayImportanceMotion(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Width = 64, Height = 64, CpuPaths = 160000, Batches = 64;
    auto Previous = ReplayTexturedMotionRigid(0, 0, 0, 7, Width, Height);
    auto Current = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    auto Covered = ReplayTexturedMotionRigid(.07f, .12f, 1, 0, Width, Height);
    SettleReplayMotion(Covered);
    SetRoughTextureReplayMaterials(Previous, .65f, .35f);
    SetRoughTextureReplayMaterials(Current, .48f, .7f);
    SetRoughTextureReplayMaterials(Covered, .48f, .7f);
    ValidateReplayRigidTextureCoordinates(Previous, Current, .12f);
    LightComputeSubmission Compute(Driver);
    std::printf("IMPORTANCE_MOTION preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    (void)TraceRoughTextureReplay(Compute, Previous, 1, 724001, false, false, true, true);
    const auto PreviousGuides = Compute.PrimaryReceivers();
    const auto PreviousReference = ValidateReplayPrimaryReference(Previous, PreviousGuides, "importance_previous");
    ValidateRoughMotionCoefficients(Previous, PreviousReference, PreviousGuides, "importance_previous");
    (void)TraceRoughTextureReplay(Compute, Current, 1, 724002, false, false, true, true);
    const auto CurrentGuides = Compute.PrimaryReceivers();
    const auto CurrentReference = ValidateReplayPrimaryReference(Current, CurrentGuides, "importance_moved");
    ValidateRoughMotionCoefficients(Current, CurrentReference, CurrentGuides, "importance_moved");
    (void)TraceRoughTextureReplay(Compute, Covered, 1, 724003, false, false, true, true);
    const auto CoveredGuides = Compute.PrimaryReceivers();
    const auto CoveredReference = ValidateReplayPrimaryReference(Covered, CoveredGuides, "importance_blocked");
    ValidateRoughMotionCoefficients(Covered, CoveredReference, CoveredGuides, "importance_blocked");
    const std::array<uint32_t,2> Pixels{
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 8, 28, 38),
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 1, 46, 50)};
    std::array<ReplayRgbMoments,2> CpuOpen, CpuBlocked, CosineFresh, MixtureFresh, Moved, Blocked;
    std::array<std::array<ReplayRgbMoments,2>,2> CosineCohorts, MixtureCohorts;
    std::printf("IMPORTANCE_MOTION tracing %u independent CPU paths for each of four primary probes\n", CpuPaths);
    std::fflush(stdout);
    for (uint32_t I = 0; I < Pixels.size(); ++I)
    {
        CpuOpen[I] = TraceRoughMotionReference(Current, CurrentReference[Pixels[I]], 725001 + I, CpuPaths);
        CpuBlocked[I] = TraceRoughMotionReference(Covered, CoveredReference[Pixels[I]], 726001 + I, CpuPaths);
    }
    uint64_t MovedLinks = 0, BlockedLinks = 0, SpatialLinks = 0;
    uint32_t MovedProbeAccepts = 0, BlockedProbeAccepts = 0;
    auto RepresentedCount = [&](uint32_t Pixel)
    {
        const size_t Start = size_t(Width) * Height * 27 + 13889 + SolarSourceCount;
        std::array<float,4> Packed{};
        std::memcpy(Packed.data(), static_cast<const char*>(Compute.Buffers[6].Address) + (Start + Pixel) * 16, 16);
        return Packed[3];
    };
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 727000 + 101 * Batch;
        const auto CosineImage = TraceRoughTextureReplay(Compute, Current, 4, Seed, false, true, true,
                                                          true, true, true, false);
        const auto MixtureImage = TraceRoughTextureReplay(Compute, Current, 4, Seed, false, true, true,
                                                           true, true, true, true);
        for (uint32_t I = 0; I < Pixels.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C)
            {
                CosineFresh[I][C].Accumulate(CosineImage[Pixels[I]][C]);
                MixtureFresh[I][C].Accumulate(MixtureImage[Pixels[I]][C]);
                CosineCohorts[Batch / (Batches / 2)][I][C].Accumulate(CosineImage[Pixels[I]][C]);
                MixtureCohorts[Batch / (Batches / 2)][I][C].Accumulate(MixtureImage[Pixels[I]][C]);
            }
        for (uint32_t Iteration = 0; Iteration < 3; ++Iteration)
            (void)TraceRoughTextureReplay(Compute, Previous, 4, Seed + 10 + Iteration, true, true, Iteration == 0,
                                          true, true, true, false);
        const auto MovedImage = TraceRoughTextureReplay(Compute, Current, 4, Seed + 20, true, true, false,
                                                         true, true, true, true);
        auto Counts = Compute.ExtendedStatistics(); MovedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving and changing the proposal lost capped history");
        for (uint32_t I = 0; I < Pixels.size(); ++I)
        {
            MovedProbeAccepts += RepresentedCount(Pixels[I]) == 2 ? 1u : 0u;
            for (uint32_t C = 0; C < 3; ++C) Moved[I][C].Accumulate(MovedImage[Pixels[I]][C]);
        }
        const auto BlockedImage = TraceRoughTextureReplay(Compute, Covered, 4, Seed + 21, true, true, false,
                                                           true, true, true, true);
        Counts = Compute.ExtendedStatistics(); BlockedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving the blocker lost capped mixed-proposal history");
        for (uint32_t I = 0; I < Pixels.size(); ++I)
        {
            BlockedProbeAccepts += RepresentedCount(Pixels[I]) == 2 ? 1u : 0u;
            for (uint32_t C = 0; C < 3; ++C) Blocked[I][C].Accumulate(BlockedImage[Pixels[I]][C]);
        }
        if ((Batch + 1) % 16 == 0)
        {
            std::printf("IMPORTANCE_MOTION completed=%u/%u batches\n", Batch + 1, Batches);
            std::fflush(stdout);
        }
    }
    bool DetectBlocked = false;
    for (uint32_t I = 0; I < Pixels.size(); ++I)
    {
        const std::string Name = I == 0 ? "importance_object" : "importance_floor";
        CompareReplayMotionProbe((Name + "_cosine_fresh").c_str(), CpuOpen[I], CosineFresh[I]);
        CompareReplayMotionProbe((Name + "_mixture_fresh").c_str(), CpuOpen[I], MixtureFresh[I]);
        CompareReplayMotionProbe((Name + "_moved_reused").c_str(), CpuOpen[I], Moved[I]);
        CompareReplayMotionProbe((Name + "_blocked_reused").c_str(), CpuBlocked[I], Blocked[I]);
        for (uint32_t C = 0; C < 3; ++C)
        {
            DetectBlocked |= CpuOpen[I][C].Mean() - CpuBlocked[I][C].Mean() >
                6 * std::hypot(ReplaySampleStandardError(CpuOpen[I][C]), ReplaySampleStandardError(CpuBlocked[I][C]));
            std::printf("IMPORTANCE_MOTION_VARIANCE probe=%s channel=%u cosine_sd=%.9g mixture_sd=%.9g "
                        "ratio=%.9g batches=%u\n", Name.c_str(), C, CosineFresh[I][C].Deviation(),
                        MixtureFresh[I][C].Deviation(), MixtureFresh[I][C].Deviation() / CosineFresh[I][C].Deviation(), Batches);
            for (uint32_t Cohort = 0; Cohort < 2; ++Cohort)
            {
                const auto& A = CosineCohorts[Cohort][I][C]; const auto& B = MixtureCohorts[Cohort][I][C];
                std::printf("IMPORTANCE_MOTION_COHORT probe=%s channel=%u cohort=%u cosine_mean=%.9g "
                            "cosine_se=%.9g mixture_mean=%.9g mixture_se=%.9g sd_ratio=%.9g n=%u\n",
                            Name.c_str(), C, Cohort, A.Mean(), ReplaySampleStandardError(A), B.Mean(),
                            ReplaySampleStandardError(B), B.Deviation() / A.Deviation(), A.Count);
            }
        }
    }
    std::printf("IMPORTANCE_MOTION_LINKS moved=%llu blocked=%llu spatial=%llu probe_accepts=%u/%u batches=%u\n",
                (unsigned long long)MovedLinks, (unsigned long long)BlockedLinks, (unsigned long long)SpatialLinks,
                MovedProbeAccepts, BlockedProbeAccepts, Batches);
    Require(DetectBlocked && MovedProbeAccepts == 2 * Batches && BlockedProbeAccepts == 2 * Batches &&
                MovedLinks > Batches * 500 && BlockedLinks > Batches * 500 && SpatialLinks > 0,
            "Mixed-proposal motion failed to exercise independent blocker or temporal/spatial history");
    std::printf("PASS mixed-proposal moving rough-textured primary and independent four-hit paths\n");
}
} // namespace
