/*==========================================================================================================================================
                                           REPLAYRECONSTRUCTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Check bounded replay colour accumulation against raw frames, quadrature and invalid-history controls.
#pragma once

namespace
{
LightComputeArchive ReplayReconstructionScene(bool Rough)
{
    auto Scene = ReplayOcclusionFixture(0);
    auto Receiver = Scene.Receivers[size_t(Scene.Width) + 1];
    Receiver.NormalRoughness = {0,0,1,.6f};
    Receiver.ViewMetallic = {0,0,1,Rough ? .7f : 0};
    Receiver.AlbedoIor = Rough ? std::array<float,4>{.8f,.3f,.1f,1.5f}
                              : std::array<float,4>{.5f,.5f,.5f,1.5f};
    Receiver.ResidualSpecular = {0,0,0,.5f};
    Scene.Width = Scene.Height = 32;
    Scene.Receivers.assign(size_t(Scene.Width) * Scene.Height, Receiver);
    for (uint32_t Y = 0; Y < Scene.Height; ++Y)
        for (uint32_t X = 0; X < Scene.Width; ++X)
            Scene.Receivers[size_t(Y) * Scene.Width + X].MotionPixel = {float(X), float(Y), .2f, 1};
    return Scene;
}
std::array<double,3> ReplayReconstructionQuadrature(const LightComputeReceiver& R, bool Rough, uint32_t Steps)
{
    if (!Rough)
    {
        const double Expected = ReplayOccludedAreaIntegral(0, 0, Steps);
        return {Expected,Expected,Expected};
    }
    constexpr double H = .999, Emitter = 2;
    const double Step = 1.0 / Steps;
    std::array<double,3> Sum{};
    for (uint32_t Y = 0; Y < Steps; ++Y)
        for (uint32_t X = 0; X < Steps; ++X)
        {
            const double Px = (X+.5)*Step-.5, Py = (Y+.5)*Step-.5;
            if (Px * (.499 / H) >= 0) continue;
            const double D2 = Px*Px + Py*Py + H*H, D = std::sqrt(D2);
            const auto Brdf = RoughReplayBrdf(R, {Px/D,Py/D,H/D});
            for (uint32_t C = 0; C < 3; ++C) Sum[C] += Brdf[C] * Emitter * H*H / (D2*D2);
        }
    for (double& C : Sum) C *= Step*Step;
    return Sum;
}
std::vector<std::array<float,4>> RunReconstructionReplay(LightComputeSubmission& Compute,
    const LightComputeArchive& Scene, uint32_t Seed, bool Reset, bool Rough, bool Reconstruct,
    bool Temporal = false, bool Spatial = false)
{
    return Compute.ExecuteReplay(Scene, 1, Seed, Temporal, Spatial, Reset, {0,0,0}, .01f, false,
                                 false, Rough, false, false, false, 64, false, false, false,
                                 false, false, false, false, Reconstruct);
}
double ReconstructionRmse(const std::vector<std::array<float,4>>& Image, const std::array<double,3>& Reference)
{
    double Sum = 0;
    for (const auto& Pixel : Image)
        for (uint32_t C = 0; C < 3; ++C)
        {
            const double Difference = Pixel[C] - Reference[C];
            Sum += Difference * Difference;
        }
    return std::sqrt(Sum / (Image.size() * 3));
}
void ValidateReplayReconstructionCase(const char* Driver, const std::string& Shaders, bool Rough)
{
    const auto Scene = ReplayReconstructionScene(Rough);
    const auto Reference = ReplayReconstructionQuadrature(Scene.Receivers[0], Rough, 512);
    const auto Coarse = ReplayReconstructionQuadrature(Scene.Receivers[0], Rough, 256);
    for (uint32_t C = 0; C < 3; ++C)
        Require(std::abs(Reference[C] - Coarse[C]) < 1e-5, "Reconstruction area quadrature did not converge");
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Scene, Shaders, false, false, false, false, true);
    Filtered.Prepare(Scene, Shaders, false, false, false, false, true);
    std::vector<std::array<float,4>> Expected(Scene.Receivers.size()), Raw, Result;
    double MaximumError = 0;
    for (uint32_t Frame = 0; Frame < 20; ++Frame)
    {
        const uint32_t Seed = 681000 + Frame * 7919u;
        Raw = RunReconstructionReplay(Fresh, Scene, Seed, Frame == 0, Rough, false);
        Result = RunReconstructionReplay(Filtered, Scene, Seed, Frame == 0, Rough, true);
        const float Weight = 1.0f / float(std::min(Frame + 1, 16u));
        for (size_t I = 0; I < Result.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C)
            {
                Expected[I][C] += (Raw[I][C] - Expected[I][C]) * Weight;
                MaximumError = std::max(MaximumError, std::abs(double(Expected[I][C]) - Result[I][C]));
            }
        const auto Count = Filtered.ExtendedStatistics()[23];
        Require((Frame == 0 ? Count == 0 && Filtered.ReplayLastReconstructionReset
                            : Count == Scene.Receivers.size() && !Filtered.ReplayLastReconstructionReset),
                "Static reconstruction did not accept/reset the expected history");
    }
    const double FreshError = ReconstructionRmse(Raw, Reference), FilteredError = ReconstructionRmse(Result, Reference);
    Require(MaximumError < 2e-6 && FilteredError < .55 * FreshError,
            "Replay reconstruction differs from the independent frame average or fails bounded RMSE reduction");
    std::printf("RECONSTRUCTION_STATIC rough=%u frames=20 cap=16 accepted=%u average_max_error=%.9g "
                "reference=%.9g,%.9g,%.9g fresh_rmse=%.9g reconstructed_rmse=%.9g ratio=%.9g\n",
                Rough ? 1u : 0u, Filtered.ExtendedStatistics()[23], MaximumError, Reference[0], Reference[1],
                Reference[2], FreshError, FilteredError, FilteredError / FreshError);

    // 📝 Corrupt previous-frame guides after the completed submission, then retain only unaffected pixels.
    auto* Records = static_cast<std::array<float,4>*>(Filtered.Buffers[6].Address);
    const size_t N = Scene.Receivers.size(), Start = 25 * N + 15425;
    const auto Slot = [&](size_t Index, size_t Pixel) -> std::array<float,4>& { return Records[Start + Index*N + Pixel]; };
    Slot(1,0)[3] = 77;                    // Different surface token.
    Slot(1,1)[0] += .5f;                  // Outside the capped position radius.
    Slot(4,2) = {0,0,-1,0};              // Back-facing history normal.
    Slot(7,3)[0] += .1f;                  // Albedo revision.
    Slot(8,4)[0] += .1f;                  // Roughness revision.
    Slot(8,5)[2] += .1f;                  // Specular level revision.
    Slot(9,6)[0] += .2f;                  // View-dependent reflection mismatch.
    Slot(10,7)[0] += .1f;                 // Emissive-radiance revision.
    Slot(5,8)[0] = std::numeric_limits<float>::quiet_NaN();
    Slot(5,9)[3] = 17;                    // Invalid stored history length.
    Slot(8,10)[3] = 7;                    // Authored material identity mismatch.
    Raw = RunReconstructionReplay(Fresh, Scene, 689000, false, Rough, false);
    Result = RunReconstructionReplay(Filtered, Scene, 689000, false, Rough, true);
    Require(!Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == N - 11,
            "Reconstruction guide guards did not isolate all eleven bad histories");
    for (size_t I = 0; I < 11; ++I)
        Require(Result[I] == Raw[I], "An incompatible history contaminated a fresh pixel");
    std::printf("RECONSTRUCTION_GUARDS rough=%u rejected=11 retained=%u raw_exact=1\n",
                Rough ? 1u : 0u, Filtered.ExtendedStatistics()[23]);

    Require(std::any_of(Result.begin(), Result.end(), [](const auto& P) { return P[0] > 0; }),
            "Source-off negative control had no prior radiance");
    auto Relit = Scene;
    for (auto& Facet : Relit.Surfaces) Facet.Emissive = {0,0,0,0};
    Raw = RunReconstructionReplay(Fresh, Relit, 690000, false, Rough, false);
    Result = RunReconstructionReplay(Filtered, Relit, 690000, false, Rough, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 && Result == Raw &&
                std::all_of(Result.begin(), Result.end(), [](const auto& P) { return P[0] == 0 && P[1] == 0 && P[2] == 0; }),
            "Changed emission retained old radiance");
    (void)RunReconstructionReplay(Fresh, Scene, 690001, false, Rough, false);
    Result = RunReconstructionReplay(Filtered, Scene, 690001, false, Rough, true);
    Require(std::any_of(Result.begin(), Result.end(), [](const auto& P) { return P[0] > 0; }),
            "Material-revision negative control had no prior radiance");
    auto RevisedMaterial = Scene;
    for (auto& R : RevisedMaterial.Receivers) R.AlbedoIor[0] = .2f;
    Raw = RunReconstructionReplay(Fresh, RevisedMaterial, 690002, false, Rough, false);
    Result = RunReconstructionReplay(Filtered, RevisedMaterial, 690002, false, Rough, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 && Result == Raw,
            "Changed material retained old radiance");
    (void)RunReconstructionReplay(Fresh, Scene, 690003, false, Rough, false);
    Result = RunReconstructionReplay(Filtered, Scene, 690003, false, Rough, true);
    Require(std::any_of(Result.begin(), Result.end(), [](const auto& P) { return P[0] > 0; }),
            "Blocker negative control had no prior radiance");
    auto Covered = ReplayOcclusionFixture(-5);
    Covered.Width = Scene.Width; Covered.Height = Scene.Height; Covered.Receivers = Scene.Receivers;
    Raw = RunReconstructionReplay(Fresh, Covered, 690004, false, Rough, false);
    Result = RunReconstructionReplay(Filtered, Covered, 690004, false, Rough, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 && Result == Raw,
            "Changed blocker retained old radiance or changed the fresh output");
    std::printf("RECONSTRUCTION_REVISIONS rough=%u blocker_fresh_exact=1 source_off_black=1 material_fresh_exact=1\n",
                Rough ? 1u : 0u);
}
void ValidateReplayReconstructionReservoirs(const char* Driver, const std::string& Shaders)
{
    auto Scene = ReplayReconstructionScene(false);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Scene, Shaders, false, false, false, false, true);
    Filtered.Prepare(Scene, Shaders, false, false, false, false, true);
    std::vector<std::array<float,4>> Expected(Scene.Receivers.size()), Raw, Result;
    std::array<uint32_t,24> LastCounts{};
    double MaximumError = 0;
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
    {
        Raw = RunReconstructionReplay(Fresh, Scene, 700000 + Frame * 7919u, Frame == 0, false, false, true, true);
        Result = RunReconstructionReplay(Filtered, Scene, 700000 + Frame * 7919u, Frame == 0, false, true, true, true);
        const auto Counts = Filtered.ExtendedStatistics();
        LastCounts = Counts;
        if (Frame != 0)
            Require(Counts[20] == Scene.Receivers.size() && Counts[22] > 0 && Counts[23] == Scene.Receivers.size(),
                    "Replay and colour histories were not concurrently exercised");
        for (size_t I = 0; I < Result.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C)
            {
                Expected[I][C] += (Raw[I][C] - Expected[I][C]) / float(Frame + 1);
                MaximumError = std::max(MaximumError, std::abs(double(Expected[I][C]) - Result[I][C]));
            }
    }
    Require(MaximumError < 2e-6, "Reconstruction changed the reservoir output before averaging");
    // Disabling and re-enabling reconstruction must not revive its old colour history.
    (void)RunReconstructionReplay(Fresh, Scene, 770000, false, false, false, true, true);
    (void)RunReconstructionReplay(Filtered, Scene, 770000, false, false, false, true, true);
    Raw = RunReconstructionReplay(Fresh, Scene, 770001, false, false, false, true, true);
    Result = RunReconstructionReplay(Filtered, Scene, 770001, false, false, true, true, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 && Raw == Result,
            "Re-enabling reconstruction retained stale colour");
    // Changing reservoir reuse policy also invalidates accumulated radiance.
    Raw = RunReconstructionReplay(Fresh, Scene, 770002, false, false, false, false, true);
    Result = RunReconstructionReplay(Filtered, Scene, 770002, false, false, true, false, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 && Raw == Result,
            "Changed reservoir-reuse policy retained old colour");
    std::printf("RECONSTRUCTION_RESERVOIRS frames=8 temporal=%u spatial=%u colour=%u average_max_error=%.9g "
                "reenable_reset=1 policy_reset=1\n", LastCounts[20], LastCounts[22], LastCounts[23], MaximumError);
}
void ValidateReplayReconstructionDisocclusion(const char* Driver, const std::string& Shaders)
{
    auto Scene = ReplayReconstructionScene(false);
    Scene.Receivers[0].MotionPixel[0] = -20;
    Scene.Receivers[1].PositionHit[3] = 0;
    Scene.Receivers[1].ResidualSpecular = {.7f,.2f,.1f,0};
    Scene.Receivers[2].MotionPixel[0] = 1e30f; // Finite but far outside the shader's safe integer range.
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Scene, Shaders, false, false, false, false, true);
    Filtered.Prepare(Scene, Shaders, false, false, false, false, true);
    (void)RunReconstructionReplay(Fresh, Scene, 780000, true, false, false, true);
    (void)RunReconstructionReplay(Filtered, Scene, 780000, true, false, true, true);
    auto Raw = RunReconstructionReplay(Fresh, Scene, 780001, false, false, false, true);
    auto Result = RunReconstructionReplay(Filtered, Scene, 780001, false, false, true, true);
    const auto Counts = Filtered.ExtendedStatistics();
    Require(Counts[20] == Scene.Receivers.size() - 3 && Counts[23] == Scene.Receivers.size() - 3 &&
                Raw[0] == Result[0] && Raw[1] == Result[1] && Raw[2] == Result[2],
            "Out-of-bounds or extreme reprojection or background retained reservoir or colour");
    const auto* Records = static_cast<const std::array<float,4>*>(Filtered.Buffers[6].Address);
    const size_t N = Scene.Receivers.size(), Start = 25 * N + 15425;
    Require(Records[Start + 5*N + 1] == std::array<float,4>{0,0,0,0}, "Background stored a reusable history");
    std::printf("RECONSTRUCTION_DISOCCLUSION outside=1 extreme_coordinate=1 background=1 "
                "reservoir=%u colour=%u raw_exact=1\n", Counts[20], Counts[23]);
}
void ValidateReplayReconstructionCamera(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayMotionCamera(0, 7, 32, 32);
    const auto Current = ReplayMotionCamera(.07f, 7, 32, 32);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Previous, Shaders, false, false, false, false, true);
    Filtered.Prepare(Previous, Shaders, false, false, false, false, true);
    const auto Render = [](LightComputeSubmission& Compute, const LightComputeArchive& Scene, uint32_t Seed,
                           bool Reset, bool Reconstruct)
    {
        return Compute.ExecuteReplay(Scene, 1, Seed, true, true, Reset, {.025f,.025f,.025f}, .01f,
                                     true, false, true, false, false, false, 64, false, false, false,
                                     false, false, false, false, Reconstruct);
    };
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
    {
        (void)Render(Fresh, Previous, 790000 + Frame, Frame == 0, false);
        (void)Render(Filtered, Previous, 790000 + Frame, Frame == 0, true);
    }
    const auto Raw = Render(Fresh, Current, 790100, false, false);
    const auto Result = Render(Filtered, Current, 790100, false, true);
    const auto Counts = Filtered.ExtendedStatistics();
    Require(!Filtered.ReplayLastReset && Filtered.ReplayLastHistoryCap == 1 && Filtered.ReplayLastReconstructionReset &&
                Counts[20] > 0 && Counts[23] == 0 && Raw == Result,
            "Camera transition failed to reset colour independently of capped reservoir reuse");
    std::printf("RECONSTRUCTION_CAMERA moved=.07 reservoir_accepted=%u colour_accepted=%u raw_exact=1\n",
                Counts[20], Counts[23]);
}
void ValidateReplayReconstructionTextured(const char* Driver, const std::string& Shaders)
{
    auto Scene = ReplayTexturedMotionRigid(0, 0, 0, 7, 32, 32);
    SetRoughTextureReplayMaterials(Scene, .65f, .35f);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Scene, Shaders, false, false, false, false, true);
    Filtered.Prepare(Scene, Shaders, false, false, false, false, true);
    const auto Render = [](LightComputeSubmission& Compute, const LightComputeArchive& Current, uint32_t Seed,
                           bool Reset, bool Reconstruct)
    {
        return Compute.ExecuteReplay(Current, 2, Seed, true, true, Reset, {.025f,.025f,.025f}, .01f,
                                     true, false, true, true, false, false, 64, true, true, true,
                                     true, true, false, false, Reconstruct);
    };
    const size_t N = Scene.Receivers.size(), Start = 25 * N + 15425;
    std::vector<std::array<float,4>> Expected(N), Raw, Result;
    double MaximumError = 0;
    uint32_t TexturedAccepted = 0;
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
    {
        Raw = Render(Fresh, Scene, 800000 + Frame * 7919u, Frame == 0, false);
        Result = Render(Filtered, Scene, 800000 + Frame * 7919u, Frame == 0, true);
        const auto Guides = Filtered.PrimaryReceivers();
        const auto* Records = static_cast<const std::array<float,4>*>(Filtered.Buffers[6].Address);
        TexturedAccepted = 0;
        for (size_t I = 0; I < N; ++I)
        {
            const bool Accepted = Records[Start + 6*N + I][3] >= 1;
            if (Accepted && Guides[I].ExpectedDirect[3] > 0) ++TexturedAccepted;
            for (uint32_t C = 0; C < 3; ++C)
            {
                const float Length = std::min(Records[Start + 6*N + I][3] + 1, 16.0f);
                Expected[I][C] = Accepted ? Expected[I][C] + (Raw[I][C] - Expected[I][C]) / Length : Raw[I][C];
                MaximumError = std::max(MaximumError, std::abs(double(Expected[I][C]) - Result[I][C]));
            }
        }
    }
    const auto Counts = Filtered.ExtendedStatistics();
    Require(TexturedAccepted > 20 && Counts[23] > 200 && Counts[20] > 0 && Counts[22] > 0 && MaximumError < 2e-6,
            "Textured rough materials did not retain compatible colour/reservoir history");
    auto Changed = Scene;
    Changed.Angles[8] = .2f;
    Raw = Render(Fresh, Changed, 810000, false, false);
    Result = Render(Filtered, Changed, 810000, false, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 && Raw == Result,
            "Changed texture atlas retained old reconstructed colour");
    std::printf("RECONSTRUCTION_TEXTURED textured_accepted=%u colour=%u reservoir=%u spatial=%u "
                "average_max_error=%.9g atlas_reset_raw_exact=1\n", TexturedAccepted, Counts[23], Counts[20],
                Counts[22], MaximumError);
}
void ValidateReplayReconstruction(const char* Driver, const std::string& Shaders)
{
    ValidateReplayReconstructionCase(Driver, Shaders, false);
    ValidateReplayReconstructionCase(Driver, Shaders, true);
    ValidateReplayReconstructionReservoirs(Driver, Shaders);
    ValidateReplayReconstructionDisocclusion(Driver, Shaders);
    ValidateReplayReconstructionCamera(Driver, Shaders);
    ValidateReplayReconstructionTextured(Driver, Shaders);
    std::printf("PASS bounded replay diffuse and rough-specular temporal accumulation\n");
}
void CaptureReplayReconstruction(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Width = 80, Height = 60, Frames = 12;
    const auto Scene = ReplayMotionCamera(0, 7, Width, Height);
    const auto Blocked = ReplayMotionCamera(0, 0, Width, Height);
    LightComputeSubmission Fresh(Driver), Filtered(Driver);
    Fresh.Prepare(Scene, Shaders, false, false, false, false, true);
    Filtered.Prepare(Scene, Shaders, false, false, false, false, true);
    const auto Render = [](LightComputeSubmission& Compute, const LightComputeArchive& Current, uint32_t Seed,
                           bool Reset, bool Reconstruct)
    {
        return Compute.ExecuteReplay(Current, 2, Seed, true, true, Reset, {.025f,.025f,.025f}, .01f,
                                     true, false, true, true, false, false, 64, true, false, false,
                                     false, false, false, false, Reconstruct);
    };
    std::vector<std::array<float,4>> Raw, Result;
    for (uint32_t Frame = 0; Frame < Frames; ++Frame)
    {
        Raw = Render(Fresh, Scene, 820000 + 7919u*Frame, Frame == 0, false);
        Result = Render(Filtered, Scene, 820000 + 7919u*Frame, Frame == 0, true);
    }
    const auto Counts = Filtered.ExtendedStatistics();
    double Difference = 0;
    for (size_t I = 0; I < Raw.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C) Difference += std::abs(double(Raw[I][C]) - Result[I][C]);
    Require(Counts[23] > 100 && Difference > 1, "Room capture did not exercise temporal colour reconstruction");
    const auto RawBlocked = Render(Fresh, Blocked, 830000, false, false);
    const auto FirstBlocked = Render(Filtered, Blocked, 830000, false, true);
    Require(Filtered.ReplayLastReconstructionReset && Filtered.ExtendedStatistics()[23] == 0 && RawBlocked == FirstBlocked,
            "First blocker frame retained stale reconstructed colour");
    std::filesystem::create_directories(Directory);
    WriteImage(Directory + "/ReplayReconstructionFresh.ppm", Raw, Width, Height);
    WriteImage(Directory + "/ReplayReconstructionTemporal.ppm", Result, Width, Height);
    WriteImage(Directory + "/ReplayReconstructionBlockedFirst.ppm", FirstBlocked, Width, Height);
    std::printf("RECONSTRUCTION_CAPTURE %ux%u frames=%u hits_accepted=%u spatial=%u colour=%u "
                "raw_temporal_l1=%.9g blocker_first_raw_exact=1\n", Width, Height, Frames,
                Counts[20], Counts[22], Counts[23], Difference);
    std::printf("PASS room replay reconstruction and first blocker readback\n");
}
} // namespace
