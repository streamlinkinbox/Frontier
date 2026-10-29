/*============================================================================================================================================
                                                       VECTORSURFACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Verify authored channel mapping against actual Float32 Vulkan storage and readback for every material surface channel.

#include "Exhibits/TextureStudio/Code/AtlasReadback.h"
#include "Engine/Render/VectorAuthoringCoverageRendering.h"
#include "Engine/ContentInterchange/SurfaceMapEncoding.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <string_view>

using namespace Frontier;

int main(int Count, char **Arguments)
{
    try
    {
        AssertRasterCondition(Count == 4, "Expected Vulkan driver, VectorSurface SPIR-V and evidence directory.");
        const std::filesystem::path Destination(Arguments[3]);
        std::filesystem::create_directories(Destination);
        AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
        std::ifstream Input(Arguments[2], std::ios::binary | std::ios::ate);
        const auto Bytes = Input.tellg();
        AssertRasterCondition(Input && Bytes >= 20 && Bytes % 4 == 0, "Float32 surface SPIR-V is missing or malformed.");
        std::vector<uint32_t> Words(size_t(Bytes) / 4);
        Input.seekg(0);
        AssertRasterCondition(bool(Input.read(reinterpret_cast<char *>(Words.data()), Bytes)), "Float32 shader archive read failed.");
        VectorCoverageRasterization Raster;
        Raster.Initialize(Device.QueryAccess(), RasterShader{Words, "main"}, 32, 32);

        VectorAuthoringDocument Document;
        VectorCoverageInstruction Rectangle;
        Rectangle.Topology = {0, 0, 0, 0};
        Rectangle.Bounds = {256, 256, 256, 256};
        Rectangle.Transform = {0, 0, 1, 0};
        Rectangle.Colour = {.8f, .2f, .1f, 1};
        Rectangle.ColourEnd = Rectangle.Colour;
        Rectangle.Paint[3] = 1;
        Document.Instructions.push_back(Rectangle);
        VectorCoverageInstruction GoldEllipse;
        GoldEllipse.Topology = {1, 0, 0, 0};
        GoldEllipse.Bounds = {256, 256, 100, 100};
        GoldEllipse.Transform = {0, 0, 1, 0};
        GoldEllipse.Colour = {.9f, .65f, .08f, 1};
        GoldEllipse.ColourEnd = GoldEllipse.Colour;
        GoldEllipse.Paint[3] = 1;
        Document.Instructions.push_back(GoldEllipse);
        VectorAuthoringLayer Layer;
        Layer.Token = 1; Layer.Root = 0; Layer.Name = "Float32 validation surface";
        Layer.SurfaceStructure = VectorAuthoringSurfaceStructure::Weave;
        Layer.TextileGeometry = VectorAuthoringTextileGeometry::WeaveYarn;
        Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength = .63f;
        Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Normal)].Strength = .8f;
        Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength = .34f;
        Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Metalness)].Strength = .82f;
        Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Opacity)].Strength = .91f;
        Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Occlusion)].Enabled = true;
        Layer.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Occlusion)].Strength = .4f;
        Document.Layers.push_back(Layer);
        VectorAuthoringLayer GoldenThread;
        GoldenThread.Token = 2; GoldenThread.Root = 1; GoldenThread.Name = "Gold thread";
        GoldenThread.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Strength = .25f;
        GoldenThread.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength = .18f;
        GoldenThread.SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Metalness)].Strength = 1.f;
        Document.Layers.push_back(GoldenThread);
        Document.TokenClock = 2;
        auto InvalidWeave = Document;
        InvalidWeave.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Height)].Enabled = false;
        bool MissingWeaveHeightRefused = false;
        try { (void)CompileVectorSurface(InvalidWeave, VectorAuthoringSurfaceChannel::Height); }
        catch (const std::exception &) { MissingWeaveHeightRefused = true; }
        AssertRasterCondition(MissingWeaveHeightRefused,
            "Native weave material compilation accepted a missing Height contribution.");

        const std::array<std::array<float, 4>, 7> Expected{{
            {.787412f, .380056f, .007194f, 1.f},
            {.25f, .25f, .25f, 1.f},
            {.5f, .5f, 1.f, 1.f},
            {.18f, .18f, .18f, 1.f},
            {1.f, 1.f, 1.f, 1.f},
            {1.f, 1.f, 1.f, 1.f},
            {.6f, .6f, .6f, 1.f}
        }};
        std::array<uint64_t, 7> Dispatches{};
        std::vector<float> BaselineNormalPixels;
        uint64_t Serial = 0;
        for (uint32_t Channel = 0; Channel < 7; ++Channel)
        {
            const auto Program = CompileVectorSurface(Document, VectorAuthoringSurfaceChannel(Channel));
            Raster.ConfigureRetiredSurface(Program, Serial);
            const auto Pixels = Device.CaptureSurface(Raster.QueryImage(), Raster.QueryExtent(), [&](VkCommandBuffer Commands)
            {
                AssertRasterCondition(Raster.ConstructCommands(Commands, Serial, Serial + 1), "Surface channel failed to dispatch.");
            }, [&]()
            {
                Raster.ConfirmSubmission(Serial + 1);
                ++Serial;
            });
            const size_t Centre = (16 * 32 + 16) * 4;
            for (size_t Component = 0; Component < 4; ++Component)
            {
                const float Tolerance = Channel == 2 ? .015f : .002f;
                AssertRasterCondition(std::abs(Pixels[Centre + Component] - Expected[Channel][Component]) <= Tolerance,
                    "GPU Float32 surface component differs from its authored channel expectation.");
            }
            Dispatches[Channel] = Raster.QueryDispatchCount();
            if (Channel == uint32_t(VectorAuthoringSurfaceChannel::Normal)) BaselineNormalPixels = Pixels;
            constexpr std::array<const char *, 7> ChannelNames{"Colour", "Height", "Normal", "Roughness", "Metalness", "Opacity", "Occlusion"};
            std::ofstream Preview(Destination / (std::string("VectorSurface") + ChannelNames[Channel] + ".ppm"), std::ios::binary);
            Preview << "P6\n32 32\n255\n";
            for (size_t Pixel = 0; Pixel < size_t(32 * 32); ++Pixel)
                for (size_t Component = 0; Component < 3; ++Component)
                {
                    const float Sample = std::clamp(Pixels[Pixel * 4 + Component], 0.f, 1.f);
                    const uint8_t Byte = uint8_t(std::lround(Sample * 255.f));
                    Preview.write(reinterpret_cast<const char *>(&Byte), 1);
                }
            AssertRasterCondition(bool(Preview), "Native GPU channel preview capture could not be written.");
            const auto WritePfm = [&](const std::string &Suffix, const std::vector<uint8_t> &Encoded)
            {
                std::ofstream Output(Destination / (std::string("GpuFloat") + ChannelNames[Channel] + Suffix + ".pfm"), std::ios::binary);
                AssertRasterCondition(bool(Output.write(reinterpret_cast<const char *>(Encoded.data()), std::streamsize(Encoded.size()))),
                    "Float32 channel PFM sample could not be written.");
            };
            if (Channel == uint32_t(VectorAuthoringSurfaceChannel::Colour))
                WritePfm("", EncodeSurfaceRgbPfm(32, 32, Pixels));
            else WritePfm("", EncodeSurfaceScalarPfm(32, 32, Pixels, 0));
            WritePfm("Alpha", EncodeSurfaceScalarPfm(32, 32, Pixels, 3));
            if (Channel == uint32_t(VectorAuthoringSurfaceChannel::Height) ||
                Channel == uint32_t(VectorAuthoringSurfaceChannel::Metalness) ||
                Channel == uint32_t(VectorAuthoringSurfaceChannel::Roughness))
            {
                const size_t Fabric = (10 * 32 + 10) * 4;
                const float FabricExpected = Channel == uint32_t(VectorAuthoringSurfaceChannel::Height) ? .63f
                    : Channel == uint32_t(VectorAuthoringSurfaceChannel::Roughness) ? .34f : .82f;
                AssertRasterCondition(std::abs(Pixels[Fabric] - FabricExpected) <= .002f,
                    "Gold-thread channel settings contaminated the separate fabric layer.");
            }
        }
        auto ReliefDocument = Document;
        ReliefDocument.Layers[0].SurfaceRelief = .5f;
        const auto ReliefHeightProgram = CompileVectorSurface(ReliefDocument, VectorAuthoringSurfaceChannel::Height);
        Raster.ConfigureRetiredSurface(ReliefHeightProgram, Serial);
        const auto ReliefHeightPixels = Device.CaptureSurface(Raster.QueryImage(), Raster.QueryExtent(), [&](VkCommandBuffer Commands)
        {
            AssertRasterCondition(Raster.ConstructCommands(Commands, Serial, Serial + 1),
                "Calibrated height relief did not dispatch its Float32 map.");
        }, [&]()
        {
            Raster.ConfirmSubmission(Serial + 1);
            ++Serial;
        });
        const size_t ReliefFabricPixel = (10 * 32 + 10) * 4;
        const size_t ReliefGoldPixel = (16 * 32 + 16) * 4;
        AssertRasterCondition(std::abs(ReliefHeightPixels[ReliefFabricPixel] - .315f) <= .002f &&
            std::abs(ReliefHeightPixels[ReliefGoldPixel] - .25f) <= .002f,
            "Surface relief failed to scale height while retaining the separate gold-thread height.");
        const auto ReliefNormalProgram = CompileVectorSurface(ReliefDocument, VectorAuthoringSurfaceChannel::Normal);
        Raster.ConfigureRetiredSurface(ReliefNormalProgram, Serial);
        const auto ReliefNormalPixels = Device.CaptureSurface(Raster.QueryImage(), Raster.QueryExtent(), [&](VkCommandBuffer Commands)
        {
            AssertRasterCondition(Raster.ConstructCommands(Commands, Serial, Serial + 1),
                "Calibrated height relief did not dispatch derived tangent normals.");
        }, [&]()
        {
            Raster.ConfirmSubmission(Serial + 1);
            ++Serial;
        });
        float MaximumReliefNormalDifference = 0;
        for (size_t Pixel = 0; Pixel < size_t(32 * 32); ++Pixel)
            for (size_t Component = 0; Component < 3; ++Component)
                MaximumReliefNormalDifference = std::max(MaximumReliefNormalDifference,
                    std::abs(ReliefNormalPixels[Pixel * 4 + Component] - BaselineNormalPixels[Pixel * 4 + Component]));
        AssertRasterCondition(MaximumReliefNormalDifference > .02f &&
            std::abs(ReliefNormalPixels[ReliefGoldPixel + 2] - BaselineNormalPixels[ReliefGoldPixel + 2]) < .002f,
            "Height-relief calibration failed to modify fabric normals independently of gold-thread normals.");
        Document.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Enabled = false;
        const auto ShapeScopedRoughness = CompileVectorSurface(Document, VectorAuthoringSurfaceChannel::Roughness);
        Raster.ConfigureRetiredSurface(ShapeScopedRoughness, Serial);
        const auto ShapeScopedPixels = Device.CaptureSurface(Raster.QueryImage(), Raster.QueryExtent(), [&](VkCommandBuffer Commands)
        {
            AssertRasterCondition(Raster.ConstructCommands(Commands, Serial, Serial + 1),
                "Single-shape roughness disable did not dispatch its revised material map.");
        }, [&]()
        {
            Raster.ConfirmSubmission(Serial + 1);
            ++Serial;
        });
        const size_t ShapeScopedFabric = (10 * 32 + 10) * 4;
        const size_t ShapeScopedThread = (16 * 32 + 16) * 4;
        AssertRasterCondition(ShapeScopedPixels[ShapeScopedFabric + 3] == 0,
            "Disabled roughness still contributes from the dielectric fabric shape.");
        AssertRasterCondition(std::abs(ShapeScopedPixels[ShapeScopedThread] - .18f) <= .002f
            && ShapeScopedPixels[ShapeScopedThread + 3] == 1,
            "Per-shape roughness disable altered the separate golden-thread contribution.");
        Document.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Enabled = true;
        Document.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Roughness)].Strength = .23f;
        const auto StrengthScopedRoughness = CompileVectorSurface(Document, VectorAuthoringSurfaceChannel::Roughness);
        Raster.ConfigureRetiredSurface(StrengthScopedRoughness, Serial);
        const auto StrengthScopedPixels = Device.CaptureSurface(Raster.QueryImage(), Raster.QueryExtent(), [&](VkCommandBuffer Commands)
        {
            AssertRasterCondition(Raster.ConstructCommands(Commands, Serial, Serial + 1),
                "Per-shape roughness strength edit did not dispatch its revised material map.");
        }, [&]()
        {
            Raster.ConfirmSubmission(Serial + 1);
            ++Serial;
        });
        const size_t StrengthScopedFabric = (10 * 32 + 10) * 4;
        const size_t StrengthScopedThread = (16 * 32 + 16) * 4;
        AssertRasterCondition(std::abs(StrengthScopedPixels[StrengthScopedFabric] - .23f) <= .002f &&
            std::abs(StrengthScopedPixels[StrengthScopedThread] - .18f) <= .002f &&
            StrengthScopedPixels[StrengthScopedFabric + 3] == 1 && StrengthScopedPixels[StrengthScopedThread + 3] == 1,
            "Per-shape roughness strength failed to update the Float32 map or contaminated golden thread.");
        Document.Layers[0].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Occlusion)].Enabled = false;
        Document.Layers[1].SurfaceChannels[size_t(VectorAuthoringSurfaceChannel::Occlusion)].Enabled = false;
        const auto DisabledOcclusion = CompileVectorSurface(Document, VectorAuthoringSurfaceChannel::Occlusion);
        Raster.ConfigureRetiredSurface(DisabledOcclusion, Serial);
        const auto DisabledPixels = Device.CaptureSurface(Raster.QueryImage(), Raster.QueryExtent(), [&](VkCommandBuffer Commands)
        {
            AssertRasterCondition(Raster.ConstructCommands(Commands, Serial, Serial + 1), "Disabled channel failed to dispatch its contribution mask.");
        }, [&]()
        {
            Raster.ConfirmSubmission(Serial + 1);
            ++Serial;
        });
        const size_t DisabledCentre = (16 * 32 + 16) * 4;
        AssertRasterCondition(DisabledPixels[DisabledCentre] == 1 && DisabledPixels[DisabledCentre + 1] == 1 &&
            DisabledPixels[DisabledCentre + 2] == 1 && DisabledPixels[DisabledCentre + 3] == 0,
            "Disabled Float32 channel must retain its neutral map and export a zero-contribution mask.");
        VectorAuthoringCoverageRendering CoverageRendering;
        CoverageRendering.Initialize(Device.QueryAccess(), RasterShader{Words, "main"});
        const auto CachedProgram = CompileVectorSurface(Document, VectorAuthoringSurfaceChannel::Metalness);
        const int CachedSlot = CoverageRendering.SelectSurfaceProgram(CachedProgram);
        AssertRasterCondition(CachedSlot >= 0, "Float32 surface cache did not select a retired image slot.");
        const auto CachedPixels = Device.CaptureSurface(CoverageRendering.QueryImage(size_t(CachedSlot)), {512, 512}, [&](VkCommandBuffer Commands)
        {
            CoverageRendering.ConstructCommands(Commands, 0, 1);
        }, [&]()
        {
            CoverageRendering.ConfirmSubmission();
        });
        const size_t CachedGold = (256 * 512 + 256) * 4;
        const size_t CachedFabric = (160 * 512 + 160) * 4;
        AssertRasterCondition(std::abs(CachedPixels[CachedGold] - 1.f) <= .002f &&
            std::abs(CachedPixels[CachedFabric] - .82f) <= .002f,
            "Cached Float32 surface path failed to preserve gold/fabric material separation.");
        AssertRasterCondition(CoverageRendering.SelectSurfaceProgram(CachedProgram) == CachedSlot,
            "Equivalent Float32 authoring edits did not reuse their selected cache slot.");
        CoverageRendering.Finalize();

        std::ofstream Report(Destination / "VectorSurfaceChecks.json");
        Report << "{\"result\":\"passed\",\"channel_count\":7,\"weave_height_normal_gate\":true,\"surface_relief_calibration\":true,\"roughness_strength_scoped\":true,\"per_layer_golden_thread_isolated\":true,\"cached_surface_submission\":true,\"dispatch_count\":" << Serial
               << ",\"format\":\"VK_FORMAT_R32G32B32A32_SFLOAT\",\"float32_readback\":true,\"channels\":[";
        for (size_t Index = 0; Index < Dispatches.size(); ++Index)
        {
            if (Index) Report << ',';
            Report << "{\"channel\":" << Index << ",\"dispatches\":" << Dispatches[Index] << '}';
        }
        Report << "]}\n";
        std::cout << "PASS: Float32 channels, weave gates, height-relief calibration, scoped Roughness strength, and separate gold thread.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << Failure.what() << '\n';
        return 1;
    }
}
