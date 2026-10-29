/*==========================================================================================================================================
                                           REPLAYMATERIALREUSEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Check temporal/spatial authored-material replay against a separate hemisphere integral.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                               BOUNDED REUSE FIXTURE
//------------------------------------------------------------------------------------------------------------------------

struct ReplayMaterialReuseMode
{
    const char* Label;
    uint32_t Candidates;
    bool Temporal;
    bool Spatial;
};

struct ReplayMaterialReuseMeasurements
{
    ReplayMaterialBatchMoments Mean;
    ReplayMaterialBatchMoments Mse;
    ReplayMaterialBatchMoments DifferenceFresh1;
    ReplayMaterialBatchMoments DifferenceFresh4;
    uint64_t FinalRays = 0;
    uint64_t WarmRays = 0;
    uint64_t TemporalAccepted = 0;
    uint64_t SpatialLinks = 0;
};

LightComputeArchive ConstructReplayMaterialReuseFixture(uint32_t Size,
                                                         const ReplayMaterialSamplingSpecification& Specification)
{
    auto Scene = ConstructReplayMaterialFixture(Size, Specification);
    for (uint32_t Y = 0; Y < Size; ++Y)
        for (uint32_t X = 0; X < Size; ++X)
            Scene.Receivers[size_t(Y) * Size + X].MotionPixel = {float(X), float(Y), .1f, 1};
    return Scene;
}

std::vector<std::array<float, 4>> ExecuteReplayMaterialReuse(LightComputeSubmission& Compute,
                                                             const LightComputeArchive& Scene,
                                                             const ReplayMaterialReuseMode& Mode,
                                                             uint32_t Seed,
                                                             bool Reset,
                                                             const std::array<float, 3>& Environment,
                                                             bool Sequenced = false)
{
    return Compute.ExecuteReplay(Scene, 1u, Seed, Mode.Temporal, Mode.Spatial, Reset, Environment, .01f,
                                 false, false, true, false, false, false, 64u, false, false, true,
                                 false, false, false, false, false, Mode.Candidates, false, false, false,
                                 Mode.Spatial, false, true, false, false, true, Sequenced, 1u, false, false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                         C++ HISTORY REVISION CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayMaterialReuseRevisions(LightComputeSubmission& Compute, uint32_t Size)
{
    const auto& Specifications = EnumerateReplayMaterialSpecifications();
    const auto Original = ConstructReplayMaterialReuseFixture(Size, Specifications[2]);
    const auto Changed = ConstructReplayMaterialReuseFixture(Size, Specifications[1]);
    const ReplayMaterialReuseMode Mode{"temporal_disk", 1u, true, true};
    const std::array<float, 3> On{1, 1, 1}, Off{0, 0, 0};
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)ExecuteReplayMaterialReuse(Compute, Original, Mode, 901001u + 997u * Frame, Frame == 0, On, true);
    Require(Compute.ExtendedStatistics()[20] > 0, "Revision fixture did not warm temporal history");

    const auto MaterialChanged = ExecuteReplayMaterialReuse(Compute, Changed, Mode, 902003u, false, On, true);
    const bool MaterialReset = Compute.ReplayLastSequence.Reset;
    const uint32_t MaterialTemporal = Compute.ExtendedStatistics()[20];
    const auto MaterialFresh = ExecuteReplayMaterialReuse(Compute, Changed, Mode, 902003u, true, On, true);
    Require(MaterialReset && MaterialTemporal == 0 && MaterialChanged == MaterialFresh,
            "Material revision retained stale sequence history");

    for (uint32_t Frame = 0; Frame < 3; ++Frame)
        (void)ExecuteReplayMaterialReuse(Compute, Changed, Mode, 903007u + 997u * Frame, false, On, true);
    Require(Compute.ExtendedStatistics()[20] > 0, "Light revision fixture did not warm temporal history");
    const auto LightChanged = ExecuteReplayMaterialReuse(Compute, Changed, Mode, 904009u, false, Off, true);
    const bool LightReset = Compute.ReplayLastSequence.Reset;
    const uint32_t LightTemporal = Compute.ExtendedStatistics()[20];
    const auto LightFresh = ExecuteReplayMaterialReuse(Compute, Changed, Mode, 904009u, true, Off, true);
    const bool LightZero = std::all_of(LightChanged.begin(), LightChanged.end(), [](const auto& Pixel)
    {
        return Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 0;
    });
    Require(LightReset && LightTemporal == 0 && LightChanged == LightFresh && LightZero,
            "Environment revision retained stale sequence history");
    std::printf("REPLAY_MATERIAL_REVISIONS material_reset=%u material_exact=%u material_temporal=%u "
                "light_reset=%u light_exact=%u light_zero=%u light_temporal=%u\n",
                unsigned(MaterialReset), unsigned(MaterialChanged == MaterialFresh), MaterialTemporal,
                unsigned(LightReset), unsigned(LightChanged == LightFresh), unsigned(LightZero), LightTemporal);
    std::fflush(stdout);
}

//------------------------------------------------------------------------------------------------------------------------
//                                        MEAN, QUALITY AND WORK CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayMaterialReuse(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 32u, Batches = 32u, Frames = 4u;
    constexpr std::array<uint32_t, 4> CaseIndices{0u, 1u, 2u, 4u};
    const std::array<ReplayMaterialReuseMode, 5> Modes{{
        {"fresh1", 1u, false, false}, {"fresh4", 4u, false, false}, {"temporal", 1u, true, false},
        {"spatial_disk", 1u, false, true}, {"temporal_disk", 1u, true, true}}};
    const auto Capacity = ConstructReplayDiffuseBounceFixture(Size);
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_MATERIAL_REUSE preparing production pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    std::printf("REPLAY_MATERIAL_REUSE pipelines_ready=1\n");
    std::fflush(stdout);
    bool WithinReference = true;
    for (uint32_t CaseIndex : CaseIndices)
    {
        const auto& Specification = EnumerateReplayMaterialSpecifications()[CaseIndex];
        const auto Scene = ConstructReplayMaterialReuseFixture(Size, Specification);
        const double Reference = IntegrateReplayMaterialReference(Scene, 1024u) * Specification.ReportingScale;
        const double Coarse = IntegrateReplayMaterialReference(Scene, 512u) * Specification.ReportingScale;
        Require(std::abs(Reference - Coarse) < .0002, "Material reuse quadrature did not converge");
        std::array<ReplayMaterialReuseMeasurements, Modes.size()> Results{};
        std::mt19937_64 Random(0xa317e4c59b01ull + uint64_t(CaseIndex) * 0x1000193ull);
        std::printf("REPLAY_MATERIAL_REUSE starting=%s\n", Specification.Label);
        std::fflush(stdout);
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            std::array<uint32_t, Frames> Seeds{};
            for (uint32_t& Seed : Seeds) Seed = uint32_t(Random());
            std::array<double, Modes.size()> BatchMse{};
            for (size_t Index = 0; Index < Modes.size(); ++Index)
            {
                const auto& Mode = Modes[Index];
                const uint32_t Submissions = Mode.Temporal ? Frames : 1u;
                std::vector<std::array<float, 4>> Image;
                for (uint32_t Frame = 0; Frame < Submissions; ++Frame)
                {
                    const bool Final = Frame + 1u == Submissions;
                    const uint32_t Seed = Mode.Temporal ? Seeds[Frame] : Seeds[Frames - 1u];
                    Image = ExecuteReplayMaterialReuse(Compute, Scene, Mode, Seed, Frame == 0,
                                                        {1, 1, 1});
                    const auto Counters = Compute.ExtendedStatistics();
                    if (!Final) Results[Index].WarmRays += Counters[8];
                    else
                    {
                        Results[Index].FinalRays += Counters[8];
                        Results[Index].TemporalAccepted += Counters[20];
                        Results[Index].SpatialLinks += Counters[22];
                        Require((Counters[20] > 0) == Mode.Temporal,
                                "Temporal material reuse did not match requested mode");
                        Require((Counters[22] > 0) == Mode.Spatial,
                                "Spatial material reuse did not match requested mode");
                    }
                }
                double Sum = 0, Squared = 0;
                for (const auto& Pixel : Image)
                {
                    const double Sample = double(Pixel[0]) * Specification.ReportingScale;
                    Require(std::isfinite(Sample) && Sample >= 0, "Invalid material reuse radiance");
                    Sum += Sample;
                    Squared += (Sample - Reference) * (Sample - Reference);
                }
                Results[Index].Mean.Accumulate(Sum / double(Image.size()));
                BatchMse[Index] = Squared / double(Image.size());
                Results[Index].Mse.Accumulate(BatchMse[Index]);
            }
            for (size_t Index = 0; Index < Modes.size(); ++Index)
            {
                Results[Index].DifferenceFresh1.Accumulate(BatchMse[Index] - BatchMse[0]);
                Results[Index].DifferenceFresh4.Accumulate(BatchMse[Index] - BatchMse[1]);
            }
        }
        for (size_t Index = 0; Index < Modes.size(); ++Index)
        {
            const auto& Measurement = Results[Index];
            const double Difference1 = Measurement.DifferenceFresh1.Mean();
            const double Difference4 = Measurement.DifferenceFresh4.Mean();
            const double Difference1Se = Measurement.DifferenceFresh1.StandardError();
            const double Difference4Se = Measurement.DifferenceFresh4.StandardError();
            const bool MeanGate = std::abs(Measurement.Mean.Mean() - Reference) <=
                                  5.0 * Measurement.Mean.StandardError() + .001;
            WithinReference = WithinReference && MeanGate;
            std::printf("REPLAY_MATERIAL_REUSE case=%s mode=%s reference=%.9g mean=%.9g batch_se=%.9g "
                        "rmse=%.9g mse=%.9g mse_se=%.9g delta_fresh1=%.9g delta_fresh1_se=%.9g "
                        "delta_fresh4=%.9g delta_fresh4_se=%.9g mean_rays=%.3f warm_rays=%.3f "
                        "temporal=%llu spatial=%llu mean_gate=%u quality_fresh1=%u quality_fresh4=%u "
                        "scale=%.3g batches=%u pixels=%u warm_frames=%u\n",
                        Specification.Label, Modes[Index].Label, Reference, Measurement.Mean.Mean(),
                        Measurement.Mean.StandardError(), std::sqrt(Measurement.Mse.Mean()),
                        Measurement.Mse.Mean(), Measurement.Mse.StandardError(), Difference1, Difference1Se,
                        Difference4, Difference4Se, double(Measurement.FinalRays) / Batches,
                        double(Measurement.WarmRays) / Batches,
                        static_cast<unsigned long long>(Measurement.TemporalAccepted),
                        static_cast<unsigned long long>(Measurement.SpatialLinks), unsigned(MeanGate),
                        unsigned(Difference1 + 3.0 * Difference1Se < 0),
                        unsigned(Difference4 + 3.0 * Difference4Se < 0), Specification.ReportingScale,
                        Batches, Size * Size, Modes[Index].Temporal ? Frames - 1u : 0u);
        }
        std::fflush(stdout);
    }
    ValidateReplayMaterialReuseRevisions(Compute, Size);
    Require(WithinReference, "Material replay reuse failed the independent hemisphere mean gates");
    std::printf("PASS ReplayMaterialReuseValidation\n");
}
