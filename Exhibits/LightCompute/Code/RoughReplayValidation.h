/*==========================================================================================================================================
                                                  ROUGHREPLAYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Audit rough opaque replay against independent hemispherical quadrature and material changes.
#pragma once

namespace
{
std::array<double,3> RoughReplayQuadrature(const LightComputeReceiver& R, uint32_t Steps)
{
    std::array<double,3> Result{};
    constexpr double Pi = 3.14159265358979323846;
    const uint32_t Azimuths = 2*Steps;
    for (uint32_t I = 0; I < Steps; ++I)
    {
        const double Mu = (I+.5)/Steps, Radius = std::sqrt(1-Mu*Mu);
        for (uint32_t J = 0; J < Azimuths; ++J)
        {
            const double Phi = 2*Pi*(J+.5)/Azimuths;
            const auto Value = RoughReplayBrdf(R, {Radius*std::cos(Phi),Radius*std::sin(Phi),Mu});
            for (uint32_t C = 0; C < 3; ++C) Result[C] += Value[C]*Mu;
        }
    }
    for (double& V : Result) V *= 2*Pi/(double(Steps)*Azimuths);
    return Result;
}
LightComputeArchive RoughReplayFixture(float Metallic, float Roughness, float ViewAngle)
{
    auto Scene = DirectGpuFixture(false);
    Scene.Lights.clear();
    for (auto& R : Scene.Receivers)
        if (R.PositionHit[3] != 0)
        {
            R.NormalRoughness = {0,0,1,Roughness};
            R.ViewMetallic = {std::sin(ViewAngle),0,std::cos(ViewAngle),Metallic};
            R.AlbedoIor = {.8f,.3f,.1f,1.5f};
            R.ResidualSpecular = {0,0,0,.5f};
        }
    return Scene;
}
void ValidateRoughReplay(const char* Driver, const std::string& Shaders)
{
    const auto Dielectric = RoughReplayFixture(0,.65f,.35f), Metal = RoughReplayFixture(.9f,.45f,.9f);
    const auto& D = Dielectric.Receivers[size_t(Dielectric.Width)+1];
    const auto& M = Metal.Receivers[size_t(Metal.Width)+1];
    const auto ExpectedD = RoughReplayQuadrature(D, 512), CoarseD = RoughReplayQuadrature(D, 256);
    const auto ExpectedM = RoughReplayQuadrature(M, 512), CoarseM = RoughReplayQuadrature(M, 256);
    for (uint32_t C = 0; C < 3; ++C)
        Require(std::abs(ExpectedD[C]-CoarseD[C]) < 1e-4 && std::abs(ExpectedM[C]-CoarseM[C]) < 1e-4,
                "Rough hemisphere quadrature did not converge");
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Dielectric, Shaders, false, false, false, false, true);
    ReplayGroupedMoments FreshD, FreshM, ReusedM;
    uint32_t Accepted = 0, Links = 0;
    const std::array<float,3> White{1,1,1};
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 12000 + 100*Batch;
        FreshD.Accumulate(Compute.ExecuteReplay(Dielectric, 1, Seed, false, false, true, White, .01f, false, false, true),
                          Dielectric.Width);
        FreshM.Accumulate(Compute.ExecuteReplay(Metal, 1, Seed+1, false, false, true, White, .01f, false, false, true),
                          Metal.Width);
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(Dielectric, 1, Seed+10+Frame, true, false, Frame == 0, White, .01f, false, false, true);
        ReusedM.Accumulate(Compute.ExecuteReplay(Metal, 1, Seed+19, true, true, false, White, .01f, false, false, true),
                           Metal.Width);
        const auto Counts = Compute.ExtendedStatistics();
        Accepted += Counts[20]; Links += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1,
                "Rough material transition unexpectedly reset or retained long history");
    }
    auto Compare = [&](const char* Name, const std::array<double,3>& Expected, const ReplayGroupedMoments& Actual)
    {
        for (uint32_t C = 0; C < 3; ++C)
        {
            const double Se = std::max(Actual.Pixels[C].StandardError(), Actual.Batches[C].StandardError());
            std::printf("ROUGH_REPLAY %s channel=%u quadrature=%.9g gpu=%.9g pixel_se=%.9g batch_se=%.9g n=%u\n",
                        Name, C, Expected[C], Actual.Pixels[C].Mean(), Actual.Pixels[C].StandardError(),
                        Actual.Batches[C].StandardError(), Actual.Pixels[C].Count);
            Require(std::abs(Expected[C]-Actual.Pixels[C].Mean()) < 6*Se + .002,
                    "Rough opaque replay disagrees with hemispherical quadrature");
        }
    };
    Compare("dielectric_fresh", ExpectedD, FreshD);
    Compare("metal_fresh", ExpectedM, FreshM);
    Compare("metal_reused", ExpectedM, ReusedM);
    auto TooSmooth = Metal;
    for (auto& R : TooSmooth.Receivers) if (R.PositionHit[3] != 0) R.NormalRoughness[3] = .05f;
    bool RejectedSmooth = false;
    try { (void)Compute.ExecuteReplay(TooSmooth, 1, 1, false, false, true, White, .01f, false, false, true); }
    catch (const std::exception&) { RejectedSmooth = true; }
    Require(Accepted == 8*5120 && Links > 0 && RejectedSmooth, "Rough replay reuse/guard was not exercised");
    std::printf("ROUGH_REPLAY accepted=%u spatial_links=%u unsupported_near_delta_rejected=%u\n",
                Accepted, Links, RejectedSmooth ? 1u : 0u);
    std::printf("PASS guarded rough opaque environment replay; not general materials\n");
}
void ValidateRoughReplayRoom(const char* Driver, const std::string& Shaders)
{
    auto Open = ReplayRoom(7), Blocked = ReplayRoom(0);
    for (auto* Scene : {&Open, &Blocked})
        for (auto& F : Scene->Surfaces)
        {
            const int Token = int(F.OpticalToken[3]);
            F.OpticalToken[2] = Token == 6 ? 0 : .5f;
            if (Token == 2 || Token == 4)
            {
                F.OpticalToken[0] = Token == 4 ? .7f : .35f;
                F.AlbedoRoughness[3] = .6f;
            }
        }
    auto Receivers = DirectGpuFixture(false);
    for (auto& R : Receivers.Receivers)
        if (R.PositionHit[3] != 0)
        {
            R.PositionHit = {1.4f,2,0,1}; R.MotionPositionToken = {1.4f,2,0,1};
            R.NormalRoughness = {0,0,1,.7f}; R.ViewMetallic = {0,0,1,.2f};
            R.AlbedoIor = {.7f,.7f,.7f,1.5f}; R.ResidualSpecular = {0,0,0,.5f};
        }
    Open.Width = Blocked.Width = Receivers.Width; Open.Height = Blocked.Height = Receivers.Height;
    Open.Receivers = Blocked.Receivers = Receivers.Receivers;
    std::printf("ROUGH_ROOM tracing two 400000-path CPU baselines across four hits\n"); std::fflush(stdout);
    const auto CpuOpen = ReplayReferenceIntegrate(Open, 4, 918273, true);
    const auto CpuBlocked = ReplayReferenceIntegrate(Blocked, 4, 746382, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Open, Shaders, false, false, false, false, true);
    ReplayGroupedMoments FreshOpen, FreshBlocked, ReusedBlocked;
    uint32_t Accepted = 0, Links = 0;
    const std::array<float,3> Environment{.025f,.025f,.025f};
    for (uint32_t Batch = 0; Batch < 8; ++Batch)
    {
        const uint32_t Seed = 15000 + 100*Batch;
        FreshOpen.Accumulate(Compute.ExecuteReplay(Open, 4, Seed, false, false, true, Environment, .01f, false, false, true), Open.Width);
        FreshBlocked.Accumulate(Compute.ExecuteReplay(Blocked, 4, Seed+1, false, false, true, Environment, .01f, false, false, true), Open.Width);
        for (uint32_t Frame = 0; Frame < 8; ++Frame)
            (void)Compute.ExecuteReplay(Open, 4, Seed+10+Frame, true, false, Frame == 0, Environment, .01f, false, false, true);
        ReusedBlocked.Accumulate(Compute.ExecuteReplay(Blocked, 4, Seed+19, true, true, false, Environment, .01f, false, false, true), Open.Width);
        const auto Counts = Compute.ExtendedStatistics();
        Accepted += Counts[20]; Links += Counts[22];
        Require(!Compute.ReplayLastReset && Compute.ReplayLastHistoryCap == 1, "Rough occlusion transition lost its history");
    }
    ReplayCompareRgb("rough_open_four", CpuOpen, FreshOpen);
    ReplayCompareRgb("rough_blocked_fresh_four", CpuBlocked, FreshBlocked);
    ReplayCompareRgb("rough_blocked_reused_four", CpuBlocked, ReusedBlocked);
    Require(Accepted == 8*5120 && Links > 0, "Rough room did not exercise dynamic and spatial reuse");
    std::printf("ROUGH_ROOM accepted=%u spatial_links=%u cpu_rng=mt19937_64 brute_force=1\n", Accepted, Links);
    std::printf("PASS rough four-hit moving-occluder reference; no refraction, textures or production claim\n");
}
} // namespace
