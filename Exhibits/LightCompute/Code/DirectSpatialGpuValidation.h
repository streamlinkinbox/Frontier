/*==========================================================================================================================================
                                              DIRECTSPATIALGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare historical and current Vulkan direct-light reservoirs against exhaustive summation.
#pragma once

namespace
{
struct DirectGpuMoments
{
    double Sum = 0, Squares = 0;
    uint32_t Count = 0;
    void Accumulate(double V) { Sum += V; Squares += V * V; ++Count; }
    double Mean() const { return Sum / Count; }
    double Deviation() const { return std::sqrt(std::max(0.0, Squares / Count - Mean() * Mean())); }
    double StandardError() const { return Deviation() / std::sqrt(double(Count)); }
};
LightComputeArchive DirectGpuFixture(bool Unequal)
{
    LightComputeArchive Scene;
    Scene.Width = Scene.Height = 96;
    Scene.Receivers.resize(size_t(Scene.Width) * Scene.Height);
    const float X0 = Unequal ? .05f : 0, Y0 = Unequal ? 2.0f : 0;
    for (uint32_t TileY = 0; TileY < 32; ++TileY)
        for (uint32_t TileX = 0; TileX < 32; ++TileX)
        {
            const uint32_t CentreX = 3 * TileX + 1, CentreY = 3 * TileY + 1;
            const std::array<std::array<int32_t, 2>, 5> Offsets{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto& Offset : Offsets)
            {
                const uint32_t X = uint32_t(int32_t(CentreX) + Offset[0]);
                const uint32_t Y = uint32_t(int32_t(CentreY) + Offset[1]);
                auto& R = Scene.Receivers[size_t(Y) * Scene.Width + X];
                const float Px = X0 + (Unequal ? .08f * Offset[0] : 0);
                const float Py = Y0 + (Unequal ? .08f * Offset[1] : 0);
                R.PositionHit = {Px, Py, 0, 1};
                R.NormalRoughness = {0, 0, 1, .6f};
                R.ViewMetallic = {0, 0, 1, 0};
                R.AlbedoIor = {.6f, .6f, .6f, 1.5f};
                R.ResidualSpecular = {0, 0, 0, 1};
                R.MotionPositionToken = {Px, Py, 0, 1};
                R.MotionNormal = {0, 0, 1, 0};
                R.MotionPixel = {float(X), float(Y), .2f, 1};
            }
        }
    if (!Unequal)
    {
        // The proposal floor gives equal probability to one upward and fifteen dark lights.
        for (uint32_t Index = 0; Index < 16; ++Index)
        {
            LightComputeShape L;
            L.PositionReach = {0, 0, Index == 0 ? 1.0f : -1.0f, 0};
            L.RadianceCategory = {1e-8f, 1e-8f, 1e-8f, 0};
            L.PeakActive[1] = 1;
            Scene.Lights.push_back(L);
        }
    }
    else
    {
        LightComputeShape Directional;
        Directional.PositionReach = {0, 0, 1, 0};
        Directional.RadianceCategory = {1e-8f, 1e-8f, 1e-8f, 0};
        Directional.PeakActive[1] = 1;
        Scene.Lights.push_back(Directional);
        LightComputeShape Local;
        Local.PositionReach = {.05f, 2, .02f, .04f};
        Local.RadianceCategory = {5e-11f, 5e-11f, 5e-11f, 1};
        Local.PeakActive[1] = 1;
        Scene.Lights.push_back(Local);
    }
    return Scene;
}
DirectGpuMoments DirectGpuCentres(const std::vector<std::array<float, 4>>& Pixels, uint32_t Width)
{
    DirectGpuMoments S;
    for (uint32_t Y = 0; Y < 32; ++Y)
        for (uint32_t X = 0; X < 32; ++X)
            S.Accumulate(Pixels[size_t(3 * Y + 1) * Width + 3 * X + 1][0]);
    return S;
}
void ValidateDirectSpatialGpu(const char* Driver, const std::string& Shaders, bool Enforce)
{
    auto Scene = DirectGpuFixture(false);
    LightComputeSubmission Compute(Driver);
    std::printf("DIRECT preparing Vulkan pipelines on %s\n", Compute.Properties.deviceName);
    std::fflush(stdout);
    Compute.Prepare(Scene, Shaders, false, true);
    const auto Exact = Compute.Execute(Scene, 0, 16, 33, 64, 0);
    const auto Fresh = Compute.Execute(Scene, 2, 16, 71, 64, 0);
    const auto Spatial = Compute.Execute(Scene, 3, 16, 71, 64, 2, true);
    const double Expected = DirectGpuCentres(Exact, Scene.Width).Mean();
    const auto FreshMoments = DirectGpuCentres(Fresh, Scene.Width), SpatialMoments = DirectGpuCentres(Spatial, Scene.Width);
    Require(Expected > 0, "Sparse exact reference is dark");
    std::printf("DIRECT_SPARSE exact=%.12g fresh=%.12g se=%.12g spatial=%.12g se=%.12g ratio=%.9g n=%u\n",
                Expected, FreshMoments.Mean(), FreshMoments.StandardError(), SpatialMoments.Mean(),
                SpatialMoments.StandardError(), SpatialMoments.Mean() / Expected, SpatialMoments.Count);
    if (Enforce)
        Require(std::abs(SpatialMoments.Mean() - Expected) < 6 * SpatialMoments.StandardError() + .01 * Expected,
                "Spatial direct reservoir failed sparse-domain energy");
    std::vector<std::array<float, 4>> Temporal;
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
        Temporal = Compute.Execute(Scene, 3, 16, 500 + Frame, 64, 1, Frame == 0);
    const auto TemporalMoments = DirectGpuCentres(Temporal, Scene.Width);
    std::printf("DIRECT_TEMPORAL frames=8 exact=%.12g mean=%.12g se=%.12g ratio=%.9g std_ratio=%.9g\n",
                Expected, TemporalMoments.Mean(), TemporalMoments.StandardError(), TemporalMoments.Mean() / Expected,
                TemporalMoments.Deviation() / FreshMoments.Deviation());
    if (Enforce)
        Require(std::abs(TemporalMoments.Mean() - Expected) < 6 * TemporalMoments.StandardError() + .01 * Expected &&
                    TemporalMoments.Deviation() < FreshMoments.Deviation(), "Static temporal direct energy/variance failed");
    auto Shifted = Scene;
    for (uint32_t Y = 0; Y < Shifted.Height; ++Y)
        for (uint32_t X = 0; X < Shifted.Width; ++X)
        {
            auto& R = Shifted.Receivers[size_t(Y) * Shifted.Width + X];
            if (R.PositionHit[3] != 0) R.MotionPixel[0] = float(X < 93 ? X + 3 : X - 3);
        }
    const auto Reprojected = Compute.Execute(Shifted, 3, 16, 600, 64, 1);
    const auto ReprojectionCounts = Compute.ExtendedStatistics();
    const auto Cold = Compute.Execute(Shifted, 3, 16, 600, 64, 1, true);
    const auto ReprojectionMoments = DirectGpuCentres(Reprojected, Shifted.Width), ColdMoments = DirectGpuCentres(Cold, Shifted.Width);
    std::printf("DIRECT_REPROJECT accepted=%u domain_rejected=%u reuse_std=%.12g cold_std=%.12g\n",
                ReprojectionCounts[4], ReprojectionCounts[23], ReprojectionMoments.Deviation(), ColdMoments.Deviation());
    if (Enforce)
        Require(ReprojectionCounts[4] > 0 && ReprojectionCounts[23] == 0 &&
                    ReprojectionMoments.Deviation() < ColdMoments.Deviation() &&
                    std::abs(ReprojectionMoments.Mean() - Expected) < 6 * ReprojectionMoments.StandardError() + .01 * Expected,
                "Identical-domain pixel reprojection failed");
    auto Unequal = DirectGpuFixture(true);
    const auto UnequalExact = Compute.Execute(Unequal, 0, 16, 33, 64, 0);
    const auto UnequalSpatial = Compute.Execute(Unequal, 3, 16, 73, 64, 2, true);
    const auto* Cells = static_cast<const uint32_t*>(Compute.Buffers[5].Address);
    const auto Contains = [&](float X, float Y)
    {
        const uint32_t Cx = uint32_t((X + 16) / 32 * 8), Cy = uint32_t((Y + 16) / 32 * 8);
        const uint32_t First = (Cy * 8 + Cx) * 17;
        for (uint32_t I = 0; I < Cells[First]; ++I) if (Cells[First + 1 + I] == 1) return true;
        return false;
    };
    Require(Contains(.05f, 2) && !Contains(-.03f, 2), "Fixture does not cross clustered support boundary");
    const double UnequalExpected = DirectGpuCentres(UnequalExact, Unequal.Width).Mean();
    const auto UnequalMoments = DirectGpuCentres(UnequalSpatial, Unequal.Width);
    std::printf("DIRECT_UNEQUAL exact=%.12g spatial=%.12g se=%.12g ratio=%.9g n=%u\n",
                UnequalExpected, UnequalMoments.Mean(), UnequalMoments.StandardError(),
                UnequalMoments.Mean() / UnequalExpected, UnequalMoments.Count);
    if (Enforce)
        Require(std::abs(UnequalMoments.Mean() - UnequalExpected) < 6 * UnequalMoments.StandardError() + .02 * UnequalExpected,
                "Unequal support direct estimate failed");
    auto Previous = Unequal, Moving = Unequal;
    for (size_t I = 0; I < Previous.Receivers.size(); ++I)
    {
        auto& Before = Previous.Receivers[I]; auto& After = Moving.Receivers[I];
        if (Before.PositionHit[3] == 0) continue;
        Before.PositionHit[0] += .08f;
        Before.MotionPositionToken[0] = Before.PositionHit[0];
        After.MotionPositionToken[0] = Before.PositionHit[0];
    }
    for (uint32_t Frame = 0; Frame < 8; ++Frame)
        (void)Compute.Execute(Previous, 3, 16, 800 + Frame, 64, 1, Frame == 0);
    const auto Moved = Compute.Execute(Moving, 3, 16, 900, 64, 1);
    const auto MotionCounts = Compute.ExtendedStatistics();
    const auto MovingCold = Compute.Execute(Moving, 3, 16, 900, 64, 1, true);
    uint32_t Different = 0;
    for (size_t I = 0; I < Moved.size(); ++I) Different += Moved[I] != MovingCold[I];
    std::printf("DIRECT_MOVING exact=%.12g reproj=%.12g cold=%.12g differing=%u rejected=%u\n",
                UnequalExpected, DirectGpuCentres(Moved, Moving.Width).Mean(), DirectGpuCentres(MovingCold, Moving.Width).Mean(),
                Different, MotionCounts[23]);
    if (Enforce) Require(Different == 0 && MotionCounts[23] > 0, "Changed direct domain must match cold");
    for (uint32_t Variant = 0; Variant < 2; ++Variant)
    {
        for (uint32_t Frame = 0; Frame < 4; ++Frame)
            (void)Compute.Execute(Unequal, 3, 16, 1100 + 10 * Variant + Frame, 64, 1, Frame == 0);
        auto Tiny = Unequal;
        for (auto& R : Tiny.Receivers)
            if (R.PositionHit[3] != 0)
            {
                if (Variant == 0) R.PositionHit[0] += 5e-7f;
                else R.AlbedoIor[0] += 5e-5f;
            }
        const auto Reused = Compute.Execute(Tiny, 3, 16, 1200 + Variant, 64, 1);
        const auto Counts = Compute.ExtendedStatistics();
        const auto Restarted = Compute.Execute(Tiny, 3, 16, 1200 + Variant, 64, 1, true);
        uint32_t UnequalPixels = 0;
        for (size_t I = 0; I < Reused.size(); ++I) UnequalPixels += Reused[I] != Restarted[I];
        std::printf("DIRECT_SUBTOLERANCE kind=%s rejected=%u differing_from_cold=%u\n",
                    Variant == 0 ? "position_5e-7" : "albedo_5e-5", Counts[23], UnequalPixels);
        if (Enforce) Require(Counts[23] > 0 && UnequalPixels == 0, "Tiny changed domain must reject history");
    }
    std::printf("%s direct-light reservoir audit; moving-domain reuse and ReSTIR GI remain open\n", Enforce ? "PASS" : "OBSERVED");
}
} // namespace
