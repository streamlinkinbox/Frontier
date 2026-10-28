/*==========================================================================================================================================
                                                   SPECULARHISTORYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Measure authored rough-reflection reconstruction against independent seeds and reject stale moving-geometry radiance.

//------------------------------------------------------------------------------------------------------------------------
//                                                   REFLECTION HISTORY ACCEPTANCE
//------------------------------------------------------------------------------------------------------------------------

void ValidateSpecularHistory(
    const char*        Driver,
    const std::string& Shaders,
    const std::string& Output,
    bool               Original)
{
    using namespace TransportReference;
    std::filesystem::create_directories(Output);
    LightComputeArchive Scene;
    Prepare(Scene, 32, 4, false);
    auto Materials = AppendSceneMaterials(Scene, Shaders);
    for (uint32_t I = 0; I < 4; ++I)
    {
        constexpr float Roughness[]{.045f, .3f, .55f, .8f};
        uint32_t Address = DuplicateSceneMaterial(Scene, Materials[3], 3, {.8f, .8f, .8f, Roughness[I]}, 1);
        MaterialRectangle(Scene, {-1.5 + I, 0, 0}, {.48, 0, 0}, {0, 2, 0}, Address, 800 + I);
    }
    for (uint32_t Y = 0; Y < 4; ++Y)
        for (uint32_t X = 0; X < 8; ++X)
        {
            MaterialRectangle(Scene, {-1.75 + X * .5, 2.4, .3 + Y * .5}, {.25, 0, 0}, {0, 0, .25}, 0, 900);
            for (size_t I = Scene.Surfaces.size() - 2; I < Scene.Surfaces.size(); ++I)
                Scene.Surfaces[I].Emissive = (X + Y) % 2 ? std::array<float, 4>{8, 6, 3, 0} : std::array<float, 4>{.025f, .035f, .06f, 0};
        }
    // 📝 A foreground plank creates genuinely newly visible floor pixels during lateral camera motion.
    MaterialRectangle(Scene, {.4, -.7, .5}, {.3, 0, 0}, {0, 0, .5}, 0, 950);
    RebuildMaterialVisibility(Scene);
    MaterialCamera(Scene, {0, -4, 3.2}, {0, .4, .35}, 96, 64);
    Scene.Camera[8][2] = 1e9f; // Static geometry uses identity motion, not the vehicle's rigid-token range.
    Scene.Camera[11] = {1, 0, 1, 0};
    LightComputeSubmission RawCompute(Driver), History(Driver);
    RawCompute.Prepare(Scene, Shaders); History.Prepare(Scene, Shaders);
    History.TransportGeometryRevision = !Original;
    std::cout << "SPECULAR_DEVICE name=" << History.Properties.deviceName << " type=" << History.Properties.deviceType << std::endl;
    std::vector<std::array<float, 4>> Gold(Scene.Receivers.size()), Raw, Filtered;
    for (uint32_t I = 0; I < 8; ++I)
    {
        auto P = RawCompute.Execute(Scene, 0, 16, 800 + I, 64, 0, true, true, 16, false);
        for (size_t J = 0; J < P.size(); ++J)
            for (uint32_t C = 0; C < 3; ++C) Gold[J][C] += P[J][C] / 8;
    }
    for (uint32_t I = 0; I < 8; ++I)
    {
        Raw = RawCompute.Execute(Scene, 0, 16, 101 + I, 64, 0, true, true, 4, false);
        Filtered = History.Execute(Scene, 3, 16, 101 + I, 64, 0, false, true, 4, false, false, true);
    }
    double Before = ImageError(Raw, Gold), After = ImageError(Filtered, Gold);
    auto Receivers = History.PrimaryReceivers();
    auto Diagnostics = History.TransportSlab(20);
    uint32_t Accepted = 0, Spatial = 0, Sharp = 0;
    for (size_t I = 0; I < Receivers.size(); ++I)
    {
        if (!Receivers[I].PositionHit[3]) continue;
        if (Receivers[I].MotionPositionToken[3] == 800)
        {
            for (uint32_t C = 0; C < 3; ++C) Require(Raw[I][C] == Filtered[I][C], "Smooth reflection was blurred by diffuse reconstruction");
            ++Sharp;
        }
        Accepted += Diagnostics[I][0] > 0; Spatial += Diagnostics[I][1] > 1;
    }
    Require(Accepted > 0 && Spatial > 0 && Sharp > 0 && After < Before, "Rough-reflection reconstruction did not improve independent-reference error");
    auto Temporal = History.TransportSlab(17), SpatialResult = History.TransportSlab(19);
    double FilterError = 0;
    uint32_t FilterProbes = 0;
    for (size_t I = 0; I < Receivers.size(); I += 17)
    {
        const auto& A = Receivers[I];
        if (!A.PositionHit[3] || A.MotionPositionToken[3] < 801 || A.MotionPositionToken[3] > 803) continue;
        std::array<double, 3> Sum{};
        double Weight = 0, Roughness = A.NormalRoughness[3];
        auto Reflection = [](const LightComputeReceiver& R) { auto N = Vector(R.NormalRoughness), V = Vector(R.ViewMetallic); return Sub(Mul(N, 2 * Dot(N, V)), V); };
        auto Direction = Reflection(A);
        int X = int(I % Scene.Width), Y = int(I / Scene.Width);
        for (int Dy = -2; Dy <= 2; ++Dy)
            for (int Dx = -2; Dx <= 2; ++Dx)
            {
                int Qx = X + Dx, Qy = Y + Dy;
                if (Qx < 0 || Qy < 0 || Qx >= int(Scene.Width) || Qy >= int(Scene.Height)) continue;
                size_t J = size_t(Qy) * Scene.Width + size_t(Qx);
                const auto& B = Receivers[J];
                auto Delta = Sub(Vector(A.PositionHit), Vector(B.PositionHit));
                if (!B.PositionHit[3] || A.MotionPositionToken[3] != B.MotionPositionToken[3] ||
                    Dot(Unit(Vector(A.NormalRoughness)), Unit(Vector(B.NormalRoughness))) <= .95 ||
                    std::sqrt(Dot(Delta, Delta)) >= std::max(.03, double(A.MotionPixel[2] * 2))) continue;
                bool Match = true;
                for (uint32_t C = 0; C < 4; ++C) Match &= std::abs(A.AlbedoIor[C] - B.AlbedoIor[C]) < 1e-4;
                if (!Match || std::abs(A.ViewMetallic[3] - B.ViewMetallic[3]) >= 1e-4 ||
                    std::abs(A.NormalRoughness[3] - B.NormalRoughness[3]) >= 1e-4 ||
                    std::abs(A.ResidualSpecular[3] - B.ResidualSpecular[3]) >= 1e-4) continue;
                double W = (Dx == 0 ? 6 : std::abs(Dx) == 1 ? 4 : 1) * (Dy == 0 ? 6 : std::abs(Dy) == 1 ? 4 : 1);
                W *= std::exp(-std::max(0., 1 - Dot(Direction, Reflection(B))) / std::max(1e-6, 2 * std::pow(Roughness, 4)));
                Weight += W;
                for (uint32_t C = 0; C < 3; ++C) Sum[C] += Temporal[J][C] * W;
            }
        for (uint32_t C = 0; C < 3; ++C) FilterError = std::max(FilterError, std::abs(Sum[C] / Weight - SpatialResult[I][C]));
        ++FilterProbes;
    }
    Require(FilterProbes > 20 && FilterError < .0005, "Native angular-filter reconstruction differs from Vulkan");
    std::cout << "SPECULAR_FILTER native_probes=" << FilterProbes << " max_rgb_error=" << FilterError << std::endl;

    if (!Original)
    {
        WriteImage(Output + "/SpecularHistoryRaw.ppm", Raw, Scene.Width, Scene.Height);
        WriteImage(Output + "/SpecularHistoryFiltered.ppm", Filtered, Scene.Width, Scene.Height);
        WriteImage(Output + "/SpecularHistoryReference.ppm", Gold, Scene.Width, Scene.Height);
    }
    auto Moved = Scene;
    for (auto& F : Moved.Surfaces)
        if (F.OpticalToken[3] == 900)
            for (auto* P : {&F.A, &F.B, &F.C}) (*P)[1] -= .8f;
    RebuildMaterialVisibility(Moved);
    auto Motion = History.Execute(Moved, 3, 16, 117, 64, 0, false, true, 4, false, false, true);
    LightComputeSubmission Fresh(Driver);
    Fresh.Prepare(Moved, Shaders);
    auto FreshMotion = Fresh.Execute(Moved, 3, 16, 117, 64, 0, false, true, 4, false, false, true);
    double MotionError = ImageError(Motion, FreshMotion);
    uint32_t Stale = 0;
    for (auto D : History.TransportSlab(20)) Stale += D[0] > 0;
    std::cout << "SPECULAR_HISTORY original_geometry_revision=" << Original << " reference_seeds=8 reference_samples=16 frames=8 samples=4"
              << " temporal_pixels=" << Accepted << " spatial_pixels=" << Spatial << " preserved_smooth_pixels=" << Sharp
              << " raw_rmse=" << Before << " filtered_rmse=" << After << " moved_fresh_rmse=" << MotionError
              << " moved_history_pixels=" << Stale << std::endl;
    if (Original)
    {
        Require(Stale > 0 && MotionError > .0001, "Disabled geometry revision did not reproduce stale reflected lighting");
        return;
    }
    Require(Stale == 0 && MotionError == 0, "Moved geometry retained reflected lighting history");
    WriteImage(Output + "/SpecularHistoryMoved.ppm", Motion, Moved.Width, Moved.Height);
    // Keep the immediately preceding camera for real reprojection, not an identity camera reset.
    (void)History.Execute(Moved, 3, 16, 118, 64, 0, false, true, 4, false, false, true);
    auto CameraMoved = Moved;
    MaterialCamera(CameraMoved, {.7, -4, 3.2}, {0, .4, .35}, Scene.Width, Scene.Height);
    for (uint32_t I = 0; I < 4; ++I) CameraMoved.Camera[4 + I] = Moved.Camera[I];
    auto CameraImage = History.Execute(CameraMoved, 3, 16, 119, 64, 0, false, true, 4, false, false, true);
    auto CameraRaw = RawCompute.Execute(CameraMoved, 0, 16, 119, 64, 0, true, true, 4, false);
    uint32_t CameraAccepted = 0, CameraRejected = 0, CameraSharp = 0;
    auto CameraReceivers = History.PrimaryReceivers();
    auto CameraDiagnostic = History.TransportSlab(20);
    for (size_t I = 0; I < CameraImage.size(); ++I)
    {
        for (uint32_t C = 0; C < 3; ++C) Require(std::isfinite(CameraImage[I][C]) && CameraImage[I][C] >= 0, "Invalid camera-motion reflection");
        auto Token = CameraReceivers[I].MotionPositionToken[3];
        if (CameraReceivers[I].PositionHit[3] && Token >= 801 && Token <= 803)
        {
            CameraAccepted += CameraDiagnostic[I][0] > 0;
            CameraRejected += CameraDiagnostic[I][0] == 0;
        }
        if (Token == 800)
        {
            for (uint32_t C = 0; C < 3; ++C) Require(CameraImage[I][C] == CameraRaw[I][C], "Camera motion blurred a preserved smooth reflection");
            ++CameraSharp;
        }
    }
    std::cout << "SPECULAR_CAMERA accepted=" << CameraAccepted << " rejected=" << CameraRejected << " smooth_exact_pixels=" << CameraSharp << " finite_nonnegative=1" << std::endl;
    Require(CameraAccepted > 0 && CameraRejected > 0 && CameraSharp > 0, "Camera reprojection acceptance/rejection not exercised");
    WriteImage(Output + "/SpecularHistoryCamera.ppm", CameraImage, Scene.Width, Scene.Height);
    auto Dark = CameraMoved;
    for (auto& F : Dark.Surfaces) F.Emissive[0] = F.Emissive[1] = F.Emissive[2] = 0;
    auto Black = History.Execute(Dark, 3, 16, 118, 64, 0, false, true, 4, false, false, true);
    for (const auto& P : Black)
        Require(P[0] == 0 && P[1] == 0 && P[2] == 0, "Temporal reflection retained switched-off source radiance");
    auto Legacy = Scene;
    for (auto& F : Legacy.Surfaces) F.Emissive[3] = 0;
    LightComputeSubmission LegacyGuard(Driver);
    LegacyGuard.Prepare(Legacy, Shaders);
    LegacyGuard.Upload(Legacy);
    LegacyGuard.TransportDirty = false;
    Legacy.Surfaces[0].A[0] += .01f;
    LegacyGuard.Upload(Legacy);
    Require(!LegacyGuard.TransportDirty, "Authored geometry revision changed the retained compact diffuse policy");
    std::cout << "SPECULAR_HISTORY_RESET geometry_fresh_equal=1 source_off_black=1 smooth_preserved=1 compact_policy_retained=1" << std::endl;
}
