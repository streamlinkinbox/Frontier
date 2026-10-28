/*==========================================================================================================================================
                                                     UNIFIEDSCENEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Six-pose moving ControlVehicle with closed 6 mm glazing, cellular paint, rubber, mirrors, SSR cones/apron, denim cloth,
//    thin-pane canopy, baked DistanceFieldSpace, moving clouds and analytic/emissive volumetric fog shafts.

namespace UnifiedSceneReference
{
using namespace TransportReference;
using namespace PriorExitanceReference;

constexpr uint32_t UnifiedRigidThreshold = 500;
constexpr uint32_t UnifiedGlassToken = 100700;
constexpr uint32_t UnifiedMirrorToken = 100301;

inline void AppendTokenizedCone(LightComputeArchive& Scene, double X, double Y, uint32_t Orange, uint32_t Band,
                                uint32_t Token)
{
    constexpr uint32_t Slices = 48;
    for (uint32_t Ring = 0; Ring < 5; ++Ring)
    {
        constexpr double Heights[]{.08, .40, .56, .80, .96, 1.22};
        double Z0 = Heights[Ring], Z1 = Heights[Ring + 1];
        uint32_t Material = (Ring == 1 || Ring == 3) ? Band : Orange;
        for (uint32_t I = 0; I < Slices; ++I)
        {
            auto Corner = [&](uint32_t J, double Z)
            {
                double Theta = 2.0 * std::numbers::pi * double(J) / double(Slices);
                double Radius = .44 - Z * .30;
                return SpatialVector{X + Radius * std::cos(Theta), Y + Radius * std::sin(Theta), Z};
            };
            auto A = Corner(I, Z0), B = Corner(I + 1, Z0), C = Corner(I + 1, Z1), D = Corner(I, Z1);
            auto First = Scene.Surfaces.size();
            MaterialTriangle(Scene, A, B, C, Material, Token);
            MaterialTriangle(Scene, A, C, D, Material, Token);
            for (auto At : {First, First + 1})
            {
                auto& F = Scene.Surfaces[At];
                auto SmoothNormal = [&](const auto& P)
                {
                    auto N = Unit({P[0] - X, P[1] - Y, .30 * (.44 - P[2] * .30)});
                    return std::array<float, 4>{float(N.X), float(N.Y), float(N.Z), 0};
                };
                F.NormalA = SmoothNormal(F.A);
                F.NormalB = SmoothNormal(F.B);
                F.NormalC = SmoothNormal(F.C);
            }
        }
    }
    MaterialRectangle(Scene, {X, Y, .06}, {.52, 0, 0}, {0, .52, 0}, Orange, Token);
}

inline void AppendDrapedDenimBanner(LightComputeArchive& Scene, SpatialVector Origin, uint32_t FabricMaterial,
                                    uint32_t Token)
{
    constexpr uint32_t Nu = 8, Nv = 6;
    auto VertexAt = [&](uint32_t U, uint32_t V, float& OutUvU, float& OutUvV)
    {
        double Fu = double(U) / double(Nu), Fv = double(V) / double(Nv);
        OutUvU = float(Fu * 1.5);
        OutUvV = float(Fv * 1.5);
        double Fold = 0.085 * std::sin(Fu * 3.0 * std::numbers::pi) * (0.35 + 0.65 * Fv);
        return SpatialVector{Origin.X + (Fu - 0.5) * 1.35, Origin.Y + Fold, Origin.Z + (1.0 - Fv) * 1.35};
    };
    for (uint32_t V = 0; V < Nv; ++V)
        for (uint32_t U = 0; U < Nu; ++U)
        {
            float U00, V00, U10, V10, U11, V11, U01, V01;
            auto P00 = VertexAt(U, V, U00, V00);
            auto P10 = VertexAt(U + 1, V, U10, V10);
            auto P11 = VertexAt(U + 1, V + 1, U11, V11);
            auto P01 = VertexAt(U, V + 1, U01, V01);
            auto Emit = [&](SpatialVector A, float Au, float Av, SpatialVector B, float Bu, float Bv,
                            SpatialVector C, float Cu, float Cv)
            {
                auto First = Scene.Surfaces.size();
                MaterialTriangle(Scene, A, B, C, FabricMaterial, Token);
                auto& F = Scene.Surfaces[First];
                F.A[3] = Au;
                F.NormalA[3] = Av;
                F.B[3] = Bu;
                F.NormalB[3] = Bv;
                F.C[3] = Cu;
                F.NormalC[3] = Cv;
            };
            Emit(P00, U00, V00, P10, U10, V10, P11, U11, V11);
            Emit(P00, U00, V00, P11, U11, V11, P01, U01, V01);
        }
}

struct UnifiedPoseProduct
{
    LightComputeArchive Scene;
    uint32_t CloudOffset = UINT32_MAX, FogOffset = UINT32_MAX, DistanceOffset = UINT32_MAX;
    uint32_t PaintFacets = 0, RubberFacets = 0, ClosedGlassFacets = 0, SideMirrorFacets = 0;
    double EnclosedVolume = 0;
};

inline TriangleDistanceVolume BakeUnifiedVehicleDistance(const std::string& Directory,
                                                         const std::string& Shaders,
                                                         const std::array<float, 4>& GlazingSpectrum)
{
    auto Raw = LightComputeArchive::Load(Directory + "/VehicleMotion0.bin");
    const double RawThreshold = Raw.Camera[8][2];
    Prepare(Raw, 16, 24, true);
    const auto Materials = AppendSceneMaterials(Raw, Shaders);
    AssignVehicleSurfaces(Raw, Materials);
    SurfaceGeometry Windows;
    for (const auto& F : Raw.Surfaces)
        if (F.Emissive[3] == float(Materials[2]))
            for (const auto& P : {F.A, F.B, F.C})
            {
                SurfaceCorner Corner;
                Corner.Position = {P[0], P[1], P[2]};
                Corner.Normal = {0, 0, 1};
                Windows.TriangleCorners.push_back(uint32_t(Windows.Corners.size()));
                Windows.Corners.push_back(Corner);
            }
    const auto Glazing = ThickenPane(Windows, .006);
    const auto Mirror = DuplicateSceneMaterial(Raw, Materials[3], 1, {.94f, .94f, .94f, .04f}, 1);
    const auto GlassAddress = DuplicateSceneMaterial(Raw, Materials[2], 4, {1, 1, 1, 0}, 0);
    BindGlazingSpectrum(Raw, GlassAddress, GlazingSpectrum);

    std::vector<std::array<SpatialVector, 3>> RigidTriangles;
    for (const auto& F : Raw.Surfaces)
        if (F.Emissive[3] != float(Materials[2]) && double(F.OpticalToken[3]) > RawThreshold)
            RigidTriangles.push_back({Vector(F.A), Vector(F.B), Vector(F.C)});
    for (size_t I = 0; I < Glazing.Surface.TriangleCorners.size(); I += 3)
    {
        auto Corner = [&](uint32_t C)
        {
            const auto& V = Glazing.Surface.Corners[Glazing.Surface.TriangleCorners[I + C]].Position;
            return SpatialVector{V[0], V[1], V[2]};
        };
        RigidTriangles.push_back({Corner(0), Corner(1), Corner(2)});
    }
    LightComputeArchive TempMirrors;
    for (double Side : {-1., 1.})
        MaterialRectangle(TempMirrors, {.85, Side * 1.24, 1.30}, {.035, Side * .17, 0}, {0, 0, .09}, Mirror,
                          UnifiedMirrorToken);
    for (const auto& F : TempMirrors.Surfaces)
        RigidTriangles.push_back({Vector(F.A), Vector(F.B), Vector(F.C)});

    Require(RigidTriangles.size() == 25068 - 74 + 284 + 4, "Unexpected rigid vehicle facet count for SDF bake");
    TriangleDistanceVolume Volume;
    Volume.Bake(RigidTriangles, {64, 32, 24});
    return Volume;
}

inline UnifiedPoseProduct AssembleUnifiedPose(LightComputeArchive Scene, const std::string& Shaders,
                                              uint32_t Pose, uint32_t Width, uint32_t Height,
                                              const std::array<float, 4>& GlazingSpectrum,
                                              const TriangleDistanceVolume& DistanceVolume,
                                              const LightComputeArchive* PreviousPoseScene)
{
    const double Angle = Scene.Camera[8][0];
    const double PreviousAngle = Pose == 0 ? Angle : double(Scene.Camera[9][0]);
    const double RawThreshold = Scene.Camera[8][2];
    Require(std::abs(Angle - 0.08 * double(Pose)) < 1e-6 && RawThreshold > 0,
            "Unexpected archived vehicle pose transform");

    // 📝 Configure both analytic lighting families: solar directional + volumetric canopy spotlight + weather-reactive fill.
    Scene.Lights.clear();
    const auto SolarDir = Unit(SpatialVector{0.46, -0.74, 0.489});
    LightComputeShape Sun{};
    Sun.PositionReach = {float(SolarDir.X), float(SolarDir.Y), float(SolarDir.Z), 0};
    Sun.RadianceCategory = {7.2f, 6.6f, 5.6f, 0};
    Sun.PeakActive = {7.2f, 1.0f, 0, 0};
    Scene.Lights.push_back(Sun);

    LightComputeShape CanopySpot{};
    CanopySpot.PositionReach = {2.1f, 1.1f, 5.4f, 14.0f};
    const auto SpotAim = Unit(SpatialVector{-0.22, -0.28, -0.934});
    CanopySpot.DirectionInner = {float(SpotAim.X), float(SpotAim.Y), float(SpotAim.Z), float(std::cos(0.24))};
    CanopySpot.UpOuter = {1.0f, 0.0f, 0.0f, float(std::cos(0.46))};
    CanopySpot.RadianceCategory = {95.0f, 88.0f, 72.0f, 2.0f};
    CanopySpot.PeakActive = {95.0f, 1.0f, 0, 0};
    Scene.Lights.push_back(CanopySpot);

    LightComputeShape AccentPoint{};
    AccentPoint.PositionReach = {-1.8f, -3.4f, 2.9f, 13.0f};
    AccentPoint.RadianceCategory =
        Pose < 3 ? std::array<float, 4>{20.0f, 38.0f, 58.0f, 1.0f}
                 : std::array<float, 4>{56.0f, 26.0f, 14.0f, 1.0f};
    AccentPoint.PeakActive = {58.0f, 1.0f, 0, 0};
    Scene.Lights.push_back(AccentPoint);

    Prepare(Scene, 16, 24, true);
    const auto Materials = AppendSceneMaterials(Scene, Shaders);
    const uint32_t Green = Materials[0], Rubber = Materials[1], GlassBase = Materials[2],
                   Plastic = Materials[3], Fabric = Materials[4];

    // 📝 Shift vehicle mesh tokens above UnifiedRigidThreshold (500) and assign chassis-local UVs so flakes move rigidly.
    uint32_t PaintFacets = 0, RubberFacets = 0, OpenGlassFacets = 0;
    SurfaceGeometry Windows;
    for (auto& F : Scene.Surfaces)
    {
        if (double(F.OpticalToken[3]) > RawThreshold)
            F.OpticalToken[3] += float(UnifiedRigidThreshold);
        const auto Colour = F.AlbedoRoughness;
        uint32_t Material = 0;
        if (std::abs(Colour[0] - .5304039f) < 1e-5 && std::abs(Colour[1] - .62435724f) < 1e-5)
        {
            Material = Green;
            ++PaintFacets;
        }
        if (std::abs(Colour[0] - .03741f) < 1e-5 && std::abs(Colour[3] - .76087f) < 1e-5)
        {
            Material = Rubber;
            ++RubberFacets;
        }
        if (std::abs(Colour[0] - .026f) < 1e-5 && std::abs(Colour[3] - .02f) < 1e-5)
        {
            Material = GlassBase;
            ++OpenGlassFacets;
            for (const auto& P : {F.A, F.B, F.C})
            {
                SurfaceCorner Corner;
                Corner.Position = {P[0], P[1], P[2]};
                Corner.Normal = {0, 0, 1};
                Windows.TriangleCorners.push_back(uint32_t(Windows.Corners.size()));
                Windows.Corners.push_back(Corner);
            }
        }
        if (Material == 0)
            continue;
        F.Emissive[3] = float(Material);
        double CosA = std::cos(Angle), SinA = std::sin(Angle), ShiftX = 0.8 * Angle;
        for (uint32_t J = 0; J < 3; ++J)
        {
            auto& P = J == 0 ? F.A : J == 1 ? F.B : F.C;
            auto& N = J == 0 ? F.NormalA : J == 1 ? F.NormalB : F.NormalC;
            double LocalX = CosA * (double(P[0]) - ShiftX) + SinA * double(P[1]);
            P[3] = float(LocalX);
            N[3] = P[2];
        }
    }
    Require(PaintFacets == 778 && OpenGlassFacets == 74 && RubberFacets > 1000,
            "Unified scene vehicle material counts changed");

    const auto Glazing = ThickenPane(Windows, .006);
    Require(Glazing.PaneCount == 3 && Glazing.BoundaryEdges == 68 &&
                Glazing.Surface.TriangleCorners.size() / 3 == 284,
            "Unified scene closed glazing topology changed");
    Scene.Surfaces.erase(
        std::remove_if(Scene.Surfaces.begin(), Scene.Surfaces.end(),
                       [&](const auto& F) { return F.Emissive[3] == float(GlassBase); }),
        Scene.Surfaces.end());

    const uint32_t ClosedGlass = DuplicateSceneMaterial(Scene, GlassBase, 4, {1, 1, 1, 0}, 0);
    BindGlazingSpectrum(Scene, ClosedGlass, GlazingSpectrum);
    for (size_t I = 0; I < Glazing.Surface.TriangleCorners.size(); I += 3)
    {
        auto Corner = [&](uint32_t C)
        {
            const auto& V = Glazing.Surface.Corners[Glazing.Surface.TriangleCorners[I + C]].Position;
            return SpatialVector{V[0], V[1], V[2]};
        };
        MaterialTriangle(Scene, Corner(0), Corner(1), Corner(2), ClosedGlass, UnifiedGlassToken);
    }

    const uint32_t Mirror = DuplicateSceneMaterial(Scene, Plastic, 1, {.94f, .94f, .94f, .04f}, 1);
    const uint32_t WindowSheet = DuplicateSceneMaterial(Scene, GlassBase, 2, {1, 1, 1, .02f}, 0);
    const uint32_t Orange = DuplicateSceneMaterial(Scene, Plastic, 3, {.85f, .105f, .009f, .22f}, 0);
    const uint32_t Band = DuplicateSceneMaterial(Scene, Plastic, 3, {.82f, .85f, .88f, .12f}, .65f);
    const uint32_t WetApron = DuplicateSceneMaterial(Scene, Plastic, 3, {.16f, .18f, .21f, .26f}, .58f);
    const uint32_t MullionMat = DuplicateSceneMaterial(Scene, Plastic, 0, {.12f, .13f, .15f, .55f}, 0);

    // 📝 Rigidly posed door mirrors (Token > 500).
    auto PosePoint = [&](SpatialVector P)
    {
        double C = std::cos(Angle), S = std::sin(Angle);
        return SpatialVector{C * P.X - S * P.Y + .8 * Angle, S * P.X + C * P.Y, P.Z};
    };
    auto PoseVector = [&](SpatialVector P)
    {
        double C = std::cos(Angle), S = std::sin(Angle);
        return SpatialVector{C * P.X - S * P.Y, S * P.X + C * P.Y, P.Z};
    };
    for (double Side : {-1., 1.})
        MaterialRectangle(Scene, PosePoint({.85, Side * 1.24, 1.30}), PoseVector({.035, Side * .17, 0}),
                          {0, 0, .09}, Mirror, UnifiedMirrorToken);

    // 📝 Stationary proving-ground fixtures (unique Tokens 10..18 <= UnifiedRigidThreshold 500).
    AppendTokenizedCone(Scene, 2.8, -2.9, Orange, Band, 10);
    AppendTokenizedCone(Scene, -0.4, -3.5, Orange, Band, 11);
    MaterialRectangle(Scene, {1.15, -3.15, 0.012}, {2.45, 0, 0}, {0, 1.55, 0}, WetApron, 12);
    MaterialRectangle(Scene, {-3.45, 0.85, 1.25}, {0.68, 0.12, 0}, {0, 0, 0.92}, Mirror, 13);

    // 📝 Three-pane thin-sheet skylight canopy + mullion casting spectral attenuated shafts into participating fog.
    for (uint32_t Pane = 0; Pane < 3; ++Pane)
        MaterialRectangle(Scene, {2.35, 0.85, 3.35 + 0.04 * double(Pane)}, {0.95, 0, 0}, {0, 0.95, 0},
                          WindowSheet, 14);
    MaterialRectangle(Scene, {1.25, 0.85, 3.38}, {0.16, 0, 0}, {0, 0.95, 0}, MullionMat, 15);

    // 📝 Stationary indigo-denim cloth banner (preset Materials[4]) and compact emissive light strip.
    AppendDrapedDenimBanner(Scene, {-2.15, 2.55, 0.35}, Fabric, 16);
    {
        auto First = Scene.Surfaces.size();
        MaterialRectangle(Scene, {-0.85, 2.85, 2.55}, {0.95, 0, 0}, {0, 0.18, 0.22}, MullionMat, 17);
        for (size_t At = First; At < Scene.Surfaces.size(); ++At)
        {
            Scene.Surfaces[At].Emissive[0] = 11.0f;
            Scene.Surfaces[At].Emissive[1] = 7.8f;
            Scene.Surfaces[At].Emissive[2] = 4.2f;
        }
    }

    RebuildMaterialVisibility(Scene);

    // 📝 Two coherent tracking camera segments: wide proving-ground view (Poses 0..2), close 3/4 view (Poses 3..5).
    SpatialVector Eye = Pose < 3 ? SpatialVector{6.2 + 0.12 * double(Pose), -7.1, 3.35}
                                 : SpatialVector{3.45 + 0.10 * double(Pose - 3), -5.65, 2.38};
    SpatialVector Focus{0.8 * Angle, -0.25, 1.02};
    MaterialCamera(Scene, Eye, Focus, Width, Height);
    if (PreviousPoseScene)
        for (uint32_t I = 0; I < 4; ++I)
            Scene.Camera[4 + I] = PreviousPoseScene->Camera[I];

    float Samples = Scene.Camera[9][3];
    Scene.Camera[8] = {float(Angle), float(0.8 * Angle), float(UnifiedRigidThreshold), Scene.Camera[8][3]};
    Scene.Camera[9] = {float(PreviousAngle), float(0.8 * PreviousAngle), float(UnifiedRigidThreshold), Samples};
    Scene.Camera[11] = {float(std::cos(Angle)), float(std::sin(Angle)), float(std::cos(PreviousAngle)),
                        float(std::sin(PreviousAngle))};

    // 📝 Volumetric cloud deck (Wind = 0 s on Poses 0..2, shifting to 90 s on Poses 3..5) + participating ground mist.
    CloudParameters Clouds;
    Clouds.Coverage = 0.52;
    Clouds.DensityScale = 1.0;
    Clouds.WindTimeS = Pose < 3 ? 0.0 : 90.0;
    Clouds.AmbientRadiance = {0.22, 0.28, 0.38};
    Scene.Camera[10][1] = 32.0f;
    uint32_t CloudOffset = AppendCloudParameters(Scene, Clouds, SolarDir, {7.2, 6.6, 5.6}, 20);

    uint32_t FogOffset = AppendFogParameters(Scene);
    Scene.Angles[FogOffset + 0] = 0.0042f;
    Scene.Angles[FogOffset + 1] = 0.0f;
    Scene.Angles[FogOffset + 2] = 18.0f;
    Scene.Angles[FogOffset + 6] = 14.0f;
    Scene.Angles[FogOffset + 7] = 12.0f;
    Scene.Angles[FogOffset + 8] = 0.004f;
    Scene.Angles[FogOffset + 9] = 0.005f;
    Scene.Angles[FogOffset + 10] = 0.007f;
    Scene.Angles[FogOffset + 14] = 80.0f;
    Scene.Angles[FogOffset + 15] = 1.0f;

    EmissiveQuadrature::AppendDistribution(Scene);
    uint32_t DistanceOffset =
        AppendDistanceFieldSpace(Scene, DistanceVolume, Angle, 0.8 * Angle, 1.0f);

    uint32_t VerifiedGlass = 0, VerifiedSideMirrors = 0;
    for (const auto& F : Scene.Surfaces)
    {
        VerifiedGlass += F.OpticalToken[3] == float(UnifiedGlassToken);
        VerifiedSideMirrors += F.OpticalToken[3] == float(UnifiedMirrorToken);
    }
    Require(VerifiedGlass == 284 && VerifiedSideMirrors == 4, "Unified pose lost closed glazing or side mirrors");

    return {std::move(Scene), CloudOffset, FogOffset,         DistanceOffset,
            PaintFacets,      RubberFacets, VerifiedGlass,    VerifiedSideMirrors,
            Glazing.SignedVolume};
}
} // namespace UnifiedSceneReference

void ValidateUnifiedScene(const char* Driver, const std::string& Shaders, const std::string& Directory,
                          const std::string& Output, const std::string& Saved, bool NativeOnly = false)
{
    using namespace UnifiedSceneReference;
    std::filesystem::create_directories(Output);
    const auto Spectra = ReadGlazingSpectra(Saved);
    const auto DistanceVolume = BakeUnifiedVehicleDistance(Directory, Shaders, Spectra[3]);

    std::vector<UnifiedPoseProduct> Poses;
    Poses.reserve(6);
    double BaseVolume = 0, MaxVolumeDrift = 0, MaxDistanceOverstep = 0;
    uint32_t RigidContinuations = 0, DistanceProbes = 0;
    uint32_t Bits = 0x73f19a2bu;

    for (uint32_t Pose = 0; Pose < 6; ++Pose)
    {
        auto Raw = LightComputeArchive::Load(Directory + "/VehicleMotion" + std::to_string(Pose) + ".bin");
        const LightComputeArchive* Prev = Pose ? &Poses.back().Scene : nullptr;
        auto Product =
            AssembleUnifiedPose(std::move(Raw), Shaders, Pose, 192, 120, Spectra[3], DistanceVolume, Prev);
        if (Pose == 0)
            BaseVolume = Product.EnclosedVolume;
        MaxVolumeDrift = std::max(MaxVolumeDrift, std::abs(Product.EnclosedVolume - BaseVolume));
        if (Pose > 0)
        {
            bool Valid = ConfirmRigidPoseContinuation(
                Poses.back().Scene.Surfaces, Product.Scene.Surfaces, Poses.back().Scene.Camera[8],
                Product.Scene.Camera[8], Product.Scene.Camera[9], Product.Scene.Camera[11]);
            Require(Valid, "Unified vehicle pose failed rigid continuation check");
            ++RigidContinuations;
        }

        // 📝 Verify 500 random exterior points per pose against exact posed rigid ControlVehicle triangles.
        std::vector<std::array<SpatialVector, 3>> PosedRigid;
        for (const auto& F : Product.Scene.Surfaces)
            if (double(F.OpticalToken[3]) > double(UnifiedRigidThreshold))
                PosedRigid.push_back({Vector(F.A), Vector(F.B), Vector(F.C)});
        TriangleVisibilitySpace PosedSpace;
        PosedSpace.Build(PosedRigid);
        for (uint32_t K = 0; K < 500; ++K)
        {
            SpatialVector World{-3.2 + 6.8 * EmissiveQuadrature::Random(Bits),
                                -2.4 + 4.8 * EmissiveQuadrature::Random(Bits),
                                0.05 + 2.6 * EmissiveQuadrature::Random(Bits)};
            double Bound = EvaluateNativeShaderDistanceLowerBound(Product.Scene.Angles, Product.DistanceOffset,
                                                                  World);
            if (Bound > 1e-4)
            {
                SpatialVector Dir = Unit({2.0 * EmissiveQuadrature::Random(Bits) - 1.0,
                                          2.0 * EmissiveQuadrature::Random(Bits) - 1.0,
                                          2.0 * EmissiveQuadrature::Random(Bits) - 1.0});
                Require(!PosedSpace.Occluded(World, Dir, 0.0, Bound * 0.995),
                        "Unified DistanceFieldSpace lower bound overstepped posed vehicle triangle");
            }
            double Angle = Product.Scene.Camera[8][0];
            double Dx = World.X - 0.8 * Angle, Dy = World.Y;
            SpatialVector Local{std::cos(Angle) * Dx + std::sin(Angle) * Dy,
                                -std::sin(Angle) * Dx + std::cos(Angle) * Dy, World.Z};
            MaxDistanceOverstep =
                std::max(MaxDistanceOverstep, Bound - DistanceVolume.ExactDistance(Local).Distance);
            ++DistanceProbes;
        }
        Poses.push_back(std::move(Product));
    }
    Require(MaxVolumeDrift < 1e-7 && RigidContinuations == 5 && MaxDistanceOverstep < 5e-5,
            "Unified native invariant check failed");
    std::cout << "UNIFIED_SCENE_NATIVE poses=6 rigid_continuations=" << RigidContinuations
              << " distance_samples=" << DistanceVolume.SampleCount()
              << " distance_probes=" << DistanceProbes << " max_volume_drift=" << MaxVolumeDrift
              << " max_bound_overestimate=" << MaxDistanceOverstep << std::endl;
    if (NativeOnly)
        return;

    auto Capacity = Poses.front().Scene;
    for (const auto& P : Poses)
    {
        Capacity.Visibility.Enclosures.resize(
            std::max(Capacity.Visibility.Enclosures.size(), P.Scene.Visibility.Enclosures.size()));
        Capacity.Angles.resize(std::max(Capacity.Angles.size(), P.Scene.Angles.size()));
    }

    LightComputeSubmission Compute(Driver);
    Compute.RigidMotionHistory = true;
    Compute.Prepare(Capacity, Shaders);
    std::cout << "UNIFIED_SCENE_DEVICE name=" << Compute.Properties.deviceName
              << " type=" << Compute.Properties.deviceType << " triangle_bvh=1 hardware_ray_query=0"
              << " width=192 height=120 samples=16" << std::endl;

    // 1. Verify GPU DistanceFieldSpace Mode 1 vs Mode 0 equivalence and Mode 2 per-pixel posed diagnostic readback.
    const auto Mode0 = Compute.Execute(Poses[0].Scene, 3, 16, 81, 64, 7, true, true, 16, true, true, true,
                                       Poses[0].CloudOffset, Poses[0].FogOffset, true, true, UINT32_MAX);
    const auto Mode1 = Compute.Execute(Poses[0].Scene, 3, 16, 81, 64, 7, true, true, 16, true, true, true,
                                       Poses[0].CloudOffset, Poses[0].FogOffset, true, true,
                                       Poses[0].DistanceOffset);
    const auto Mode1Stats = Compute.ExtendedStatistics();
    double MaxDistanceDiff = 0;
    for (size_t I = 0; I < Mode0.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C)
            MaxDistanceDiff = std::max(MaxDistanceDiff, double(std::abs(Mode1[I][C] - Mode0[I][C])));
    Require(MaxDistanceDiff < 2e-5, "Unified DistanceFieldSpace presence changed exact BVH transport");

    auto Mode2Scene = Poses[0].Scene;
    UpdateDistanceFieldPose(Mode2Scene, Poses[0].DistanceOffset, 0.0, 0.0, 2.0f);
    Compute.Execute(Mode2Scene, 3, 16, 81, 64, 7, true, true, 4, true, true, true, Poses[0].CloudOffset,
                    Poses[0].FogOffset, true, false, Poses[0].DistanceOffset);
    const auto DiagField = Compute.DistanceDiagnostics();
    const auto DiagRec = Compute.PrimaryReceivers();
    std::vector<std::array<float, 4>> DistanceVisual(DiagField.size(), {0, 0, 0, 1});
    uint32_t ClearSolarCount = 0, NearVehicleCount = 0, LipschitzViolations = 0;
    for (size_t I = 0; I < DiagField.size(); ++I)
    {
        if (DiagField[I][3] < 0.5f || DiagRec[I].PositionHit[3] == 0)
            continue;
        double Lower = DiagField[I][0];
        // 📝 Shader layout: [0] certified lower bound, [1] solar clearance flag, [2] probe minimum travel.
        bool ClearSolar = DiagField[I][1] > 0.5f;
        ClearSolarCount += ClearSolar ? 1u : 0u;
        NearVehicleCount += Lower < 0.15 ? 1u : 0u;
        SpatialVector World = Vector(DiagRec[I].PositionHit);
        if (Lower > DistanceVolume.ExactDistance(World).Distance + 5e-4)
            ++LipschitzViolations;
        float Band = float(0.5 + 0.5 * std::cos(Lower * 14.0));
        float Reach = float(std::clamp(Lower / 4.0, 0.0, 1.0));
        DistanceVisual[I] = {ClearSolar ? 0.12f + 0.65f * Reach : 0.72f * (1.0f - Reach) + 0.08f,
                             0.18f + 0.58f * Band * (1.0f - 0.5f * Reach),
                             0.25f + 0.70f * (1.0f - Reach), 1.0f};
    }
    std::cerr << "UNIFIED_MODE2 lipschitz_violations=" << LipschitzViolations << " clear_solar=" << ClearSolarCount
              << " near_vehicle=" << NearVehicleCount << std::endl;
    Require(LipschitzViolations == 0 && ClearSolarCount > 500 && NearVehicleCount > 500,
            "Unified Mode 2 DistanceFieldSpace diagnostic probe failed");
    WriteImage(Output + "/UnifiedDistanceDiagnostic.ppm", DistanceVisual, 192, 120);

    // 2. Run the 6-pose moving sequence: Pose 0 Cold -> Pose 0 Warm -> Pose 1 -> Pose 2 -> Pose 3 Refresh -> Pose 4 -> Pose 5.
    struct PoseReport
    {
        std::string Label;
        uint32_t OpaqueReused = 0, RigidReused = 0, GlassPixels = 0, WindowPixels = 0, MirrorPixels = 0,
                 SsrPixels = 0, PyramidHits = 0, PyramidFallbacks = 0, DistanceSkips = 0, Capped = 0,
                 Invalid = 0;
    };
    std::vector<PoseReport> Reports;
    auto RunFrame = [&](UnifiedPoseProduct& P, uint32_t Seed, bool Reset, const std::string& Label,
                        const std::string& FileStem)
    {
        const auto Image = Compute.Execute(P.Scene, 3, 16, Seed, 64, 7, Reset, true, 16, true, true, true,
                                           P.CloudOffset, P.FogOffset, true, true, P.DistanceOffset);
        const auto Rec = Compute.PrimaryReceivers();
        const auto Diag = Compute.TransportSlab(20);
        const auto Ext = Compute.ExtendedStatistics();
        PoseReport R;
        R.Label = Label;
        R.PyramidHits = Ext[16];
        R.PyramidFallbacks = Ext[17];
        R.DistanceSkips = Ext[19];
        // 📝 Caps/unsupported are packed per receiver category, not in Counters[13..15] (those count compact samples).
        for (size_t I = 0; I < Rec.size(); ++I)
        {
            if (Rec[I].PositionHit[3] == 0)
                continue;
            // 📝 Codes: 1 mirror, 2 thin sheet, 3/4 SSR screen/BVH, 5 solid volume (glass).
            const uint32_t Cat = uint32_t(Rec[I].ExpectedDirect[0]);
            const uint32_t E1 = uint32_t(Rec[I].ExpectedDirect[1]), E2 = uint32_t(Rec[I].ExpectedDirect[2]);
            if (Cat == 5u) { R.Capped += E1 / 65536u; R.Invalid += E2; }
            else if (Cat == 1u) { R.Capped += E2 / 256u; R.Invalid += E2 % 256u; }
            else if (Cat == 2u) { R.Capped += E2 / 512u; R.Invalid += E2 % 512u; }
        }
        uint32_t DirectionalBlurred = 0;
        for (size_t I = 0; I < Image.size(); ++I)
        {
            for (uint32_t C = 0; C < 3; ++C)
                Require(std::isfinite(Image[I][C]) && Image[I][C] >= 0, "Non-finite unified pixel");
            if (Rec[I].PositionHit[3] == 0)
                continue;
            uint32_t Cat = uint32_t(Rec[I].ExpectedDirect[0]);
            R.MirrorPixels += Cat == 1u;
            R.WindowPixels += Cat == 2u;
            R.SsrPixels += Cat == 3u || Cat == 4u;
            R.GlassPixels += Cat == 5u;
            if (Diag[I][0] > 0.5f)
            {
                ++R.OpaqueReused;
                if (Rec[I].MotionPositionToken[3] > float(UnifiedRigidThreshold))
                    ++R.RigidReused;
                if (Cat == 1u || Cat == 5u)
                    ++DirectionalBlurred;
            }
        }
        Require(R.Capped == 0 && R.Invalid == 0 && DirectionalBlurred == 0,
                "Unified frame hit interaction cap, invalid pixel or blurred mirror/solid-glass history");
        if (!FileStem.empty())
            WriteImage(Output + "/" + FileStem + ".ppm", Image, P.Scene.Width, P.Scene.Height);
        std::cout << "UNIFIED_POSE label=" << R.Label << " opaque_reused=" << R.OpaqueReused
                  << " rigid_reused=" << R.RigidReused << " glass_pixels=" << R.GlassPixels
                  << " window_pixels=" << R.WindowPixels << " mirror_pixels=" << R.MirrorPixels
                  << " ssr_pixels=" << R.SsrPixels << " pyramid_hits=" << R.PyramidHits
                  << " pyramid_fallbacks=" << R.PyramidFallbacks << " distance_skips=" << R.DistanceSkips
                  << " capped=" << R.Capped << " invalid=" << R.Invalid << std::endl;
        Reports.push_back(R);
        return R;
    };

    const auto R0Cold = RunFrame(Poses[0], 90, true, "pose0_cold", "");
    const auto R0Warm = RunFrame(Poses[0], 91, false, "pose0_warm", "UnifiedPose0");
    const auto R1 = RunFrame(Poses[1], 92, false, "pose1_moving", "UnifiedPose1");
    const auto R2 = RunFrame(Poses[2], 93, false, "pose2_moving", "UnifiedPose2");
    const auto R3 = RunFrame(Poses[3], 94, false, "pose3_weather_light_shift", "UnifiedPose3");
    const auto R4 = RunFrame(Poses[4], 95, false, "pose4_moving_tir", "UnifiedPose4");
    const auto R5 = RunFrame(Poses[5], 96, false, "pose5_moving", "UnifiedPose5");

    Require(R0Cold.OpaqueReused == 0 && R0Cold.PyramidHits == 0, "Cold frame unexpectedly reused history");
    Require(R0Warm.OpaqueReused > 8000 && R0Warm.PyramidHits > 100, "Stationary warm frame missed reuse");
    Require(R1.OpaqueReused > 7000 && R1.RigidReused > 1000 && R1.PyramidHits > 80 &&
                R2.OpaqueReused > 7000 && R2.RigidReused > 1000 && R2.PyramidHits > 80,
            "Poses 1..2 failed continuous moving authored vehicle + ExitancePyramid reuse");
    Require(R3.OpaqueReused == 0 && R3.PyramidHits == 0,
            "Pose 3 weather + lighting shift did not trigger clean history/pyramid refresh");
    Require(R4.OpaqueReused > 5000 && R4.RigidReused > 800 && R4.PyramidHits > 50 &&
                R5.OpaqueReused > 5000 && R5.RigidReused > 800 && R5.PyramidHits > 50,
            "Poses 4..5 failed continuous moving reuse after weather refresh");

    // 3. All-sources-off zero check (analytic + emissive + sky/clouds + fog ambient all disabled).
    auto Dark = Poses[5].Scene;
    for (auto& L : Dark.Lights)
        L.PeakActive[1] = 0;
    for (auto& F : Dark.Surfaces)
        F.Emissive[0] = F.Emissive[1] = F.Emissive[2] = 0;
    uint32_t DarkFog = Poses[5].FogOffset;
    Dark.Angles[DarkFog + 8] = Dark.Angles[DarkFog + 9] = Dark.Angles[DarkFog + 10] = 0;
    Dark.Angles[DarkFog + 15] = 0;
    const auto OffPixels = Compute.Execute(Dark, 3, 16, 97, 64, 7, true, true, 16, false, true, true,
                                           UINT32_MAX, DarkFog, true, false, Poses[5].DistanceOffset);
    uint32_t BlackPixels = 0;
    for (const auto& P : OffPixels)
        BlackPixels += (P[0] == 0.0f && P[1] == 0.0f && P[2] == 0.0f) ? 1u : 0u;
    Require(BlackPixels == OffPixels.size(), "All-sources-off unified frame left residual radiance");

    // 4. Higher-resolution 320x200 warm hero captures for Pose 0 (Wind 0 s) and Pose 3 (Wind 90 s + warm shaft shift).
    auto HeroRaw0 = LightComputeArchive::Load(Directory + "/VehicleMotion0.bin");
    auto Hero0 = AssembleUnifiedPose(std::move(HeroRaw0), Shaders, 0, 320, 200, Spectra[3], DistanceVolume,
                                     nullptr);
    auto HeroRaw3 = LightComputeArchive::Load(Directory + "/VehicleMotion3.bin");
    auto Hero3 = AssembleUnifiedPose(std::move(HeroRaw3), Shaders, 3, 320, 200, Spectra[3], DistanceVolume,
                                     nullptr);
    auto HeroCapacity = Hero0.Scene;
    HeroCapacity.Visibility.Enclosures.resize(
        std::max(HeroCapacity.Visibility.Enclosures.size(), Hero3.Scene.Visibility.Enclosures.size()));
    HeroCapacity.Angles.resize(std::max(HeroCapacity.Angles.size(), Hero3.Scene.Angles.size()));

    LightComputeSubmission HeroCompute(Driver);
    HeroCompute.RigidMotionHistory = true;
    HeroCompute.Prepare(HeroCapacity, Shaders);
    HeroCompute.Execute(Hero0.Scene, 3, 16, 101, 64, 7, true, true, 16, true, true, true, Hero0.CloudOffset,
                        Hero0.FogOffset, true, true, Hero0.DistanceOffset);
    const auto HeroImg0 =
        HeroCompute.Execute(Hero0.Scene, 3, 16, 102, 64, 7, false, true, 16, true, true, true,
                            Hero0.CloudOffset, Hero0.FogOffset, true, true, Hero0.DistanceOffset);
    WriteImage(Output + "/UnifiedHeroPose0.ppm", HeroImg0, 320, 200);

    HeroCompute.Execute(Hero3.Scene, 3, 16, 103, 64, 7, true, true, 16, true, true, true, Hero3.CloudOffset,
                        Hero3.FogOffset, true, true, Hero3.DistanceOffset);
    const auto HeroImg3 =
        HeroCompute.Execute(Hero3.Scene, 3, 16, 104, 64, 7, false, true, 16, true, true, true,
                            Hero3.CloudOffset, Hero3.FogOffset, true, true, Hero3.DistanceOffset);
    WriteImage(Output + "/UnifiedHeroPose3.ppm", HeroImg3, 320, 200);

    std::ofstream Metrics(Output + "/UnifiedSceneMetrics.json");
    Metrics << "{\n"
            << "  \"device\": \"" << Compute.Properties.deviceName << "\",\n"
            << "  \"device_type\": " << Compute.Properties.deviceType << ",\n"
            << "  \"distance_samples\": " << DistanceVolume.SampleCount() << ",\n"
            << "  \"distance_queries_pose0\": " << Mode1Stats[18] << ",\n"
            << "  \"distance_skips_pose0\": " << Mode1Stats[19] << ",\n"
            << "  \"max_distance_mode_diff\": " << MaxDistanceDiff << ",\n"
            << "  \"mode2_lipschitz_violations\": " << LipschitzViolations << ",\n"
            << "  \"mode2_clear_solar_pixels\": " << ClearSolarCount << ",\n"
            << "  \"mode2_near_vehicle_pixels\": " << NearVehicleCount << ",\n"
            << "  \"black_source_off_pixels\": " << BlackPixels << ",\n"
            << "  \"poses\": [\n";
    for (size_t I = 0; I < Reports.size(); ++I)
    {
        const auto& R = Reports[I];
        Metrics << "    {\"label\": \"" << R.Label << "\", \"opaque_reused\": " << R.OpaqueReused
                << ", \"rigid_reused\": " << R.RigidReused << ", \"glass_pixels\": " << R.GlassPixels
                << ", \"window_pixels\": " << R.WindowPixels << ", \"mirror_pixels\": " << R.MirrorPixels
                << ", \"ssr_pixels\": " << R.SsrPixels << ", \"pyramid_hits\": " << R.PyramidHits
                << ", \"pyramid_fallbacks\": " << R.PyramidFallbacks
                << ", \"distance_skips\": " << R.DistanceSkips << ", \"capped\": " << R.Capped
                << ", \"invalid\": " << R.Invalid << "}" << (I + 1 < Reports.size() ? "," : "") << "\n";
    }
    Metrics << "  ]\n}\n";

    std::cout << "UNIFIED_SCENE_COMPLETE distance_mode_diff=" << MaxDistanceDiff
              << " distance_queries=" << Mode1Stats[18] << " distance_skips=" << Mode1Stats[19]
              << " mode2_violations=" << LipschitzViolations << " black_pixels=" << BlackPixels << std::endl;
}
