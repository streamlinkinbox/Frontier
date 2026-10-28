/*==========================================================================================================================================
                                       REPLAYFACETMIXTURESPECTRUMVALIDATION.H
==========================================================================================================================================*/
// 🧩 Exercise one and odd numbers of emitters with colour-coded selection and independent area integrals.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                         NON-POWER-OF-TWO SOURCE FIXTURES
//------------------------------------------------------------------------------------------------------------------------

uint32_t ReplayMixtureColourMask(const std::array<float, 4>& Colour)
{
    return (Colour[0] > 0 ? 1u : 0u) | (Colour[1] > 0 ? 2u : 0u) | (Colour[2] > 0 ? 4u : 0u);
}

LightComputeArchive ReplayMixtureSpectrumFixture(uint32_t SourceCount)
{
    Require(SourceCount == 1 || SourceCount == 3 || SourceCount == 5, "Unexpected spectrum fixture size");
    auto Scene = DirectGpuFixture(false);
    Scene.Lights.clear();
    for (auto& Receiver : Scene.Receivers)
        if (Receiver.PositionHit[3] != 0) Receiver.AlbedoIor = {.5f, .5f, .5f, 1.5f};
    std::vector<std::array<SpatialVector, 3>> Geometry;
    for (uint32_t Index = 0; Index < SourceCount; ++Index)
    {
        const double X = (double(Index) - .5 * (SourceCount - 1)) * .6;
        const double HalfWidth = .16 + .015 * Index, HalfHeight = .18 + .015 * Index;
        Geometry.push_back({{{X - HalfWidth, -HalfHeight, 1}, {X + HalfWidth, -HalfHeight, 1},
                             {X, HalfHeight, 1}}});
    }
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    const std::array<std::array<float, 4>, 5> Colours{{
        {1, 0, 0, 0}, {0, .025f, 0, 0}, {0, 0, 20, 0}, {.04f, .04f, 0, 0}, {2, 0, 2, 0}}};
    for (size_t Index = 0; Index < Scene.Visibility.Corners.size(); ++Index)
    {
        const auto& T = Scene.Visibility.Corners[Index];
        LightComputeFacet Facet;
        Facet.A = T.A; Facet.B = T.B; Facet.C = T.C;
        Facet.NormalA = Facet.NormalB = Facet.NormalC = {0, 0, -1, 0};
        Facet.AlbedoRoughness = {0, 0, 0, 1};
        Facet.OpticalToken = {0, 1.5f, 0, 1};
        Facet.Emissive = Colours[Scene.Visibility.OriginalFacets[Index]];
        Scene.Surfaces.push_back(Facet);
    }
    return Scene;
}

// 📝 Midpoint quadrature over each planar triangle; no renderer sampling or CDF is used.
std::array<double, 3> ReplayMixtureSpectrumIntegral(const LightComputeArchive& Scene, uint32_t Steps)
{
    constexpr double Pi = 3.14159265358979323846, Height = .999;
    std::array<double, 3> Integral{};
    for (const auto& Facet : Scene.Surfaces)
    {
        const double Ax = Facet.A[0], Ay = Facet.A[1], Bx = Facet.B[0], By = Facet.B[1];
        const double Cx = Facet.C[0], Cy = Facet.C[1];
        const double Area = .5 * std::abs((Bx - Ax) * (Cy - Ay) - (By - Ay) * (Cx - Ax));
        double Sum = 0;
        for (uint32_t U = 0; U < Steps; ++U)
            for (uint32_t V = 0; V < Steps; ++V)
            {
                const double Root = std::sqrt((U + .5) / Steps), Along = (V + .5) / Steps;
                const double X = Ax * (1 - Root) + Bx * Root * (1 - Along) + Cx * Root * Along;
                const double Y = Ay * (1 - Root) + By * Root * (1 - Along) + Cy * Root * Along;
                const double DistanceSquared = X * X + Y * Y + Height * Height;
                Sum += Height * Height / (DistanceSquared * DistanceSquared);
            }
        const double Factor = .5 / Pi * Area * Sum / (double(Steps) * Steps);
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
            Integral[Channel] += Factor * Facet.Emissive[Channel];
    }
    return Integral;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          COLOUR-CODED SELECTION CHECKS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayFacetMixtureSpectrum(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Batches = 16, CentresPerBatch = 1024;
    for (uint32_t SourceCount : {1u, 3u, 5u})
    {
        const auto Scene = ReplayMixtureSpectrumFixture(SourceCount);
        const auto Exact = ReplayMixtureSpectrumIntegral(Scene, 512);
        const auto Coarse = ReplayMixtureSpectrumIntegral(Scene, 256);
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
            Require(std::abs(Exact[Channel] - Coarse[Channel]) < 2e-5 * Exact[Channel] + 1e-8,
                    "Spectrum reference quadrature has not converged");
        const auto Distribution = CompileRadiantFacetDistribution(Scene.Surfaces);
        ValidateRadiantFacetDistribution(Distribution, 0, Scene.Surfaces.size());
        Require(Distribution[0] == SourceCount, "Spectrum CDF has an unexpected source count");
        std::array<double, 8> Expected{};
        for (uint32_t Index = 0; Index < SourceCount; ++Index)
        {
            const uint32_t Facet = uint32_t(Distribution[1 + 4 * Index]);
            const uint32_t Mask = ReplayMixtureColourMask(Scene.Surfaces[Facet].Emissive);
            Require(Mask != 0 && Expected[Mask] == 0, "Spectrum source colours are not distinct");
            Expected[Mask] = .5 * double(Distribution[2 + 4 * Index]) + .5 / SourceCount;
        }
        LightComputeSubmission Compute(Driver);
        Compute.Prepare(Scene, Shaders, false, false, false, false, true);
        ReplayGroupedMoments Radiance;
        std::array<DirectGpuMoments, 8> Frequencies;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            const auto Image = TraceReplayPower(Compute, Scene, 1, 860001 + SourceCount * 10000 + 103 * Batch,
                                                false, false, true, true, false, false, false, false,
                                                {0, 0, 0}, true);
            Radiance.Accumulate(Image, Scene.Width);
            std::array<uint32_t, 8> Counts{};
            for (uint32_t Y = 0; Y < 32; ++Y)
                for (uint32_t X = 0; X < 32; ++X)
                {
                    const uint32_t Mask = ReplayMixtureColourMask(Image[size_t(3 * Y + 1) * Scene.Width + 3 * X + 1]);
                    Require(Mask != 0 && Expected[Mask] > 0, "A spectrum sample has no matching visible source");
                    ++Counts[Mask];
                }
            for (uint32_t Mask = 1; Mask < 8; ++Mask)
                if (Expected[Mask] > 0) Frequencies[Mask].Accumulate(double(Counts[Mask]) / CentresPerBatch);
        }
        for (uint32_t Mask = 1; Mask < 8; ++Mask)
            if (Expected[Mask] > 0)
            {
                const double BatchSe = ReplaySampleStandardError(Frequencies[Mask]);
                const double BinomialSe = std::sqrt(Expected[Mask] * (1 - Expected[Mask]) /
                                                    (Batches * CentresPerBatch));
                std::printf("MIXTURE_SPECTRUM sources=%u mask=%u expected_probability=%.9g observed=%.9g batch_se=%.9g\n",
                            SourceCount, Mask, Expected[Mask], Frequencies[Mask].Mean(), BatchSe);
                Require(std::abs(Frequencies[Mask].Mean() - Expected[Mask]) <
                            6 * std::max(BatchSe, BinomialSe) + .003,
                        "Colour-coded source selection disagrees with the mixture probability");
            }
        for (uint32_t Channel = 0; Channel < 3; ++Channel)
        {
            const double BatchSe = ReplaySampleStandardError(Radiance.Batches[Channel]);
            const double PixelSe = ReplaySampleStandardError(Radiance.Pixels[Channel]);
            std::printf("MIXTURE_SPECTRUM sources=%u channel=%u exact=%.9g mean=%.9g batch_se=%.9g\n",
                        SourceCount, Channel, Exact[Channel], Radiance.Pixels[Channel].Mean(), BatchSe);
            Require(std::abs(Radiance.Pixels[Channel].Mean() - Exact[Channel]) <
                        6 * std::max(PixelSe, BatchSe) + .003 * Exact[Channel] + 1e-7,
                    "Spectrum mixture disagrees with independent triangle-area quadrature");
        }
        if (SourceCount == 5)
        {
            auto Empty = DirectGpuFixture(false);
            Empty.Lights.clear();
            const auto Image = TraceReplayPower(Compute, Empty, 1, 860999, false, false, true, true,
                                                true, false, false, false, {.6f, .6f, .6f}, true);
            const double Mean = DirectGpuCentres(Image, Empty.Width).Mean();
            Require(std::abs(Mean - .36) < 1e-6, "Mixture empty-source environment fallback failed");
            std::printf("MIXTURE_SPECTRUM empty_environment=%.9g\n", Mean);
        }
    }
    std::printf("PASS colour-coded one, three and five-source mixture, area integrals and empty fallback\n");
}
} // namespace
