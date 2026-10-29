/*==========================================================================================================================================
                                           REPLAYMATERIALFLOORVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare accepted replay target floors against independent authored-material integrals.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                TARGET FLOOR DISPATCH
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::array<float, 4>> ExecuteReplayMaterialFloor(LightComputeSubmission& Compute,
                                                             const LightComputeArchive& Scene,
                                                             const ReplayMaterialReuseMode& Mode,
                                                             uint32_t Seed,
                                                             bool Reset,
                                                             float PositiveFloor,
                                                             bool Sequenced = false)
{
    return Compute.ExecuteReplay(Scene, 1u, Seed, Mode.Temporal, Mode.Spatial, Reset, {1, 1, 1}, PositiveFloor,
                                 false, false, true, false, false, false, 64u, false, false, true,
                                 false, false, false, false, false, Mode.Candidates, false, false, false,
                                 Mode.Spatial, false, true, false, false, true, Sequenced, 1u, false, false);
}

void ValidateReplayMaterialFloorRevision(LightComputeSubmission& Compute, const LightComputeArchive& Scene)
{
    const ReplayMaterialReuseMode Mode{"temporal_disk", 1u, true, true};
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)ExecuteReplayMaterialFloor(Compute, Scene, Mode, 920003u + 997u * Frame, Frame == 0, .01f, true);
    Require(Compute.ExtendedStatistics()[20] > 0, "Floor-revision fixture did not warm temporal history");
    const auto Changed = ExecuteReplayMaterialFloor(Compute, Scene, Mode, 924011u, false, .0001f, true);
    const bool Reset = Compute.ReplayLastSequence.Reset;
    const uint32_t Temporal = Compute.ExtendedStatistics()[20];
    const auto Fresh = ExecuteReplayMaterialFloor(Compute, Scene, Mode, 924011u, true, .0001f, true);
    Require(Reset && Temporal == 0 && Changed == Fresh, "Changed target floor retained sequence history");
    std::printf("REPLAY_MATERIAL_FLOOR_REVISION reset=%u exact=%u temporal=%u\n",
                unsigned(Reset), unsigned(Changed == Fresh), Temporal);
    std::fflush(stdout);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             PAIRED FLOOR EXPERIMENT
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayMaterialFloors(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 32u, Batches = 32u, Frames = 4u;
    const std::array<ReplayMaterialReuseMode, 3> Modes{{
        {"fresh1", 1u, false, false}, {"fresh4", 4u, false, false}, {"temporal_disk", 1u, true, true}}};
    const std::array<float, 2> Floors{.01f, .0001f};
    const auto& Known = EnumerateReplayMaterialSpecifications();
    std::array<ReplayMaterialSamplingSpecification, 4> Cases{Known[1], Known[2], Known[4], Known[4]};
    Cases[3].Label = "dim";
    Cases[3].Coverage = .001f;
    Cases[3].CoatCoverage = .002f;
    Cases[3].ReportingScale = 1000.0;
    const auto Capacity = ConstructReplayDiffuseBounceFixture(Size);
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_MATERIAL_FLOOR preparing production pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    std::printf("REPLAY_MATERIAL_FLOOR pipelines_ready=1\n");
    std::fflush(stdout);
    bool WithinReference = true;
    for (size_t CaseIndex = 0; CaseIndex < Cases.size(); ++CaseIndex)
    {
        const auto& Specification = Cases[CaseIndex];
        const auto Scene = ConstructReplayMaterialReuseFixture(Size, Specification);
        const double Reference = IntegrateReplayMaterialReference(Scene, 1024u) * Specification.ReportingScale;
        const double Coarse = IntegrateReplayMaterialReference(Scene, 512u) * Specification.ReportingScale;
        Require(std::abs(Reference - Coarse) < .0002, "Floor-study quadrature did not converge");
        std::array<std::array<ReplayMaterialReuseMeasurements, 2>, 3> Results{};
        std::array<ReplayMaterialBatchMoments, 3> Difference{};
        std::mt19937_64 Random(0x4c2db7a90131ull + uint64_t(CaseIndex) * 0x1000193ull);
        double MaximumFresh1Difference = 0;
        std::printf("REPLAY_MATERIAL_FLOOR starting=%s\n", Specification.Label);
        std::fflush(stdout);
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            std::array<uint32_t, Frames> Seeds{};
            for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
            for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
            {
                const auto& Mode = Modes[ModeIndex];
                std::array<double, 2> BatchMse{};
                std::vector<std::array<float, 4>> BaselineImage;
                for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
                {
                    const uint32_t Submissions = Mode.Temporal ? Frames : 1u;
                    std::vector<std::array<float, 4>> Image;
                    for (uint32_t Frame = 0; Frame < Submissions; ++Frame)
                    {
                        const bool Final = Frame + 1u == Submissions;
                        const uint32_t Seed = Mode.Temporal ? Seeds[Frame] : Seeds[Frames - 1u];
                        Image = ExecuteReplayMaterialFloor(Compute, Scene, Mode, Seed, Frame == 0, Floors[FloorIndex]);
                        const auto Counters = Compute.ExtendedStatistics();
                        auto& Measurement = Results[ModeIndex][FloorIndex];
                        if (!Final) Measurement.WarmRays += Counters[8];
                        else
                        {
                            Measurement.FinalRays += Counters[8];
                            Measurement.TemporalAccepted += Counters[20];
                            Measurement.SpatialLinks += Counters[22];
                            Require((Counters[20] > 0) == Mode.Temporal,
                                    "Floor-study temporal reuse did not match requested mode");
                            Require((Counters[22] > 0) == Mode.Spatial,
                                    "Floor-study spatial reuse did not match requested mode");
                        }
                    }
                    double Sum = 0, Squared = 0;
                    for (size_t Pixel = 0; Pixel < Image.size(); ++Pixel)
                    {
                        const double Sample = double(Image[Pixel][0]) * Specification.ReportingScale;
                        Require(std::isfinite(Sample) && Sample >= 0, "Invalid floor-study material radiance");
                        Sum += Sample;
                        Squared += (Sample - Reference) * (Sample - Reference);
                        if (ModeIndex == 0 && FloorIndex == 1)
                            MaximumFresh1Difference = std::max(MaximumFresh1Difference,
                                std::abs(double(Image[Pixel][0] - BaselineImage[Pixel][0]) * Specification.ReportingScale));
                    }
                    if (ModeIndex == 0 && FloorIndex == 0) BaselineImage = Image;
                    auto& Measurement = Results[ModeIndex][FloorIndex];
                    Measurement.Mean.Accumulate(Sum / double(Image.size()));
                    BatchMse[FloorIndex] = Squared / double(Image.size());
                    Measurement.Mse.Accumulate(BatchMse[FloorIndex]);
                }
                Difference[ModeIndex].Accumulate(BatchMse[1] - BatchMse[0]);
            }
        }
        Require(MaximumFresh1Difference < 1e-5, "One-candidate result changed with target floor");
        for (size_t ModeIndex = 0; ModeIndex < Modes.size(); ++ModeIndex)
        {
            const double Delta = Difference[ModeIndex].Mean(), DeltaSe = Difference[ModeIndex].StandardError();
            for (size_t FloorIndex = 0; FloorIndex < Floors.size(); ++FloorIndex)
            {
                const auto& Measurement = Results[ModeIndex][FloorIndex];
                const bool MeanGate = std::abs(Measurement.Mean.Mean() - Reference) <=
                                      5.0 * Measurement.Mean.StandardError() + .001;
                WithinReference = WithinReference && MeanGate;
                std::printf("REPLAY_MATERIAL_FLOOR case=%s mode=%s floor=%s floor_value=%.8g "
                            "reference=%.9g mean=%.9g batch_se=%.9g rmse=%.9g mse=%.9g mse_se=%.9g "
                            "delta_low_vs_default=%.9g delta_se=%.9g mean_rays=%.3f warm_rays=%.3f "
                            "temporal=%llu spatial=%llu mean_gate=%u quality_low=%u scale=%.3g batches=%u pixels=%u\n",
                            Specification.Label, Modes[ModeIndex].Label, FloorIndex == 0 ? "default" : "lower",
                            double(Floors[FloorIndex]), Reference, Measurement.Mean.Mean(),
                            Measurement.Mean.StandardError(), std::sqrt(Measurement.Mse.Mean()),
                            Measurement.Mse.Mean(), Measurement.Mse.StandardError(), Delta, DeltaSe,
                            double(Measurement.FinalRays) / Batches, double(Measurement.WarmRays) / Batches,
                            static_cast<unsigned long long>(Measurement.TemporalAccepted),
                            static_cast<unsigned long long>(Measurement.SpatialLinks), unsigned(MeanGate),
                            unsigned(Delta + 3.0 * DeltaSe < 0), Specification.ReportingScale, Batches, Size * Size);
            }
        }
        std::printf("REPLAY_MATERIAL_FLOOR_IDENTITY case=%s fresh1_max_scaled_difference=%.9g\n",
                    Specification.Label, MaximumFresh1Difference);
        std::fflush(stdout);
    }
    ValidateReplayMaterialFloorRevision(Compute, ConstructReplayMaterialReuseFixture(Size, Cases[3]));
    Require(WithinReference, "Target-floor study failed an independent hemisphere mean gate");
    std::printf("PASS ReplayMaterialFloorValidation\n");
}
