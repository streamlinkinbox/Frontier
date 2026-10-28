/*==========================================================================================================================================
                                                REPLAYTEXTUREVALIDATION.H
==========================================================================================================================================*/
// 🧩 Opaque diffuse texture-atlas replay fixtures, analytic bounce checks and guarded-input tests.
#pragma once

namespace
{
using ReplayTextureColour = std::array<float,3>;
void ReplayInstallTexture(LightComputeArchive& Scene, const std::vector<ReplayTextureColour>& Pixels, uint32_t Dimension)
{
    Require(Dimension > 0 && (Dimension & (Dimension - 1)) == 0 && Pixels.size() == size_t(Dimension) * Dimension,
            "Invalid texture fixture");
    Scene.Angles.assign(272, 0);
    Scene.Angles[0] = 1;       // One stratum, opaque transport.
    Scene.Angles[2] = 268;     // Absolute float offset of the texture header.
    Scene.Angles[4] = Scene.Angles[5] = 1;
    Scene.Angles[8] = Scene.Angles[9] = Scene.Angles[10] = Scene.Angles[11] = 1;
    Scene.Angles[13] = 1.5f; Scene.Angles[14] = 1;
    Scene.Angles[268] = float(Dimension);
    auto Level = Pixels;
    uint32_t Size = Dimension;
    for (;;)
    {
        for (const auto& Pixel : Level) for (float Channel : Pixel) Scene.Angles.push_back(Channel);
        if (Size == 1) break;
        const uint32_t NextSize = Size / 2;
        std::vector<ReplayTextureColour> Next(size_t(NextSize) * NextSize);
        for (uint32_t Y = 0; Y < NextSize; ++Y)
            for (uint32_t X = 0; X < NextSize; ++X)
                for (uint32_t C = 0; C < 3; ++C)
                    for (uint32_t Dy = 0; Dy < 2; ++Dy)
                        for (uint32_t Dx = 0; Dx < 2; ++Dx)
                            Next[size_t(Y)*NextSize+X][C] += .25f * Level[size_t(2*Y+Dy)*Size+2*X+Dx][C];
        Level = std::move(Next);
        Size = NextSize;
    }
}
std::vector<ReplayTextureColour> ReplayCheckerPixels(uint32_t Palette)
{
    std::vector<ReplayTextureColour> Pixels(64);
    const ReplayTextureColour A = Palette == 0 ? ReplayTextureColour{.84f,.14f,.06f}
                                                : ReplayTextureColour{.10f,.72f,.15f};
    const ReplayTextureColour B = Palette == 0 ? ReplayTextureColour{.08f,.50f,.80f}
                                                : ReplayTextureColour{.90f,.64f,.08f};
    for (uint32_t Y = 0; Y < 8; ++Y)
        for (uint32_t X = 0; X < 8; ++X)
            Pixels[size_t(Y)*8+X] = ((X/2 + Y/2) & 1) ? B : A;
    return Pixels;
}
LightComputeArchive ReplayTexturedPlanes(const ReplayTextureColour& Colour, bool Covered = false)
{
    auto Scene = ReplayMultibounceFixture(Covered);
    ReplayInstallTexture(Scene, std::vector<ReplayTextureColour>(64, Colour), 8);
    Scene.Camera[0] = {0,0,0,0}; Scene.Camera[3][3] = 0;
    for (size_t I = 0; I < Scene.Surfaces.size(); ++I)
        if (Scene.Visibility.OriginalFacets[I] < 2)
        {
            auto& F = Scene.Surfaces[I];
            F.Emissive[3] = 1;
            F.A[3] = F.B[3] = F.C[3] = .0625f;
            F.NormalA[3] = F.NormalB[3] = F.NormalC[3] = .0625f;
        }
    return Scene;
}
LightComputeArchive ReplayTexturedRoom(uint32_t Palette, float Blocker)
{
    auto Scene = ReplayRoom(Blocker);
    ReplayInstallTexture(Scene, ReplayCheckerPixels(Palette), 8);
    Scene.Angles[4] = Scene.Angles[5] = 1.8f;
    Scene.Angles[6] = .21f; Scene.Angles[7] = -.37f;
    Scene.Angles[8] = Scene.Angles[9] = Scene.Angles[10] = .8f;
    for (auto& F : Scene.Surfaces)
        if (F.OpticalToken[3] == 1 || F.OpticalToken[3] == 8)
        {
            F.Emissive[3] = 1;
            auto Coordinate = [](std::array<float,4>& P, std::array<float,4>& N)
            {
                P[3] = .36f*P[0] + .10f*P[1] - .25f;
                N[3] = .30f*P[1] + .33f*P[2] - .4f;
            };
            Coordinate(F.A,F.NormalA); Coordinate(F.B,F.NormalB); Coordinate(F.C,F.NormalC);
        }
    return Scene;
}
std::vector<std::array<float,4>> TraceTexturedReplay(LightComputeSubmission& Compute, const LightComputeArchive& Scene,
                                                      uint32_t Hits, uint32_t Seed, bool Temporal, bool Spatial, bool Reset,
                                                      bool Primary = false, bool Facets = false, bool Balance = false,
                                                      const std::array<float,3>& Environment = {0,0,0})
{
    return Compute.ExecuteReplay(Scene, Hits, Seed, Temporal, Spatial, Reset, Environment, .01f, Primary,
                                 false, false, Facets, false, false, 64, Balance, true);
}
void ValidateReplayTextureGuards()
{
    const auto Valid = ReplayTexturedPlanes({.8f,.2f,.1f});
    ValidatePathReplayDomain(ReplayDomainView(Valid), {false, false, true});
    auto Rejected = [](const LightComputeArchive& Scene, bool Rough = false, bool OptIn = true, bool Primary = false)
    {
        try { ValidatePathReplayDomain(ReplayDomainView(Scene), {Primary, Rough, OptIn}); }
        catch (const std::exception&) { return true; }
        return false;
    };
    uint32_t Cases = 0;
    auto Check = [&](LightComputeArchive Scene)
    {
        Require(Rejected(Scene), "Malformed diffuse atlas was not rejected");
        ++Cases;
    };
    Require(Rejected(Valid, false, false) && !Rejected(Valid, true, true),
            "Textured replay failed opt-in or guarded rough-material acceptance");
    auto Rough = Valid;
    Rough.Angles[11] = .6f; Rough.Angles[12] = .4f;
    Require(Rejected(Rough, false, true) && !Rejected(Rough, true, true),
            "Textured GGX coefficients bypassed the diffuse guard or failed the rough guard");
    for (const auto& Pair : std::array<std::pair<uint32_t, float>, 4>{{{11, .249f}, {11, 1.01f},
                                                                       {12, -.01f}, {12, 1.01f}}})
    {
        auto BadRough = Rough;
        BadRough.Angles[Pair.first] = Pair.second;
        Require(Rejected(BadRough, true, true), "Out-of-domain rough texture coefficients were accepted");
    }
    auto TexturedFacet = [](LightComputeArchive& S) -> LightComputeFacet&
    {
        for (auto& F : S.Surfaces) if (F.Emissive[3] != 0) return F;
        throw std::runtime_error("Fixture has no textured facet");
    };
    auto Bad = Valid; TexturedFacet(Bad).Emissive[3] = 1.5f; Check(Bad);
    Bad = Valid; TexturedFacet(Bad).Emissive[3] = float(Bad.Angles.size()+1); Check(Bad);
    Bad = Valid; TexturedFacet(Bad).Emissive[3] = -1; Check(Bad);
    Bad = Valid; TexturedFacet(Bad).A[3] = std::numeric_limits<float>::quiet_NaN(); Check(Bad);
    Bad = Valid; TexturedFacet(Bad).NormalB[3] = 10001; Check(Bad);
    Bad = Valid; TexturedFacet(Bad).A[0] += 1; Check(Bad);
    Bad = Valid; Bad.Angles.resize(267); Check(Bad);
    Bad = Valid; Bad.Angles[0] = 2; Check(Bad);
    Bad = Valid; Bad.Angles[1] = 2; Check(Bad);
    Bad = Valid; Bad.Angles[3] = 268; Check(Bad);
    Bad = Valid; Bad.Angles[200] = 1; Check(Bad);
    Bad = Valid; Bad.Angles[11] = .5f; Check(Bad);
    Bad = Valid; Bad.Angles[12] = .2f; Check(Bad);
    Bad = Valid; Bad.Angles[14] = .5f; Check(Bad);
    Bad = Valid; Bad.Angles[15] = 1; Check(Bad);
    Bad = Valid; Bad.Angles[16] = .1f; Check(Bad);
    Bad = Valid; Bad.Angles[20] = 1; Check(Bad);
    Bad = Valid; Bad.Angles[21] = 1; Check(Bad);
    Bad = Valid; Bad.Angles[27] = .5f; Check(Bad);
    Bad = Valid; Bad.Angles[4] = std::numeric_limits<float>::infinity(); Check(Bad);
    Bad = Valid; Bad.Angles[2] = float(Bad.Angles.size()); Check(Bad);
    Bad = Valid; Bad.Angles[2] = 268.5f; Check(Bad);
    Bad = Valid; Bad.Angles[268] = 3; Check(Bad);
    Bad = Valid; Bad.Angles[268] = 0; Check(Bad);
    Bad = Valid; Bad.Angles[268] = 2048; Check(Bad);
    Bad = Valid; Bad.Angles.pop_back(); Check(Bad);
    Bad = Valid; Bad.Angles[272] = -0.1f; Check(Bad);
    Bad = Valid; Bad.Angles.back() = std::numeric_limits<float>::quiet_NaN(); Check(Bad);
    Bad = Valid; Bad.Camera[3][3] = std::numeric_limits<float>::infinity(); Check(Bad);
    Bad = Valid; Bad.Camera[10][2] = 1; Check(Bad);
    Bad = Valid; Bad.Receivers[size_t(Bad.Width)+1].PositionHit[0] = 1000001; Check(Bad);
    Bad = Valid; Bad.Surfaces[0].A[0] = Bad.Visibility.Corners[0].A[0] = 1000001; Check(Bad);
    auto Primary = ReplayTexturedRoom(0, 7);
    ValidatePathReplayDomain(ReplayDomainView(Primary), {true, false, true});
    Primary.Camera[1][0] = 1;
    Require(Rejected(Primary, false, true, true), "Malformed primary camera was not rejected");
    ++Cases;
    std::printf("TEXTURE_GUARDS malformed=%u opt_in=1 rough_opt_in=1 rough_bounds=4 diffuse_rejects_rough=1\n", Cases);
}
void ValidateReplayTextureAnalytic(const char* Driver, const std::string& Shaders)
{
    const auto Dark = ReplayTexturedPlanes({0,0,0});
    const auto Bright = ReplayTexturedPlanes({.8f,.2f,.1f});
    const auto Covered = ReplayTexturedPlanes({.8f,.2f,.1f}, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Dark, Shaders, false, false, false, false, true);
    ReplayGroupedMoments Fresh, Changed, Blocked, Reopened;
    uint32_t ChangedAccepted = 0, BlockedAccepted = 0, ReopenedAccepted = 0, Links = 0;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 80000 + Batch * 100;
        Fresh.Accumulate(TraceTexturedReplay(Compute, Bright, 2, Seed, false, false, true), Bright.Width);
        for (uint32_t Iteration = 0; Iteration < 5; ++Iteration)
            (void)TraceTexturedReplay(Compute, Dark, 2, Seed+10+Iteration, true, false, Iteration == 0);
        Changed.Accumulate(TraceTexturedReplay(Compute, Bright, 2, Seed+20, true, true, false), Bright.Width);
        auto Counts = Compute.ExtendedStatistics(); ChangedAccepted += Counts[20]; Links += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "In-place texture revision discarded or retained long history");
        Blocked.Accumulate(TraceTexturedReplay(Compute, Covered, 2, Seed+21, true, true, false), Covered.Width);
        Counts = Compute.ExtendedStatistics(); BlockedAccepted += Counts[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Textured blocker transition discarded or retained long history");
        Reopened.Accumulate(TraceTexturedReplay(Compute, Bright, 2, Seed+22, true, true, false), Bright.Width);
        Counts = Compute.ExtendedStatistics(); ReopenedAccepted += Counts[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Textured reopening discarded or retained long history");
    }
    for (uint32_t C = 0; C < 3; ++C)
    {
        const double Expected = std::array<double,3>{.8,.2,.1}[C];
        std::printf("TEXTURE_TWO_HIT channel=%u exact=%.9g fresh=%.9g changed=%.9g blocked=%.9g reopened=%.9g\n",
                    C, Expected, Fresh.Pixels[C].Mean(), Changed.Pixels[C].Mean(), Blocked.Pixels[C].Mean(),
                    Reopened.Pixels[C].Mean());
        for (const auto* Result : {&Fresh, &Changed, &Reopened})
            Require(std::abs(Result->Pixels[C].Mean()-Expected) < .002,
                    "Textured two-hit transport disagrees with the infinite-plane analytic value");
        Require(Blocked.Pixels[C].Mean() == 0, "Textured replay did not retrace the moving blocker");
    }
    Require(ChangedAccepted == 8*5120 && BlockedAccepted == 8*5120 && ReopenedAccepted == 8*5120 && Links > 0,
            "Textured transitions did not exercise temporal and spatial reuse");
    Require(Compute.PendingOrdinaryHistoryReset && !Compute.HistoryDirty, "Texture revision lost the ordinary-pipeline reset");
    Compute.Upload(Bright);
    Require(Compute.HistoryDirty && !Compute.PendingOrdinaryHistoryReset,
            "An ordinary pipeline could retain history after a replay texture revision");
    std::printf("TEXTURE_TWO_HIT_LINKS changed=%u blocked=%u reopened=%u spatial=%u ordinary_reset=1\n",
                ChangedAccepted, BlockedAccepted, ReopenedAccepted, Links);
    std::printf("PASS two-hit textured diffuse transport, in-place changes and moving occlusion\n");
}
void ValidateReplayTextureSecondaryFiltering(const char* Driver, const std::string& Shaders)
{
    auto Scene = ReplayTexturedPlanes({0,0,0});
    ReplayInstallTexture(Scene, ReplayCheckerPixels(0), 8);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    struct Probe { float U, V, Footprint; ReplayTextureColour Expected; };
    const ReplayTextureColour A{.84f,.14f,.06f}, Mixed{.46f,.32f,.43f};
    const std::array<Probe,5> Probes{{{.0625f,.0625f,0,A}, {.25f,.0625f,0,Mixed},
                                      {-.75f,.0625f,0,Mixed}, {0,.0625f,0,Mixed},
                                      {.0625f,.0625f,100,Mixed}}};
    for (uint32_t I = 0; I < Probes.size(); ++I)
    {
        auto ProbeScene = Scene;
        ProbeScene.Camera[3][3] = Probes[I].Footprint;
        for (auto& F : ProbeScene.Surfaces) if (F.Emissive[3] != 0)
        {
            F.A[3] = F.B[3] = F.C[3] = Probes[I].U;
            F.NormalA[3] = F.NormalB[3] = F.NormalC[3] = Probes[I].V;
        }
        const auto Pixels = TraceTexturedReplay(Compute, ProbeScene, 2, 90000+I, false, false, true);
        ReplayRgbMoments Moments; ReplayAccumulateRgb(Moments, Pixels, ProbeScene.Width);
        for (uint32_t C = 0; C < 3; ++C)
            Require(std::abs(Moments[C].Mean() - Probes[I].Expected[C]) < .002,
                    "Secondary texture center, wrap, bilinear or coarse mip differs from analytic colour");
        std::printf("TEXTURE_SECONDARY probe=%u uv=%.4g/%.4g vertical_tangent=%.4g rgb=%.9g/%.9g/%.9g\n",
                    I, Probes[I].U, Probes[I].V, Probes[I].Footprint,
                    Moments[0].Mean(), Moments[1].Mean(), Moments[2].Mean());
    }
    std::printf("PASS secondary texel center, bilinear, negative wrap, seam and coarse mip probes\n");
}
void ValidateReplayTexturePrimary(const char* Driver, const std::string& Shaders)
{
    auto Scene = ReplayTexturedRoom(0, 7);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    (void)TraceTexturedReplay(Compute, Scene, 2, 91000, false, false, true, true, true);
    const auto Strikes = Compute.PrimaryStrikes();
    const auto Guides = Compute.PrimaryReceivers();
    std::array<uint32_t,4> Mips{};
    uint32_t Compared = 0, Wrapped = 0, Boundary = 0;
    double MaximumError = 0;
    for (size_t I = 0; I < Strikes.size(); ++I)
    {
        if (Strikes[I][1] < 0) continue;
        const auto& F = Scene.Surfaces[size_t(Strikes[I][1])];
        if (F.Emissive[3] == 0) continue;
        const auto& R = Guides[I];
        const auto Reference = ReplayReferenceTexture(Scene, F, Strikes[I][2], Strikes[I][3],
                                                        ReplayReferenceXyz(R.PositionHit),
                                                        ReplayReferenceXyz(R.NormalRoughness),
                                                        ReplayReferenceXyz(R.ViewMetallic));
        if (Reference.NearMipBoundary) { ++Boundary; continue; }
        Require(Reference.Mip < Mips.size(), "Unexpected texture mip level");
        ++Mips[Reference.Mip]; ++Compared; Wrapped += Reference.Wrapped ? 1 : 0;
        for (uint32_t C = 0; C < 3; ++C)
            MaximumError = std::max(MaximumError, std::abs(double(R.AlbedoIor[C]) - Reference.Albedo[C]));
    }
    std::printf("TEXTURE_PRIMARY compared=%u wrapped=%u mips=%u/%u/%u/%u boundary_skipped=%u max_error=%.9g\n",
                Compared, Wrapped, Mips[0], Mips[1], Mips[2], Mips[3], Boundary, MaximumError);
    Require(Compared > 1000 && Wrapped > 100 && Mips[0] > 0 && Mips[1] > 0 && MaximumError < .0001,
            "Primary Vulkan UV/wrapping/bilinear/mip sampling disagrees with independent CPU evaluation");
    std::printf("PASS primary texture evaluation against independent double-precision filter\n");
}
} // namespace
