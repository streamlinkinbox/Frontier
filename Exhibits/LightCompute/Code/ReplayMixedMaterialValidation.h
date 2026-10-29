/*==========================================================================================================================================
                                           REPLAYMIXEDMATERIALVALIDATION.H
==========================================================================================================================================*/
// 🧩 Test heterogeneous material donors and unequal visibility against separate per-receiver integrals.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                           MIXED RECEIVERS AND OCCLUDER
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive ConstructReplayMixedMaterialFixture(uint32_t Size,
                                                        const LightComputeArchive& Dim,
                                                        const LightComputeArchive& Metal)
{
    auto Scene = Dim;
    Require(Dim.Angles.size() == 268u && Metal.Angles.size() == 268u, "Unexpected material atlas size");
    Scene.Angles.insert(Scene.Angles.end(), Metal.Angles.begin(), Metal.Angles.end());
    for (uint32_t Y = 0; Y < Size; ++Y)
        for (uint32_t X = 0; X < Size; ++X)
        {
            const size_t Pixel = size_t(Y) * Size + X;
            const bool IsDim = (X + Y) % 2u == 0;
            auto Receiver = IsDim ? Dim.Receivers[Pixel] : Metal.Receivers[Pixel];
            const float PositionX = IsDim ? -.75f : .75f;
            Receiver.PositionHit[0] = PositionX;
            Receiver.MotionPositionToken[0] = PositionX;
            Receiver.MotionPixel = {float(X), float(Y), .1f, 1};
            Receiver.ExpectedDirect[3] = IsDim ? 1.0f : 269.0f;
            Scene.Receivers[Pixel] = Receiver;
        }
    std::vector<LightComputeFacet> Surfaces;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    const SpatialVector Down{0, 0, -1};
    AppendPbrQuad(Surfaces, Geometry, {-1.25, -.5, 1}, {-.25, -.5, 1}, {-.25, .5, 1}, {-1.25, .5, 1},
                  Down, Down, Down, Down, 0u, 2.0f, {0, 0, 0}, .65f, 0);
    TriangleVisibilitySpace Space;
    Space.BuildSurfaceAreaHierarchy(Geometry, 1u);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.resize(Scene.Visibility.OriginalFacets.size());
    for (size_t Index = 0; Index < Scene.Surfaces.size(); ++Index)
        Scene.Surfaces[Index] = Surfaces[Scene.Visibility.OriginalFacets[Index]];
    return Scene;
}

// 📝 Independently integrate the blocked portion of the hemisphere over the planar square. The geometric
//     Jacobian and the cosine term contribute height^2 / distance^4; the GPU performs ray intersections instead.
double IntegrateReplayMixedVisibleReference(const LightComputeArchive& Material, double PositionX, uint32_t Steps)
{
    const double Unblocked = IntegrateReplayMaterialReference(Material, Steps);
    const double ViewX = double(Material.Receivers[0].ViewMetallic[0]);
    const double ViewZ = double(Material.Receivers[0].ViewMetallic[2]);
    const double ViewLength = std::sqrt(ViewX * ViewX + ViewZ * ViewZ);
    const double VX = ViewX / ViewLength, VZ = ViewZ / ViewLength;
    const double Height = 1.0 - double(.001f), Step = 1.0 / double(Steps);
    double Blocked = 0;
    for (uint32_t Row = 0; Row < Steps; ++Row)
    {
        const double Y = (double(Row) + .5) * Step - .5;
        for (uint32_t Column = 0; Column < Steps; ++Column)
        {
            const double X = (double(Column) + .5) * Step - 1.25 - PositionX;
            const double DistanceSquared = X * X + Y * Y + Height * Height;
            const double Distance = std::sqrt(DistanceSquared);
            const double LX = X / Distance, LY = Y / Distance, LZ = Height / Distance;
            const double HX = VX + LX, HY = LY, HZ = VZ + LZ;
            const double HalfLength = std::sqrt(HX * HX + HY * HY + HZ * HZ);
            const double NH = HZ / HalfLength;
            const double VH = std::clamp((VX * HX + VZ * HZ) / HalfLength, 0.0, 1.0);
            const double BrdfValue = EvaluateReplayMaterialReference(Material, VZ, LZ, NH, VH);
            Blocked += BrdfValue * Height * Height / (DistanceSquared * DistanceSquared);
        }
    }
    return Unblocked - Blocked * Step * Step;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      ACTUAL SHADER COMPATIBILITY/LINK AUDIT
//------------------------------------------------------------------------------------------------------------------------

void InspectReplayMixedMaterialLinks(LightComputeSubmission& Compute, const std::string& Shaders,
                                     uint32_t Size, uint32_t Seed, uint32_t ResolveLinks)
{
    VkPipeline Audit = Compute.ConstructPipeline(Shaders + "/ReplayMixedLinks.spv");
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
                    "Mixed-link audit returned an invalid count");
            Counts[C] += uint32_t(Pixel[C]);
        }
    const uint32_t Eligible = Counts[0] + Counts[1] + Counts[2];
    std::printf("REPLAY_MIXED_LINKS seed=%u eligible_same=%u dim_receives_metal=%u metal_receives_dim=%u "
                "rejected=%u eligible_total=%u resolve_links=%u\n", Seed, Counts[0], Counts[1], Counts[2],
                Counts[3], Eligible, ResolveLinks);
    std::fflush(stdout);
    Require(Counts[1] > 0 && Counts[2] > 0 && Eligible == ResolveLinks,
            "Mixed-material cross links were absent or differed from the actual Resolve counter");
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PAIRED QUALITY AND WORK
//------------------------------------------------------------------------------------------------------------------------

struct ReplayMixedMaterialGroupMeasurements
{
    ReplayMaterialBatchMoments Mean, Mse;
    double MaximumPixel = 0;
    double MaximumBatchMean = 0;
};

struct ReplayMixedMaterialMeasurements
{
    std::array<ReplayMixedMaterialGroupMeasurements, 2> Groups;
    uint64_t FinalRays = 0, WarmRays = 0, BoxTests = 0, TriangleTests = 0;
    uint64_t TemporalAccepted = 0, SpatialLinks = 0;
};

void ValidateReplayMixedMaterial(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 32u, Batches = 32u, Frames = 4u;
    const std::array<ReplayMaterialReuseMode, 3> Modes{{
        {"fresh1", 1u, false, false}, {"fresh4", 4u, false, false}, {"temporal_disk", 1u, true, true}}};
    constexpr std::array<float, 2> Floors{.01f, .0001f};
    constexpr std::array<const char*, 2> FloorNames{"default", "lower"};
    constexpr std::array<const char*, 2> GroupNames{"dim", "metal"};
    constexpr std::array<double, 2> Scales{1e3, 1.0};
    auto DimSpecification = EnumerateReplayMaterialSpecifications()[4];
    DimSpecification.Coverage = 1e-3f;
    DimSpecification.CoatCoverage = 2e-3f;
    DimSpecification.ReportingScale = Scales[0];
    const auto Dim = ConstructReplayMaterialReuseFixture(Size, DimSpecification);
    const auto Metal = ConstructReplayMaterialReuseFixture(Size, EnumerateReplayMaterialSpecifications()[1]);
    const auto Scene = ConstructReplayMixedMaterialFixture(Size, Dim, Metal);
    const std::array<const LightComputeArchive*, 2> Materials{&Dim, &Metal};
    const std::array<double, 2> PositionX{-.75, .75};
    std::array<double, 2> References{};
    for (uint32_t Group = 0; Group < 2u; ++Group)
    {
        References[Group] = IntegrateReplayMixedVisibleReference(*Materials[Group], PositionX[Group], 1024u) * Scales[Group];
        const double Coarse = IntegrateReplayMixedVisibleReference(*Materials[Group], PositionX[Group], 512u) * Scales[Group];
        Require(std::isfinite(References[Group]) && References[Group] > 0 &&
                    std::abs(References[Group] - Coarse) < .0002,
                "Mixed-material hemisphere and occluded-area quadratures did not converge");
        std::printf("REPLAY_MIXED_REFERENCE group=%s reference=%.9g coarse=%.9g scale=%.3g\n",
                    GroupNames[Group], References[Group], Coarse, Scales[Group]);
    }
    std::fflush(stdout);
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_MIXED preparing production pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    std::printf("REPLAY_MIXED pipelines_ready=1\n");
    std::fflush(stdout);
    std::array<ReplayMixedMaterialMeasurements, Modes.size() * Floors.size()> Results{};
    std::array<std::array<ReplayMaterialBatchMoments, 2>, Modes.size()> Differences{};
    std::array<double, 2> MaxFreshDifference{};
    std::mt19937_64 Random(0x318cd4a00761ull);
    uint32_t LastSeed = 0, LastResolveLinks = 0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        std::array<uint32_t, Frames> Seeds{};
        for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
        std::array<std::array<std::array<double, 2>, 2>, Modes.size()> BatchMse{};
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
        {
            const auto& Mode = Modes[ModeIndex];
            std::vector<std::array<float, 4>> DefaultImage;
            for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
            {
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
                        Require((Counts[20] > 0) == Mode.Temporal, "Mixed temporal reuse did not match mode");
                        Require((Counts[22] > 0) == Mode.Spatial, "Mixed spatial reuse did not match mode");
                        if (ModeIndex + 1u == Modes.size() && FloorIndex + 1u == Floors.size())
                        {
                            LastSeed = Seeds[Frame];
                            LastResolveLinks = Counts[22];
                        }
                    }
                }
                std::array<double, 2> Sum{}, Squared{};
                std::array<uint32_t, 2> Counts{};
                for (uint32_t Y = 0; Y < Size; ++Y)
                    for (uint32_t X = 0; X < Size; ++X)
                    {
                        const size_t Pixel = size_t(Y) * Size + X;
                        const uint32_t Group = (X + Y) % 2u;
                        const double Sample = double(Image[Pixel][0]) * Scales[Group];
                        Require(std::isfinite(Sample) && Sample >= 0, "Mixed material replay produced invalid radiance");
                        Sum[Group] += Sample;
                        Squared[Group] += (Sample - References[Group]) * (Sample - References[Group]);
                        ++Counts[Group];
                        Measurement.Groups[Group].MaximumPixel = std::max(Measurement.Groups[Group].MaximumPixel, Sample);
                        if (ModeIndex == 0 && FloorIndex == 1)
                            MaxFreshDifference[Group] = std::max(MaxFreshDifference[Group],
                                std::abs(double(Image[Pixel][0] - DefaultImage[Pixel][0]) * Scales[Group]));
                    }
                for (uint32_t Group = 0; Group < 2u; ++Group)
                {
                    Require(Counts[Group] == Size * Size / 2u, "Mixed fixture group size changed");
                    const double BatchMean = Sum[Group] / double(Counts[Group]);
                    auto& GroupResult = Measurement.Groups[Group];
                    GroupResult.Mean.Accumulate(BatchMean);
                    GroupResult.MaximumBatchMean = std::max(GroupResult.MaximumBatchMean, BatchMean);
                    BatchMse[ModeIndex][FloorIndex][Group] = Squared[Group] / double(Counts[Group]);
                    GroupResult.Mse.Accumulate(BatchMse[ModeIndex][FloorIndex][Group]);
                }
                if (FloorIndex == 0) DefaultImage = Image;
            }
            for (uint32_t Group = 0; Group < 2u; ++Group)
                Differences[ModeIndex][Group].Accumulate(BatchMse[ModeIndex][1][Group] - BatchMse[ModeIndex][0][Group]);
        }
        std::printf("REPLAY_MIXED cohort=%u/%u\n", Batch + 1u, Batches);
        std::fflush(stdout);
    }
    bool WithinReference = true;
    for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
        for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
        {
            const auto& Measurement = Results[ModeIndex * Floors.size() + FloorIndex];
            for (uint32_t Group = 0; Group < 2u; ++Group)
            {
                const auto& Result = Measurement.Groups[Group];
                const double Delta = Differences[ModeIndex][Group].Mean();
                const double DeltaSe = Differences[ModeIndex][Group].StandardError();
                const bool MeanGate = std::abs(Result.Mean.Mean() - References[Group]) <=
                                      5.0 * Result.Mean.StandardError() + .001;
                WithinReference = WithinReference && MeanGate;
                std::printf("REPLAY_MIXED group=%s mode=%s floor=%s floor_value=%.8g reference=%.9g "
                            "mean=%.9g batch_se=%.9g rmse=%.9g mse=%.9g mse_se=%.9g "
                            "delta_low_vs_default=%.9g delta_se=%.9g max_pixel=%.9g max_batch_mean=%.9g "
                            "mean_rays=%.3f warm_rays=%.3f mean_boxes=%.3f mean_triangles=%.3f "
                            "temporal=%llu spatial=%llu mean_gate=%u quality_low=%u regression_low=%u "
                            "scale=%.3g batches=%u pixels_per_group=%u\n",
                            GroupNames[Group], Modes[ModeIndex].Label, FloorNames[FloorIndex], Floors[FloorIndex],
                            References[Group], Result.Mean.Mean(), Result.Mean.StandardError(), std::sqrt(Result.Mse.Mean()),
                            Result.Mse.Mean(), Result.Mse.StandardError(), Delta, DeltaSe,
                            Result.MaximumPixel, Result.MaximumBatchMean, double(Measurement.FinalRays) / Batches,
                            double(Measurement.WarmRays) / Batches, double(Measurement.BoxTests) / Batches,
                            double(Measurement.TriangleTests) / Batches,
                            static_cast<unsigned long long>(Measurement.TemporalAccepted),
                            static_cast<unsigned long long>(Measurement.SpatialLinks), unsigned(MeanGate),
                            unsigned(Delta + 3.0 * DeltaSe < 0), unsigned(Delta - 3.0 * DeltaSe > 0),
                            Scales[Group], Batches, Size * Size / 2u);
            }
        }
    std::fflush(stdout);
    for (uint32_t Group = 0; Group < 2u; ++Group)
    {
        std::printf("REPLAY_MIXED_IDENTITY group=%s fresh1_max_scaled_difference=%.9g\n",
                    GroupNames[Group], MaxFreshDifference[Group]);
        Require(MaxFreshDifference[Group] <= 1e-5, "Fresh1 changed with the target floor");
    }
    InspectReplayMixedMaterialLinks(Compute, Shaders, Size, LastSeed, LastResolveLinks);
    Require(WithinReference, "Mixed-material replay failed the independent visibility/material mean gates");
    std::printf("PASS ReplayMixedMaterialValidation\n");
}
