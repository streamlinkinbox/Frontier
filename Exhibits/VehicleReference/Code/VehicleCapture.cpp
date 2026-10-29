/*==========================================================================================================================================
                                                              VEHICLECAPTURE.CPP
==========================================================================================================================================*/
// 🧩 Authored ControlVehicle archives and material records rendered through the
// native CPU reference path.

#include "../../LightCompute/Code/LightComputeFormat.h"
#include "Engine/Rendering/ReferenceRenderer.h"
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace Frontier;

//------------------------------------------------------------------------------------------------------------------------
//                                                       ARCHIVE DECODE
//------------------------------------------------------------------------------------------------------------------------

template <typename Scalar> Scalar Read(std::ifstream& Stream)
{
    Scalar Number{};
    Stream.read(reinterpret_cast<char*>(&Number), sizeof(Number));
    return Number;
}

void AppendArchive(ReferenceScene& Scene, const std::string& Path, SpatialVector Translation, double Side,
                   double θ, const std::vector<ReferenceMaterial>& Materials,
                   const std::vector<SpectrumRgb>& Glow, uint32_t& FacetCount)
{
    std::ifstream Stream(Path, std::ios::binary);
    Stream.exceptions(std::ios::failbit | std::ios::badbit);
    char Magic[4];
    Stream.read(Magic, 4);
    if (std::memcmp(Magic, "FCT1", 4))
        throw std::runtime_error("Unknown vehicle archive");
    const uint32_t Vertices = Read<uint32_t>(Stream), Facets = Read<uint32_t>(Stream);
    const uint32_t Features = Read<uint32_t>(Stream);
    if (!Vertices || !Facets || Vertices > 1000000 || Facets > 1000000 || Features > 1)
        throw std::runtime_error("Vehicle archive extent rejected");
    Stream.seekg(48);
    std::vector<SpatialVector> Positions(Vertices);
    for (auto& Point : Positions)
    {
        Point = {Read<float>(Stream), Side * Read<float>(Stream), Read<float>(Stream)};
        Point = {Point.X + Translation.X, Point.Y + Translation.Y, Point.Z + Translation.Z};
        const double X = Point.X, Y = Point.Y;
        Point.X = std::cos(θ) * X - std::sin(θ) * Y + .8 * θ;
        Point.Y = std::sin(θ) * X + std::cos(θ) * Y;
        if (!std::isfinite(Point.X) || !std::isfinite(Point.Y) || !std::isfinite(Point.Z))
            throw std::runtime_error("Nonfinite vehicle corner");
    }
    std::vector<uint32_t> Indices(Facets * 3), MaterialIds(Facets);
    Stream.read(reinterpret_cast<char*>(Indices.data()), std::streamsize(Indices.size() * 4));
    Stream.read(reinterpret_cast<char*>(MaterialIds.data()), std::streamsize(MaterialIds.size() * 4));
    std::map<uint32_t, ReferenceMesh> Surfaces;
    for (uint32_t Facet = 0; Facet < Facets; ++Facet)
    {
        uint32_t MaterialIndex = MaterialIds[Facet];
        // The authored rim archive retains the legacy identity 16; the material
        // table assigns the rim to 81.
        if (Path.find("Rim.facet") != std::string::npos && MaterialIndex == 16)
            MaterialIndex = 81;
        if (MaterialIndex >= Materials.size())
            throw std::runtime_error("Vehicle material outside authored table");
        auto& Surface = Surfaces[MaterialIndex];
        Surface.Material = Materials[MaterialIndex];
        Surface.Emissive = Glow[MaterialIndex];
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t Vertex = Indices[Facet * 3 + (Side < 0 ? 2 - Corner : Corner)];
            if (Vertex >= Vertices)
                throw std::runtime_error("Vehicle archive index rejected");
            Surface.Geometry.Indices.push_back(uint32_t(Surface.Geometry.Vertices.size()));
            Surface.Geometry.Vertices.push_back(Positions[Vertex]);
        }
    }
    for (auto& Surface : Surfaces)
        Scene.Meshes.push_back(std::move(Surface.second));
    FacetCount += Facets;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      NATIVE CAPTURE
//------------------------------------------------------------------------------------------------------------------------

int main(int Count, char** Arguments)
{
    try
    {
        if (Count != 2 && !(Count == 3 && (std::string(Arguments[2]) == "--clustered-lights" ||
                                           std::string(Arguments[2]) == "--vulkan-input" ||
                                           std::string(Arguments[2]) == "--motion-input" ||
                                           std::string(Arguments[2]) == "--primary-input" ||
                                           std::string(Arguments[2]) == "--primary-motion")))
            throw std::runtime_error(
                "Usage: VehicleCapture output-directory "
                "[--clustered-lights|--vulkan-input|--motion-input|--primary-input|--primary-motion]");
        std::filesystem::path Output = Arguments[1];
        std::filesystem::create_directories(Output);
        std::ifstream Table("_ClaudeScratch/vehicle-reference/Materials.txt");
        std::vector<ReferenceMaterial> Materials;
        std::vector<SpectrumRgb> Glow;
        ReferenceMaterial Material;
        SpectrumRgb Radiance;
        while (Table >> Material.Albedo.R >> Material.Albedo.G >> Material.Albedo.B >> Material.Roughness >>
               Material.Metallic >> Radiance.R >> Radiance.G >> Radiance.B)
        {
            Materials.push_back(Material);
            Glow.push_back(Radiance);
        }
        if (Materials.size() != 86)
            throw std::runtime_error("Expected 86 authored material records");
        const bool Analytic = Count == 3;
        const bool PrimaryMotion = Count == 3 && std::string(Arguments[2]) == "--primary-motion";
        const bool PrimaryInput =
            PrimaryMotion || (Count == 3 && std::string(Arguments[2]) == "--primary-input");
        const bool MotionInput =
            PrimaryMotion || (Count == 3 && std::string(Arguments[2]) == "--motion-input");
        const bool ComputeInput =
            Count == 3 && (std::string(Arguments[2]) == "--vulkan-input" || MotionInput || PrimaryInput);
        CullCamera PreviousCamera;
        double PreviousAngle = 0;
        for (uint32_t Capture = 0; Capture < (MotionInput ? 6u : (ComputeInput ? 1u : (Analytic ? 2u : 6u)));
             ++Capture)
        {
            const uint32_t Pose = MotionInput ? Capture : Analytic ? 0 : Capture % 3;
            const bool WithDistance = !Analytic && Capture >= 3;
            const std::string Stem = MotionInput    ? "MotionInput"
                                     : ComputeInput ? "VulkanInput"
                                     : Analytic     ? (Capture == 0 ? "AnalyticAll" : "AnalyticCluster")
                                                    : (WithDistance ? "Distance" : "Vehicle");
            const double θ = Pose * (MotionInput ? .08 : .45);
            ReferenceScene Scene;
            std::array<ReferenceMesh, 2> Ground;
            Ground[0].Material.Albedo = {.6, .62, .65};
            Ground[1].Material.Albedo = {.045, .055, .07};
            for (int Y = -20; Y < 20; ++Y)
                for (int X = -20; X < 20; ++X)
                {
                    auto& Geometry = Ground[uint32_t((X + Y + 40) % 2)].Geometry;
                    uint32_t First = uint32_t(Geometry.Vertices.size());
                    Geometry.Vertices.insert(Geometry.Vertices.end(), {{2. * X, 2. * Y, 0},
                                                                       {2. * X + 2, 2. * Y, 0},
                                                                       {2. * X + 2, 2. * Y + 2, 0},
                                                                       {2. * X, 2. * Y + 2, 0}});
                    Geometry.Indices.insert(Geometry.Indices.end(),
                                            {First, First + 1, First + 2, First, First + 2, First + 3});
                }
            for (auto& Surface : Ground)
                Scene.Meshes.push_back(std::move(Surface));
            ReferenceMesh ExteriorGround;
            ExteriorGround.Material.Albedo = {.32, .34, .37};
            for (auto Rectangle :
                 {std::array<double, 4>{-2000, -2000, -40, 2000},
                  std::array<double, 4>{40, -2000, 2000, 2000}, std::array<double, 4>{-40, -2000, 40, -40},
                  std::array<double, 4>{-40, 40, 40, 2000}})
            {
                auto& Geometry = ExteriorGround.Geometry;
                const uint32_t First = uint32_t(Geometry.Vertices.size());
                Geometry.Vertices.insert(Geometry.Vertices.end(), {{Rectangle[0], Rectangle[1], 0},
                                                                   {Rectangle[2], Rectangle[1], 0},
                                                                   {Rectangle[2], Rectangle[3], 0},
                                                                   {Rectangle[0], Rectangle[3], 0}});
                Geometry.Indices.insert(Geometry.Indices.end(),
                                        {First, First + 1, First + 2, First, First + 2, First + 3});
            }
            Scene.Meshes.push_back(std::move(ExteriorGround));
            const std::string Archive = "Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/"
                                        "Archive/ControlVehicle";
            const size_t VehicleFirst = Scene.Meshes.size();
            uint32_t Facets = 0;
            AppendArchive(Scene, Archive + "Body.facet", {0, 0, .41715}, 1, θ, Materials, Glow, Facets);
            for (auto Mount :
                 {SpatialVector{1.7274, 1.0475, .50855}, SpatialVector{1.7274, -1.0475, .50855},
                  SpatialVector{-1.6686, 1.0475, .50855}, SpatialVector{-1.6686, -1.0475, .50855}})
                for (auto Part : {"Tyre", "Rim"})
                    AppendArchive(Scene, Archive + Part + ".facet", Mount, Mount.Y < 0 ? -1 : 1, θ, Materials,
                                  Glow, Facets);
            if (Facets != 25068)
                throw std::runtime_error("ControlVehicle facet count changed");
            const SpatialVector Sun{.35, -.55, .7582875444};
            Scene.Lights.push_back({ReferenceLight::Kind::Directional, Sun, {5.5, 5.2, 4.7}});
            if (Analytic)
            {
                Scene.Lights[0].Radiance = {.2, .2, .2};
                ReferenceLight Point;
                Point.Kind = ReferenceLight::Kind::Point;
                Point.Vector = {1, -3, 3};
                Point.Radiance = {35, 80, 120};
                Point.Reach = 12;
                if (MotionInput && Capture >= 3)
                    Point.Radiance = {100, 25, 15};
                Scene.Lights.push_back(Point);
                ReferenceLight Spot;
                Spot.Kind = ReferenceLight::Kind::Spot;
                Spot.Vector = {-3, -1, 4};
                Spot.Radiance = {150, 75, 20};
                Spot.Reach = 10;
                Spot.Direction = {0, 0, -1};
                Spot.InnerCosine = std::cos(.25);
                Spot.OuterCosine = std::cos(.55);
                Scene.Lights.push_back(Spot);
                ReferenceLight Profile;
                Profile.Kind = ReferenceLight::Kind::Point;
                Profile.Vector = {3, 3, 4};
                Profile.Direction = {-.5, -.5, -1};
                Profile.Radiance = {140, 140, 130};
                Profile.Reach = 12;
                std::ifstream Ies("Exhibits/ClusteredLighting/Fixtures/Synthetic.ies");
                Profile.Photometry = std::make_shared<CandelaDistribution>(CandelaDistribution::Read(Ies));
                Scene.Lights.push_back(Profile);
                for (uint32_t Index = 0; Index < 32; ++Index)
                {
                    ReferenceLight Remote;
                    Remote.Vector = {40 + 2. * Index, 0, 3};
                    Remote.Radiance = {1, 1, 1};
                    Remote.Reach = 1;
                    Scene.Lights.push_back(Remote);
                }
            }
            if (WithDistance)
            {
                std::vector<std::array<SpatialVector, 3>> SurfaceFacets;
                std::vector<const ReferenceMesh*> FacetSurfaces;
                for (size_t Surface = VehicleFirst; Surface < Scene.Meshes.size(); ++Surface)
                {
                    const auto& Geometry = Scene.Meshes[Surface].Geometry;
                    for (size_t Slot = 0; Slot < Geometry.Indices.size(); Slot += 3)
                    {
                        SurfaceFacets.push_back({Geometry.Vertices[Geometry.Indices[Slot]],
                                                 Geometry.Vertices[Geometry.Indices[Slot + 1]],
                                                 Geometry.Vertices[Geometry.Indices[Slot + 2]]});
                        FacetSurfaces.push_back(&Scene.Meshes[Surface]);
                    }
                }
                auto Volume = std::make_shared<TriangleDistanceVolume>();
                Volume->Bake(SurfaceFacets, {64, 32, 24});
                TriangleVisibilitySpace Visibility;
                Visibility.Build(SurfaceFacets);
                for (size_t Facet = 0; Facet < SurfaceFacets.size(); ++Facet)
                {
                    const auto& F = SurfaceFacets[Facet];
                    const auto& Surface = *FacetSurfaces[Facet];
                    const SpatialVector E{F[1].X - F[0].X, F[1].Y - F[0].Y, F[1].Z - F[0].Z};
                    const SpatialVector G{F[2].X - F[0].X, F[2].Y - F[0].Y, F[2].Z - F[0].Z};
                    SpatialVector N{E.Y * G.Z - E.Z * G.Y, E.Z * G.X - E.X * G.Z, E.X * G.Y - E.Y * G.X};
                    const double Length = std::hypot(N.X, N.Y, N.Z);
                    if (Length > 0)
                        N = {N.X / Length, N.Y / Length, N.Z / Length};
                    const SpatialVector Centre{(F[0].X + F[1].X + F[2].X) / 3, (F[0].Y + F[1].Y + F[2].Y) / 3,
                                               (F[0].Z + F[1].Z + F[2].Z) / 3};
                    const SpatialVector Origin{Centre.X + .002 * N.X, Centre.Y + .002 * N.Y,
                                               Centre.Z + .002 * N.Z};
                    const double Cosine = Visibility.Occluded(Origin, Sun, 0, 200)
                                              ? 0
                                              : std::max(0.0, N.X * Sun.X + N.Y * Sun.Y + N.Z * Sun.Z);
                    const double Diffuse = (1 - Surface.Material.Metallic) * Cosine / 3.141592653589793;
                    Scene.TriangleRadiance.push_back(
                        {Surface.Emissive.R + Surface.Material.Albedo.R * 5.5 * Diffuse,
                         Surface.Emissive.G + Surface.Material.Albedo.G * 5.2 * Diffuse,
                         Surface.Emissive.B + Surface.Material.Albedo.B * 4.7 * Diffuse});
                }
                Scene.TriangleDistance = Volume;
                std::ofstream Grid(Output / ("Distance" + std::to_string(Pose) + ".bin"), std::ios::binary);
                Grid.exceptions(std::ios::failbit | std::ios::badbit);
                Grid.write(reinterpret_cast<const char*>(Volume->Distances().data()),
                           std::streamsize(Volume->SampleCount() * sizeof(double)));
                std::cout << "Unsigned triangle distance bake: " << Volume->SampleCount() << " samples; "
                          << Volume->FacetCount() << " original facets; exact ray refinement enabled"
                          << std::endl;
            }
            ReferenceRenderer Renderer;
            const auto ComputeScene = ComputeInput ? Scene : ReferenceScene{};
            if (!PrimaryInput)
                Renderer.SetScene(std::move(Scene));
            CullCamera Camera;
            Camera.Position = {8, -9, 4};
            const SpatialVector Target{.8 * θ, 0, .9};
            auto Forward = SpatialVector{Target.X - Camera.Position.X, Target.Y - Camera.Position.Y,
                                         Target.Z - Camera.Position.Z};
            double Length = std::sqrt(Forward.X * Forward.X + Forward.Y * Forward.Y + Forward.Z * Forward.Z);
            Camera.Forward = {Forward.X / Length, Forward.Y / Length, Forward.Z / Length};
            Length = std::hypot(Camera.Forward.X, Camera.Forward.Y);
            Camera.Right = {Camera.Forward.Y / Length, -Camera.Forward.X / Length, 0};
            Camera.Up = {Camera.Right.Y * Camera.Forward.Z, -Camera.Right.X * Camera.Forward.Z,
                         Camera.Right.X * Camera.Forward.Y - Camera.Right.Y * Camera.Forward.X};
            Camera.FovYRadians = .65;
            Camera.Aspect = 1.6;
            Camera.NearPlane = .1;
            Camera.FarPlane = 4000;
            ReferenceRenderSettings Settings;
            Settings.Width = 640;
            Settings.Height = 400;
            Settings.AccumulationFrames = 8;
            Settings.RisCandidates = 4;
            Settings.IndirectSamples = 0;
            Settings.EnvironmentSamples = 8;
            Settings.SunDirection = Sun;
            Settings.CloudsEnabled = true;
            Settings.Clouds.WindTimeS = 5 * Pose;
            if (Analytic)
            {
                Settings.ExhaustiveLights = true;
                Settings.ClusteredLights = Capture == 1;
                Settings.SpatialReuse = false;
                Settings.AccumulationFrames = 1;
                Settings.SunIrradiance = {.2, .22, .3};
            }
            if (ComputeInput)
            {
                Settings.Denoise = false;
                Settings.StoreSurfaceSamples = true;
                Settings.ClusteredLights = false;
            }
            ReferenceImage Image;
            if (PrimaryInput)
            {
                Image.Width = Settings.Width;
                Image.Height = Settings.Height;
                Image.Pixels.resize(size_t(Image.Width) * Image.Height);
                Image.SurfaceSamples.resize(Image.Pixels.size());
            }
            else
                Image = Renderer.Render(Camera, Settings);
            if (ComputeInput)
            {
                auto Packed = PackLightCompute(ComputeScene, Image, &Camera);
                if (MotionInput)
                    ProjectLightComputeMotion(Packed, Capture ? PreviousCamera : Camera, θ,
                                              Capture ? PreviousAngle : θ, uint32_t(VehicleFirst));
                Packed.Save((Output / (MotionInput    ? ("VehicleMotion" + std::to_string(Capture) + ".bin")
                                       : PrimaryInput ? "VehiclePrimary.bin"
                                                      : "VehicleLightInput.bin"))
                                .string());
                PreviousCamera = Camera;
                PreviousAngle = θ;
            }
            if (!PrimaryInput && (!Image.Stats.SurfacePixels || !std::isfinite(Image.Stats.MeanLuminance)))
                throw std::runtime_error("Vehicle capture is empty or nonfinite");
            for (auto Pixel : Image.Pixels)
                if (!std::isfinite(Pixel.R) || !std::isfinite(Pixel.G) || !std::isfinite(Pixel.B))
                    throw std::runtime_error("Nonfinite HDR vehicle pixel");
            std::vector<uint8_t> Rgb;
            EncodeDisplayRgb8(Image, .45, Rgb);
            std::ofstream Stream(Output / (Stem + std::to_string(Pose) + ".ppm"), std::ios::binary);
            Stream.exceptions(std::ios::failbit | std::ios::badbit);
            Stream << "P6\n640 400\n255\n";
            Stream.write(reinterpret_cast<const char*>(Rgb.data()), std::streamsize(Rgb.size()));
            std::cout << (PrimaryInput ? "Geometry export cpu_primary_raster=0 distance="
                                       : "CPU reference distance=")
                      << WithDistance << " pose=" << Pose << " original_vehicle_facets=" << Facets
                      << " total_facets=" << Image.Stats.TrianglesTotal
                      << " visible=" << Image.Stats.TrianglesVisible
                      << " light_list_visits=" << Image.Stats.LightListVisits
                      << " light_fallbacks=" << Image.Stats.LightFallbackQueries
                      << " surface_pixels=" << Image.Stats.SurfacePixels
                      << " hash=" << ReferenceImageHash(Image) << std::endl;
        }
    }
    catch (const std::exception& Error)
    {
        std::cerr << Error.what() << '\n';
        return 1;
    }
}
