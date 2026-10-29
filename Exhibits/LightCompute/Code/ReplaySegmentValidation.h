/*==========================================================================================================================================
                                               REPLAYSEGMENTVALIDATION.H
==========================================================================================================================================*/
// 🧩 Exercise pane transmittance, stack-overflow fallback, and MIS limit weights on Vulkan.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                              FOUR-PANE BOUNDED BVH
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive BuildReplaySegmentFixture(uint32_t Size)
{
    auto Scene = BuildReplayTransmissionScene(Size, 0u);
    Scene.Surfaces.clear();
    Scene.Visibility.Corners.clear();
    Scene.Visibility.Enclosures.clear();
    Scene.Visibility.OriginalFacets.clear();
    const float Heights[] = {.2f, .8f, .9f, 1.0f};
    for (uint32_t Index = 0; Index < 4u; ++Index)
    {
        LightComputeFacet Facet{};
        Facet.A = {-2, -2, Heights[Index], 0};
        Facet.B = {2, -2, Heights[Index], 0};
        Facet.C = {0, 2, Heights[Index], 0};
        Facet.NormalA = Facet.NormalB = Facet.NormalC = {0, 0, -1, 0};
        Facet.AlbedoRoughness = {.8f, .8f, .8f, .5f};
        Facet.OpticalToken = {0, 1.5f, .5f, float(Index + 2u)};
        Facet.Emissive = {0, 0, 0, 2};
        Scene.Surfaces.push_back(Facet);
        Scene.Visibility.Corners.push_back({Facet.A, Facet.B, Facet.C});
        Scene.Visibility.OriginalFacets.push_back(Index);
    }
    const auto Enclosure = [](float Low, float High, uint32_t A, uint32_t B, bool Leaf)
    {
        TriangleVisibilitySpace::FloatEnclosure Result{};
        Result.Minimum = {-2, -2, Low - .001f, 0};
        Result.Maximum = {2, 2, High + .001f, 0};
        Result.Links = {A, B, Leaf ? 1u : 0u, 0};
        return Result;
    };
    // 📝 Near leaf 1 is visited first. A capacity-two stack then overflows inside enclosure 3,
    //     after one sheet has already been integrated. Brute-force fallback must start from unity.
    Scene.Visibility.Enclosures = {
        Enclosure(.2f, 1.0f, 1, 2, false),
        Enclosure(.2f, .2f, 0, 1, true),
        Enclosure(.8f, 1.0f, 3, 4, false),
        Enclosure(.8f, .9f, 5, 6, false),
        Enclosure(1.0f, 1.0f, 3, 1, true),
        Enclosure(.8f, .8f, 1, 1, true),
        Enclosure(.9f, .9f, 2, 1, true),
    };
    return Scene;
}

LightComputeArchive BuildReplayManySheetFixture(uint32_t Size)
{
    auto Scene = BuildReplaySegmentFixture(Size);
    const LightComputeFacet Template = Scene.Surfaces[0];
    std::vector<LightComputeFacet> Authored;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    for (uint32_t Index = 0; Index < 9u; ++Index)
    {
        const float Height = .15f + .12f * float(Index);
        LightComputeFacet Facet = Template;
        Facet.A[2] = Facet.B[2] = Facet.C[2] = Height;
        Facet.OpticalToken[3] = float(Index + 2u);
        Authored.push_back(Facet);
        Geometry.push_back({SpatialVector{-2, -2, Height}, SpatialVector{2, -2, Height},
                            SpatialVector{0, 2, Height}});
    }
    TriangleVisibilitySpace Space;
    Space.BuildSurfaceAreaHierarchy(Geometry, 2u);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.resize(Scene.Visibility.OriginalFacets.size());
    for (size_t Index = 0; Index < Scene.Surfaces.size(); ++Index)
        Scene.Surfaces[Index] = Authored[Scene.Visibility.OriginalFacets[Index]];
    return Scene;
}

std::vector<std::array<float, 4>> ExecuteReplaySegment(LightComputeSubmission& Compute,
                                                        const LightComputeArchive& Scene,
                                                        uint32_t StackLimit,
                                                        bool SourceSampling,
                                                        bool PowerMis)
{
    return Compute.ExecuteReplay(Scene, 1u, 72991u, false, false, true, {0, 0, 0}, .01f,
                                 false, false, true, SourceSampling, false, false, StackLimit,
                                 SourceSampling, false, true, false, false, false, false, false,
                                 1u, false, false, false, false, false, true, PowerMis, false, true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               VULKAN READBACK CHECK
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplaySegment(LightComputeSubmission& Compute, const std::string& Shaders, uint32_t Size)
{
    const auto Scene = BuildReplaySegmentFixture(Size);
    const VkPipeline Original = Compute.ReplayResolvePipeline;
    const VkPipeline Segment = Compute.ConstructPipeline(Shaders + "/ReplaySegmentValidation.spv");
    Compute.ReplayResolvePipeline = Segment;
    const auto Full = ExecuteReplaySegment(Compute, Scene, 64u, false, false);
    const auto FullCounts = Compute.ExtendedStatistics();
    const auto Fallback = ExecuteReplaySegment(Compute, Scene, 2u, false, false);
    const auto FallbackCounts = Compute.ExtendedStatistics();
    const auto ManySheets = BuildReplayManySheetFixture(Size);
    const auto ManyFull = ExecuteReplaySegment(Compute, ManySheets, 64u, false, false);
    const auto ManyFallback = ExecuteReplaySegment(Compute, ManySheets, 2u, false, false);
    Compute.ReplayResolvePipeline = Original;
    Compute.DestroyPipeline(Compute.Device, Segment, nullptr);

    const double OneTransmission = (1.0 - .04) / (1.0 + .04) * std::exp(-.4 * .03);
    const double ExpectedTransmission = std::pow(OneTransmission, 4.0);
    const double ExpectedProbability = std::pow(.92, 4.0);
    double FullError = 0, FallbackError = 0, ParityError = 0;
    for (size_t Pixel = 0; Pixel < Full.size(); ++Pixel)
    {
        FullError = std::max(FullError, std::abs(double(Full[Pixel][0]) - ExpectedTransmission));
        FallbackError = std::max(FallbackError, std::abs(double(Fallback[Pixel][0]) - ExpectedTransmission));
        ParityError = std::max(ParityError, std::abs(double(Full[Pixel][0]) - double(Fallback[Pixel][0])));
        Require(Full[Pixel][2] == 4.0f && Fallback[Pixel][2] == 4.0f &&
                    std::abs(double(Full[Pixel][1]) - ExpectedProbability) < 1e-5 &&
                    std::abs(double(Fallback[Pixel][1]) - ExpectedProbability) < 1e-5,
                "Pane probability or represented count differs from the four-sheet reference");
    }
    std::printf("REPLAY_SEGMENT panes=4 full_error=%.9g fallback_error=%.9g max_difference=%.9g "
                "full_overflows=%u fallback_overflows=%u\n",
                FullError, FallbackError, ParityError, FullCounts[3], FallbackCounts[3]);
    Require(FullError < 1e-5 && FallbackError < 1e-5 && ParityError < 1e-5 &&
                FullCounts[3] == 0u && FallbackCounts[3] == Size * Size,
            "Pane stack overflow changed transmittance or failed to exercise fallback");
    const double ExpectedManyTransmission = std::pow(OneTransmission, 9.0);
    const double ExpectedManyProbability = std::pow(.92, 9.0);
    double ManyError = 0;
    for (size_t Pixel = 0; Pixel < ManyFull.size(); ++Pixel)
    {
        ManyError = std::max(ManyError, std::abs(double(ManyFull[Pixel][0]) - ExpectedManyTransmission));
        Require(std::abs(double(ManyFallback[Pixel][0]) - ExpectedManyTransmission) < 1e-5 &&
                    std::abs(double(ManyFull[Pixel][1]) - ExpectedManyProbability) < 1e-5 &&
                    std::abs(double(ManyFallback[Pixel][1]) - ExpectedManyProbability) < 1e-5 &&
                    ManyFull[Pixel][2] == 9.0f && ManyFallback[Pixel][2] == 9.0f,
                "More than eight panes were discarded or integrated incorrectly");
    }
    Require(ManyError < 1e-5, "Nine-pane transmittance differs from the analytic product");
    std::printf("REPLAY_SEGMENT panes=9 full_error=%.9g finite_nonzero=1 fallback_matches=1\n", ManyError);

    const VkPipeline Limits = Compute.ConstructPipeline(Shaders + "/ReplayMisLimits.spv");
    Compute.ReplayResolvePipeline = Limits;
    for (bool Power : {false, true})
    {
        const auto Weights = ExecuteReplaySegment(Compute, Scene, 64u, true, Power);
        for (uint32_t Pixel = 0; Pixel < Weights.size(); ++Pixel)
        {
            const uint32_t Case = Pixel % 4u;
            const float ExpectedSource = Case == 0u ? 1.0f : Case == 1u ? .5f : 0.0f;
            const float ExpectedBsdf = Case == 0u ? 0.0f : Case == 1u ? .5f : Case == 2u ? 1.0f : 0.0f;
            Require(Weights[Pixel][0] == ExpectedSource && Weights[Pixel][1] == ExpectedBsdf &&
                        Weights[Pixel][2] == 1.0f,
                    "Shader MIS weight has an invalid ratio or unreachable-competitor limit");
        }
    }
    Compute.ReplayResolvePipeline = Original;
    Compute.DestroyPipeline(Compute.Device, Limits, nullptr);
    std::printf("REPLAY_MIS_LIMITS balance=4 power=4 matches=1\n");
}
