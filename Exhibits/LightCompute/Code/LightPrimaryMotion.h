/*==========================================================================================================================================
                                                        LIGHTPRIMARYMOTION.H
==========================================================================================================================================*/
// 🧩 Moving camera-ray surfaces with independent native hits and persistent Vulkan light reconstruction.

void ValidatePrimaryMotion(const char* Driver, const std::string& Shaders, const std::string& Directory,
                           const std::string& Output)
{
    auto First = LightComputeArchive::Load(Directory + "/VehicleMotion0.bin");
    LightComputeSubmission History(Driver);
    History.Prepare(First, Shaders);
    double FreshSum = 0, FilteredSum = 0;
    for (uint32_t Pose = 0; Pose < 6; ++Pose)
    {
        auto Scene = LightComputeArchive::Load(Directory + "/VehicleMotion" + std::to_string(Pose) + ".bin");
        // 📝 Keep oracle submissions separate: they must not consume the persistent history's light-revision reset.
        LightComputeSubmission Oracle(Driver);
        Oracle.Prepare(Scene, Shaders);
        ValidatePrimary(Oracle, Scene, Output, 64, false);
        auto Exact = Oracle.Execute(Scene, 0, 16, 1, 64, 0, true, true);
        auto Fresh = Oracle.Execute(Scene, 2, 16, 501 + Pose, 64, 0, true, true);
        std::vector<std::array<float, 4>> Filtered;
        for (uint32_t Step = 0; Step < (Pose == 0 ? 16u : 1u); ++Step)
            Filtered = History.Execute(Scene, 3, 16, Pose == 0 ? 101 + Step : 501 + Pose, 64, 7, false, true);
        auto Counts = History.HistoryStatistics();
        auto Receivers = History.PrimaryReceivers();
        auto NativeMotion = Scene;
        NativeMotion.Receivers = Receivers;
        CullCamera Previous;
        auto Vector = [](const auto& A) { return SpatialVector{A[0], A[1], A[2]}; };
        Previous.Position = Vector(Scene.Camera[4]);
        Previous.NearPlane = Scene.Camera[4][3];
        Previous.Forward = Vector(Scene.Camera[5]);
        Previous.FarPlane = Scene.Camera[5][3];
        Previous.Right = Vector(Scene.Camera[6]);
        Previous.Up = Vector(Scene.Camera[7]);
        Previous.FovYRadians = 2 * std::atan(Scene.Camera[7][3]);
        Previous.Aspect = Scene.Camera[6][3] / Scene.Camera[7][3];
        ProjectLightComputeMotion(NativeMotion, Previous, Scene.Camera[8][0], Scene.Camera[9][0],
                                  uint32_t(Scene.Camera[8][2]));
        double PixelError = 0, PositionError = 0, NormalError = 0;
        for (size_t I = 0; I < Receivers.size(); ++I)
            if (Receivers[I].PositionHit[3])
            {
                const auto& A = Receivers[I];
                const auto& B = NativeMotion.Receivers[I];
                Require(A.MotionPixel[3] == B.MotionPixel[3],
                        "GPU-primary projection validity differs from native");
                for (uint32_t C = 0; C < 3; ++C)
                {
                    PositionError = std::max(
                        PositionError, double(std::abs(A.MotionPositionToken[C] - B.MotionPositionToken[C])));
                    NormalError =
                        std::max(NormalError, double(std::abs(A.MotionNormal[C] - B.MotionNormal[C])));
                }
                if (A.MotionPixel[3])
                    for (uint32_t C = 0; C < 2; ++C)
                        PixelError =
                            std::max(PixelError, double(std::abs(A.MotionPixel[C] - B.MotionPixel[C])));
            }
        std::cout << "PRIMARY_REPROJECTION pose=" << Pose << " max_pixel_error=" << PixelError
                  << " max_world_error=" << PositionError << " max_normal_error=" << NormalError << "\n";
        Require(PixelError < .002 && PositionError < .001 && NormalError < .00001,
                "GPU-primary motion metadata differs from native rigid projection");
        double Before = ImageError(Fresh, Exact), After = ImageError(Filtered, Exact);
        if (Pose)
        {
            FreshSum += Before;
            FilteredSum += After;
        }
        std::cout << "PRIMARY_MOTION pose=" << Pose << " accepted=" << Counts[4] << " rejected=" << Counts[5]
                  << " visibility_rejected=" << Counts[7] << " colour_history=" << Counts[9]
                  << " fresh_rmse=" << Before << " filtered_rmse=" << After << "\n";
        if (Pose == 3)
            Require(Counts[4] == 0 && Counts[9] == 0, "GPU-primary light revision retained history");
        else
            Require(Counts[4] > 0 && Counts[8] > 0 && Counts[9] > 0,
                    "GPU-primary reuse or reconstruction not exercised");
        if (Pose && Pose != 3)
            Require(Counts[5] > 0, "GPU-primary disocclusion rejection not exercised");
        auto Links = History.HistoryLinks();
        std::vector<std::array<float, 4>> Mask(Links.size());
        for (size_t I = 0; I < Mask.size(); ++I)
            if (Receivers[I].PositionHit[3])
                Mask[I] = Links[I][2] > 0   ? std::array<float, 4>{0, 3, 0, 0}
                          : Links[I][1] > 0 ? std::array<float, 4>{3, 1, 0, 0}
                                            : std::array<float, 4>{3, 0, 0, 0};
        const auto Stem = Output + "/PrimaryMotion";
        WriteImage(Stem + std::to_string(Pose) + ".ppm", Filtered, Scene.Width, Scene.Height);
        WriteImage(Stem + "Exact" + std::to_string(Pose) + ".ppm", Exact, Scene.Width, Scene.Height);
        WriteImage(Stem + "Fresh" + std::to_string(Pose) + ".ppm", Fresh, Scene.Width, Scene.Height);
        WriteImage(Stem + "Reject" + std::to_string(Pose) + ".ppm", Mask, Scene.Width, Scene.Height);
    }
    auto Cut = LightComputeArchive::Load(Directory + "/VehicleMotion5.bin");
    Cut.Camera[4][0] += 100000;
    auto CutImage = History.Execute(Cut, 3, 16, 990, 64, 0, false, true);
    Require(History.HistoryStatistics()[4] == 0, "GPU-primary camera cut retained history");
    auto Cold = History.Execute(Cut, 3, 16, 990, 64, 0, true, true);
    Require(CutImage == Cold, "GPU-primary camera cut differs from cold restart");
    for (auto& L : Cut.Lights)
        L.PeakActive[1] = 0;
    auto ExactDark = History.Execute(Cut, 0, 16, 991, 64, 0, false, true);
    auto HistoryDark = History.Execute(Cut, 3, 16, 991, 64, 7, false, true);
    Require(ExactDark == HistoryDark && History.HistoryStatistics()[4] == 0,
            "GPU-primary light-off reset leaked old lighting");
    for (auto& F : Cut.Surfaces)
    {
        F.Emissive[0] += .3f;
        F.Emissive[1] += .1f;
    }
    auto ExactRefresh = History.Execute(Cut, 0, 16, 992, 64, 0, false, true);
    auto HistoryRefresh = History.Execute(Cut, 3, 16, 992, 64, 7, false, true);
    Require(ExactRefresh == HistoryRefresh, "GPU-primary emissive refresh retained prior emission");
    std::cout
        << "PRIMARY_RESET camera_cut_cold_equal=1 lights_off_exact_equal=1 current_emission_exact_equal=1\n";
    Require(FilteredSum < FreshSum, "Moving GPU-primary reconstruction failed to reduce aggregate error");
    std::cout << "PRIMARY_MOTION_SUM fresh_rmse_sum=" << FreshSum << " filtered_rmse_sum=" << FilteredSum
              << "\n";
}
