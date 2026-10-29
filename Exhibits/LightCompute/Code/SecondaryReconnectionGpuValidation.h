/*==========================================================================================================================================
                                                SECONDARYRECONNECTIONGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Check actual Vulkan secondary-vertex readbacks against independent clipped-area quadrature.
#pragma once

namespace
{
double SecondaryGpuIntegral(double ReceiverX, double Blocker, uint32_t Steps, bool Restricted = false)
{
    std::array<double, 6> Cuts{-.1, .5, -2 * Blocker - ReceiverX, 2 * Blocker - ReceiverX,
                              -2 * Blocker, 2 * Blocker};
    std::sort(Cuts.begin(), Cuts.end());
    double Integral = 0;
    for (size_t Segment = 1; Segment < Cuts.size(); ++Segment)
    {
        const double Low = std::max(-.1, Cuts[Segment - 1]), High = std::min(.5, Cuts[Segment]);
        if (High <= Low) continue;
        const double Centre = (Low + High) * .5;
        if (Blocker > 0 && std::abs((Centre + ReceiverX) * .5) <= Blocker) continue;
        if (Restricted && Blocker > 0 && std::abs(Centre * .5) <= Blocker) continue;
        const double Dx = (High - Low) / Steps, Dy = 1. / Steps;
        for (uint32_t X = 0; X < Steps; ++X)
            for (uint32_t Y = 0; Y < Steps; ++Y)
            {
                const double A = Low + (X + .5) * Dx - ReceiverX, B = -.5 + (Y + .5) * Dy;
                const double DistanceSquared = 1 + A * A + B * B;
                Integral += .42 * Dx * Dy / (3.14159265358979323846 * DistanceSquared * DistanceSquared);
            }
    }
    return Integral;
}

void ValidateSecondaryReconnectionGpu(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 64, Samples = 4096, Methods = 7;
    const std::array<const char*, Methods> Names{"angular", "area", "missing_j", "double_j", "no_fallback",
                                               "no_floor", "fresh8"};
    std::filesystem::create_directories(Directory);
    auto Scene = RectangleGpuFixture();
    Scene.Width = 64;
    Scene.Height = 64 * Methods;
    const auto Receiver = *std::find_if(Scene.Receivers.begin(), Scene.Receivers.end(),
        [](const auto& R) { return R.PositionHit[3] != 0; });
    Scene.Receivers.assign(Samples * Methods, Receiver);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, false, true);
    std::printf("SECONDARY_GPU_DEVICE name=%s type=%u\n", Compute.Properties.deviceName,
                unsigned(Compute.Properties.deviceType));
    std::fflush(stdout);
    for (uint32_t Fixture = 0; Fixture < 4; ++Fixture)
    {
        const double ReceiverX = Fixture == 0 ? 0 : 1;
        const float Blocker = Fixture < 2 ? 0 : Fixture == 2 ? .12f : .6f;
        const double Reference = SecondaryGpuIntegral(ReceiverX, Blocker, 2048);
        const double Refinement = std::abs(Reference - SecondaryGpuIntegral(ReceiverX, Blocker, 1024));
        const double Restricted = SecondaryGpuIntegral(ReceiverX, Blocker, 2048, true);
        Require(Refinement < 1e-7, "Secondary GPU reference quadrature did not converge");
        std::printf("SECONDARY_GPU_REFERENCE fixture=%u integral=%.12g refinement=%.12g donor_support=%.12g\n",
                    Fixture, Reference, Refinement, Restricted);
        for (uint32_t Pixel = 0; Pixel < Scene.Receivers.size(); ++Pixel)
        {
            auto& R = Scene.Receivers[Pixel];
            R.PositionHit[0] = float(ReceiverX);
            R.PositionHit[1] = R.PositionHit[2] = 0;
            R.AlbedoIor[3] = Blocker;
            R.MotionPixel[2] = float(Pixel / Samples);
        }
        std::mt19937_64 Random(0x296ad713ull + Fixture * 0x1000193ull);
        std::array<DirectGpuMoments, Methods> Statistics;
        std::array<double, Methods> Squared{};
        uint64_t Present = 0, Considered = 0;
        double MaximumDifference = 0;
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            const auto Readback = Compute.ExecuteRectangle(Scene, uint32_t(Random()), false, false, true);
            if (Batch == 0)
            {
                std::ofstream Capture(Directory + "/SecondaryGpu" + std::to_string(Fixture) + ".f32",
                                      std::ios::binary);
                Capture.exceptions(std::ios::failbit | std::ios::badbit);
                Capture.write(reinterpret_cast<const char*>(Readback.data()), std::streamsize(Readback.size() * 16));
            }
            std::array<double, Methods> Sum{};
            for (uint32_t Pixel = 0; Pixel < Readback.size(); ++Pixel)
            {
                const auto& P = Readback[Pixel];
                const uint32_t Method = Pixel / Samples;
                Require(std::all_of(P.begin(), P.end(), [](float V) { return std::isfinite(V) && V >= 0; }),
                        "Invalid secondary GPU readback");
                Require(P[2] == 8 && P[1] <= 8 && P[1] == std::floor(P[1]), "GPU null candidate accounting changed");
                Sum[Method] += P[0];
                Squared[Method] += (P[0] - Reference) * (P[0] - Reference);
                if (Method == 0)
                {
                    MaximumDifference = std::max(MaximumDifference, std::abs(double(P[0]) - Readback[Pixel + Samples][0]));
                    Require(P[1] == Readback[Pixel + Samples][1], "Paired GPU area/angular support differs");
                    Present += uint32_t(P[1]);
                    Considered += 8;
                }
            }
            for (uint32_t Method = 0; Method < Methods; ++Method) Statistics[Method].Accumulate(Sum[Method] / Samples);
        }
        Require(MaximumDifference < 2e-6 && Present > 0 && Present < Considered,
                "GPU area/angular identity or null support failed");
        for (uint32_t Method = 0; Method < Methods; ++Method)
        {
            const double Mean = Statistics[Method].Mean();
            const double Error = Statistics[Method].StandardError() * std::sqrt(double(Batches) / (Batches - 1));
            const bool Correct = Method < 2 || Method == 6;
            const bool Negative = ((Method == 2 || Method == 3) && Fixture == 1) ||
                                  ((Method == 4 || Method == 5) && Fixture >= 2);
            const bool MeanGate = std::abs(Mean - Reference) < 4 * Error + .0001;
            const bool NegativeGate = std::abs(Mean - Reference) > 6 * Error + .005 * Reference;
            std::printf("SECONDARY_GPU_RESULT fixture=%u method=%s mean=%.12g nominal_se=%.12g rmse=%.12g "
                        "mean_gate=%u negative_required=%u negative_gate=%u\n", Fixture, Names[Method], Mean, Error,
                        std::sqrt(Squared[Method] / (Batches * Samples)), unsigned(MeanGate), unsigned(Negative),
                        unsigned(NegativeGate));
            Require(!Correct || MeanGate, "GPU secondary transport failed independent reference");
            Require(!Negative || NegativeGate, "GPU negative control failed to reveal expected error");
            if (Method == 4 || Method == 5)
                Require(std::abs(Mean - Restricted) < 4 * Error + .0001, "GPU support-limited reference disagrees");
        }
        std::printf("SECONDARY_GPU_IDENTITY fixture=%u maximum_difference=%.12g present=%llu considered=%llu\n",
                    Fixture, MaximumDifference, static_cast<unsigned long long>(Present),
                    static_cast<unsigned long long>(Considered));
        std::fflush(stdout);
    }
    std::printf("SECONDARY_GPU_DECISION correctness=1 promote=0 physical_gpu_timing=0\n");
}
} // namespace
