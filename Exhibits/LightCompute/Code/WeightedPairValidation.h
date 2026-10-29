/*==========================================================================================================================================
                                                     WEIGHTEDPAIRVALIDATION.H
==========================================================================================================================================*/
// 🧩 Measure bounded payload refresh, moving receiver error, temporal correlation and actual traversal queries.
#pragma once

namespace
{
void ValidateWeightedPair(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, Samples = 1024, Methods = 14, Poses = 16;
    const std::array<const char*, Methods> Names{"area8", "bank8", "rotate4", "rotate2", "fresh6", "fresh8", "fastarea8", "fastbank8", "fastrotate4", "fastrotate2", "fastfresh6", "fastfresh8", "full2", "rotate1"};
    const std::array<float, 4> Blockers{0, .12f, .3f, .6f};
    const std::array<uint32_t, 4> PairArm{12, 13, 13, 13}, PairControl{11, 11, 10, 8};
    std::filesystem::create_directories(Directory);
    auto Scene = TriangleReconnectionScene(.7f);
    Scene.Width = 32; Scene.Height = 32 * Methods;
    Scene.Receivers.assign(Samples * Methods, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::printf("WEIGHTED_PAIR_DEVICE name=%s type=%u\n", Compute.Properties.deviceName, unsigned(Compute.Properties.deviceType));
    std::fflush(stdout);
    uint32_t EdgeFailures = 0, HistoricalDisagreements = 0;
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
            if (ProbeIndex >= 960)
            {
                const uint32_t Near = ProbeIndex - 960;
                const std::array<float, 3> Receivers{std::nextafter(1.f, 0.f), 1.f, std::nextafter(1.f, 2.f)};
                R.PositionHit[0] = Receivers[(Near / 21) % 3];
                float Target = 2 * Blockers[Fixture] - R.PositionHit[0] + 1e-7f;
                const int Steps = int(Near % 7) - 3;
                for (int Step = 0; Step < std::abs(Steps); ++Step)
                    Target = std::nextafter(Target, Steps < 0 ? -10.f : 10.f);
                R.MotionPixel[0] = Target;
                R.MotionPixel[1] = float((Near / 7) % 3) * .5f - .5f;
            }
            R.MotionPixel[2] = 0;
            R.ResidualSpecular[1] = 1;
        }
        auto Image = Compute.ExecuteDiffuse(Scene, 2, 17, true, false, true);
        Image.resize(Samples);
        uint32_t Hits = 0, Disagreements = 0, SafeDifferences = 0;
        for (const auto& P : Image)
        {
            Require(P[0] == 0 || P[0] == 1, "Invalid first-hit edge result");
            Hits += uint32_t(P[0]); Disagreements += P[0] != P[1]; SafeDifferences += P[1] != P[2];
        }
        std::ofstream Capture(Directory + "/WeightedPairEdge" + std::to_string(Fixture) + ".f32", std::ios::binary);
        Capture.exceptions(std::ios::failbit | std::ios::badbit);
        Capture.write(reinterpret_cast<const char*>(Image.data()), std::streamsize(Image.size() * 16));
        std::printf("WEIGHTED_PAIR_EDGE fixture=%u probes=%u hits=%u disagreements=%u safe_differences=%u\n", Fixture, Samples, Hits, Disagreements, SafeDifferences);
        Require(Hits > 0, "Edge probes did not exercise any valid first hits");
        EdgeFailures += SafeDifferences;
        const auto* Diagnostic = static_cast<const std::array<float, 4>*>(Compute.Buffers[6].Address);
        std::vector<std::array<float, 4>> Rays(Samples * 5);
        for (uint32_t Pixel = 0; Pixel < Samples; ++Pixel)
            for (uint32_t Slot = 0; Slot < 5; ++Slot)
                Rays[Pixel * 5 + Slot] = Diagnostic[(11 + Slot) * Scene.Receivers.size() + Pixel];
        std::ofstream Details(Directory + "/WeightedPairRays" + std::to_string(Fixture) + ".f32", std::ios::binary);
        Details.exceptions(std::ios::failbit | std::ios::badbit);
        Details.write(reinterpret_cast<const char*>(Rays.data()), std::streamsize(Rays.size() * 16));
        if (Fixture == 2)
            for (uint32_t Pixel : {918u, 938u, 958u})
            {
                Require(Image[Pixel][0] == 1 && Image[Pixel][1] == 0 && Image[Pixel][2] == 0,
                        "Original boundary regression was hidden or changed");
                ++HistoricalDisagreements;
            }
        for (uint32_t Pixel = 0; Pixel < Samples; ++Pixel)
            if (Image[Pixel][0] != Image[Pixel][1])
                std::printf("WEIGHTED_PAIR_EDGE_DETAIL fixture=%u pixel=%u receiver_x=%.9g target_x=%.9g target_y=%.9g first=%.0f recheck=%.0f\n",
                            Fixture, Pixel, Scene.Receivers[Pixel].PositionHit[0], Scene.Receivers[Pixel].MotionPixel[0],
                            Scene.Receivers[Pixel].MotionPixel[1], Image[Pixel][0], Image[Pixel][1]);
    }
    std::fflush(stdout);
    Require(HistoricalDisagreements == 3, "Missing original boundary regressions");
    Require(EdgeFailures == 0, "Confirmed visibility must preserve the reconstructed-point contract");
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
            std::printf("WEIGHTED_PAIR_REFERENCE fixture=%u pose=%u x=%.9g integral=%.12g scale=%.1f\n",
                        Fixture, Pose, Positions[Pose], References[Pose], Scale);
        }
        std::array<std::array<DirectGpuMoments, Methods>, Poses> Means;
        std::array<DirectGpuMoments, Methods> Instant, Temporal, Flicker;
        std::array<DirectGpuMoments, 4> InstantDifference, TemporalDifference;
        std::array<CacheErrorPairs, Methods> Lag;
        std::array<uint64_t, Methods> Queries{}, SourceQueries{}, Reused{};
        double MaximumPair = 0, MaximumElision = 0, MaximumCompression = 0;
        uint64_t PairRetention = 0;
        uint64_t MatchedSamples = 0;
        uint64_t RetainedSlotChecks = 0;
        std::mt19937_64 Random(0x38d29a17ull + Fixture * 0x1000193ull);
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            std::vector<std::array<float, 4>> PreviousCandidates(6 * Samples * 8), Candidates(6 * Samples * 8);
            std::vector<std::array<float, 4>> PriorSelected(4 * Samples);
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
                std::vector<std::array<float, 4>> PairCapture(2 * Samples * 17);
                constexpr std::array<uint32_t, 17> PairSlots{1,2,9,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
                for (uint32_t Arm = 0; Arm < 2; ++Arm)
                    for (uint32_t Sample = 0; Sample < Samples; ++Sample)
                    {
                        for (uint32_t Slot = 0; Slot < PairSlots.size(); ++Slot)
                            PairCapture[(Arm * Samples + Sample) * 17 + Slot] =
                                Storage[PairSlots[Slot] * Samples * Methods + (12 + Arm) * Samples + Sample];
                        const auto& Metadata = PairCapture[(Arm * Samples + Sample) * 17 + 2];
                        Require(Metadata[0] == 2 && Metadata[2] == (Pose < 9 ? 1.f : Pose < 13 ? 2.f : 3.f),
                                "Weighted pair epoch was not rebuilt");
                        for (uint32_t Lane = 0; Lane < 2; ++Lane)
                        {
                            const auto& C = PairCapture[(Arm * Samples + Sample) * 17 + Lane];
                            Require(std::all_of(C.begin(), C.end(), [](float X) { return std::isfinite(X); }) &&
                                    C[2] >= 0 && C[3] >= 0 && std::abs(C[0]) <= .5f && std::abs(C[1]) <= .5f,
                                    "Invalid weighted pair payload");
                            if (C[3] == 0) Require(C == std::array<float, 4>{}, "Empty pair must be canonical zero");
                            const uint32_t Offset = (Arm * Samples + Sample) * 2 + Lane;
                            if (Arm == 1 && Pose != 0 && Pose != 9 && Pose != 13 && Lane != Pose % 2)
                            {
                                Require(C == PriorSelected[Offset], "Historical pair weight/point changed");
                                ++PairRetention;
                            }
                            PriorSelected[Offset] = C;
                        }
                        if (Fixture == 0)
                            MaximumCompression = std::max(MaximumCompression,
                                std::abs(double(Image[12 * Samples + Sample][0]) - Image[7 * Samples + Sample][0]));
                    }
                if (Batch == 0)
                {
                    std::ofstream PairFile(Directory + "/WeightedPairSelected" + std::to_string(Fixture) + "-" +
                                           std::to_string(Pose) + ".f32", std::ios::binary);
                    PairFile.exceptions(std::ios::failbit | std::ios::badbit);
                    PairFile.write(reinterpret_cast<const char*>(PairCapture.data()), std::streamsize(PairCapture.size() * 16));
                }
                for (uint32_t Offset = 0; Offset < 3 * Samples * 8; ++Offset)
                    Require(Candidates[Offset] == Candidates[Offset + 3 * Samples * 8], "Query elision changed a stored candidate");
                PreviousCandidates = Candidates;
                if (Batch == 0)
                {
                    std::ofstream Capture(Directory + "/WeightedPair" + std::to_string(Fixture) + "-" + std::to_string(Pose) + ".f32",
                                          std::ios::binary);
                    Capture.exceptions(std::ios::failbit | std::ios::badbit);
                    Capture.write(reinterpret_cast<const char*>(Image.data()), std::streamsize(Image.size() * 16));
                    std::ofstream Payload(Directory + "/WeightedPairPayload" + std::to_string(Fixture) + "-" +
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
                    const uint32_t Retained = Method >= 12 ?
                        (Method == 13 && Pose != 0 && Pose != 9 && Pose != 13 ? 1u : 0u) : (Local == 2 || Local == 3) && Pose != 0 && Pose != 9 && Pose != 13
                        ? (Local == 2 ? 4u : 6u) : 0u;
                    Require(P[2] == float(Retained), "Candidate rotation or light invalidation failed");
                    if (Method < 12 && Local >= 1 && Local <= 3)
                        Require(P[1] >= P[3] && P[1] - P[3] <= 8, "Bank receiver query budget exceeded");
                    if (Method < 12 && Local == 1) Require(P[1] == Image[Pixel - Samples][1], "Bank full-refresh query identity failed");
                    if (Method < 12 && Local == 1) MaximumPair = std::max(MaximumPair, std::abs(double(P[0]) - Image[Pixel - Samples][0]));
                    if (Method >= 6 && Method < 12)
                    {
                        const auto& Legacy = Image[Pixel - 6 * Samples];
                        MaximumElision = std::max(MaximumElision, std::abs(double(P[0]) - Legacy[0]));
                        Require(P[1] <= Legacy[1], "Query elision increased traversal calls");
                        if (Local >= 4)
                        {
                            const auto& Audit = Storage[16 * Samples * Methods + Pixel];
                            Require(Audit[0] == (Local == 4 ? 6.f : 8.f) && Legacy[1] - P[1] == Audit[1],
                                    "Fresh savings must exactly equal out-of-domain misses, not all candidates");
                        }
                        ++MatchedSamples;
                    }
                    if (Method >= 12) Require(P[1] >= P[3] && P[1] - P[3] <= 2,
                                             "Weighted pair receiver query budget exceeded");
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
        Require(MaximumCompression < 2e-6, "Positive-target compression identity failed on empty strip");
        std::printf("WEIGHTED_PAIR_COMPRESSION fixture=%u maximum_empty_difference=%.12g retained=%llu logical_bytes=48\n",
                    Fixture, MaximumCompression, static_cast<unsigned long long>(PairRetention));
        Require(MaximumPair < 2e-6, "Full-refresh bank disagrees with inline eight-draw area IS");
        uint32_t PassedMeans = 0;
        for (uint32_t Pose = 0; Pose < Poses; ++Pose)
            for (uint32_t Method = 0; Method < Methods; ++Method)
            {
                const auto& M = Means[Pose][Method];
                const double Error = M.StandardError() * std::sqrt(double(Batches) / (Batches - 1));
                const bool Gate = std::abs(M.Mean() - References[Pose]) < 4 * Error + .0001;
                std::printf("WEIGHTED_PAIR_MEAN fixture=%u pose=%u method=%s mean=%.12g nominal_se=%.12g gate=%u\n",
                            Fixture, Pose, Names[Method], M.Mean(), Error, unsigned(Gate));
                Require(Gate, "Refresh mean disagrees with independent integral");
                PassedMeans += Gate;
            }
        for (uint32_t Method = 0; Method < Methods; ++Method)
            std::printf("WEIGHTED_PAIR_RESULT fixture=%u method=%s rmse=%.12g temporal_rmse=%.12g flicker_rmse=%.12g lag1=%.12g "
                        "queries=%llu source_queries=%llu reused=%llu\n", Fixture, Names[Method],
                        std::sqrt(Instant[Method].Mean()), std::sqrt(Temporal[Method].Mean()), std::sqrt(Flicker[Method].Mean()),
                        Lag[Method].Correlation(), static_cast<unsigned long long>(Queries[Method]),
                        static_cast<unsigned long long>(SourceQueries[Method]), static_cast<unsigned long long>(Reused[Method]));
        for (uint32_t Pair = 0; Pair < 4; ++Pair)
            std::printf("WEIGHTED_PAIR_PAIRED fixture=%u arm=%s control=%s instantaneous_mse=%.12g instantaneous_se=%.12g "
                        "temporal_mse=%.12g temporal_se=%.12g\n", Fixture, Names[PairArm[Pair]], Names[PairControl[Pair]],
                        InstantDifference[Pair].Mean(), InstantDifference[Pair].StandardError() * std::sqrt(double(Batches) / (Batches - 1)),
                        TemporalDifference[Pair].Mean(), TemporalDifference[Pair].StandardError() * std::sqrt(double(Batches) / (Batches - 1)));
        std::printf("WEIGHTED_PAIR_CHECK fixture=%u mean_gates=%u maximum_bank8_difference=%.12g retained_slot_checks=%llu maximum_elision_difference=%.12g matched_samples=%llu\n",
                    Fixture, PassedMeans, MaximumPair, static_cast<unsigned long long>(RetainedSlotChecks), MaximumElision,
                    static_cast<unsigned long long>(MatchedSamples));
        Require(MaximumElision < 2e-6, "Query elision changed an estimator");
        std::fflush(stdout);
    }
    std::printf("WEIGHTED_PAIR_DECISION correctness=1 promote=0 physical_gpu_timing=0\n");
}
} // namespace
