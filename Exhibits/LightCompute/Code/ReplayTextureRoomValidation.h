/*==========================================================================================================================================
                                              REPLAYTEXTUREROOMVALIDATION.H
==========================================================================================================================================*/
// 🧩 Four-hit textured room comparisons against independent brute-force paths and paired negative controls.
#pragma once

namespace
{
void ValidateReplayTextureRoom(const char* Driver, const std::string& Shaders)
{
    auto First = ReplayTexturedRoom(0, 7), Relit = ReplayTexturedRoom(1, 7), Covered = ReplayTexturedRoom(1, 0);
    auto Receivers = DirectGpuFixture(false);
    for (auto& R : Receivers.Receivers)
        if (R.PositionHit[3] != 0)
        {
            R.PositionHit = {1.4f,2,0,1}; R.MotionPositionToken = {1.4f,2,0,1};
            R.AlbedoIor = {.7f,.7f,.7f,1.5f};
        }
    for (auto* Scene : {&First, &Relit, &Covered})
    {
        Scene->Width = Receivers.Width; Scene->Height = Receivers.Height;
        Scene->Receivers = Receivers.Receivers;
    }
    auto Untextured = Covered;
    Untextured.Angles.clear();
    for (size_t I = 0; I < Untextured.Surfaces.size(); ++I)
    {
        auto& F = Untextured.Surfaces[I];
        F.Emissive[3] = 0;
        F.A[3] = Untextured.Visibility.Corners[I].A[3];
        F.B[3] = Untextured.Visibility.Corners[I].B[3];
        F.C[3] = Untextured.Visibility.Corners[I].C[3];
    }
    std::printf("TEXTURE_ROOM tracing three independent 400000-path CPU references\n");
    std::fflush(stdout);
    const auto CpuFirst = ReplayReferenceIntegrate(First, 4, 328931, false, true);
    const auto CpuRelit = ReplayReferenceIntegrate(Relit, 4, 529837, false, true);
    const auto CpuCovered = ReplayReferenceIntegrate(Covered, 4, 781631, false, true);
    std::mt19937_64 TexturedRandom(832493), PlainRandom(832493);
    ReplayRgbMoments TextureDifference;
    const auto& Receiver = Covered.Receivers[size_t(Covered.Width)+1];
    for (uint32_t Index = 0; Index < 120000; ++Index)
    {
        const auto Textured = ReplayReferencePath(Covered, Receiver, 4, TexturedRandom, false, true);
        const auto Plain = ReplayReferencePath(Untextured, Receiver, 4, PlainRandom, false, false);
        for (uint32_t C = 0; C < 3; ++C) TextureDifference[C].Accumulate(Textured[C]-Plain[C]);
    }
    bool TextureInfluence = false;
    for (uint32_t C = 0; C < 3; ++C)
    {
        const auto& Difference = TextureDifference[C];
        std::printf("TEXTURE_COUNTERFACTUAL channel=%u textured_minus_untextured=%.9g paired_se=%.9g n=%u\n",
                    C, Difference.Mean(), Difference.StandardError(), Difference.Count);
        TextureInfluence |= std::abs(Difference.Mean()) > 6*Difference.StandardError() + 1e-5;
    }
    Require(TextureInfluence, "The textured-room fixture does not exercise a measurable secondary-albedo difference");
    const std::array<float,3> Environment{.025f,.025f,.025f};
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(First, Shaders, false, false, false, false, true);
    const auto OneFirst = TraceTexturedReplay(Compute, First, 1, 100100, false, false, true, false, true, true, Environment);
    const auto OneRelit = TraceTexturedReplay(Compute, Relit, 1, 100100, false, false, true, false, true, true, Environment);
    Require(OneFirst == OneRelit, "Secondary-only atlas changes affected the one-hit control");
    ReplayGroupedMoments FreshFirst, FacetRelit, ReusedRelit, ReusedCovered, ReopenedFirst, GpuTextureDifference;
    uint32_t ChangedAccepted = 0, CoveredAccepted = 0, ReopenedAccepted = 0, Spatial = 0;
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 101000 + 100*Batch;
        FreshFirst.Accumulate(TraceTexturedReplay(Compute, First, 4, Seed, false, false, true,
                                                   false, true, true, Environment), First.Width);
        FacetRelit.Accumulate(TraceTexturedReplay(Compute, Relit, 4, Seed+1, false, false, true,
                                                  false, true, false, Environment), First.Width);
        for (uint32_t Iteration = 0; Iteration < 6; ++Iteration)
            (void)TraceTexturedReplay(Compute, First, 4, Seed+10+Iteration, true, false, Iteration == 0,
                                      false, true, true, Environment);
        ReusedRelit.Accumulate(TraceTexturedReplay(Compute, Relit, 4, Seed+20, true, true, false,
                                                   false, true, true, Environment), First.Width);
        auto Counts = Compute.ExtendedStatistics(); ChangedAccepted += Counts[20]; Spatial += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Four-hit atlas transition lost capped history");
        ReusedCovered.Accumulate(TraceTexturedReplay(Compute, Covered, 4, Seed+21, true, true, false,
                                                     false, true, true, Environment), First.Width);
        Counts = Compute.ExtendedStatistics(); CoveredAccepted += Counts[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Four-hit textured blocker transition lost capped history");
        ReopenedFirst.Accumulate(TraceTexturedReplay(Compute, First, 4, Seed+22, true, true, false,
                                                      false, true, true, Environment), First.Width);
        Counts = Compute.ExtendedStatistics(); ReopenedAccepted += Counts[20];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Four-hit simultaneous atlas and blocker transition lost capped history");
    }
    // 📝 Same seeds couple the textured and untextured paths without substituting the CPU generator.
    for (uint32_t Batch = 0; Batch < 24; ++Batch)
    {
        const uint32_t Seed = 104000 + 100*Batch;
        auto TexturedPixels = TraceTexturedReplay(Compute, Covered, 4, Seed, false, false, true,
                                                   false, false, false, Environment);
        const auto PlainPixels = Compute.ExecuteReplay(Untextured, 4, Seed, false, false, true, Environment);
        for (size_t I = 0; I < TexturedPixels.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C) TexturedPixels[I][C] -= PlainPixels[I][C];
        GpuTextureDifference.Accumulate(TexturedPixels, Covered.Width);
    }
    ReplayCompareRgb("texture_first_fresh_balanced", CpuFirst, FreshFirst);
    ReplayCompareRgb("texture_relit_fresh_facet", CpuRelit, FacetRelit);
    ReplayCompareRgb("texture_relit_reused_balanced", CpuRelit, ReusedRelit);
    ReplayCompareRgb("texture_covered_reused_balanced", CpuCovered, ReusedCovered);
    ReplayCompareRgb("texture_reopened_reused_balanced", CpuFirst, ReopenedFirst);
    ReplayCompareRgb("texture_paired_counterfactual", TextureDifference, GpuTextureDifference);
    bool GpuTextureInfluence = false;
    for (uint32_t C = 0; C < 3; ++C)
    {
        const auto& Batches = GpuTextureDifference.Batches[C];
        const double BatchSe = Batches.StandardError() * std::sqrt(double(Batches.Count) / (Batches.Count - 1));
        GpuTextureInfluence |= std::abs(GpuTextureDifference.Pixels[C].Mean()) > 5*BatchSe + 1e-5;
    }
    Require(GpuTextureInfluence, "The Vulkan paired control did not detect the secondary-albedo change");
    Require(ChangedAccepted == 8*5120 && CoveredAccepted == 8*5120 && ReopenedAccepted == 8*5120 && Spatial > 0,
            "Four-hit textured transitions did not exercise temporal and spatial reuse");
    std::printf("TEXTURE_ROOM_LINKS atlas=%u blocker=%u simultaneous=%u spatial=%u one_hit_equal=1 "
                "cpu_rng=mt19937_64 brute_force=1\n", ChangedAccepted, CoveredAccepted, ReopenedAccepted, Spatial);
    std::printf("PASS four-hit textured room versus independent paths, including in-place changes and occlusion\n");
}
} // namespace
