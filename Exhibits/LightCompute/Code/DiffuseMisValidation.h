/*============================================================================================================================================
                                                        DIFFUSEMISVALIDATION.H
============================================================================================================================================*/
// 🧩 Independently replay diffuse directional proposals,first-hit area Jacobians,balance MIS and actual query work.

#pragma once

namespace
{
GeometricPrimaryStrike EvaluateMisClosest(const LightComputeArchive &Scene, const TraceVector &Origin,
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

double EvaluateMisRandom(uint32_t Seed, uint32_t Draw)
{
    return double(RgbQuartetHash(Seed ^ (Draw * 0x9e3779b9u)) >> 8) / 16777216.;
}

void ValidateDiffuseMis(const char *Driver, const std::string &Shaders, const std::string &Directory)
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
    uint64_t Checks = 0, Misses = 0, SuffixQueries = 0;
    std::printf("DIFFUSE_MIS_DEVICE name=%s type=%u pixels=1024 samples=32 methods=4 cases=3\n",
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
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x12f7c413u) ^ (Case * 0x6c8e9cf5u));
            const std::array<uint32_t, 2> Identity{Seed, Pixel / Samples};
            std::memcpy(Scene.Receivers[Pixel].PositionHit.data(), Identity.data(), 8);
        }
        Compute.ExecuteDiffuse(Scene, 2, 17, true, false, true);
        const auto *Storage = static_cast<const std::array<float, 4> *>(Compute.Buffers[6].Address);
        double Maximum = 0, MaximumPdf = 0;
        std::array<uint64_t, 4> Queries{}, Nulls{};
        for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
        {
            ++Checks;
            const uint32_t Method = Pixel / Samples;
            const auto Primary = ValidateGeometricPrimary(Scene, Pixel, Storage, Count);
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x12f7c413u) ^ (Case * 0x6c8e9cf5u));
            const bool Bsdf = Method == 2 || (Method == 3 && EvaluateMisRandom(Seed, 7) < .5);
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
                const double First = EvaluateMisRandom(Seed, 2), Angle = 6.283185307179586 * EvaluateMisRandom(Seed, 3);
                for (uint32_t Coordinate = 0; Coordinate < 3; ++Coordinate)
                    ExpectedDirection[Coordinate] = std::sqrt(First) * (std::cos(Angle) * Tangent[Coordinate] +
                                                                        std::sin(Angle) * Lateral[Coordinate]) +
                                                    std::sqrt(1 - First) * Normal[Coordinate];
            }
            else
            {
                const double Choice = EvaluateMisRandom(Seed, 1);
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
                const double Root = std::sqrt(EvaluateMisRandom(Seed, 2));
                Require(std::abs(Wire[1] - (1 - Root)) < 2e-6 &&
                            std::abs(Wire[2] - Root * EvaluateMisRandom(Seed, 3)) < 2e-6,
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
                ProposalQueried ? EvaluateMisClosest(Scene, Origin, Direction) : GeometricPrimaryStrike{};
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
                    SuffixVisible = !TraceBlocked(Scene, SuffixOrigin, Light, 100);
                    const std::array<double, 3> LightRgb{1, double(.7f), double(.4f)};
                    for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    {
                        const double Integrand = Facet.AlbedoRoughness[Channel] * LightRgb[Channel] * LightCosine /
                                                 3.141592653589793 * Primary.AlbedoIor[Channel] * Omega * SourceCosine /
                                                 Squared * double(SuffixVisible);
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
            Require(Work[0] == 1 && Work[1] == unsigned(ProposalQueried) && Work[2] == unsigned(Positive) &&
                        Work[3] == 1 + unsigned(ProposalQueried) + unsigned(Positive),
                    "MIS query accounting differs");
            SuffixQueries += Positive;
            Queries[Method] += uint64_t(Work[3]);
        }
        std::printf("DIFFUSE_MIS_CASE case=%u error=%.12g pdf_error=%.12g queries=%llu,%llu,%llu,%llu "
                    "nulls=%llu,%llu,%llu,%llu\n",
                    Case, Maximum, MaximumPdf, (unsigned long long)Queries[0], (unsigned long long)Queries[1],
                    (unsigned long long)Queries[2], (unsigned long long)Queries[3], (unsigned long long)Nulls[0],
                    (unsigned long long)Nulls[1], (unsigned long long)Nulls[2], (unsigned long long)Nulls[3]);
        std::ofstream Capture(Directory + "/DiffuseMis" + std::to_string(Case) + ".f32", std::ios::binary);
        Capture.exceptions(std::ios::failbit | std::ios::badbit);
        Capture.write(reinterpret_cast<const char *>(Storage), 23 * Count * 16);
        std::array<std::array<std::array<float, 4>, 1024>, 4> Primaries{};
        for (uint32_t Slab = 0; Slab < 4; ++Slab)
            for (uint32_t Pixel = 0; Pixel < 1024; ++Pixel)
                Primaries[Slab][Pixel] = Storage[(19 + Slab) * Count + Pixel];
        std::array<std::array<std::array<float, 4>, 1024>, 2> References{};
        for (uint32_t Bank = 0; Bank < 2; ++Bank)
        {
            std::array<std::array<double, 3>, 1024> Sum{};
            for (uint32_t Batch = 0; Batch < 8; ++Batch)
            {
                for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
                {
                    Scene.Receivers[Pixel] = ConstructGeometricAreaRay(Pixel % 1024);
                    const uint32_t Seed = RgbQuartetHash((Pixel + 0x5e65b9c7u) ^ ((Bank * 8 + Batch) * 0x9e3779b9u) ^
                                                         (Case * 0x6c8e9cf5u));
                    const std::array<uint32_t, 2> Identity{Seed, 0};
                    std::memcpy(Scene.Receivers[Pixel].PositionHit.data(), Identity.data(), 8);
                }
                Compute.ExecuteDiffuse(Scene, 2, 17, true, false, true);
                for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
                {
                    for (uint32_t Slab = 0; Slab < 4; ++Slab)
                        Require(
                            std::memcmp(&Storage[(19 + Slab) * Count + Pixel], &Primaries[Slab][Pixel % 1024], 16) == 0,
                            "MIS reference primary changed with seed/method");
                    for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    {
                        Require(std::isfinite(Storage[Pixel][Channel]), "Nonfinite MIS reference");
                        Sum[Pixel % 1024][Channel] += Storage[Pixel][Channel] / 1024.;
                    }
                }
            }
            for (uint32_t Pixel = 0; Pixel < 1024; ++Pixel)
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    References[Bank][Pixel][Channel] = float(Sum[Pixel][Channel]);
        }
        std::ofstream Reference(Directory + "/DiffuseMisReference" + std::to_string(Case) + ".f32", std::ios::binary);
        Reference.exceptions(std::ios::failbit | std::ios::badbit);
        Reference.write(reinterpret_cast<const char *>(References.data()), 2 * 1024 * 16);
        std::printf("DIFFUSE_MIS_REFERENCE case=%u banks=2 samples_per_bank_pixel=1024\n", Case);
        std::fflush(stdout);
    }
    Require(Ambiguous <= 16 && Checks == 393216, "MIS replay coverage differs");
    std::printf("DIFFUSE_MIS_DECISION correctness=1 checks=%llu nulls=%llu suffix_queries=%llu ambiguous=%u "
                "production_promote=0\n",
                (unsigned long long)Checks, (unsigned long long)Misses, (unsigned long long)SuffixQueries, Ambiguous);
}
} // namespace
