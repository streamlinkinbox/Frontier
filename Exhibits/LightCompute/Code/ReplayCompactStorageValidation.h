/*==========================================================================================================================================
                                             REPLAYCOMPACTSTORAGEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify actual compact Vulkan allocation, logical-slab equivalence, primary misses and reset/corruption behaviour.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                 BYTE-EXACT COMPARISON
//------------------------------------------------------------------------------------------------------------------------

void VerifyCompactTail(const LightComputeSubmission& Compute)
{
    Require(Compute.ReplayOutputDescriptorBytes <= Compute.Buffers[6].Size, "Descriptor exceeds output allocation");
    const auto* Bytes = static_cast<const unsigned char*>(Compute.Buffers[6].Address);
    for (size_t Offset = size_t(Compute.ReplayOutputDescriptorBytes); Offset < Compute.Buffers[6].Size; ++Offset)
        Require(Bytes[Offset] == 0x5a, "Compact output tail guard was overwritten");
}

std::vector<std::array<float,4>> CompareCompactReplay(LightComputeSubmission& Legacy,
    LightComputeSubmission& Compact, const LightComputeArchive& Scene, uint32_t Seed, bool Reset,
    bool Reservoir = true, bool Spatial = true, uint32_t Bounces = 4, bool Facets = true)
{
    const auto Execute = [&](LightComputeSubmission& Compute)
    {
        if (Reservoir) return ExecuteReplaySeedReservoir(Compute, Scene, Seed, Reset, Spatial, Bounces, Facets);
        return ExecuteReplaySampleHistory(Compute, Scene, Seed, Reset, true, Bounces, {0,0,0}, 1,
                                          false, false, false, false, false, true, Facets);
    };
    const auto Before = Execute(Legacy), After = Execute(Compact);
    Require(ReplayCandidateImagesEqual(Before, After), "Compact output differs from legacy replay");
    Require(Legacy.ExtendedStatistics() == Compact.ExtendedStatistics(), "Compact replay changed GPU work/history counters");
    const auto BeforeGuides = Legacy.PrimaryReceivers(), AfterGuides = Compact.PrimaryReceivers();
    Require(BeforeGuides.size() == AfterGuides.size() &&
        std::memcmp(BeforeGuides.data(), AfterGuides.data(), BeforeGuides.size() * sizeof(LightComputeReceiver)) == 0,
        "Compact replay changed primary guides");
    Require(ReplayCandidateImagesEqual(Legacy.PrimaryStrikes(), Compact.PrimaryStrikes()),
            "Compact primary hit/miss slab differs");
    constexpr std::array<uint32_t,9> Slots{0,1,2,3,4,11,12,13,14};
    for (uint32_t Pixel = 0; Pixel < Before.size(); ++Pixel)
        for (uint32_t Slot : Slots)
        {
            const auto A = ReplaySampleSlot(Legacy, Slot, Pixel), B = ReplaySampleSlot(Compact, Slot, Pixel);
            Require(std::memcmp(A.data(), B.data(), sizeof(A)) == 0, "Compact logical history slab differs");
        }
    VerifyCompactTail(Compact);
    return After;
}

void ReportCompactAllocation(const LightComputeSubmission& Compute, const char* Layout)
{
    uint64_t Requested = 0, Allocated = 0;
    for (const auto& Buffer : Compute.Buffers)
    {
        Requested += Buffer.Size;
        Allocated += Buffer.AllocationBytes;
    }
    Require(Compute.Buffers[6].AllocationBytes >= Compute.Buffers[6].Size,
            "Vulkan output memory requirement is undersized");
    std::printf("COMPACT_ALLOCATION layout=%s pixels=%u descriptor_bytes=%llu output_buffer_bytes=%llu "
                "output_vk_allocation_bytes=%llu all_buffers_requested_bytes=%llu all_vk_allocation_bytes=%llu\n",
                Layout, Compute.ReceiverCount, static_cast<unsigned long long>(Compute.ReplayOutputDescriptorBytes),
                static_cast<unsigned long long>(Compute.Buffers[6].Size),
                static_cast<unsigned long long>(Compute.Buffers[6].AllocationBytes),
                static_cast<unsigned long long>(Requested), static_cast<unsigned long long>(Allocated));
}

//------------------------------------------------------------------------------------------------------------------------
//                                            MOTION / CORRUPTION / MISSES
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayCompactStorage(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 17;
    const auto Scene = ReplayCameraPlane(0, 0, 0, 2, Size);
    LightComputeSubmission Legacy(Driver), Compact(Driver);
    Legacy.Prepare(Scene, Shaders, false, false, false, false, true);
    Compact.Prepare(Scene, Shaders, false, false, false, false, true, false, true, 16);
    Require(Compact.ReplayOutputDescriptorBytes == PathReplayRequiredCompactOutputRecords(Size * Size) * 16 &&
        Compact.Buffers[6].Size == Compact.ReplayOutputDescriptorBytes + 256 &&
        Compact.Buffers[6].AllocationBytes < Legacy.Buffers[6].AllocationBytes,
        "Compact replay did not reduce the actual Vulkan allocation");
    std::printf("COMPACT_DEVICE name=%s type=%u api=%u\n", Compact.Properties.deviceName,
                uint32_t(Compact.Properties.deviceType), Compact.Properties.apiVersion);
    std::fflush(stdout);
    uint32_t Frames = 0;
    for (uint32_t Index = 0; Index < 3; ++Index)
    {
        (void)CompareCompactReplay(Legacy, Compact, Scene, 150001u + Index * 7919u, Index == 0);
        ++Frames;
    }
    Require(Compact.ExtendedStatistics()[23] == Size * Size, "Compact stationary seed banks were not retained");
    const auto Moved = ReplayCameraPlane(1.f / Size, 0, 0, 2, Size);
    (void)CompareCompactReplay(Legacy, Compact, Moved, 191111, false); ++Frames;
    Require(Compact.ExtendedStatistics()[23] > 0 && !Compact.ReplayLastSampleHistoryReset,
            "Compact camera-only transition lost all seed banks");
    const auto Steady = ReplayCameraPlane(1.f / Size, 1.f / Size, 0, 2, Size);
    (void)CompareCompactReplay(Legacy, Compact, Steady, 191113, false, true, false); ++Frames;
    Require(Compact.ReplayLastSampleHistoryReset, "Spatial-policy transition did not reset compact banks");
    (void)CompareCompactReplay(Legacy, Compact, Steady, 191117, false, true, false); ++Frames;
    for (uint32_t Pixel = 0; Pixel < 7; ++Pixel)
        for (auto* Compute : {&Legacy, &Compact})
        {
            const uint32_t Slot = Pixel < 4 ? 11 : 12;
            auto Bank = ReplaySampleSlot(*Compute, Slot, Pixel);
            if (Pixel == 0) Bank[0] = std::numeric_limits<float>::quiet_NaN();
            if (Pixel == 1) Bank[0] = -1;
            if (Pixel == 2) Bank[0] = .5f;
            if (Pixel == 3) Bank[0] = 65536;
            if (Pixel == 4) Bank[2] = std::numeric_limits<float>::quiet_NaN();
            if (Pixel == 5) Bank[2] = 4;
            if (Pixel == 6) Bank[2] = 1.5f;
            SetReplaySampleSlot(*Compute, Slot, Pixel, Bank);
        }
    (void)CompareCompactReplay(Legacy, Compact, Steady, 191119, false, true, false); ++Frames;
    Require(Compact.ExtendedStatistics()[23] == Size * Size - 7, "Compact corrupt-bank rejection count differs");
    for (uint32_t Pixel = 0; Pixel < 7; ++Pixel)
        Require(ReplaySampleSlot(Compact, 14, Pixel)[2] == 0, "Compact corrupt bank was retained");
    const auto Geometry = ReplayCameraPlane(1.f / Size, 1.f / Size, .1f, 2, Size);
    (void)CompareCompactReplay(Legacy, Compact, Geometry, 191123, false, true, false); ++Frames;
    Require(Compact.ReplayLastSampleHistoryReset && Compact.ExtendedStatistics()[23] == 0,
            "Compact geometry revision retained stale banks");
    const auto Dark = ReplayCameraPlane(1.f / Size, 1.f / Size, .1f, 0, Size);
    const auto DarkImage = CompareCompactReplay(Legacy, Compact, Dark, 191127, false, true, false); ++Frames;
    for (const auto& Pixel : DarkImage)
        Require(Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 0, "Compact source-off frame leaked old radiance");
    (void)CompareCompactReplay(Legacy, Compact, Geometry, 191129, false, true, false); ++Frames;
    const auto Miss = ReplayCameraPlane(12, 1.f / Size, .1f, 2, Size);
    (void)CompareCompactReplay(Legacy, Compact, Miss, 191131, false, true, false); ++Frames;
    Require(Compact.ExtendedStatistics()[11] == Size * Size && Compact.ExtendedStatistics()[23] == 0,
            "Compact miss case did not exercise every primary miss");
    for (const auto& Strike : Compact.PrimaryStrikes())
        Require(Strike[0] == -1 && Strike[1] == -1, "Compact miss diagnostics were not stored");
    for (uint32_t Index = 0; Index < 3; ++Index)
    {
        (void)CompareCompactReplay(Legacy, Compact, Scene, 200001u + Index * 7919u, Index == 0,
                                   false, false, 1, false);
        ++Frames;
    }
    const auto Sequence = Compact.HistorySequence;
    bool Rejected = false;
    try { (void)ExecuteReplaySampleHistory(Compact, Scene, 1, false, false); }
    catch (const std::exception&) { Rejected = true; }
    Require(Rejected && Compact.HistorySequence == Sequence, "Compact allocation accepted a legacy-only dispatch");
    VerifyCompactTail(Compact);
    ReportCompactAllocation(Legacy, "legacy");
    ReportCompactAllocation(Compact, "compact_guarded");
    std::printf("COMPACT_PARITY frames=%u pixels_each=%u logical_slabs=9 primary_strikes=1 output_exact=1 "
                "guides_exact=1 counters_exact=1 tail_guard_bytes=256 corrupt_banks=7 misses=%u "
                "mean_and_ris=1 incompatible_request_rejected=1\n", Frames, Size * Size, Size * Size);
    std::printf("PASS actual compact seed-history Vulkan allocation and byte-exact replay\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ACTUAL READBACK PAIR
//------------------------------------------------------------------------------------------------------------------------

void CaptureReplayCompactStorage(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Size = 64;
    const auto Previous = ReplayCameraPlane(0, 0, 0, 2, Size);
    const auto Current = ReplayCameraPlane(1.f / Size, 0, 0, 2, Size);
    LightComputeSubmission Legacy(Driver), Compact(Driver);
    Legacy.Prepare(Previous, Shaders, false, false, false, false, true);
    Compact.Prepare(Previous, Shaders, false, false, false, false, true, false, true);
    for (uint32_t Index = 0; Index < 3; ++Index)
        (void)CompareCompactReplay(Legacy, Compact, Previous, 340001u + Index * 7919u, Index == 0);
    const auto Image = CompareCompactReplay(Legacy, Compact, Current, 380011, false);
    Require(Compact.ExtendedStatistics()[23] == 3990, "Compact capture seed acceptance changed");
    std::filesystem::create_directories(Directory);
    // 📝 The equality assertion ran before writing. Both files contain their own actual Vulkan readback.
    std::vector<std::array<float,4>> LegacyImage(Image.size());
    std::memcpy(LegacyImage.data(), Legacy.Buffers[6].Address, LegacyImage.size() * 16);
    WriteImage(Directory + "/CompactReplayLegacy.ppm", LegacyImage, Size, Size, 3);
    WriteImage(Directory + "/CompactReplayPacked.ppm", Image, Size, Size, 3);
    ReportCompactAllocation(Legacy, "legacy_capture");
    ReportCompactAllocation(Compact, "compact_capture");
    std::printf("COMPACT_CAPTURE size=64 accepted=3990 bounces=4 spatial=1 output_and_all_slabs_exact=1\n");
}
} // namespace
