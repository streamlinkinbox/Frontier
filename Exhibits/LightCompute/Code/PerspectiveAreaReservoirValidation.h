/*============================================================================================================================================
                                                    PERSPECTIVEAREARESERVOIRVALIDATION.H
============================================================================================================================================*/
// 🧩 Validate shared-BVH primary receivers,scene-consistent shadows and fresh RIS against double triangle rays.

#pragma once

namespace
{
LightComputeReceiver ConstructPerspectiveAreaRay(uint32_t Pixel, uint32_t Case)
{
    const std::array<TraceVector, 3> Eyes{{{-.3, 0, 1.2}, {-.6, .15, 1}, {-.4, -.25, 1.3}}};
    const double U = (double(Pixel % 32) + .5) / 32, V = (double(Pixel / 32) + .5) / 32;
    const double Centre = Case == 2 ? .25 : -.35, Half = Case == 2 ? 1.25 : 1;
    const auto &Eye = Eyes[Case];
    const TraceVector Plane{Centre + Half * (2 * U - 1), 1.2 * (2 * V - 1), 4};
    LightComputeReceiver Ray{};
    TraceVector Delta{};
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        Ray.ViewMetallic[Axis] = float(Eye[Axis]);
        Delta[Axis] = Plane[Axis] - Eye[Axis];
    }
    const auto Direction = TraceNormalize(Delta);
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Ray.NormalRoughness[Axis] = float(Direction[Axis]);
    return Ray;
}

GeometricPrimaryStrike EvaluatePerspectivePrimary(const LightComputeArchive &Scene, uint32_t Pixel, uint32_t Case)
{
    const auto Ray = ConstructPerspectiveAreaRay(Pixel, Case);
    const TraceVector Origin{Ray.ViewMetallic[0], Ray.ViewMetallic[1], Ray.ViewMetallic[2]},
        Direction{Ray.NormalRoughness[0], Ray.NormalRoughness[1], Ray.NormalRoughness[2]};
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
    Require(Selected.Index != UINT32_MAX, "Geometric primary missed receiver");
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Selected.Position[Axis] = Origin[Axis] + Direction[Axis] * Selected.Distance;
    const auto &Facet = Scene.Surfaces[Selected.Index];
    Selected.Normal = TraceNormalize({Facet.NormalA[0], Facet.NormalA[1], Facet.NormalA[2]});
    if (TraceDot(Selected.Normal, Direction) > 0)
        for (auto &Coordinate : Selected.Normal)
            Coordinate = -Coordinate;
    return Selected;
}

LightComputeReceiver ValidatePerspectivePrimary(const LightComputeArchive &Scene, uint32_t Pixel,
                                                const std::array<float, 4> *Storage, uint32_t Count, uint32_t Case)
{
    const auto Expected = EvaluatePerspectivePrimary(Scene, Pixel % 1024, Case);
    const auto Wire = Storage[19 * Count + Pixel];
    uint32_t Index;
    std::memcpy(&Index, &Wire[0], 4);
    Require(Index < Scene.Surfaces.size(), "GPU primary missed receiver");
    const auto &Facet = Scene.Surfaces[Index];
    const auto &Closest = Scene.Surfaces[Expected.Index];
    Require(std::isfinite(Wire[1]) && std::abs(Wire[1] - Expected.Distance) < 2e-5,
            "Primary closest distance differs from double rays");
    Require(Wire[2] >= -2e-6f && Wire[3] >= -2e-6f && Wire[2] + Wire[3] <= 1 + 2e-6f,
            "Primary barycentrics outside facet");
    if (Index != Expected.Index)
    {
        const double ExpectedMargin = std::min({Expected.U, Expected.V, 1 - Expected.U - Expected.V});
        const double WireMargin = std::min({double(Wire[2]), double(Wire[3]), 1. - Wire[2] - Wire[3]});
        Require(std::abs(Wire[1] - Expected.Distance) < 2e-6 && ExpectedMargin < 2e-6 && WireMargin < 2e-6,
                "Primary facet identity differs away from a shared edge");
    }
    LightComputeReceiver Surface{};
    Surface.ViewMetallic = Storage[20 * Count + Pixel];
    Surface.NormalRoughness = Storage[21 * Count + Pixel];
    Surface.AlbedoIor = Storage[22 * Count + Pixel];
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        const double Point = Facet.A[Axis] + (double(Facet.B[Axis]) - Facet.A[Axis]) * Wire[2] +
                             (double(Facet.C[Axis]) - Facet.A[Axis]) * Wire[3];
        Require(std::isfinite(Surface.ViewMetallic[Axis]) &&
                    std::abs(Surface.ViewMetallic[Axis] - Expected.Position[Axis]) < 2e-5 &&
                    std::abs(Point - Expected.Position[Axis]) < 2e-5,
                "Primary position/barycentrics differ");
        Require(std::isfinite(Surface.NormalRoughness[Axis]) &&
                    std::abs(Surface.NormalRoughness[Axis] - Expected.Normal[Axis]) < 2e-5,
                "Primary geometric normal differs");
        Require(Surface.AlbedoIor[Axis] == Facet.AlbedoRoughness[Axis] &&
                    Facet.AlbedoRoughness[Axis] == Closest.AlbedoRoughness[Axis],
                "Primary material or tie differs");
    }
    Require(Surface.ViewMetallic[3] == 1 && Surface.NormalRoughness[3] == 0 && Surface.AlbedoIor[3] == 0,
            "Primary telemetry padding differs");
    return Surface;
}

void ValidatePerspectiveAreaReservoir(const char *Driver, const std::string &Shaders, const std::string &Directory)
{
    constexpr uint32_t Samples = 4096, Count = Samples * 6;
    auto Scene = ConstructGeometricAreaScene(0);
    Scene.Width = 192;
    Scene.Height = 128;
    Scene.Receivers.assign(Count, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true);
    std::filesystem::create_directories(Directory);
    uint32_t Ambiguous = 0;
    uint64_t SelectedChecks = 0, CandidateChecks = 0, ReceiverShadows = 0;
    uint32_t PerspectiveChanged = 0, WallPrimaries = 0;
    std::printf("PERSPECTIVE_AREA_RESERVOIR_DEVICE name=%s type=%u pixels=1024 packets=4 methods=6 cases=3\n",
                Compute.Properties.deviceName, unsigned(Compute.Properties.deviceType));
    for (uint32_t Case = 0; Case < 3; ++Case)
    {
        Scene = ConstructGeometricAreaScene(Case);
        Scene.Width = 192;
        Scene.Height = 128;
        Scene.Receivers.resize(Count, Scene.Receivers.front());
        for (uint32_t Pixel = 0; Pixel < 1024; ++Pixel)
        {
            const auto Perspective = EvaluatePerspectivePrimary(Scene, Pixel, Case);
            const auto Orthographic = EvaluateGeometricPrimary(Scene, Pixel);
            if (Perspective.Index != Orthographic.Index ||
                std::abs(Perspective.Position[0] - Orthographic.Position[0]) > 1e-4 ||
                std::abs(Perspective.Position[1] - Orthographic.Position[1]) > 1e-4)
                ++PerspectiveChanged;
            if (std::abs(Perspective.Normal[2]) < .5) ++WallPrimaries;
        }
        auto WithoutReceiver = Scene;
        WithoutReceiver.Surfaces.erase(std::remove_if(WithoutReceiver.Surfaces.begin(), WithoutReceiver.Surfaces.end(),
                                                      [](const auto &Facet) { return Facet.NormalA[2] < -.5f; }),
                                       WithoutReceiver.Surfaces.end());
        Require(WithoutReceiver.Surfaces.size() == 10, "Receiver visibility control facet count differs");
        std::vector<double> Areas;
        double TotalArea = 0;
        for (const auto &Facet : Scene.Surfaces)
        {
            Areas.push_back(ContinuousFacetArea(Facet));
            TotalArea += Areas.back();
        }
        const uint32_t FacetCount = uint32_t(Areas.size());
        for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
        {
            const auto ReceiverSurface = ConstructPerspectiveAreaRay(Pixel % 1024, Case);
            const TraceVector Receiver{ReceiverSurface.ViewMetallic[0], ReceiverSurface.ViewMetallic[1],
                                       ReceiverSurface.ViewMetallic[2]};
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x4a615e23u) ^ (Case * 0x6c8e9cf5u));
            Scene.Receivers[Pixel] = ReceiverSurface;
            const std::array<uint32_t, 2> Identity{Seed, Pixel / Samples};
            std::memcpy(Scene.Receivers[Pixel].PositionHit.data(), Identity.data(), 8);
            Scene.Receivers[Pixel].ViewMetallic = {float(Receiver[0]), float(Receiver[1]), float(Receiver[2]), 0};
        }
        Compute.ExecuteDiffuse(Scene, 2, 17, true, false, true);
        const auto *Storage = static_cast<const std::array<float, 4> *>(Compute.Buffers[6].Address);
        double Maximum = 0, MaximumWeight = 0;
        for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
        {
            const uint32_t Method = Pixel / Samples;
            const uint32_t ProposalMeasure = Method % 2;
            const auto ReceiverSurface = ValidatePerspectivePrimary(Scene, Pixel, Storage, Count, Case);
            const TraceVector Receiver{ReceiverSurface.ViewMetallic[0], ReceiverSurface.ViewMetallic[1],
                                       ReceiverSurface.ViewMetallic[2]};
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x4a615e23u) ^ (Case * 0x6c8e9cf5u));
            std::array<double, 3> Estimate{}, Negative{};
            for (uint32_t Lane = 0; Lane < 4; ++Lane)
            {
                std::array<uint32_t, 2> Packed{}, Indices{};
                std::array<double, 2> Density{}, Score{};
                std::array<AreaTransport, 2> Transport{};
                const auto Contribution = Storage[(4 + 4 * Lane) * Count + Pixel];
                const auto Telemetry = Storage[(5 + 4 * Lane) * Count + Pixel];
                const uint32_t Proposals = Method < 2 || Method >= 4 ? 2 : 1;
                for (uint32_t Proposal = 0; Proposal < 2; ++Proposal)
                {
                    const auto Wire = Storage[(2 + 4 * Lane + Proposal) * Count + Pixel];
                    std::memcpy(&Packed[Proposal], &Wire[0], 4);
                    if (Proposal >= Proposals)
                    {
                        Require(Packed[Proposal] == UINT32_MAX && Wire[1] == 0 && Wire[2] == 0 && Wire[3] == 0,
                                "Noncanonical unused raw proposal");
                        continue;
                    }
                    ++CandidateChecks;
                    const uint32_t Draw = 2 * Lane + Proposal;
                    const double Choice = double(RgbQuartetHash(Seed ^ ((3 * Draw + 1) * 0x9e3779b9u)) >> 8) / 16777216;
                    const double First = double(RgbQuartetHash(Seed ^ ((3 * Draw + 2) * 0x9e3779b9u)) >> 8) / 16777216;
                    const double Second = double(RgbQuartetHash(Seed ^ ((3 * Draw + 3) * 0x9e3779b9u)) >> 8) / 16777216;
                    Require(std::abs(Wire[1] - (1 - std::sqrt(First))) < 2e-6 &&
                                std::abs(Wire[2] - std::sqrt(First) * Second) < 2e-6,
                            "Area reservoir barycentric draw differs");
                    uint32_t Expected = std::min(uint32_t(Choice * FacetCount), FacetCount - 1);
                    double Lower = 0, Upper = 0;
                    if (ProposalMeasure == 1)
                    {
                        for (uint32_t Index = 0; Index < FacetCount; ++Index)
                        {
                            Upper = Lower + Areas[Index] / TotalArea;
                            if (Choice < Upper || Index == FacetCount - 1)
                            {
                                Expected = Index;
                                break;
                            }
                            Lower = Upper;
                        }
                    }
                    Indices[Proposal] = Packed[Proposal] & 0xffffu;
                    Require(Indices[Proposal] < FacetCount, "Foreign continuous RIS proposal");
                    if (Indices[Proposal] != Expected)
                    {
                        Require(ProposalMeasure == 1 &&
                                    std::min(std::abs(Choice - Lower), std::abs(Choice - Upper)) < 2e-6,
                                "Continuous RIS facet selector differs");
                        ++Ambiguous;
                    }
                    const uint32_t Index = Indices[Proposal];
                    Density[Proposal] = ProposalMeasure == 0 ? 1. / FacetCount / Areas[Index] : 1. / TotalArea;
                    Score[Proposal] =
                        EvaluateCameraAreaProxy(Scene, Index, Wire[1], Wire[2], Receiver, ReceiverSurface);
                    Transport[Proposal] =
                        EvaluateGeometricAreaTransport(Scene, Index, Wire[1], Wire[2], Receiver, ReceiverSurface);
                    Require(std::isfinite(Wire[3]) && Wire[3] > 0 &&
                                std::abs(Wire[3] - Density[Proposal]) / std::max(1., Density[Proposal]) < 2e-5,
                            "Continuous RIS proposal density differs");
                    Require(std::isfinite(Telemetry[Proposal]) && Telemetry[Proposal] > 0 &&
                                std::abs(Telemetry[Proposal] - Score[Proposal]) < 2e-5,
                            "Unoccluded RIS score differs");
                }
                uint32_t Selected = 0;
                double Normalizer = 1 / Density[0], WrongNormalizer = 1;
                if (Method < 2)
                {
                    const double FirstWeight = Score[0] / Density[0], SecondWeight = Score[1] / Density[1];
                    const double Threshold = FirstWeight / (FirstWeight + SecondWeight);
                    const double Uniform =
                        double(RgbQuartetHash(Seed ^ (0x7f4a7c15u + Lane * 0x9e3779b9u)) >> 8) / 16777216;
                    Selected = Uniform < Threshold ? 0 : 1;
                    Require(Telemetry[3] == float(Uniform), "RIS selector random domain differs");
                    if (uint32_t(Telemetry[2]) != Selected)
                    {
                        Require(std::abs(Uniform - Threshold) < 2e-6, "RIS weight selector differs");
                        ++Ambiguous;
                        Selected = uint32_t(Telemetry[2]);
                    }
                    Require(Selected < 2, "Foreign RIS selected ordinal");
                    Normalizer = (FirstWeight + SecondWeight) / (2 * Score[Selected]);
                    WrongNormalizer = (Score[0] + Score[1]) / (2 * Score[Selected]);
                }
                else Require(Telemetry[2] == 0 && Telemetry[3] == 0, "Raw path performed RIS selection");
                for (uint32_t Proposal = 0; Proposal < Proposals; ++Proposal)
                {
                    const bool Traced = Method >= 2 || Proposal == Selected;
                    Require(((Packed[Proposal] >> 18) & 1u) == unsigned(Traced), "Unselected proposal traced");
                    if (!Traced)
                    {
                        Require(Packed[Proposal] == Indices[Proposal], "Untraced proposal contains visibility bits");
                        continue;
                    }
                    ++SelectedChecks;
                    {
                        const auto CandidateWire = Storage[(2 + 4 * Lane + Proposal) * Count + Pixel];
                        const auto &Facet = Scene.Surfaces[Indices[Proposal]];
                        TraceVector Point{}, Normal{};
                        for (uint32_t Axis = 0; Axis < 3; ++Axis)
                        {
                            Point[Axis] = Facet.A[Axis] + (double(Facet.B[Axis]) - Facet.A[Axis]) * CandidateWire[1] +
                                          (double(Facet.C[Axis]) - Facet.A[Axis]) * CandidateWire[2];
                            Normal[Axis] = Facet.NormalA[Axis];
                        }
                        Normal = TraceNormalize(Normal);
                        for (uint32_t Axis = 0; Axis < 3; ++Axis)
                            Point[Axis] += Normal[Axis] * .002f;
                        const bool VisibleWithout =
                            !TraceBlocked(WithoutReceiver, Point, {double(.8f), 0, double(.6f)}, 100);
                        ReceiverShadows += VisibleWithout && !Transport[Proposal].SuffixVisible &&
                                           Facet.NormalA[2] > -.5f &&
                                           TraceDot(Normal, {double(.8f), 0, double(.6f)}) > 0;
                    }
                    if (((Packed[Proposal] >> 16) & 1u) != unsigned(Transport[Proposal].SuffixVisible) ||
                        ((Packed[Proposal] >> 17) & 1u) != unsigned(Transport[Proposal].ConnectionVisible))
                        std::fprintf(stderr,
                                     "GEOMETRIC_VISIBILITY case=%u pixel=%u method=%u lane=%u proposal=%u facet=%u "
                                     "gpu=%u,%u cpu=%u,%u uv=%.12g,%.12g\n",
                                     Case, Pixel, Method, Lane, Proposal, Indices[Proposal],
                                     (Packed[Proposal] >> 16) & 1u, (Packed[Proposal] >> 17) & 1u,
                                     unsigned(Transport[Proposal].SuffixVisible),
                                     unsigned(Transport[Proposal].ConnectionVisible),
                                     double(Storage[(2 + 4 * Lane + Proposal) * Count + Pixel][1]),
                                     double(Storage[(2 + 4 * Lane + Proposal) * Count + Pixel][2]));
                    Require(((Packed[Proposal] >> 16) & 1u) == unsigned(Transport[Proposal].SuffixVisible) &&
                                ((Packed[Proposal] >> 17) & 1u) == unsigned(Transport[Proposal].ConnectionVisible),
                            "RIS selected visibility differs from double rays");
                }
                const double WeightReference = Method >= 4 ? 0 : Normalizer;
                const double WeightError = std::abs(Contribution[3] - WeightReference) / std::max(1., WeightReference);
                if (!(std::isfinite(Contribution[3]) && WeightError < 2e-5))
                    std::fprintf(stderr,
                                 "CAMERA_NUMERIC case=%u pixel=%u lane=%u selected=%u weight=%.12g cpu=%.12g "
                                 "error=%.12g scores=%.12g,%.12g cpu_scores=%.12g,%.12g\n",
                                 Case, Pixel, Lane, Selected, double(Contribution[3]), WeightReference, WeightError,
                                 double(Telemetry[0]), double(Telemetry[1]), Score[0], Score[1]);
                Require(std::isfinite(Contribution[3]) && WeightError < 2e-5, "RIS normalizer differs");
                MaximumWeight = std::max(MaximumWeight, WeightError);
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                {
                    double Expected = Transport[Selected].Integrand[Channel] * Normalizer;
                    double Wrong = Transport[Selected].Integrand[Channel] * WrongNormalizer;
                    if (Method >= 4)
                    {
                        Expected = .5 * (Transport[0].Integrand[Channel] / Density[0] +
                                         Transport[1].Integrand[Channel] / Density[1]);
                        Wrong = .5 * (Transport[0].Integrand[Channel] + Transport[1].Integrand[Channel]);
                    }
                    const double Error = std::abs(Contribution[Channel] - Expected) / std::max(1., std::abs(Expected));
                    Require(std::isfinite(Contribution[Channel]) && Error < 2e-5, "RIS selected contribution differs");
                    Maximum = std::max(Maximum, Error);
                    Estimate[Channel] += Expected / 4;
                    Negative[Channel] += Wrong / 4;
                }
            }
            for (uint32_t Channel = 0; Channel < 3; ++Channel)
            {
                for (const auto &Pair :
                     std::array<std::array<double, 2>, 2>{{{Storage[Pixel][Channel], Estimate[Channel]},
                                                           {Storage[Count + Pixel][Channel], Negative[Channel]}}})
                {
                    const double Error = std::abs(Pair[0] - Pair[1]) / std::max(1., std::abs(Pair[1]));
                    Require(std::isfinite(Pair[0]) && Error < 2e-5,
                            "Area reservoir output differs from double reference");
                    Maximum = std::max(Maximum, Error);
                }
            }
            const auto Work = Storage[18 * Count + Pixel];
            Require(Work[0] == float(Method >= 4 ? 8 : 4) && Work[1] == Work[0] &&
                        Work[2] == float(Method < 2 || Method >= 4 ? 8 : 4) && Work[3] == 0,
                    "RIS query/proposal telemetry differs");
        }
        std::printf("PERSPECTIVE_AREA_RESERVOIR_CASE case=%u error=%.12g normalizer_error=%.12g\n", Case, Maximum,
                    MaximumWeight);
        std::ofstream Capture(Directory + "/PerspectiveAreaReservoir" + std::to_string(Case) + ".f32",
                              std::ios::binary);
        Capture.exceptions(std::ios::failbit | std::ios::badbit);
        Capture.write(reinterpret_cast<const char *>(Storage), 23 * Count * 16);
        std::array<std::array<std::array<float, 4>, 1024>, 4> PrimaryRecords{};
        for (uint32_t Slab = 0; Slab < 4; ++Slab)
            for (uint32_t Pixel = 0; Pixel < 1024; ++Pixel)
                PrimaryRecords[Slab][Pixel] = Storage[(19 + Slab) * Count + Pixel];
        // 📝 Disjoint reference domains,uniform raw8 only;24 packets/pixel/dispatch,16 dispatches/bank.
        std::array<std::array<std::array<float, 4>, 1024>, 2> Reference{};
        for (uint32_t Bank = 0; Bank < 2; ++Bank)
        {
            std::array<std::array<double, 3>, 1024> Sum{};
            for (uint32_t Batch = 0; Batch < 16; ++Batch)
            {
                for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
                {
                    Scene.Receivers[Pixel] = ConstructPerspectiveAreaRay(Pixel % 1024, Case);
                    const uint32_t Seed = RgbQuartetHash((Pixel + 0x174b6d39u) ^ ((Bank * 16 + Batch) * 0x9e3779b9u) ^
                                                         (Case * 0x6c8e9cf5u));
                    const std::array<uint32_t, 2> Identity{Seed, 4};
                    std::memcpy(Scene.Receivers[Pixel].PositionHit.data(), Identity.data(), 8);
                }
                Compute.ExecuteDiffuse(Scene, 2, 17, true, false, true);
                for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
                    for (uint32_t Slab = 0; Slab < 4; ++Slab)
                        Require(std::memcmp(&Storage[(19 + Slab) * Count + Pixel], &PrimaryRecords[Slab][Pixel % 1024],
                                            16) == 0,
                                "Reference primary geometry changed with RNG/method");
                for (uint32_t Pixel = 0; Pixel < Count; ++Pixel)
                    for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    {
                        Require(std::isfinite(Storage[Pixel][Channel]), "Nonfinite camera reference");
                        Sum[Pixel % 1024][Channel] += Storage[Pixel][Channel] / 384.;
                    }
            }
            for (uint32_t Pixel = 0; Pixel < 1024; ++Pixel)
                for (uint32_t Channel = 0; Channel < 3; ++Channel)
                    Reference[Bank][Pixel][Channel] = float(Sum[Pixel][Channel]);
        }
        std::ofstream ReferenceCapture(Directory + "/PerspectiveAreaReference" + std::to_string(Case) + ".f32",
                                       std::ios::binary);
        ReferenceCapture.exceptions(std::ios::failbit | std::ios::badbit);
        ReferenceCapture.write(reinterpret_cast<const char *>(Reference.data()), 2 * 1024 * 16);
        std::printf(
            "PERSPECTIVE_AREA_REFERENCE case=%u banks=2 packets_per_bank_pixel=384 samples_per_bank_pixel=3072\n",
            Case);
        std::fflush(stdout);
    }
    Require(PerspectiveChanged > 0 && WallPrimaries > 0, "Perspective primary controls had no effect");
    Require(ReceiverShadows > 0, "Omitted-receiver visibility control had no effect");
    std::printf("PERSPECTIVE_AREA_OCCLUSION receiver_suffix_blocks=%llu primary_checks=73728 "
                "reference_primary_checks=2359296 changed=%u wall_primaries=%u\n",
                (unsigned long long)ReceiverShadows, PerspectiveChanged, WallPrimaries);
    Require(Ambiguous <= 16 && CandidateChecks == 491520 && SelectedChecks == 393216, "RIS replay coverage differs");
    std::printf("PERSPECTIVE_AREA_RESERVOIR_DECISION correctness=1 candidates=%llu traced=%llu ambiguous=%u "
                "production_promote=0\n",
                (unsigned long long)CandidateChecks, (unsigned long long)SelectedChecks, Ambiguous);
}
} // namespace
