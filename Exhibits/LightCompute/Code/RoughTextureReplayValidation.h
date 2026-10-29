/*==========================================================================================================================================
                                               ROUGHTEXTUREREPLAYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Check guarded textured GGX replay against independent primary and multibounce path references.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                  MATERIAL AND SAMPLING FIXTURES
//------------------------------------------------------------------------------------------------------------------------

void SetRoughTextureReplayMaterials(LightComputeArchive& Scene, float Roughness, float Metallic)
{
    Scene.Angles[11] = Roughness;
    Scene.Angles[12] = Metallic;
    for (auto& F : Scene.Surfaces)
    {
        F.AlbedoRoughness[3] = .75f;
        F.OpticalToken[2] = F.OpticalToken[3] == 6 ? 0 : .5f;
        if (F.OpticalToken[3] == 2 || F.OpticalToken[3] == 4)
            F.OpticalToken[0] = F.OpticalToken[3] == 2 ? .25f : .6f;
    }
}

std::vector<std::array<float,4>> TraceRoughTextureReplay(LightComputeSubmission& Compute,
                                                          const LightComputeArchive& Scene, uint32_t Hits,
                                                          uint32_t Seed, bool Temporal, bool Spatial, bool Reset,
                                                          bool Primary = false, bool Facets = true, bool Balance = true,
                                                          bool ImportanceRough = false, bool PowerWeighted = false,
                                                          bool UniformFacetMixture = false)
{
    return Compute.ExecuteReplay(Scene, Hits, Seed, Temporal, Spatial, Reset, {.025f,.025f,.025f}, .01f,
                                 Primary, false, true, Facets, false, false, 64, Balance, true, ImportanceRough,
                                 PowerWeighted, UniformFacetMixture);
}

LightComputeArchive RoughTexturedRoom(uint32_t Palette, float Blocker, float Roughness, float Metallic)
{
    auto Scene = ReplayTexturedRoom(Palette, Blocker);
    SetRoughTextureReplayMaterials(Scene, Roughness, Metallic);
    auto Receivers = DirectGpuFixture(false);
    for (auto& R : Receivers.Receivers)
        if (R.PositionHit[3] != 0)
        {
            R.PositionHit = {1.4f,2,0,1}; R.MotionPositionToken = {1.4f,2,0,1};
            R.NormalRoughness = {0,0,1,.7f}; R.ViewMetallic = {0,0,1,.2f};
            R.AlbedoIor = {.7f,.7f,.7f,1.5f}; R.ResidualSpecular = {0,0,0,.5f};
        }
    Scene.Width = Receivers.Width;
    Scene.Height = Receivers.Height;
    Scene.Receivers = Receivers.Receivers;
    return Scene;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FOUR-HIT REFERENCE AND REUSE
//------------------------------------------------------------------------------------------------------------------------

void ValidateRoughTextureRoom(const char* Driver, const std::string& Shaders)
{
    const auto First = RoughTexturedRoom(0, 7, .65f, .35f);
    const auto Relit = RoughTexturedRoom(1, 7, .48f, .7f);
    const auto Covered = RoughTexturedRoom(1, 0, .48f, .7f);
    auto Dark = Covered;
    for (size_t I = 272; I < Dark.Angles.size(); ++I) Dark.Angles[I] = .05f;
    std::printf("ROUGH_TEXTURE_ROOM tracing three independent 400000-path CPU references\n");
    std::fflush(stdout);
    const auto CpuFirst = ReplayReferenceIntegrate(First, 4, 218391, true, true);
    const auto CpuRelit = ReplayReferenceIntegrate(Relit, 4, 430921, true, true);
    const auto CpuCovered = ReplayReferenceIntegrate(Covered, 4, 839127, true, true);
    ReplayRgbMoments Difference;
    std::mt19937_64 TexturedRandom(193231), DarkRandom(193231);
    const auto& Receiver = Covered.Receivers[size_t(Covered.Width) + 1];
    for (uint32_t I = 0; I < 120000; ++I)
    {
        const auto Textured = ReplayReferencePath(Covered, Receiver, 4, TexturedRandom, true, true);
        const auto Flat = ReplayReferencePath(Dark, Receiver, 4, DarkRandom, true, true);
        for (uint32_t C = 0; C < 3; ++C) Difference[C].Accumulate(Textured[C] - Flat[C]);
    }
    bool CpuInfluence = false;
    for (uint32_t C = 0; C < 3; ++C)
    {
        const auto& M = Difference[C];
        CpuInfluence |= std::abs(M.Mean()) > 6 * M.StandardError() + 1e-5;
        std::printf("ROUGH_TEXTURE_CONTROL channel=%u cpu_difference=%.9g cpu_se=%.9g n=%u\n",
                    C, M.Mean(), M.StandardError(), M.Count);
    }
    Require(CpuInfluence, "Rough textured fixture did not exercise a secondary albedo change");
    LightComputeSubmission Compute(Driver);
    std::printf("ROUGH_TEXTURE_ROOM preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(First, Shaders, false, false, false, false, true);
    const auto OneFirst = TraceRoughTextureReplay(Compute, First, 1, 210001, false, false, true);
    const auto OneRelit = TraceRoughTextureReplay(Compute, Relit, 1, 210001, false, false, true);
    Require(OneFirst == OneRelit, "Secondary atlas change modified the manual-receiver one-hit control");
    ReplayGroupedMoments FreshFirst, FacetRelit, ReusedRelit, ReusedCovered, ReopenedFirst, GpuDifference;
    uint32_t Changed = 0, Blocked = 0, Reopened = 0, Spatial = 0;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 211000 + Batch * 100;
        FreshFirst.Accumulate(TraceRoughTextureReplay(Compute, First, 4, Seed, false, false, true), First.Width);
        FacetRelit.Accumulate(TraceRoughTextureReplay(Compute, Relit, 4, Seed + 1, false, false, true,
                                                      false, true, false), First.Width);
        for (uint32_t Frame = 0; Frame < 6; ++Frame)
            (void)TraceRoughTextureReplay(Compute, First, 4, Seed + 10 + Frame, true, false, Frame == 0);
        ReusedRelit.Accumulate(TraceRoughTextureReplay(Compute, Relit, 4, Seed + 20, true, true, false), First.Width);
        auto Counts = Compute.ExtendedStatistics(); Changed += Counts[20]; Spatial += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1, "Rough atlas revision lost capped history");
        ReusedCovered.Accumulate(TraceRoughTextureReplay(Compute, Covered, 4, Seed + 21, true, true, false), First.Width);
        Counts = Compute.ExtendedStatistics(); Blocked += Counts[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1, "Rough blocker revision lost capped history");
        ReopenedFirst.Accumulate(TraceRoughTextureReplay(Compute, First, 4, Seed + 22, true, true, false), First.Width);
        Counts = Compute.ExtendedStatistics(); Reopened += Counts[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1, "Rough reopening lost capped history");
    }
    for (uint32_t Batch = 0; Batch < 24; ++Batch)
    {
        const uint32_t Seed = 214000 + 100 * Batch;
        auto Textured = TraceRoughTextureReplay(Compute, Covered, 4, Seed, false, false, true, false, false, false);
        const auto Flat = TraceRoughTextureReplay(Compute, Dark, 4, Seed, false, false, true, false, false, false);
        for (size_t I = 0; I < Textured.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C) Textured[I][C] -= Flat[I][C];
        GpuDifference.Accumulate(Textured, Covered.Width);
    }
    ReplayCompareRgb("rough_texture_first_fresh", CpuFirst, FreshFirst);
    ReplayCompareRgb("rough_texture_changed_facet", CpuRelit, FacetRelit);
    ReplayCompareRgb("rough_texture_changed_reused", CpuRelit, ReusedRelit);
    ReplayCompareRgb("rough_texture_blocked_reused", CpuCovered, ReusedCovered);
    ReplayCompareRgb("rough_texture_reopened_reused", CpuFirst, ReopenedFirst);
    ReplayCompareRgb("rough_texture_paired_control", Difference, GpuDifference);
    bool GpuInfluence = false;
    for (uint32_t C = 0; C < 3; ++C)
    {
        const auto& M = GpuDifference.Batches[C];
        const double Se = M.StandardError() * std::sqrt(double(M.Count) / (M.Count - 1));
        GpuInfluence |= std::abs(GpuDifference.Pixels[C].Mean()) > 5 * Se + 1e-5;
    }
    Require(GpuInfluence && Changed == 8 * 5120 && Blocked == 8 * 5120 && Reopened == 8 * 5120 && Spatial > 0,
            "Rough texture controls or temporal/spatial reuse were not exercised");
    std::printf("ROUGH_TEXTURE_ROOM_LINKS atlas=%u blocker=%u reopening=%u spatial=%u one_hit_equal=1\n",
                Changed, Blocked, Reopened, Spatial);
    std::printf("PASS guarded rough texture four-hit room, atlas revision and moving blocker\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  TEXTURED PRIMARY SURFACE REFERENCE
//------------------------------------------------------------------------------------------------------------------------

void ValidateRoughTextureQuadrature(const char* Driver, const std::string& Shaders)
{
    auto Scene = ReplayTexturedMotionRigid(0, 0, 1, 7, 64, 64);
    SetRoughTextureReplayMaterials(Scene, .6f, .4f);
    std::vector<LightComputeFacet> Floor;
    std::vector<std::array<SpatialVector,3>> Geometry;
    for (const auto& F : Scene.Surfaces)
        if (F.OpticalToken[3] == 1)
        {
            Floor.push_back(F);
            Geometry.push_back({SpatialVector{F.A[0],F.A[1],F.A[2]}, SpatialVector{F.B[0],F.B[1],F.B[2]},
                                SpatialVector{F.C[0],F.C[1],F.C[2]}});
        }
    Require(Floor.size() == 2, "Textured quadrature floor is missing");
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.clear();
    for (uint32_t Original : Scene.Visibility.OriginalFacets) Scene.Surfaces.push_back(Floor[Original]);
    std::vector<uint32_t> Candidates;
    std::vector<ReplayPrimaryReference> References(Scene.Receivers.size());
    for (uint32_t Y = 0; Y < Scene.Height; ++Y)
        for (uint32_t X = 0; X < Scene.Width; ++X)
        {
            const uint32_t I = Y * Scene.Width + X;
            References[I] = TraceReplayPrimaryReference(Scene, X, Y);
            const auto& P = References[I];
            if (P.Hit && !P.NearMipBoundary && P.Receiver.ViewMetallic[2] > .1 &&
                std::min({P.BaryU, P.BaryV, 1-P.BaryU-P.BaryV}) > .01) Candidates.push_back(I);
        }
    Require(Candidates.size() >= 16, "Textured quadrature scene has too few primary probes");
    std::array<uint32_t,16> Probes{};
    std::array<double,3> Expected{}, Coarse{}, Wrong{};
    for (uint32_t I = 0; I < Probes.size(); ++I)
    {
        Probes[I] = Candidates[(size_t(2 * I + 1) * Candidates.size()) / (2 * Probes.size())];
        const auto& R = References[Probes[I]].Receiver;
        const auto Fine = RoughReplayQuadrature(R, 256), Lower = RoughReplayQuadrature(R, 128);
        auto Legacy = R;
        Legacy.ExpectedDirect[3] = 0;
        Legacy.ResidualSpecular[3] = .5f;
        const auto DifferentModel = RoughReplayQuadrature(Legacy, 256);
        for (uint32_t C = 0; C < 3; ++C)
        {
            Expected[C] += Fine[C] / Probes.size();
            Coarse[C] += Lower[C] / Probes.size();
            Wrong[C] += DifferentModel[C] / Probes.size();
        }
    }
    double WrongModelDifference = 0;
    for (uint32_t C = 0; C < 3; ++C)
    {
        Require(std::abs(Expected[C] - Coarse[C]) < 1e-4, "Textured rough quadrature did not converge");
        WrongModelDifference = std::max(WrongModelDifference, std::abs(Expected[C] - Wrong[C]));
    }
    Require(WrongModelDifference > .001, "Quadrature probes cannot distinguish authored and compact BRDFs");
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    ReplayRgbMoments Batches;
    for (uint32_t Batch = 0; Batch < 128; ++Batch)
    {
        const auto Image = Compute.ExecuteReplay(Scene, 1, 215000 + 101 * Batch, false, false, true, {1,1,1},
                                                  .01f, true, false, true, false, false, false, 64, false, true);
        for (uint32_t C = 0; C < 3; ++C)
        {
            double Mean = 0;
            for (uint32_t Pixel : Probes) Mean += Image[Pixel][C] / Probes.size();
            Batches[C].Accumulate(Mean);
        }
    }
    for (uint32_t C = 0; C < 3; ++C)
    {
        const double Se = ReplaySampleStandardError(Batches[C]);
        std::printf("ROUGH_TEXTURE_QUADRATURE channel=%u cpu=%.9g vulkan=%.9g batch_se=%.9g "
                    "legacy_wrong=%.9g\n", C, Expected[C], Batches[C].Mean(), Se, Wrong[C]);
        Require(std::abs(Expected[C] - Batches[C].Mean()) < 6 * Se + .001,
                "Textured authored GGX disagrees with independent hemispherical quadrature");
    }
    std::printf("ROUGH_TEXTURE_QUADRATURE probes=%zu batches=%u wrong_model_max_difference=%.9g\n",
                Probes.size(), Batches[0].Count, WrongModelDifference);
    std::printf("PASS textured authored GGX against converged hemisphere quadrature\n");
}

void ValidateRoughTexturePrimary(const char* Driver, const std::string& Shaders)
{
    constexpr uint32_t Width = 64, Height = 64, CpuPaths = 240000, Batches = 128;
    auto Scene = ReplayTexturedMotionRigid(0, 0, 1, 7, Width, Height);
    SetRoughTextureReplayMaterials(Scene, .6f, .4f);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    (void)TraceRoughTextureReplay(Compute, Scene, 1, 220001, false, false, true, true);
    const auto Guides = Compute.PrimaryReceivers();
    const auto Reference = ValidateReplayPrimaryReference(Scene, Guides, "rough_texture");
    uint32_t Textured = 0;
    double ParameterError = 0;
    for (size_t I = 0; I < Reference.size(); ++I)
    {
        if (!Reference[I].Hit || Scene.Surfaces[Reference[I].Facet].Emissive[3] <= 0) continue;
        ++Textured;
        const auto& A = Guides[I]; const auto& R = Reference[I].Receiver;
        ParameterError = std::max({ParameterError, std::abs(double(A.NormalRoughness[3]) - R.NormalRoughness[3]),
                                   std::abs(double(A.ViewMetallic[3]) - R.ViewMetallic[3]),
                                   std::abs(double(A.AlbedoIor[3]) - R.AlbedoIor[3]),
                                   std::abs(double(A.ExpectedDirect[3]) - R.ExpectedDirect[3])});
    }
    Require(Textured > 400 && ParameterError < 1e-6, "Rough atlas primary coefficients differ from independent rays");
    const std::array<uint32_t,2> Pixels{
        SelectReplayMotionProbe(Scene, Guides, Guides, Reference, 8, 28, 38),
        SelectReplayMotionProbe(Scene, Guides, Guides, Reference, 1, 46, 50)};
    std::array<ReplayRgbMoments,2> Cpu, Gpu;
    std::printf("ROUGH_TEXTURE_PRIMARY tracing %u independent four-hit paths for each of two pixels\n", CpuPaths);
    std::fflush(stdout);
    for (uint32_t I = 0; I < Pixels.size(); ++I)
    {
        std::mt19937_64 Random(221001 + I);
        const auto& Primary = Reference[Pixels[I]].Receiver;
        for (uint32_t J = 0; J < CpuPaths; ++J)
        {
            const auto Radiance = ReplayReferencePath(Scene, Primary, 4, Random, true, true);
            for (uint32_t C = 0; C < 3; ++C) Cpu[I][C].Accumulate(Radiance[C] + Primary.ResidualSpecular[C]);
        }
    }
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const auto Image = TraceRoughTextureReplay(Compute, Scene, 4, 222000 + 101 * Batch,
                                                    false, false, true, true);
        for (uint32_t I = 0; I < Pixels.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C) Gpu[I][C].Accumulate(Image[Pixels[I]][C]);
    }
    CompareReplayMotionProbe("rough_primary_object", Cpu[0], Gpu[0]);
    CompareReplayMotionProbe("rough_primary_floor", Cpu[1], Gpu[1]);
    std::printf("ROUGH_TEXTURE_PRIMARY pixels=%u/%u textured=%u parameter_error=%.9g cpu_paths=%u gpu_batches=%u\n",
                Pixels[0], Pixels[1], Textured, ParameterError, CpuPaths, Batches);
    std::printf("PASS rough-textured primary and four-hit paths against independent rays\n");
}
} // namespace
