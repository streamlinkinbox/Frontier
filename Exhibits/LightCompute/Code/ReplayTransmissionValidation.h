/*==========================================================================================================================================
                                               REPLAYTRANSMISSIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare replayed thin-sheet transport with an independent area integral and separately sampled paths.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                               DIFFUSE RECEIVER AND PANE
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive BuildReplayTransmissionScene(uint32_t Size, uint32_t PaneLayers = 1u)
{
    LightComputeArchive Scene{};
    Scene.Width = Size;
    Scene.Height = Size;
    Scene.Receivers.resize(size_t(Size) * Size);
    for (auto& Receiver : Scene.Receivers)
    {
        Receiver.PositionHit = {0, 0, 0, 1};
        Receiver.NormalRoughness = {0, 0, 1, .65f};
        Receiver.ViewMetallic = {0, 0, 1, 0};
        Receiver.AlbedoIor = {.7f, .7f, .7f, 1.5f};
        Receiver.ResidualSpecular = {0, 0, 0, .5f};
        Receiver.MotionPositionToken = {0, 0, 0, 1};
        Receiver.MotionNormal = {0, 0, 1, 0};
        Receiver.MotionPixel = {0, 0, .1f, 0};
    }
    // 📝 A category-2 parallel sheet with authored absorption and thickness [1/m, m].
    Scene.Angles.resize(269u, 0.0f);
    const uint32_t Start = 1u;
    Scene.Angles[Start] = 1.0f;
    Scene.Angles[Start + 1u] = 2.0f;
    Scene.Angles[Start + 4u] = 1.0f;
    Scene.Angles[Start + 5u] = 1.0f;
    Scene.Angles[Start + 8u] = .8f;
    Scene.Angles[Start + 9u] = .8f;
    Scene.Angles[Start + 10u] = .8f;
    Scene.Angles[Start + 11u] = .5f;
    Scene.Angles[Start + 13u] = 1.5f;
    Scene.Angles[Start + 14u] = 1.0f;
    Scene.Angles[Start + 16u] = .4f;
    Scene.Angles[Start + 17u] = .4f;
    Scene.Angles[Start + 18u] = .4f;
    Scene.Angles[Start + 19u] = .03f;
    Scene.Camera[3][3] = .5f;
    Scene.Camera[11] = {1, 0, 1, 0};

    std::vector<LightComputeFacet> Authored;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    const SpatialVector Down{0, 0, -1};
    Require(PaneLayers <= 2u, "Pane/source quadrature supports at most two layers");
    for (uint32_t Index = 0; Index < PaneLayers; ++Index)
    {
        const double Height = double(Index + 1u) / double(PaneLayers + 1u);
        AppendPbrQuad(Authored, Geometry, {-2, -2, Height}, {2, -2, Height}, {2, 2, Height}, {-2, 2, Height},
                      Down, Down, Down, Down, Start + 1u, float(2u + Index), {.8f, .8f, .8f}, .5f, 0);
    }
    AppendPbrQuad(Authored, Geometry, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
                  Down, Down, Down, Down, 0u, 10.0f, {0, 0, 0}, .65f, 0, {1, 1, 1});
    TriangleVisibilitySpace Space;
    Space.BuildSurfaceAreaHierarchy(Geometry, 1u);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.resize(Scene.Visibility.OriginalFacets.size());
    for (size_t Index = 0; Index < Scene.Surfaces.size(); ++Index)
    {
        Scene.Surfaces[Index] = Authored[Scene.Visibility.OriginalFacets[Index]];
    }
    return Scene;
}

// 📝 Independent double-precision area quadrature over the source; no shader RNG or BVH is used.
double IntegrateReplayTransmissionReference(uint32_t Steps, uint32_t PaneLayers)
{
    constexpr double Pi = 3.14159265358979323846;
    constexpr double Roughness = .65;
    constexpr double Reflectance = .04;
    const double Alpha = Roughness * Roughness;
    const double Alpha2 = Alpha * Alpha;
    const double Height = 1.0 - .001;
    const double Step = 2.0 / double(Steps);
    double Sum = 0;
    for (uint32_t Row = 0; Row < Steps; ++Row)
    {
        for (uint32_t Column = 0; Column < Steps; ++Column)
        {
            const double X = (double(Column) + .5) * Step - 1.0;
            const double Y = (double(Row) + .5) * Step - 1.0;
            const double DistanceSquared = X * X + Y * Y + Height * Height;
            const double Cosine = Height / std::sqrt(DistanceSquared);
            const double NormalHalf = std::sqrt((1.0 + Cosine) * .5);
            const double Denominator = NormalHalf * NormalHalf * (Alpha2 - 1.0) + 1.0;
            const double Distribution = Alpha2 / (Pi * Denominator * Denominator);
            const double Visibility = .5 / (Cosine + std::sqrt(Cosine * Cosine * (1.0 - Alpha2) + Alpha2));
            const double Fresnel = Reflectance + (1.0 - Reflectance) * std::pow(1.0 - NormalHalf, 5.0);
            const double Brdf = Distribution * Visibility * Fresnel + (1.0 - Fresnel) * .7 / Pi;
            const double SheetFresnel = Reflectance + (1.0 - Reflectance) * std::pow(1.0 - Cosine, 5.0);
            const double Transmittance = (1.0 - SheetFresnel) / (1.0 + SheetFresnel) *
                                         std::exp(-.4 * .03 / std::max(.05, Cosine));
            Sum += Brdf * std::pow(Transmittance, int(PaneLayers)) * Cosine * Cosine / DistanceSquared;
        }
    }
    return Sum * Step * Step;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               VULKAN ESTIMATOR CHECK
//------------------------------------------------------------------------------------------------------------------------

std::vector<std::array<float, 4>> ExecuteReplayTransmission(LightComputeSubmission& Compute,
                                                             const LightComputeArchive& Scene,
                                                             uint32_t Seed,
                                                             uint32_t Bounces,
                                                             bool FacetSampling,
                                                             bool Balance,
                                                             bool PowerMis)
{
    return Compute.ExecuteReplay(Scene, Bounces, Seed, false, false, true, {0, 0, 0}, .01f,
                                 false, false, true, FacetSampling, false, false, 64u,
                                 Balance, false, true, false, false, false, false, false,
                                 1u, false, false, false, false, false, true, PowerMis, false, true);
}

struct ReplayTransmissionMoments
{
    double Sum = 0;
    double Squared = 0;
    uint32_t Count = 0;

    void Include(double Sample)
    {
        Sum += Sample;
        Squared += Sample * Sample;
        ++Count;
    }
    double Mean() const { return Sum / double(Count); }
    double StandardError() const
    {
        const double Variance = (Squared - Sum * Sum / double(Count)) / double(Count - 1u);
        return std::sqrt(std::max(0.0, Variance) / double(Count));
    }
};

void ValidateReplaySegment(LightComputeSubmission& Compute, const std::string& Shaders, uint32_t Size);

void ValidateReplayTransmission(const char* Driver, const std::string& Shaders, bool Baseline = false)
{
    constexpr uint32_t Size = 64u;
    // 📝 Prepare the larger scene first so one submission can also exercise the diffuse seed-bank fixture.
    const auto Capacity = BuildPbrShowroomScene(Size, Size, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Capacity, Shaders, false, false, false, false, true);
    struct TransmissionCase
    {
        uint32_t PaneLayers;
        uint32_t Bounces;
        uint32_t Batches;
    };
    // 📝 Include both attainable BSDF paths and bounce-limited direct-only connections.
    const std::array<TransmissionCase, 5> Cases{{{0u, 1u, 12u}, {1u, 1u, 12u}, {1u, 2u, 24u},
                                                 {2u, 2u, 12u}, {2u, 3u, 12u}}};
    const char* Names[] = {"bsdf", "facet_only", "balance", "power"};
    bool WithinReference = true;
    for (const auto& Configuration : Cases)
    {
        const auto Scene = BuildReplayTransmissionScene(Size, Configuration.PaneLayers);
        const double Reference = IntegrateReplayTransmissionReference(512u, Configuration.PaneLayers);
        const double Coarse = IntegrateReplayTransmissionReference(256u, Configuration.PaneLayers);
        Require(std::abs(Reference - Coarse) < 1e-5, "Pane/source quadrature did not converge");
        std::array<ReplayTransmissionMoments, 4> Methods{};
        const uint32_t FirstMethod = Configuration.Bounces > Configuration.PaneLayers ? 0u : 1u;
        for (uint32_t Batch = 0; Batch < Configuration.Batches; ++Batch)
        {
            const uint32_t Seed = 137041u + Batch * 7919u;
            for (uint32_t Method = FirstMethod; Method < Methods.size(); ++Method)
            {
                const auto Picture = ExecuteReplayTransmission(Compute, Scene, Seed, Configuration.Bounces,
                                                                Method != 0u, Method >= 2u, Method == 3u);
                double Mean = 0;
                for (const auto& Pixel : Picture)
                {
                    Require(std::isfinite(Pixel[0]) && Pixel[0] >= 0, "Pane transport produced invalid radiance");
                    Mean += Pixel[0];
                }
                Methods[Method].Include(Mean / double(Picture.size()));
            }
        }
        for (uint32_t Method = FirstMethod; Method < Methods.size(); ++Method)
        {
            std::printf("REPLAY_TRANSMISSION panes=%u bounces=%u method=%s mean=%.9f batch_se=%.9f "
                        "reference=%.9f error=%.9f batches=%u samples_per_batch=%u\n",
                        Configuration.PaneLayers, Configuration.Bounces, Names[Method], Methods[Method].Mean(),
                        Methods[Method].StandardError(), Reference, Methods[Method].Mean() - Reference,
                        Configuration.Batches, Size * Size);
            WithinReference = WithinReference &&
                std::abs(Methods[Method].Mean() - Reference) <= 5.0 * Methods[Method].StandardError() + .006;
        }
    }

    // 📝 A deliberately duplicated temporal/bank seed tests proposal bookkeeping, not distribution unbiasedness.
    //     Both proposals must retain their weights and counts even if one current-surface path can be cached.
    const auto Plane = ReplayCameraPlane(0, 0, 0, 2, Size);
    (void)ExecuteReplaySampleHistory(Compute, Plane, 271001u, true, true, 1u, {0, 0, 0}, 1u, true, false);
    const auto Guides = Compute.PrimaryReceivers();
    std::vector<uint32_t> SelectedPixels;
    for (uint32_t Pixel = 0; Pixel < Guides.size() && SelectedPixels.size() < 64u; ++Pixel)
    {
        if (Guides[Pixel].PositionHit[3] != 0 && Guides[Pixel].MotionPositionToken[3] == 1.0f &&
            Guides[Pixel].MotionPixel[3] != 0)
        {
            const auto Source = ReplaySampleSlot(Compute, 0u, Pixel);
            if (Source[3] != 1.0f) continue;
            SetReplaySampleSlot(Compute, 11u, Pixel, {Source[0], Source[1], 0, 0});
            SetReplaySampleSlot(Compute, 12u, Pixel, {0, 0, 1, 0});
            SelectedPixels.push_back(Pixel);
        }
    }
    Require(SelectedPixels.size() == 64u, "Seed equality check needs 64 reprojectable floor pixels");
    (void)ExecuteReplaySampleHistory(Compute, Plane, 271003u, false, true, 1u, {0, 0, 0}, 1u, true, false);
    uint32_t RetainedBoth = 0;
    for (uint32_t Pixel : SelectedPixels)
    {
        RetainedBoth += ReplaySampleSlot(Compute, 2u, Pixel)[3] == 3.0f ? 1u : 0u;
    }
    std::printf("REPLAY_DUPLICATE_PROPOSALS selected=%zu represented_both=%u expected_count=3\n",
                SelectedPixels.size(), RetainedBoth);
    std::fflush(stdout);
    if (!Baseline) ValidateReplaySegment(Compute, Shaders, Size);
    Require(WithinReference, "Thin-sheet transport failed the independent area-reference gate");
    Require(RetainedBoth == SelectedPixels.size(), "Equal seeds lost a separately weighted proposal");
    std::printf("PASS ReplayTransmissionValidation\n");
}
