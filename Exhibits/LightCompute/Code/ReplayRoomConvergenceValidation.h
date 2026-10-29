/*==========================================================================================================================================
                                                 REPLAYROOMCONVERGENCEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Diagnose room sampling agreement with double-precision area proposals and bounded BSDF controls.
#pragma once

namespace
{
struct ReplayConvergenceFacet
{
    ReplayReferenceVector A{}, EdgeA{}, EdgeB{}, NormalA{}, NormalB{}, NormalC{}, Albedo{}, Radiance{};
    double Area = 0;
};

std::vector<ReplayConvergenceFacet> ReplayConvergenceFacets(const LightComputeArchive& Scene)
{
    std::vector<ReplayConvergenceFacet> Facets;
    for (const auto& F : Scene.Surfaces)
    {
        ReplayConvergenceFacet T;
        T.A = ReplayReferenceXyz(F.A);
        T.EdgeA = ReplayReferenceSub(ReplayReferenceXyz(F.B), T.A);
        T.EdgeB = ReplayReferenceSub(ReplayReferenceXyz(F.C), T.A);
        T.NormalA = ReplayReferenceXyz(F.NormalA);
        T.NormalB = ReplayReferenceXyz(F.NormalB);
        T.NormalC = ReplayReferenceXyz(F.NormalC);
        T.Albedo = ReplayReferenceXyz(F.AlbedoRoughness);
        T.Radiance = ReplayReferenceXyz(F.Emissive);
        const auto Cross = ReplayReferenceCross(T.EdgeA, T.EdgeB);
        T.Area = .5 * std::sqrt(ReplayReferenceDot(Cross, Cross));
        Facets.push_back(T);
    }
    return Facets;
}

bool ReplayConvergenceStrike(const ReplayConvergenceFacet& F, ReplayReferenceVector Origin,
                             ReplayReferenceVector Direction, double Minimum, double& Distance,
                             double& U, double& V)
{
    const auto P = ReplayReferenceCross(Direction, F.EdgeB);
    const double Determinant = ReplayReferenceDot(F.EdgeA, P);
    if (std::abs(Determinant) < 1e-12) return false;
    const auto Delta = ReplayReferenceSub(Origin, F.A);
    const double First = ReplayReferenceDot(Delta, P) / Determinant;
    if (First < 0 || First > 1) return false;
    const auto Q = ReplayReferenceCross(Delta, F.EdgeA);
    const double Second = ReplayReferenceDot(Direction, Q) / Determinant;
    if (Second < 0 || First + Second > 1) return false;
    const double Along = ReplayReferenceDot(F.EdgeB, Q) / Determinant;
    if (Along <= Minimum || Along >= Distance) return false;
    Distance = Along; U = First; V = Second;
    return true;
}

double ReplayConvergenceUnit(std::mt19937_64& Random)
{
    return std::generate_canonical<double, 53>(Random);
}

ReplayReferenceVector ReplayConvergenceDirection(ReplayReferenceVector Normal, std::mt19937_64& Random)
{
    const double U = ReplayConvergenceUnit(Random), Phi = 6.2831853071795864769 * ReplayConvergenceUnit(Random);
    const auto Axis = std::abs(Normal[0]) < .75 ? ReplayReferenceVector{1, 0, 0} : ReplayReferenceVector{0, 1, 0};
    const auto Tangent = ReplayReferenceUnit(ReplayReferenceCross(Axis, Normal));
    auto Direction = ReplayReferenceAdd(ReplayReferenceScale(Tangent, std::sqrt(U) * std::cos(Phi)),
        ReplayReferenceScale(ReplayReferenceCross(Normal, Tangent), std::sqrt(U) * std::sin(Phi)));
    return ReplayReferenceUnit(ReplayReferenceAdd(Direction, ReplayReferenceScale(Normal, std::sqrt(1 - U))));
}

double ReplayConvergencePower(double Ratio, bool Bsdf)
{
    const double Small = Ratio <= 1 ? Ratio : 1 / Ratio;
    const double Low = Small * Small / (1 + Small * Small), High = 1 / (1 + Small * Small);
    return Bsdf ? (Ratio <= 1 ? Low : High) : (Ratio <= 1 ? High : Low);
}

double ReplayConvergencePath(const std::vector<ReplayConvergenceFacet>& Facets,
                             const std::vector<size_t>& Sources, const LightComputeReceiver& Receiver,
                             uint32_t Bounces, std::mt19937_64& Random, bool Power)
{
    constexpr double π = 3.14159265358979323846;
    auto Normal = ReplayReferenceUnit(ReplayReferenceXyz(Receiver.NormalRoughness));
    auto Origin = ReplayReferenceAdd(ReplayReferenceXyz(Receiver.PositionHit), ReplayReferenceScale(Normal, .001));
    double Albedo = Receiver.AlbedoIor[0], Throughput = 1, Result = 0;
    for (uint32_t Bounce = 0; Bounce < Bounces; ++Bounce)
    {
        // 📝 Independently sampled uniform triangle area; no GPU RNG, float geometry or packed visibility.
        const size_t Source = Sources[std::uniform_int_distribution<size_t>(0, Sources.size() - 1)(Random)];
        const auto& Light = Facets[Source];
        const double Root = std::sqrt(ReplayConvergenceUnit(Random)), Along = ReplayConvergenceUnit(Random);
        const auto Point = ReplayReferenceAdd(Light.A,
            ReplayReferenceAdd(ReplayReferenceScale(Light.EdgeA, Root * (1 - Along)),
                               ReplayReferenceScale(Light.EdgeB, Root * Along)));
        auto Delta = ReplayReferenceSub(Point, Origin);
        const double SquaredDistance = ReplayReferenceDot(Delta, Delta);
        if (Light.Area > 0 && SquaredDistance > 1e-10)
        {
            const double Distance = std::sqrt(SquaredDistance);
            const auto Direction = ReplayReferenceScale(Delta, 1 / Distance);
            const double Cosine = std::max(0., ReplayReferenceDot(Normal, Direction));
            const double SourceCosine = std::abs(ReplayReferenceDot(
                ReplayReferenceUnit(ReplayReferenceCross(Light.EdgeA, Light.EdgeB)), Direction));
            bool Occluded = false;
            if (Cosine > 0 && SourceCosine > 0)
                for (size_t Index = 0; Index < Facets.size(); ++Index)
                {
                    double Limit = Distance, U = 0, V = 0;
                    if (Index != Source && ReplayConvergenceStrike(Facets[Index], Origin, Direction, 0, Limit, U, V))
                    { Occluded = true; break; }
                }
            if (!Occluded)
            {
                const double Ratio = Cosine * SourceCosine * Light.Area * Sources.size() / (π * SquaredDistance);
                Result += Throughput * Albedo * Light.Radiance[0] * Ratio *
                    (Power ? ReplayConvergencePower(Ratio, false) : 1);
            }
        }
        const auto Direction = ReplayConvergenceDirection(Normal, Random);
        Throughput *= Albedo;
        if (Throughput <= 0) break;
        double Distance = 1e30, U = 0, V = 0;
        size_t Hit = Facets.size();
        for (size_t Index = 0; Index < Facets.size(); ++Index)
            if (ReplayConvergenceStrike(Facets[Index], Origin, Direction, .0001, Distance, U, V)) Hit = Index;
        if (Hit == Facets.size()) { Result += Throughput * .025; break; }
        const auto& F = Facets[Hit];
        if (Power && F.Radiance[0] > 0)
        {
            const double Cosine = std::max(0., ReplayReferenceDot(Normal, Direction));
            const double SourceCosine = std::abs(ReplayReferenceDot(
                ReplayReferenceUnit(ReplayReferenceCross(F.EdgeA, F.EdgeB)), Direction));
            const double Ratio = Cosine * SourceCosine * F.Area * Sources.size() / (π * Distance * Distance);
            Result += Throughput * F.Radiance[0] * ReplayConvergencePower(Ratio, true);
        }
        Normal = ReplayReferenceUnit(ReplayReferenceAdd(ReplayReferenceScale(F.NormalA, 1 - U - V),
            ReplayReferenceAdd(ReplayReferenceScale(F.NormalB, U), ReplayReferenceScale(F.NormalC, V))));
        if (ReplayReferenceDot(Normal, Direction) > 0) Normal = ReplayReferenceScale(Normal, -1);
        Origin = ReplayReferenceAdd(ReplayReferenceAdd(Origin, ReplayReferenceScale(Direction, Distance)),
                                    ReplayReferenceScale(Normal, .001));
        Albedo = F.Albedo[0];
    }
    return Result;
}

void ValidateReplayNearEmitter()
{
    constexpr double π = 3.14159265358979323846, Height = .019, A = 1, B = 1.6, AlbedoRadiance = .7 * 12;
    const double X = std::sqrt(A * A + Height * Height), Y = std::sqrt(B * B + Height * Height);
    const double Exact = AlbedoRadiance * (2 / π) * (A / X * std::atan(B / X) + B / Y * std::atan(A / Y));
    std::array<DirectGpuMoments, 3> Moments;
    std::mt19937_64 Random(0x48cab51d);
    double Maximum = 0;
    for (uint32_t Sample = 1; Sample <= 4194304; ++Sample)
    {
        const double Px = (2 * ReplayConvergenceUnit(Random) - 1) * A;
        const double Py = (2 * ReplayConvergenceUnit(Random) - 1) * B;
        const double Squared = Height * Height + Px * Px + Py * Py;
        const double Ratio = 4 * A * B * Height * Height / (π * Squared * Squared);
        const double Nee = AlbedoRadiance * Ratio;
        const auto Direction = ReplayConvergenceDirection({0, 0, 1}, Random);
        const double Along = Height / Direction[2];
        const double Bsdf = std::abs(Direction[0] * Along) <= A && std::abs(Direction[1] * Along) <= B
            ? AlbedoRadiance : 0;
        const double HitSquared = Along * Along;
        const double HitRatio = 4 * A * B * Height * Height / (π * HitSquared * HitSquared);
        Moments[0].Accumulate(Bsdf);
        Moments[1].Accumulate(Nee);
        Moments[2].Accumulate(Nee * ReplayConvergencePower(Ratio, false) +
                              Bsdf * ReplayConvergencePower(HitRatio, true));
        Maximum = std::max(Maximum, Nee);
        if (Sample == 1024 || Sample == 16384 || Sample == 262144 || Sample == 4194304)
            for (uint32_t Method = 0; Method < 3; ++Method)
                std::printf("CONVERGENCE_ANALYTIC samples=%u method=%s mean=%.9g nominal_se=%.9g exact=%.9g "
                            "maximum_nee=%.9g\n", Sample, Method == 0 ? "bsdf" : Method == 1 ? "nee" : "power",
                            Moments[Method].Mean(), Moments[Method].StandardError(), Exact, Maximum);
    }
    for (const auto& M : Moments)
        Require(std::abs(M.Mean() - Exact) < 4 * M.StandardError() + .001,
                "Near-emitter sampler disagrees with analytic rectangle integral");
}

void ValidateReplayRoomConvergence(const char* Driver, const std::string& Shaders, const std::string& Directory)
{
    std::filesystem::create_directories(Directory);
    ValidateReplayNearEmitter();
    const auto Scene = ReplaySplitRoom();
    const auto Facets = ReplayConvergenceFacets(Scene);
    std::vector<size_t> Sources;
    for (size_t Index = 0; Index < Facets.size(); ++Index)
        if (Facets[Index].Radiance[0] > 0) Sources.push_back(Index);
    Require(Sources.size() == 2, "Room source domain changed");
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders + "/power", false, false, false, false, true, false, true);
    ReplayFreshBudgetPipeline Access(Compute, Shaders + "/power");
    (void)ExecuteFirstHopWhole(Access, Scene, 314159, {.025f, .025f, .025f}, true);
    const auto Guides = Compute.PrimaryReceivers();
    std::vector<uint32_t> Pixels;
    for (uint32_t Pixel = 0; Pixel < Guides.size(); ++Pixel)
        if (Guides[Pixel].PositionHit[3] != 0 && Guides[Pixel].MotionPositionToken[3] > 0 &&
            std::max({Guides[Pixel].AlbedoIor[0], Guides[Pixel].AlbedoIor[1], Guides[Pixel].AlbedoIor[2]}) > 0)
            Pixels.push_back(Pixel);
    Require(Pixels.size() == 528, "Room convergence mask changed");
    std::printf("CONVERGENCE_DEVICE name=%s type=%u points=%zu sources=%zu\n", Compute.Properties.deviceName,
                unsigned(Compute.Properties.deviceType), Pixels.size(), Sources.size());
    bool Agreement = true;
    for (uint32_t Bounces : {1u, 4u})
    {
        std::array<DirectGpuMoments, 3> Cpu;
        std::array<double, 3> Maximum{}, Tail{};
        constexpr uint32_t CpuBatches = 64, Samples = 64;
        for (uint32_t Batch = 0; Batch < CpuBatches; ++Batch)
        {
            std::array<double, 3> Sum{};
            for (uint32_t Pixel : Pixels)
                for (uint32_t Method = 0; Method < 3; ++Method)
                {
                    std::mt19937_64 Random(0x2ca971b5ull + Pixel * 0x9e3779b97f4a7c15ull +
                        uint64_t(Batch) * 0x1000193ull + Bounces * 0x19b5ull + Method * 0x317f3ull);
                    for (uint32_t Sample = 0; Sample < Samples; ++Sample)
                    {
                        const double Red = Method == 0 ? ReplayReferencePath(Scene, Guides[Pixel], Bounces, Random)[0]
                            : ReplayConvergencePath(Facets, Sources, Guides[Pixel], Bounces, Random, Method == 2);
                        Require(std::isfinite(Red) && Red >= 0, "CPU convergence path is invalid");
                        Sum[Method] += Red;
                        Maximum[Method] = std::max(Maximum[Method], Red);
                        if (Red > 32) Tail[Method] += Red;
                    }
                }
            for (uint32_t Method = 0; Method < 3; ++Method)
            {
                const double Mean = Sum[Method] / (Pixels.size() * Samples);
                Cpu[Method].Accumulate(Mean);
                std::printf("CONVERGENCE_CPU_BATCH bounces=%u batch=%u method=%s mean=%.12g\n", Bounces, Batch,
                            Method == 0 ? "bsdf" : Method == 1 ? "nee" : "power", Mean);
            }
            if ((Batch + 1) % 16 == 0) std::fflush(stdout);
        }
        const double CpuCorrection = std::sqrt(double(CpuBatches) / (CpuBatches - 1));
        for (uint32_t Method = 0; Method < 3; ++Method)
            std::printf("CONVERGENCE_CPU bounces=%u method=%s paths=%zu mean=%.9g nominal_se=%.9g max_path=%.9g "
                        "tail_gt32_mean=%.9g\n", Bounces, Method == 0 ? "bsdf" : Method == 1 ? "nee" : "power",
                        Pixels.size() * Samples * CpuBatches, Cpu[Method].Mean(),
                        Cpu[Method].StandardError() * CpuCorrection, Maximum[Method],
                        Tail[Method] / (Pixels.size() * Samples * CpuBatches));
        std::array<DirectGpuMoments, 2> Gpu;
        std::mt19937_64 Seeds(0x94cde17bull + 0x1000193ull);
        constexpr uint32_t GpuBatches = 256;
        for (uint32_t Batch = 0; Batch < GpuBatches; ++Batch)
        {
            // 📝 Preserve the previous experiment's measured seed subsequence, including eight skipped warm seeds.
            for (uint32_t Warm = 0; Warm < 8; ++Warm) (void)Seeds();
            const uint32_t Seed = uint32_t(Seeds());
            std::array<uint32_t, 24> PreviousCounts{};
            for (uint32_t Method = 0; Method < 2; ++Method)
            {
                const auto Image = Compute.ExecuteReplay(Scene, Bounces, Seed, false, false, true,
                    {.025f, .025f, .025f}, .01f, true, false, false, true, false, false, 64, Method == 1,
                    false, false, false, false, false, false, false, 1, false, false, true, false);
                const auto Counts = Compute.ExtendedStatistics();
                if (Method == 0) PreviousCounts = Counts;
                else Require(Counts[17] == PreviousCounts[17] && Counts[2] == PreviousCounts[2],
                             "Convergence MIS controls changed path-query budget");
                Require(Counts[20] == 0 && Counts[22] == 0 && Counts[23] == 0,
                        "Convergence control unexpectedly consumed history");
                double Sum = 0, MaxPixel = 0;
                for (uint32_t Pixel : Pixels)
                {
                    Require(std::isfinite(Image[Pixel][0]) && Image[Pixel][0] >= 0, "GPU convergence output invalid");
                    Sum += Image[Pixel][0]; MaxPixel = std::max(MaxPixel, double(Image[Pixel][0]));
                }
                const double Mean = Sum / Pixels.size();
                Gpu[Method].Accumulate(Mean);
                std::printf("CONVERGENCE_GPU_BATCH bounces=%u batch=%u seed=%u method=%s mean=%.12g max_pixel=%.9g\n",
                            Bounces, Batch, Seed, Method == 0 ? "nee" : "power", Mean, MaxPixel);
                if (Batch == 0)
                    WriteImage(Directory + "/Room" + std::to_string(Bounces) + (Method == 0 ? "Nee.ppm" : "Power.ppm"),
                               Image, Scene.Width, Scene.Height, 3);
                if (Batch + 1 == 32 || Batch + 1 == 64 || Batch + 1 == 128 || Batch + 1 == GpuBatches)
                    std::printf("CONVERGENCE_GPU bounces=%u batches=%u method=%s mean=%.9g nominal_se=%.9g\n",
                                Bounces, Batch + 1, Method == 0 ? "nee" : "power", Gpu[Method].Mean(),
                                Gpu[Method].StandardError() * std::sqrt(double(Batch + 1) / Batch));
            }
            std::fflush(stdout);
        }
        // 📝 Fixed diagnostic agreement checks, not a revision of the previous 32-batch acceptance decision.
        for (uint32_t Method = 0; Method < 3; ++Method)
        {
            const double Difference = Cpu[Method].Mean() - Cpu[0].Mean();
            const double Error = std::hypot(Cpu[Method].StandardError(), Cpu[0].StandardError()) * CpuCorrection;
            const bool Agrees = std::abs(Difference) < 4 * Error + .001;
            Agreement = Agreement && Agrees;
            std::printf("CONVERGENCE_AGREEMENT bounces=%u domain=cpu method=%s difference=%.9g nominal_se=%.9g gate=%u\n",
                        Bounces, Method == 0 ? "bsdf" : Method == 1 ? "nee" : "power", Difference, Error, unsigned(Agrees));
        }
        for (uint32_t Method = 0; Method < 2; ++Method)
        {
            const double Difference = Gpu[Method].Mean() - Cpu[Method + 1].Mean();
            const double Error = std::hypot(Gpu[Method].StandardError() * std::sqrt(double(GpuBatches) / (GpuBatches - 1)),
                                            Cpu[Method + 1].StandardError() * CpuCorrection);
            const bool Agrees = std::abs(Difference) < 4 * Error + .001;
            Agreement = Agreement && Agrees;
            std::printf("CONVERGENCE_AGREEMENT bounces=%u domain=gpu method=%s difference=%.9g nominal_se=%.9g gate=%u\n",
                        Bounces, Method == 0 ? "nee" : "power", Difference, Error, unsigned(Agrees));
        }
    }
    std::printf("CONVERGENCE_DECISION agreement=%u prior_rejection_preserved=1 promote=0 physical_gpu_timing=0\n",
                unsigned(Agreement));
    Require(Agreement, "Room convergence diagnostic failed fixed agreement gates");
}
}
