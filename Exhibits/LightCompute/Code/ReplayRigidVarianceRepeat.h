/*==========================================================================================================================================
                                            REPLAYRIGIDVARIANCEREPEAT.H
==========================================================================================================================================*/
// 🧩 Investigate the rigid red-channel discrepancy with disjoint primary-ray and spatial controls.
#pragma once

namespace
{

constexpr uint32_t RigidRepeatModeCount = 4;
constexpr std::array<const char*, RigidRepeatModeCount> RigidRepeatModeNames{
    "uniform_single", "uniform_spatial", "mixture_single", "mixture_spatial"};

std::vector<uint32_t> CompileReplayRigidRepeatSeeds(const std::string& PreviousTable, uint32_t Count)
{
    std::ifstream Input(PreviousTable);
    Require(bool(Input), "Previous moving-repeat sample table is unavailable");
    std::string Line;
    Require(bool(std::getline(Input, Line)) && Line.rfind("group,batch,seed,", 0) == 0,
            "Previous moving-repeat sample table has an unexpected header");
    constexpr std::array<uint32_t, 7> Offsets{0, 10, 11, 12, 20, 21, 22};
    std::set<uint32_t> Reserved, PriorSeeds, Used;
    for (uint32_t Batch = 0; Batch < 96; ++Batch)
        for (uint32_t Offset : Offsets) Reserved.insert(944001 + 103 * Batch + Offset);
    while (std::getline(Input, Line))
    {
        if (Line.empty()) continue;
        const size_t First = Line.find(','), Second = Line.find(',', First + 1), Third = Line.find(',', Second + 1);
        Require(First != std::string::npos && Second != std::string::npos && Third != std::string::npos,
                "Previous moving-repeat sample table contains an incomplete row");
        const uint64_t Parsed = std::stoull(Line.substr(Second + 1, Third - Second - 1));
        Require(Parsed <= UINT32_MAX - 22 && PriorSeeds.insert(uint32_t(Parsed)).second,
                "Previous moving-repeat seeds are invalid or duplicated");
        for (uint32_t Offset : Offsets) Reserved.insert(uint32_t(Parsed) + Offset);
    }
    Require(PriorSeeds.size() == 256, "Previous moving-repeat sample table must contain 256 seeds");
    std::mt19937 Random(0xb97c25a1u);
    std::vector<uint32_t> Seeds;
    Seeds.reserve(Count);
    while (Seeds.size() < Count)
    {
        const uint32_t Seed = Random();
        if (Reserved.count(Seed) || !Used.insert(Seed).second) continue;
        Seeds.push_back(Seed);
    }
    return Seeds;
}

std::vector<std::array<float, 4>> TraceReplayRigidVariance(LightComputeSubmission& Compute,
                                                            const LightComputeArchive& Scene,
                                                            uint32_t Seed, uint32_t Mode, bool Restrict = false)
{
    Require(Mode < RigidRepeatModeCount, "Unknown rigid-repeat proposal");
    const bool Spatial = (Mode & 1u) != 0, Mixture = Mode >= 2;
    return Compute.ExecuteReplay(Scene, 4, Seed, false, Spatial, true, {.025f, .025f, .025f}, .01f,
                                 true, false, true, true, false, false, 64, true, true, true,
                                 Mixture, Mixture, Restrict);
}

void ValidateReplayRigidVarianceRepeat(const char* Driver, const std::string& CompleteShaders,
                                       const std::string& RestrictedShaders, const std::string& PreviousTable,
                                       const std::string& OutputTable, uint32_t Groups = 8,
                                       uint32_t BatchesPerGroup = 256, uint32_t CpuPathsPerGroup = 500000)
{
    Require(Groups >= 2 && BatchesPerGroup >= 8 && CpuPathsPerGroup >= 10000,
            "Rigid repeat requires disjoint groups and separately seeded CPU paths");
    constexpr uint32_t Width = 64, Height = 64, Pixel = 38 * Width + 28;
    constexpr std::array<uint32_t, 5> Region{Pixel, Pixel - 1, Pixel + 1, Pixel - Width, Pixel + Width};
    auto Scene = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    SetRoughTextureReplayMaterials(Scene, .48f, .7f);
    TranslateReplayRadiantFacets(Scene, .18f);
    SetReplayCaptureEmitterPowers(Scene, true);
    LightComputeSubmission Complete(Driver), Restricted(Driver);
    std::printf("RIGID_REPEAT preparing Vulkan on %s\n", Complete.Properties.deviceName);
    std::fflush(stdout);
    Complete.Prepare(Scene, CompleteShaders, false, false, false, false, true);
    Restricted.Prepare(Scene, RestrictedShaders, false, false, false, false, true);
    (void)TraceReplayRigidVariance(Complete, Scene, 0x70100321u, 0);
    const auto CompleteGuides = Complete.PrimaryReceivers();
    const auto Reference = ValidateReplayPrimaryReference(Scene, CompleteGuides, "rigid_repeat_moved");
    ValidateRoughMotionCoefficients(Scene, Reference, CompleteGuides, "rigid_repeat_moved");
    Require(Reference[Pixel].Hit && Reference[Pixel].Receiver.MotionPositionToken[3] == 8 &&
                !Reference[Pixel].NearMipBoundary,
            "Rigid repeat selected a different or unstable primary surface");
    const auto Seeds = CompileReplayRigidRepeatSeeds(PreviousTable, Groups * BatchesPerGroup);
    constexpr uint32_t AuditSeeds = 8;
    uint64_t Components = 0, Different = 0, GuideComponents = 0, GuideDifferences = 0;
    for (uint32_t Audit = 0; Audit < AuditSeeds; ++Audit)
        for (uint32_t Mode = 0; Mode < RigidRepeatModeCount; ++Mode)
        {
            const auto Full = TraceReplayRigidVariance(Complete, Scene, Seeds[Audit], Mode);
            const auto FullGuides = Complete.PrimaryReceivers();
            const auto Sparse = TraceReplayRigidVariance(Restricted, Scene, Seeds[Audit], Mode, true);
            const auto SparseGuides = Restricted.PrimaryReceivers();
            const auto Counts = Restricted.ExtendedStatistics();
            Require(Restricted.ReplayLastReset && Counts[22] == ((Mode & 1u) ? 8u : 0u),
                    "Restricted receivers did not exercise the expected spatial donors");
            uint32_t Hits = 0;
            for (const auto& Receiver : SparseGuides) Hits += Receiver.PositionHit[3] != 0 ? 1u : 0u;
            Require(Hits == Region.size(), "Restricted region did not retain exactly five receiver hits");
            for (uint32_t Receiver : Region)
            {
                const auto* A = reinterpret_cast<const char*>(&FullGuides[Receiver]);
                const auto* B = reinterpret_cast<const char*>(&SparseGuides[Receiver]);
                for (uint32_t Field = 0; Field < sizeof(LightComputeReceiver) / sizeof(uint32_t); ++Field)
                {
                    uint32_t Left = 0, Right = 0;
                    std::memcpy(&Left, A + Field * sizeof(uint32_t), sizeof(Left));
                    std::memcpy(&Right, B + Field * sizeof(uint32_t), sizeof(Right));
                    GuideDifferences += Left != Right ? 1u : 0u;
                    ++GuideComponents;
                }
            }
            for (uint32_t Channel = 0; Channel < 4; ++Channel)
            {
                Different += std::memcmp(&Full[Pixel][Channel], &Sparse[Pixel][Channel], sizeof(float)) != 0;
                ++Components;
            }
        }
    std::printf("RIGID_REPEAT_REGION seeds=%u modes=%u output_components=%llu output_mismatches=%llu "
                "guide_components=%llu guide_mismatches=%llu retained_hits=5\n",
                AuditSeeds, RigidRepeatModeCount, (unsigned long long)Components, (unsigned long long)Different,
                (unsigned long long)GuideComponents, (unsigned long long)GuideDifferences);
    Require(Different == 0 && GuideDifferences == 0,
            "Restricted receiver guides or selected output differ from the complete shader");
    ReplayRgbMoments Cpu{};
    std::printf("RIGID_REPEAT tracing %u separately seeded CPU groups of %u paths\n", 4u, CpuPathsPerGroup);
    std::fflush(stdout);
    for (uint32_t Group = 0; Group < 4; ++Group)
    {
        const auto Current = TraceRoughMotionReference(Scene, Reference[Pixel], 0x954a7001ULL + Group,
                                                        CpuPathsPerGroup);
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            Cpu[Channel].Sum += Current[Channel].Sum;
            Cpu[Channel].Squares += Current[Channel].Squares;
            Cpu[Channel].Count += Current[Channel].Count;
        }
        std::printf("RIGID_REPEAT_CPU_GROUP group=%u red=%.9g red_se=%.9g paths=%u\n", Group,
                    Current[0].Mean(), ReplaySampleStandardError(Current[0]), CpuPathsPerGroup);
        std::fflush(stdout);
    }
    const auto Directory = std::filesystem::path(OutputTable).parent_path();
    if (!Directory.empty()) std::filesystem::create_directories(Directory);
    std::ofstream Table(OutputTable);
    Table.exceptions(std::ios::failbit | std::ios::badbit);
    Table << "group,batch,seed";
    for (const auto* Name : RigidRepeatModeNames)
        for (const auto* Channel : {"red", "green", "blue"}) Table << ',' << Name << '_' << Channel;
    Table << '\n' << std::setprecision(std::numeric_limits<float>::max_digits10);
    std::array<ReplayRgbMoments, RigidRepeatModeCount> Aggregate{};
    std::array<ReplayRgbMoments, 2> SpatialDifference{}, ProposalDifference{};
    for (uint32_t Group = 0; Group < Groups; ++Group)
    {
        std::array<ReplayRgbMoments, RigidRepeatModeCount> GroupMoments{};
        for (uint32_t Batch = 0; Batch < BatchesPerGroup; ++Batch)
        {
            const uint32_t Seed = Seeds[size_t(Group) * BatchesPerGroup + Batch];
            std::array<std::array<float, 4>, RigidRepeatModeCount> Radiance{};
            Table << Group << ',' << Batch << ',' << Seed;
            for (uint32_t Mode = 0; Mode < RigidRepeatModeCount; ++Mode)
            {
                const auto Image = TraceReplayRigidVariance(Restricted, Scene, Seed, Mode, true);
                Radiance[Mode] = Image[Pixel];
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                {
                    const float Component = Radiance[Mode][Channel];
                    Require(std::isfinite(Component) && Component >= 0, "Rigid repeat produced invalid radiance");
                    GroupMoments[Mode][Channel].Accumulate(Component);
                    Aggregate[Mode][Channel].Accumulate(Component);
                    Table << ',' << Component;
                }
            }
            Table << '\n';
            for (uint32_t Choice = 0; Choice < 2; ++Choice)
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                {
                    SpatialDifference[Choice][Channel].Accumulate(double(Radiance[Choice * 2 + 1][Channel]) -
                                                                   Radiance[Choice * 2][Channel]);
                    ProposalDifference[Choice][Channel].Accumulate(double(Radiance[Choice + 2][Channel]) -
                                                                    Radiance[Choice][Channel]);
                }
        }
        for (uint32_t Mode = 0; Mode < RigidRepeatModeCount; ++Mode)
        {
            const auto& Red = GroupMoments[Mode][0];
            std::printf("RIGID_REPEAT_GROUP group=%u mode=%s red=%.9g red_se=%.9g batches=%u\n",
                        Group, RigidRepeatModeNames[Mode], Red.Mean(), ReplaySampleStandardError(Red), BatchesPerGroup);
        }
        std::fflush(stdout);
    }
    Table.close();
    for (uint32_t Mode = 0; Mode < RigidRepeatModeCount; ++Mode)
    {
        const std::string Label = std::string("rigid_repeat_") + RigidRepeatModeNames[Mode];
        CompareReplayMotionProbe(Label.c_str(), Cpu, Aggregate[Mode]);
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
            std::printf("RIGID_REPEAT_DISPERSION mode=%s channel=%u mean=%.9g batch_se=%.9g batch_sd=%.9g n=%u\n",
                        RigidRepeatModeNames[Mode], Channel, Aggregate[Mode][Channel].Mean(),
                        ReplaySampleStandardError(Aggregate[Mode][Channel]),
                        ReplaySampleStandardError(Aggregate[Mode][Channel]) * std::sqrt(double(Aggregate[Mode][Channel].Count)),
                        Aggregate[Mode][Channel].Count);
    }
    for (uint32_t Proposal = 0; Proposal < 2; ++Proposal)
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const auto& Difference = SpatialDifference[Proposal][Channel];
            std::printf("RIGID_REPEAT_SPATIAL_DIFFERENCE proposal=%s channel=%u mean=%.9g paired_se=%.9g n=%u\n",
                        Proposal == 0 ? "uniform" : "mixture", Channel, Difference.Mean(),
                        ReplaySampleStandardError(Difference), Difference.Count);
        }
    for (uint32_t Spatial = 0; Spatial < 2; ++Spatial)
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const auto& Difference = ProposalDifference[Spatial][Channel];
            std::printf("RIGID_REPEAT_PROPOSAL_DIFFERENCE spatial=%u channel=%u mean=%.9g paired_se=%.9g n=%u\n",
                        Spatial, Channel, Difference.Mean(), ReplaySampleStandardError(Difference), Difference.Count);
        }
    std::printf("RIGID_REPEAT distinct_frame_seeds=%zu prior_repeat_overlap=0 original_overlap=0 "
                "cpu_paths=%u groups=%u batches_per_group=%u\n", Seeds.size(), 4 * CpuPathsPerGroup,
                Groups, BatchesPerGroup);
    std::printf("PASS disjoint rigid-primary repeat with spatial on/off controls\n");
}
} // namespace
