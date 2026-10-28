/*==========================================================================================================================================
                                                ROUGHTEXTUREMOTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare rough-textured moving-primary replay against independent multibounce paths.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                               INDEPENDENT PATH AND PRIMARY COEFFICIENTS
//------------------------------------------------------------------------------------------------------------------------

ReplayRgbMoments TraceRoughMotionReference(const LightComputeArchive& Scene, const ReplayPrimaryReference& Primary,
                                           uint64_t Seed, uint32_t Count)
{
    ReplayRgbMoments Result;
    std::mt19937_64 Random(Seed);
    for (uint32_t I = 0; I < Count; ++I)
    {
        const auto Radiance = ReplayReferencePath(Scene, Primary.Receiver, 4, Random, true, true);
        for (uint32_t C = 0; C < 3; ++C)
            Result[C].Accumulate(Radiance[C] + Primary.Receiver.ResidualSpecular[C]);
    }
    return Result;
}

void ValidateRoughMotionCoefficients(const LightComputeArchive& Scene,
                                     const std::vector<ReplayPrimaryReference>& Reference,
                                     const std::vector<LightComputeReceiver>& Actual, const char* Name)
{
    Require(Reference.size() == Actual.size(), "Rough motion primary coefficient extent mismatch");
    uint32_t Count = 0;
    double Error = 0;
    for (size_t I = 0; I < Reference.size(); ++I)
    {
        if (!Reference[I].Hit || Scene.Surfaces[Reference[I].Facet].Emissive[3] <= 0) continue;
        ++Count;
        const auto& R = Reference[I].Receiver;
        const auto& G = Actual[I];
        Error = std::max({Error, std::abs(double(R.NormalRoughness[3]) - G.NormalRoughness[3]),
                          std::abs(double(R.ViewMetallic[3]) - G.ViewMetallic[3]),
                          std::abs(double(R.AlbedoIor[3]) - G.AlbedoIor[3]),
                          std::abs(double(R.ExpectedDirect[3]) - G.ExpectedDirect[3])});
    }
    std::printf("ROUGH_TEXTURE_MOTION_COEFFICIENTS %s textured=%u max_error=%.9g\n", Name, Count, Error);
    Require(Count > 400 && Error < 1e-6, "Rough moving-primary material coefficients differ from independent rays");
}

//------------------------------------------------------------------------------------------------------------------------
//                                               CAMERA, RIGID MOTION AND OCCLUSION
//------------------------------------------------------------------------------------------------------------------------

void ValidateRoughTextureMotion(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Width = 64, Height = 64, CpuPaths = 240000, Batches = 128;
    auto Previous = ReplayTexturedMotionRigid(0, 0, 0, 7, Width, Height);
    auto Current = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    auto Covered = ReplayTexturedMotionRigid(.07f, .12f, 1, 0, Width, Height);
    SettleReplayMotion(Covered);
    SetRoughTextureReplayMaterials(Previous, .65f, .35f);
    SetRoughTextureReplayMaterials(Current, .48f, .7f);
    SetRoughTextureReplayMaterials(Covered, .48f, .7f);
    ValidateReplayRigidTextureCoordinates(Previous, Current, .12f);
    LightComputeSubmission Compute(Driver);
    std::printf("ROUGH_TEXTURE_MOTION preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    (void)TraceRoughTextureReplay(Compute, Previous, 1, 240001, false, false, true, true);
    const auto PreviousGuides = Compute.PrimaryReceivers();
    const auto PreviousReference = ValidateReplayPrimaryReference(Previous, PreviousGuides, "rough_previous");
    ValidateRoughMotionCoefficients(Previous, PreviousReference, PreviousGuides, "previous");
    (void)TraceRoughTextureReplay(Compute, Current, 1, 240002, false, false, true, true);
    const auto CurrentGuides = Compute.PrimaryReceivers();
    const auto CurrentReference = ValidateReplayPrimaryReference(Current, CurrentGuides, "rough_moved");
    ValidateRoughMotionCoefficients(Current, CurrentReference, CurrentGuides, "moved");
    (void)TraceRoughTextureReplay(Compute, Covered, 1, 240003, false, false, true, true);
    const auto CoveredGuides = Compute.PrimaryReceivers();
    const auto CoveredReference = ValidateReplayPrimaryReference(Covered, CoveredGuides, "rough_blocked");
    ValidateRoughMotionCoefficients(Covered, CoveredReference, CoveredGuides, "blocked");
    const std::array<uint32_t,2> Pixels{
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 8, 28, 38),
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 1, 46, 50)};
    std::array<ReplayRgbMoments,2> CpuOpen, CpuBlocked, GpuFresh, GpuMoved, GpuBlocked;
    std::printf("ROUGH_TEXTURE_MOTION tracing %u CPU paths for each of four primary pixels\n", CpuPaths);
    std::fflush(stdout);
    for (uint32_t I = 0; I < Pixels.size(); ++I)
    {
        CpuOpen[I] = TraceRoughMotionReference(Current, CurrentReference[Pixels[I]], 241001 + I, CpuPaths);
        CpuBlocked[I] = TraceRoughMotionReference(Covered, CoveredReference[Pixels[I]], 242001 + I, CpuPaths);
    }
    uint64_t MovedLinks = 0, BlockedLinks = 0, SpatialLinks = 0;
    uint32_t MovedProbeAccepts = 0, BlockedProbeAccepts = 0;
    auto RepresentedCount = [&](uint32_t Pixel)
    {
        // 📝 Slot two is the pre-spatial current reservoir; its fourth component counts represented candidates.
        const size_t Start = size_t(Width) * Height * 27 + 13889 + SolarSourceCount;
        std::array<float,4> Packed{};
        std::memcpy(Packed.data(), static_cast<const char*>(Compute.Buffers[6].Address) + (Start + Pixel) * 16, 16);
        return Packed[3];
    };
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 243000 + 101 * Batch;
        const auto Fresh = TraceRoughTextureReplay(Compute, Current, 4, Seed, false, true, true, true);
        for (uint32_t I = 0; I < Pixels.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C) GpuFresh[I][C].Accumulate(Fresh[Pixels[I]][C]);
        for (uint32_t Iteration = 0; Iteration < 3; ++Iteration)
            (void)TraceRoughTextureReplay(Compute, Previous, 4, Seed + 10 + Iteration, true, true, Iteration == 0, true);
        const auto Moved = TraceRoughTextureReplay(Compute, Current, 4, Seed + 20, true, true, false, true);
        auto Counts = Compute.ExtendedStatistics();
        MovedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Rough-textured rigid transition did not retain capped history");
        for (uint32_t I = 0; I < Pixels.size(); ++I)
        {
            MovedProbeAccepts += RepresentedCount(Pixels[I]) == 2 ? 1u : 0u;
            for (uint32_t C = 0; C < 3; ++C) GpuMoved[I][C].Accumulate(Moved[Pixels[I]][C]);
        }
        const auto Blocked = TraceRoughTextureReplay(Compute, Covered, 4, Seed + 21, true, true, false, true);
        Counts = Compute.ExtendedStatistics();
        BlockedLinks += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Rough-textured blocker transition did not retain capped history");
        for (uint32_t I = 0; I < Pixels.size(); ++I)
        {
            BlockedProbeAccepts += RepresentedCount(Pixels[I]) == 2 ? 1u : 0u;
            for (uint32_t C = 0; C < 3; ++C) GpuBlocked[I][C].Accumulate(Blocked[Pixels[I]][C]);
        }
    }
    bool DetectBlocked = false;
    for (uint32_t I = 0; I < Pixels.size(); ++I)
    {
        const char* Name = I == 0 ? "rough_object" : "rough_floor";
        const std::string FreshName = std::string(Name) + "_fresh";
        const std::string MovedName = std::string(Name) + "_moved_reused";
        const std::string BlockedName = std::string(Name) + "_blocked_reused";
        CompareReplayMotionProbe(FreshName.c_str(), CpuOpen[I], GpuFresh[I]);
        CompareReplayMotionProbe(MovedName.c_str(), CpuOpen[I], GpuMoved[I]);
        CompareReplayMotionProbe(BlockedName.c_str(), CpuBlocked[I], GpuBlocked[I]);
        for (uint32_t C = 0; C < 3; ++C)
            DetectBlocked |= CpuOpen[I][C].Mean() - CpuBlocked[I][C].Mean() >
                6 * std::hypot(ReplaySampleStandardError(CpuOpen[I][C]), ReplaySampleStandardError(CpuBlocked[I][C]));
    }
    std::printf("ROUGH_TEXTURE_MOTION_LINKS moved=%llu blocked=%llu spatial=%llu moved_probe_accepts=%u "
                "blocked_probe_accepts=%u batches=%u\n", (unsigned long long)MovedLinks,
                (unsigned long long)BlockedLinks, (unsigned long long)SpatialLinks, MovedProbeAccepts,
                BlockedProbeAccepts, Batches);
    Require(DetectBlocked && MovedProbeAccepts == 2 * Batches && BlockedProbeAccepts == 2 * Batches &&
                MovedLinks > Batches * 500 && BlockedLinks > Batches * 500 && SpatialLinks > 0,
            "Rough motion probes did not retain history or the independent blocker control was insensitive");
    std::printf("PASS independent rough-textured primary and four-hit paths during rigid motion and occlusion\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                              HIGHER-SAMPLE FRESH FLOOR CHECK
//------------------------------------------------------------------------------------------------------------------------

void ValidateRoughTextureFreshFloor(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Width = 64, Height = 64, CpuPaths = 960000, Batches = 512;
    constexpr uint32_t Pixel = 50 * Width + 46;
    auto Current = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    SetRoughTextureReplayMaterials(Current, .48f, .7f);
    LightComputeSubmission Compute(Driver);
    std::printf("ROUGH_TEXTURE_FRESH preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Current, Shaders, false, false, false, false, true);
    (void)TraceRoughTextureReplay(Compute, Current, 1, 462999, false, false, true, true);
    const auto Guides = Compute.PrimaryReceivers();
    const auto Reference = ValidateReplayPrimaryReference(Current, Guides, "rough_fresh_repeat");
    ValidateRoughMotionCoefficients(Current, Reference, Guides, "fresh_repeat");
    Require(Reference[Pixel].Hit && Reference[Pixel].Mip == 2 &&
                Current.Surfaces[Reference[Pixel].Facet].OpticalToken[3] == 1 &&
                Current.Surfaces[Reference[Pixel].Facet].Emissive[3] > 0,
            "Fresh repeat did not target the original textured floor probe");
    std::printf("ROUGH_TEXTURE_FRESH tracing %u independently seeded CPU paths for pixel=%u,%u\n",
                CpuPaths, Pixel % Width, Pixel / Width);
    std::fflush(stdout);
    const auto Cpu = TraceRoughMotionReference(Current, Reference[Pixel], 361111, CpuPaths);
    ReplayRgbMoments Gpu;
    uint64_t SpatialLinks = 0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const auto Fresh = TraceRoughTextureReplay(Compute, Current, 4, 463000 + 101 * Batch,
                                                    false, true, true, true);
        const auto Counts = Compute.ExtendedStatistics();
        SpatialLinks += Counts[22];
        for (uint32_t C = 0; C < 3; ++C) Gpu[C].Accumulate(Fresh[Pixel][C]);
        if ((Batch + 1) % 128 == 0)
        {
            std::printf("ROUGH_TEXTURE_FRESH completed=%u/%u batches\n", Batch + 1, Batches);
            std::fflush(stdout);
        }
    }
    CompareReplayMotionProbe("rough_floor_fresh_repeat", Cpu, Gpu);
    const double CombinedSe = std::hypot(ReplaySampleStandardError(Cpu[2]), ReplaySampleStandardError(Gpu[2]));
    std::printf("ROUGH_TEXTURE_FRESH blue_delta=%.9g combined_se=%.9g z=%.9g spatial_links=%llu\n",
                Gpu[2].Mean() - Cpu[2].Mean(), CombinedSe, std::abs(Gpu[2].Mean() - Cpu[2].Mean()) / CombinedSe,
                (unsigned long long)SpatialLinks);
    Require(SpatialLinks > 0, "Fresh repeat did not exercise spatial candidates");
    std::printf("PASS independently seeded higher-sample rough-textured fresh floor reference\n");
}
} // namespace
