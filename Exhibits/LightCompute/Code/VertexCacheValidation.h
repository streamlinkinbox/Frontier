/*==========================================================================================================================================
                                                      VERTEXCACHEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Validate persistent triangle vertex/outgoing payloads, revision invalidation and fresh receiver visibility.
#pragma once

namespace
{
void ValidateVertexCache(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    constexpr uint32_t Batches = 32, Samples = 4096, Methods = 4, Steps = 8;
    const std::array<const char*, Methods> Names{"inline", "cache", "stale_light", "stale_geometry"};
    const std::array<const char*, Steps> States{"cold", "warm", "receiver", "light", "material", "geometry", "off", "on"};
    std::filesystem::create_directories(Directory);
    auto Scene = TriangleReconnectionScene(.7f);
    Scene.Width = 64; Scene.Height = 64 * Methods;
    Scene.Receivers.assign(Samples * Methods, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::printf("VERTEX_CACHE_DEVICE name=%s type=%u payload_bytes_per_receiver=32 telemetry_bytes_per_receiver=16\n",
                Compute.Properties.deviceName, unsigned(Compute.Properties.deviceType));
    std::fflush(stdout);
    for (uint32_t Fixture = 0; Fixture < 4; ++Fixture)
    {
        const float BaseBlocker = Fixture < 2 ? 0 : Fixture == 2 ? .12f : .6f;
        std::array<std::array<DirectGpuMoments, Methods>, Steps> Moments;
        std::array<std::array<double, Methods>, Steps> Squared{};
        std::array<std::array<uint64_t, Methods>, Steps> Queries{}, SourceQueries{}, Reused{};
        std::array<double, Steps> Reference{}, Maximum{};
        for (uint32_t Step = 0; Step < Steps; ++Step)
        {
            const float Blocker = BaseBlocker + (Step >= 5 ? .05f : 0);
            const double X = Fixture == 0 || Step < 2 ? 0 : 1;
            const double Scale = Step < 3 || Step == 7 ? 1 : Step == 6 ? 0 : .5;
            const double MaterialScale = Step >= 4 ? .5 : 1;
            Reference[Step] = TriangleReconnectionIntegral(X, Blocker, 2048) * Scale * MaterialScale;
            const double Coarse = TriangleReconnectionIntegral(X, Blocker, 1024) * Scale * MaterialScale;
            Require(std::abs(Reference[Step] - Coarse) < 1e-7, "Vertex cache quadrature has not converged");
            std::printf("VERTEX_CACHE_REFERENCE fixture=%u step=%s integral=%.12g\n", Fixture, States[Step], Reference[Step]);
        }
        std::mt19937_64 Random(0x73a4c951ull + Fixture * 0x1000193ull);
        for (uint32_t Batch = 0; Batch < Batches; ++Batch)
        {
            const uint32_t Seed = uint32_t(Random());
            for (uint32_t Step = 0; Step < Steps; ++Step)
            {
                const float Blocker = BaseBlocker + (Step >= 5 ? .05f : 0);
                const auto Geometry = TriangleReconnectionScene(Blocker);
                Scene.Surfaces = Geometry.Surfaces;
                Scene.Visibility = Geometry.Visibility;
                if (Step >= 4)
                    for (auto& F : Scene.Surfaces)
                        if (F.A[2] == 1) F.AlbedoRoughness = {.35f, .35f, .35f, 1};
                for (uint32_t Pixel = 0; Pixel < Scene.Receivers.size(); ++Pixel)
                {
                    auto& R = Scene.Receivers[Pixel];
                    R.PositionHit[0] = Fixture == 0 || Step < 2 ? 0 : 1;
                    R.MotionPixel[2] = float(Pixel / Samples);
                    R.MotionNormal[3] = Step == 0 ? 1.f : 0.f;
                    R.ResidualSpecular[0] = Step < 3 || Step == 7 ? 1.f : Step == 6 ? 0.f : .5f;
                    R.MotionPositionToken[3] = Step < 3 ? 1.f : Step < 6 ? 2.f : Step == 6 ? 3.f : 4.f;
                }
                const auto Image = Compute.ExecuteDiffuse(Scene, 2, Seed, true, false, Step == 0);
                if (Batch == 0)
                {
                    std::ofstream Capture(Directory + "/VertexCache" + std::to_string(Fixture) + "-" + std::to_string(Step) + ".f32",
                                          std::ios::binary);
                    Capture.exceptions(std::ios::failbit | std::ios::badbit);
                    Capture.write(reinterpret_cast<const char*>(Image.data()), std::streamsize(Image.size() * 16));
                }
                std::array<double, Methods> Sum{};
                for (uint32_t Pixel = 0; Pixel < Image.size(); ++Pixel)
                {
                    const uint32_t Method = Pixel / Samples;
                    const auto& P = Image[Pixel];
                    Require(std::all_of(P.begin(), P.end(), [](float V) { return std::isfinite(V) && V >= 0; }),
                            "Invalid vertex cache readback");
                    Require((P[2] == 0 || P[2] == 1) && P[1] == std::floor(P[1]) && P[3] == std::floor(P[3]),
                            "Vertex cache telemetry invalid");
                    if (Method == 1)
                    {
                        const bool ExpectedReuse = Step == 1 || Step == 2;
                        Require(P[2] == float(ExpectedReuse), "Cache reuse/invalidation cadence incorrect");
                        Require(!ExpectedReuse || (P[3] == 0 && P[1] <= 1), "Warm cache retraced source/suffix");
                        Maximum[Step] = std::max(Maximum[Step], std::abs(double(P[0]) - Image[Pixel - Samples][0]));
                    }
                    Queries[Step][Method] += uint32_t(P[1]);
                    SourceQueries[Step][Method] += uint32_t(P[3]);
                    Reused[Step][Method] += uint32_t(P[2]);
                    Sum[Method] += P[0];
                    const double Error = P[0] - Reference[Step];
                    Squared[Step][Method] += Error * Error;
                }
                for (uint32_t Method = 0; Method < Methods; ++Method) Moments[Step][Method].Accumulate(Sum[Method] / Samples);
            }
        }
        for (uint32_t Step = 0; Step < Steps; ++Step)
        {
            Require(Maximum[Step] < 2e-6, "Persistent cache differs from same-seed current inline estimator");
            for (uint32_t Method = 0; Method < Methods; ++Method)
            {
                const double Mean = Moments[Step][Method].Mean();
                const double Error = Moments[Step][Method].StandardError() * std::sqrt(double(Batches) / (Batches - 1));
                const bool Gate = std::abs(Mean - Reference[Step]) < 4 * Error + .0001;
                const bool Negative = (Method == 2 && (Step == 3 || Step == 6)) || (Method == 3 && Step == 4);
                const bool NegativeGate = std::abs(Mean - Reference[Step]) > 6 * Error + .005 * Reference[Step];
                std::printf("VERTEX_CACHE_RESULT fixture=%u step=%s method=%s mean=%.12g nominal_se=%.12g rmse=%.12g "
                            "queries=%llu source_queries=%llu reused=%llu mean_gate=%u negative_required=%u negative_gate=%u\n",
                            Fixture, States[Step], Names[Method], Mean, Error,
                            std::sqrt(Squared[Step][Method] / (Batches * Samples)),
                            static_cast<unsigned long long>(Queries[Step][Method]),
                            static_cast<unsigned long long>(SourceQueries[Step][Method]),
                            static_cast<unsigned long long>(Reused[Step][Method]), unsigned(Gate), unsigned(Negative), unsigned(NegativeGate));
                Require(Method >= 2 || Gate, "Inline or cached estimate failed independent integral");
                Require(!Negative || NegativeGate, "Stale-cache negative control did not expose expected error");
            }
            std::printf("VERTEX_CACHE_IDENTITY fixture=%u step=%s maximum_difference=%.12g\n", Fixture, States[Step], Maximum[Step]);
        }
        std::fflush(stdout);
    }
    std::printf("VERTEX_CACHE_DECISION correctness=1 promote=0 physical_gpu_timing=0\n");
}
} // namespace
