/*==========================================================================================================================================
                                                REPLAYMOTIONVALIDATION.H
==========================================================================================================================================*/
// 🧩 Audit primary-ray camera reprojection and bounded temporal-guide rejection.
#pragma once

namespace
{
LightComputeArchive ReplayMotionCamera(float Shift, float Blocker = 7, uint32_t Width = 96, uint32_t Height = 96)
{
    auto Scene = ReplayRoom(Blocker);
    Scene.Width = Width; Scene.Height = Height;
    Scene.Receivers.resize(size_t(Scene.Width) * Scene.Height);
    Scene.Camera[2][3] = Scene.Camera[3][3] * float(Width) / float(Height);
    for (uint32_t I = 0; I < 4; ++I) Scene.Camera[4+I] = Scene.Camera[I];
    Scene.Camera[0][0] += Shift;
    return Scene;
}
LightComputeArchive ReplayMotionRigid(float Shift, float Angle, uint32_t Width = 96, uint32_t Height = 96,
                                      float Blocker = 7)
{
    auto Scene = ReplayMotionCamera(Shift, Blocker, Width, Height);
    const float Cosine = std::cos(Angle), Sine = std::sin(Angle);
    for (auto& F : Scene.Surfaces)
        if (F.OpticalToken[3] > 7.5f)
        {
            for (auto* P : {&F.A, &F.B, &F.C})
            {
                const float X = (*P)[0], Y = (*P)[1];
                (*P)[0] = Cosine*X - Sine*Y + .8f*Angle; (*P)[1] = Sine*X + Cosine*Y;
            }
            for (auto* N : {&F.NormalA, &F.NormalB, &F.NormalC})
            {
                const float X = (*N)[0], Y = (*N)[1];
                (*N)[0] = Cosine*X - Sine*Y; (*N)[1] = Sine*X + Cosine*Y;
            }
        }
    std::vector<std::array<SpatialVector,3>> Geometry;
    for (const auto& F : Scene.Surfaces)
        Geometry.push_back({SpatialVector{F.A[0],F.A[1],F.A[2]}, SpatialVector{F.B[0],F.B[1],F.B[2]},
                            SpatialVector{F.C[0],F.C[1],F.C[2]}});
    const auto Old = Scene.Surfaces;
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    for (size_t I = 0; I < Scene.Surfaces.size(); ++I)
    {
        Scene.Surfaces[I] = Old[Scene.Visibility.OriginalFacets[I]];
        Scene.Surfaces[I].A = Scene.Visibility.Corners[I].A;
        Scene.Surfaces[I].B = Scene.Visibility.Corners[I].B;
        Scene.Surfaces[I].C = Scene.Visibility.Corners[I].C;
    }
    Scene.Camera[8] = {Angle,.8f*Angle,7.5f,0};
    Scene.Camera[9] = {0,0,7.5f,0};
    Scene.Camera[11] = {Cosine,Sine,1,0};
    return Scene;
}
std::array<uint32_t,24> ReplayCameraTransition(LightComputeSubmission& Compute, const LightComputeArchive& Previous,
                                               const LightComputeArchive& Current, bool Strict)
{
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
        (void)Compute.ExecuteReplay(Previous, 1, 41000 + Frame, true, false, Frame == 0,
                                    {.025f,.025f,.025f}, .01f, true, false, false, true, false, Strict);
    (void)Compute.ExecuteReplay(Current, 1, 41010, true, false, false,
                                {.025f,.025f,.025f}, .01f, true, false, false, true, false, Strict);
    Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
            "Camera motion unexpectedly reset or failed to cap replay history");
    return Compute.ExtendedStatistics();
}
void ValidateReplayMotionGuards(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayOcclusionFixture(0);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    DirectGpuMoments Approximate;
    std::array<uint32_t,7> Accepted{};
    for (uint32_t Case = 0; Case < Accepted.size(); ++Case)
    {
        auto Current = Previous;
        for (auto& R : Current.Receivers)
            if (R.PositionHit[3] != 0)
            {
                R.MotionPositionToken[0] = Case == 1 ? .5f : .05f;
                if (Case == 2) R.MotionPositionToken[3] = 2;
                if (Case == 3) R.MotionNormal = {0,0,-1,0};
                if (Case == 4 || Case == 5) R.MotionNormal = {0,0,0,0};
                if (Case == 5) R.MotionPositionToken[0] = 0;
                if (Case == 6) { R.MotionPositionToken[0] = .3f; R.MotionPixel[2] = 100; }
            }
        const uint32_t Batches = Case == 0 ? 4 : 1;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            for (uint32_t Frame = 0; Frame < 4; ++Frame)
                (void)Compute.ExecuteReplay(Previous, 1, 42000 + Batch * 100 + Frame, true, false, Frame == 0,
                                            {0,0,0}, .01f, false, false, false, true);
            const auto Result = Compute.ExecuteReplay(Current, 1, 42009 + Batch * 100, true, false, false,
                                                      {0,0,0}, .01f, false, false, false, true);
            const auto Counts = Compute.ExtendedStatistics();
            Accepted[Case] += Counts[20];
            Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                    "Changed reprojection guide did not retain capped history");
            if (Case == 0) AccumulateReplayCentres(Approximate, Result, Current.Width);
        }
    }
    const double Reference = ReplayOccludedAreaIntegral(0, 0);
    std::printf("MOTION_GUARDS nearby=%u outside=%u token=%u normal=%u missing_offset=%u missing_exact=%u "
                "radius_cap=%u area=%.9g mean=%.9g se=%.9g n=%u\n", Accepted[0], Accepted[1], Accepted[2], Accepted[3],
                Accepted[4], Accepted[5], Accepted[6], Reference, Approximate.Mean(), Approximate.StandardError(),
                Approximate.Count);
    Require(Accepted[0] == 4 * 5120 && Accepted[1] == 0 && Accepted[2] == 0 && Accepted[3] == 0 &&
                Accepted[4] == 0 && Accepted[5] == 5120 && Accepted[6] == 0 &&
                std::abs(Approximate.Mean()-Reference) < 6*Approximate.StandardError()+.002,
            "Approximate motion guide failed rejection guards or independent area integral");
    bool BadTolerance = false, BadNormal = false, HugeNormal = false;
    auto Invalid = Previous;
    Invalid.Receivers[Previous.Width + 1].MotionPixel[2] = std::numeric_limits<float>::quiet_NaN();
    try { (void)Compute.ExecuteReplay(Invalid, 1, 1, true, false); }
    catch (const std::exception&) { BadTolerance = true; }
    Invalid = Previous;
    Invalid.Receivers[Previous.Width + 1].MotionNormal[0] = std::numeric_limits<float>::quiet_NaN();
    try { (void)Compute.ExecuteReplay(Invalid, 1, 1, true, false); }
    catch (const std::exception&) { BadNormal = true; }
    Invalid = Previous;
    Invalid.Receivers[Previous.Width + 1].MotionNormal[0] = 1e20f;
    try { (void)Compute.ExecuteReplay(Invalid, 1, 1, true, false); }
    catch (const std::exception&) { HugeNormal = true; }
    std::printf("MOTION_INPUT_GUARDS nan_radius=%u nan_normal=%u huge_normal=%u\n", BadTolerance ? 1u : 0u,
                BadNormal ? 1u : 0u, HugeNormal ? 1u : 0u);
    Require(BadTolerance && BadNormal && HugeNormal, "Invalid replay motion fields were not rejected");
}
void ValidateReplayCameraAndBlocker(LightComputeSubmission& Compute, const LightComputeArchive& Previous,
                                    const LightComputeArchive& Current, const char* Label)
{
    constexpr uint32_t Batches = 8;
    std::array<DirectGpuMoments,3> Differences, Reused;
    uint64_t Accepted = 0, SpatialLinks = 0;
    for (uint32_t Batch = 0; Batch < Batches; ++Batch)
    {
        const uint32_t Seed = 45000 + 100 * Batch;
        for (uint32_t Frame = 0; Frame < 3; ++Frame)
            (void)Compute.ExecuteReplay(Previous, 4, Seed + Frame, true, true, Frame == 0,
                                        {.025f,.025f,.025f}, .01f, true, false, false, true);
        const auto Image = Compute.ExecuteReplay(Current, 4, Seed + 10, true, true, false,
                                                 {.025f,.025f,.025f}, .01f, true, false, false, true);
        const auto Counts = Compute.ExtendedStatistics();
        Accepted += Counts[20]; SpatialLinks += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Moving primary replay unexpectedly reset or failed to cap history");
        const auto Fresh = Compute.ExecuteReplay(Current, 4, Seed + 10, false, true, true,
                                                 {.025f,.025f,.025f}, .01f, true, false, false, true);
        std::array<double,3> Diff{}, Sum{};
        for (size_t I = 0; I < Image.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C)
            {
                Require(std::isfinite(Image[I][C]) && Image[I][C] >= 0 && std::isfinite(Fresh[I][C]) &&
                            Fresh[I][C] >= 0, "Invalid moving-camera pixel");
                Diff[C] += double(Image[I][C]) - Fresh[I][C];
                Sum[C] += Image[I][C];
            }
        for (uint32_t C = 0; C < 3; ++C)
        {
            Differences[C].Accumulate(Diff[C] / Image.size());
            Reused[C].Accumulate(Sum[C] / Image.size());
        }
    }
    for (uint32_t C = 0; C < 3; ++C)
    {
        std::printf("MOTION_CAMERA_%s channel=%u reused_mean=%.9g paired_difference=%.9g batch_se=%.9g "
                    "batches=%u temporal=%llu spatial=%llu\n", Label, C, Reused[C].Mean(), Differences[C].Mean(),
                    Differences[C].StandardError(), Batches, (unsigned long long)Accepted, (unsigned long long)SpatialLinks);
        Require(std::abs(Differences[C].Mean()) < 6*Differences[C].StandardError()+.004,
                "Moving-camera replay and fresh four-hit estimates disagree");
    }
    Require(Accepted > uint64_t(Batches) * 5000 && SpatialLinks > 0,
            "Moving-camera four-hit replay did not accept enough guides");
}
void ValidateReplayRigidPrimary(LightComputeSubmission& Compute, const LightComputeArchive& Previous,
                                LightComputeArchive Current)
{
    // First capture actual primary-ray guides, then retain only the rigid object's receivers so the
    // acceptance counter cannot be satisfied by static walls. The previous frame is regenerated afterward.
    (void)Compute.ExecuteReplay(Current, 1, 48000, false, false, true,
                                {.025f,.025f,.025f}, .01f, true, false, false, true);
    Current.Receivers = Compute.PrimaryReceivers();
    uint32_t ObjectPixels = 0;
    for (auto& R : Current.Receivers)
    {
        if (R.PositionHit[3] != 0 && R.MotionPositionToken[3] == 8) ++ObjectPixels;
        else R = {};
    }
    for (uint32_t Frame = 0; Frame < 4; ++Frame)
        (void)Compute.ExecuteReplay(Previous, 1, 48100 + Frame, true, false, Frame == 0,
                                    {.025f,.025f,.025f}, .01f, true, false, false, true);
    (void)Compute.ExecuteReplay(Current, 1, 48110, true, false, false,
                                {.025f,.025f,.025f}, .01f, false, false, false, true);
    const auto Counts = Compute.ExtendedStatistics();
    std::printf("MOTION_RIGID_OBJECT angle=.12 camera_shift=.07 object_pixels=%u accepted=%u rejected=%u "
                "reset=%u cap=%u\n", ObjectPixels, Counts[20], Counts[21], Compute.ReplayLastReset ? 1u : 0u,
                Compute.ReplayLastHistoryCap);
    Require(ObjectPixels > 200 && Counts[20] > ObjectPixels / 2 && !Compute.ReplayLastReset &&
                Compute.ReplayLastHistoryCap == 1, "Rigidly moved primary receivers did not reuse history");
}
void ValidateReplayMotion(const char* Driver, const std::string& Shaders)
{
    const auto Previous = ReplayMotionCamera(0), Current = ReplayMotionCamera(.07f);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Previous, Shaders, false, false, false, false, true);
    const auto Strict = ReplayCameraTransition(Compute, Previous, Current, true);
    const auto Nearby = ReplayCameraTransition(Compute, Previous, Current, false);
    std::printf("MOTION_CAMERA shift=.07 hits=%u exact_accepted=%u exact_rejected=%u nearby_accepted=%u "
                "nearby_rejected=%u reset=%u history_cap=%u\n", Nearby[10], Strict[20], Strict[21], Nearby[20],
                Nearby[21], Compute.ReplayLastReset ? 1u : 0u, Compute.ReplayLastHistoryCap);
    Require(Strict[20] < Nearby[10] / 20 && Nearby[20] > Nearby[10] * 3 / 4,
            "Nearby guide did not improve real primary-camera temporal acceptance");
    ValidateReplayMotionGuards(Driver, Shaders);
    ValidateReplayCameraAndBlocker(Compute, Previous, Current, "only");
    ValidateReplayCameraAndBlocker(Compute, Previous, ReplayMotionCamera(.07f, 0), "and_blocker");
    const auto RigidPrevious = ReplayMotionRigid(0, 0), RigidCurrent = ReplayMotionRigid(.07f, .12f);
    ValidateReplayRigidPrimary(Compute, RigidPrevious, RigidCurrent);
    ValidateReplayCameraAndBlocker(Compute, RigidPrevious, RigidCurrent, "and_rigid");
    std::printf("PASS bounded camera, moving blocker and rigid-object four-hit replay\n");
}
} // namespace
