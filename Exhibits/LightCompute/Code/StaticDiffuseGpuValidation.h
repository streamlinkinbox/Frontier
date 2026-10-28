/*==========================================================================================================================================
                                               STATICDIFFUSEGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Vulkan cosine-path reservoirs compared with independent area integrals; identical domains only.
#pragma once

namespace
{
LightComputeArchive DiffuseGpuFixture(double HalfWidth, bool TwoBounce, bool Colour = false)
{
    auto Scene = DirectGpuFixture(false);
    Scene.Lights.clear();
    for (auto& R : Scene.Receivers)
        if (R.PositionHit[3] != 0) R.AlbedoIor = {.5f, .5f, .5f, 1.5f};
    const double A = HalfWidth;
    const SpatialVector L0{-A,-A,1}, L1{0,-A,1}, L2{0,A,1}, L3{-A,A,1};
    const SpatialVector R0{0,-A,1}, R1{A,-A,1}, R2{A,A,1}, R3{0,A,1};
    const SpatialVector B0{-A,-A,-1}, B1{A,-A,-1}, B2{A,A,-1}, B3{-A,A,-1};
    TriangleVisibilitySpace Space;
    Space.Build({{L0,L1,L2}, {L0,L2,L3}, {R0,R1,R2}, {R0,R2,R3}, {B0,B1,B2}, {B0,B2,B3}});
    Scene.Visibility = Space.PackFloat32();
    for (size_t I = 0; I < Scene.Visibility.Corners.size(); ++I)
    {
        const auto& C = Scene.Visibility.Corners[I];
        const auto Original = Scene.Visibility.OriginalFacets[I];
        const bool Top = Original < 4;
        LightComputeFacet F;
        F.A = C.A; F.B = C.B; F.C = C.C;
        F.NormalA = F.NormalB = F.NormalC = Top ? std::array<float,4>{0,0,-1,0} : std::array<float,4>{0,0,1,0};
        F.AlbedoRoughness = Top && TwoBounce ? std::array<float,4>{.5f,.5f,.5f,1} : std::array<float,4>{0,0,0,1};
        F.OpticalToken = {0,1.5f,0,1};
        if ((Top && !TwoBounce) || (!Top && TwoBounce))
            F.Emissive = Colour && Top ? (Original < 2 ? std::array<float,4>{2,0,0,0} : std::array<float,4>{0,0,2,0})
                                       : std::array<float,4>{2,2,2,0};
        Scene.Surfaces.push_back(F);
    }
    return Scene;
}
double DiffuseSquareAreaIntegral(double HalfWidth, double Height)
{
    constexpr uint32_t Steps = 1200;
    const double Delta = 2 * HalfWidth / Steps;
    double Sum = 0;
    for (uint32_t Y = 0; Y < Steps; ++Y)
        for (uint32_t X = 0; X < Steps; ++X)
        {
            const double U = -HalfWidth + (X + .5) * Delta, V = -HalfWidth + (Y + .5) * Delta;
            const double Squared = U * U + V * V + Height * Height;
            Sum += Height * Height / (Squared * Squared);
        }
    return Sum * Delta * Delta / 3.14159265358979323846;
}
void ValidateStaticDiffuseGpu(const char* Driver, const std::string& Shaders)
{
    auto Scene = DiffuseGpuFixture(.5, false);
    LightComputeSubmission Compute(Driver);
    std::printf("DIFFUSE preparing Vulkan pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Scene, Shaders, false, false, true);
    const double Expected = DiffuseSquareAreaIntegral(.5, .999); // .5 albedo × 2 emission.
    DirectGpuMoments FreshMoments, SpatialMoments;
    uint32_t Links = 0;
    for (uint32_t Seed : {71u,101u,131u,161u})
    {
        const auto Fresh = Compute.ExecuteDiffuse(Scene, 1, Seed, false, false, true);
        const auto Spatial = Compute.ExecuteDiffuse(Scene, 1, Seed, false, true, true);
        Links += Compute.ExtendedStatistics()[22];
        for (uint32_t Y = 0; Y < 32; ++Y)
            for (uint32_t X = 0; X < 32; ++X)
            {
                const size_t I = size_t(3 * Y + 1) * Scene.Width + 3 * X + 1;
                FreshMoments.Accumulate(Fresh[I][0]); SpatialMoments.Accumulate(Spatial[I][0]);
            }
    }
    std::printf("DIFFUSE_STATIC area_exact=%.12g fresh=%.12g se=%.12g spatial=%.12g se=%.12g "
                "std_ratio=%.9g spatial_links=%u n=%u seeds=4\n", Expected, FreshMoments.Mean(),
                FreshMoments.StandardError(), SpatialMoments.Mean(), SpatialMoments.StandardError(),
                SpatialMoments.Deviation() / FreshMoments.Deviation(), Links, SpatialMoments.Count);
    Require(std::abs(FreshMoments.Mean() - Expected) < 6 * FreshMoments.StandardError() + .002 &&
            std::abs(SpatialMoments.Mean() - Expected) < 6 * SpatialMoments.StandardError() + .002 &&
            SpatialMoments.Deviation() < .65 * FreshMoments.Deviation() && Links > 0,
            "Same-domain spatial diffuse energy or variance failed");
    std::vector<std::array<float,4>> Temporal;
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
        Temporal = Compute.ExecuteDiffuse(Scene, 1, 500 + Frame, true, false, Frame == 0);
    const auto TemporalCounts = Compute.ExtendedStatistics();
    const auto TemporalMoments = DirectGpuCentres(Temporal, Scene.Width);
    std::printf("DIFFUSE_TEMPORAL frames=8 exact=%.12g mean=%.12g se=%.12g std_ratio=%.9g accepted=%u\n",
                Expected, TemporalMoments.Mean(), TemporalMoments.StandardError(),
                TemporalMoments.Deviation() / FreshMoments.Deviation(), TemporalCounts[20]);
    Require(TemporalCounts[20] > 0 &&
            std::abs(TemporalMoments.Mean() - Expected) < 6 * TemporalMoments.StandardError() + .002 &&
            TemporalMoments.Deviation() < .65 * FreshMoments.Deviation(), "Static temporal path pooling failed");
    auto Shifted = Scene;
    for (uint32_t Y = 0; Y < Shifted.Height; ++Y)
        for (uint32_t X = 0; X < Shifted.Width; ++X)
        {
            auto& R = Shifted.Receivers[size_t(Y) * Shifted.Width + X];
            if (R.PositionHit[3] != 0) R.MotionPixel[0] = float(X < 93 ? X + 3 : X - 3);
        }
    const auto Reprojected = Compute.ExecuteDiffuse(Shifted, 1, 700, true, false);
    const auto Counts = Compute.ExtendedStatistics();
    const auto ReprojMoments = DirectGpuCentres(Reprojected, Shifted.Width);
    const auto Cold = Compute.ExecuteDiffuse(Shifted, 1, 700, true, false, true);
    const auto ColdMoments = DirectGpuCentres(Cold, Shifted.Width);
    std::printf("DIFFUSE_REPROJECT accepted=%u rejected=%u reuse_std=%.12g cold_std=%.12g\n",
                Counts[20], Counts[21], ReprojMoments.Deviation(), ColdMoments.Deviation());
    Require(Counts[20] > 0 && Counts[21] == 0 && ReprojMoments.Deviation() < ColdMoments.Deviation() &&
            std::abs(ReprojMoments.Mean() - Expected) < 6 * ReprojMoments.StandardError() + .002,
            "Identical diffuse domain did not accept pixel reprojection");
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
        (void)Compute.ExecuteDiffuse(Scene, 1, 800 + Frame, true, false, Frame == 0);
    auto Moving = Scene;
    for (auto& R : Moving.Receivers) if (R.PositionHit[3] != 0) R.PositionHit[0] += .2f;
    const auto Moved = Compute.ExecuteDiffuse(Moving, 1, 900, true, false);
    const auto MovingCounts = Compute.ExtendedStatistics();
    const bool MovingReset = Compute.LastDiffuseReset;
    const auto MovingCold = Compute.ExecuteDiffuse(Moving, 1, 900, true, false, true);
    std::printf("DIFFUSE_MOVING rejected=%u global_reset=%u matches_cold=%u\n",
                MovingCounts[21], MovingReset ? 1u : 0u, Moved == MovingCold ? 1u : 0u);
    Require(!MovingReset && MovingCounts[21] > 0 && Moved == MovingCold, "Moved receiver did not reject history");
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Compute.ExecuteDiffuse(Scene, 1, 910 + Frame, true, false, Frame == 0);
    auto Micro = Scene;
    for (auto& R : Micro.Receivers) if (R.PositionHit[3] != 0) R.PositionHit[0] += 5e-7f;
    const auto MicroResult = Compute.ExecuteDiffuse(Micro, 1, 920, true, false);
    const auto MicroCounts = Compute.ExtendedStatistics();
    const auto MicroCold = Compute.ExecuteDiffuse(Micro, 1, 920, true, false, true);
    std::printf("DIFFUSE_SUBTOLERANCE position_5e-7 rejected=%u matches_cold=%u\n",
                MicroCounts[21], MicroResult == MicroCold ? 1u : 0u);
    Require(MicroCounts[21] > 0 && MicroResult == MicroCold, "Tiny changed domain did not reject history");
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Compute.ExecuteDiffuse(Scene, 1, 950 + Frame, true, false, Frame == 0);
    auto Dark = Scene;
    for (auto& F : Dark.Surfaces) F.Emissive = {0,0,0,0};
    const auto Invalidated = Compute.ExecuteDiffuse(Dark, 1, 960, true, false);
    const bool SceneReset = Compute.LastDiffuseReset;
    const auto InvalidatedCold = Compute.ExecuteDiffuse(Dark, 1, 960, true, false, true);
    Require(SceneReset && Invalidated == InvalidatedCold && DirectGpuCentres(Invalidated, Dark.Width).Mean() == 0,
            "Changed emission did not invalidate history");
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Compute.ExecuteDiffuse(Scene, 1, 970 + Frame, true, false, Frame == 0);
    const auto Narrow = DiffuseGpuFixture(.4, false);
    const auto Resized = Compute.ExecuteDiffuse(Narrow, 1, 980, true, false);
    const bool GeometryReset = Compute.LastDiffuseReset;
    const auto ResizedCold = Compute.ExecuteDiffuse(Narrow, 1, 980, true, false, true);
    Require(GeometryReset && Resized == ResizedCold, "Changed geometry did not invalidate history");
    std::printf("DIFFUSE_REVISION emission_reset=%u geometry_reset=%u matches_cold=1\n",
                SceneReset ? 1u : 0u, GeometryReset ? 1u : 0u);
    const auto Colour = DiffuseGpuFixture(.5, false, true);
    const auto Coloured = Compute.ExecuteDiffuse(Colour, 1, 1234, false, true, true);
    DirectGpuMoments Red, Blue;
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
        {
            const auto& C = Coloured[size_t(3 * Y + 1) * Colour.Width + 3 * X + 1];
            Red.Accumulate(C[0]); Blue.Accumulate(C[2]);
        }
    std::printf("DIFFUSE_COLOUR exact_per_channel=%.12g red=%.12g red_se=%.12g blue=%.12g blue_se=%.12g\n",
                Expected / 2, Red.Mean(), Red.StandardError(), Blue.Mean(), Blue.StandardError());
    Require(std::abs(Red.Mean() - Expected / 2) < 6 * Red.StandardError() + .002 &&
            std::abs(Blue.Mean() - Expected / 2) < 6 * Blue.StandardError() + .002, "RGB energy failed");
    const auto Multiple = DiffuseGpuFixture(1000, true);
    const auto OneHit = Compute.ExecuteDiffuse(Multiple, 1, 1500, false, false, true);
    const auto TwoHits = Compute.ExecuteDiffuse(Multiple, 2, 1500, true, false);
    const auto OneMoments = DirectGpuCentres(OneHit, Multiple.Width), TwoMoments = DirectGpuCentres(TwoHits, Multiple.Width);
    const bool BounceReset = Compute.LastDiffuseReset;
    std::printf("DIFFUSE_MULTIBOUNCE one_hit=%.12g two_hits=%.12g expected_nearly=0.5 bounce_reset=%u\n",
                OneMoments.Mean(), TwoMoments.Mean(), BounceReset ? 1u : 0u);
    Require(OneMoments.Mean() == 0 && std::abs(TwoMoments.Mean() - .5) < .002 && BounceReset,
            "Two-hit diffuse transport failed bounded plane result or budget reset");
    std::printf("PASS static diffuse path reservoirs; general dynamic/spatial ReSTIR GI remains open\n");
}
} // namespace
