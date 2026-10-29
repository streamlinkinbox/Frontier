/*============================================================================================================================================
                                                         VECTORCOVERAGEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Read actual Vulkan compute pixels for Boolean holes, analytic curve winding, masks, gradients and retired-residence caching.

#include "Engine/Render/VectorCoverageRasterization.h"
#include "Engine/ContentInterchange/VectorAuthoringEditing.h"
#include "AtlasReadback.h"
#include <stb_image_write.h>
#include <iostream>

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        AssertRasterCondition(Count == 4, "Expected driver, compiled shader and evidence directory.");
        std::cerr << "GPU: initialize device\n";
        AtlasValidationDevice Device(Arguments[1], VK_API_VERSION_1_2);
        const auto Shader = DecodeShaderArchive(Arguments[2]);
        VectorCoverageRasterization Raster;
        std::cerr << "GPU: construct coverage pipeline\n";
        Raster.Initialize(Device.QueryAccess(), {Shader, "main"}, 256, 256);
        const std::filesystem::path Destination(Arguments[3]);
        std::filesystem::create_directories(Destination);
        uint64_t Serial = 0;
        uint32_t Checks = 0;
        // 📝 Pure-function coverage: a live, zoomable canvas asks for the device-pixel edge it is
        // actually displayed at, so the bake tracks zoom instead of a fixed-size raster being
        // stretched (crisp when zoomed in, not needlessly large when zoomed out). The display hint
        // must never exceed what the GPU watchdog budget affords.
        AssertRasterCondition(VectorCoverageRasterization::SelectBakeExtent(0, 1) == 2048,
            "Unhinted single-channel bake extent regressed from its full-resolution default."); ++Checks;
        AssertRasterCondition(VectorCoverageRasterization::SelectBakeExtent(0, 1, 256) == 256,
            "A zoomed-out display hint did not shrink an empty document's bake extent."); ++Checks;
        AssertRasterCondition(VectorCoverageRasterization::SelectBakeExtent(0, 1, 300) == 512,
            "A display hint between pooled tiles did not round up to the next tile."); ++Checks;
        AssertRasterCondition(VectorCoverageRasterization::SelectBakeExtent(0, 1, 2048) == 2048,
            "A zoomed-in display hint did not reach the raised 2048 ceiling for a light document."); ++Checks;
        AssertRasterCondition(VectorCoverageRasterization::SelectBakeExtent(0, 1, 4096) == 2048,
            "A display hint beyond the pooled ceiling was not capped at the affordable extent."); ++Checks;
        AssertRasterCondition(VectorCoverageRasterization::SelectBakeExtent(5000, 1) == 512,
            "Budget-only bake extent for a mid-size document regressed."); ++Checks;
        AssertRasterCondition(VectorCoverageRasterization::SelectBakeExtent(5000, 1, 1024) == 512,
            "A display hint above the GPU budget overrode the affordable ceiling."); ++Checks;
        AssertRasterCondition(VectorCoverageRasterization::SelectBakeExtent(5000, 1, 128) == 128,
            "A display hint below the affordable ceiling was not honoured for a mid-size document."); ++Checks;
        const auto Confirm = [&](bool Condition, const char *Caption) { AssertRasterCondition(Condition, Caption); ++Checks; };
        const auto Evaluate = [&](const VectorCoverageProgram &Program)
        {
            std::cerr << "GPU: upload/dispatch " << Serial << "\n";
            Raster.ConfigureRetiredProgram(Program, Serial);
            return Device.Capture(Raster.QueryImage(), Raster.QueryExtent(), [&](VkCommandBuffer Commands)
            {
                Confirm(Raster.ConstructCommands(Commands, Serial, Serial + 1), "Dirty artwork did not dispatch.");
            }, [&]() { Raster.ConfirmSubmission(); ++Serial; });
        };
        const auto Alpha = [](const std::vector<unsigned char> &Pixels, uint32_t X, uint32_t Y) { return Pixels[(Y * 256 + X) * 4 + 3]; };
        VectorCoverageProgram Program;
        VectorCoverageInstruction Rectangle, Ellipse, Operation;
        Rectangle.Bounds = {256, 256, 300, 300}; Rectangle.Colour = {.9f, .65f, .3f, 1};
        Ellipse = Rectangle; Ellipse.Topology[0] = 1;
        Operation.Topology = {17, 0, 1, 0};
        Program.Instructions = {Rectangle, Ellipse, Operation}; Program.Root = 2;
        for (uint32_t Category : {16u, 17u, 18u, 19u})
        {
            Program.Instructions[2].Topology[0] = Category;
            const auto Pixels = Evaluate(Program);
            Confirm(Alpha(Pixels, 128, 128) == (Category == 17 || Category == 19 ? 0 : 255), "Boolean centre coverage is incorrect.");
            Confirm(Alpha(Pixels, 56, 56) == (Category == 18 ? 0 : 255), "Boolean corner coverage is incorrect.");
            Confirm(Alpha(Pixels, 20, 20) == 0, "Boolean filled outside its geometry.");
            const auto Filename = Destination / ("GpuBoolean" + std::to_string(Category) + ".png");
            Confirm(stbi_write_png(Filename.string().c_str(), 256, 256, 4, Pixels.data(), 256 * 4) != 0, "GPU capture encoding failed.");
        }
        Program.Instructions[2].Topology[0] = 24;
        auto Pixels = Evaluate(Program);
        Confirm(Alpha(Pixels, 128, 128) == 255 && Alpha(Pixels, 56, 56) == 0, "Alpha mask coverage is incorrect.");
        Program.Instructions[2].Topology[3] = 1;
        Pixels = Evaluate(Program);
        Confirm(Alpha(Pixels, 128, 128) == 0 && Alpha(Pixels, 56, 56) == 255, "Inverted alpha mask is incorrect.");
        Program.Instructions[2].Topology = {25, 0, 1, 0};
        Program.Instructions[1].Colour = {.5f, .5f, .5f, 1};
        Pixels = Evaluate(Program);
        Confirm(Alpha(Pixels, 128, 128) >= 127 && Alpha(Pixels, 128, 128) <= 128, "Luminance mask is not a real fractional mask.");
        Program.Instructions[2].Transform[2] = 0;
        Pixels = Evaluate(Program);
        Confirm(Alpha(Pixels, 128, 128) == 0, "Zero mask opacity is not exactly transparent.");
        Program.Instructions.resize(1); Program.Root = 0;
        Program.Instructions[0].Transform[3] = 1; Program.Instructions[0].Colour = {1, 0, 0, 1}; Program.Instructions[0].ColourEnd = {0, 0, 1, 1};
        Pixels = Evaluate(Program);
        Confirm(Pixels[(128 * 256 + 70) * 4] > Pixels[(128 * 256 + 185) * 4], "GPU gradient does not vary spatially.");
        Program.Instructions[0].Topology = {6, 0, 4, 0};
        const std::array<std::array<float, 2>, 4> Corners{{{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}}};
        for (size_t Index = 0; Index < 4; ++Index)
        {
            const auto Start = Corners[Index], End = Corners[(Index + 1) % 4];
            Program.Segments.push_back({{Start[0], Start[1], Start[0] + (End[0] - Start[0]) / 3, Start[1] + (End[1] - Start[1]) / 3}, {Start[0] + 2 * (End[0] - Start[0]) / 3, Start[1] + 2 * (End[1] - Start[1]) / 3, End[0], End[1]}});
        }
        Pixels = Evaluate(Program);
        Confirm(Alpha(Pixels, 128, 128) == 255 && Alpha(Pixels, 20, 20) == 0, "GPU cubic winding does not retain a closed contour.");
        bool Refused = false;
        try { Raster.ConfigureRetiredProgram(Program, Serial - 1); } catch (const std::exception &) { Refused = true; }
        Confirm(Refused, "In-flight descriptor overwrite was accepted.");
        Confirm(!Raster.ConstructCommands(VK_NULL_HANDLE, Serial, Serial + 1), "Clean artwork dispatched again.");
        auto Invalid = Program; Invalid.Instructions[0].Topology = {17, 0, 0, 0};
        Refused = false; try { ValidateVectorCoverage(Invalid); } catch (const std::exception &) { Refused = true; }
        Confirm(Refused, "Cyclic GPU expression accepted.");
        // 📝 Evaluate the production authoring port through the real retained Slang/Vulkan path.
        VectorAuthoringDocument VectorAuthoring;
        VectorAuthoring.TokenClock = 2;
        VectorCoverageInstruction Outer, Hole, Difference;
        Outer.Bounds = {256, 256, 80, 80};
        Outer.Colour = {.2f, .8f, .45f, 1};
        Hole.Bounds = {256, 256, 40, 40};
        Hole.Topology[0] = 1;
        Difference.Topology = {17, 0, 1, 0};
        VectorAuthoring.Instructions = {Outer, Hole, Difference};
        VectorAuthoringLayer Motif;
        Motif.Token = 1;
        Motif.Root = 2;
        Motif.Name = "Retained Boolean motif";
        Motif.EnclosureToken = 2;
        VectorAuthoring.Layers.push_back(Motif);
        VectorAuthoring.Enclosures.push_back({2, 0, "Array source"});
        VectorAuthoringArraySpecification Specification;
        Specification.Count = 3;
        Specification.StepX = 128;
        const std::array<uint32_t, 1> Selection{1};
        auto Array = GenerateVectorAuthoringArray(VectorAuthoring, Selection, Specification);
        Pixels = Evaluate(ConstructVectorAuthoringCoverage(Array.Document));
        for (uint32_t Centre : {64u, 128u, 192u})
        {
            Confirm(Alpha(Pixels, Centre, 128) == 0, "Native array lost a retained Boolean hole.");
            Confirm(Alpha(Pixels, Centre + 15, 143) == 255, "Native array lost its source corner coverage.");
        }
        Confirm(Alpha(Pixels, 96, 128) == 0, "Native array painted between independent copies.");
        Confirm(stbi_write_png((Destination / "GpuNativeVectorAuthoringArray.png").string().c_str(), 256, 256, 4, Pixels.data(), 256 * 4) != 0, "Native GPU array capture failed.");
        VectorAuthoring.Layers[0].Mask = Hole;
        VectorAuthoring.Layers[0].Mask->Bounds[2] = 60;
        VectorAuthoring.Layers[0].Mask->Bounds[3] = 60;
        Array = GenerateVectorAuthoringArray(VectorAuthoring, Selection, Specification);
        Pixels = Evaluate(ConstructVectorAuthoringCoverage(Array.Document));
        for (uint32_t Centre : {64u, 128u, 192u})
        {
            Confirm(Alpha(Pixels, Centre + 13, 128) == 255, "Native array mask did not retain fractional source geometry.");
            Confirm(Alpha(Pixels, Centre + 18, 128) == 0, "Native array mask did not follow its copy placement.");
        }
        Confirm(stbi_write_png((Destination / "GpuNativeVectorAuthoringMasks.png").string().c_str(), 256, 256, 4, Pixels.data(), 256 * 4) != 0, "Native GPU mask capture failed.");
        for (auto &Entry : Array.Document.Enclosures) Entry.VisibilityEnabled = false;
        Pixels = Evaluate(ConstructVectorAuthoringCoverage(Array.Document));
        Confirm(Alpha(Pixels, 77, 128) == 0 && Alpha(Pixels, 141, 128) == 0 && Alpha(Pixels, 205, 128) == 0, "Hidden native enclosures leaked retained GPU pixels.");
        std::ofstream Report(Destination / "VectorCoverageChecks.json");
        Report << "{\"checks\":" << Checks << ",\"dispatches\":" << Raster.QueryDispatchCount() << ",\"device\":\"" << Device.Properties.deviceName << "\",\"editable_contour_reconstruction\":false,\"result\":\"PASS\"}\n";
        std::cout << Checks << " GPU coverage assertions passed; device: " << Device.Properties.deviceName << '\n';
    }
    catch (const std::exception &Failure) { std::cerr << Failure.what() << '\n'; return 1; }
}
