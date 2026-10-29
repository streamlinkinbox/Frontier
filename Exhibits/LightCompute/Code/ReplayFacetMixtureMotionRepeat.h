/*==========================================================================================================================================
                                         REPLAYFACETMIXTUREMOTIONREPEAT.H
==========================================================================================================================================*/
// 🧩 Repeat moving-primary replay with disjoint seeds and paired fresh/reused comparisons.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                               PAIRED MOTION SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t RepeatProbeCount = 2, RepeatModeCount = 7;
constexpr std::array<const char*, RepeatModeCount> RepeatModeNames{
    "uniform_moved", "mixture_moved", "mixture_blocked", "mixture_reopened",
    "reused_moved", "reused_blocked", "reused_reopened"};
using ReplayRepeatMoments = std::array<std::array<ReplayRgbMoments, RepeatProbeCount>, RepeatModeCount>;
struct ReplayMotionRepeatFootprint
{
    std::array<std::array<std::array<float, 4>, RepeatProbeCount>, RepeatModeCount> Radiance{};
    std::array<std::array<float, RepeatProbeCount>, 3> Represented{};
    std::array<uint32_t, 3> Temporal{}, Spatial{}, Caps{};
    std::array<bool, 3> Reset{};
};

ReplayMotionRepeatFootprint TraceReplayMotionRepeat(LightComputeSubmission& Compute,
                                                     const LightComputeArchive& Previous,
                                                     const LightComputeArchive& Current,
                                                     const LightComputeArchive& Covered,
                                                     const LightComputeArchive& Reopened,
                                                     const std::array<uint32_t, RepeatProbeCount>& Pixels,
                                                     uint32_t Seed, bool RegionAudit)
{
    ReplayMotionRepeatFootprint Result;
    auto Trace = [&](const LightComputeArchive& Scene, uint32_t Offset, bool Temporal, bool Reset,
                     bool Importance, bool Weighted, bool Mixture)
    {
        return Compute.ExecuteReplay(Scene, 4, Seed + Offset, Temporal, true, Reset, {.025f, .025f, .025f}, .01f,
                                     true, false, true, true, false, false, 64, true, true, Importance,
                                     Weighted, Mixture, RegionAudit);
    };
    auto Record = [&](uint32_t Mode, const std::vector<std::array<float, 4>>& Image)
    {
        for (uint32_t Probe = 0; Probe < RepeatProbeCount; ++Probe)
        {
            Result.Radiance[Mode][Probe] = Image[Pixels[Probe]];
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
                Require(std::isfinite(Result.Radiance[Mode][Probe][Channel]) &&
                            Result.Radiance[Mode][Probe][Channel] >= 0,
                        "Moving repeat encountered invalid probe radiance");
        }
    };
    Record(0, Trace(Current, 20, false, true, true, false, false));
    Record(1, Trace(Current, 20, false, true, true, true, true));
    Record(2, Trace(Covered, 21, false, true, true, true, true));
    Record(3, Trace(Reopened, 22, false, true, true, true, true));
    for (uint32_t Iteration = 0; Iteration < 3; ++Iteration)
        (void)Trace(Previous, 10 + Iteration, true, Iteration == 0, false, false, false);
    auto RecordTransition = [&](uint32_t Transition, uint32_t Mode,
                                const std::vector<std::array<float, 4>>& Image)
    {
        Record(Mode, Image);
        const auto Counts = Compute.ExtendedStatistics();
        Result.Temporal[Transition] = Counts[20];
        Result.Spatial[Transition] = Counts[22];
        Result.Caps[Transition] = Compute.ReplayLastHistoryCap;
        Result.Reset[Transition] = Compute.ReplayLastReset;
        const size_t Start = size_t(Current.Width) * Current.Height * 27 + 13889 + SolarSourceCount;
        for (uint32_t Probe = 0; Probe < RepeatProbeCount; ++Probe)
        {
            std::array<float, 4> Packed{};
            std::memcpy(Packed.data(), static_cast<const char*>(Compute.Buffers[6].Address) +
                                          (Start + Pixels[Probe]) * 16, 16);
            Result.Represented[Transition][Probe] = Packed[3];
        }
        Require(!Result.Reset[Transition] && Result.Caps[Transition] == 1 &&
                    Result.Temporal[Transition] > 0 && Result.Spatial[Transition] > 0 &&
                    Result.Represented[Transition][0] == 2 && Result.Represented[Transition][1] == 2,
                "Moving repeat did not retain capped temporal/spatial history at both probes");
    };
    RecordTransition(0, 4, Trace(Current, 20, true, false, true, true, true));
    RecordTransition(1, 5, Trace(Covered, 21, true, false, true, true, true));
    RecordTransition(2, 6, Trace(Reopened, 22, true, false, true, true, true));
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PAIRED DISPERSION COMPARISON
//------------------------------------------------------------------------------------------------------------------------

void ReportReplayMotionDispersion(const std::vector<ReplayMotionRepeatFootprint>& Samples,
                                  const std::vector<ReplayRepeatMoments>& Groups,
                                  const ReplayRepeatMoments& Aggregate, uint32_t Numerator,
                                  uint32_t Denominator, const char* Label)
{
    Require(Samples.size() >= 8 && Groups.size() >= 2, "Repeat dispersion needs multiple seed groups");
    for (uint32_t Probe = 0; Probe < RepeatProbeCount; ++Probe)
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const auto& NumeratorMoments = Aggregate[Numerator][Probe][Channel];
            const auto& DenominatorMoments = Aggregate[Denominator][Probe][Channel];
            Require(DenominatorMoments.Deviation() > 0, "Repeat dispersion denominator has zero spread");
            const double Ratio = NumeratorMoments.Deviation() / DenominatorMoments.Deviation();
            double GroupMinimum = std::numeric_limits<double>::infinity(), GroupMaximum = 0;
            for (const auto& Group : Groups)
            {
                const double DenominatorDeviation = Group[Denominator][Probe][Channel].Deviation();
                Require(DenominatorDeviation > 0, "Repeat group has zero denominator spread");
                const double GroupRatio = Group[Numerator][Probe][Channel].Deviation() / DenominatorDeviation;
                GroupMinimum = std::min(GroupMinimum, GroupRatio);
                GroupMaximum = std::max(GroupMaximum, GroupRatio);
            }
            DirectGpuMoments Difference;
            for (const auto& Sample : Samples)
                Difference.Accumulate(double(Sample.Radiance[Numerator][Probe][Channel]) -
                                      Sample.Radiance[Denominator][Probe][Channel]);
            // 📝 Frame seeds are resampled as paired units. This is conditional on the observed batches;
            //     the nominal interval does not establish independent shader streams or bound unseen rare events.
            constexpr uint32_t Replicates = 2000;
            std::mt19937_64 Random(0x19b4a3d7ULL + 100 * Numerator + 10 * Probe + Channel);
            std::uniform_int_distribution<size_t> Index(0, Samples.size() - 1);
            std::vector<double> Ratios;
            Ratios.reserve(Replicates);
            for (uint32_t Iteration = 0; Iteration < Replicates; ++Iteration)
            {
                DirectGpuMoments NumeratorResample, DenominatorResample;
                for (size_t Draw = 0; Draw < Samples.size(); ++Draw)
                {
                    const auto& Sample = Samples[Index(Random)];
                    NumeratorResample.Accumulate(Sample.Radiance[Numerator][Probe][Channel]);
                    DenominatorResample.Accumulate(Sample.Radiance[Denominator][Probe][Channel]);
                }
                if (DenominatorResample.Deviation() > 0)
                    Ratios.push_back(NumeratorResample.Deviation() / DenominatorResample.Deviation());
            }
            Require(Ratios.size() >= Replicates * 99 / 100, "Repeat bootstrap has too many zero-spread resamples");
            std::sort(Ratios.begin(), Ratios.end());
            const double Lower = Ratios[size_t(.025 * (Ratios.size() - 1))];
            const double Upper = Ratios[size_t(.975 * (Ratios.size() - 1))];
            std::printf("MOTION_REPEAT_DISPERSION comparison=%s probe=%s channel=%u sd_ratio=%.9g "
                        "bootstrap_95=%.9g,%.9g group_range=%.9g,%.9g paired_difference=%.9g "
                        "paired_se=%.9g samples=%zu\n", Label, Probe == 0 ? "rigid" : "floor", Channel,
                        Ratio, Lower, Upper, GroupMinimum, GroupMaximum, Difference.Mean(),
                        ReplaySampleStandardError(Difference), Samples.size());
        }
}

//------------------------------------------------------------------------------------------------------------------------
//                                         DISJOINT-SEED MOVING-PRIMARY REPEAT
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayFacetMixtureMotionRepeat(const char* Driver, const std::string& Shaders,
                                            const std::string& TablePath, uint32_t Groups = 4,
                                            uint32_t BatchesPerGroup = 64, uint32_t CpuPaths = 240000)
{
    Require(Groups >= 2 && BatchesPerGroup >= 4 && CpuPaths >= 10000,
            "Moving repeat requires multiple seed groups and independent CPU paths");
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
    LightComputeSubmission Complete(Driver), Restricted(Driver);
    std::printf("MOTION_REPEAT preparing Vulkan on %s\n", Complete.Properties.deviceName);
    std::fflush(stdout);
    Complete.Prepare(Previous, Shaders, false, false, false, false, true);
    Restricted.Prepare(Previous, Shaders, false, false, false, false, true);
    (void)TraceRoughTextureReplay(Complete, Previous, 1, 1840001, false, false, true, true);
    const auto PreviousGuides = Complete.PrimaryReceivers();
    const auto PreviousReference = ValidateReplayPrimaryReference(Previous, PreviousGuides, "repeat_previous");
    ValidateRoughMotionCoefficients(Previous, PreviousReference, PreviousGuides, "repeat_previous");
    (void)TraceRoughTextureReplay(Complete, Current, 1, 1840002, false, false, true, true);
    const auto CurrentGuides = Complete.PrimaryReceivers();
    const auto CurrentReference = ValidateReplayPrimaryReference(Current, CurrentGuides, "repeat_moved");
    ValidateRoughMotionCoefficients(Current, CurrentReference, CurrentGuides, "repeat_moved");
    (void)TraceRoughTextureReplay(Complete, Covered, 1, 1840003, false, false, true, true);
    const auto CoveredGuides = Complete.PrimaryReceivers();
    const auto CoveredReference = ValidateReplayPrimaryReference(Covered, CoveredGuides, "repeat_blocked");
    ValidateRoughMotionCoefficients(Covered, CoveredReference, CoveredGuides, "repeat_blocked");
    (void)TraceRoughTextureReplay(Complete, Reopened, 1, 1840004, false, false, true, true);
    const auto ReopenedGuides = Complete.PrimaryReceivers();
    const auto ReopenedReference = ValidateReplayPrimaryReference(Reopened, ReopenedGuides, "repeat_reopened");
    ValidateRoughMotionCoefficients(Reopened, ReopenedReference, ReopenedGuides, "repeat_reopened");
    const std::array<uint32_t, RepeatProbeCount> Pixels{
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 8, 28, 38),
        SelectReplayMotionProbe(Current, PreviousGuides, CurrentGuides, CurrentReference, 1, 46, 50)};
    Require(Pixels[0] == 38 * Width + 28 && Pixels[1] == 50 * Width + 46,
            "Probe-region audit shader does not contain the independently selected pixels");
    std::array<std::array<ReplayRgbMoments, RepeatProbeCount>, 3> Cpu{};
    std::printf("MOTION_REPEAT tracing %u paths per independent CPU reference\n", CpuPaths);
    std::fflush(stdout);
    for (uint32_t Probe = 0; Probe < RepeatProbeCount; ++Probe)
    {
        Cpu[0][Probe] = TraceRoughMotionReference(Current, CurrentReference[Pixels[Probe]], 0x73ab1001ULL + Probe, CpuPaths);
        Cpu[1][Probe] = TraceRoughMotionReference(Covered, CoveredReference[Pixels[Probe]], 0x73ab2001ULL + Probe, CpuPaths);
        Cpu[2][Probe] = TraceRoughMotionReference(Reopened, ReopenedReference[Pixels[Probe]], 0x73ab3001ULL + Probe, CpuPaths);
    }
    // 📝 Reject overlap with the previous 96-batch sequence, including its history and transition offsets.
    constexpr std::array<uint32_t, 7> Offsets{0, 10, 11, 12, 20, 21, 22};
    std::set<uint32_t> Reserved, Used;
    for (uint32_t Batch = 0; Batch < 96; ++Batch)
        for (uint32_t Offset : Offsets) Reserved.insert(944001 + 103 * Batch + Offset);
    for (uint32_t Seed : {940001u, 940002u, 940003u, 940004u}) Reserved.insert(Seed);
    std::mt19937 FrameRandom(0x5462a971u);
    std::vector<uint32_t> Seeds;
    Seeds.reserve(size_t(Groups) * BatchesPerGroup);
    while (Seeds.size() < size_t(Groups) * BatchesPerGroup)
    {
        const uint32_t Seed = FrameRandom();
        if (Seed > UINT32_MAX - 22) continue;
        bool Collision = false;
        for (uint32_t Offset : Offsets)
            Collision |= Reserved.count(Seed + Offset) != 0 || Used.count(Seed + Offset) != 0;
        if (Collision) continue;
        for (uint32_t Offset : Offsets) Used.insert(Seed + Offset);
        Seeds.push_back(Seed);
    }
    // 📝 The audit dispatch preserves primary rays but skips distant receivers. Verify the exact selected
    //     output and local history against the complete dispatch before relying on the restricted repeat.
    uint64_t Compared = 0, Different = 0;
    constexpr uint32_t AuditSeeds = 3;
    for (uint32_t Audit = 0; Audit < AuditSeeds; ++Audit)
    {
        const auto Full = TraceReplayMotionRepeat(Complete, Previous, Current, Covered, Reopened, Pixels, Seeds[Audit], false);
        const auto Sparse = TraceReplayMotionRepeat(Restricted, Previous, Current, Covered, Reopened, Pixels, Seeds[Audit], true);
        Require(Full.Caps == Sparse.Caps && Full.Reset == Sparse.Reset && Full.Represented == Sparse.Represented,
                "Restricted probe history differs from the complete primary dispatch");
        for (uint32_t Mode = 0; Mode < RepeatModeCount; ++Mode)
            for (uint32_t Probe = 0; Probe < RepeatProbeCount; ++Probe)
                for (uint32_t Channel = 0; Channel < 4; ++Channel)
                {
                    Different += std::memcmp(&Full.Radiance[Mode][Probe][Channel],
                                             &Sparse.Radiance[Mode][Probe][Channel], sizeof(float)) != 0;
                    ++Compared;
                }
    }
    std::printf("MOTION_REPEAT_REGION seeds=%u rgba_components=%llu differing_bits=%llu history_transitions=%u\n",
                AuditSeeds, (unsigned long long)Compared, (unsigned long long)Different, AuditSeeds * 3);
    Require(Different == 0, "Restricted probe results differ from the complete dispatch");
    const auto RegionGuides = Restricted.PrimaryReceivers();
    uint32_t RegionHits = 0;
    for (const auto& Receiver : RegionGuides) RegionHits += Receiver.PositionHit[3] != 0 ? 1u : 0u;
    std::printf("MOTION_REPEAT_REGION active_hits=%u full_pixels=%u\n", RegionHits, Width * Height);
    Require(RegionHits > 10 && RegionHits <= 2 * 13 * 13, "Restricted primary region has an unexpected extent");
    const auto TableDirectory = std::filesystem::path(TablePath).parent_path();
    if (!TableDirectory.empty()) std::filesystem::create_directories(TableDirectory);
    std::ofstream Table(TablePath);
    Table.exceptions(std::ios::failbit | std::ios::badbit);
    Table << "group,batch,seed";
    for (uint32_t Mode = 0; Mode < RepeatModeCount; ++Mode)
        for (uint32_t Probe = 0; Probe < RepeatProbeCount; ++Probe)
            for (const char* Channel : {"red", "green", "blue"})
                Table << ',' << RepeatModeNames[Mode] << '_' << (Probe == 0 ? "rigid" : "floor") << '_' << Channel;
    Table << '\n' << std::setprecision(std::numeric_limits<float>::max_digits10);
    ReplayRepeatMoments Aggregate{};
    std::vector<ReplayRepeatMoments> GroupMoments(Groups);
    std::vector<ReplayMotionRepeatFootprint> Samples;
    Samples.reserve(Seeds.size());
    std::array<uint64_t, 3> Temporal{}, Spatial{};
    for (uint32_t Group = 0; Group < Groups; ++Group)
    {
        for (uint32_t Batch = 0; Batch < BatchesPerGroup; ++Batch)
        {
            const uint32_t Seed = Seeds[size_t(Group) * BatchesPerGroup + Batch];
            const auto Sample = TraceReplayMotionRepeat(Restricted, Previous, Current, Covered, Reopened, Pixels, Seed, true);
            Samples.push_back(Sample);
            Table << Group << ',' << Batch << ',' << Seed;
            for (uint32_t Mode = 0; Mode < RepeatModeCount; ++Mode)
                for (uint32_t Probe = 0; Probe < RepeatProbeCount; ++Probe)
                    for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    {
                        const float Radiance = Sample.Radiance[Mode][Probe][Channel];
                        Aggregate[Mode][Probe][Channel].Accumulate(Radiance);
                        GroupMoments[Group][Mode][Probe][Channel].Accumulate(Radiance);
                        Table << ',' << Radiance;
                    }
            Table << '\n';
            for (uint32_t Transition = 0; Transition < 3; ++Transition)
            {
                Temporal[Transition] += Sample.Temporal[Transition];
                Spatial[Transition] += Sample.Spatial[Transition];
            }
        }
        const auto& Uniform = GroupMoments[Group][0][0][1];
        const auto& Mixture = GroupMoments[Group][1][0][1];
        std::printf("MOTION_REPEAT_GROUP group=%u batches=%u rigid_green_uniform=%.9g uniform_se=%.9g "
                    "mixture=%.9g mixture_se=%.9g sd_ratio=%.9g\n", Group, BatchesPerGroup,
                    Uniform.Mean(), ReplaySampleStandardError(Uniform), Mixture.Mean(),
                    ReplaySampleStandardError(Mixture), Mixture.Deviation() / Uniform.Deviation());
        std::fflush(stdout);
    }
    Table.close();
    constexpr std::array<uint32_t, RepeatModeCount> ReferenceIndices{0, 0, 1, 2, 0, 1, 2};
    for (uint32_t Mode = 0; Mode < RepeatModeCount; ++Mode)
        for (uint32_t Probe = 0; Probe < RepeatProbeCount; ++Probe)
        {
            const std::string Name = std::string("repeat_") + RepeatModeNames[Mode] + (Probe == 0 ? "_rigid" : "_floor");
            CompareReplayMotionProbe(Name.c_str(), Cpu[ReferenceIndices[Mode]][Probe], Aggregate[Mode][Probe]);
        }
    ReportReplayMotionDispersion(Samples, GroupMoments, Aggregate, 1, 0, "mixture_over_uniform");
    ReportReplayMotionDispersion(Samples, GroupMoments, Aggregate, 4, 1, "moved_reuse_over_fresh");
    ReportReplayMotionDispersion(Samples, GroupMoments, Aggregate, 5, 2, "blocked_reuse_over_fresh");
    ReportReplayMotionDispersion(Samples, GroupMoments, Aggregate, 6, 3, "reopened_reuse_over_fresh");
    std::printf("MOTION_REPEAT_LINKS temporal=%llu/%llu/%llu spatial=%llu/%llu/%llu seed_groups=%u "
                "batches_per_group=%u distinct_seeds=%zu original_overlap=0\n",
                (unsigned long long)Temporal[0], (unsigned long long)Temporal[1], (unsigned long long)Temporal[2],
                (unsigned long long)Spatial[0], (unsigned long long)Spatial[1], (unsigned long long)Spatial[2],
                Groups, BatchesPerGroup, Seeds.size());
    std::printf("PASS disjoint moving-primary repeat with paired fresh and reused comparisons\n");
}
} // namespace
