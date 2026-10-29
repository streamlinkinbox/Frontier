/*==========================================================================================================================================
                                            REPLAYRAYACCELERATIONGPUVALIDATION.H
==========================================================================================================================================*/
// 🧩 Verify runtime Vulkan ray-acceleration tier selection, bit-exact ordered BVH parity, and SAH-2 GPU traversal speedup.
#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                      URBAN OVERPASS + MULTI-VEHICLE SCENE BUILDER
//------------------------------------------------------------------------------------------------------------------------

struct UrbanBoxSpec
{
    double MinX = 0.0, MinY = 0.0, MinZ = 0.0;
    double MaxX = 0.0, MaxY = 0.0, MaxZ = 0.0;
    std::array<float, 3> Albedo{0.5f, 0.5f, 0.5f};
    float Roughness = 0.5f;
    float Metallic = 0.0f;
    float Specular = 0.5f;
    float Token = 1.0f;
    std::array<float, 3> Emissive{0.0f, 0.0f, 0.0f};
};

inline void AppendUrbanBoxFacets(std::vector<LightComputeFacet>& AuthoredFacets,
                                 std::vector<std::array<SpatialVector, 3>>& Geometry,
                                 const UrbanBoxSpec& Box,
                                 bool RoughOpaque)
{
    const SpatialVector Corners[8] = {
        {Box.MinX, Box.MinY, Box.MinZ}, {Box.MaxX, Box.MinY, Box.MinZ},
        {Box.MaxX, Box.MaxY, Box.MinZ}, {Box.MinX, Box.MaxY, Box.MinZ},
        {Box.MinX, Box.MinY, Box.MaxZ}, {Box.MaxX, Box.MinY, Box.MaxZ},
        {Box.MaxX, Box.MaxY, Box.MaxZ}, {Box.MinX, Box.MaxY, Box.MaxZ},
    };
    const int Faces[6][4] = {
        {0, 3, 2, 1}, // -Z bottom
        {4, 5, 6, 7}, // +Z top
        {0, 1, 5, 4}, // -Y front
        {2, 3, 7, 6}, // +Y back
        {1, 2, 6, 5}, // +X right
        {3, 0, 4, 7}, // -X left
    };
    const std::array<float, 4> FaceNormals[6] = {
        {0.0f, 0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f, 0.0f},
    };

    for (int Face = 0; Face < 6; ++Face)
    {
        const int TriPairs[2][3] = {
            {Faces[Face][0], Faces[Face][1], Faces[Face][2]},
            {Faces[Face][0], Faces[Face][2], Faces[Face][3]},
        };
        for (const auto& Tri : TriPairs)
        {
            const SpatialVector P0 = Corners[Tri[0]], P1 = Corners[Tri[1]], P2 = Corners[Tri[2]];
            Geometry.push_back({P0, P1, P2});
            LightComputeFacet Facet{};
            Facet.A = {float(P0.X), float(P0.Y), float(P0.Z), 0.0f};
            Facet.B = {float(P1.X), float(P1.Y), float(P1.Z), 0.0f};
            Facet.C = {float(P2.X), float(P2.Y), float(P2.Z), 0.0f};
            Facet.NormalA = FaceNormals[Face];
            Facet.NormalB = FaceNormals[Face];
            Facet.NormalC = FaceNormals[Face];
            const float EffectiveRoughness = RoughOpaque ? std::clamp(Box.Roughness, 0.28f, 0.95f) : 0.85f;
            Facet.AlbedoRoughness = {Box.Albedo[0], Box.Albedo[1], Box.Albedo[2], EffectiveRoughness};
            Facet.OpticalToken = {RoughOpaque ? Box.Metallic : 0.0f, 1.5f,
                                  RoughOpaque ? Box.Specular : 0.0f, Box.Token};
            Facet.Emissive = {Box.Emissive[0], Box.Emissive[1], Box.Emissive[2], 0.0f};
            AuthoredFacets.push_back(Facet);
        }
    }
}

inline void SetUrbanDrivingCamera(LightComputeArchive& Scene,
                                  std::array<float, 3> EyeCurrent,
                                  std::array<float, 3> EyePrevious)
{
    auto MakeBasis = [](std::array<float, 3> Eye, std::array<float, 4>* Dest)
    {
        // Camera looks slightly downward (-0.08 pitch) along +Y down the urban boulevard.
        const float Pitch = -0.075f;
        const float CosP = std::cos(Pitch), SinP = std::sin(Pitch);
        Dest[0] = {Eye[0], Eye[1], Eye[2], 0.08f};
        Dest[1] = {0.0f, CosP, SinP, 80.0f};        // Forward (+Y, slight -Z)
        Dest[2] = {1.0f, 0.0f, 0.0f, 0.72f};        // Right (+X), tan(fovX/2)
        Dest[3] = {0.0f, -SinP, CosP, 0.72f};       // Up (+Z tilted), tan(fovY/2)
    };
    Scene.Camera.fill({0.0f, 0.0f, 0.0f, 0.0f});
    MakeBasis(EyeCurrent, &Scene.Camera[0]);
    MakeBasis(EyePrevious, &Scene.Camera[4]);
    Scene.Camera[11] = {1.0f, 0.0f, 1.0f, 0.0f};
}

inline LightComputeArchive BuildUrbanOverpassScene(uint32_t Width,
                                                   uint32_t Height,
                                                   bool UseSahHierarchy,
                                                   bool RoughOpaque,
                                                   bool MultiStreetlamp,
                                                   std::array<float, 3> EyeCurrent = {-0.35f, -0.6f, 1.68f},
                                                   std::array<float, 3> EyePrevious = {-0.35f, -0.6f, 1.68f})
{
    LightComputeArchive Scene{};
    Scene.Width = Width;
    Scene.Height = Height;
    Scene.Receivers.resize(std::size_t(Width) * Height);

    std::vector<LightComputeFacet> Authored;
    std::vector<std::array<SpatialVector, 3>> Geometry;
    Authored.reserve(264);
    Geometry.reserve(264);

    // Non-overlapping architectural geometry (2mm separation avoids coplanar Z-fighting ties).
    // 1. Wide asphalt roadway (12 tris, token 1)
    AppendUrbanBoxFacets(Authored, Geometry,
                         {-6.0, -2.5, -0.25, 6.0, 23.0, 0.0, {0.22f, 0.23f, 0.25f}, 0.30f, 0.0f, 0.55f, 1.0f},
                         RoughOpaque);
    // 2. Left & right concrete sidewalks (24 tris, tokens 2..3)
    AppendUrbanBoxFacets(Authored, Geometry,
                         {-6.0, -2.5, 0.003, -3.95, 23.0, 0.22, {0.58f, 0.57f, 0.54f}, 0.68f, 0.0f, 0.35f, 2.0f},
                         RoughOpaque);
    AppendUrbanBoxFacets(Authored, Geometry,
                         {3.95, -2.5, 0.003, 6.0, 23.0, 0.22, {0.58f, 0.57f, 0.54f}, 0.68f, 0.0f, 0.35f, 3.0f},
                         RoughOpaque);
    // 3. Warm terracotta left building & cool slate-blue right building (24 tris, tokens 4..5)
    AppendUrbanBoxFacets(Authored, Geometry,
                         {-6.45, -2.5, 0.224, -5.82, 23.0, 6.4, {0.74f, 0.36f, 0.24f}, 0.72f, 0.0f, 0.3f, 4.0f},
                         RoughOpaque);
    AppendUrbanBoxFacets(Authored, Geometry,
                         {5.82, -2.5, 0.224, 6.45, 23.0, 6.4, {0.26f, 0.44f, 0.72f}, 0.72f, 0.0f, 0.3f, 5.0f},
                         RoughOpaque);
    // 4. Elevated concrete overpass deck spanning the boulevard (12 tris, token 6)
    AppendUrbanBoxFacets(Authored, Geometry,
                         {-5.78, 7.6, 3.553, 5.78, 14.4, 4.12, {0.62f, 0.63f, 0.65f}, 0.62f, 0.0f, 0.4f, 6.0f},
                         RoughOpaque);
    // 5. Six concrete overpass columns (72 tris, tokens 10..15)
    float PillarToken = 10.0f;
    for (int Side = -1; Side <= 1; Side += 2)
    {
        for (int Row = 0; Row < 3; ++Row)
        {
            const double Px = Side * 3.55;
            const double Py = 5.4 + Row * 5.1;
            AppendUrbanBoxFacets(Authored, Geometry,
                                 {Px - 0.34, Py - 0.34, 0.003, Px + 0.34, Py + 0.34, 3.55,
                                  {0.66f, 0.65f, 0.62f}, 0.65f, 0.0f, 0.35f, PillarToken},
                                 RoughOpaque);
            PillarToken += 1.0f;
        }
    }
    // 6. Four distinct multi-box traffic vehicles along the lanes (96 tris, tokens 20..27)
    struct VehicleSpec
    {
        double X = 0.0, Y = 0.0;
        std::array<float, 3> BodyColor{};
        std::array<float, 3> CabinColor{};
    };
    const VehicleSpec Cars[4] = {
        {-1.45, 4.7,  {0.80f, 0.16f, 0.14f}, {0.22f, 0.26f, 0.32f}}, // Foreground crimson coupe
        {1.52,  8.7,  {0.84f, 0.85f, 0.86f}, {0.25f, 0.29f, 0.34f}}, // Mid-ground white van under overpass
        {-1.58, 13.4, {0.18f, 0.42f, 0.80f}, {0.22f, 0.26f, 0.32f}}, // Blue sedan emerging from overpass
        {1.48,  17.8, {0.86f, 0.66f, 0.14f}, {0.24f, 0.27f, 0.31f}}, // Distant amber taxi
    };
    for (int CarIdx = 0; CarIdx < 4; ++CarIdx)
    {
        const auto& Car = Cars[CarIdx];
        const float BaseToken = 20.0f + float(CarIdx * 2);
        AppendUrbanBoxFacets(Authored, Geometry,
                             {Car.X - 0.88, Car.Y - 1.95, 0.14, Car.X + 0.88, Car.Y + 1.95, 0.84,
                              Car.BodyColor, 0.32f, 0.15f, 0.65f, BaseToken},
                             RoughOpaque);
        AppendUrbanBoxFacets(Authored, Geometry,
                             {Car.X - 0.74, Car.Y - 0.88, 0.843, Car.X + 0.74, Car.Y + 0.88, 1.40,
                              Car.CabinColor, 0.28f, 0.05f, 0.75f, BaseToken + 1.0f},
                             RoughOpaque);
    }

    // 7. Overhead luminaire panels: either 1 sunlit sky-portal strip (for cross-BVH NEE bit-exactness)
    //    or 2 under-overpass warm sodium luminaires + 1 bright boulevard sky-strip (252..276 tris).
    if (MultiStreetlamp)
    {
        // Under-overpass warm LED luminaires illuminating the shadowed carriageway + high sunlit sky strip.
        AppendUrbanBoxFacets(Authored, Geometry,
                             {-2.4, 8.8, 3.44, -0.6, 10.6, 3.548,
                              {0.0f, 0.0f, 0.0f}, 0.8f, 0.0f, 0.0f, 40.0f, {14.5f, 11.2f, 7.6f}},
                             RoughOpaque);
        AppendUrbanBoxFacets(Authored, Geometry,
                             {0.6, 11.2, 3.44, 2.4, 13.0, 3.548,
                              {0.0f, 0.0f, 0.0f}, 0.8f, 0.0f, 0.0f, 41.0f, {14.5f, 11.2f, 7.6f}},
                             RoughOpaque);
        AppendUrbanBoxFacets(Authored, Geometry,
                             {-3.2, 2.2, 5.85, 3.2, 5.8, 6.05,
                              {0.0f, 0.0f, 0.0f}, 0.8f, 0.0f, 0.0f, 42.0f, {8.5f, 9.8f, 12.2f}},
                             RoughOpaque);
    }
    else
    {
        // Single flat emissive quad (1 triangle!) so SAH and Median-8 sample the exact same emitter.
        const SpatialVector L0{-2.8, 2.5, 5.8}, L1{2.8, 2.5, 5.8}, L2{0.0, 6.8, 5.8};
        Geometry.push_back({L0, L1, L2});
        LightComputeFacet Luminaire{};
        Luminaire.A = {float(L0.X), float(L0.Y), float(L0.Z), 0.0f};
        Luminaire.B = {float(L1.X), float(L1.Y), float(L1.Z), 0.0f};
        Luminaire.C = {float(L2.X), float(L2.Y), float(L2.Z), 0.0f};
        Luminaire.NormalA = {0.0f, 0.0f, -1.0f, 0.0f};
        Luminaire.NormalB = {0.0f, 0.0f, -1.0f, 0.0f};
        Luminaire.NormalC = {0.0f, 0.0f, -1.0f, 0.0f};
        Luminaire.AlbedoRoughness = {0.0f, 0.0f, 0.0f, 0.8f};
        Luminaire.OpticalToken = {0.0f, 1.5f, 0.0f, 45.0f};
        Luminaire.Emissive = {14.0f, 13.0f, 11.0f, 0.0f};
        Authored.push_back(Luminaire);
    }

    TriangleVisibilitySpace Space;
    if (UseSahHierarchy)
    {
        Space.BuildSurfaceAreaHierarchy(Geometry, 2u);
    }
    else
    {
        Space.Build(Geometry);
    }
    Scene.Visibility = Space.PackFloat32();
    Scene.Surfaces.resize(Scene.Visibility.OriginalFacets.size());
    for (std::size_t I = 0u; I < Scene.Visibility.OriginalFacets.size(); ++I)
    {
        Scene.Surfaces[I] = Authored[Scene.Visibility.OriginalFacets[I]];
    }
    SetUrbanDrivingCamera(Scene, EyeCurrent, EyePrevious);
    return Scene;
}

inline double MaxAbsImageDiff(const std::vector<std::array<float, 4>>& First,
                              const std::vector<std::array<float, 4>>& Second)
{
    Require(First.size() == Second.size(), "Mismatched image sizes in parity check");
    double MaxDiff = 0.0;
    for (std::size_t I = 0u; I < First.size(); ++I)
    {
        for (uint32_t C = 0u; C < 3u; ++C)
        {
            Require(std::isfinite(First[I][C]) && std::isfinite(Second[I][C]),
                    "Non-finite pixel in ray-acceleration parity check");
            MaxDiff = std::max(MaxDiff, double(std::abs(First[I][C] - Second[I][C])));
        }
    }
    return MaxDiff;
}

inline double UrbanImageRmse(const std::vector<std::array<float, 4>>& Candidate,
                             const std::vector<std::array<float, 4>>& Reference)
{
    Require(Candidate.size() == Reference.size() && !Candidate.empty(), "Mismatched image sizes in RMSE");
    double SumSq = 0.0;
    for (std::size_t I = 0u; I < Candidate.size(); ++I)
    {
        for (uint32_t C = 0u; C < 3u; ++C)
        {
            const double D = double(Candidate[I][C]) - double(Reference[I][C]);
            SumSq += D * D;
        }
    }
    return std::sqrt(SumSq / double(Candidate.size() * 3u));
}

//------------------------------------------------------------------------------------------------------------------------
//                                  32X32 SWIFTSHADER VULKAN RAY ACCELERATION SUITE
//------------------------------------------------------------------------------------------------------------------------

inline void ValidateReplayRayAccelerationAll(const char* Driver, const char* Shaders)
{
    const uint32_t Width = 32u, Height = 32u;
    const std::array<float, 3> SkyEnv{0.26f, 0.34f, 0.48f};

    // Prepare once with SAH-2 scene (235+ enclosures) so Buffers[2] fits both SAH-2 and Median-8 (65 enclosures).
    auto InitialSahScene = BuildUrbanOverpassScene(Width, Height, true, false, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(InitialSahScene, Shaders, false, false, false, false, true, false);

    // 1. Runtime capability negotiation verification on live Vulkan physical device.
    const bool ExpectedHwReady = Compute.RayQueryCapabilities.ApiVersion >= VK_API_VERSION_1_2 &&
                                 Compute.RayQueryCapabilities.AccelerationStructureExtension &&
                                 Compute.RayQueryCapabilities.RayQueryExtension &&
                                 Compute.RayQueryCapabilities.DeferredHostOperationsExtension &&
                                 Compute.RayQueryCapabilities.BufferDeviceAddressExtension &&
                                 Compute.RayQueryCapabilities.AccelerationStructureFeature &&
                                 Compute.RayQueryCapabilities.RayQueryFeature &&
                                 Compute.RayQueryCapabilities.BufferDeviceAddressFeature;
    Require(Compute.AccelerationSelection.HardwareRayQueryReady == ExpectedHwReady,
            "Live Vulkan acceleration-tier selection disagreed with physical-device capabilities");
    if (!ExpectedHwReady)
    {
        Require(Compute.AccelerationSelection.Tier == PathReplayAccelerationTier::OrderedSoftwareBvh &&
                    Compute.AccelerationSelection.FallbackReason != nullptr &&
                    PathReplayAccelerationFlags(Compute.AccelerationSelection.Tier) == PathReplayOrderedTraversal,
                "Non-RT Vulkan device did not select OrderedSoftwareBvh fallback");
    }
    std::cout << "RAY_ACCEL_RUNTIME_TIER device=\"" << Compute.Properties.deviceName
              << "\" api_major=" << VK_API_VERSION_MAJOR(Compute.Properties.apiVersion)
              << " api_minor=" << VK_API_VERSION_MINOR(Compute.Properties.apiVersion)
              << " hw_ray_query_ready=" << (Compute.AccelerationSelection.HardwareRayQueryReady ? 1 : 0)
              << " selected_tier="
              << (Compute.AccelerationSelection.HardwareRayQueryReady ? "HardwareRayQuery" : "OrderedSoftwareBvh")
              << " fallback_reason=\""
              << (Compute.AccelerationSelection.FallbackReason ? Compute.AccelerationSelection.FallbackReason : "none")
              << "\"\n";

    // 2. Bit-exact parity + GPU Slot-9 conservation on identical BVH across 4-frame subpixel driving (Diffuse + GGX).
    {
        double MaxDiffuseDiff = 0.0;
        uint64_t LegacyDiffBoxes = 0u, LegacyDiffTris = 0u;
        uint64_t OrderedDiffBoxes = 0u, OrderedDiffTris = 0u;

        std::vector<std::vector<std::array<float, 4>>> LegacyFrames(4), OrderedFrames(4);
        for (int Mode = 0; Mode < 2; ++Mode)
        {
            const bool Ordered = (Mode == 1);
            std::array<float, 3> PrevEye{-0.35f, -0.6f, 1.68f};
            for (uint32_t Frame = 0u; Frame < 4u; ++Frame)
            {
                const std::array<float, 3> Eye{
                    -0.35f + 0.03f * float(Frame),
                    -0.6f + 0.14f * float(Frame),
                    1.68f,
                };
                auto Scene = BuildUrbanOverpassScene(Width, Height, false, false, true, Eye,
                                                     Frame == 0u ? Eye : PrevEye);
                const uint32_t Seed = 0x7100u + Frame * 977u;
                auto Img = Compute.ExecuteReplay(
                    Scene, 4u, Seed, true, true, Frame == 0u, SkyEnv, 0.01f, true, false,
                    false, true, false, false, 64u, true, false, false, true, false,
                    false, false, false, 1u, false, false, true, true, true, Ordered);
                const auto Stats = Compute.ExtendedStatistics();
                uint64_t SlotBoxSum = 0u, SlotTriSum = 0u;
                for (uint32_t P = 0u; P < Width * Height; ++P)
                {
                    const auto Slot9 = ReplaySampleSlot(Compute, 9u, P);
                    SlotBoxSum += uint64_t(std::llround(Slot9[0] + Slot9[2]));
                    SlotTriSum += uint64_t(std::llround(Slot9[1] + Slot9[3]));
                }
                Require(SlotBoxSum == Stats[18] && SlotTriSum == Stats[19],
                        "Per-pixel GPU Slot-9 traversal sum disagreed with atomic Counters[18..19]");
                if (!Ordered)
                {
                    LegacyFrames[Frame] = std::move(Img);
                    LegacyDiffBoxes += Stats[18];
                    LegacyDiffTris += Stats[19];
                }
                else
                {
                    OrderedFrames[Frame] = std::move(Img);
                    OrderedDiffBoxes += Stats[18];
                    OrderedDiffTris += Stats[19];
                }
                PrevEye = Eye;
            }
        }
        for (uint32_t Frame = 0u; Frame < 4u; ++Frame)
        {
            MaxDiffuseDiff = std::max(MaxDiffuseDiff, MaxAbsImageDiff(LegacyFrames[Frame], OrderedFrames[Frame]));
        }

        // Also check Wet-Asphalt Rough GGX bit-exactness on SAH-2 tree (Legacy vs Ordered).
        auto RoughScene = BuildUrbanOverpassScene(Width, Height, true, true, true);
        const auto RoughLeg = Compute.ExecuteReplay(
            RoughScene, 4u, 0x8811u, true, true, true, SkyEnv, 0.01f, true, false,
            true, true, false, false, 64u, true, false, true, true, false,
            false, false, false, 1u, false, false, false, true, true, false);
        const auto RoughLegStats = Compute.ExtendedStatistics();
        const auto RoughOrd = Compute.ExecuteReplay(
            RoughScene, 4u, 0x8811u, true, true, true, SkyEnv, 0.01f, true, false,
            true, true, false, false, 64u, true, false, true, true, false,
            false, false, false, 1u, false, false, false, true, true, true);
        const auto RoughOrdStats = Compute.ExtendedStatistics();
        const double MaxRoughDiff = MaxAbsImageDiff(RoughLeg, RoughOrd);

        std::cout << "RAY_ACCEL_BIT_EXACT_PARITY diffuse_4f_max_diff=" << MaxDiffuseDiff
                  << " rough_ggx_max_diff=" << MaxRoughDiff
                  << " median_legacy_boxes=" << LegacyDiffBoxes
                  << " median_ordered_boxes=" << OrderedDiffBoxes
                  << " median_legacy_tris=" << LegacyDiffTris
                  << " median_ordered_tris=" << OrderedDiffTris
                  << " sah_rough_legacy_boxes=" << RoughLegStats[18]
                  << " sah_rough_ordered_boxes=" << RoughOrdStats[18]
                  << " sah_rough_legacy_tris=" << RoughLegStats[19]
                  << " sah_rough_ordered_tris=" << RoughOrdStats[19]
                  << " slot9_conservation=1\n";

        Require(MaxDiffuseDiff == 0.0 && MaxRoughDiff == 0.0,
                "Ordered front-to-back BVH traversal was not bit-exact on identical BVH");
        Require(OrderedDiffTris <= LegacyDiffTris &&
                    RoughOrdStats[19] < RoughLegStats[19] &&
                    (RoughOrdStats[18] + RoughOrdStats[19]) < (RoughLegStats[18] + RoughLegStats[19]),
                "Ordered front-to-back traversal did not reduce GPU triangle and total intersection work");
    }

    // 3. End-to-end Engine Acceleration Upgrade: Baseline Median-8 Unordered vs SAH-2 + Front-to-Back Ordered.
    {
        // 3A. Single-luminaire scene: cross-tree bit-exactness check (identical emitter index order).
        auto SingleMed = BuildUrbanOverpassScene(Width, Height, false, false, false);
        auto SingleSah = BuildUrbanOverpassScene(Width, Height, true, false, false);
        const auto ImgSingleMed = Compute.ExecuteReplay(
            SingleMed, 4u, 0x9901u, false, true, true, SkyEnv, 0.01f, true, false,
            false, true, false, false, 64u, true, false, false, false, false,
            false, false, false, 1u, false, false, false, true, true, false);
        const auto StatsSingleMed = Compute.ExtendedStatistics();
        const auto ImgSingleSah = Compute.ExecuteReplay(
            SingleSah, 4u, 0x9901u, false, true, true, SkyEnv, 0.01f, true, false,
            false, true, false, false, 64u, true, false, false, false, false,
            false, false, false, 1u, false, false, false, true, true, true);
        const auto StatsSingleSah = Compute.ExtendedStatistics();
        const double CrossTreeDiff = MaxAbsImageDiff(ImgSingleMed, ImgSingleSah);
        Require(CrossTreeDiff <= 1e-6,
                "SAH-2 + Ordered BVH diverged from Median-8 Legacy on single-emitter urban overpass");

        // 3B. Multi-streetlamp 276-triangle urban overpass vs 64-pass converged reference.
        auto MultiMed = BuildUrbanOverpassScene(Width, Height, false, false, true);
        auto MultiSah = BuildUrbanOverpassScene(Width, Height, true, false, true);
        std::vector<std::array<float, 4>> Reference(Width * Height, {0.0f, 0.0f, 0.0f, 1.0f});
        constexpr uint32_t RefPasses = 64u;

        // Run 4-frame subpixel trajectory on Baseline (Median-8 Unordered) vs Engine Upgrade (SAH-2 Ordered).
        std::vector<std::array<float, 4>> FinalMed, FinalSah;
        uint64_t MedBoxes = 0u, MedTris = 0u, SahBoxes = 0u, SahTris = 0u, TotalRays = 0u;
        for (int Variant = 0; Variant < 2; ++Variant)
        {
            const bool UseSahOrd = (Variant == 1);
            std::array<float, 3> PrevEye{-0.35f, -0.6f, 1.68f};
            for (uint32_t Frame = 0u; Frame < 4u; ++Frame)
            {
                const std::array<float, 3> Eye{-0.35f, -0.6f + 0.12f * float(Frame), 1.68f};
                auto Scene = BuildUrbanOverpassScene(Width, Height, UseSahOrd, false, true, Eye,
                                                     Frame == 0u ? Eye : PrevEye);
                auto Out = Compute.ExecuteReplay(
                    Scene, 4u, 0xa300u + Frame * 613u, true, true, Frame == 0u, SkyEnv, 0.01f, true, false,
                    false, true, false, false, 64u, true, false, false, true, false,
                    false, false, false, 1u, false, false, true, true, true, UseSahOrd);
                const auto Stats = Compute.ExtendedStatistics();
                if (Frame == 3u)
                {
                    if (!UseSahOrd)
                    {
                        FinalMed = std::move(Out);
                        MedBoxes = Stats[18];
                        MedTris = Stats[19];
                        TotalRays = uint64_t(Stats[10] + Stats[11]) + Stats[8] + Stats[2];
                    }
                    else
                    {
                        FinalSah = std::move(Out);
                        SahBoxes = Stats[18];
                        SahTris = Stats[19];
                    }
                }
                PrevEye = Eye;
            }
        }
        // Compute reference at Frame 3 camera pose!
        const std::array<float, 3> FinalEye{-0.35f, -0.6f + 0.12f * 3.0f, 1.68f};
        auto RefScene = BuildUrbanOverpassScene(Width, Height, true, false, true, FinalEye, FinalEye);
        std::fill(Reference.begin(), Reference.end(), std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
        for (uint32_t Pass = 0u; Pass < RefPasses; ++Pass)
        {
            const auto PassImg = Compute.ExecuteReplay(
                RefScene, 4u, 0xd0000u + Pass * 7919u, false, false, true, SkyEnv, 0.01f, true, false,
                false, true, false, false, 64u, true, false, false, true, false,
                false, false, false, 4u, false, false, false, false, false, true);
            for (std::size_t I = 0u; I < Reference.size(); ++I)
            {
                for (uint32_t C = 0u; C < 3u; ++C)
                {
                    Reference[I][C] += PassImg[I][C] / float(RefPasses);
                }
            }
        }

        const double MedRmse = UrbanImageRmse(FinalMed, Reference);
        const double SahRmse = UrbanImageRmse(FinalSah, Reference);
        const double BoxSavedPct = 100.0 * (1.0 - double(SahBoxes) / double(std::max<uint64_t>(1u, MedBoxes)));
        const double TriSavedPct = 100.0 * (1.0 - double(SahTris) / double(std::max<uint64_t>(1u, MedTris)));
        const double TotalOpsSavedPct =
            100.0 * (1.0 - double(SahBoxes + SahTris) / double(std::max<uint64_t>(1u, MedBoxes + MedTris)));

        std::cout << "RAY_ACCEL_SAH_ORDERED_SPEEDUP facets=" << MultiSah.Surfaces.size()
                  << " median_nodes=" << MultiMed.Visibility.Enclosures.size()
                  << " sah_nodes=" << MultiSah.Visibility.Enclosures.size()
                  << " single_emitter_max_diff=" << CrossTreeDiff
                  << " total_rays=" << TotalRays
                  << " legacy_med_boxes=" << MedBoxes
                  << " sah_ord_boxes=" << SahBoxes
                  << " box_saved_pct=" << std::fixed << std::setprecision(2) << BoxSavedPct
                  << " legacy_med_tris=" << MedTris
                  << " sah_ord_tris=" << SahTris
                  << " tri_saved_pct=" << TriSavedPct
                  << " total_ops_saved_pct=" << TotalOpsSavedPct
                  << " legacy_rmse=" << std::setprecision(4) << MedRmse
                  << " sah_ordered_rmse=" << SahRmse << "\n";

        Require(TriSavedPct >= 80.0 && TotalOpsSavedPct >= 50.0,
                "SAH-2 + Ordered BVH did not achieve expected >=80% triangle and >=50% total op reduction");
        Require(SahRmse <= 0.12 && std::abs(SahRmse - MedRmse) <= 0.025,
                "SAH-2 + Ordered BVH degraded ReSTIR GI + SVGF convergence quality");

        std::cout << "PASS replay Vulkan ray acceleration tier selection, bit-exact parity, and SAH-2 GPU speedup\n";
        (void)StatsSingleMed;
        (void)StatsSingleSah;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                              64X64 URBAN OVERPASS VISUAL & HEATMAP CAPTURE
//------------------------------------------------------------------------------------------------------------------------

inline void CaptureReplayRayAccelerationComparison(const char* Driver,
                                                   const char* Shaders,
                                                   const char* OutputDir)
{
    std::filesystem::create_directories(OutputDir);
    const uint32_t Width = 64u, Height = 64u;
    const std::array<float, 3> SkyEnv{0.26f, 0.34f, 0.48f};

    auto InitialSah = BuildUrbanOverpassScene(Width, Height, true, false, true);
    LightComputeSubmission Compute(Driver);
    Compute.Prepare(InitialSah, Shaders, false, false, false, false, true, false);

    // 6-frame subpixel forward driving trajectory (~0.38 px/frame) into the urban overpass!
    constexpr uint32_t TrajectoryFrames = 6u;
    const auto EyeAt = [](uint32_t Frame) -> std::array<float, 3>
    {
        return {-0.35f + 0.018f * float(Frame), -0.75f + 0.11f * float(Frame), 1.68f};
    };

    std::vector<std::array<float, 4>> LegacyFinal, MedianOrderedFinal, SahOrderedFinal;
    std::vector<float> LegacyHeat(Width * Height * 2u, 0.0f), SahOrderedHeat(Width * Height * 2u, 0.0f);
    uint64_t LegBoxes = 0u, LegTris = 0u, MedOrdBoxes = 0u, MedOrdTris = 0u, SahBoxes = 0u, SahTris = 0u;
    uint64_t TotalRays = 0u;

    for (int Mode = 0; Mode < 3; ++Mode)
    {
        const bool UseSah = (Mode == 2);
        const bool Ordered = (Mode >= 1);
        for (uint32_t Frame = 0u; Frame < TrajectoryFrames; ++Frame)
        {
            const auto Eye = EyeAt(Frame);
            const auto Prev = Frame == 0u ? Eye : EyeAt(Frame - 1u);
            auto Scene = BuildUrbanOverpassScene(Width, Height, UseSah, false, true, Eye, Prev);
            auto Out = Compute.ExecuteReplay(
                Scene, 4u, 0x55000u + Frame * 1297u, true, true, Frame == 0u, SkyEnv, 0.01f, true, false,
                false, true, false, false, 64u, true, false, false, true, false,
                false, false, false, 1u, false, false, true, true, true, Ordered);
            if (Frame + 1u == TrajectoryFrames)
            {
                const auto Stats = Compute.ExtendedStatistics();
                if (Mode == 0)
                {
                    LegacyFinal = std::move(Out);
                    LegBoxes = Stats[18];
                    LegTris = Stats[19];
                    TotalRays = uint64_t(Stats[10] + Stats[11]) + Stats[8] + Stats[2];
                    for (uint32_t P = 0u; P < Width * Height; ++P)
                    {
                        const auto S9 = ReplaySampleSlot(Compute, 9u, P);
                        LegacyHeat[P * 2u + 0u] = S9[0] + S9[2];
                        LegacyHeat[P * 2u + 1u] = S9[1] + S9[3];
                    }
                }
                else if (Mode == 1)
                {
                    MedianOrderedFinal = std::move(Out);
                    MedOrdBoxes = Stats[18];
                    MedOrdTris = Stats[19];
                }
                else
                {
                    SahOrderedFinal = std::move(Out);
                    SahBoxes = Stats[18];
                    SahTris = Stats[19];
                    for (uint32_t P = 0u; P < Width * Height; ++P)
                    {
                        const auto S9 = ReplaySampleSlot(Compute, 9u, P);
                        SahOrderedHeat[P * 2u + 0u] = S9[0] + S9[2];
                        SahOrderedHeat[P * 2u + 1u] = S9[1] + S9[3];
                    }
                }
            }
        }
    }

    const double SameTreeMaxDiff = MaxAbsImageDiff(LegacyFinal, MedianOrderedFinal);
    Require(SameTreeMaxDiff == 0.0, "64x64 urban overpass ordered traversal was not bit-exact on Median-8 BVH");

    // 96-pass * 4-candidate (= 384 initial-candidate paths/pixel!) reference on final camera pose using fast SAH-2!
    const auto FinalEye = EyeAt(TrajectoryFrames - 1u);
    auto RefScene = BuildUrbanOverpassScene(Width, Height, true, false, true, FinalEye, FinalEye);
    auto MedScene = BuildUrbanOverpassScene(Width, Height, false, false, true, FinalEye, FinalEye);
    std::vector<std::array<float, 4>> Reference(Width * Height, {0.0f, 0.0f, 0.0f, 1.0f});
    constexpr uint32_t RefPasses = 96u;
    for (uint32_t Pass = 0u; Pass < RefPasses; ++Pass)
    {
        const auto PassImg = Compute.ExecuteReplay(
            RefScene, 4u, 0xe0000u + Pass * 7919u, false, false, true, SkyEnv, 0.01f, true, false,
            false, true, false, false, 64u, true, false, false, true, false,
            false, false, false, 4u, false, false, false, false, false, true);
        for (std::size_t I = 0u; I < Reference.size(); ++I)
        {
            for (uint32_t C = 0u; C < 3u; ++C)
            {
                Reference[I][C] += PassImg[I][C] / float(RefPasses);
            }
        }
    }

    const double LegacyRmse = UrbanImageRmse(LegacyFinal, Reference);
    const double SahRmse = UrbanImageRmse(SahOrderedFinal, Reference);
    const double BoxSavedPct = 100.0 * (1.0 - double(SahBoxes) / double(std::max<uint64_t>(1u, LegBoxes)));
    const double TriSavedPct = 100.0 * (1.0 - double(SahTris) / double(std::max<uint64_t>(1u, LegTris)));
    const double CombinedLegacyOps = double(LegBoxes + LegTris);
    const double CombinedSahOps = double(SahBoxes + SahTris);
    const double CombinedSavedPct = 100.0 * (1.0 - CombinedSahOps / std::max(1.0, CombinedLegacyOps));

    const std::string OutPath(OutputDir);
    WriteImage(OutPath + "/UrbanLegacy.ppm", LegacyFinal, Width, Height);
    WriteImage(OutPath + "/UrbanSahOrdered.ppm", SahOrderedFinal, Width, Height);
    WriteImage(OutPath + "/UrbanReference.ppm", Reference, Width, Height);

    auto WriteFloatDump = [](const std::string& Path, const std::vector<float>& Data)
    {
        std::ofstream Out(Path, std::ios::binary);
        Require(bool(Out), "Could not open traversal heatmap output");
        Out.write(reinterpret_cast<const char*>(Data.data()), std::streamsize(Data.size() * sizeof(float)));
    };
    WriteFloatDump(OutPath + "/LegacyHeat.bin", LegacyHeat);
    WriteFloatDump(OutPath + "/SahOrderedHeat.bin", SahOrderedHeat);

    {
        std::ofstream Metrics(OutPath + "/RayAccelerationMetrics.txt");
        Metrics << std::fixed << std::setprecision(6)
                << "width=" << Width << "\n"
                << "height=" << Height << "\n"
                << "facets=" << RefScene.Surfaces.size() << "\n"
                << "median_nodes=" << MedScene.Visibility.Enclosures.size() << "\n"
                << "sah_nodes=" << RefScene.Visibility.Enclosures.size() << "\n"
                << "total_rays=" << TotalRays << "\n"
                << "legacy_boxes=" << LegBoxes << "\n"
                << "legacy_tris=" << LegTris << "\n"
                << "median_ordered_boxes=" << MedOrdBoxes << "\n"
                << "median_ordered_tris=" << MedOrdTris << "\n"
                << "sah_ordered_boxes=" << SahBoxes << "\n"
                << "sah_ordered_tris=" << SahTris << "\n"
                << "same_tree_max_diff=" << SameTreeMaxDiff << "\n"
                << "box_saved_pct=" << BoxSavedPct << "\n"
                << "tri_saved_pct=" << TriSavedPct << "\n"
                << "combined_saved_pct=" << CombinedSavedPct << "\n"
                << "legacy_rmse=" << LegacyRmse << "\n"
                << "sah_ordered_rmse=" << SahRmse << "\n";
    }

    std::cout << "RAY_ACCEL_CAPTURE width=" << Width << " height=" << Height
              << " facets=" << RefScene.Surfaces.size()
              << " median_nodes=" << MedScene.Visibility.Enclosures.size()
              << " sah_nodes=" << RefScene.Visibility.Enclosures.size()
              << " total_rays=" << TotalRays
              << " same_tree_max_diff=" << SameTreeMaxDiff
              << " legacy_boxes=" << LegBoxes << " sah_ordered_boxes=" << SahBoxes
              << " box_saved_pct=" << std::fixed << std::setprecision(2) << BoxSavedPct
              << " legacy_tris=" << LegTris << " sah_ordered_tris=" << SahTris
              << " tri_saved_pct=" << TriSavedPct
              << " combined_saved_pct=" << CombinedSavedPct
              << " legacy_rmse=" << std::setprecision(4) << LegacyRmse
              << " sah_ordered_rmse=" << SahRmse << "\n";
}
