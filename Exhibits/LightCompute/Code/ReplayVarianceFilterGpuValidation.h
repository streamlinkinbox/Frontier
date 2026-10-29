/*==========================================================================================================================================
                                         REPLAYVARIANCEFILTERGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Vulkan GPU validation and game-workload benchmarks for motion-aware SVGF / à-trous replay denoising.
#pragma once

namespace
{

LightComputeArchive MakeTexturedDrivingCorridorScene(float LateralShift, float PreviousShift, uint32_t Size)
{
    auto Scene = ReplayDrivingCorridorScene(LateralShift, PreviousShift, Size);
    std::vector<ReplayTextureColour> MacroChecker(32 * 32);
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
            MacroChecker[size_t(Y) * 32 + X] = ((X / 8 + Y / 8) & 1u) != 0
                ? ReplayTextureColour{0.06f, 0.52f, 0.86f}
                : ReplayTextureColour{0.88f, 0.12f, 0.06f};
    ReplayInstallTexture(Scene, MacroChecker, 32);
    Scene.Angles[4] = Scene.Angles[5] = 0.55f;
    Scene.Angles[6] = 0.25f;
    Scene.Angles[7] = 0.15f;
    Scene.Angles[8] = Scene.Angles[9] = Scene.Angles[10] = 1.0f;
    Scene.Camera[10][2] = 0.0f;
    for (auto& F : Scene.Surfaces)
        if (F.OpticalToken[3] == 1.0f || F.OpticalToken[3] == 2.0f || F.OpticalToken[3] == 4.0f)
        {
            F.Emissive[3] = 1.0f;
            auto AssignUv = [](std::array<float, 4>& Pos, std::array<float, 4>& Norm)
            {
                Pos[3] = 0.90f * Pos[0] + 0.15f * Pos[1];
                Norm[3] = 0.35f * Pos[1] + 0.65f * Pos[2];
            };
            AssignUv(F.A, F.NormalA);
            AssignUv(F.B, F.NormalB);
            AssignUv(F.C, F.NormalC);
        }
    return Scene;
}

double EvaluateMotionCompensatedFlicker(const std::vector<std::array<float, 4>>& PreviousImage,
                                        const std::vector<LightComputeReceiver>& PreviousGuides,
                                        const std::vector<std::array<float, 4>>& CurrentImage,
                                        const std::vector<LightComputeReceiver>& CurrentGuides,
                                        uint32_t Width, uint32_t Height)
{
    auto Lum = [](const std::array<float, 4>& C)
    {
        return 0.2126 * double(C[0]) + 0.7152 * double(C[1]) + 0.0722 * double(C[2]);
    };
    double SquaredDiffSum = 0.0;
    uint32_t Tracked = 0;
    for (size_t Pixel = 0; Pixel < CurrentGuides.size(); ++Pixel)
    {
        const auto& R = CurrentGuides[Pixel];
        if (R.PositionHit[3] == 0.0f || R.MotionPositionToken[3] <= 0.0f || R.MotionPositionToken[3] == 6.0f ||
            R.MotionPixel[3] == 0.0f || !std::isfinite(R.MotionPixel[0]) || !std::isfinite(R.MotionPixel[1]))
            continue;
        const int CornerX = int(std::floor(R.MotionPixel[0]));
        const int CornerY = int(std::floor(R.MotionPixel[1]));
        const double Fx = std::clamp(double(R.MotionPixel[0]) - double(CornerX), 0.0, 1.0);
        const double Fy = std::clamp(double(R.MotionPixel[1]) - double(CornerY), 0.0, 1.0);
        const double ChordLimit = std::max(0.05, double(R.MotionPixel[2]) * 1.35);
        double PrevLumSum = 0.0, WeightSum = 0.0;
        for (int Dy = 0; Dy <= 1; ++Dy)
            for (int Dx = 0; Dx <= 1; ++Dx)
            {
                const int Qx = CornerX + Dx, Qy = CornerY + Dy;
                if (Qx < 0 || Qy < 0 || Qx >= int(Width) || Qy >= int(Height)) continue;
                const size_t Q = size_t(Qy) * Width + size_t(Qx);
                const auto& P = PreviousGuides[Q];
                if (P.PositionHit[3] == 0.0f || P.MotionPositionToken[3] != R.MotionPositionToken[3]) continue;
                double Dist2 = 0.0;
                for (int C = 0; C < 3; ++C)
                {
                    const double D = double(P.PositionHit[C]) - double(R.MotionPositionToken[C]);
                    Dist2 += D * D;
                }
                if (Dist2 > ChordLimit * ChordLimit) continue;
                const double W = (Dx == 0 ? (1.0 - Fx) : Fx) * (Dy == 0 ? (1.0 - Fy) : Fy);
                PrevLumSum += W * Lum(PreviousImage[Q]);
                WeightSum += W;
            }
        if (WeightSum < 0.75) continue;
        const double Diff = Lum(CurrentImage[Pixel]) - (PrevLumSum / WeightSum);
        SquaredDiffSum += Diff * Diff;
        ++Tracked;
    }
    return Tracked > 0 ? std::sqrt(SquaredDiffSum / double(Tracked)) : 0.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  ANALYTIC QUADRATURE AND TEXTURED DEMODULATION
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayVarianceQuadrature(LightComputeSubmission& Compute)
{
    for (bool Rough : {false, true})
    {
        const auto Scene = ReplayReconstructionScene(Rough);
        const auto Reference = ReplayReconstructionQuadrature(Scene.Receivers[0], Rough, 512);

        double FrameZeroRawRmse = 0.0, FrameZeroSvgfRmse = 0.0;
        double FinalRawRmse = 0.0, FinalSvgfRmse = 0.0;
        for (uint32_t Frame = 0; Frame < 16; ++Frame)
        {
            const uint32_t Seed = 910000u + Frame * 7919u;
            const auto Raw = Compute.ExecuteReplay(Scene, 1, Seed, false, false, true, {0, 0, 0}, 0.01f, false,
                                                   false, Rough, false, false, false, 64, false, false, false,
                                                   false, false, false, false, false, 1, false, false, false,
                                                   false, false);
            if (Frame == 0) FrameZeroRawRmse = ReconstructionRmse(Raw, Reference);
            if (Frame == 15) FinalRawRmse = ReconstructionRmse(Raw, Reference);
        }
        for (uint32_t Frame = 0; Frame < 16; ++Frame)
        {
            const uint32_t Seed = 910000u + Frame * 7919u;
            const auto Filtered = Compute.ExecuteReplay(Scene, 1, Seed, false, false, Frame == 0, {0, 0, 0}, 0.01f,
                                                        false, false, Rough, false, false, false, 64, false, false,
                                                        false, false, false, false, false, false, 1, false, false,
                                                        false, false, true);
            const auto Stats = Compute.ExtendedStatistics();
            if (Frame == 0)
            {
                FrameZeroSvgfRmse = ReconstructionRmse(Filtered, Reference);
                Require(Stats[14] == 0 && Stats[15] == Scene.Receivers.size() && Stats[16] > 1000,
                        "Frame-0 SVGF did not activate spatial disocclusion variance and wavelet taps");
            }
            if (Frame == 15)
            {
                FinalSvgfRmse = ReconstructionRmse(Filtered, Reference);
                Require(Stats[14] == Scene.Receivers.size() && Stats[15] == 0,
                        "Converged static SVGF did not retain full temporal history across all pixels");
            }
        }
        std::printf("SVGF_QUADRATURE mode=%s frame0_raw_rmse=%.6f frame0_svgf_rmse=%.6f "
                    "frame15_raw_rmse=%.6f frame15_svgf_rmse=%.6f\n",
                    Rough ? "rough_metallic" : "diffuse", FrameZeroRawRmse, FrameZeroSvgfRmse,
                    FinalRawRmse, FinalSvgfRmse);
        Require(FrameZeroSvgfRmse < FrameZeroRawRmse * 0.45 && FinalSvgfRmse < FinalRawRmse * 0.15 &&
                    FinalSvgfRmse < 0.025,
                "GPU SVGF failed frame-0 disocclusion denoising or 16-frame quadrature convergence");
    }
}

void ValidateReplayVarianceTexturedDemodulation(LightComputeSubmission& Compute)
{
    constexpr uint32_t Size = 32, Bounces = 3;
    const auto Previous = MakeTexturedDrivingCorridorScene(0.0f, 0.0f, Size);
    const auto Current = MakeTexturedDrivingCorridorScene(0.014f, 0.0f, Size);
    const std::array<float, 3> Environment{0.025f, 0.025f, 0.025f};

    // Accumulate 96-pass converged textured reference.
    std::vector<std::array<double, 3>> Reference(Current.Receivers.size(), {0.0, 0.0, 0.0});
    for (uint32_t Pass = 0; Pass < 96; ++Pass)
    {
        const auto Sample = Compute.ExecuteReplay(Current, Bounces, 940001u + Pass * 7919u, false, false, true,
                                                  Environment, 0.01f, true, false, false, true, false, false, 64,
                                                  true, true, false, false, false, false, false, false, 1, false,
                                                  false, false, false, false);
        for (size_t I = 0; I < Sample.size(); ++I)
            for (int C = 0; C < 3; ++C) Reference[I][C] += double(Sample[I][C]) / 96.0;
    }
    const auto Guides = Compute.PrimaryReceivers();

    const auto Fresh = Compute.ExecuteReplay(Current, Bounces, 955003u, false, false, true, Environment, 0.01f,
                                             true, false, false, true, false, false, 64, true, true, false, false,
                                             false, false, false, false, 1, false, false, false, false, false);
    const auto FreshStats = Compute.ExtendedStatistics();

    for (uint32_t Warm = 0; Warm < 4; ++Warm)
        (void)Compute.ExecuteReplay(Previous, Bounces, 951001u + Warm * 733u, true, true, Warm == 0, Environment,
                                    0.01f, true, false, false, true, false, false, 64, true, true, false, false,
                                    false, false, false, false, 1, false, false, false, true, true);
    const auto Denoised = Compute.ExecuteReplay(Current, Bounces, 955003u, true, true, false, Environment, 0.01f,
                                                true, false, false, true, false, false, 64, true, true, false,
                                                false, false, false, false, false, 1, false, false, false, true,
                                                true);
    const auto DenoisedStats = Compute.ExtendedStatistics();

    const auto FreshMetrics = EvaluateCorridorMetrics(Fresh, Reference, Guides, Size, Size, FreshStats);
    const auto DenoisedMetrics = EvaluateCorridorMetrics(Denoised, Reference, Guides, Size, Size, DenoisedStats);

    // Measure checkerboard red-vs-blue chromatic contrast across adjacent textured pixels.
    double RefContrastSum = 0.0, DenoisedContrastSum = 0.0;
    uint32_t ContrastPairs = 0;
    for (uint32_t Y = 1; Y + 1 < Size; ++Y)
        for (uint32_t X = 1; X + 1 < Size; ++X)
        {
            const size_t P = size_t(Y) * Size + X;
            for (size_t Q : {P + 1, P + Size})
            {
                if (Guides[P].PositionHit[3] == 0.0f || Guides[Q].PositionHit[3] == 0.0f ||
                    Guides[P].MotionPositionToken[3] != Guides[Q].MotionPositionToken[3])
                    continue;
                const double AlbedoJump = std::abs(double(Guides[P].AlbedoIor[0]) - double(Guides[Q].AlbedoIor[0]));
                if (AlbedoJump < 0.18) continue;
                RefContrastSum += std::abs(Reference[P][0] - Reference[Q][0]);
                DenoisedContrastSum += std::abs(double(Denoised[P][0]) - double(Denoised[Q][0]));
                ++ContrastPairs;
            }
        }
    const double ContrastRetention = RefContrastSum > 1e-6 ? (DenoisedContrastSum / RefContrastSum) : 1.0;
    std::printf("SVGF_TEXTURED_DEMODULATION %ux%u fresh_rmse=%.6f svgf_rmse=%.6f contrast_pairs=%u "
                "contrast_retention=%.4f reprojected=%u\n",
                Size, Size, FreshMetrics.RmseAll, DenoisedMetrics.RmseAll, ContrastPairs, ContrastRetention,
                DenoisedStats[14]);
    Require(DenoisedMetrics.RmseAll < FreshMetrics.RmseAll * 0.45 && ContrastPairs >= 10 &&
                ContrastRetention >= 0.85 && DenoisedStats[14] > 400,
            "Textured SVGF failed RMSE reduction, checkerboard contrast preservation or subpixel reprojection");
}

//------------------------------------------------------------------------------------------------------------------------
//                                  DYNAMIC LIGHT RESET AND CORRUPTION GUARDS
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayVarianceDynamicAndGuards(LightComputeSubmission& Compute)
{
    constexpr uint32_t Size = 32;
    const auto LitPlane = ReplayCameraPlane(0.0f, 0.0f, 0.0f, 3.0f, Size);
    const auto DarkPlane = ReplayCameraPlane(0.0f, 0.0f, 0.0f, 0.0f, Size);

    // 1. Dynamic light switch-off: zero stale light ghosting when luminaire turns off.
    for (uint32_t Warm = 0; Warm < 5; ++Warm)
        (void)Compute.ExecuteReplay(LitPlane, 2, 960001u + Warm * 997u, true, true, Warm == 0, {0, 0, 0}, 0.01f,
                                    true, false, false, true, false, false, 64, true, false, false, false, false,
                                    false, false, false, 1, false, false, false, true, true);
    const auto SwitchedOff = Compute.ExecuteReplay(DarkPlane, 2, 969991u, true, true, false, {0, 0, 0}, 0.01f,
                                                   true, false, false, true, false, false, 64, true, false, false,
                                                   false, false, false, false, false, 1, false, false, false, true,
                                                   true);
    Require(Compute.ReplayLastVarianceFilterReset && Compute.ExtendedStatistics()[14] == 0,
            "Dynamic source power revision did not trigger PathReplayResetVarianceFilter");
    for (const auto& P : SwitchedOff)
        Require(P[0] == 0.0f && P[1] == 0.0f && P[2] == 0.0f,
                "Stale SVGF irradiance leaked across dynamic light switch-off");

    // 2. Corrupt history buffer injection on a subpixel camera transition.
    const auto SubpixelNext = ReplayCameraPlane(0.35f / float(Size), 0.0f, 0.0f, 3.0f, Size);
    (void)Compute.ExecuteReplay(LitPlane, 2, 970001u, true, true, true, {0, 0, 0}, 0.01f, true, false, false,
                                true, false, false, 64, true, false, false, false, false, false, false, false, 1,
                                false, false, false, true, true);
    const auto Guides = Compute.PrimaryReceivers();
    std::vector<uint32_t> CorruptPixels;
    for (uint32_t Pixel = 0; Pixel < Guides.size() && CorruptPixels.size() < 9; ++Pixel)
        if (Guides[Pixel].MotionPositionToken[3] == 1.0f) CorruptPixels.push_back(Pixel);
    Require(CorruptPixels.size() == 9, "SVGF corruption checks require nine lit floor pixels");
    const float Nan = std::numeric_limits<float>::quiet_NaN();
    const float Inf = std::numeric_limits<float>::infinity();
    for (uint32_t I = 0; I < CorruptPixels.size(); ++I)
    {
        auto IrrSlot = ReplaySampleSlot(Compute, 15, CorruptPixels[I]);
        auto MomSlot = ReplaySampleSlot(Compute, 16, CorruptPixels[I]);
        if (I == 0) IrrSlot[0] = Nan;
        if (I == 1) IrrSlot[1] = Inf;
        if (I == 2) IrrSlot[2] = -0.5f;
        if (I == 3) IrrSlot[3] = 0.0f;
        if (I == 4) IrrSlot[3] = 99.0f;
        if (I == 5) MomSlot[0] = Nan;
        if (I == 6) MomSlot[1] = -2.0f;
        if (I == 7) MomSlot[2] = 1.5f;
        if (I == 8) MomSlot[3] = -0.2f;
        SetReplaySampleSlot(Compute, 15, CorruptPixels[I], IrrSlot);
        SetReplaySampleSlot(Compute, 16, CorruptPixels[I], MomSlot);
    }
    const auto Guarded = Compute.ExecuteReplay(SubpixelNext, 2, 970003u, true, true, false, {0, 0, 0}, 0.01f,
                                               true, false, false, true, false, false, 64, true, false, false,
                                               false, false, false, false, false, 1, false, false, false, true,
                                               true);
    const auto GuardStats = Compute.ExtendedStatistics();
    for (size_t Pixel = 0; Pixel < Guarded.size(); ++Pixel)
    {
        for (int C = 0; C < 3; ++C)
            Require(std::isfinite(Guarded[Pixel][C]) && Guarded[Pixel][C] >= 0.0f,
                    "Corrupt SVGF history slot leaked non-finite or negative colour");
        const auto Persisted = ReplaySampleSlot(Compute, 15, uint32_t(Pixel));
        Require(std::isfinite(Persisted[0]) && std::isfinite(Persisted[3]) && Persisted[3] <= 16.0f,
                "Persisted SVGF history exceeded bounded finite invariants");
    }
    Require(!Compute.ReplayLastVarianceFilterReset && GuardStats[14] > 600 && GuardStats[15] >= 9,
            "Subpixel SVGF guard test did not reproject valid pixels while falling back on corrupted slots");
    std::printf("SVGF_GUARDS light_off_exact_zero=1 corrupt_slots=9 reprojected=%u fallback_disoccluded=%u "
                "all_finite=1\n",
                GuardStats[14], GuardStats[15]);
}

//------------------------------------------------------------------------------------------------------------------------
//                             CONTINUOUS SUBPIXEL DRIVING + WET-GGX + FLICKER BENCHMARK
//------------------------------------------------------------------------------------------------------------------------

void ValidateReplayVarianceDrivingCorridor(LightComputeSubmission& Compute)
{
    constexpr uint32_t Size = 32, Bounces = 4, TrajectorySteps = 8;
    constexpr float StepShift = 0.012f; // ~0.30 px/frame continuous subpixel driving motion!
    std::vector<LightComputeArchive> Steps, WetSteps;
    for (uint32_t Step = 0; Step < TrajectorySteps; ++Step)
    {
        const float Cur = float(Step) * StepShift;
        const float Prev = float(Step > 0 ? Step - 1 : 0) * StepShift;
        Steps.push_back(ReplayDrivingCorridorScene(Cur, Prev, Size));
        WetSteps.push_back(MakeWetDrivingCorridorScene(Cur, Prev, Size));
    }
    const auto& FinalScene = Steps.back();
    const auto& FinalWetScene = WetSteps.back();
    const std::array<float, 3> Environment{0.025f, 0.025f, 0.025f};

    const auto CleanReference = AccumulateConvergedCorridorReference(Compute, FinalScene, Bounces, Environment, 128);
    const auto FinalGuides = Compute.PrimaryReceivers();

    // Evaluate an 8-spp brute-force multi-sample reference on FinalScene to measure ray-efficiency speedups!
    std::vector<std::array<float, 4>> BruteEight(FinalGuides.size(), {0.0f, 0.0f, 0.0f, 1.0f});
    uint64_t BruteEightRays = 0;
    for (uint32_t Spp = 0; Spp < 8; ++Spp)
    {
        const auto PassImg = ExecuteReplaySampleHistory(Compute, FinalScene, 988001u + Spp * 7919u, true, false,
                                                        Bounces, Environment, 1, false, false, false, false, false,
                                                        true, true, false, true);
        const auto Stats = Compute.ExtendedStatistics();
        BruteEightRays += Stats[2] + Stats[8];
        for (size_t I = 0; I < PassImg.size(); ++I)
            for (int C = 0; C < 3; ++C) BruteEight[I][C] += PassImg[I][C] / 8.0f;
    }
    const auto BruteEightMetrics = EvaluateCorridorMetrics(BruteEight, CleanReference, FinalGuides, Size, Size, {});

    auto RunTrajectory = [&](bool UseTemporal, bool UseSpatialDisk, bool UseSeedBank, bool UseSvgf,
                             double& OutFlicker, std::array<uint32_t, 24>& OutFinalStats)
    {
        std::vector<std::array<float, 4>> PrevImg, CurImg;
        std::vector<LightComputeReceiver> PrevGuides, CurGuides;
        double FlickerSum = 0.0;
        uint32_t FlickerPairs = 0;
        for (uint32_t Step = 0; Step < TrajectorySteps; ++Step)
        {
            const uint32_t Seed = 980001u + Step * 7919u;
            CurImg = Compute.ExecuteReplay(Steps[Step], Bounces, Seed, UseTemporal, UseSpatialDisk, Step == 0,
                                           Environment, 0.01f, true, false, false, true, false, false, 64, true,
                                           false, false, false, false, false, false, false, 1, false, false,
                                           UseSeedBank, UseSpatialDisk, UseSvgf);
            CurGuides = Compute.PrimaryReceivers();
            if (Step >= 2)
            {
                FlickerSum += EvaluateMotionCompensatedFlicker(PrevImg, PrevGuides, CurImg, CurGuides, Size, Size);
                ++FlickerPairs;
            }
            PrevImg = CurImg;
            PrevGuides = CurGuides;
        }
        OutFlicker = FlickerPairs > 0 ? FlickerSum / double(FlickerPairs) : 0.0;
        OutFinalStats = Compute.ExtendedStatistics();
        return CurImg;
    };

    double FreshFlicker = 0.0, DiskFlicker = 0.0, DiskSvgfFlicker = 0.0, UnifiedSvgfFlicker = 0.0;
    std::array<uint32_t, 24> FreshStats{}, DiskStats{}, DiskSvgfStats{}, UnifiedSvgfStats{};
    const auto FreshImg = RunTrajectory(false, false, false, false, FreshFlicker, FreshStats);
    const auto DiskImg = RunTrajectory(true, true, false, false, DiskFlicker, DiskStats);
    const auto DiskSvgfImg = RunTrajectory(true, true, false, true, DiskSvgfFlicker, DiskSvgfStats);
    const auto UnifiedSvgfImg = RunTrajectory(true, true, true, true, UnifiedSvgfFlicker, UnifiedSvgfStats);

    const auto MFresh = EvaluateCorridorMetrics(FreshImg, CleanReference, FinalGuides, Size, Size, FreshStats);
    const auto MDisk = EvaluateCorridorMetrics(DiskImg, CleanReference, FinalGuides, Size, Size, DiskStats);
    const auto MDiskSvgf = EvaluateCorridorMetrics(DiskSvgfImg, CleanReference, FinalGuides, Size, Size, DiskSvgfStats);
    const auto MUnifiedSvgf = EvaluateCorridorMetrics(UnifiedSvgfImg, CleanReference, FinalGuides, Size, Size,
                                                      UnifiedSvgfStats);

    const uint32_t DiskRays = MDisk.BounceQueries + MDisk.ShadowQueries;
    const uint32_t DiskSvgfRays = MDiskSvgf.BounceQueries + MDiskSvgf.ShadowQueries;
    const uint32_t UnifiedSvgfRays = MUnifiedSvgf.BounceQueries + MUnifiedSvgf.ShadowQueries;

    std::printf("SVGF_CORRIDOR_TRAJECTORY %ux%u frames=%u "
                "fresh_rmse=%.6f brute8_rmse=%.6f disk_rmse=%.6f disk_svgf_rmse=%.6f unified_svgf_rmse=%.6f "
                "fresh_edge=%.6f disk_edge=%.6f disk_svgf_edge=%.6f unified_svgf_edge=%.6f "
                "fresh_flicker=%.6f disk_flicker=%.6f disk_svgf_flicker=%.6f unified_svgf_flicker=%.6f "
                "fresh_rays=%u brute8_rays=%llu disk_rays=%u disk_svgf_rays=%u unified_svgf_rays=%u "
                "svgf_reprojected=%u svgf_wavelet_taps=%u svgf_edge_stopped=%u\n",
                Size, Size, TrajectorySteps,
                MFresh.RmseAll, BruteEightMetrics.RmseAll, MDisk.RmseAll, MDiskSvgf.RmseAll, MUnifiedSvgf.RmseAll,
                MFresh.RmseBoundary, MDisk.RmseBoundary, MDiskSvgf.RmseBoundary, MUnifiedSvgf.RmseBoundary,
                FreshFlicker, DiskFlicker, DiskSvgfFlicker, UnifiedSvgfFlicker,
                MFresh.BounceQueries + MFresh.ShadowQueries, (unsigned long long)BruteEightRays,
                DiskRays, DiskSvgfRays, UnifiedSvgfRays,
                DiskSvgfStats[14], DiskSvgfStats[16], DiskSvgfStats[17]);
    std::fflush(stdout);

    // Wet glossy asphalt (alpha = 0.28) + rough concrete kerb GGX trajectory on the same Vulkan submission.
    const auto WetClean = AccumulateConvergedCorridorReference(Compute, FinalWetScene, Bounces, Environment, 128,
                                                               true, true);
    const auto WetFinalGuides = Compute.PrimaryReceivers();
    auto RunWetTrajectory = [&](bool UseSpatialDisk, bool UseSvgf, double& OutFlicker,
                                std::array<uint32_t, 24>& OutStats)
    {
        std::vector<std::array<float, 4>> PrevImg, CurImg;
        std::vector<LightComputeReceiver> PrevGuides, CurGuides;
        double FlickerSum = 0.0;
        uint32_t FlickerPairs = 0;
        for (uint32_t Step = 0; Step < TrajectorySteps; ++Step)
        {
            const uint32_t Seed = 992001u + Step * 7919u;
            CurImg = Compute.ExecuteReplay(WetSteps[Step], Bounces, Seed, UseSpatialDisk, UseSpatialDisk, Step == 0,
                                           Environment, 0.01f, true, false, true, true, false, false, 64, true,
                                           false, true, false, false, false, false, false, 1, false, false,
                                           false, UseSpatialDisk, UseSvgf);
            CurGuides = Compute.PrimaryReceivers();
            if (Step >= 2)
            {
                FlickerSum += EvaluateMotionCompensatedFlicker(PrevImg, PrevGuides, CurImg, CurGuides, Size, Size);
                ++FlickerPairs;
            }
            PrevImg = CurImg;
            PrevGuides = CurGuides;
        }
        OutFlicker = FlickerPairs > 0 ? FlickerSum / double(FlickerPairs) : 0.0;
        OutStats = Compute.ExtendedStatistics();
        return CurImg;
    };

    double WetFreshFlicker = 0.0, WetDiskFlicker = 0.0, WetDiskSvgfFlicker = 0.0;
    std::array<uint32_t, 24> WetFreshStats{}, WetDiskStats{}, WetDiskSvgfStats{};
    const auto WetFreshImg = RunWetTrajectory(false, false, WetFreshFlicker, WetFreshStats);
    const auto WetDiskImg = RunWetTrajectory(true, false, WetDiskFlicker, WetDiskStats);
    const auto WetDiskSvgfImg = RunWetTrajectory(true, true, WetDiskSvgfFlicker, WetDiskSvgfStats);

    const auto MWetFresh = EvaluateCorridorMetrics(WetFreshImg, WetClean, WetFinalGuides, Size, Size, WetFreshStats);
    const auto MWetDisk = EvaluateCorridorMetrics(WetDiskImg, WetClean, WetFinalGuides, Size, Size, WetDiskStats);
    const auto MWetSvgf = EvaluateCorridorMetrics(WetDiskSvgfImg, WetClean, WetFinalGuides, Size, Size,
                                                  WetDiskSvgfStats);

    std::printf("SVGF_WET_GGX_TRAJECTORY %ux%u frames=%u "
                "fresh_rmse=%.6f disk_rmse=%.6f disk_svgf_rmse=%.6f "
                "fresh_flicker=%.6f disk_flicker=%.6f disk_svgf_flicker=%.6f "
                "disk_rays=%u disk_svgf_rays=%u svgf_reprojected=%u\n",
                Size, Size, TrajectorySteps,
                MWetFresh.RmseAll, MWetDisk.RmseAll, MWetSvgf.RmseAll,
                WetFreshFlicker, WetDiskFlicker, WetDiskSvgfFlicker,
                MWetDisk.BounceQueries + MWetDisk.ShadowQueries,
                MWetSvgf.BounceQueries + MWetSvgf.ShadowQueries, WetDiskSvgfStats[14]);
    std::fflush(stdout);

    // Exact zero-extra-ray guarantee + quality & temporal-stability assertions.
    Require(DiskSvgfRays == DiskRays, "GPU SVGF unexpectedly altered ReSTIR BVH ray count");
    Require(MDiskSvgf.RmseAll < MDisk.RmseAll * 0.72 && MUnifiedSvgf.RmseAll < MDisk.RmseAll * 0.68 &&
                MUnifiedSvgf.RmseAll < BruteEightMetrics.RmseAll &&
                DiskSvgfFlicker < DiskFlicker * 0.50 && UnifiedSvgfFlicker < DiskFlicker * 0.45 &&
                DiskSvgfStats[14] > 700 && DiskSvgfStats[17] > 0,
            "Corridor GPU SVGF failed RMSE, brute-force 8-spp parity, temporal flicker or edge-stopping targets");
    Require((MWetSvgf.BounceQueries + MWetSvgf.ShadowQueries) == (MWetDisk.BounceQueries + MWetDisk.ShadowQueries) &&
                MWetSvgf.RmseAll < MWetDisk.RmseAll * 0.78 && WetDiskSvgfFlicker < WetDiskFlicker * 0.55 &&
                WetDiskSvgfStats[14] > 700,
            "Wet-GGX GPU SVGF failed zero-extra-ray RMSE or flicker reduction");
}

void ValidateReplayVarianceFilterAll(const char* Driver, const std::string& Shaders)
{
    // Prepare once with the largest 32x32 archive (14 facets + 527-float texture atlas) and reuse across all 4 suites!
    const auto PrepScene = MakeTexturedDrivingCorridorScene(0.0f, 0.0f, 32);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(PrepScene, Shaders, false, false, false, false, true);

    ValidateReplayVarianceQuadrature(Compute);
    ValidateReplayVarianceTexturedDemodulation(Compute);
    ValidateReplayVarianceDynamicAndGuards(Compute);
    ValidateReplayVarianceDrivingCorridor(Compute);
    std::printf("PASS GPU motion-aware SVGF quadrature, textured demodulation, anti-ghosting guards and driving corridors\n");
}

//------------------------------------------------------------------------------------------------------------------------
//                              64X64 SUBPIXEL DRIVING COMPARISON AND HEATMAP CAPTURE
//------------------------------------------------------------------------------------------------------------------------

void CaptureReplayVarianceFilterComparison(const char* Driver, const std::string& Shaders,
                                           const std::string& Directory)
{
    constexpr uint32_t Size = 64, Bounces = 4, TrajectorySteps = 8;
    constexpr float StepShift = 0.008f; // Continuous ~0.40 px/frame subpixel lateral driving!
    std::vector<LightComputeArchive> CorridorSteps;
    for (uint32_t Step = 0; Step < TrajectorySteps; ++Step)
        CorridorSteps.push_back(ReplayDrivingCorridorScene(float(Step) * StepShift,
                                                           float(Step > 0 ? Step - 1 : 0) * StepShift, Size));
    const auto& Current = CorridorSteps.back();
    const std::array<float, 3> Environment{0.025f, 0.025f, 0.025f};

    LightComputeSubmission Compute(Driver);
    Compute.Prepare(CorridorSteps[0], Shaders, false, false, false, false, true);

    const auto Reference = AccumulateConvergedCorridorReference(Compute, Current, Bounces, Environment, 256);
    const auto Guides = Compute.PrimaryReceivers();

    auto RunCaptureTrajectory = [&](bool UseTemporal, bool UseSpatialDisk, bool UseLegacyReconstruct,
                                    bool UseSeedBank, bool UseSvgf, double& OutFlicker,
                                    std::array<uint32_t, 24>& OutStats)
    {
        std::vector<std::array<float, 4>> PrevImg, CurImg;
        std::vector<LightComputeReceiver> PrevGuides, CurGuides;
        double FlickerSum = 0.0;
        uint32_t FlickerPairs = 0;
        for (uint32_t Step = 0; Step < TrajectorySteps; ++Step)
        {
            const uint32_t Seed = 880001u + Step * 7919u;
            CurImg = Compute.ExecuteReplay(CorridorSteps[Step], Bounces, Seed, UseTemporal, UseSpatialDisk,
                                           Step == 0, Environment, 0.01f, true, false, false, true, false, false,
                                           64, true, false, false, false, false, false, false,
                                           UseLegacyReconstruct, 1, false, UseLegacyReconstruct, UseSeedBank,
                                           UseSpatialDisk, UseSvgf);
            CurGuides = Compute.PrimaryReceivers();
            if (Step >= 2)
            {
                FlickerSum += EvaluateMotionCompensatedFlicker(PrevImg, PrevGuides, CurImg, CurGuides, Size, Size);
                ++FlickerPairs;
            }
            PrevImg = CurImg;
            PrevGuides = CurGuides;
        }
        OutFlicker = FlickerPairs > 0 ? FlickerSum / double(FlickerPairs) : 0.0;
        OutStats = Compute.ExtendedStatistics();
        return CurImg;
    };

    double FreshFlicker = 0.0, LegacyFlicker = 0.0, DiskFlicker = 0.0, DiskSvgfFlicker = 0.0, UnifiedSvgfFlicker = 0.0;
    std::array<uint32_t, 24> FreshStats{}, LegacyStats{}, DiskStats{}, DiskSvgfStats{}, UnifiedSvgfStats{};

    const auto FreshImg = RunCaptureTrajectory(false, false, false, false, false, FreshFlicker, FreshStats);
    const auto LegacyImg = RunCaptureTrajectory(false, false, true, false, false, LegacyFlicker, LegacyStats);
    const auto DiskImg = RunCaptureTrajectory(true, true, false, false, false, DiskFlicker, DiskStats);
    const auto DiskSvgfImg = RunCaptureTrajectory(true, true, false, false, true, DiskSvgfFlicker, DiskSvgfStats);
    const auto UnifiedSvgfImg = RunCaptureTrajectory(true, true, false, true, true, UnifiedSvgfFlicker,
                                                     UnifiedSvgfStats);

    // Build temporal-length + disocclusion diagnostic visualization from the final Unified+SVGF pass!
    std::vector<std::array<float, 4>> RefImg(Guides.size()), SvgfHistoryMap(Guides.size());
    for (size_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
    {
        RefImg[Pixel] = {float(Reference[Pixel][0]), float(Reference[Pixel][1]), float(Reference[Pixel][2]), 1.0f};
        if (Guides[Pixel].PositionHit[3] == 0.0f)
        {
            SvgfHistoryMap[Pixel] = {0.03f, 0.05f, 0.11f, 1.0f};
        }
        else if (Guides[Pixel].MotionPositionToken[3] == 6.0f)
        {
            SvgfHistoryMap[Pixel] = {1.0f, 0.96f, 0.84f, 1.0f};
        }
        else
        {
            const float Length = ReplaySampleSlot(Compute, 15, uint32_t(Pixel))[3];
            const float NormLen = std::clamp(Length / 8.0f, 0.0f, 1.0f);
            // Crimson/amber = disoccluded spatial-variance fallback (Length < 4); emerald/cyan = converged temporal SVGF.
            SvgfHistoryMap[Pixel] = {
                std::clamp(1.0f - NormLen * 0.90f, 0.05f, 1.0f),
                std::clamp(0.18f + NormLen * 0.76f, 0.10f, 1.0f),
                std::clamp(0.12f + NormLen * 0.65f, 0.10f, 1.0f),
                1.0f};
        }
    }

    const auto MFresh = EvaluateCorridorMetrics(FreshImg, Reference, Guides, Size, Size, FreshStats);
    const auto MLegacy = EvaluateCorridorMetrics(LegacyImg, Reference, Guides, Size, Size, LegacyStats);
    const auto MDisk = EvaluateCorridorMetrics(DiskImg, Reference, Guides, Size, Size, DiskStats);
    const auto MDiskSvgf = EvaluateCorridorMetrics(DiskSvgfImg, Reference, Guides, Size, Size, DiskSvgfStats);
    const auto MUnifiedSvgf = EvaluateCorridorMetrics(UnifiedSvgfImg, Reference, Guides, Size, Size, UnifiedSvgfStats);

    std::filesystem::create_directories(Directory);
    WriteImage(Directory + "/SvgfFresh1Spp.ppm", FreshImg, Size, Size, 1.8f);
    WriteImage(Directory + "/SvgfLegacyReconstruct.ppm", LegacyImg, Size, Size, 1.8f);
    WriteImage(Directory + "/SvgfSpatialDiskRaw.ppm", DiskImg, Size, Size, 1.8f);
    WriteImage(Directory + "/SvgfSpatialDiskFiltered.ppm", DiskSvgfImg, Size, Size, 1.8f);
    WriteImage(Directory + "/SvgfUnifiedFiltered.ppm", UnifiedSvgfImg, Size, Size, 1.8f);
    WriteImage(Directory + "/SvgfReference256.ppm", RefImg, Size, Size, 1.8f);
    WriteImage(Directory + "/SvgfHistoryDiagnostic.ppm", SvgfHistoryMap, Size, Size, 1.0f);

    const auto WriteErrorMap = [&](const std::string& Name, const std::vector<std::array<float, 4>>& Candidate)
    {
        std::vector<std::array<float, 4>> Heatmap(Guides.size(), {0.0f, 0.0f, 0.0f, 1.0f});
        for (size_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
        {
            if (Guides[Pixel].PositionHit[3] == 0.0f || Guides[Pixel].MotionPositionToken[3] == 6.0f) continue;
            double Err = 0.0;
            for (uint32_t C = 0; C < 3; ++C)
            {
                const double Diff = double(Candidate[Pixel][C]) - Reference[Pixel][C];
                Err += Diff * Diff;
            }
            const float T = std::clamp(float(std::sqrt(Err / 3.0) * 4.0), 0.0f, 1.0f);
            const float R = std::clamp(T * 2.2f - 0.35f, 0.0f, 1.0f);
            const float G = std::clamp(T < 0.5f ? T * 1.6f : 1.3f - (T - 0.5f) * 0.8f, 0.0f, 1.0f);
            const float B = std::clamp(T < 0.4f ? 0.2f + T * 2.0f : 1.0f - (T - 0.4f) * 1.6f, 0.0f, 1.0f);
            Heatmap[Pixel] = {R * R, G * G, B * B, 1.0f};
        }
        WriteImage(Directory + "/" + Name + ".ppm", Heatmap, Size, Size, 1.0f);
    };
    WriteErrorMap("SvgfErrorFresh", FreshImg);
    WriteErrorMap("SvgfErrorLegacy", LegacyImg);
    WriteErrorMap("SvgfErrorDiskRaw", DiskImg);
    WriteErrorMap("SvgfErrorDiskFiltered", DiskSvgfImg);
    WriteErrorMap("SvgfErrorUnifiedFiltered", UnifiedSvgfImg);

    std::printf("SVGF_CAPTURE %ux%u bounces=%u frames=%u ref_passes=256 "
                "fresh_rmse=%.6f legacy_rmse=%.6f disk_rmse=%.6f disk_svgf_rmse=%.6f unified_svgf_rmse=%.6f "
                "fresh_edge=%.6f legacy_edge=%.6f disk_edge=%.6f disk_svgf_edge=%.6f unified_svgf_edge=%.6f "
                "fresh_flicker=%.6f legacy_flicker=%.6f disk_flicker=%.6f disk_svgf_flicker=%.6f unified_svgf_flicker=%.6f "
                "fresh_rays=%u legacy_rays=%u disk_rays=%u disk_svgf_rays=%u unified_svgf_rays=%u "
                "legacy_retained=%u svgf_reprojected=%u svgf_disoccluded=%u svgf_wavelet_taps=%u svgf_edge_stopped=%u\n",
                Size, Size, Bounces, TrajectorySteps,
                MFresh.RmseAll, MLegacy.RmseAll, MDisk.RmseAll, MDiskSvgf.RmseAll, MUnifiedSvgf.RmseAll,
                MFresh.RmseBoundary, MLegacy.RmseBoundary, MDisk.RmseBoundary, MDiskSvgf.RmseBoundary,
                MUnifiedSvgf.RmseBoundary,
                FreshFlicker, LegacyFlicker, DiskFlicker, DiskSvgfFlicker, UnifiedSvgfFlicker,
                MFresh.BounceQueries + MFresh.ShadowQueries,
                MLegacy.BounceQueries + MLegacy.ShadowQueries,
                MDisk.BounceQueries + MDisk.ShadowQueries,
                MDiskSvgf.BounceQueries + MDiskSvgf.ShadowQueries,
                MUnifiedSvgf.BounceQueries + MUnifiedSvgf.ShadowQueries,
                LegacyStats[23], UnifiedSvgfStats[14], UnifiedSvgfStats[15], UnifiedSvgfStats[16],
                UnifiedSvgfStats[17]);
    std::printf("PASS 64x64 continuous subpixel driving SVGF comparison and error heatmaps\n");
}
} // namespace
