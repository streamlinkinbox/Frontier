/*==========================================================================================================================================
                                                     LIGHTHISTORYVALIDATION.H
==========================================================================================================================================*/
// 🧩 Exact-reference checks for persistent Vulkan reservoirs, reprojection rejection and reconstructed lighting.

#pragma once

double ImageError(const std::vector<std::array<float, 4>>& A, const std::vector<std::array<float, 4>>& B)
{
    Require(A.size() == B.size() && !A.empty(), "Image comparison extent mismatch");
    double Sum = 0;
    for (size_t I = 0; I < A.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C)
        {
            double D = double(A[I][C]) - B[I][C];
            Sum += D * D;
        }
    return std::sqrt(Sum / (A.size() * 3));
}
void ValidateHistory(const char* Driver, const std::string& Shaders, const LightComputeArchive& Scene,
                     const std::string& Output, const std::string& MotionDirectory = "")
{
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders);
    auto Gold = Compute.Execute(Scene, 0), Fresh = Compute.Execute(Scene, 2, 16, 101);
    std::vector<std::array<float, 4>> Reconstructed;
    for (uint32_t I = 0; I < 16; ++I)
        Reconstructed = Compute.Execute(Scene, 3, 16, 101 + I);
    auto Counts = Compute.HistoryStatistics();
    double FreshError = ImageError(Fresh, Gold), HistoryError = ImageError(Reconstructed, Gold);
    std::cout << "HISTORY static_sequences=16 accepted=" << Counts[4] << " rejected=" << Counts[5]
              << " spatial=" << Counts[8] << " colour_history=" << Counts[9] << " fresh_rmse=" << FreshError
              << " reconstructed_rmse=" << HistoryError << "\n";
    Require(Counts[4] > 0 && Counts[8] > 0 && Counts[9] > 0,
            "History/spatial/filter computations not exercised");
    Require(HistoryError < FreshError, "Reconstruction did not improve static image error");
    WriteImage(Output + "/HistoryFresh.ppm", Fresh, Scene.Width, Scene.Height);
    WriteImage(Output + "/History.ppm", Reconstructed, Scene.Width, Scene.Height);
    if (!MotionDirectory.empty())
    {
        double MovingFresh = 0, MovingFiltered = 0;
        WriteImage(Output + "/Motion0.ppm", Reconstructed, Scene.Width, Scene.Height);
        WriteImage(Output + "/MotionExact0.ppm", Gold, Scene.Width, Scene.Height);
        for (uint32_t Index = 1; Index < 6; ++Index)
        {
            auto Moving = LightComputeArchive::Load(MotionDirectory + "/VehicleMotion" +
                                                    std::to_string(Index) + ".bin");
            auto Exact = Compute.Execute(Moving, 0), Noisy = Compute.Execute(Moving, 2, 16, 501 + Index);
            auto Filtered = Compute.Execute(Moving, 3, 16, 501 + Index);
            auto Statistics = Compute.HistoryStatistics();
            double NativeMaximum = 0, NativeTotal = 0;
            for (size_t I = 0; I < Exact.size(); ++I)
                for (uint32_t C = 0; C < 3; ++C)
                {
                    double Expected =
                        Moving.Receivers[I].ResidualSpecular[C] + Moving.Receivers[I].ExpectedDirect[C];
                    double Error = std::abs(double(Exact[I][C]) - Expected);
                    NativeMaximum = std::max(NativeMaximum, Error);
                    NativeTotal += Error;
                }
            Require(NativeTotal / (Exact.size() * 3) < .001,
                    "Moving exact GPU image differs from native reference");
            double Before = ImageError(Noisy, Exact), After = ImageError(Filtered, Exact);
            MovingFresh += Before;
            MovingFiltered += After;
            std::cout << "MOTION index=" << Index << " accepted=" << Statistics[4]
                      << " rejected=" << Statistics[5] << " visibility_rejected=" << Statistics[7]
                      << " colour_history=" << Statistics[9] << " fresh_rmse=" << Before
                      << " filtered_rmse=" << After << " native_max_abs=" << NativeMaximum << "\n";
            if (Index == 3)
                Require(Statistics[4] == 0, "Changed vehicle light table retained history");
            else
                Require(Statistics[4] > 0 && Statistics[5] > 0,
                        "Moving reprojection/disocclusion not exercised");
            WriteImage(Output + "/Motion" + std::to_string(Index) + ".ppm", Filtered, Scene.Width,
                       Scene.Height);
            WriteImage(Output + "/MotionExact" + std::to_string(Index) + ".ppm", Exact, Scene.Width,
                       Scene.Height);
            WriteImage(Output + "/MotionFresh" + std::to_string(Index) + ".ppm", Noisy, Scene.Width,
                       Scene.Height);
            auto Links = Compute.HistoryLinks();
            std::vector<std::array<float, 4>> Mask(Links.size());
            for (size_t I = 0; I < Mask.size(); ++I)
                if (Moving.Receivers[I].PositionHit[3] != 0)
                    Mask[I] = Links[I][2] > 0   ? std::array<float, 4>{0, 3, 0, 0}
                              : Links[I][1] > 0 ? std::array<float, 4>{3, 1, 0, 0}
                                                : std::array<float, 4>{3, 0, 0, 0};
            WriteImage(Output + "/MotionReject" + std::to_string(Index) + ".ppm", Mask, Scene.Width,
                       Scene.Height);
        }
        Require(MovingFiltered < MovingFresh,
                "Moving reconstruction did not reduce aggregate reference error");
        std::cout << "MOTION_SUM fresh_rmse_sum=" << MovingFresh << " filtered_rmse_sum=" << MovingFiltered
                  << "\n";
    }
    auto Dark = Scene;
    for (auto& L : Dark.Lights)
        L.PeakActive[1] = 0;
    auto ExactDark = Compute.Execute(Dark, 0), HistoryDark = Compute.Execute(Dark, 3, 16, 201);
    Require(ExactDark == HistoryDark, "Old lighting leaked after light-table invalidation");
    Require(Compute.HistoryStatistics()[4] == 0, "Changed lights reused old histories");
    std::cout << "RESET lights_off_exact_match=1 accepted=" << Compute.HistoryStatistics()[4] << "\n";
    auto Refreshed = Dark;
    for (auto& R : Refreshed.Receivers)
    {
        R.ResidualSpecular[0] += .3f;
        R.ResidualSpecular[1] += .1f;
    }
    auto ExactRefresh = Compute.Execute(Refreshed, 0),
         HistoryRefresh = Compute.Execute(Refreshed, 3, 16, 202);
    Require(ExactRefresh == HistoryRefresh, "Prior residual radiance survived refresh");
    std::cout << "REFRESH current_residual_exact_match=1\n";
}
