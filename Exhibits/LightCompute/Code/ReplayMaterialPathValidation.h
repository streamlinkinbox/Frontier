/*==========================================================================================================================================
                                             REPLAYMATERIALPATHVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify recorded replay dispatches against independent material and two-bounce diffuse references.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                           DIFFUSE TWO-BOUNCE FIXTURE
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive ConstructReplayDiffuseBounceFixture(uint32_t Size)
{
    auto Scene = ConstructReplayMaterialFixture(Size, EnumerateReplayMaterialSpecifications()[0]);
    for (auto& Receiver : Scene.Receivers)
    {
        Receiver.NormalRoughness = {0, 0, 1, .65f};
        Receiver.ViewMetallic = {0, 0, 1, 0};
        Receiver.AlbedoIor = {.7f, .7f, .7f, 1.5f};
        Receiver.ExpectedDirect = {0, 0, 0, 0};
    }
    std::vector<LightComputeFacet> Surfaces;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    const SpatialVector Down{0, 0, -1};
    AppendPbrQuad(Surfaces, Geometry, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
                  Down, Down, Down, Down, 0u, 2.0f, {.4f, .4f, .4f}, .65f, 0);
    TriangleVisibilitySpace Space;
    Space.BuildSurfaceAreaHierarchy(Geometry, 1u);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.resize(Scene.Visibility.OriginalFacets.size());
    for (size_t Index = 0; Index < Scene.Surfaces.size(); ++Index)
        Scene.Surfaces[Index] = Surfaces[Scene.Visibility.OriginalFacets[Index]];
    return Scene;
}

// 📝 The first diffuse direction hits the square with cosine-weighted projected solid angle F.
//     An escaped ray sees unit environment; a hit contributes only after a second diffuse bounce.
double IntegrateReplayDiffuseSquareFactor(uint32_t Steps)
{
    constexpr double π = 3.14159265358979323846264338327950288;
    const double Height = 1.0 - double(.001f), Step = 2.0 / double(Steps);
    double Sum = 0;
    for (uint32_t Row = 0; Row < Steps; ++Row)
    {
        const double Y = (double(Row) + .5) * Step - 1.0;
        for (uint32_t Column = 0; Column < Steps; ++Column)
        {
            const double X = (double(Column) + .5) * Step - 1.0;
            const double DistanceSquared = X * X + Y * Y + Height * Height;
            Sum += Height * Height / (π * DistanceSquared * DistanceSquared);
        }
    }
    return Sum * Step * Step;
}

//------------------------------------------------------------------------------------------------------------------------
//                                         RECORDED VULKAN REPLAY CHECKS
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::array<float, 4>> EvaluateReplayMaterialPath(LightComputeSubmission& Compute,
                                                             const LightComputeArchive& Scene,
                                                             uint32_t Bounces,
                                                             uint32_t Seed,
                                                             bool Authored)
{
    return Compute.ExecuteReplay(Scene, Bounces, Seed, false, false, true, {1, 1, 1}, .01f,
                                 false, false, Authored, false, false, false, 64u,
                                 false, false, Authored, false, false, false, false, false,
                                 1u, false, false, false, false, false, true, false, false, Authored);
}

void ValidateReplayMaterialPath(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Size = 64u, Batches = 32u;
    const auto Capacity = ConstructReplayDiffuseBounceFixture(Size);
    LightComputeSubmission Compute(Driver);
    std::printf("REPLAY_MATERIAL_PATH preparing production pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    std::printf("REPLAY_MATERIAL_PATH pipelines_ready=1\n");
    std::fflush(stdout);
    bool WithinReference = true;
    for (const auto& Specification : EnumerateReplayMaterialSpecifications())
    {
        std::printf("REPLAY_MATERIAL_PATH starting=%s\n", Specification.Label);
        std::fflush(stdout);
        const auto Scene = ConstructReplayMaterialFixture(Size, Specification);
        const double Reference = IntegrateReplayMaterialReference(Scene, 1024u) * Specification.ReportingScale;
        const double Coarse = IntegrateReplayMaterialReference(Scene, 512u) * Specification.ReportingScale;
        Require(std::abs(Reference - Coarse) < .0002, "Material path hemisphere quadrature did not converge");
        ReplayMaterialBatchMoments Mean, Mse;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            const auto Picture = EvaluateReplayMaterialPath(Compute, Scene, 1u, 330001u + 7919u * Batch, true);
            double Sum = 0, Squared = 0;
            for (const auto& Pixel : Picture)
            {
                const double Sample = double(Pixel[0]) * Specification.ReportingScale;
                Require(std::isfinite(Sample) && Sample >= 0, "Recorded material replay produced invalid radiance");
                Sum += Sample;
                Squared += (Sample - Reference) * (Sample - Reference);
            }
            Mean.Accumulate(Sum / double(Picture.size()));
            Mse.Accumulate(Squared / double(Picture.size()));
        }
        std::printf("REPLAY_MATERIAL_PATH case=%s bounces=1 reference=%.9g mean=%.9g batch_se=%.3g "
                    "sample_rmse=%.9g scale=%.3g batches=%u samples_per_batch=%u\n",
                    Specification.Label, Reference, Mean.Mean(), Mean.StandardError(), std::sqrt(Mse.Mean()),
                    Specification.ReportingScale, Batches, Size * Size);
        std::fflush(stdout);
        WithinReference = WithinReference && std::abs(Mean.Mean() - Reference) <= 5.0 * Mean.StandardError() + .001;
    }
    const auto Diffuse = ConstructReplayDiffuseBounceFixture(Size);
    const double Factor = IntegrateReplayDiffuseSquareFactor(1024u);
    const double CoarseFactor = IntegrateReplayDiffuseSquareFactor(512u);
    Require(std::abs(Factor - CoarseFactor) < 1e-5, "Diffuse square area quadrature did not converge");
    for (uint32_t Bounces : {1u, 2u})
    {
        const double ReceiverAlbedo = double(Diffuse.Receivers[0].AlbedoIor[0]);
        const double ReflectorAlbedo = double(Diffuse.Surfaces[0].AlbedoRoughness[0]);
        const double Reference = ReceiverAlbedo * (1.0 - Factor + (Bounces == 2u ? ReflectorAlbedo * Factor : 0));
        ReplayMaterialBatchMoments Mean;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            const auto Picture = EvaluateReplayMaterialPath(Compute, Diffuse, Bounces, 520003u + 7919u * Batch, false);
            double Sum = 0;
            for (const auto& Pixel : Picture)
            {
                Require(std::isfinite(Pixel[0]) && Pixel[0] >= 0, "Diffuse bounce replay produced invalid radiance");
                Sum += Pixel[0];
            }
            Mean.Accumulate(Sum / double(Picture.size()));
        }
        std::printf("REPLAY_DIFFUSE_BOUNCE bounces=%u square_factor=%.9g reference=%.9g mean=%.9g "
                    "batch_se=%.3g batches=%u samples_per_batch=%u\n",
                    Bounces, Factor, Reference, Mean.Mean(), Mean.StandardError(), Batches, Size * Size);
        std::fflush(stdout);
        WithinReference = WithinReference && std::abs(Mean.Mean() - Reference) <= 5.0 * Mean.StandardError() + .001;
    }
    Require(WithinReference, "Recorded material or diffuse bounce replay differs from the independent references");
    std::printf("PASS ReplayMaterialPathValidation\n");
}
