/*==========================================================================================================================================
                                             REPLAYIMPORTANCEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Check the optional cosine/GGX proposal, facet MIS and bounded four-hit history against independent references.
#pragma once

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                 PROPOSAL-MASS AUDIT
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayImportanceMass()
{
    // 📝 Independent midpoint integration of outgoing density and rejected GGX-normal mass.
    constexpr double π = 3.14159265358979323846, Roughness = .7, Angle = 1.2;
    constexpr uint32_t Steps = 1024, Azimuths = 2 * Steps;
    const double α2 = std::pow(Roughness, 4), Vx = std::sin(Angle), Vz = std::cos(Angle);
    double Upper = 0, Rejected = 0;
    for (uint32_t I = 0; I < Steps; ++I)
    {
        const double μ = (I + .5) / Steps, Radius = std::sqrt(1 - μ * μ);
        const double Denom = μ * μ * (α2 - 1) + 1;
        const double HalfDensity = α2 * μ / (π * Denom * Denom);
        for (uint32_t J = 0; J < Azimuths; ++J)
        {
            const double φ = 2 * π * (J + .5) / Azimuths;
            const double X = Radius * std::cos(φ), ViewHalf = Vx * X + Vz * μ;
            if (ViewHalf <= 0 || 2 * ViewHalf * μ - Vz <= 0) Rejected += HalfDensity;
            const double ViewLight = Vx * X + Vz * μ;
            const double Magnitude = std::sqrt(2 + 2 * ViewLight);
            const double Nh = (Vz + μ) / Magnitude, Vh = (1 + ViewLight) / Magnitude;
            const double D = Nh * Nh * (α2 - 1) + 1;
            const double Reflected = α2 * Nh / (π * D * D * 4 * Vh);
            Upper += .5 * (μ / π + Reflected);
        }
    }
    const double Area = 2 * π / (double(Steps) * Azimuths);
    Upper *= Area;
    Rejected *= Area;
    std::printf("IMPORTANCE_MASS upper=%.9g rejected_ggx=%.9g combined=%.9g quadrature=%ux%u\n",
                Upper, Rejected, Upper + .5 * Rejected, Steps, Azimuths);
    Require(Rejected > .1 && std::abs(Upper + .5 * Rejected - 1) < .001,
            "GGX reflected density and zero-event mass did not normalize");
    std::printf("PASS independent proposal mass including rejected below-surface draws\n");
}

std::vector<std::array<float,4>> TraceImportanceReplay(LightComputeSubmission& Compute, const LightComputeArchive& Scene,
                                                        uint32_t Hits, uint32_t Seed, bool Temporal, bool Spatial,
                                                        bool Reset, bool Importance, bool Facets = false,
                                                        bool Balance = false,
                                                        const std::array<float,3>& Environment = {1,1,1})
{
    return Compute.ExecuteReplay(Scene, Hits, Seed, Temporal, Spatial, Reset, Environment, .01f,
                                 false, false, true, Facets, false, false, 64, Balance, false, Importance);
}

//------------------------------------------------------------------------------------------------------------------------
//                                               ENVIRONMENT AND VARIANCE CONTROLS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayImportanceEnvironment(const char* Driver, const std::string& Shaders)
{
    auto Metal = RoughReplayFixture(.95f, .25f, .8f);
    auto Matte = RoughReplayFixture(0, 1, 0);
    for (auto& R : Matte.Receivers)
        if (R.PositionHit[3] != 0)
        {
            R.AlbedoIor = {.7f,.7f,.7f,1};
            R.ResidualSpecular[3] = 0;
        }
    const auto& MetallicReceiver = Metal.Receivers[size_t(Metal.Width) + 1];
    const auto& MatteReceiver = Matte.Receivers[size_t(Matte.Width) + 1];
    const auto MetalExact = RoughReplayQuadrature(MetallicReceiver, 1024);
    const auto MetalCoarse = RoughReplayQuadrature(MetallicReceiver, 512);
    const auto MatteExact = RoughReplayQuadrature(MatteReceiver, 512);
    const auto MatteCoarse = RoughReplayQuadrature(MatteReceiver, 256);
    for (uint32_t C = 0; C < 3; ++C)
        Require(std::abs(MetalExact[C] - MetalCoarse[C]) < .0005 && std::abs(MatteExact[C] - MatteCoarse[C]) < .0005,
                "Environment quadrature did not converge");
    LightComputeSubmission Compute(Driver);
    std::printf("IMPORTANCE_ENV preparing Vulkan on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Metal, Shaders, false, false, false, false, true);
    (void)TraceImportanceReplay(Compute, Metal, 1, 703997, false, false, true, false);
    (void)TraceImportanceReplay(Compute, Metal, 1, 703998, false, false, true, true);
    constexpr uint32_t Batches = 32;
    ReplayGroupedMoments MetalCosine, MetalMixture, MatteCosine, MatteMixture;
    double CosineMs = 0, MixtureMs = 0;
    uint64_t ZeroDraws = 0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 704000 + 101 * Batch;
        auto TraceMetal = [&](bool Importance)
        {
            const auto Begin = std::chrono::steady_clock::now();
            auto Image = TraceImportanceReplay(Compute, Metal, 1, Seed, false, false, true, Importance);
            const double Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begin).count();
            (Importance ? MixtureMs : CosineMs) += Milliseconds;
            (Importance ? MetalMixture : MetalCosine).Accumulate(Image, Metal.Width);
        };
        // 📝 Interleave order to reduce warm-up/order effects; timings include upload, readback and CPU Vulkan.
        if (Batch & 1u) { TraceMetal(true); TraceMetal(false); }
        else { TraceMetal(false); TraceMetal(true); }
        MatteCosine.Accumulate(TraceImportanceReplay(Compute, Matte, 1, Seed + 1, false, false, true, false), Matte.Width);
        const auto MatteImage = TraceImportanceReplay(Compute, Matte, 1, Seed + 1, false, false, true, true);
        MatteMixture.Accumulate(MatteImage, Matte.Width);
        for (uint32_t Y = 0; Y < 32; ++Y)
            for (uint32_t X = 0; X < 32; ++X)
                if (MatteImage[size_t(3 * Y + 1) * Matte.Width + 3 * X + 1][0] == 0) ++ZeroDraws;
    }
    auto Check = [](const char* Name, const std::array<double,3>& Expected, const ReplayGroupedMoments& Actual)
    {
        for (uint32_t C = 0; C < 3; ++C)
        {
            const auto& P = Actual.Pixels[C];
            const double BatchSe = ReplaySampleStandardError(Actual.Batches[C]);
            const double Se = std::max(ReplaySampleStandardError(P), BatchSe);
            std::printf("IMPORTANCE_ENV %s channel=%u reference=%.9g mean=%.9g pixel_se=%.9g batch_se=%.9g "
                        "sample_sd=%.9g n=%u batches=%u\n", Name, C, Expected[C], P.Mean(),
                        ReplaySampleStandardError(P), BatchSe, P.Deviation(), P.Count, Actual.Batches[C].Count);
            Require(std::abs(P.Mean() - Expected[C]) < 6 * Se + .001,
                    "Environment replay disagrees with hemisphere quadrature");
        }
    };
    Check("metal_cosine", MetalExact, MetalCosine); Check("metal_mixture", MetalExact, MetalMixture);
    Check("matte_cosine", MatteExact, MatteCosine); Check("matte_mixture", MatteExact, MatteMixture);
    const double MetalRatio = MetalMixture.Pixels[0].Deviation() / MetalCosine.Pixels[0].Deviation();
    const double MatteRatio = MatteMixture.Pixels[0].Deviation() / MatteCosine.Pixels[0].Deviation();
    const double TimeRatio = MixtureMs / CosineMs;
    std::printf("IMPORTANCE_VARIANCE metal_sd_ratio=%.9g matte_sd_ratio=%.9g zero_draws=%llu/%u "
                "cosine_ms=%.9g mixture_ms=%.9g time_ratio=%.9g estimated_equal_time_variance_ratio=%.9g\n",
                MetalRatio, MatteRatio, (unsigned long long)ZeroDraws, Batches * 1024, CosineMs / Batches,
                MixtureMs / Batches, TimeRatio, MetalRatio * MetalRatio * TimeRatio);
    Require(MetalRatio < .65 && MatteRatio > 1.05 && ZeroDraws > Batches * 1024 / 10,
            "Variance benefit, matte counterexample or rejected draws were not exercised");
    std::printf("PASS GGX mixture and cosine against environment quadrature with a variance counterexample\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  FACET MIS AND OCCLUSION
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayImportanceFacet(const char* Driver, const std::string& Shaders)
{
    auto Half = ReplayFacetFixture(.5f, 0);
    auto Covered = ReplayFacetFixture(.5f, -5);
    for (auto* Scene : {&Half, &Covered})
        for (auto& R : Scene->Receivers)
            if (R.PositionHit[3] != 0)
            {
                R.NormalRoughness = {0,0,1,.45f};
                R.ViewMetallic = {std::sin(.9f),0,std::cos(.9f),.9f};
                R.AlbedoIor = {.8f,.3f,.1f,1.5f};
                R.ResidualSpecular = {0,0,0,.5f};
            }
    const auto& Receiver = Half.Receivers[size_t(Half.Width) + 1];
    const auto Fine = ReplayRoughAreaIntegral(Receiver, .5, 0, 1000);
    const auto Coarse = ReplayRoughAreaIntegral(Receiver, .5, 0, 500);
    for (uint32_t C = 0; C < 3; ++C)
        Require(std::abs(Fine[C] - Coarse[C]) < .0001, "Area quadrature did not converge");
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Half, Shaders, false, false, false, false, true);
    ReplayGroupedMoments Balanced, BsdfOnly, FacetOnly;
    double CoveredMaximum = 0;
    for (uint32_t Batch = 0; Batch < 16; ++Batch)
    {
        const uint32_t Seed = 711000 + 101 * Batch;
        Balanced.Accumulate(TraceImportanceReplay(Compute, Half, 1, Seed, false, false, true, true, true, true, {0,0,0}), Half.Width);
        BsdfOnly.Accumulate(TraceImportanceReplay(Compute, Half, 1, Seed + 1, false, false, true, true, false, false, {0,0,0}), Half.Width);
        FacetOnly.Accumulate(TraceImportanceReplay(Compute, Half, 1, Seed + 2, false, false, true, true, true, false, {0,0,0}), Half.Width);
        const auto Blocked = TraceImportanceReplay(Compute, Covered, 1, Seed + 3, false, false, true, true, true, true, {0,0,0});
        for (const auto& Pixel : Blocked) CoveredMaximum = std::max(CoveredMaximum, double(Pixel[0]));
    }
    for (const auto& Variant : {std::pair<const char*, const ReplayGroupedMoments*>{"balance", &Balanced},
                                {"bsdf_only", &BsdfOnly}, {"facet_only", &FacetOnly}})
        for (uint32_t C = 0; C < 3; ++C)
        {
            const auto& Pixels = Variant.second->Pixels[C];
            const double BatchSe = ReplaySampleStandardError(Variant.second->Batches[C]);
            const double Se = std::max(ReplaySampleStandardError(Pixels), BatchSe);
            std::printf("IMPORTANCE_FACET %s channel=%u quadrature=%.9g mean=%.9g batch_se=%.9g n=%u\n",
                        Variant.first, C, Fine[C], Pixels.Mean(), BatchSe, Pixels.Count);
            Require(std::abs(Pixels.Mean() - Fine[C]) < 6 * Se + .001,
                    "GGX facet/BSDF strategy disagrees with area quadrature");
        }
    std::printf("IMPORTANCE_FACET covered_max=%.9g\n", CoveredMaximum);
    Require(CoveredMaximum == 0, "Covered source leaked through the mixed-proposal shadow test");
    std::printf("PASS mixed proposal and facet MIS against independent half-visible area integral\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                                             FOUR-HIT TEXTURED REUSE
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayImportanceRoom(const char* Driver, const std::string& Shaders)
{
    const auto Open = RoughTexturedRoom(1, 7, .48f, .7f);
    const auto Blocked = RoughTexturedRoom(1, 0, .48f, .7f);
    std::printf("IMPORTANCE_ROOM tracing two 400000-path independent cosine CPU references\n");
    std::fflush(stdout);
    const auto CpuOpen = ReplayReferenceIntegrate(Open, 4, 715921, true, true);
    const auto CpuBlocked = ReplayReferenceIntegrate(Blocked, 4, 716147, true, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    ReplayGroupedMoments Fresh, ProposalChanged, Occluded;
    uint32_t ProposalAccepted = 0, BlockedAccepted = 0, Spatial = 0;
    constexpr uint32_t Batches = 16;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 717000 + 101 * Batch;
        Fresh.Accumulate(TraceRoughTextureReplay(Compute, Open, 4, Seed, false, false, true,
                                                  false, true, true, true), Open.Width);
        for (uint32_t Iteration = 0; Iteration < 4; ++Iteration)
            (void)TraceRoughTextureReplay(Compute, Open, 4, Seed + 10 + Iteration, true, false, Iteration == 0,
                                          false, true, true, false);
        ProposalChanged.Accumulate(TraceRoughTextureReplay(Compute, Open, 4, Seed + 20, true, true, false,
                                                            false, true, true, true), Open.Width);
        auto Counts = Compute.ExtendedStatistics(); ProposalAccepted += Counts[20]; Spatial += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Changing the proposal did not retain capped temporal history");
        Occluded.Accumulate(TraceRoughTextureReplay(Compute, Blocked, 4, Seed + 21, true, true, false,
                                                   false, true, true, true), Blocked.Width);
        Counts = Compute.ExtendedStatistics(); BlockedAccepted += Counts[20]; Spatial += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving the blocker did not retain capped temporal history");
    }
    ReplayCompareRgb("importance_rough_open_fresh", CpuOpen, Fresh);
    ReplayCompareRgb("importance_rough_proposal_reuse", CpuOpen, ProposalChanged);
    ReplayCompareRgb("importance_rough_blocked_reuse", CpuBlocked, Occluded);
    std::printf("IMPORTANCE_ROOM proposal_accepts=%u blocked_accepts=%u spatial=%u batches=%u\n",
                ProposalAccepted, BlockedAccepted, Spatial, Batches);
    Require(ProposalAccepted == Batches * 5120 && BlockedAccepted == Batches * 5120 && Spatial > 0,
            "Mixed-proposal textured room did not exercise transition and blocker reuse");
    std::printf("PASS mixed-proposal four-hit textured room and bounded temporal/spatial reuse\n");
}
} // namespace
