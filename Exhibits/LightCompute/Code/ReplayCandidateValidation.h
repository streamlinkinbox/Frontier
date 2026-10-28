/*==========================================================================================================================================
                                              REPLAYCANDIDATEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify multiple fresh replay candidates against independent area integration, host RIS and motion controls.
#pragma once

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                          INDEPENDENT ONE-BOUNCE ORACLE
//------------------------------------------------------------------------------------------------------------------------

struct ReplayCandidateOracle
{
    uint32_t Selected = 0;
    float Weight = 0;
    float Radiance = 0;
    bool Boundary = false;
};

// 📝 Analytic ray/rectangle intersections and a host reservoir are independent of the shader's BVH traversal.
//    PCG is deliberately mirrored only to reconstruct individual selected candidates, not to validate randomness.
ReplayCandidateOracle EvaluateReplayCandidateOracle(uint32_t FrameSeed, uint32_t Pixel, uint32_t Candidates)
{
    uint32_t SeedBits = FrameSeed ^ (Pixel * 747796405u + 2891336453u);
    uint32_t SelectionBits = (FrameSeed * 277803737u + 1597334677u) ^ (Pixel * 2891336453u + 1u);
    float Sum = 0, SelectedTarget = 0;
    ReplayCandidateOracle Result;
    for (uint32_t CandidateIndex = 0; CandidateIndex < Candidates; ++CandidateIndex)
    {
        const uint32_t Candidate = NextReplayTailRandom(SeedBits);
        uint32_t RayBits = Candidate;
        const float Radius = std::sqrt(float(NextReplayTailRandom(RayBits) >> 8) / 16777216.f);
        const float Azimuth = 6.283185307179586f * (float(NextReplayTailRandom(RayBits) >> 8) / 16777216.f);
        const float Z = std::sqrt(std::max(0.f, 1.f - Radius * Radius));
        const float X = .999f * Radius * std::cos(Azimuth) / Z;
        const float Y = .999f * Radius * std::sin(Azimuth) / Z;
        const bool Illuminated = X < 0 && X >= -.5f && std::abs(Y) <= .5f;
        // 📝 Floating-point trigonometry or intersection rules can disagree at an exact geometric edge.
        const float EdgeDistance = std::min({std::abs(X), std::abs(X + .5f), std::abs(std::abs(Y) - .5f)});
        Result.Boundary = Result.Boundary || (std::abs(X) < .501f && std::abs(Y) < .501f && EdgeDistance < 2e-4f);
        const float Radiance = Illuminated ? 1.f : 0.f;
        const float Target = Radiance + .01f;
        Sum += Target;
        bool Select = CandidateIndex == 0;
        if (CandidateIndex != 0)
        {
            const float Draw = float(NextReplayTailRandom(SelectionBits) >> 8) / 16777216.f;
            Result.Boundary = Result.Boundary || std::abs(Draw * Sum - Target) < 1e-5f;
            Select = Draw * Sum < Target;
        }
        if (Select)
        {
            Result.Selected = Candidate;
            Result.Radiance = Radiance;
            SelectedTarget = Target;
        }
    }
    Result.Weight = Sum / (float(Candidates) * SelectedTarget);
    return Result;
}

std::vector<std::array<float,4>> ExecuteReplayCandidates(LightComputeSubmission& Compute,
    const LightComputeArchive& Scene, uint32_t Bounces, uint32_t Seed, uint32_t Candidates,
    bool Temporal, bool Spatial, bool Reset, bool Reconstruct = false,
    const std::array<float,3>& Environment = {0,0,0})
{
    return Compute.ExecuteReplay(Scene, Bounces, Seed, Temporal, Spatial, Reset, Environment, .01f, false,
                                 false, false, false, false, false, 64, false, false, false,
                                 false, false, false, false, Reconstruct, Candidates);
}

void ValidateReplayCandidateOracle(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayReconstructionScene(false);
    LightComputeSubmission Compute(Driver);
    std::printf("CANDIDATE_DEVICE name=%s type=%u api=%u\n", Compute.Properties.deviceName,
                uint32_t(Compute.Properties.deviceType), Compute.Properties.apiVersion);
    std::fflush(stdout);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    uint32_t Compared = 0, Boundary = 0;
    double MaximumWeightError = 0, MaximumColourError = 0;
    for (uint32_t Candidates = 1; Candidates <= PathReplayMaximumInitialCandidates; ++Candidates)
    {
        const uint32_t Seed = 111003u + Candidates * 7937u;
        const auto Image = ExecuteReplayCandidates(Compute, Scene, 1, Seed, Candidates, false, false, true);
        for (uint32_t Pixel = 0; Pixel < Image.size(); ++Pixel)
        {
            const auto Reservoir = ReplayTailSlot(Compute, 2, Pixel);
            const auto Chosen = ReplayTailSlot(Compute, 3, Pixel);
            Require(Reservoir[3] == float(Candidates), "Fresh candidate tally differs from its configured count");
            const auto Oracle = EvaluateReplayCandidateOracle(Seed, Pixel, Candidates);
            if (Oracle.Boundary) { ++Boundary; continue; }
            const uint32_t Selected = uint32_t(Reservoir[0]) | (uint32_t(Reservoir[1]) << 16u);
            const double WeightError = std::abs(double(Reservoir[2]) - Oracle.Weight);
            const double ColourError = std::abs(double(Image[Pixel][0]) - Oracle.Radiance * Oracle.Weight);
            MaximumWeightError = std::max(MaximumWeightError, WeightError);
            MaximumColourError = std::max(MaximumColourError, ColourError);
            if (Selected != Oracle.Selected || WeightError > 2e-5 || ColourError > 2e-5 ||
                std::abs(Chosen[0] - Oracle.Radiance) > 1e-6)
            {
                std::fprintf(stderr, "CANDIDATE_ORACLE_MISMATCH candidates=%u pixel=%u selected=%u expected=%u "
                             "weight=%.9g expected_weight=%.9g colour=%.9g expected_colour=%.9g\n",
                             Candidates, Pixel, Selected, Oracle.Selected, Reservoir[2], Oracle.Weight,
                             Image[Pixel][0], Oracle.Radiance * Oracle.Weight);
                throw std::runtime_error("Host candidate reservoir differs from the GPU");
            }
            ++Compared;
        }
    }
    Require(Compared >= 4000 && Boundary < 96, "Too many candidate-oracle paths were edge-ambiguous");
    std::printf("CANDIDATE_ORACLE candidates=1..4 compared=%u edge_or_decision_excluded=%u "
                "weight_max_error=%.9g colour_max_error=%.9g\n",
                Compared, Boundary, MaximumWeightError, MaximumColourError);
}

void ValidateReplayCandidateEncoding(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayReconstructionScene(false);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    constexpr uint32_t Seed = 139001;
    const auto Legacy = ExecuteReplayCandidates(Compute, Scene, 1, Seed, 1, false, false, true);
    std::vector<std::array<float,4>> Reservoirs;
    for (uint32_t Pixel = 0; Pixel < Legacy.size(); ++Pixel)
        Reservoirs.push_back(ReplayTailSlot(Compute, 2, Pixel));
    const auto Explicit = Compute.ExecuteReplay(Scene, 1, Seed, false, false, true, {0,0,0}, .01f, false,
                                               false, false, false, false, false, 64, false, false, false,
                                               false, false, false, false, false, 1, true);
    Require(Legacy.size() == Explicit.size() &&
                std::memcmp(Legacy.data(), Explicit.data(), Legacy.size() * sizeof(Legacy[0])) == 0,
            "Legacy zero and explicit one produced different colour bits");
    for (uint32_t Pixel = 0; Pixel < Legacy.size(); ++Pixel)
    {
        const auto Current = ReplayTailSlot(Compute, 2, Pixel);
        Require(std::memcmp(Reservoirs[Pixel].data(), Current.data(), sizeof(Current)) == 0,
                "Legacy zero and explicit one produced different reservoir bits");
    }
    uint32_t Rejected = 0;
    for (uint32_t Invalid : {0u, 5u})
    {
        try { (void)ExecuteReplayCandidates(Compute, Scene, 1, Seed, Invalid, false, false, true); }
        catch (const std::exception&) { ++Rejected; }
    }
    Require(Rejected == 2, "Invalid candidate counts reached the Vulkan submission");
    std::printf("CANDIDATE_ENCODING legacy_zero_explicit_one_bit_exact=1 histories=%zu host_rejected=%u\n",
                Reservoirs.size(), Rejected);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           AREA INTEGRAL AND VARIANCE
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayCandidateConvergence(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayReconstructionScene(false);
    const double Reference = ReplayOccludedAreaIntegral(0, 0, 1024);
    const double Coarse = ReplayOccludedAreaIntegral(0, 0, 512);
    Require(std::abs(Reference - Coarse) < 1e-6, "Independent area quadrature did not converge");
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    constexpr uint32_t Batches = 32;
    std::array<DirectGpuMoments, 4> Pixels, BatchMeans;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 190009u + Batch * 7919u;
        for (uint32_t Candidates = 1; Candidates <= 4; ++Candidates)
        {
            const auto Image = ExecuteReplayCandidates(Compute, Scene, 1, Seed, Candidates, false, false, true);
            DirectGpuMoments BatchPixels;
            for (const auto& Colour : Image)
            {
                Require(std::isfinite(Colour[0]) && Colour[0] >= 0, "Nonfinite candidate result");
                Pixels[Candidates - 1].Accumulate(Colour[0]);
                BatchPixels.Accumulate(Colour[0]);
            }
            BatchMeans[Candidates - 1].Accumulate(BatchPixels.Mean());
        }
    }
    for (uint32_t Candidates = 1; Candidates <= 4; ++Candidates)
    {
        const auto& Samples = Pixels[Candidates - 1];
        const auto& BatchesSample = BatchMeans[Candidates - 1];
        const double PixelSe = ReplaySampleStandardError(Samples);
        const double BatchSe = ReplaySampleStandardError(BatchesSample);
        std::printf("CANDIDATE_AREA count=%u reference=%.9g mean=%.9g pixel_se=%.9g batch_se=%.9g "
                    "pixel_sd=%.9g sd_ratio=%.9g samples=%u batches=%u\n", Candidates, Reference,
                    Samples.Mean(), PixelSe, BatchSe, Samples.Deviation(),
                    Samples.Deviation() / Pixels[0].Deviation(), Samples.Count, BatchesSample.Count);
        Require(std::abs(Samples.Mean() - Reference) < 6 * std::max(PixelSe, BatchSe) + .002 * Reference + 1e-6,
                "Multiple replay candidates disagree with the independent area integral");
    }
    Require(Pixels[3].Deviation() < .85 * Pixels[0].Deviation(),
            "Four candidates did not reduce per-pixel variance in the half-occluded fixture");
}

//------------------------------------------------------------------------------------------------------------------------
//                                            SPATIAL RESERVOIR ORACLE
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayCandidateSpatial(const char* Driver, const std::string& Shaders)
{
    const auto Scene = ReplayReconstructionScene(false);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, false, true);
    constexpr uint32_t Seed = 380021, Candidates = 4;
    const auto Image = ExecuteReplayCandidates(Compute, Scene, 1, Seed, Candidates, false, true, true);
    double MaximumError = 0;
    uint32_t Compared = 0;
    for (uint32_t Pixel = 0; Pixel < Image.size(); ++Pixel)
    {
        const int32_t X = int32_t(Pixel % Scene.Width), Y = int32_t(Pixel / Scene.Width);
        const std::array<std::array<int32_t,2>,5> Offsets{{{0,0}, {-1,0}, {1,0}, {0,-1}, {0,1}}};
        uint32_t SelectionBits = (Seed * 2891336453u + 1013904223u) ^ (Pixel * 277803737u + 13u);
        float Sum = 0, Count = 0, ChosenRadiance = 0, ChosenTarget = 0;
        for (const auto& Offset : Offsets)
        {
            const int32_t DonorX = X + Offset[0], DonorY = Y + Offset[1];
            if (DonorX < 0 || DonorY < 0 || DonorX >= int32_t(Scene.Width) || DonorY >= int32_t(Scene.Height))
                continue;
            const uint32_t Donor = uint32_t(DonorY) * Scene.Width + uint32_t(DonorX);
            const auto Source = ReplayTailSlot(Compute, 2, Donor);
            const auto Radiance = ReplayTailSlot(Compute, 3, Donor);
            Require(Source[3] == float(Candidates), "Spatial donor lost its initial candidate count");
            const float Weight = Radiance[3] * Source[2] * Source[3];
            Sum += Weight;
            Count += Source[3];
            const float Draw = float(NextReplayTailRandom(SelectionBits) >> 8) / 16777216.f;
            if (Draw * Sum < Weight) { ChosenRadiance = Radiance[0]; ChosenTarget = Radiance[3]; }
        }
        Require(ChosenTarget > 0 && Count >= float(3 * Candidates), "Spatial oracle did not select a donor");
        const float Expected = ChosenRadiance * (Sum / (Count * ChosenTarget));
        MaximumError = std::max(MaximumError, std::abs(double(Image[Pixel][0]) - Expected));
        ++Compared;
    }
    const auto Links = Compute.ExtendedStatistics()[22];
    Require(Compared == 1024 && Links == 3968 && MaximumError < 2e-5,
            "Spatial multi-candidate merge differs from the host reservoir");
    std::printf("CANDIDATE_SPATIAL candidates=4 pixels=%u links=%u host_max_error=%.9g\n",
                Compared, Links, MaximumError);
}

//------------------------------------------------------------------------------------------------------------------------
//                                         MULTIBOUNCE AND OCCLUSION
//------------------------------------------------------------------------------------------------------------------------

LightComputeArchive ResizeReplayCandidateScene(LightComputeArchive Scene)
{
    const auto Receiver = Scene.Receivers[size_t(Scene.Width) + 1];
    Scene.Width = Scene.Height = 32;
    Scene.Receivers.assign(size_t(Scene.Width) * Scene.Height, Receiver);
    for (uint32_t Y = 0; Y < Scene.Height; ++Y)
        for (uint32_t X = 0; X < Scene.Width; ++X)
            Scene.Receivers[size_t(Y) * Scene.Width + X].MotionPixel = {float(X), float(Y), .2f, 1};
    return Scene;
}

void ValidateReplayCandidateRoom(const char* Driver, const std::string& Shaders)
{
    auto Open = ResizeReplayCandidateScene(ReplayRoom(7));
    auto Covered = ResizeReplayCandidateScene(ReplayRoom(0));
    for (auto* Scene : {&Open, &Covered})
        for (auto& Receiver : Scene->Receivers)
        {
            Receiver.PositionHit = {1.4f,2.f,0,1};
            Receiver.NormalRoughness = {0,0,1,1};
            Receiver.ViewMetallic = {0,0,1,0};
            Receiver.AlbedoIor = {.7f,.7f,.7f,1.5f};
            Receiver.ResidualSpecular = {0,0,0,.5f};
            Receiver.MotionPositionToken = {1.4f,2.f,0,1};
            Receiver.MotionNormal = {0,0,1,0};
        }
    std::printf("CANDIDATE_ROOM tracing 400000 independent CPU paths per scene\n");
    std::fflush(stdout);
    const std::array<ReplayRgbMoments,2> References{ReplayReferenceIntegrate(Open, 4, 581001),
                                                    ReplayReferenceIntegrate(Covered, 4, 582001)};
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    std::array<std::array<ReplayRgbMoments,2>,2> Pixels, Batches;
    constexpr uint32_t BatchCount = 32;
    const std::array<float,3> Environment{.025f,.025f,.025f};
    for (uint32_t Batch = 0; Batch < BatchCount; ++Batch)
        for (uint32_t SceneIndex = 0; SceneIndex < 2; ++SceneIndex)
            for (uint32_t Mode = 0; Mode < 2; ++Mode)
            {
                const auto& Scene = SceneIndex == 0 ? Open : Covered;
                const uint32_t Candidates = Mode == 0 ? 1 : 4;
                const auto Image = ExecuteReplayCandidates(Compute, Scene, 4, 583001 + Batch * 7919u,
                                                           Candidates, false, false, true, false, Environment);
                ReplayRgbMoments Local;
                for (const auto& Colour : Image)
                    for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    {
                        Pixels[SceneIndex][Mode][Channel].Accumulate(Colour[Channel]);
                        Local[Channel].Accumulate(Colour[Channel]);
                    }
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    Batches[SceneIndex][Mode][Channel].Accumulate(Local[Channel].Mean());
            }
    for (uint32_t SceneIndex = 0; SceneIndex < 2; ++SceneIndex)
        for (uint32_t Mode = 0; Mode < 2; ++Mode)
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                const auto& Reference = References[SceneIndex][Channel];
                const auto& Samples = Pixels[SceneIndex][Mode][Channel];
                const double CpuSe = ReplaySampleStandardError(Reference);
                const double PixelSe = ReplaySampleStandardError(Samples);
                const double BatchSe = ReplaySampleStandardError(Batches[SceneIndex][Mode][Channel]);
                const double Tolerance = 6 * std::hypot(CpuSe, std::max(PixelSe, BatchSe)) +
                                         .01 * std::abs(Reference.Mean()) + 1e-4;
                const uint32_t Candidates = Mode == 0 ? 1u : 4u;
                std::printf("CANDIDATE_ROOM scene=%s count=%u channel=%u cpu=%.9g cpu_se=%.9g gpu=%.9g "
                            "batch_se=%.9g sd_ratio=%.9g cpu_paths=%u gpu_outputs=%u initial_paths=%u batches=%u\n",
                            SceneIndex == 0 ? "open" : "covered", Candidates, Channel,
                            Reference.Mean(), CpuSe, Samples.Mean(), BatchSe,
                            Samples.Deviation() / Pixels[SceneIndex][0][Channel].Deviation(),
                            Reference.Count, Samples.Count, Samples.Count * Candidates, BatchCount);
                Require(std::abs(Samples.Mean() - Reference.Mean()) < Tolerance,
                        "Multibounce candidate replay disagrees with independent CPU paths");
            }
    Require(References[0][0].Mean() > References[1][0].Mean() + 5 *
                std::hypot(ReplaySampleStandardError(References[0][0]), ReplaySampleStandardError(References[1][0])),
            "Room blocker did not produce a statistically resolved reference difference");
}

bool ReplayCandidateImagesEqual(const std::vector<std::array<float,4>>& Left,
                                const std::vector<std::array<float,4>>& Right)
{
    return Left.size() == Right.size() &&
           std::memcmp(Left.data(), Right.data(), Left.size() * sizeof(Left[0])) == 0;
}

void ValidateReplayCandidateMotion(const char* Driver, const std::string& Shaders)
{
    const auto Open = ResizeReplayCandidateScene(ReplayMultibounceFixture(false, true));
    const auto Covered = ResizeReplayCandidateScene(ReplayMultibounceFixture(true, true));
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Open, Shaders, false, false, false, false, true);
    Filtered.Prepare(Open, Shaders, false, false, false, false, true);
    std::vector<std::array<float,4>> Raw, Result;
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
    {
        Raw = ExecuteReplayCandidates(Fresh, Open, 4, 480000 + Frame, 4, true, true, Frame == 0, false);
        Result = ExecuteReplayCandidates(Filtered, Open, 4, 480000 + Frame, 4, true, true, Frame == 0, true);
        Require(Frame == 0 || (Filtered.ExtendedStatistics()[20] == 1024 && Filtered.ExtendedStatistics()[23] == 1024),
                "Static multi-candidate temporal history was not exercised");
        const auto Local = ReplayTailSlot(Filtered, 2, 17);
        Require(Local[3] == float(4 * (Frame + 1)), "Multi-candidate temporal tally is incorrect");
    }
    DirectGpuMoments OpenMoments;
    for (const auto& Colour : Raw) OpenMoments.Accumulate(Colour[0]);
    Require(std::abs(OpenMoments.Mean() - .625) < .003, "Four-bounce parallel-plane reference failed");
    Raw = ExecuteReplayCandidates(Fresh, Covered, 4, 480100, 4, true, true, false, false);
    Result = ExecuteReplayCandidates(Filtered, Covered, 4, 480100, 4, true, true, false, true);
    const auto BlockedCounts = Filtered.ExtendedStatistics();
    Require(!Filtered.ReplayLastReset && Filtered.ReplayLastHistoryCap == 1 &&
                Filtered.ReplayLastReconstructionReset && BlockedCounts[20] == 1024 &&
                BlockedCounts[22] == 3968 && BlockedCounts[23] == 0 && ReplayCandidateImagesEqual(Raw, Result) &&
                std::all_of(Result.begin(), Result.end(), [](const auto& Pixel)
                            { return Pixel[0] == 0 && Pixel[1] == 0 && Pixel[2] == 0; }),
            "Moving blocker retained stale multi-candidate colour or failed to replay histories");
    Raw = ExecuteReplayCandidates(Fresh, Open, 4, 480101, 4, true, true, false, false);
    Result = ExecuteReplayCandidates(Filtered, Open, 4, 480101, 4, true, true, false, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 && ReplayCandidateImagesEqual(Raw, Result) &&
                Filtered.ReplayLastHistoryCap == 1,
            "Reopening the blocker retained stale colour");
    Raw = ExecuteReplayCandidates(Fresh, Open, 4, 480102, 2, true, true, false, false);
    Result = ExecuteReplayCandidates(Filtered, Open, 4, 480102, 2, true, true, false, true);
    const auto ChangedCount = ReplayTailSlot(Filtered, 2, 17);
    Require(Filtered.ReplayLastHistoryCap == 1 && Filtered.ReplayLastReconstructionReset &&
                Filtered.ExtendedStatistics()[23] == 0 && ChangedCount[3] == 3 && ReplayCandidateImagesEqual(Raw, Result),
            "Changing candidate count failed to cap reservoir history and reset accumulated colour");
    (void)ExecuteReplayCandidates(Filtered, Open, 4, 480103, 2, true, true, false, true);
    const auto ContinuedCount = ReplayTailSlot(Filtered, 2, 17);
    Require(Filtered.ReplayLastHistoryCap == 64 && !Filtered.ReplayLastReconstructionReset &&
                Filtered.ExtendedStatistics()[23] == 1024 && ContinuedCount[3] == 5,
            "Stable candidate count did not resume colour and reservoir history");
    std::printf("CANDIDATE_MOTION bounces=4 open_mean=%.9g reference_nearly=.625 blocker_temporal=%u "
                "spatial=%u blocker_colour=%u blocked_black=1 reopen_raw_exact=1 "
                "revision_tally=%.0f continued_tally=%.0f\n", OpenMoments.Mean(), BlockedCounts[20],
                BlockedCounts[22], BlockedCounts[23], ChangedCount[3], ContinuedCount[3]);
}

void ValidateReplayCandidateCamera(const char* Driver, const std::string& Shaders)
{
    auto Previous = ReplayTexturedMotionRigid(0, 0, 0, 7, 32, 32);
    auto Current = ReplayTexturedMotionRigid(.07f, .12f, 1, 7, 32, 32);
    SetRoughTextureReplayMaterials(Previous, .65f, .35f);
    SetRoughTextureReplayMaterials(Current, .48f, .7f);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Filtered.Prepare(Previous, Shaders, false, false, false, false, true);
    const auto Render = [](LightComputeSubmission& Compute, const LightComputeArchive& Scene, uint32_t Seed,
                           bool Reset, bool Reconstruct)
    {
        return Compute.ExecuteReplay(Scene, 4, Seed, true, true, Reset, {.025f,.025f,.025f}, .01f,
                                     true, false, true, true, false, false, 64, true, true, true,
                                     true, true, false, false, Reconstruct, 4);
    };
    for (uint32_t Frame = 0; Frame < 3; ++Frame)
    {
        (void)Render(Fresh, Previous, 490001 + Frame, Frame == 0, false);
        (void)Render(Filtered, Previous, 490001 + Frame, Frame == 0, true);
    }
    const auto Raw = Render(Fresh, Current, 490020, false, false);
    const auto Result = Render(Filtered, Current, 490020, false, true);
    const auto Counts = Filtered.ExtendedStatistics();
    const auto Guides = Filtered.PrimaryReceivers();
    uint32_t Textured = 0, MovedHistory = 0, Radiant = 0;
    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
    {
        Textured += Guides[Pixel].ExpectedDirect[3] > 0 ? 1u : 0u;
        MovedHistory += ReplayTailSlot(Filtered, 2, Pixel)[3] == 5 ? 1u : 0u;
        Radiant += Result[Pixel][0] > 0 ? 1u : 0u;
    }
    Require(!Filtered.ReplayLastReset && Filtered.ReplayLastHistoryCap == 1 &&
                Filtered.ReplayLastReconstructionReset && Counts[20] > 0 && Counts[22] > 0 && Counts[23] == 0 &&
                Textured > 20 && MovedHistory == Counts[20] && Radiant > 100 && ReplayCandidateImagesEqual(Raw, Result),
            "Moving rough-textured primary paths failed multi-candidate capped replay or colour reset");
    std::printf("CANDIDATE_CAMERA shift=.07 rotation=.12 candidates=4 bounces=4 textured=%u temporal=%u "
                "spatial=%u colour=%u radiant=%u raw_exact=1\n", Textured, Counts[20], Counts[22], Counts[23], Radiant);
}

void ValidateReplayCandidates(const char* Driver, const std::string& Shaders)
{
    ValidateReplayCandidateOracle(Driver, Shaders);
    ValidateReplayCandidateEncoding(Driver, Shaders);
    ValidateReplayCandidateConvergence(Driver, Shaders);
    ValidateReplayCandidateSpatial(Driver, Shaders);
    ValidateReplayCandidateRoom(Driver, Shaders);
    ValidateReplayCandidateMotion(Driver, Shaders);
    ValidateReplayCandidateCamera(Driver, Shaders);
    std::printf("PASS bounded initial replay candidates, independent occlusion and multibounce motion\n");
}
} // namespace
