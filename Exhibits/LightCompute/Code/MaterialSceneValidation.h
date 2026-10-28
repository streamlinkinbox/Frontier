/*==========================================================================================================================================
                                                        MATERIALSCENEVALIDATION.H
==========================================================================================================================================*/
// 🧩 Bind retained material presets to the actual vehicle and reuse native shaderball/cloth geometry in BVH-rendered scenes.

//------------------------------------------------------------------------------------------------------------------------
//                                                     SCENE MATERIAL BINDINGS
//------------------------------------------------------------------------------------------------------------------------

std::array<uint32_t, 5> AppendSceneMaterials(LightComputeArchive& Scene, const std::string& Shaders)
{
#if __has_include("../../../_ClaudeScratch/build/light-compute/MaterialSceneOffsets.h")
#include "../../../_ClaudeScratch/build/light-compute/MaterialSceneOffsets.h"
    std::ifstream Input(Shaders + "/MaterialSceneMaps.bin", std::ios::binary | std::ios::ate);
    Require(bool(Input), "Run PrepareMaterialScene.py first");
    auto Bytes = Input.tellg();
    Require(Bytes > 0 && uint64_t(Bytes) % 4 == 0 && uint64_t(Bytes) < 8 * 1024 * 1024,
            "Material float atlas exceeds budget");
    uint32_t First = uint32_t(Scene.Angles.size());
    Scene.Angles.resize(First + size_t(Bytes) / 4);
    Input.seekg(0);
    Input.read(reinterpret_cast<char*>(Scene.Angles.data() + First), Bytes);
    Require(bool(Input), "Truncated material map atlas");
    std::array<uint32_t, 5> Addresses{};
    for (uint32_t I = 0; I < Addresses.size(); ++I)
    {
        uint32_t At = First + MaterialSceneOffsets[I];
        Addresses[I] = At + 1;
        for (uint32_t H : {2u, 3u})
            if (Scene.Angles[At + H] > 0) Scene.Angles[At + H] += float(First);
    }
    return Addresses;
#else
    (void)Scene; (void)Shaders;
    throw std::runtime_error("Run PrepareMaterialScene.py before compiling this validation");
#endif
}
uint32_t DuplicateSceneMaterial(LightComputeArchive& Scene, uint32_t Address, uint32_t Category,
                                std::array<float, 4> Colour, float Metal)
{
    std::vector<float> Coefficients(Scene.Angles.begin() + Address - 1, Scene.Angles.begin() + Address - 1 + 268);
    uint32_t First = uint32_t(Scene.Angles.size());
    Coefficients[0] = 1;
    Coefficients[1] = float(Category);
    Coefficients[2] = Coefficients[3] = 0;
    for (uint32_t C = 0; C < 4; ++C) Coefficients[8 + C] = Colour[C];
    Coefficients[12] = Metal;
    Coefficients[200] = 0;
    Scene.Angles.insert(Scene.Angles.end(), Coefficients.begin(), Coefficients.end());
    return First + 1;
}
void MaterialTriangle(LightComputeArchive& Scene, SpatialVector A, SpatialVector B, SpatialVector C,
                      uint32_t Material, uint32_t Token)
{
    using namespace TransportReference;
    auto Pack = [](SpatialVector P) { return std::array<float, 4>{float(P.X), float(P.Y), float(P.Z), 0}; };
    LightComputeFacet F;
    F.A = Pack(A); F.B = Pack(B); F.C = Pack(C);
    F.NormalA = F.NormalB = F.NormalC = Pack(Unit(Cross(Sub(B, A), Sub(C, A))));
    F.AlbedoRoughness = {.5f, .5f, .5f, .4f};
    F.OpticalToken = {0, 1.5f, .5f, float(Token)};
    F.Emissive[3] = float(Material);
    Scene.Surfaces.push_back(F);
}
void MaterialRectangle(LightComputeArchive& Scene, SpatialVector Centre, SpatialVector U, SpatialVector V,
                       uint32_t Material, uint32_t Token)
{
    using namespace TransportReference;
    auto A = Sub(Sub(Centre, U), V), B = Add(Sub(Centre, V), U), C = Add(Add(Centre, U), V), D = Add(Sub(Centre, U), V);
    MaterialTriangle(Scene, A, B, C, Material, Token);
    MaterialTriangle(Scene, A, C, D, Material, Token);
}
void MaterialCone(LightComputeArchive& Scene, double X, double Y, uint32_t Orange, uint32_t Band)
{
    using namespace TransportReference;
    constexpr uint32_t Slices = 64;
    for (uint32_t Ring = 0; Ring < 5; ++Ring)
    {
        constexpr double Heights[]{.08, .40, .56, .80, .96, 1.22};
        double Z0 = Heights[Ring], Z1 = Heights[Ring + 1];
        uint32_t Material = Ring == 1 || Ring == 3 ? Band : Orange;
        for (uint32_t I = 0; I < Slices; ++I)
        {
            auto Corner = [&](uint32_t J, double Z) {
                double Angle = 2 * std::numbers::pi * J / Slices, Radius = .44 - Z * .30;
                return SpatialVector{X + Radius * std::cos(Angle), Y + Radius * std::sin(Angle), Z};
            };
            auto A = Corner(I, Z0), B = Corner(I + 1, Z0), C = Corner(I + 1, Z1), D = Corner(I, Z1);
            auto First = Scene.Surfaces.size();
            MaterialTriangle(Scene, A, B, C, Material, 300);
            MaterialTriangle(Scene, A, C, D, Material, 300);
            for (auto At : {First, First + 1})
            {
                auto& F = Scene.Surfaces[At];
                auto Normal = [&](const auto& P) { auto N = Unit({P[0] - X, P[1] - Y, .30 * (.44 - P[2] * .30)});
                    return std::array<float, 4>{float(N.X), float(N.Y), float(N.Z), 0}; };
                F.NormalA = Normal(F.A); F.NormalB = Normal(F.B); F.NormalC = Normal(F.C);
            }
        }
    }
    MaterialRectangle(Scene, {X, Y, .06}, {.52, 0, 0}, {0, .52, 0}, Orange, 300);
}
void AppendMaterialPreview(LightComputeArchive& Scene, const SurfaceGeometry& Geometry,
                           SpatialVector Centre, double Scale, uint32_t Material)
{
    auto Pack = [&](const std::array<float, 3>& P, float W, bool Normal) {
        return std::array<float, 4>{float(P[0] * (Normal ? 1 : Scale) + (Normal ? 0 : Centre.X)),
            float(P[2] * (Normal ? 1 : Scale) + (Normal ? 0 : Centre.Y)),
            float(P[1] * (Normal ? 1 : Scale) + (Normal ? 0 : Centre.Z)), W};
    };
    for (size_t I = 0; I < Geometry.TriangleCorners.size(); I += 3)
    {
        LightComputeFacet F;
        for (uint32_t J = 0; J < 3; ++J)
        {
            const auto& P = Geometry.Corners[Geometry.TriangleCorners[I + J]];
            (J == 0 ? F.A : J == 1 ? F.B : F.C) = Pack(P.Position, P.Coordinates[0], false);
            (J == 0 ? F.NormalA : J == 1 ? F.NormalB : F.NormalC) = Pack(P.Normal, P.Coordinates[1], true);
        }
        F.AlbedoRoughness = {.5f, .5f, .5f, .4f}; F.OpticalToken = {0, 1.5f, .5f, 400};
        F.Emissive[3] = float(Material);
        Scene.Surfaces.push_back(F);
    }
}
void RebuildMaterialVisibility(LightComputeArchive& Scene)
{
    std::vector<std::array<SpatialVector, 3>> Triangles;
    for (const auto& F : Scene.Surfaces)
        Triangles.push_back({TransportReference::Vector(F.A), TransportReference::Vector(F.B), TransportReference::Vector(F.C)});
    TriangleVisibilitySpace Space;
    Space.Build(std::move(Triangles));
    Scene.Visibility = Space.PackFloat32();
    auto Previous = std::move(Scene.Surfaces);
    for (uint32_t I : Scene.Visibility.OriginalFacets) Scene.Surfaces.push_back(Previous[I]);
}
void MaterialCamera(LightComputeArchive& Scene, SpatialVector Eye, SpatialVector Focus, uint32_t Width, uint32_t Height)
{
    using namespace TransportReference;
    Scene.Width = Width; Scene.Height = Height; Scene.Receivers.resize(size_t(Width) * Height);
    auto F = Unit(Sub(Focus, Eye)), R = Unit(SpatialVector{F.Y, -F.X, 0}), U = Cross(R, F);
    Scene.Camera[0] = {float(Eye.X), float(Eye.Y), float(Eye.Z), .01f};
    Scene.Camera[1] = {float(F.X), float(F.Y), float(F.Z), 6000};
    Scene.Camera[2] = {float(R.X), float(R.Y), float(R.Z), float(.48 * Width / Height)};
    Scene.Camera[3] = {float(U.X), float(U.Y), float(U.Z), .48f};
    for (uint32_t I = 0; I < 4; ++I) Scene.Camera[4 + I] = Scene.Camera[I];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RENDERED VALIDATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Shared selectors preserve native vehicle material assignment and planar UVs across captures.
std::array<uint32_t,3> AssignVehicleSurfaces(LightComputeArchive& Scene, const std::array<uint32_t,5>& Materials)
{
    const uint32_t Green = Materials[0], Rubber = Materials[1], Glass = Materials[2];
    uint32_t PaintFacets = 0, RubberFacets = 0, GlassFacets = 0;
    for (auto& F : Scene.Surfaces)
    {
        const auto Colour = F.AlbedoRoughness;
        uint32_t Material = 0;
        if (std::abs(Colour[0] - .5304039f) < 1e-5 && std::abs(Colour[1] - .62435724f) < 1e-5)
            { Material = Green; ++PaintFacets; }
        if (std::abs(Colour[0] - .03741f) < 1e-5 && std::abs(Colour[3] - .76087f) < 1e-5)
            { Material = Rubber; ++RubberFacets; }
        if (std::abs(Colour[0] - .026f) < 1e-5 && std::abs(Colour[3] - .02f) < 1e-5)
            { Material = Glass; ++GlassFacets; }
        if (Material == 0) continue;
        F.Emissive[3] = float(Material);
        for (uint32_t J = 0; J < 3; ++J)
        {
            auto& P = J == 0 ? F.A : J == 1 ? F.B : F.C;
            auto& N = J == 0 ? F.NormalA : J == 1 ? F.NormalB : F.NormalC;
            // 📝 Archives have no UV stream here: documented object-planar mapping, not reconstructed author UVs.
            P[3] = P[0]; N[3] = P[2];
        }
    }
    Require(PaintFacets == 778 && GlassFacets == 74 && RubberFacets > 1000, "Vehicle material assignment changed");
    return {PaintFacets,RubberFacets,GlassFacets};
}

void ValidateMaterialScene(const char* Driver, const std::string& Shaders, LightComputeArchive Scene,
                           const std::string& Output)
{
    using namespace TransportReference;
    for (auto& L : Scene.Lights)
        if (L.RadianceCategory[3] == 0) L.RadianceCategory = {5.5f, 5.2f, 4.7f, 0};
    Prepare(Scene, 8, 24, true);
    auto Materials = AppendSceneMaterials(Scene, Shaders);
    uint32_t Green = Materials[0], Rubber = Materials[1], Glass = Materials[2], Plastic = Materials[3], Fabric = Materials[4];
    uint32_t Mirror = DuplicateSceneMaterial(Scene, Plastic, 1, {.94f, .94f, .94f, .04f}, 1);
    uint32_t Orange = DuplicateSceneMaterial(Scene, Plastic, 3, {.85f, .105f, .009f, .22f}, 0);
    uint32_t Band = DuplicateSceneMaterial(Scene, Plastic, 3, {.82f, .85f, .88f, .12f}, .65f);
    const auto [PaintFacets,RubberFacets,GlassFacets] = AssignVehicleSurfaces(Scene,Materials);
    MaterialCone(Scene, 2.8, -2.9, Orange, Band);
    MaterialCone(Scene, -.4, -3.5, Orange, Band);
    // 📝 Added actual reflective panes, one at each side; housing geometry remains the authored vehicle.
    for (double Side : {-1., 1.})
        MaterialRectangle(Scene, {.85, Side * 1.24, 1.30}, {.035, Side * .17, 0}, {0, 0, .09}, Mirror, 301);
    // 📝 Large calibration mirror makes off-screen scene reflection inspectable independently of tiny side panes.
    MaterialRectangle(Scene, {-3.5, .8, 1.25}, {.7, 0, 0}, {0, 0, .95}, Mirror, 302);
    MaterialRectangle(Scene, {3.25, -3.5, .9}, {.65, .4, 0}, {0, 0, .8}, Glass, 303);
    RebuildMaterialVisibility(Scene);
    MaterialCamera(Scene, {8, -10, 6}, {0, -.3, .9}, 640, 400);
    auto Compute = std::make_unique<LightComputeSubmission>(Driver);
    Compute->Prepare(Scene, Shaders);
    auto Render = [&](const std::string& Name) {
        auto Pixels = Compute->Execute(Scene, 0, 16, 71, 64, 0, true, true, 8, true);
        WriteImage(Output + "/" + Name + ".ppm", Pixels, Scene.Width, Scene.Height);
        uint32_t Mirrors = 0, MirrorHits = 0, Panes = 0, Screen = 0, Fallback = 0, Paint = 0, Tyres = 0;
        uint32_t SidePixels = 0, SideHits = 0, NativeMirrors = 0, NativeProbes = 0, Pixel = 0;
        double NativeError = 0;
        RaySpace Native(Scene);
        for (const auto& R : Compute->PrimaryReceivers())
        {
            uint32_t At = Pixel++;
            if (R.PositionHit[3] == 0) continue;
            if (R.ExpectedDirect[0] == 1)
            {
                SidePixels += R.MotionPositionToken[3] == 301;
                SideHits += R.MotionPositionToken[3] == 301 && R.ExpectedDirect[1] == 1;
                if (NativeMirrors < 64)
                {
                    auto N = Vector(R.NormalRoughness), V = Vector(R.ViewMetallic);
                    auto Direction = Sub(Mul(N, 2 * Dot(N, V)), V);
                    auto Hit = Native.Space.Trace(Add(Vector(R.PositionHit), Mul(N, .003)), Direction, .001, 1e30);
                    Require(Hit.Hit == (R.ExpectedDirect[1] == 1), "Mirror GPU/native BVH hit mismatch");
                    ++NativeMirrors;
                }
            }
            if (R.ExpectedDirect[3] > 0 && NativeProbes < 128 && At % 37 == 0)
            {
                uint32_t Address = uint32_t(R.ExpectedDirect[3]) - 1;
                PreviewScattering S;
                S.Configuration[0] = uint32_t(Scene.Angles[Address]);
                for (uint32_t I = 0; I < S.Configuration[0]; ++I)
                {
                    auto& Stratum = S.Strata[I];
                    uint32_t P = Address + 8 + I * 24;
                    std::copy_n(Scene.Angles.data() + P, 4, Stratum.AlbedoRoughness.begin());
                    std::copy_n(Scene.Angles.data() + P + 4, 4, Stratum.Optics.begin());
                    std::copy_n(Scene.Angles.data() + P + 8, 4, Stratum.AbsorptionThickness.begin());
                    Stratum.Classification[0] = uint32_t(Scene.Angles[P + 12]);
                    std::copy_n(Scene.Angles.data() + P + 16, 4, Stratum.Sheen.begin());
                    std::copy_n(Scene.Angles.data() + P + 20, 4, Stratum.SheenFinish.begin());
                }
                S.Strata[0].AlbedoRoughness = {R.AlbedoIor[0], R.AlbedoIor[1], R.AlbedoIor[2], R.NormalRoughness[3]};
                S.Strata[0].Optics[0] = R.ViewMetallic[3];
                if (Scene.Angles[Address + 1] == 2)
                {
                    S.Strata[0].AlbedoRoughness[0] = S.Strata[0].AlbedoRoughness[1] = S.Strata[0].AlbedoRoughness[2] = .04f;
                    S.Strata[0].Optics[0] = 1;
                }
                std::array<double, 3> Expected{R.ResidualSpecular[0], R.ResidualSpecular[1], R.ResidualSpecular[2]};
                auto N = Unit(Vector(R.NormalRoughness)), V = Unit(Vector(R.ViewMetallic));
                for (const auto& Light : Native.Sources)
                {
                    SpatialVector L;
                    double Distance;
                    auto Arrival = ReferenceLightArrival(Light, Vector(R.PositionHit), L, Distance);
                    if (Dot(N, L) <= 0) continue;
                    auto Origin = Add(Vector(R.PositionHit), Mul(N, .002));
                    double Far = Distance - .004;
                    std::array<double, 3> Shadow{1, 1, 1};
                    for (uint32_t Pane = 0; Pane < 8; ++Pane)
                    {
                        auto Hit = Native.Space.Trace(Origin, L, .001, Far);
                        if (!Hit.Hit) break;
                        const auto& F = Scene.Surfaces[Hit.Facet];
                        uint32_t M = F.Emissive[3] > 0 ? uint32_t(F.Emissive[3]) : 0;
                        if (!M || Scene.Angles[M] != 2) { Shadow = {}; break; }
                        auto Normal = Unit(Vector(F.NormalA));
                        double Cosine = std::max(.05, std::abs(Dot(Normal, L)));
                        double Fresnel = .04 + .96 * std::pow(1 - Cosine, 5);
                        std::array<double, 3> Absorption{.8, .15, .4};
                        for (uint32_t C = 0; C < 3; ++C)
                            Shadow[C] *= (1 - Fresnel) / (1 + Fresnel) * std::exp(-Absorption[C] * .006 / Cosine);
                        Origin = Add(Origin, Mul(L, Hit.Travel + .003));
                        Far -= Hit.Travel + .003;
                        if (Far <= .001) break;
                        if (Pane == 7) Shadow = {};
                    }
                    auto H = Unit(Add(V, L));
                    auto Brdf = EvaluateScatteringSequence(S, Dot(N, V), Dot(N, L), std::clamp(Dot(N, H), 0., 1.), std::clamp(Dot(V, H), 0., 1.));
                    std::array<double, 3> Incident{Arrival.R, Arrival.G, Arrival.B};
                    for (uint32_t C = 0; C < 3; ++C) Expected[C] += Brdf[C] * Incident[C] * Shadow[C] * Dot(N, L);
                }
                for (uint32_t C = 0; C < 3; ++C)
                    NativeError = std::max(NativeError, std::abs(Expected[C] - Pixels[At][C]) / (1 + std::abs(Expected[C])));
                ++NativeProbes;
            }
            Mirrors += R.ExpectedDirect[0] == 1; MirrorHits += R.ExpectedDirect[0] == 1 && R.ExpectedDirect[1] == 1;
            Panes += R.ExpectedDirect[0] == 2; Screen += R.ExpectedDirect[0] == 3; Fallback += R.ExpectedDirect[0] == 4;
            Paint += R.ExpectedDirect[3] == Green; Tyres += R.ExpectedDirect[3] == Rubber;
        }
        std::cout << "MATERIAL_SCENE name=" << Name << " width=" << Scene.Width << " height=" << Scene.Height
                  << " facets=" << Scene.Surfaces.size() << " paint_facets=" << PaintFacets
                  << " rubber_facets=" << RubberFacets << " glass_facets=" << GlassFacets
                  << " mirror_pixels=" << Mirrors << " mirror_scene_hits=" << MirrorHits << " glass_pixels=" << Panes
                  << " ssr_hits=" << Screen << " ssr_bvh_fallback=" << Fallback << " paint_pixels=" << Paint
                  << " rubber_pixels=" << Tyres << " side_mirror_pixels=" << SidePixels << " side_mirror_hits=" << SideHits
                  << " native_mirror_probes=" << NativeMirrors << " native_brdf_probes=" << NativeProbes
                  << " native_brdf_scaled_error=" << NativeError << std::endl;
        Require(NativeProbes > 0 && NativeError < .005, "Reused native/Slang material response mismatch");
        if (Name == "MaterialVehicle")
            Require(SidePixels > 0 && MirrorHits > 0 && Panes > 0 && Screen > 0 && Fallback > 0,
                    "Missing requested material/reflection coverage");
        return Pixels;
    };
    auto Vehicle = Render("MaterialVehicle");
    auto Repeated = Render("MaterialVehicleRepeat");
    Require(Vehicle == Repeated, "Material scene did not repeat deterministically");
    MaterialCamera(Scene, {2, -3, 1.5}, {.4, -.8, .95}, 640, 400);
    Render("MaterialVehicleDetail");
    // 📝 Keep the same ground and lighting; the next capture isolates the reused C++ specimen geometry.
    Scene.Surfaces.erase(std::remove_if(Scene.Surfaces.begin(), Scene.Surfaces.end(), [](const auto& F) {
        return F.OpticalToken[3] > 3;
    }), Scene.Surfaces.end());
    AppendMaterialPreview(Scene, ConstructMaterialPreviewShaderBall(), {-1.25, 0, 1}, 1.15, Green);
    AppendMaterialPreview(Scene, ConstructMaterialPreviewCloth(), {1.15, 0, 1}, 1.15, Fabric);
    RebuildMaterialVisibility(Scene);
    MaterialCamera(Scene, {4, -7, 3.7}, {0, 0, .9}, 640, 400);
    Compute = std::make_unique<LightComputeSubmission>(Driver);
    Compute->Prepare(Scene, Shaders);
    Render("MaterialSpecimens");
}
