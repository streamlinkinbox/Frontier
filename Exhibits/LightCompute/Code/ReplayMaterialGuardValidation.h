/*==========================================================================================================================================
                                          REPLAYMATERIALGUARDVALIDATION.H
==========================================================================================================================================*/
// 🧩 Paired holdout of a test-only material-boundary guard, with same-material visibility controls.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                           LINK AND IMAGE DIAGNOSTICS
//------------------------------------------------------------------------------------------------------------------------

void InspectReplayMaterialGuardLinks(LightComputeSubmission& Compute, const std::string& Shaders, uint32_t Size,
                                     uint32_t Seed, uint32_t ResolveLinks, bool Mixed, bool Candidate)
{
    VkPipeline Audit = Compute.ConstructPipeline(Shaders + "/ReplayGuardLinks.spv");
    ComputeExtent Extent{};
    Extent.Dimensions[0] = Size * Size;
    Extent.Sampling[0] = Seed;
    Extent.History = {Size, Size, 0, 0};
    Extent.Primary[3] = PathReplayRoughOpaque | PathReplayImportanceRough | PathReplaySpatialDisk |
                        PathReplayOrderedTraversal | PathReplayAuthoredMaterials;
    Confirm(Compute.ResetCommandPool(Compute.Device, Compute.Pool, 0));
    Confirm(Compute.ResetFences(Compute.Device, 1, &Compute.Completion));
    VkCommandBufferBeginInfo Begin{};
    Begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Confirm(Compute.BeginCommandBuffer(Compute.Commands, &Begin));
    VkMemoryBarrier Barrier{};
    Barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    Compute.CmdPipelineBarrier(Compute.Commands, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &Barrier, 0, nullptr, 0, nullptr);
    Compute.CmdBindDescriptorSets(Compute.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Compute.PipelineLayout, 0, 1,
                                  &Compute.DescriptorSet, 0, nullptr);
    Compute.CmdPushConstants(Compute.Commands, Compute.PipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                             0, sizeof(Extent), &Extent);
    Compute.CmdBindPipeline(Compute.Commands, VK_PIPELINE_BIND_POINT_COMPUTE, Audit);
    Compute.CmdDispatch(Compute.Commands, (Extent.Dimensions[0] + 63u) / 64u, 1, 1);
    Barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    Compute.CmdPipelineBarrier(Compute.Commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                               0, 1, &Barrier, 0, nullptr, 0, nullptr);
    Confirm(Compute.EndCommandBuffer(Compute.Commands));
    VkSubmitInfo Submission{};
    Submission.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submission.commandBufferCount = 1;
    Submission.pCommandBuffers = &Compute.Commands;
    Confirm(Compute.QueueSubmit(Compute.Queue, 1, &Submission, Compute.Completion));
    Confirm(Compute.WaitForFences(Compute.Device, 1, &Compute.Completion, VK_TRUE, 600000000000ull));
    std::vector<std::array<float, 4>> Pixels(Extent.Dimensions[0]);
    std::memcpy(Pixels.data(), Compute.Buffers[6].Address, Pixels.size() * sizeof(Pixels[0]));
    Compute.DestroyPipeline(Compute.Device, Audit, nullptr);
    std::array<uint32_t, 4> Counts{};
    for (const auto& Pixel : Pixels)
        for (uint32_t C = 0; C < Counts.size(); ++C)
        {
            Require(std::isfinite(Pixel[C]) && Pixel[C] >= 0 && Pixel[C] <= 4 && std::floor(Pixel[C]) == Pixel[C],
                    "Material-guard link audit returned an invalid count");
            Counts[C] += uint32_t(Pixel[C]);
        }
    std::printf("REPLAY_GUARD_LINKS arm=%s scene=%s seed=%u eligible=%u cross_material=%u cross_position=%u "
                "rejected=%u resolve_links=%u\n", Candidate ? "guard" : "baseline", Mixed ? "mixed" : "visibility",
                Seed, Counts[0], Counts[1], Counts[2], Counts[3], ResolveLinks);
    std::fflush(stdout);
    Require(Counts[0] > 0 && Counts[0] == ResolveLinks, "Material-guard probe did not match Resolve links");
    if (Mixed)
    {
        Require(Counts[1] == Counts[2] && ((Counts[1] == 0) == Candidate),
                "Mixed-material candidate did not enforce its specified address boundary");
    }
    else
        Require(Counts[1] == 0 && Counts[2] > 0,
                "Same-material visibility control did not retain different-position links");
}

uint64_t ReplayMaterialImageHash(const std::vector<std::array<float, 4>>& Image, uint32_t Size, uint32_t Group)
{
    uint64_t Hash = 14695981039346656037ull;
    for (uint32_t Y = 0; Y < Size; ++Y)
        for (uint32_t X = 0; X < Size; ++X)
            if ((X + Y) % 2u == Group)
                for (float Value : Image[size_t(Y) * Size + X])
                {
                    uint32_t Bits = 0;
                    std::memcpy(&Bits, &Value, sizeof(Bits));
                    Hash = (Hash ^ Bits) * 1099511628211ull;
                }
    return Hash;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         PAIRED HOLDOUT MEASUREMENTS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayMaterialGuard(const char* Driver, const std::string& Shaders, bool Candidate)
{
    constexpr uint32_t Size = 32u, Batches = 48u, Frames = 4u;
    const std::array<ReplayMaterialReuseMode, 2> Modes{{
        {"fresh1", 1u, false, false}, {"temporal_disk", 1u, true, true}}};
    constexpr std::array<float, 2> Floors{.01f, .0001f};
    constexpr std::array<const char*, 2> FloorNames{"default", "lower"};
    constexpr std::array<const char*, 2> SceneNames{"mixed", "visibility"};
    const char* Arm = Candidate ? "guard" : "baseline";
    auto DimSpecification = EnumerateReplayMaterialSpecifications()[4];
    DimSpecification.Coverage = 1e-3f;
    DimSpecification.CoatCoverage = 2e-3f;
    DimSpecification.ReportingScale = 1e3;
    const auto Dim = ConstructReplayMaterialReuseFixture(Size, DimSpecification);
    const auto Metal = ConstructReplayMaterialReuseFixture(Size, EnumerateReplayMaterialSpecifications()[1]);
    const auto Capacity = ConstructReplayMixedMaterialFixture(Size, Dim, Metal);
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_GUARD arm=%s preparing production pipelines on %s\n", Arm, Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    std::printf("REPLAY_GUARD arm=%s pipelines_ready=1\n", Arm);
    std::fflush(stdout);
    bool WithinReference = true;
    for (uint32_t SceneIndex = 0; SceneIndex < SceneNames.size(); ++SceneIndex)
    {
        const bool Mixed = SceneIndex == 0;
        auto Scene = Mixed ? Capacity : ConstructReplayMixedMaterialFixture(Size, Metal, Metal);
        if (!Mixed)
            for (auto& Receiver : Scene.Receivers) Receiver.ExpectedDirect[3] = 1.0f;
        const std::array<const char*, 2> GroupNames = Mixed
            ? std::array<const char*, 2>{"dim", "metal"} : std::array<const char*, 2>{"near", "far"};
        const std::array<const LightComputeArchive*, 2> Materials{Mixed ? &Dim : &Metal, &Metal};
        const std::array<double, 2> Scales{Mixed ? 1e3 : 1.0, 1.0};
        const std::array<double, 2> PositionX{-.75, .75};
        std::array<double, 2> References{};
        for (uint32_t Group = 0; Group < 2u; ++Group)
        {
            References[Group] = IntegrateReplayMixedVisibleReference(*Materials[Group], PositionX[Group], 1024u) * Scales[Group];
            const double Coarse = IntegrateReplayMixedVisibleReference(*Materials[Group], PositionX[Group], 512u) * Scales[Group];
            Require(std::isfinite(References[Group]) && References[Group] > 0 &&
                        std::abs(References[Group] - Coarse) < .0002,
                    "Material-guard hemisphere and area quadratures did not converge");
            std::printf("REPLAY_GUARD_REFERENCE arm=%s scene=%s group=%s reference=%.9g coarse=%.9g scale=%.3g\n",
                        Arm, SceneNames[SceneIndex], GroupNames[Group], References[Group], Coarse, Scales[Group]);
        }
        std::array<ReplayMixedMaterialMeasurements, Modes.size() * Floors.size()> Results{};
        std::mt19937_64 Random(0x14cd5b20b771ull + uint64_t(SceneIndex) * 0x1000193ull);
        uint32_t LastSeed = 0, LastResolveLinks = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            std::array<uint32_t, Frames> Seeds{};
            for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
            for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
                for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
                {
                    const auto& Mode = Modes[ModeIndex];
                    auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
                    std::vector<std::array<float, 4>> Image;
                    const uint32_t FirstFrame = Mode.Temporal ? 0u : Frames - 1u;
                    for (uint32_t Frame = FirstFrame; Frame < Frames; ++Frame)
                    {
                        Image = ExecuteReplayMaterialFloor(Compute, Scene, Mode, Seeds[Frame], Frame == FirstFrame,
                                                           Floors[FloorIndex]);
                        const auto Counts = Compute.ExtendedStatistics();
                        if (Frame + 1u < Frames) Measurement.WarmRays += Counts[8];
                        else
                        {
                            Measurement.FinalRays += Counts[8];
                            Measurement.BoxTests += Counts[18];
                            Measurement.TriangleTests += Counts[19];
                            Measurement.TemporalAccepted += Counts[20];
                            Measurement.SpatialLinks += Counts[22];
                            Require((Counts[20] > 0) == Mode.Temporal, "Material-guard temporal reuse did not match mode");
                            Require((Counts[22] > 0) == Mode.Spatial, "Material-guard spatial reuse did not match mode");
                            if (ModeIndex + 1u == Modes.size() && FloorIndex + 1u == Floors.size())
                            {
                                LastSeed = Seeds[Frame];
                                LastResolveLinks = Counts[22];
                            }
                        }
                    }
                    std::array<double, 2> Sum{}, Squared{}, Maximum{};
                    std::array<uint32_t, 2> Counts{};
                    for (uint32_t Y = 0; Y < Size; ++Y)
                        for (uint32_t X = 0; X < Size; ++X)
                        {
                            const size_t Pixel = size_t(Y) * Size + X;
                            const uint32_t Group = (X + Y) % 2u;
                            const double Sample = double(Image[Pixel][0]) * Scales[Group];
                            Require(std::isfinite(Sample) && Sample >= 0, "Material-guard replay produced invalid radiance");
                            Sum[Group] += Sample;
                            Squared[Group] += (Sample - References[Group]) * (Sample - References[Group]);
                            Maximum[Group] = std::max(Maximum[Group], Sample);
                            ++Counts[Group];
                        }
                    for (uint32_t Group = 0; Group < 2u; ++Group)
                    {
                        Require(Counts[Group] == Size * Size / 2u, "Material-guard fixture group size changed");
                        const double Mean = Sum[Group] / double(Counts[Group]);
                        const double Mse = Squared[Group] / double(Counts[Group]);
                        auto& Result = Measurement.Groups[Group];
                        Result.Mean.Accumulate(Mean);
                        Result.Mse.Accumulate(Mse);
                        Result.MaximumPixel = std::max(Result.MaximumPixel, Maximum[Group]);
                        Result.MaximumBatchMean = std::max(Result.MaximumBatchMean, Mean);
                        std::printf("REPLAY_GUARD_BATCH arm=%s scene=%s group=%s mode=%s floor=%s batch=%u "
                                    "mean=%.12g mse=%.12g max_pixel=%.12g hash=%016llx\n", Arm,
                                    SceneNames[SceneIndex], GroupNames[Group], Mode.Label, FloorNames[FloorIndex],
                                    Batch, Mean, Mse, Maximum[Group],
                                    static_cast<unsigned long long>(ReplayMaterialImageHash(Image, Size, Group)));
                    }
                }
            std::fflush(stdout);
        }
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
            for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
            {
                const auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
                for (uint32_t Group = 0; Group < 2u; ++Group)
                {
                    const auto& Result = Measurement.Groups[Group];
                    const bool MeanGate = std::abs(Result.Mean.Mean() - References[Group]) <=
                                          5.0 * Result.Mean.StandardError() + .001;
                    WithinReference = WithinReference && MeanGate;
                    std::printf("REPLAY_GUARD_RESULT arm=%s scene=%s group=%s mode=%s floor=%s reference=%.9g "
                                "mean=%.9g batch_se=%.9g rmse=%.9g mse=%.9g mse_se=%.9g "
                                "max_pixel=%.9g max_batch_mean=%.9g mean_rays=%.3f warm_rays=%.3f "
                                "mean_boxes=%.3f mean_triangles=%.3f temporal=%llu spatial=%llu "
                                "mean_gate=%u scale=%.3g batches=%u pixels_per_group=%u\n", Arm,
                                SceneNames[SceneIndex], GroupNames[Group], Modes[ModeIndex].Label, FloorNames[FloorIndex],
                                References[Group], Result.Mean.Mean(), Result.Mean.StandardError(),
                                std::sqrt(Result.Mse.Mean()), Result.Mse.Mean(), Result.Mse.StandardError(),
                                Result.MaximumPixel, Result.MaximumBatchMean, double(Measurement.FinalRays) / Batches,
                                double(Measurement.WarmRays) / Batches, double(Measurement.BoxTests) / Batches,
                                double(Measurement.TriangleTests) / Batches,
                                static_cast<unsigned long long>(Measurement.TemporalAccepted),
                                static_cast<unsigned long long>(Measurement.SpatialLinks), unsigned(MeanGate),
                                Scales[Group], Batches, Size * Size / 2u);
                }
            }
        std::fflush(stdout);
        InspectReplayMaterialGuardLinks(Compute, Shaders, Size, LastSeed, LastResolveLinks, Mixed, Candidate);
    }
    Require(WithinReference, "Material-guard holdout failed the independent mean gates");
    std::printf("PASS ReplayMaterialGuardValidation arm=%s\n", Arm);
    std::fflush(stdout);
}
