/*==========================================================================================================================================
                                                     CACHEREFRESHVALIDATION.H
==========================================================================================================================================*/
// 🧩 Measure bounded payload refresh, moving receiver error, temporal correlation and actual traversal queries.
#pragma once

namespace
{
struct CacheErrorPairs
{
    double SumX = 0, SumY = 0, SquaredX = 0, SquaredY = 0, Product = 0, Count = 0;
    void Accumulate(double X, double Y)
    {
        SumX += X; SumY += Y; SquaredX += X * X; SquaredY += Y * Y; Product += X * Y; ++Count;
    }
    double Correlation() const
    {
        const double A = SquaredX - SumX * SumX / Count, B = SquaredY - SumY * SumY / Count;
        return A > 0 && B > 0 ? (Product - SumX * SumY / Count) / std::sqrt(A * B) : 0;
    }
};

void ValidateCacheRefresh(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, Samples = 1024, Methods = 6, Poses = 16;
    const std::array<const char*, Methods> Names{"ris8", "period1", "period4", "period8", "fresh6", "fresh8"};
    const std::array<float, 4> Blockers{0, .12f, .3f, .6f};
    const std::array<uint32_t, 4> PairArm{2, 3, 2, 3}, PairControl{5, 5, 4, 4};
    std::filesystem::create_directories(Directory);
    auto Scene = TriangleReconnectionScene(.7f);
    Scene.Width = 32; Scene.Height = 32 * Methods;
    Scene.Receivers.assign(Samples * Methods, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::printf("CACHE_REFRESH_DEVICE name=%s type=%u\n", Compute.Properties.deviceName, unsigned(Compute.Properties.deviceType));
    std::fflush(stdout);
    for (uint32_t Fixture = 0; Fixture < 4; ++Fixture)
    {
        const auto Geometry = TriangleReconnectionScene(Blockers[Fixture]);
        Scene.Surfaces = Geometry.Surfaces; Scene.Visibility = Geometry.Visibility;
        std::array<double, Poses> Positions{}, References{};
        for (uint32_t Pose = 0; Pose < Poses; ++Pose)
        {
            Positions[Pose] = float(.5 - .5 * std::cos(6.2831853071795864769 * Pose / Poses));
            const double Scale = Pose >= 9 && Pose < 13 ? .5 : 1;
            References[Pose] = TriangleReconnectionIntegral(Positions[Pose], Blockers[Fixture], 2048) * Scale;
            Require(std::abs(References[Pose] - TriangleReconnectionIntegral(Positions[Pose], Blockers[Fixture], 1024) * Scale) < 1e-7,
                    "Refresh reference quadrature has not converged");
            std::printf("CACHE_REFRESH_REFERENCE fixture=%u pose=%u x=%.9g integral=%.12g scale=%.1f\n",
                        Fixture, Pose, Positions[Pose], References[Pose], Scale);
        }
        std::array<std::array<DirectGpuMoments, Methods>, Poses> Means;
        std::array<DirectGpuMoments, Methods> Instant, Temporal, Flicker;
        std::array<DirectGpuMoments, 4> InstantDifference, TemporalDifference;
        std::array<CacheErrorPairs, Methods> Lag;
        std::array<uint64_t, Methods> Queries{}, SourceQueries{}, Reused{};
        double MaximumPair = 0;
        std::mt19937_64 Random(0x38d29a17ull + Fixture * 0x1000193ull);
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            std::vector<double> Previous(Samples * Methods), Trajectory(Samples * Methods, 0);
            std::array<double, Methods> Squared{}, TemporalSquared{}, DifferenceSquared{};
            for (uint32_t Pose = 0; Pose < Poses; ++Pose)
            {
                const uint32_t Seed = uint32_t(Random());
                for (uint32_t Pixel = 0; Pixel < Scene.Receivers.size(); ++Pixel)
                {
                    auto& R = Scene.Receivers[Pixel];
                    const uint32_t Method = Pixel / Samples;
                    R.PositionHit[0] = float(Positions[Pose]);
                    R.MotionPixel[2] = float(Method);
                    const uint32_t Period = Method == 2 ? 4 : Method == 3 ? 8 : 1;
                    R.MotionNormal[3] = Pose % Period == 0 ? 1.f : 0.f;
                    R.ResidualSpecular[0] = Pose >= 9 && Pose < 13 ? .5f : 1.f;
                    R.MotionPositionToken[3] = Pose < 9 ? 1.f : Pose < 13 ? 2.f : 3.f;
                }
                const auto Image = Compute.ExecuteDiffuse(Scene, 2, Seed, true, false, Pose == 0);
                if (Batch == 0)
                {
                    std::ofstream Capture(Directory + "/CacheRefresh" + std::to_string(Fixture) + "-" + std::to_string(Pose) + ".f32",
                                          std::ios::binary);
                    Capture.exceptions(std::ios::failbit | std::ios::badbit);
                    Capture.write(reinterpret_cast<const char*>(Image.data()), std::streamsize(Image.size() * 16));
                }
                std::array<double, Methods> Sum{};
                for (uint32_t Pixel = 0; Pixel < Image.size(); ++Pixel)
                {
                    const auto& P = Image[Pixel];
                    const uint32_t Method = Pixel / Samples;
                    Require(std::all_of(P.begin(), P.end(), [](float V) { return std::isfinite(V) && V >= 0; }),
                            "Nonfinite refresh readback");
                    const bool ExpectedReuse = (Method == 2 || Method == 3) && Pose != 9 && Pose != 13 &&
                        Pose % (Method == 2 ? 4 : 8) != 0;
                    Require(P[2] == float(ExpectedReuse), "Refresh cadence or light invalidation failed");
                    if (ExpectedReuse) Require(P[3] == 0 && P[1] <= 1, "Reused payload retraced its source");
                    if (Method == 1) MaximumPair = std::max(MaximumPair, std::abs(double(P[0]) - Image[Pixel - Samples][0]));
                    Sum[Method] += P[0];
                    const double Error = P[0] - References[Pose];
                    Trajectory[Pixel] += Error;
                    Squared[Method] += Error * Error;
                    if (Pose > 0)
                    {
                        const double Delta = Error - Previous[Pixel];
                        DifferenceSquared[Method] += Delta * Delta;
                        Lag[Method].Accumulate(Previous[Pixel], Error);
                    }
                    Previous[Pixel] = Error;
                    Queries[Method] += uint32_t(P[1]); SourceQueries[Method] += uint32_t(P[3]); Reused[Method] += uint32_t(P[2]);
                }
                for (uint32_t Method = 0; Method < Methods; ++Method) Means[Pose][Method].Accumulate(Sum[Method] / Samples);
            }
            for (uint32_t Pixel = 0; Pixel < Trajectory.size(); ++Pixel)
                TemporalSquared[Pixel / Samples] += Trajectory[Pixel] * Trajectory[Pixel] / (Poses * Poses);
            for (uint32_t Method = 0; Method < Methods; ++Method)
            {
                Instant[Method].Accumulate(Squared[Method] / (Poses * Samples));
                Temporal[Method].Accumulate(TemporalSquared[Method] / Samples);
                Flicker[Method].Accumulate(DifferenceSquared[Method] / ((Poses - 1) * Samples));
            }
            for (uint32_t Pair = 0; Pair < 4; ++Pair)
            {
                InstantDifference[Pair].Accumulate((Squared[PairArm[Pair]] - Squared[PairControl[Pair]]) / (Poses * Samples));
                TemporalDifference[Pair].Accumulate((TemporalSquared[PairArm[Pair]] - TemporalSquared[PairControl[Pair]]) / Samples);
            }
        }
        Require(MaximumPair < 2e-6, "Period-one cache disagrees with current inline RIS8");
        uint32_t PassedMeans = 0;
        for (uint32_t Pose = 0; Pose < Poses; ++Pose)
            for (uint32_t Method = 0; Method < Methods; ++Method)
            {
                const auto& M = Means[Pose][Method];
                const double Error = M.StandardError() * std::sqrt(double(Batches) / (Batches - 1));
                const bool Gate = std::abs(M.Mean() - References[Pose]) < 4 * Error + .0001;
                std::printf("CACHE_REFRESH_MEAN fixture=%u pose=%u method=%s mean=%.12g nominal_se=%.12g gate=%u\n",
                            Fixture, Pose, Names[Method], M.Mean(), Error, unsigned(Gate));
                Require(Gate, "Refresh mean disagrees with independent integral");
                PassedMeans += Gate;
            }
        for (uint32_t Method = 0; Method < Methods; ++Method)
            std::printf("CACHE_REFRESH_RESULT fixture=%u method=%s rmse=%.12g temporal_rmse=%.12g flicker_rmse=%.12g lag1=%.12g "
                        "queries=%llu source_queries=%llu reused=%llu\n", Fixture, Names[Method],
                        std::sqrt(Instant[Method].Mean()), std::sqrt(Temporal[Method].Mean()), std::sqrt(Flicker[Method].Mean()),
                        Lag[Method].Correlation(), static_cast<unsigned long long>(Queries[Method]),
                        static_cast<unsigned long long>(SourceQueries[Method]), static_cast<unsigned long long>(Reused[Method]));
        for (uint32_t Pair = 0; Pair < 4; ++Pair)
            std::printf("CACHE_REFRESH_PAIRED fixture=%u arm=%s control=%s instantaneous_mse=%.12g instantaneous_se=%.12g "
                        "temporal_mse=%.12g temporal_se=%.12g\n", Fixture, Names[PairArm[Pair]], Names[PairControl[Pair]],
                        InstantDifference[Pair].Mean(), InstantDifference[Pair].StandardError() * std::sqrt(double(Batches) / (Batches - 1)),
                        TemporalDifference[Pair].Mean(), TemporalDifference[Pair].StandardError() * std::sqrt(double(Batches) / (Batches - 1)));
        std::printf("CACHE_REFRESH_CHECK fixture=%u mean_gates=%u maximum_period1_difference=%.12g\n", Fixture, PassedMeans, MaximumPair);
        std::fflush(stdout);
    }
    std::printf("CACHE_REFRESH_DECISION correctness=1 promote=0 physical_gpu_timing=0\n");
}
} // namespace
