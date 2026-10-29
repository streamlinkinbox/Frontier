/*==========================================================================================================================================
                                        REPLAYSUBMISSIONSEQUENCEGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Exercise the Engine replay submission sequence with actual offscreen Vulkan execution and fence completion.
#pragma once

std::vector<std::array<float, 4>> ExecuteSequencedReplay(LightComputeSubmission& Compute,
                                                        const LightComputeArchive& Scene,
                                                        uint32_t Seed,
                                                        bool Reset,
                                                        bool Sequenced,
                                                        uint64_t StorageRevision = 1u,
                                                        bool Interrupt = false)
{
    return Compute.ExecuteReplay(Scene, 2u, Seed, true, true, Reset, {.025f, .025f, .025f}, .01f,
                                 true, false, false, true, false, false, 64u, true, false, false, false, false,
                                 false, false, false, 1u, false, false, true, true, true, true, false, false, false,
                                 Sequenced, StorageRevision, true, Interrupt);
}

void ValidateReplaySubmissionSequenceGpu(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 32u;
    const std::array<LightComputeArchive, 4> Scenes{
        ReplayCameraPlane(0, 0, 0, 2, Size), ReplayCameraPlane(0, 0, 0, 2, Size),
        ReplayCameraPlane(.03125f, 0, 0, 2, Size), ReplayCameraPlane(.03125f, .03125f, 0, 2, Size)};
    const std::array<uint32_t, 16> Slots{0, 1, 2, 3, 4, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scenes[0], Shaders, false, false, false, false, true);
    std::array<std::vector<std::array<float, 4>>, 4> DirectImages;
    std::array<std::array<uint32_t, 24>, 4> DirectCounters;
    std::array<std::array<std::vector<std::array<float, 4>>, 16>, 4> DirectSlabs;
    for (uint32_t Index = 0; Index < Scenes.size(); ++Index)
    {
        DirectImages[Index] = ExecuteSequencedReplay(Compute, Scenes[Index], 318001u + 977u * Index,
                                                     Index == 0u, false);
        DirectCounters[Index] = Compute.ExtendedStatistics();
        for (size_t Slot = 0; Slot < Slots.size(); ++Slot)
            DirectSlabs[Index][Slot] = Compute.ReadReplayLogicalSlab(Slots[Slot], false);
    }

    Compute.RebindOutputStorage(false, true, true);
    uint32_t CameraContinuations = 0;
    for (uint32_t Index = 0; Index < Scenes.size(); ++Index)
    {
        const auto Image = ExecuteSequencedReplay(Compute, Scenes[Index], 318001u + 977u * Index,
                                                   Index == 0u, true);
        Require(Image == DirectImages[Index] && Compute.ExtendedStatistics() == DirectCounters[Index],
                "Sequenced and direct replay output or counters differed");
        for (size_t Slot = 0; Slot < Slots.size(); ++Slot)
            Require(Compute.ReadReplayLogicalSlab(Slots[Slot], false) == DirectSlabs[Index][Slot],
                    "Sequenced and direct replay history slabs differed");
        Require(Compute.ReplayLastSequence.Reset == (Index == 0u), "Unexpected replay sequence reset");
        if (Compute.ReplayLastSequence.CameraOnly) ++CameraContinuations;
    }
    Require(CameraContinuations == 2u && DirectCounters[2][20] > 0 && DirectCounters[2][23] > 0 &&
                DirectCounters[2][14] > 0,
            "Subpixel camera continuation did not exercise reservoir, seed and variance history");

    bool Interrupted = false;
    try
    {
        (void)ExecuteSequencedReplay(Compute, Scenes[3], 420001u, false, true, 1u, true);
    }
    catch (const std::runtime_error&)
    {
        Interrupted = true;
    }
    Require(Interrupted && !Compute.ReplaySequence.IsPending(), "Interrupted recording left a pending history");
    const auto Recovered = ExecuteSequencedReplay(Compute, Scenes[3], 420009u, false, true);
    Require(Compute.ReplayLastSequence.Reset && Compute.ExtendedStatistics()[20] == 0u &&
                Compute.ExtendedStatistics()[23] == 0u,
            "Interrupted recording reused stale temporal or seed history");
    const auto ExplicitReset = ExecuteSequencedReplay(Compute, Scenes[3], 420009u, true, true);
    Require(Recovered == ExplicitReset, "Failure recovery differs from the explicit fresh reset");

    const auto NewStorage = ExecuteSequencedReplay(Compute, Scenes[3], 420017u, false, true, 2u);
    Require(Compute.ReplayLastSequence.Reset, "Changed output storage revision did not reset histories");
    const auto NewStorageReset = ExecuteSequencedReplay(Compute, Scenes[3], 420017u, true, true, 2u);
    Require(NewStorage == NewStorageReset, "Changed output storage differs from the explicit reset");
    std::printf("REPLAY_SUBMISSION_SEQUENCE frames=4 slabs=16 counters=24 exact=1 camera_continuations=%u "
                "interruption_reset=1 storage_reset=1\n", CameraContinuations);
    std::printf("PASS ReplaySubmissionSequenceGpuValidation\n");
}
