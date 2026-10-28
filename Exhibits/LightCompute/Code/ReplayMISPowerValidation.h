/*==========================================================================================================================================
                                                      REPLAYMISPOWERVALIDATION.H
==========================================================================================================================================*/
// 🧩 Compare fixed no-MIS, balance and power controls at identical Fresh8 traversal budgets.
#pragma once

namespace
{
void ValidateReplayMISPower(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32;
    const std::array<float, 3> Environment{.025f, .025f, .025f};
    std::filesystem::create_directories(Directory);
    bool Correctness = true, Quality = true;
    for (uint32_t Condition = 0; Condition < 2; ++Condition)
    {
        const auto Scene = Condition == 0 ? ReplayCameraPlane(.03125f, 0) : ReplaySplitRoom();
        const uint32_t CpuSamples = Condition == 0 ? 4096u : 2048u;
        LightComputeSubmission Balance(Driver), Power(Driver);
        Balance.Prepare(Scene, Shaders + "/balance", false, false, false, false, true, false, true);
        Power.Prepare(Scene, Shaders + "/power", false, false, false, false, true, false, true);
        ReplayFreshBudgetPipeline BalanceAccess(Balance, Shaders + "/balance");
        ReplayFreshBudgetPipeline PowerAccess(Power, Shaders + "/power");
        std::printf("POWER_DEVICE condition=%u name=%s type=%u output_bytes=%llu\n", Condition,
                    Balance.Properties.deviceName, unsigned(Balance.Properties.deviceType),
                    static_cast<unsigned long long>(Balance.ReplayOutputDescriptorBytes));
        std::mt19937_64 Seeds(0x58a1df93ull + Condition * 0x1000193ull);
        std::vector<uint32_t> Pixels;
        std::vector<double> References;
        std::array<DirectGpuMoments, 3> Means;
        std::array<double, 3> Squared{};
        std::array<uint64_t, 3> Nearest{}, Shadow{};
        DirectGpuMoments VersusBalance, VersusOff;
        double ReferenceMean = 0, ReferenceVariance = 0, MaximumDifference = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            const uint32_t Seed = uint32_t(Seeds());
            const auto Off = ExecuteFirstHopWhole(BalanceAccess, Scene, Seed, Environment, false);
            const auto OffCounts = Balance.ExtendedStatistics();
            const auto Balanced = ExecuteFirstHopWhole(BalanceAccess, Scene, Seed, Environment, true);
            const auto BalanceCounts = Balance.ExtendedStatistics();
            const auto Powered = ExecuteFirstHopWhole(PowerAccess, Scene, Seed, Environment, true);
            const auto PowerCounts = Power.ExtendedStatistics();
            for (size_t Pixel = 0; Pixel < Powered.size(); ++Pixel)
                MaximumDifference = std::max(MaximumDifference,
                    std::abs(double(Powered[Pixel][0]) - Balanced[Pixel][0]));
            Require(OffCounts[17] == BalanceCounts[17] && OffCounts[17] == PowerCounts[17] &&
                        OffCounts[2] == BalanceCounts[2] && OffCounts[2] == PowerCounts[2],
                    "MIS heuristic changed traversal budget");
            const auto OffPower = ExecuteFirstHopWhole(PowerAccess, Scene, Seed, Environment, false);
            Require(ReplayCandidateImagesEqual(Off, OffPower), "Disabled power control changed output bits");
            const std::array<std::array<uint32_t, 24>, 3> Counts{OffCounts, BalanceCounts, PowerCounts};
            for (uint32_t Method = 0; Method < 3; ++Method)
            {
                Nearest[Method] += Counts[Method][17];
                Shadow[Method] += Counts[Method][2];
            }
            if (Batch == 0)
            {
                const auto Guides = Balance.PrimaryReceivers();
                for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
                {
                    const auto& Receiver = Guides[Pixel];
                    if (Receiver.PositionHit[3] == 0 || Receiver.MotionPositionToken[3] <= 0 ||
                        std::max({Receiver.AlbedoIor[0], Receiver.AlbedoIor[1], Receiver.AlbedoIor[2]}) <= 0) continue;
                    Pixels.push_back(Pixel);
                    std::mt19937_64 Random(0x36de917bull + Condition * 0x1000193ull +
                                           uint64_t(Pixel) * 0x9e3779b97f4a7c15ull);
                    DirectGpuMoments Cpu;
                    for (uint32_t Sample = 0; Sample < CpuSamples; ++Sample)
                        Cpu.Accumulate(ReplayReferencePath(Scene, Receiver, 4, Random)[0]);
                    References.push_back(Cpu.Mean());
                    ReferenceMean += Cpu.Mean();
                    const double Error = Cpu.StandardError() * std::sqrt(double(CpuSamples) / (CpuSamples - 1));
                    ReferenceVariance += Error * Error;
                }
                Require(Pixels.size() > 100, "MIS fixture mask is too small");
                ReferenceMean /= Pixels.size();
                const std::string Prefix = Directory + "/Power" + std::to_string(Condition);
                WriteImage(Prefix + "Off.ppm", Off, Scene.Width, Scene.Height, 3);
                WriteImage(Prefix + "Balance.ppm", Balanced, Scene.Width, Scene.Height, 3);
                WriteImage(Prefix + "Power.ppm", Powered, Scene.Width, Scene.Height, 3);
                std::printf("POWER_REFERENCE condition=%u points=%zu cpu_paths=%zu mean=%.9g nominal_se=%.9g\n",
                            Condition, Pixels.size(), Pixels.size() * CpuSamples, ReferenceMean,
                            std::sqrt(ReferenceVariance) / Pixels.size());
            }
            std::array<double, 3> Sum{}, Errors{};
            const std::array<const std::vector<std::array<float, 4>>*, 3> Images{&Off, &Balanced, &Powered};
            for (size_t Index = 0; Index < Pixels.size(); ++Index)
                for (uint32_t Method = 0; Method < 3; ++Method)
                {
                    const double Red = (*Images[Method])[Pixels[Index]][0];
                    Require(std::isfinite(Red) && Red >= 0, "MIS control produced invalid radiance");
                    const double Error = Red - References[Index];
                    Sum[Method] += Red;
                    Errors[Method] += Error * Error;
                }
            for (uint32_t Method = 0; Method < 3; ++Method)
            {
                Means[Method].Accumulate(Sum[Method] / Pixels.size());
                Squared[Method] += Errors[Method];
            }
            VersusBalance.Accumulate((Errors[2] - Errors[1]) / Pixels.size());
            VersusOff.Accumulate((Errors[2] - Errors[0]) / Pixels.size());
            if ((Batch + 1) % 8 == 0)
                std::printf("POWER_PROGRESS condition=%u completed=%u/%u\n", Condition, Batch + 1, Batches);
            std::fflush(stdout);
        }
        Require(MaximumDifference > 1e-7, "Power heuristic did not affect sampled output");
        std::printf("POWER_ACTIVE condition=%u maximum_balance_difference=%.9g\n", Condition, MaximumDifference);
        const double Correction = std::sqrt(double(Batches) / (Batches - 1));
        for (uint32_t Method = 0; Method < 3; ++Method)
        {
            const double Error = Means[Method].StandardError() * Correction;
            const bool MeanAgrees = std::abs(Means[Method].Mean() - ReferenceMean) <
                4 * std::hypot(Error, std::sqrt(ReferenceVariance) / Pixels.size()) + .001;
            Correctness = Correctness && MeanAgrees;
            std::printf("POWER_RESULT condition=%u method=%s batches=%u points=%zu mean=%.9g nominal_se=%.9g "
                        "rmse=%.9g mean_gate=%u nearest=%llu shadow=%llu\n", Condition,
                        Method == 0 ? "off" : Method == 1 ? "balance" : "power", Batches, Pixels.size(),
                        Means[Method].Mean(), Error, std::sqrt(Squared[Method] / (Batches * Pixels.size())),
                        unsigned(MeanAgrees), static_cast<unsigned long long>(Nearest[Method]),
                        static_cast<unsigned long long>(Shadow[Method]));
        }
        for (const auto& Pair : {std::pair{"balance", &VersusBalance}, std::pair{"off", &VersusOff}})
        {
            const double Error = Pair.second->StandardError() * Correction;
            const bool Pass = Pair.second->Mean() + 3 * Error < 0;
            Quality = Quality && Pass;
            std::printf("POWER_PAIRED condition=%u control=%s mse_difference=%.9g nominal_se=%.9g quality_gate=%u\n",
                        Condition, Pair.first, Pair.second->Mean(), Error, unsigned(Pass));
        }
        std::printf("POWER_PARITY condition=%u disabled_output_exact=1 matched_query_counts=1 submissions=%u\n",
                    Condition, Batches);
        std::fflush(stdout);
    }
    std::printf("POWER_DECISION correctness=%u promote=%u physical_gpu_timing=0\n",
                unsigned(Correctness), unsigned(Correctness && Quality));
    Require(Correctness, "MIS control failed independent reference mean gate");
}
}
