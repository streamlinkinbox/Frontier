/*==========================================================================================================================================
                                                       XRAYSCANCAPTURE.CPP
==========================================================================================================================================*/
// 🧩 The C++ X-ray scan overlay on its own Vulkan device, over the hero car of the proving ground: the scan's clock sweeps the car from the nose
//    to the tail and the ghost shell, the wireframe, the curtain and the particles are drawn by the programs of the XRayScan archive set.
//
//     XRayScanCapture <repository root> <overlay shader folder> <stand-in shader folder> <output folder>
//
// 📝 What is real and what stands in. Real: the car's facets (EngineContent/EnvironmentArchives/ProvingGround), the vehicle's profile, the scan's
//    clock, the feature edges, the particle cloud, the profile records, the overlay programs and their draws. A stand-in: the solid picture under
//    the overlay. The illumination port's ReSTIR pipeline needs the whole scene's acceleration structures; this capture draws the car flat with
//    XRayScanCaptureSolid.slang instead, which does take the engine's vertex deformation and the scan's stochastic fade, and averages sixteen
//    refreshes of it the way temporal antialiasing averages the fade's coin flips. The images show the overlay, not the port's lighting.

#include "Frontier/Engine/Rendering/GlobalIllumination/IlluminationConstants.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/RefreshEncoding.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/SceneStructure.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/ShaderInterchange.h"
#include "Frontier/Engine/Rendering/XRayScan/XRayScanEdges.h"
#include "Frontier/Engine/Rendering/XRayScan/XRayScanOverlaySequence.h"
#include "Frontier/Engine/Rendering/XRayScan/XRayScanSparkles.h"

#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Frontier;
using namespace Frontier::GlobalIllumination;
using namespace Frontier::XRayScan;

namespace
{

constexpr uint32_t Width        = 640;           // [px]
constexpr uint32_t Height       = 400;           // [px]
constexpr uint32_t Accumulation = 16;            // [-]  stand-in refreshes averaged into one picture
constexpr double   RefreshStep  = 1.0 / 60.0;    // [s]
constexpr std::array<float, 4> Backdrop = {0.035f, 0.05f, 0.08f, 1.0f};

// 📝 One capture: the seconds into the scan's cycle (0 leaves the scan off). The cycle is 4.6 s of sweep, 1 s of hold and 1 s of fade.
struct ScanShot
{
    const char* Label;
    double      Seconds;   // [s]
};

std::vector<uint8_t> ReadBytes(const std::filesystem::path& Path)
{
    std::ifstream Stream(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
}

std::vector<ScanFacet> ConvertFacets(const FacetSet& Set)
{
    std::vector<ScanFacet> Facets;
    Facets.reserve(Set.Facets.size());
    for (const FacetEntry& Facet : Set.Facets)
    {
        Facets.push_back({Facet.Corners, Facet.Family});
    }
    return Facets;
}

bool WritePicture(const std::filesystem::path& Path, const std::vector<uint8_t>& Rgba, uint32_t PictureWidth, uint32_t PictureHeight)
{
    std::vector<uint8_t> Rgb(static_cast<size_t>(PictureWidth) * PictureHeight * 3u);
    for (size_t Pixel = 0; Pixel < static_cast<size_t>(PictureWidth) * PictureHeight; ++Pixel)
    {
        std::copy_n(&Rgba[Pixel * 4], 3, &Rgb[Pixel * 3]);
    }
    return stbi_write_png(Path.string().c_str(), static_cast<int>(PictureWidth), static_cast<int>(PictureHeight), 3, Rgb.data(),
                          static_cast<int>(PictureWidth * 3u)) != 0;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 5)
    {
        std::fprintf(stderr, "usage: XRayScanCapture <repository root> <overlay shader folder> <stand-in shader folder> <output folder>\n");
        return 2;
    }
    const std::filesystem::path Root           = Arguments[1];
    const std::filesystem::path OverlayFolder  = Arguments[2];
    const std::filesystem::path StandInFolder  = Arguments[3];
    const std::filesystem::path Output         = Arguments[4];
    std::filesystem::create_directories(Output);
    std::string Diagnostic;

    // ① the car: the engine's own facet space, the body and its four wheels
    const std::filesystem::path Ground = Root / "EngineContent/EnvironmentArchives/ProvingGround";
    SceneStructure              Scene;
    if (!Scene.LoadFacetSpace(Ground / "ProvingGround.facetspace", &Diagnostic))
    {
        std::fprintf(stderr, "🔴 scene: %s\n", Diagnostic.c_str());
        return 1;
    }
    const std::array<int32_t, 5> Vehicle = {Scene.VehicleBody, Scene.VehicleWheels[0], Scene.VehicleWheels[1], Scene.VehicleWheels[2], Scene.VehicleWheels[3]};
    for (uint32_t Slot = 0; Slot < Vehicle.size(); ++Slot)
    {
        if (Vehicle[Slot] != static_cast<int32_t>(Slot))
        {
            std::fprintf(stderr, "🔴 the surface raster expects the body and four wheels to lead the occupant list\n");
            return 1;
        }
    }
    const std::vector<uint8_t> Stream = Scene.EncodeVertexStream();
    std::vector<ScanShellRun>  Shell;
    std::vector<uint32_t>      Instances;
    std::vector<ScanOccupantSource> Sources;
    uint32_t                   VertexEnd = 0;
    for (uint32_t Slot = 0; Slot < Vehicle.size(); ++Slot)
    {
        const uint32_t  SetIndex = Scene.Occupants[Vehicle[Slot]].FacetSet;
        const FacetSet& Set      = Scene.FacetSets[SetIndex];
        Shell.push_back({Set.FirstVertex, Set.VertexCount, Slot, 1u});
        Instances.push_back(static_cast<uint32_t>(Vehicle[Slot]));
        Sources.push_back({static_cast<uint32_t>(Vehicle[Slot]), SetIndex});
        VertexEnd = std::max(VertexEnd, Set.FirstVertex + std::max(Set.VertexCount, Set.VertexCapacity));
    }
    std::vector<std::vector<ScanFacet>>        Converted(Scene.FacetSets.size());
    std::vector<std::span<const ScanFacet>>    Spans(Scene.FacetSets.size());
    for (const ScanOccupantSource& Source : Sources)
    {
        if (Converted[Source.FacetSet].empty())
        {
            Converted[Source.FacetSet] = ConvertFacets(Scene.FacetSets[Source.FacetSet]);
            Spans[Source.FacetSet]     = Converted[Source.FacetSet];
        }
    }
    const ScanEdgeRecords    Edges    = ConstructScanEdges(Sources, Spans);
    const std::vector<float> Sparkles = ConstructSparkleCloud();
    std::printf("🧩 the car: %zu draws, %u wireframe segments, %zu particles\n", Shell.size(), Edges.Count, Sparkles.size() / 4);

    // ② the device and the programs
    DeviceExchange Exchange;
    if (!Exchange.Initialize("", &Diagnostic))
    {
        std::fprintf(stderr, "🔴 device: %s\n", Diagnostic.c_str());
        return 1;
    }
    std::printf("🧩 XRayScanCapture on %s\n", Exchange.AdapterDescription().c_str());
    ShaderInterchange OverlayShaders(Exchange, OverlayFolder);
    ShaderInterchange StandInShaders(Exchange, StandInFolder);
    const std::vector<SpecializationConstant> Constants = ConstructSpecializationConstants({128u, 0u, 0u, static_cast<uint32_t>(Scene.GroundOccupant)});
    OverlayShaders.ConfigureConstants(Constants);
    StandInShaders.ConfigureConstants(Constants);

    // ③ the resources of the surface raster's group: refresh constants, occupant profiles, tyre marks, effect and vehicle profiles
    auto RefreshRegion  = Exchange.ConstructRegion(1072, "Refresh constants");
    const std::vector<uint8_t> Profiles = Scene.EncodeOccupantRange(nullptr, 0, static_cast<uint32_t>(Vehicle.size()));
    auto OccupantRegion = Exchange.ConstructRegionFrom(Profiles.data(), Profiles.size(), "Occupant profiles");
    const std::vector<uint8_t> EffectBytes  = ReadBytes(Ground / "EffectProfile.bin");
    const std::vector<uint8_t> VehicleBytes = ReadBytes(Ground / "VehicleProfile.bin");
    if (EffectBytes.size() != 1120 || VehicleBytes.size() != 10448)
    {
        std::fprintf(stderr, "🔴 the proving ground's effect and vehicle profiles are missing or of the wrong size\n");
        return 1;
    }
    auto EffectRegion   = Exchange.ConstructRegionFrom(EffectBytes.data(), EffectBytes.size(), "Effect profile");
    auto VehicleRegion  = Exchange.ConstructRegionFrom(VehicleBytes.data(), VehicleBytes.size(), "Vehicle profile");
    auto VertexRegion   = Exchange.ConstructRegionFrom(Stream.data(), static_cast<VkDeviceSize>(VertexEnd) * VertexStride, "Car vertices");
    auto InstanceRegion = Exchange.ConstructRegionFrom(Instances.data(), Instances.size() * sizeof(uint32_t), "Car occupants");
    auto StandInView    = Exchange.ConstructRegion(ScanViewBytes, "Stand-in scan view");

    TextureSpecification Deposit;
    Deposit.Label  = "Rubber deposit (idle)";
    Deposit.Format = TextureFormat::Rgba8Unorm;
    Deposit.Usage  = TextureUsage::Sampled | TextureUsage::CopyTarget;
    auto DepositTexture = Exchange.ConstructTexture(Deposit);
    const std::array<uint8_t, 4> Untouched = {0, 0, 0, 0};
    Exchange.WriteTexture(DepositTexture, 0, Untouched.data(), Untouched.size(), 1, 1);
    SamplerSpecification DepositSampling;
    DepositSampling.MagFilter = DepositSampling.MinFilter = FilterMode::Linear;
    auto DepositSampler = Exchange.ConstructSampler(DepositSampling);

    // ④ the targets: the stand-in's colour and depth, and the picture the overlay is drawn over
    TextureSpecification Colour;
    Colour.Width  = Width;
    Colour.Height = Height;
    Colour.Format = TextureFormat::Rgba8Unorm;
    Colour.Usage  = TextureUsage::RenderAttachment | TextureUsage::CopySource | TextureUsage::CopyTarget;
    Colour.Label  = "Stand-in solid";
    auto SolidTexture   = Exchange.ConstructTexture(Colour);
    Colour.Label  = "Scan picture";
    auto PictureTexture = Exchange.ConstructTexture(Colour);
    TextureSpecification DepthSpecification;
    DepthSpecification.Width  = Width;
    DepthSpecification.Height = Height;
    DepthSpecification.Format = TextureFormat::Depth32Float;
    DepthSpecification.Usage  = TextureUsage::RenderAttachment | TextureUsage::Sampled;
    DepthSpecification.Label  = "Stand-in depth";
    auto DepthTexture   = Exchange.ConstructTexture(DepthSpecification);
    auto SolidView      = Exchange.ConstructView(SolidTexture);
    auto PictureView    = Exchange.ConstructView(PictureTexture);
    auto DepthView      = Exchange.ConstructView(DepthTexture);

    // ⑤ the stand-in's own group (the raster's six slots and the scan view) and program
    using D = DescriptorCode;
    const std::vector<DescriptorSlot> SolidSlots = {{D::Uniform}, {D::StorageRead}, {D::Texture2D}, {D::Sampler}, {D::Uniform}, {D::StorageRead}, {D::Uniform}};
    auto SolidLayout = Exchange.ConstructDescriptorLayout(SolidSlots, "Stand-in solid");
    auto SolidGroup  = Exchange.ConstructDescriptorGroup(SolidLayout, {RefreshRegion, OccupantRegion, Exchange.ConstructView(DepositTexture), DepositSampler,
                                                                       EffectRegion, VehicleRegion, StandInView},
                                                         "Stand-in solid");
    RasterProgramSpecification Solid;
    Solid.VertexEntry   = "SolidVertex";
    Solid.FragmentEntry = "SolidFragment";
    Solid.Topology      = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    Solid.CullMode      = VK_CULL_MODE_NONE;
    Solid.Targets       = {ColorTarget{TextureFormat::Rgba8Unorm}};
    Solid.Layouts       = {SolidLayout};
    Solid.DepthEnabled  = true;
    Solid.DepthFormat   = TextureFormat::Depth32Float;
    Solid.DepthWrite    = true;
    Solid.DepthCompare  = CompareFunction::Greater;
    Solid.VertexLayouts = {{VertexStride, false, {{0, 0, VertexFormat::Float32x3}, {1, 12, VertexFormat::Uint32}, {2, 16, VertexFormat::Float32x3}, {3, 28, VertexFormat::Uint32}}},
                           {4, true, {{4, 0, VertexFormat::Uint32}}}};
    std::shared_ptr<DeviceProgram> SolidProgram = StandInShaders.ConstructRasterProgram("XRayScanCaptureSolid", Solid);
    if (!SolidProgram)
    {
        std::fprintf(stderr, "🔴 stand-in program: %s\n", StandInShaders.LastDiagnostic().c_str());
        return 1;
    }

    // ⑥ the scan's overlay
    XRayScanOverlaySequence Overlay(Exchange, OverlayShaders);
    ScanRasterResources     Raster;
    Raster.Refresh         = RefreshRegion;
    Raster.Occupants       = OccupantRegion;
    Raster.TyreMarks       = Exchange.ConstructView(DepositTexture);
    Raster.TyreMarkSampler = DepositSampler;
    Raster.Effect          = EffectRegion;
    Raster.Vehicle         = VehicleRegion;
    if (!Overlay.Construct(TextureFormat::Rgba8Unorm, Raster, VertexRegion, InstanceRegion, Edges, Sparkles))
    {
        std::fprintf(stderr, "🔴 overlay programs: %s\n", Overlay.QueryDiagnostic().c_str());
        return 1;
    }
    Overlay.BindDepth(DepthView);

    // ⑦ the camera and the car's stage: the car stands at its spawn, facing +x with its right side toward −y
    const Transform& Placement = Scene.Occupants[0].Current;
    ScanStage        Stage;
    Stage.Origin  = {Placement[12], Placement[13], 0.0};
    Stage.Forward = TransformDirection(Placement, {1.0, 0.0, 0.0});
    Stage.Up      = {0.0, 0.0, 1.0};
    Stage.Right   = CrossVector(Stage.Forward, Stage.Up);
    CameraPose Camera;
    Camera.Target             = AddVector(Stage.Origin, {0.0, 0.0, 0.8});
    Camera.Position           = AddVector(AddVector(AddVector(Stage.Origin, ScaleVector(Stage.Forward, 5.2)), ScaleVector(Stage.Right, 6.0)), {0.0, 0.0, 2.5});
    Camera.Forward            = NormalizeVector(SubtractVector(Camera.Target, Camera.Position));
    Camera.VerticalFieldOfView = 0.5235987755982988;   // [rad] 30°
    Camera.Aspect             = static_cast<double>(Width) / Height;
    Camera.Near               = 0.1;
    const std::array<uint32_t, 5> Scanned = {0, 1, 2, 3, 4};
    RefreshEncoder Encoder;
    const LightSolution Light{};
    const RefreshToggles Toggles{};
    const RefreshExternalInputs External{};

    const std::vector<ScanShot> Shots = {{"Scan off", 0.0},        {"Sweep 0.25 s", 0.25}, {"Sweep 0.9 s", 0.9}, {"Sweep 1.6 s", 1.6}, {"Sweep 2.3 s", 2.3},
                                         {"Sweep 3.0 s", 3.0},     {"Sweep 3.8 s", 3.8},   {"Sweep 4.8 s", 4.8}, {"Hold and fade 6.0 s", 6.0}};
    std::ofstream Ledger(Output / "Captures.tsv");
    Ledger << "capture\tseconds\tramp\tfront_m\tamount\tcurtain_wide\tcurtain_narrow\tshell_draws\tedge_draws\tcurtain_draws\tsparkle_draws\t"
              "solid_pixels\tscan_pixels\tsolid_refresh_ms\n";
    std::vector<std::vector<uint8_t>> Pictures;
    int Failures = 0;
    for (const ScanShot& Shot : Shots)
    {
        // 📝 A fresh clock per shot: the scan is turned on, then stepped at 60 Hz to the shot's seconds, as a session's frames would.
        XRayScanSequence Scan;
        if (Shot.Seconds > 0.0)
        {
            Scan.Assign(true);
            const uint32_t Steps = static_cast<uint32_t>(Shot.Seconds / RefreshStep + 0.5);
            for (uint32_t Step = 0; Step < Steps; ++Step) Scan.Advance(RefreshStep);
        }
        Overlay.Update(Scan, Stage, Scanned, Camera.Near);
        const XRayScanView View = EncodeScanView(Scan, Stage, Scanned, Camera.Near);
        Exchange.WriteRegion(StandInView, 0, &View, ScanViewBytes);

        // the stand-in, sixteen refreshes averaged
        std::vector<float> Sum(static_cast<size_t>(Width) * Height * 4u, 0.0f);
        double             SolidMilliseconds = 0.0;
        bool               Healthy = true;
        std::vector<uint8_t> Bytes;
        for (uint32_t Refresh = 0; Refresh < Accumulation && Healthy; ++Refresh)
        {
            const std::vector<uint8_t> Refreshed = Encoder.Encode(Camera, Light, Toggles, External, {Width, Height}, {Width, Height}, 1u, Refresh, Camera.Target, Refresh > 0);
            Exchange.WriteRegion(RefreshRegion, 0, Refreshed.data(), Refreshed.size());
            const auto Begun = std::chrono::steady_clock::now();
            CommandSequence Sequence = Exchange.BeginSequence();
            const std::array<ColorAttachment, 1> Colours = {{{SolidView, LoadOperation::Clear, Backdrop}}};
            const DepthAttachment                Depth{DepthView, LoadOperation::Clear, 0.0f};
            Sequence.BeginRaster(Colours, &Depth, "Stand-in solid");
            Sequence.BindProgram(SolidProgram);
            Sequence.BindGroup(0, SolidGroup);
            Sequence.BindVertexRegion(0, VertexRegion);
            Sequence.BindVertexRegion(1, InstanceRegion);
            for (const ScanShellRun& Run : Shell) Sequence.Draw(Run.VertexCount, Run.InstanceCount, Run.FirstVertex, Run.FirstInstance);
            Sequence.EndRaster();
            Healthy = Exchange.Submit(Sequence, &Diagnostic) && Exchange.ReadTexture(SolidTexture, 0, 0, Bytes);
            SolidMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begun).count();
            if (Healthy)
            {
                for (size_t Index = 0; Index < Sum.size(); ++Index) Sum[Index] += static_cast<float>(Bytes[Index]);
            }
        }
        if (!Healthy)
        {
            std::printf("  🔴 %s: %s\n", Shot.Label, Diagnostic.c_str());
            ++Failures;
            continue;
        }
        std::vector<uint8_t> Solid(Sum.size());
        uint64_t             SolidPixels = 0;
        for (size_t Index = 0; Index < Sum.size(); ++Index)
        {
            Solid[Index] = static_cast<uint8_t>(std::min(255.0f, Sum[Index] / Accumulation + 0.5f));
        }
        for (size_t Pixel = 0; Pixel < static_cast<size_t>(Width) * Height; ++Pixel)
        {
            const bool Background = std::abs(Solid[Pixel * 4 + 0] - static_cast<int>(Backdrop[0] * 255.0f + 0.5f)) < 2
                                 && std::abs(Solid[Pixel * 4 + 1] - static_cast<int>(Backdrop[1] * 255.0f + 0.5f)) < 2
                                 && std::abs(Solid[Pixel * 4 + 2] - static_cast<int>(Backdrop[2] * 255.0f + 0.5f)) < 2;
            if (!Background) ++SolidPixels;
        }

        // the overlay over that picture
        Exchange.WriteTexture(PictureTexture, 0, Solid.data(), Solid.size(), Width, Height);
        CommandSequence Sequence = Exchange.BeginSequence();
        Overlay.Record(Sequence, PictureView, Scan, Shell);
        std::vector<uint8_t> Picture;
        if (!Exchange.Submit(Sequence, &Diagnostic) || !Exchange.ReadTexture(PictureTexture, 0, 0, Picture))
        {
            std::printf("  🔴 %s: %s\n", Shot.Label, Diagnostic.c_str());
            ++Failures;
            continue;
        }
        uint64_t ScanPixels = 0;
        for (size_t Pixel = 0; Pixel < static_cast<size_t>(Width) * Height; ++Pixel)
        {
            if (std::abs(Picture[Pixel * 4 + 0] - Solid[Pixel * 4 + 0]) + std::abs(Picture[Pixel * 4 + 1] - Solid[Pixel * 4 + 1])
                + std::abs(Picture[Pixel * 4 + 2] - Solid[Pixel * 4 + 2]) > 2)
            {
                ++ScanPixels;
            }
        }
        std::string Name = Shot.Label;
        std::replace(Name.begin(), Name.end(), ' ', '-');
        if (!WritePicture(Output / ("XRayScan-" + Name + ".png"), Picture, Width, Height)) ++Failures;
        const ScanOverlayCounts& Counts = Overlay.QueryCounts();
        const std::array<double, 2> Curtain = Scan.ResolveCurtain();
        std::printf("  %-22s front %+.2f m  amount %.2f  draws shell %u edge %u curtain %u sparkle %u  solid %llu px, scan %llu px, solid refresh %.1f ms\n",
                    Shot.Label, Scan.QueryFront(), Scan.QueryAmount(), Counts.ShellDraws, Counts.EdgeDraws, Counts.CurtainDraws, Counts.SparkleDraws,
                    static_cast<unsigned long long>(SolidPixels), static_cast<unsigned long long>(ScanPixels), SolidMilliseconds / Accumulation);
        Ledger << Shot.Label << '\t' << Shot.Seconds << '\t' << Scan.QueryRamp() << '\t' << Scan.QueryFront() << '\t' << Scan.QueryAmount() << '\t'
               << Curtain[0] << '\t' << Curtain[1] << '\t' << Counts.ShellDraws << '\t' << Counts.EdgeDraws << '\t' << Counts.CurtainDraws << '\t'
               << Counts.SparkleDraws << '\t' << SolidPixels << '\t' << ScanPixels << '\t' << SolidMilliseconds / Accumulation << '\n';
        Pictures.push_back(std::move(Picture));
    }

    // ⑧ the contact sheet: three columns of the shots at half size
    if (Pictures.size() == Shots.size())
    {
        const uint32_t Columns = 3, Rows = static_cast<uint32_t>((Pictures.size() + Columns - 1) / Columns), Half = 2;
        const uint32_t CellWidth = Width / Half, CellHeight = Height / Half;
        std::vector<uint8_t> Sheet(static_cast<size_t>(Columns) * CellWidth * Rows * CellHeight * 4u, 255u);
        for (size_t Shot = 0; Shot < Pictures.size(); ++Shot)
        {
            const uint32_t Column = static_cast<uint32_t>(Shot % Columns), Row = static_cast<uint32_t>(Shot / Columns);
            for (uint32_t Y = 0; Y < CellHeight; ++Y)
            {
                for (uint32_t X = 0; X < CellWidth; ++X)
                {
                    for (uint32_t Channel = 0; Channel < 4; ++Channel)
                    {
                        unsigned Total = 0;
                        for (uint32_t Sample = 0; Sample < Half * Half; ++Sample)
                        {
                            Total += Pictures[Shot][((Y * Half + Sample / Half) * Width + X * Half + Sample % Half) * 4 + Channel];
                        }
                        Sheet[(((Row * CellHeight + Y) * Columns * CellWidth) + Column * CellWidth + X) * 4 + Channel] = static_cast<uint8_t>(Total / (Half * Half));
                    }
                }
            }
        }
        if (!WritePicture(Output / "XRayScan-Sweep.png", Sheet, Columns * CellWidth, Rows * CellHeight)) ++Failures;
    }
    std::printf("%s\n", Failures == 0 ? "XRayScanCapture: every shot written" : "XRayScanCapture: failures above");
    return Failures == 0 ? 0 : 1;
}
