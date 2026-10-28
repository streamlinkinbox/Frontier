/*==========================================================================================================================================
                                                     SOLARCAPTUREGPU.H
==========================================================================================================================================*/
// 🧩 Produce a disclosed synthetic scene using actual Vulkan solar, visibility and primary readbacks.
#pragma once

namespace
{
LightComputeArchive SolarCaptureScene()
{
    LightComputeArchive Scene;
    Scene.Width = 640; Scene.Height = 360;
    Scene.Receivers.resize(size_t(Scene.Width) * Scene.Height);
    // 📝 Camera has a small upward pitch, looking along +Y.
    const float Cosine = std::cos(.06f), Sine = std::sin(.06f), Vertical = .23f;
    Scene.Camera[0] = {0, 0, 1.6f, .001f};
    Scene.Camera[1] = {0, Cosine, Sine, 200};
    Scene.Camera[2] = {1, 0, 0, Vertical * float(Scene.Width) / float(Scene.Height)};
    Scene.Camera[3] = {0, -Sine, Cosine, Vertical};
    for (uint32_t I = 0; I < 4; ++I) Scene.Camera[4 + I] = Scene.Camera[I];
    constexpr uint32_t Cube = 64;
    Scene.Camera[10] = {0, float(Cube), 0, 1};
    Scene.Camera[11] = {1, 0, 1, 0};
    // Authored procedural sky, not a calibrated atmospheric measurement.
    SpatialVector Sun{.12, 1, .18};
    const double Length = std::sqrt(Sun.X * Sun.X + Sun.Y * Sun.Y + Sun.Z * Sun.Z);
    Sun.X /= Length; Sun.Y /= Length; Sun.Z /= Length;
    for (uint32_t Face = 0; Face < 6; ++Face)
        for (uint32_t Y = 0; Y < Cube; ++Y)
            for (uint32_t X = 0; X < Cube; ++X)
            {
                const double U = 2 * (X + .5) / Cube - 1, V = 2 * (Y + .5) / Cube - 1;
                SpatialVector D = Face == 0 ? SpatialVector{1,U,V} : Face == 1 ? SpatialVector{-1,U,V} :
                                  Face == 2 ? SpatialVector{U,1,V} : Face == 3 ? SpatialVector{U,-1,V} :
                                  Face == 4 ? SpatialVector{U,V,1} : SpatialVector{U,V,-1};
                const double Size = std::sqrt(D.X * D.X + D.Y * D.Y + D.Z * D.Z);
                D.X /= Size; D.Y /= Size; D.Z /= Size;
                const double T = std::clamp((D.Z + .12) / .48, 0.0, 1.0);
                const double Glow = std::exp((D.X * Sun.X + D.Y * Sun.Y + D.Z * Sun.Z - 1) / .012);
                Scene.Angles.push_back(float(.27 * (1-T) + .025 * T + .10 * Glow));
                Scene.Angles.push_back(float(.105 * (1-T) + .055 * T + .035 * Glow));
                Scene.Angles.push_back(float(.045 * (1-T) + .12 * T + .009 * Glow));
            }
    // A deliberately simple, non-photographic mountain silhouette; vertices are real primary-ray triangles.
    constexpr double X[] = {-24,-16,-12,-8,-5,-2,1,4,7,10,14,19,24};
    constexpr double H[] = {1.3,1.2,2.1,.7,1.7,1.1,2.4,1.8,3.2,1.6,2.1,1.4,1.0};
    std::vector<std::array<SpatialVector,3>> Geometry;
    for (size_t I = 0; I + 1 < std::size(X); ++I)
    {
        const SpatialVector A{X[I],25,H[I]}, B{X[I+1],25,H[I+1]};
        const SpatialVector C{X[I+1],25,-20}, D{X[I],25,-20};
        Geometry.push_back({A,B,C}); Geometry.push_back({A,C,D});
    }
    TriangleVisibilitySpace Space;
    Space.Build(Geometry);
    Scene.Visibility = Space.PackFloat32();
    for (const auto& Corners : Scene.Visibility.Corners)
    {
        LightComputeFacet F;
        F.A = Corners.A; F.B = Corners.B; F.C = Corners.C;
        F.NormalA = F.NormalB = F.NormalC = {0,-1,0,0};
        F.AlbedoRoughness = {0,0,0,1};
        F.OpticalToken = {0,1.5f,0,1};
        F.Emissive = {.009f,.014f,.023f,0}; // Stylised fill for silhouette legibility.
        Scene.Surfaces.push_back(F);
    }
    return Scene;
}
void CaptureSolarOptics(const char* Driver, const std::string& Shaders, const std::string& OutputPath)
{
    auto Scene = SolarCaptureScene();
    LightComputeSubmission Compute(Driver);
    std::printf("CAPTURE preparing Vulkan on %s (%ux%u)\n", Compute.Properties.deviceName, Scene.Width, Scene.Height);
    std::fflush(stdout);
    Compute.Prepare(Scene, Shaders, true);
    SolarOpticsParameters P;
    P.SunDirection = {.12,1,.18};
    P.DiscIrradiance = {.60,.40,.25}; // Authored demonstration values, not lens calibration.
    P.Disc.EdgeBlurRad = .00055;
    P.Flare.ApertureBlades = 6;
    P.Flare.GhostStrength = .8; P.Flare.StarburstStrength = 1.2;
    P.Flare.AnamorphicStrength = 1.0; P.Flare.AnamorphicThicknessNdc = .008;
    P.Flare.StreakStrength = 1.8; P.Flare.StreakAngleRad = .35;
    P.Flare.GlowStrength = 1; P.Flare.HaloStrength = .8; P.Flare.VeilingStrength = .6;
    const auto Baseline = ExecuteSolar(Compute, Scene, nullptr, true);
    const auto Image = ExecuteSolar(Compute, Scene, &P, true);
    const auto Visibility = Compute.SolarVisibility();
    double Added = 0, Peak = 0;
    uint32_t Changed = 0;
    for (size_t I = 0; I < Image.size(); ++I)
    {
        for (uint32_t C = 0; C < 3; ++C)
        {
            Require(std::isfinite(Image[I][C]) && Image[I][C] >= Baseline[I][C], "Invalid solar capture readback");
            Added += Image[I][C] - Baseline[I][C];
            Peak = std::max(Peak, double(Image[I][C]));
        }
        Changed += Image[I] != Baseline[I];
    }
    Require(Visibility[0] > .99 && Changed > 1000 && Added > 0 && Peak > 100, "Solar capture is not visible");
    WriteImage(OutputPath, Image, Scene.Width, Scene.Height);
    std::printf("CAPTURE solar_visibility=%.9g changed_pixels=%u total_linear_added=%.9g peak_linear=%.9g\n",
                Visibility[0], Changed, Added, Peak);
    std::printf("PASS synthetic GPU-readback capture; display encoding exposure=0.45, gamma=2.2\n");
}
} // namespace
