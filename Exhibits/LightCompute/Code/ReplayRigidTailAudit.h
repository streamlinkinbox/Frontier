/*==========================================================================================================================================
                                              REPLAYRIGIDTAILAUDIT.H
==========================================================================================================================================*/
// 🧩 Inspect selected high-tail samples and reconstruct their spatial reservoir on the host.
#pragma once

namespace
{

struct ReplayRigidTailSample
{
    uint32_t Group = 0, Batch = 0, Seed = 0;
    std::array<std::array<float, 3>, 2> Spatial{};
};

std::vector<ReplayRigidTailSample> LoadReplayRigidTailSamples(const std::string& TablePath)
{
    std::ifstream Input(TablePath);
    Require(bool(Input), "Rigid tail sample table is unavailable");
    std::string Line;
    Require(bool(std::getline(Input, Line)) && Line ==
                "group,batch,seed,uniform_single_red,uniform_single_green,uniform_single_blue,"
                "uniform_spatial_red,uniform_spatial_green,uniform_spatial_blue,"
                "mixture_single_red,mixture_single_green,mixture_single_blue,"
                "mixture_spatial_red,mixture_spatial_green,mixture_spatial_blue",
            "Rigid tail sample table has an unexpected header");
    std::vector<ReplayRigidTailSample> Samples;
    std::set<uint32_t> Seeds;
    while (std::getline(Input, Line))
    {
        if (Line.empty()) continue;
        std::stringstream Row(Line);
        std::array<std::string, 15> Cells{};
        for (auto& Cell : Cells) Require(bool(std::getline(Row, Cell, ',')), "Incomplete rigid tail sample row");
        Require(Row.peek() == std::char_traits<char>::eof(), "Unexpected extra rigid tail sample columns");
        ReplayRigidTailSample Sample;
        const auto Group = std::stoull(Cells[0]), Batch = std::stoull(Cells[1]), Seed = std::stoull(Cells[2]);
        Require(Group <= UINT32_MAX && Batch <= UINT32_MAX && Seed <= UINT32_MAX &&
                    Seeds.insert(uint32_t(Seed)).second, "Invalid or duplicate rigid tail seed");
        Sample.Group = uint32_t(Group); Sample.Batch = uint32_t(Batch); Sample.Seed = uint32_t(Seed);
        for (uint32_t Mode = 0; Mode < 2; ++Mode)
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                const float Value = std::stof(Cells[(Mode == 0 ? 6 : 12) + Channel]);
                Require(std::isfinite(Value) && Value >= 0, "Rigid tail table contains invalid radiance");
                Sample.Spatial[Mode][Channel] = Value;
            }
        Samples.push_back(Sample);
    }
    Require(Samples.size() == 2048, "Rigid tail audit expects the completed 2,048-seed table");
    std::sort(Samples.begin(), Samples.end(), [](const auto& A, const auto& B)
              { return A.Spatial[0][0] > B.Spatial[0][0]; });
    return Samples;
}

uint32_t NextReplayTailRandom(uint32_t& Bits)
{
    Bits = Bits * 747796405u + 2891336453u;
    const uint32_t Word = ((Bits >> ((Bits >> 28u) + 4u)) ^ Bits) * 277803737u;
    return (Word >> 22u) ^ Word;
}

std::array<float, 4> ReplayTailSlot(const LightComputeSubmission& Compute, uint32_t Slot, uint32_t Pixel)
{
    const size_t N = Compute.ReceiverCount;
    Require(Slot <= 6 && Pixel < N, "Rigid tail audit selected an invalid history slot");
    const size_t Index = (25 + size_t(Slot)) * N + 13889 + SolarSourceCount + Pixel;
    Require((Index + 1) * 16 <= Compute.Buffers[6].Size, "Rigid tail history slot exceeds output storage");
    std::array<float, 4> Value{};
    std::memcpy(Value.data(), static_cast<const char*>(Compute.Buffers[6].Address) + Index * 16, 16);
    return Value;
}

void ValidateReplayRigidTailAudit(const char* Driver, const std::string& Shaders, const std::string& TablePath)
{
    constexpr uint32_t Width = 64, Height = 64, Pixel = 38 * Width + 28;
    constexpr std::array<uint32_t, 5> Donors{Pixel, Pixel - 1, Pixel + 1, Pixel - Width, Pixel + Width};
    auto Samples = LoadReplayRigidTailSamples(TablePath);
    auto Scene = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, Width, Height);
    SetRoughTextureReplayMaterials(Scene, .48f, .7f);
    TranslateReplayRadiantFacets(Scene, .18f);
    SetReplayCaptureEmitterPowers(Scene, true);
    const auto Distribution = CompileRadiantFacetDistribution(Scene.Surfaces);
    for (uint32_t Source = 0; Source < uint32_t(Distribution[0]); ++Source)
    {
        const size_t Entry = 1 + Source * 4;
        const uint32_t Facet = uint32_t(Distribution[Entry]);
        const auto& Emission = Scene.Surfaces[Facet].Emissive;
        const float Probability = Distribution[Entry + 1];
        std::printf("RIGID_TAIL_SOURCE facet=%u emission=%.9g/%.9g/%.9g uniform=%.9g power=%.9g mixture=%.9g\n",
                    Facet, Emission[0], Emission[1], Emission[2], 1.f / Distribution[0], Probability,
                    .5f / Distribution[0] + .5f * Probability);
    }
    LightComputeSubmission Compute(Driver);
    std::printf("RIGID_TAIL_AUDIT device=%s selected_top_uniform=3 selected_median=1\n", Compute.Properties.deviceName);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    Require(Compute.ReplayTailAuditPipeline && Compute.ReplayProbeRegionPipeline, "Rigid tail audit shaders missing");
    const std::array<size_t, 4> Selected{0, 1, 2, Samples.size() / 2};
    double MaximumReconstructionError = 0, MaximumBreakdownError = 0;
    for (size_t Rank : Selected)
        for (uint32_t Mode = 0; Mode < 2; ++Mode)
        {
            const auto& Sample = Samples[Rank];
            auto Trace = [&](bool Audit)
            {
                return Compute.ExecuteReplay(Scene, 4, Sample.Seed, false, true, true, {.025f, .025f, .025f},
                                             .01f, true, false, true, true, false, false, 64, true, true, true,
                                             Mode != 0, Mode != 0, true, Audit);
            };
            const auto Ordinary = Trace(false);
            const auto Audited = Trace(true);
            for (uint32_t Channel = 0; Channel < 4; ++Channel)
                Require(std::memcmp(&Ordinary[Pixel][Channel], &Audited[Pixel][Channel], sizeof(float)) == 0,
                        "Tail diagnostic changed the selected output");
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
                Require(std::memcmp(&Audited[Pixel][Channel], &Sample.Spatial[Mode][Channel], sizeof(float)) == 0,
                        "Tail diagnostic did not reproduce the recorded sample");
            const auto Guides = Compute.PrimaryReceivers();
            uint32_t SelectionBits = (Sample.Seed * 2891336453u + 1013904223u) ^ (Pixel * 277803737u + 13u);
            float Sum = 0, ChosenTarget = 0;
            uint32_t Chosen = UINT32_MAX;
            std::array<float, 4> ChosenRadiance{};
            std::printf("RIGID_TAIL_CASE rank=%zu group=%u batch=%u frame_seed=%u mode=%s output_red=%.9g\n",
                        Rank + 1, Sample.Group, Sample.Batch, Sample.Seed, Mode == 0 ? "uniform" : "mixture",
                        Audited[Pixel][0]);
            for (uint32_t Index = 0; Index < 5; ++Index)
            {
                const auto AtCentre = ReplayTailSlot(Compute, 5, Pixel + Index);
                const auto Audit = ReplayTailSlot(Compute, 6, Pixel + Index);
                const auto Original = ReplayTailSlot(Compute, 3, Donors[Index]);
                const auto Source = ReplayTailSlot(Compute, 2, Donors[Index]);
                Require(Source[3] == 1 && Source[2] > 0 && Audit[0] >= 0 && AtCentre[3] > 0 &&
                            std::isfinite(Audit[0]) && std::abs(Audit[0] - AtCentre[3] * Source[2]) < 1e-5,
                        "Rigid tail candidate has an invalid fresh reservoir weight");
                const uint32_t Candidate = uint32_t(Audit[2]) | (uint32_t(Audit[3]) << 16u);
                const float Random = float(NextReplayTailRandom(SelectionBits) >> 8) * (1.f / 16777216.f);
                Sum += Audit[0];
                const bool Select = Random * Sum < Audit[0];
                if (Select) { Chosen = Index; ChosenTarget = AtCentre[3]; ChosenRadiance = AtCentre; }
                std::printf("RIGID_TAIL_DONOR index=%u candidate_seed=%u donor_red=%.9g centre_red=%.9g "
                            "centre_green=%.9g target=%.9g source_weight=%.9g weight=%.9g draw=%.9g selected=%u\n",
                            Index, Candidate, Original[0], AtCentre[0], AtCentre[1], AtCentre[3], Source[2],
                            Audit[0], Random, Select ? 1u : 0u);
            }
            Require(Chosen < 5 && ChosenTarget > 0, "Rigid tail reservoir did not select a valid candidate");
            const auto Detail = ReplayTailSlot(Compute, 5, Pixel + 20);
            Require(Detail[3] == float(Chosen), "GPU and host selected different tail candidates");
            std::array<double, 3> Terms{};
            for (uint32_t Bounce = 0; Bounce < 4; ++Bounce)
            {
                const auto Direct = ReplayTailSlot(Compute, 5, Pixel + 16 + Bounce);
                const auto Hit = ReplayTailSlot(Compute, 6, Pixel + 16 + Bounce);
                for (uint32_t Channel = 0; Channel < 3; ++Channel) Terms[Channel] += Direct[Channel] + Hit[Channel];
                std::printf("RIGID_TAIL_BOUNCE bounce=%u state=%.0f facet=%.0f direct_red=%.9g hit_red=%.9g "
                            "direct_green=%.9g hit_green=%.9g\n", Bounce, Direct[3], Hit[3], Direct[0], Hit[0],
                            Direct[1], Hit[1]);
            }
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                const double Error = std::max(std::abs(double(Detail[Channel]) - ChosenRadiance[Channel]),
                                              std::abs(Terms[Channel] - ChosenRadiance[Channel]));
                MaximumBreakdownError = std::max(MaximumBreakdownError, Error);
                Require(Error < 1e-7 + 2e-5 * std::abs(ChosenRadiance[Channel]),
                        "Mirrored bounce breakdown disagrees with the Engine path");
            }
            const float Scale = Sum / (5.f * ChosenTarget);
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                const float Reconstructed = ChosenRadiance[Channel] * Scale + Guides[Pixel].ResidualSpecular[Channel];
                const double Error = std::abs(double(Reconstructed) - Audited[Pixel][Channel]);
                MaximumReconstructionError = std::max(MaximumReconstructionError, Error);
                Require(Error < 1e-7 + 2e-5 * std::abs(Audited[Pixel][Channel]),
                        "Host reservoir reconstruction disagrees with Vulkan output");
            }
            std::printf("RIGID_TAIL_RECONSTRUCT chosen=%u centre_red=%.9g sum_weight=%.9g scale=%.9g "
                        "output_red=%.9g\n", Chosen, ChosenRadiance[0], Sum, Scale, Audited[Pixel][0]);
        }
    std::printf("RIGID_TAIL_AUDIT cases=8 selected_output_parity_components=32 host_max_abs_error=%.9g "
                "bounce_max_abs_error=%.9g\n", MaximumReconstructionError, MaximumBreakdownError);
    std::printf("PASS recorded rare-sample replay and host spatial-reservoir reconstruction\n");
}
} // namespace
