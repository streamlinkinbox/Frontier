/*==========================================================================================================================================
                                                     TRANSPORTRECONSTRUCTION.H
==========================================================================================================================================*/
// 🧩 Independent-seed indirect reference, reprojection rejection and current-emission preservation checks.

void ValidateTransportReconstruction(const char* Driver, const std::string& Shaders,
                                     const std::string& Directory, const std::string& Output,
                                     bool CaptureFull = true, bool CompactMotion = false)
{
    auto Load = [&](uint32_t Pose, bool Small)
    {
        auto Scene = LightComputeArchive::Load(Directory + "/VehicleMotion" + std::to_string(Pose) + ".bin");
        if (Small)
        {
            Scene.Width = 160;
            Scene.Height = 100;
            Scene.Receivers.resize(16000);
        }
        TransportReference::Prepare(Scene, 16, 48, true);
        EmissiveQuadrature::AppendDistribution(Scene);
        return Scene;
    };
    auto First = Load(0, true);
    // 📝 Isolate indirect emission from direct-light sampling noise in the reconstruction quality measurement.
    for (auto& L : First.Lights)
        L.PeakActive[1] = 0;
    First.Camera[10][3] = 0;
    LightComputeSubmission Reference(Driver);
    Reference.Prepare(First, Shaders);
    std::vector<std::array<float, 4>> Gold(First.Receivers.size());
    for (uint32_t I = 0; I < 16; ++I)
    {
        auto Image = Reference.Execute(First, 0, 16, 800 + I, 64, 0, true, true, 16, true, true);
        for (size_t P = 0; P < Gold.size(); ++P)
            for (uint32_t C = 0; C < 3; ++C)
                Gold[P][C] += Image[P][C] / 16;
    }
    LightComputeSubmission History(Driver);
    History.Prepare(First, Shaders);
    std::vector<std::array<float, 4>> Filtered, Raw;
    for (uint32_t I = 0; I < 8; ++I)
    {
        Raw = Reference.Execute(First, 0, 16, 101 + I, 64, 0, true, true, 16, true, true);
        Filtered = History.Execute(First, 3, 16, 101 + I, 64, 0, false, true, 16, true, true, true);
    }
    auto Diagnostic = History.TransportSlab(20);
    uint32_t Accepted = 0, Spatial = 0;
    for (auto D : Diagnostic)
    {
        Accepted += D[0] > 0;
        Spatial += D[1] > 1;
    }
    double Before = ImageError(Raw, Gold), After = ImageError(Filtered, Gold);
    std::cout << "TRANSPORT_RECONSTRUCTION width=160 height=100 reference_seeds=16 updates=8 accepted="
              << Accepted << " spatial=" << Spatial << " fresh_rmse=" << Before << " filtered_rmse=" << After
              << "\n";
    Require(Accepted > 0 && Spatial > 0 && After < Before,
            "Indirect reconstruction did not improve independent-reference error");
    WriteImage(Output + "/TransportProbeRaw.ppm", Raw, First.Width, First.Height);
    WriteImage(Output + "/TransportProbeFiltered.ppm", Filtered, First.Width, First.Height);
    auto Receivers = History.PrimaryReceivers();
    auto Temporal = History.TransportSlab(17), SpatialResult = History.TransportSlab(19);
    auto Compatible = [](const LightComputeReceiver& A, const LightComputeReceiver& B, double Radius)
    {
        using namespace TransportReference;
        if (!B.PositionHit[3] || A.MotionPositionToken[3] != B.MotionPositionToken[3] ||
            Dot(Unit(Vector(A.NormalRoughness)), Unit(Vector(B.NormalRoughness))) <= .95 ||
            std::sqrt(Dot(Sub(Vector(A.PositionHit), Vector(B.PositionHit)),
                          Sub(Vector(A.PositionHit), Vector(B.PositionHit)))) >= Radius)
            return false;
        for (uint32_t C = 0; C < 4; ++C)
            if (std::abs(A.AlbedoIor[C] - B.AlbedoIor[C]) >= 1e-4)
                return false;
        return std::abs(A.ViewMetallic[3] - B.ViewMetallic[3]) < 1e-4 &&
               std::abs(A.NormalRoughness[3] - B.NormalRoughness[3]) < 1e-4 &&
               std::abs(A.ResidualSpecular[3] - B.ResidualSpecular[3]) < 1e-4;
    };
    double SpatialMaximum = 0;
    uint32_t SpatialProbes = 0;
    for (uint32_t Pixel = 0; Pixel < Receivers.size(); Pixel += 67)
        if (Receivers[Pixel].PositionHit[3])
        {
            int X = int(Pixel % First.Width), Y = int(Pixel / First.Width);
            std::array<double, 3> Sum{};
            double Weight = 0;
            for (int Dy = -2; Dy <= 2; ++Dy)
                for (int Dx = -2; Dx <= 2; ++Dx)
                {
                    int Qx = X + Dx, Qy = Y + Dy;
                    if (Qx < 0 || Qy < 0 || Qx >= int(First.Width) || Qy >= int(First.Height))
                        continue;
                    uint32_t I = uint32_t(Qy) * First.Width + uint32_t(Qx);
                    if (!Compatible(Receivers[Pixel], Receivers[I],
                                    std::max(.03, double(Receivers[Pixel].MotionPixel[2] * 2))))
                        continue;
                    double W = (Dx == 0             ? 6
                                : std::abs(Dx) == 1 ? 4
                                                    : 1) *
                               (Dy == 0             ? 6
                                : std::abs(Dy) == 1 ? 4
                                                    : 1);
                    Weight += W;
                    for (uint32_t C = 0; C < 3; ++C)
                        Sum[C] += Temporal[I][C] * W;
                }
            for (uint32_t C = 0; C < 3; ++C)
                SpatialMaximum =
                    std::max(SpatialMaximum, std::abs(Sum[C] / Weight - SpatialResult[Pixel][C]));
            ++SpatialProbes;
        }
    Require(SpatialProbes > 0 && SpatialMaximum < .00001,
            "Spatial transport differs from independent native kernel");
    std::cout << "TRANSPORT_RECONSTRUCTION_SPATIAL native_probes=" << SpatialProbes
              << " max_rgb_error=" << SpatialMaximum << "\n";
    auto Changed = First;
    for (auto& F : Changed.Surfaces)
    {
        F.Emissive[0] *= .2f;
        F.Emissive[1] *= .3f;
        F.Emissive[2] *= .4f;
    }
    // Geometry weights remain usable; current radiance and revision detection must not retain the old emission.
    auto Revised = History.Execute(Changed, 3, 16, 901, 64, 0, false, true, 16, true, true, true);
    for (auto D : History.TransportSlab(20))
        Require(D[0] == 0, "Changed emissive source reused indirect history");
    auto Cold = History.Execute(Changed, 3, 16, 901, 64, 0, true, true, 16, true, true, true);
    Require(Revised == Cold, "Emission revision differs from cold reconstruction");
    auto Cut = Changed;
    Cut.Camera[4][0] += 100000;
    auto CutImage = History.Execute(Cut, 3, 16, 902, 64, 0, false, true, 16, true, true, true);
    for (auto D : History.TransportSlab(20))
        Require(D[0] == 0, "Camera cut reused indirect history");
    auto CutCold = History.Execute(Cut, 3, 16, 902, 64, 0, true, true, 16, true, true, true);
    Require(CutImage == CutCold, "Indirect camera cut differs from cold reconstruction");
    auto Off = Changed;
    for (auto& F : Off.Surfaces)
        F.Emissive = {};
    auto Black = History.Execute(Off, 3, 16, 903, 64, 0, false, true, 16, true, true, true);
    for (auto P : Black)
        Require(P[0] == 0 && P[1] == 0 && P[2] == 0, "Indirect history leaked after all sources disabled");
    std::cout << "TRANSPORT_RECONSTRUCTION_RESET emission_cold_equal=1 camera_cut_cold_equal=1 "
                 "sources_off_black=1\n";
    if (!CaptureFull && !CompactMotion)
        return;
    // 📝 Motion verification uses identical direct histories and seeds; the existing full-resolution capture mode remains available.
    auto Scene = Load(0, CompactMotion);
    LightComputeSubmission Full(Driver), Fresh(Driver);
    Full.Prepare(Scene, Shaders);
    Fresh.Prepare(Scene, Shaders);
    for (uint32_t Pose = 0; Pose < 6; ++Pose)
    {
        Scene = Load(Pose, CompactMotion);
        auto A = Fresh.Execute(Scene, 3, 16, 201 + Pose, 64, 7, false, true, 16, true, true, false);
        auto B = Full.Execute(Scene, 3, 16, 201 + Pose, 64, 7, false, true, 16, true, true, true);
        uint32_t Reused = 0;
        for (auto D : Full.TransportSlab(20))
            Reused += D[0] > 0;
        if (Pose == 0 || Pose == 3)
            Require(Reused == 0, "Indirect light revision failed reset");
        else
            Require(Reused > 0, "Moving indirect reprojection was not exercised");
        auto UnfilteredReceivers = Fresh.PrimaryReceivers();
        auto Receivers = Full.PrimaryReceivers();
        auto Strikes = Full.PrimaryStrikes();
        auto Indirect = Full.TransportSlab(19);
        for (size_t I = 0; I < Receivers.size(); ++I)
            if (Receivers[I].PositionHit[3])
            {
                const auto& F = Scene.Surfaces[uint32_t(Strikes[I][1])];
                for (uint32_t C = 0; C < 3; ++C)
                    Require(std::abs(Receivers[I].ResidualSpecular[C] - Indirect[I][C] - F.Emissive[C]) <
                                .00001,
                            "Visible emission was blurred with indirect lighting");
            }
        double DirectMaximum = 0;
        for (size_t I = 0; I < B.size(); ++I)
            for (uint32_t C = 0; C < 3; ++C)
                DirectMaximum = std::max(
                    DirectMaximum, std::abs(double(A[I][C] - UnfilteredReceivers[I].ResidualSpecular[C]) -
                                            double(B[I][C] - Receivers[I].ResidualSpecular[C])));
        Require(DirectMaximum < .00002, "Indirect reconstruction changed the direct-light histories");
        std::cout << "TRANSPORT_RECONSTRUCTION_MOTION pose=" << Pose << " accepted=" << Reused
                  << " current_emission_preserved=1 direct_max_delta=" << DirectMaximum << "\n";
        if (CompactMotion) continue;
        WriteImage(Output + "/TransportFiltered" + std::to_string(Pose) + ".ppm", B, Scene.Width,
                   Scene.Height);
        WriteImage(Output + "/TransportUnfiltered" + std::to_string(Pose) + ".ppm", A, Scene.Width,
                   Scene.Height);
    }
}
