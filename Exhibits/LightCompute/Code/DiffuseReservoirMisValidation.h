/*============================================================================================================================================
                                                        DIFFUSERESERVOIRMISVALIDATION.H
============================================================================================================================================*/
// 🧩 Independently replay diffuse directional proposals,first-hit area Jacobians,balance MIS and actual query work.

#pragma once

namespace
{
GeometricPrimaryStrike EvaluateReservoirMisClosest(const LightComputeArchive &Scene, const TraceVector &Origin,
                                                   const TraceVector &Direction)
{
    GeometricPrimaryStrike Selected;
    for (uint32_t Index = 0; Index < Scene.Surfaces.size(); ++Index)
    {
        const auto &Facet = Scene.Surfaces[Index];
        TraceVector First{}, Second{}, Delta{};
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            First[Axis] = double(Facet.B[Axis]) - Facet.A[Axis];
            Second[Axis] = double(Facet.C[Axis]) - Facet.A[Axis];
            Delta[Axis] = Origin[Axis] - Facet.A[Axis];
        }
        const auto Cross = TraceCross(Direction, Second);
        const double Determinant = TraceDot(First, Cross);
        if (std::abs(Determinant) < 1e-12) continue;
        const double U = TraceDot(Delta, Cross) / Determinant;
        const auto Other = TraceCross(Delta, First);
        const double V = TraceDot(Direction, Other) / Determinant;
        const double Distance = TraceDot(Second, Other) / Determinant;
        if (U < 0 || V < 0 || U + V > 1 || Distance <= .00001 || Distance >= Selected.Distance) continue;
        Selected.Index = Index;
        Selected.Distance = Distance;
        Selected.U = U;
        Selected.V = V;
    }
    return Selected;
}

double EvaluateReservoirMisRandom(uint32_t Seed, uint32_t Draw)
{
    return double(RgbQuartetHash(Seed ^ (Draw * 0x9e3779b9u)) >> 8) / 16777216.;
}

void ValidateDiffuseReservoirMis(const char *Driver, const std::string &Shaders, const std::string &Directory)
{
    constexpr uint32_t Samples = 32768, Count = Samples * 4;
    auto Scene = ConstructGeometricAreaScene(0);
    Scene.Width = 512;
    Scene.Height = 256;
    Scene.Receivers.resize(Count, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::filesystem::create_directories(Directory);
    uint32_t Ambiguous = 0;
    uint64_t Checks = 0, Misses = 0, SuffixQueries = 0, ReservoirChecks = 0;
    uint32_t SelectionAmbiguous = 0;
    std::printf("DIFFUSE_RESERVOIR_MIS_DEVICE name=%s type=%u pixels=1024 samples=32 methods=4 cases=3\n",
                Compute.Properties.deviceName, unsigned(Compute.Properties.deviceType));
    for (uint32_t Case = 0; Case < 3; ++Case)
    {
        Scene = ConstructGeometricAreaScene(Case);
        Scene.Width = 512;
        Scene.Height = 256;
        Scene.Receivers.resize(Count, Scene.Receivers.front());
        std::vector<double> Areas;
        double Total = 0;
        for (const auto &Facet : Scene.Surfaces)
        {
            Areas.push_back(ContinuousFacetArea(Facet));
            Total += Areas.back();
        }
        for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
        {
            Scene.Receivers[Pixel] = ConstructGeometricAreaRay(Pixel % 1024);
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x3b4a51d7u) ^ (Case * 0x6c8e9cf5u));
            const std::array<uint32_t, 2> Identity{Seed, Pixel / Samples};
            std::memcpy(Scene.Receivers[Pixel].PositionHit.data(), Identity.data(), 8);
        }
        Compute.ExecuteDiffuse(Scene, 2, 17, true, false, true);
        const auto *Storage = static_cast<const std::array<float, 4> *>(Compute.Buffers[6].Address);
        double Maximum = 0, MaximumPdf = 0;
        struct ReservoirProbe
        {
            std::array<double, 3> Estimate{};
            TraceVector SuffixOrigin{};
            double Density = 0;
            bool Positive = false, Queried = false;
        };
        std::vector<ReservoirProbe> Probes(Count);
        std::array<uint64_t, 4> Queries{}, Nulls{};
        for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
        {
            ++Checks;
            const uint32_t Method = Pixel / Samples;
            const auto Primary = ValidateGeometricPrimary(Scene, Pixel, Storage, Count);
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x3b4a51d7u) ^ (Case * 0x6c8e9cf5u));
            const bool Bsdf = Method == 2 || (Method == 3 && EvaluateReservoirMisRandom(Seed, 7) < .5);
            const auto Pdf = Storage[Count + Pixel], Wire = Storage[2 * Count + Pixel],
                       Ray = Storage[3 * Count + Pixel];
            const auto SourceWire = Storage[4 * Count + Pixel], Extra = Storage[5 * Count + Pixel];
            const auto OriginWire = Storage[7 * Count + Pixel];
            TraceVector Origin{OriginWire[0], OriginWire[1], OriginWire[2]};
            TraceVector Normal{Primary.NormalRoughness[0], Primary.NormalRoughness[1], Primary.NormalRoughness[2]};
            TraceVector Direction{Ray[0], Ray[1], Ray[2]}, ExpectedDirection{};
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
                Require(std::isfinite(Origin[Axis]) &&
                            std::abs(Origin[Axis] - (Primary.ViewMetallic[Axis] + Normal[Axis] * .002f)) < 2e-5,
                        "MIS offset receiver origin differs");
            uint32_t Index, Nearest;
            std::memcpy(&Index, &Wire[0], 4);
            std::memcpy(&Nearest, &SourceWire[3], 4);
            Require(Pdf[3] == unsigned(Bsdf), "MIS technique random domain differs");
            if (Bsdf)
            {
                const TraceVector Axis = std::abs(Normal[2]) > .999 ? TraceVector{0, 1, 0} : TraceVector{0, 0, 1};
                const auto Tangent = TraceNormalize(TraceCross(Axis, Normal)), Lateral = TraceCross(Normal, Tangent);
                const double First = EvaluateReservoirMisRandom(Seed, 2),
                             Angle = 6.283185307179586 * EvaluateReservoirMisRandom(Seed, 3);
                for (uint32_t Coordinate = 0; Coordinate < 3; ++Coordinate)
                    ExpectedDirection[Coordinate] = std::sqrt(First) * (std::cos(Angle) * Tangent[Coordinate] +
                                                                        std::sin(Angle) * Lateral[Coordinate]) +
                                                    std::sqrt(1 - First) * Normal[Coordinate];
            }
            else
            {
                const double Choice = EvaluateReservoirMisRandom(Seed, 1);
                uint32_t Expected = std::min(uint32_t(Choice * Areas.size()), uint32_t(Areas.size() - 1));
                double Lower = 0, Upper = 0;
                if (Method != 0)
                    for (uint32_t Facet = 0; Facet < Areas.size(); ++Facet)
                    {
                        Upper = Lower + Areas[Facet] / Total;
                        if (Choice < Upper || Facet == Areas.size() - 1)
                        {
                            Expected = Facet;
                            break;
                        }
                        Lower = Upper;
                    }
                Require(Index < Areas.size(), "Area MIS source index out of bounds");
                if (Index != Expected)
                {
                    Require(Method != 0 && std::min(std::abs(Choice - Lower), std::abs(Choice - Upper)) < 2e-6,
                            "MIS area CDF differs");
                    ++Ambiguous;
                }
                const double Root = std::sqrt(EvaluateReservoirMisRandom(Seed, 2));
                Require(std::abs(Wire[1] - (1 - Root)) < 2e-6 &&
                            std::abs(Wire[2] - Root * EvaluateReservoirMisRandom(Seed, 3)) < 2e-6,
                        "MIS area barycentric RNG differs");
                const auto &Facet = Scene.Surfaces[Index];
                for (uint32_t Coordinate = 0; Coordinate < 3; ++Coordinate)
                    ExpectedDirection[Coordinate] =
                        Facet.A[Coordinate] + (double(Facet.B[Coordinate]) - Facet.A[Coordinate]) * Wire[1] +
                        (double(Facet.C[Coordinate]) - Facet.A[Coordinate]) * Wire[2] - Origin[Coordinate];
                ExpectedDirection = TraceNormalize(ExpectedDirection);
            }
            if (!Bsdf && TraceDot(Normal, ExpectedDirection) <= 0) ExpectedDirection = {0, 0, 0};
            for (uint32_t Axis = 0; Axis < 3; ++Axis)
            {
                if (!std::isfinite(Direction[Axis]) || std::abs(Direction[Axis] - ExpectedDirection[Axis]) >= 2e-5)
                    std::fprintf(
                        stderr,
                        "MIS_DIRECTION case=%u pixel=%u method=%u source=%u axis=%u gpu=%.12g cpu=%.12g hemi=%.12g\n",
                        Case, Pixel, Method, Index, Axis, Direction[Axis], ExpectedDirection[Axis],
                        TraceDot(Normal, Direction));
                Require(std::isfinite(Direction[Axis]) && std::abs(Direction[Axis] - ExpectedDirection[Axis]) < 2e-5,
                        "MIS proposal direction differs from independent replay");
            }
            const bool ProposalQueried = Bsdf || TraceDot(Normal, Direction) > 0;
            const auto Closest =
                ProposalQueried ? EvaluateReservoirMisClosest(Scene, Origin, Direction) : GeometricPrimaryStrike{};
            if (Nearest != Closest.Index ||
                std::abs(Ray[3] - Closest.Distance) / std::max(1., Closest.Distance) >= 2e-5)
                std::fprintf(stderr,
                             "MIS_CLOSEST case=%u pixel=%u method=%u source=%u gpu=%u cpu=%u t_gpu=%.12g t_cpu=%.12g "
                             "hemi=%.12g\n",
                             Case, Pixel, Method, Index, Nearest, Closest.Index, double(Ray[3]), Closest.Distance,
                             TraceDot(Normal, Direction));
            Require(Nearest == Closest.Index &&
                        std::abs(Ray[3] - Closest.Distance) / std::max(1., Closest.Distance) < 2e-5,
                    "MIS first hit differs from double all-triangle rays");
            if (Bsdf)
                Require(Index == Nearest && (Index == UINT32_MAX || (std::abs(Wire[1] - Closest.U) < 2e-5 &&
                                                                     std::abs(Wire[2] - Closest.V) < 2e-5)),
                        "MIS BSDF source barycentrics differ");
            double DensityA = 0, DensityB = 0, Density = 0, WrongDensity = 0, Omega = 0;
            std::array<double, 3> Estimate{}, Negative{};
            bool Visible = false, Positive = false, SuffixVisible = false;
            double LightCosine = 0;
            if (Index != UINT32_MAX)
            {
                const auto &Facet = Scene.Surfaces[Index];
                TraceVector Point{}, SourceNormal{}, Delta{};
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                {
                    Point[Axis] = Facet.A[Axis] + (double(Facet.B[Axis]) - Facet.A[Axis]) * Wire[1] +
                                  (double(Facet.C[Axis]) - Facet.A[Axis]) * Wire[2];
                    SourceNormal[Axis] = Facet.NormalA[Axis];
                    Delta[Axis] = Point[Axis] - Origin[Axis];
                }
                SourceNormal = TraceNormalize(SourceNormal);
                for (uint32_t Axis = 0; Axis < 3; ++Axis)
                    Require(std::abs(SourceWire[Axis] - SourceNormal[Axis]) < 2e-5, "MIS source normal differs");
                const double Squared = TraceDot(Delta, Delta), Distance = std::sqrt(Squared);
                const auto Incoming = TraceNormalize(Delta);
                const TraceVector Reverse{-Incoming[0], -Incoming[1], -Incoming[2]};
                Visible = Bsdf || (ProposalQueried && TraceDot(Normal, Incoming) > 0 && Nearest == Index &&
                                   std::abs(Closest.Distance - Distance) <= 2e-5 * std::max(1., Distance));
                Omega = Visible ? std::max(0., TraceDot(Normal, Incoming)) / 3.141592653589793 : 0;
                const double SourceCosine = TraceDot(SourceNormal, Reverse);
                DensityA = Method == 0 ? 1. / Areas.size() / Areas[Index] : 1. / Total;
                DensityB = Omega * std::abs(SourceCosine) / Squared;
                Density = Method < 2 ? DensityA : Method == 2 ? DensityB : .5 * (DensityA + DensityB);
                WrongDensity = Method < 2 ? DensityA : Method == 2 ? Omega : .5 * (DensityA + Omega);
                const TraceVector Light{double(.8f), 0, double(.6f)};
                LightCosine = std::max(0., TraceDot(SourceNormal, Light));
                Positive = Visible && Omega > 0 && SourceCosine > 0 && LightCosine > 0;
                if (Positive)
                {
                    TraceVector SuffixOrigin{};
                    for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        SuffixOrigin[Axis] = Point[Axis] + SourceNormal[Axis] * .002f;
                    Probes[Pixel].SuffixOrigin = SuffixOrigin;
                    const std::array<double, 3> LightRgb{1, double(.7f), double(.4f)};
                    for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    {
                        const double Integrand = Facet.AlbedoRoughness[Channel] * LightRgb[Channel] * LightCosine /
                                                 3.141592653589793 * Primary.AlbedoIor[Channel] * Omega * SourceCosine /
                                                 Squared;
                        Estimate[Channel] = Integrand / Density;
                        Negative[Channel] = Integrand / WrongDensity;
                    }
                }
            }
            else
            {
                ++Misses;
                ++Nulls[Method];
                Require(Bsdf && Wire[1] == 0 && Wire[2] == 0, "Noncanonical null MIS proposal");
            }
            if (Wire[3] != unsigned(Visible) || Extra[2] != unsigned(SuffixVisible) || Extra[3] != unsigned(Positive))
                std::fprintf(
                    stderr,
                    "MIS_VISIBILITY case=%u pixel=%u method=%u source=%u gpu=%g,%g,%g cpu=%u,%u,%u uv=%.12g,%.12g\n",
                    Case, Pixel, Method, Index, double(Wire[3]), double(Extra[2]), double(Extra[3]), unsigned(Visible),
                    unsigned(SuffixVisible), unsigned(Positive), double(Wire[1]), double(Wire[2]));
            Require(Wire[3] == unsigned(Visible) && Extra[2] == unsigned(SuffixVisible) &&
                        Extra[3] == unsigned(Positive),
                    "MIS visibility or suffix eligibility differs");
            for (const auto &Pair : std::array<std::array<double, 2>, 6>{{{Pdf[0], DensityA},
                                                                          {Pdf[1], DensityB},
                                                                          {Pdf[2], Density},
                                                                          {Extra[0], Omega},
                                                                          {Extra[1], WrongDensity},
                                                                          {OriginWire[3], LightCosine}}})
            {
                const double Error = std::abs(Pair[0] - Pair[1]) / std::max(1., std::abs(Pair[1]));
                Require(std::isfinite(Pair[0]) && Error < 2e-5, "MIS density/Jacobian differs");
                MaximumPdf = std::max(MaximumPdf, Error);
            }
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
                for (const auto &Pair :
                     std::array<std::array<double, 2>, 2>{{{Storage[Pixel][Channel], Estimate[Channel]},
                                                           {Storage[6 * Count + Pixel][Channel], Negative[Channel]}}})
                {
                    const double Error = std::abs(Pair[0] - Pair[1]) / std::max(1., std::abs(Pair[1]));
                    Require(std::isfinite(Pair[0]) && Error < 2e-5, "MIS RGB or negative control differs");
                    Maximum = std::max(Maximum, Error);
                }
            const auto Work = Storage[18 * Count + Pixel];
            Require(Work[0] == 1 && Work[1] == unsigned(ProposalQueried) && Work[2] == 0 &&
                        Work[3] == 1 + unsigned(ProposalQueried),
                    "MIS query accounting differs");
            Probes[Pixel].Estimate = Estimate;
            Probes[Pixel].Density = Density;
            Probes[Pixel].Positive = Positive;
            Probes[Pixel].Queried = ProposalQueried;
            Queries[Method] += uint64_t(Work[3]);
        }
        std::printf("DIFFUSE_RESERVOIR_MIS_CASE case=%u error=%.12g pdf_error=%.12g queries=%llu,%llu,%llu,%llu "
                    "nulls=%llu,%llu,%llu,%llu\n",
                    Case, Maximum, MaximumPdf, (unsigned long long)Queries[0], (unsigned long long)Queries[1],
                    (unsigned long long)Queries[2], (unsigned long long)Queries[3], (unsigned long long)Nulls[0],
                    (unsigned long long)Nulls[1], (unsigned long long)Nulls[2], (unsigned long long)Nulls[3]);
        for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
        {
            if (Pixel % Samples >= 16384) continue;
            ++ReservoirChecks;
            const uint32_t Second = Pixel + 16384;
            const auto &First = Probes[Pixel];
            const auto &Other = Probes[Second];
            const double WeightFirst = (First.Estimate[0] + First.Estimate[1] + First.Estimate[2]) / 3;
            const double WeightSecond = (Other.Estimate[0] + Other.Estimate[1] + Other.Estimate[2]) / 3;
            const double Sum = WeightFirst + WeightSecond;
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x3b4a51d7u) ^ (Case * 0x6c8e9cf5u));
            const double Draw = EvaluateReservoirMisRandom(Seed, 11);
            uint32_t Selected = UINT32_MAX, GpuSelected;
            const auto Wire = Storage[10 * Count + Pixel], Weights = Storage[11 * Count + Pixel];
            std::memcpy(&GpuSelected, &Wire[0], 4);
            if (Sum > 0) Selected = Draw * Sum < WeightFirst ? Pixel : Second;
            if (GpuSelected != Selected)
            {
                Require(Sum > 0 && (GpuSelected == Pixel || GpuSelected == Second) &&
                            std::abs(Draw - WeightFirst / Sum) < 2e-6,
                        "Reservoir selector differs");
                ++SelectionAmbiguous;
                Selected = GpuSelected;
            }
            bool VisibleFirst = false, VisibleSecond = false, SelectedVisible = false;
            const TraceVector Light{double(.8f), 0, double(.6f)};
            if (First.Positive) VisibleFirst = !TraceBlocked(Scene, First.SuffixOrigin, Light, 100);
            if (Other.Positive) VisibleSecond = !TraceBlocked(Scene, Other.SuffixOrigin, Light, 100);
            double Target = 0, Normalizer = 0;
            std::array<double, 3> Estimate{}, Raw{}, Wrong{};
            if (Selected != UINT32_MAX)
            {
                const auto &Chosen = Probes[Selected];
                Target = (Selected == Pixel ? WeightFirst : WeightSecond) * Chosen.Density;
                Normalizer = Sum / (2 * Target);
                SelectedVisible = Selected == Pixel ? VisibleFirst : VisibleSecond;
                for (uint32_t C = 0; C < 3; ++C)
                {
                    Estimate[C] = Chosen.Estimate[C] * Chosen.Density * Normalizer * unsigned(SelectedVisible);
                    Wrong[C] = Estimate[C] * 2 / (unsigned(WeightFirst > 0) + unsigned(WeightSecond > 0));
                }
            }
            for (uint32_t C = 0; C < 3; ++C)
                Raw[C] = (First.Estimate[C] * unsigned(VisibleFirst) + Other.Estimate[C] * unsigned(VisibleSecond)) / 2;
            uint32_t Visibility;
            std::memcpy(&Visibility, &Weights[3], 4);
            Require(Visibility == (unsigned(VisibleFirst) | (unsigned(VisibleSecond) << 1)) &&
                        Wire[3] == unsigned(SelectedVisible),
                    "Reservoir suffix visibility differs");
            for (const auto &Pair : std::array<std::array<double, 2>, 5>{{{Wire[1], Sum},
                                                                          {Wire[2], Normalizer},
                                                                          {Weights[0], WeightFirst},
                                                                          {Weights[1], WeightSecond},
                                                                          {Weights[2], Target}}})
                Require(std::isfinite(Pair[0]) && std::abs(Pair[0] - Pair[1]) / std::max(1., std::abs(Pair[1])) < 2e-5,
                        "Reservoir weights or normalizer differ");
            for (uint32_t C = 0; C < 3; ++C)
                for (const auto &Pair :
                     std::array<std::array<double, 2>, 3>{{{Storage[8 * Count + Pixel][C], Estimate[C]},
                                                           {Storage[9 * Count + Pixel][C], Raw[C]},
                                                           {Storage[14 * Count + Pixel][C], Wrong[C]}}})
                    Require(std::isfinite(Pair[0]) &&
                                std::abs(Pair[0] - Pair[1]) / std::max(1., std::abs(Pair[1])) < 2e-5,
                            "Reservoir RGB or wrong-M control differs");
            const auto Work = Storage[12 * Count + Pixel], RawWork = Storage[13 * Count + Pixel];
            const unsigned Proposals = unsigned(First.Queried) + unsigned(Other.Queried);
            const unsigned ReservoirSuffix = Selected != UINT32_MAX,
                           RawSuffix = unsigned(First.Positive) + unsigned(Other.Positive);
            Require(Work[0] == 2 && Work[1] == Proposals && Work[2] == ReservoirSuffix &&
                        Work[3] == 2 + Proposals + ReservoirSuffix && RawWork[0] == 2 && RawWork[1] == Proposals &&
                        RawWork[2] == RawSuffix && RawWork[3] == 2 + Proposals + RawSuffix,
                    "Reservoir actual query counts differ");
            SuffixQueries += ReservoirSuffix + RawSuffix;
        }
        std::ofstream Capture(Directory + "/DiffuseReservoirMis" + std::to_string(Case) + ".f32", std::ios::binary);
        Capture.exceptions(std::ios::failbit | std::ios::badbit);
        Capture.write(reinterpret_cast<const char *>(Storage), 23 * Count * 16);
        std::fflush(stdout);
    }
    Require(Ambiguous <= 16 && SelectionAmbiguous <= 16 && ReservoirChecks == 196608 && Checks == 393216,
            "MIS replay coverage differs");
    std::printf("DIFFUSE_RESERVOIR_MIS_DECISION correctness=1 checks=%llu nulls=%llu suffix_queries=%llu ambiguous=%u "
                "reservoirs=%llu selection_ambiguous=%u production_promote=0\n",
                (unsigned long long)Checks, (unsigned long long)Misses, (unsigned long long)SuffixQueries, Ambiguous,
                (unsigned long long)ReservoirChecks, SelectionAmbiguous);
}
} // namespace
