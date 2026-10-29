/*==========================================================================================================================================
                                                     RAYBUDGETVALIDATION.H
==========================================================================================================================================*/
// 🧩 Measure bounded payload refresh, moving receiver error, temporal correlation and actual traversal queries.
#pragma once

namespace
{
void ValidateRayBudget(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, Samples = 1024, Methods = 12, Poses = 16;
    const std::array<const char*, Methods> Names{"area8", "bank8", "rotate4", "rotate2", "fresh6", "fresh8", "fastarea8", "fastbank8", "fastrotate4", "fastrotate2", "fastfresh6", "fastfresh8"};
    const std::array<float, 4> Blockers{0, .12f, .3f, .6f};
    const std::array<uint32_t, 4> PairArm{8, 9, 8, 9}, PairControl{11, 11, 10, 10};
    std::filesystem::create_directories(Directory);
    auto Scene = TriangleReconnectionScene(.7f);
    Scene.Width = 32; Scene.Height = 32 * Methods;
    Scene.Receivers.assign(Samples * Methods, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::printf("RAY_BUDGET_DEVICE name=%s type=%u\n", Compute.Properties.deviceName, unsigned(Compute.Properties.deviceType));
    std::fflush(stdout);
    uint32_t EdgeFailures = 0;
    const auto ReceiverTemplate = Scene.Receivers.front();
    for (uint32_t Fixture = 0; Fixture < 4; ++Fixture)
    {
        const auto Geometry = TriangleReconnectionScene(Blockers[Fixture]);
        Scene.Surfaces = Geometry.Surfaces; Scene.Visibility = Geometry.Visibility;
        constexpr std::array<float, 5> Offsets{-1e-5f, -1e-7f, 0.f, 1e-7f, 1e-5f};
        for (uint32_t Pixel = 0; Pixel < Scene.Receivers.size(); ++Pixel)
        {
            auto& R = Scene.Receivers[Pixel];
            const uint32_t ProbeIndex = Pixel % Samples;
            const float X = float((ProbeIndex / 60) % 16) / 15;
            const std::array<float, 4> Cuts{-.5f, .5f, -2 * Blockers[Fixture] - X, 2 * Blockers[Fixture] - X};
            R.PositionHit[0] = X;
            R.MotionPixel[0] = Cuts[(ProbeIndex / 5) % 4] + Offsets[ProbeIndex % 5];
            R.MotionPixel[1] = float((ProbeIndex / 20) % 3) * .5f - .5f;
            R.MotionPixel[2] = 0;
            R.ResidualSpecular[1] = 1;
        }
        auto Image = Compute.ExecuteDiffuse(Scene, 2, 17, true, false, true);
        Image.resize(Samples);
        uint32_t Hits = 0, Disagreements = 0;
        for (const auto& P : Image)
        {
            Require(P[0] == 0 || P[0] == 1, "Invalid first-hit edge result");
            Hits += uint32_t(P[0]); Disagreements += P[0] != P[1];
        }
        std::ofstream Capture(Directory + "/RayBudgetEdge" + std::to_string(Fixture) + ".f32", std::ios::binary);
        Capture.exceptions(std::ios::failbit | std::ios::badbit);
        Capture.write(reinterpret_cast<const char*>(Image.data()), std::streamsize(Image.size() * 16));
        std::printf("RAY_BUDGET_EDGE fixture=%u probes=%u hits=%u disagreements=%u\n", Fixture, Samples, Hits, Disagreements);
        Require(Hits > 0, "Edge probes did not exercise any valid first hits");
        EdgeFailures += Disagreements;
        for (uint32_t Pixel = 0; Pixel < Samples; ++Pixel)
            if (Image[Pixel][0] != Image[Pixel][1])
                std::printf("RAY_BUDGET_EDGE_DETAIL fixture=%u pixel=%u receiver_x=%.9g target_x=%.9g target_y=%.9g first=%.0f recheck=%.0f\n",
                            Fixture, Pixel, Scene.Receivers[Pixel].PositionHit[0], Scene.Receivers[Pixel].MotionPixel[0],
                            Scene.Receivers[Pixel].MotionPixel[1], Image[Pixel][0], Image[Pixel][1]);
    }
    std::fflush(stdout);
    Require(EdgeFailures == 0, "First-hit/recheck numerical edge disagreement blocks query elision");
    Scene.Receivers.assign(Samples * Methods, ReceiverTemplate);
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
            std::printf("RAY_BUDGET_REFERENCE fixture=%u pose=%u x=%.9g integral=%.12g scale=%.1f\n",
                        Fixture, Pose, Positions[Pose], References[Pose], Scale);
        }
        std::array<std::array<DirectGpuMoments, Methods>, Poses> Means;
        std::array<DirectGpuMoments, Methods> Instant, Temporal, Flicker;
        std::array<DirectGpuMoments, 4> InstantDifference, TemporalDifference;
        std::array<CacheErrorPairs, Methods> Lag;
        std::array<uint64_t, Methods> Queries{}, SourceQueries{}, Reused{};
        double MaximumPair = 0, MaximumElision = 0;
        uint64_t MatchedSamples = 0;
        uint64_t RetainedSlotChecks = 0;
        std::mt19937_64 Random(0x38d29a17ull + Fixture * 0x1000193ull);
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            std::vector<std::array<float, 4>> PreviousCandidates(6 * Samples * 8), Candidates(6 * Samples * 8);
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
                    R.MotionNormal[3] = float(Pose);
                    R.ResidualSpecular[1] = 0;
                    R.ResidualSpecular[0] = Pose >= 9 && Pose < 13 ? .5f : 1.f;
                    R.MotionPositionToken[3] = Pose < 9 ? 1.f : Pose < 13 ? 2.f : 3.f;
                }
                const auto Image = Compute.ExecuteDiffuse(Scene, 2, Seed, true, false, Pose == 0);
                const auto* Storage = static_cast<const std::array<float, 4>*>(Compute.Buffers[6].Address);
                for (uint32_t Bank = 0; Bank < 6; ++Bank)
                    for (uint32_t Sample = 0; Sample < Samples; ++Sample)
                        for (uint32_t Slot = 0; Slot < 8; ++Slot)
                        {
                            const uint32_t Method = 1 + Bank % 3 + 6 * (Bank / 3), Local = Method % 6;
                            const uint32_t Offset = (Bank * Samples + Sample) * 8 + Slot;
                            Candidates[Offset] = Storage[(Slot + 1) * Samples * Methods + Method * Samples + Sample];
                            const auto& C = Candidates[Offset];
                            Require(std::all_of(C.begin(), C.end(), [](float X) { return std::isfinite(X); }),
                                    "Nonfinite candidate payload");
                            Require(C[3] == 0 || (C[3] >= .25f && C[3] <= .49f && std::abs(C[0]) <= .5f &&
                                        std::abs(C[1]) <= .5f && C[2] >= 0), "Invalid candidate density or geometry");
                            if (C[3] == 0) Require(C == std::array<float, 4>{}, "Null candidate was not canonical zero");
                            const uint32_t Replace = Local == 2 ? 4 : 2, Start = (Pose * Replace) % 8;
                            if (Local >= 2 && Pose != 0 && Pose != 9 && Pose != 13 && (Slot + 8 - Start) % 8 >= Replace)
                            {
                                Require(C == PreviousCandidates[Offset], "Retained candidate payload was modified");
                                ++RetainedSlotChecks;
                            }
                        }
                for (uint32_t Offset = 0; Offset < 3 * Samples * 8; ++Offset)
                    Require(Candidates[Offset] == Candidates[Offset + 3 * Samples * 8], "Query elision changed a stored candidate");
                PreviousCandidates = Candidates;
                if (Batch == 0)
                {
                    std::ofstream Capture(Directory + "/RayBudget" + std::to_string(Fixture) + "-" + std::to_string(Pose) + ".f32",
                                          std::ios::binary);
                    Capture.exceptions(std::ios::failbit | std::ios::badbit);
                    Capture.write(reinterpret_cast<const char*>(Image.data()), std::streamsize(Image.size() * 16));
                    std::ofstream Payload(Directory + "/RayBudgetPayload" + std::to_string(Fixture) + "-" +
                                          std::to_string(Pose) + ".f32", std::ios::binary);
                    Payload.exceptions(std::ios::failbit | std::ios::badbit);
                    Payload.write(reinterpret_cast<const char*>(Candidates.data()), std::streamsize(Candidates.size() * 16));
                }
                std::array<double, Methods> Sum{};
                for (uint32_t Pixel = 0; Pixel < Image.size(); ++Pixel)
                {
                    const auto& P = Image[Pixel];
                    const uint32_t Method = Pixel / Samples, Local = Method % 6;
                    Require(std::all_of(P.begin(), P.end(), [](float V) { return std::isfinite(V) && V >= 0; }),
                            "Nonfinite refresh readback");
                    const uint32_t Retained = (Local == 2 || Local == 3) && Pose != 0 && Pose != 9 && Pose != 13
                        ? (Local == 2 ? 4u : 6u) : 0u;
                    Require(P[2] == float(Retained), "Candidate rotation or light invalidation failed");
                    if (Local >= 1 && Local <= 3)
                        Require(P[1] >= P[3] && P[1] - P[3] <= 8, "Bank receiver query budget exceeded");
                    if (Local == 1) Require(P[1] == Image[Pixel - Samples][1], "Bank full-refresh query identity failed");
                    if (Local == 1) MaximumPair = std::max(MaximumPair, std::abs(double(P[0]) - Image[Pixel - Samples][0]));
                    if (Method >= 6)
                    {
                        const auto& Legacy = Image[Pixel - 6 * Samples];
                        MaximumElision = std::max(MaximumElision, std::abs(double(P[0]) - Legacy[0]));
                        Require(P[1] <= Legacy[1], "Query elision increased traversal calls");
                        if (Local >= 4) Require(Legacy[1] - P[1] == (Local == 4 ? 6.f : 8.f), "Fresh query savings are not exactly the candidate budget");
                        ++MatchedSamples;
                    }
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
        Require(MaximumPair < 2e-6, "Full-refresh bank disagrees with inline eight-draw area IS");
        uint32_t PassedMeans = 0;
        for (uint32_t Pose = 0; Pose < Poses; ++Pose)
            for (uint32_t Method = 0; Method < Methods; ++Method)
            {
                const auto& M = Means[Pose][Method];
                const double Error = M.StandardError() * std::sqrt(double(Batches) / (Batches - 1));
                const bool Gate = std::abs(M.Mean() - References[Pose]) < 4 * Error + .0001;
                std::printf("RAY_BUDGET_MEAN fixture=%u pose=%u method=%s mean=%.12g nominal_se=%.12g gate=%u\n",
                            Fixture, Pose, Names[Method], M.Mean(), Error, unsigned(Gate));
                Require(Gate, "Refresh mean disagrees with independent integral");
                PassedMeans += Gate;
            }
        for (uint32_t Method = 0; Method < Methods; ++Method)
            std::printf("RAY_BUDGET_RESULT fixture=%u method=%s rmse=%.12g temporal_rmse=%.12g flicker_rmse=%.12g lag1=%.12g "
                        "queries=%llu source_queries=%llu reused=%llu\n", Fixture, Names[Method],
                        std::sqrt(Instant[Method].Mean()), std::sqrt(Temporal[Method].Mean()), std::sqrt(Flicker[Method].Mean()),
                        Lag[Method].Correlation(), static_cast<unsigned long long>(Queries[Method]),
                        static_cast<unsigned long long>(SourceQueries[Method]), static_cast<unsigned long long>(Reused[Method]));
        for (uint32_t Pair = 0; Pair < 4; ++Pair)
            std::printf("RAY_BUDGET_PAIRED fixture=%u arm=%s control=%s instantaneous_mse=%.12g instantaneous_se=%.12g "
                        "temporal_mse=%.12g temporal_se=%.12g\n", Fixture, Names[PairArm[Pair]], Names[PairControl[Pair]],
                        InstantDifference[Pair].Mean(), InstantDifference[Pair].StandardError() * std::sqrt(double(Batches) / (Batches - 1)),
                        TemporalDifference[Pair].Mean(), TemporalDifference[Pair].StandardError() * std::sqrt(double(Batches) / (Batches - 1)));
        std::printf("RAY_BUDGET_CHECK fixture=%u mean_gates=%u maximum_bank8_difference=%.12g retained_slot_checks=%llu maximum_elision_difference=%.12g matched_samples=%llu\n",
                    Fixture, PassedMeans, MaximumPair, static_cast<unsigned long long>(RetainedSlotChecks), MaximumElision,
                    static_cast<unsigned long long>(MatchedSamples));
        Require(MaximumElision < 2e-6, "Query elision changed an estimator");
        std::fflush(stdout);
    }
    std::printf("RAY_BUDGET_DECISION correctness=1 promote=0 physical_gpu_timing=0\n");
}
} // namespace
