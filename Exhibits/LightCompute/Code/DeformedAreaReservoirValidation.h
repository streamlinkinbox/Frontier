/*============================================================================================================================================
                                                    DEFORMEDAREARESERVOIRVALIDATION.H
============================================================================================================================================*/
// 🧩 Validate shared-BVH primary receivers,scene-consistent shadows and fresh RIS against double triangle rays.

#pragma once

namespace
{
LightComputeArchive ConstructDeformedAreaScene(uint32_t Layout)
{
    auto Scene = ConstructGeometricAreaScene(Layout);
    auto Facets = Scene.Surfaces;
    const std::array<std::array<float, 3>, 3> Palette{{{.8f, .08f, .05f}, {.06f, .75f, .12f}, {.08f, .15f, .85f}}};
    std::vector<std::array<SpatialVector, 3>> Triangles;
    for (uint32_t Index = 0; Index < Facets.size(); ++Index)
    {
        auto &Facet = Facets[Index];
        const double CentroidY = (double(Facet.A[1]) + Facet.B[1] + Facet.C[1]) / 3;
        for (auto *Point : {&Facet.A, &Facet.B, &Facet.C})
        {
            const double X = (*Point)[0], Y = (*Point)[1], Z = (*Point)[2];
            if (Layout == 0)
            {
                (*Point)[0] = float(1.05 * X);
                (*Point)[1] = float(.95 * Y);
                (*Point)[2] = float(Z + .12 * std::sin(1.3 * X) * std::cos(.7 * Y));
            }
            else if (Layout == 1)
            {
                (*Point)[0] = float(.95 * X);
                (*Point)[1] = float(1.08 * Y);
                (*Point)[2] = float(Z + .18 * std::sin(.9 * X + 1.1 * Y));
            }
            else
            {
                (*Point)[0] = float(X + .07 * Y);
                (*Point)[1] = float(.95 * Y);
                (*Point)[2] = float(Z + .14 * std::cos(1.2 * X - .8 * Y));
            }
        }
        TraceVector First{}, Second{};
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            First[Axis] = double(Facet.B[Axis]) - Facet.A[Axis];
            Second[Axis] = double(Facet.C[Axis]) - Facet.A[Axis];
        }
        const auto Normal = TraceNormalize(TraceCross(First, Second));
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            Facet.NormalA[Axis] = Facet.NormalB[Axis] = Facet.NormalC[Axis] = float(Normal[Axis]);
            Facet.AlbedoRoughness[Axis] = Layout == 0     ? .55f
                                          : Layout == 1   ? Palette[Index % 3][Axis]
                                          : CentroidY > 0 ? std::array<float, 3>{.9f, .65f, .12f}[Axis]
                                                          : std::array<float, 3>{.04f, .08f, .2f}[Axis];
        }
        Triangles.push_back({SpatialVector{Facet.A[0], Facet.A[1], Facet.A[2]},
                             SpatialVector{Facet.B[0], Facet.B[1], Facet.B[2]},
                             SpatialVector{Facet.C[0], Facet.C[1], Facet.C[2]}});
    }
    TriangleVisibilitySpace Space;
    Space.Build(Triangles);
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.clear();
    for (uint32_t Index : Scene.Visibility.OriginalFacets)
        Scene.Surfaces.push_back(Facets[Index]);
    Require(Scene.Surfaces.size() == 16, "Deformed fixture facet count differs");
    return Scene;
}

AreaTransport EvaluateDeformedRayTransport(const LightComputeArchive &Scene, uint32_t Index, double U, double V,
                                           const TraceVector &Receiver, const LightComputeReceiver &ReceiverSurface,
                                           const std::array<float, 4> &RayOrigin,
                                           const std::array<float, 4> &RayDirection)
{
    const auto &Facet = Scene.Surfaces[Index];
    TraceVector Point{}, Normal{}, Origin{}, Delta{};
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
    {
        Point[Axis] =
            Facet.A[Axis] + (double(Facet.B[Axis]) - Facet.A[Axis]) * U + (double(Facet.C[Axis]) - Facet.A[Axis]) * V;
        Normal[Axis] = Facet.NormalA[Axis] * (1 - U - V) + Facet.NormalB[Axis] * U + Facet.NormalC[Axis] * V;
        Delta[Axis] = Receiver[Axis] - Point[Axis];
    }
    Normal = TraceNormalize(Normal);
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Origin[Axis] = Point[Axis] + Normal[Axis] * .002f;
    AreaTransport Transport;
    Transport.Squared = TraceDot(Delta, Delta);
    const auto Direction = TraceNormalize(Delta);
    Transport.Cosine = TraceDot(Normal, Direction);
    const TraceVector Light{double(.8f), 0, double(.6f)};
    TraceVector ActualOrigin{RayOrigin[0], RayOrigin[1], RayOrigin[2]};
    TraceVector ActualDirection{RayDirection[0], RayDirection[1], RayDirection[2]};
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Require(std::isfinite(ActualOrigin[Axis]) && std::abs(ActualOrigin[Axis] - Origin[Axis]) < 2e-5,
                "Deformed source ray origin differs");
    Transport.SuffixVisible = !TraceBlocked(Scene, ActualOrigin, Light, 100);
    TraceVector Segment{};
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Segment[Axis] = Receiver[Axis] + ReceiverSurface.NormalRoughness[Axis] * .002f - Origin[Axis];
    const double SegmentLength = std::sqrt(TraceDot(Segment, Segment));
    const auto ExpectedDirection = TraceNormalize(Segment);
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        Require(std::isfinite(ActualDirection[Axis]) &&
                    std::abs(ActualDirection[Axis] - ExpectedDirection[Axis]) < 2e-5,
                "Deformed connection direction differs");
    Require(std::isfinite(RayOrigin[3]) && RayOrigin[3] > .00001f &&
                std::abs(RayOrigin[3] - (SegmentLength - .00001f)) / std::max(1., SegmentLength) < 2e-5 &&
                RayDirection[3] == 0,
            "Deformed connection range differs");
    Transport.ConnectionVisible = !TraceBlocked(Scene, ActualOrigin, ActualDirection, RayOrigin[3]);
    const std::array<double, 3> LightRgb{1, double(.7f), double(.4f)},
        ReceiverRgb{ReceiverSurface.AlbedoIor[0], ReceiverSurface.AlbedoIor[1], ReceiverSurface.AlbedoIor[2]};
    for (uint32_t Channel = 0; Channel < 3; ++Channel)
    {
        const double Outgoing = Transport.SuffixVisible ? Facet.AlbedoRoughness[Channel] * LightRgb[Channel] *
                                                              std::max(0., TraceDot(Normal, Light)) / 3.141592653589793
                                                        : 0;
        Transport.Incident[Channel] = Outgoing * ReceiverRgb[Channel] / 3.141592653589793 *
                                      std::max(0., -(ReceiverSurface.NormalRoughness[0] * Direction[0] +
                                                     ReceiverSurface.NormalRoughness[1] * Direction[1] +
                                                     ReceiverSurface.NormalRoughness[2] * Direction[2])) *
                                      double(Transport.ConnectionVisible);
        Transport.Integrand[Channel] = Transport.Incident[Channel] * std::max(0., Transport.Cosine) / Transport.Squared;
    }
    return Transport;
}

void ValidateDeformedAreaReservoir(const char *Driver, const std::string &Shaders, const std::string &Directory)
{
    constexpr uint32_t Samples = 4096, Count = Samples * 6;
    auto Scene = ConstructDeformedAreaScene(0);
    Scene.Width = 192;
    Scene.Height = 128;
    Scene.Receivers.assign(Count, Scene.Receivers.front());
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(Scene, Shaders, false, false, true, false, false, false, false, 0, 40);
    std::filesystem::create_directories(Directory);
    uint32_t Ambiguous = 0;
    uint64_t SelectedChecks = 0, CandidateChecks = 0, ReceiverShadows = 0;
    uint32_t PerspectiveChanged = 0, WallPrimaries = 0;
    std::printf("DEFORMED_AREA_RESERVOIR_DEVICE name=%s type=%u pixels=1024 packets=4 methods=6 cases=3\n",
                Compute.Properties.deviceName, unsigned(Compute.Properties.deviceType));
    for (uint32_t Case = 0; Case < 3; ++Case)
    {
        Scene = ConstructDeformedAreaScene(Case);
        Scene.Width = 192;
        Scene.Height = 128;
        Scene.Receivers.resize(Count, Scene.Receivers.front());
        for (uint32_t Pixel = 0; Pixel < 1024; ++Pixel)
        {
            const auto Perspective = EvaluatePerspectivePrimary(Scene, Pixel, Case);
            const auto OriginalScene = ConstructGeometricAreaScene(Case);
            const auto Orthographic = EvaluatePerspectivePrimary(OriginalScene, Pixel, Case);
            if (Perspective.Index != Orthographic.Index ||
                std::abs(Perspective.Position[0] - Orthographic.Position[0]) > 1e-4 ||
                std::abs(Perspective.Position[1] - Orthographic.Position[1]) > 1e-4 ||
                std::abs(Perspective.Position[2] - Orthographic.Position[2]) > 1e-4)
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
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x5e92b341u) ^ (Case * 0x6c8e9cf5u));
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
            const uint32_t Seed = RgbQuartetHash((Pixel % Samples + 0x5e92b341u) ^ (Case * 0x6c8e9cf5u));
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
                    const uint32_t RaySlot = Method >= 4 ? 2 * Lane + Proposal : Lane;
                    const auto CandidateCoordinates = Storage[(2 + 4 * Lane + Proposal) * Count + Pixel];
                    Transport[Proposal] = EvaluateDeformedRayTransport(
                        Scene, Indices[Proposal], CandidateCoordinates[1], CandidateCoordinates[2], Receiver,
                        ReceiverSurface, Storage[(23 + 2 * RaySlot) * Count + Pixel],
                        Storage[(24 + 2 * RaySlot) * Count + Pixel]);
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
                        const auto EmittedOrigin = Storage[(23 + 2 * RaySlot) * Count + Pixel];
                        Point = {EmittedOrigin[0], EmittedOrigin[1], EmittedOrigin[2]};
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
        std::printf("DEFORMED_AREA_RESERVOIR_CASE case=%u error=%.12g normalizer_error=%.12g\n", Case, Maximum,
                    MaximumWeight);
        std::ofstream Capture(Directory + "/DeformedAreaReservoir" + std::to_string(Case) + ".f32", std::ios::binary);
        Capture.exceptions(std::ios::failbit | std::ios::badbit);
        Capture.write(reinterpret_cast<const char *>(Storage), 39 * Count * 16);
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
                    const uint32_t Seed = RgbQuartetHash((Pixel + 0x39f24a71u) ^ ((Bank * 16 + Batch) * 0x9e3779b9u) ^
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
        std::ofstream ReferenceCapture(Directory + "/DeformedAreaReference" + std::to_string(Case) + ".f32",
                                       std::ios::binary);
        ReferenceCapture.exceptions(std::ios::failbit | std::ios::badbit);
        ReferenceCapture.write(reinterpret_cast<const char *>(Reference.data()), 2 * 1024 * 16);
        std::printf("DEFORMED_AREA_REFERENCE case=%u banks=2 packets_per_bank_pixel=384 samples_per_bank_pixel=3072\n",
                    Case);
        std::fflush(stdout);
    }
    Require(PerspectiveChanged > 0 && WallPrimaries > 0, "Perspective primary controls had no effect");
    Require(ReceiverShadows > 0, "Omitted-receiver visibility control had no effect");
    std::printf("DEFORMED_AREA_OCCLUSION receiver_suffix_blocks=%llu primary_checks=73728 "
                "reference_primary_checks=2359296 changed=%u wall_primaries=%u\n",
                (unsigned long long)ReceiverShadows, PerspectiveChanged, WallPrimaries);
    Require(Ambiguous <= 16 && CandidateChecks == 491520 && SelectedChecks == 393216, "RIS replay coverage differs");
    std::printf("DEFORMED_AREA_RESERVOIR_DECISION correctness=1 candidates=%llu traced=%llu ambiguous=%u "
                "production_promote=0\n",
                (unsigned long long)CandidateChecks, (unsigned long long)SelectedChecks, Ambiguous);
}
} // namespace
