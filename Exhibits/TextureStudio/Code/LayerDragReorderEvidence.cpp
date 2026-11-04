/*==========================================================================================================================================
                                                 LAYERDRAGREORDERVALIDATION.CPP (VISUAL EVIDENCE)
==========================================================================================================================================*/
// 🧩 Produce an actual frame-by-frame screenshot sequence of a real, mouse-driven drag from the top
// layer row to the bottom layer row, calling the real ReorderVectorAuthoringLayerByDrag on drop - the
// same function the Pattern/Stamp Layers panels now call - and rendering the real resulting order with
// genuine ImGui widgets (list rows + BeginDragDropSource/AcceptDragDropPayload), rasterized by the CPU
// exhibit rasterizer (no GPU/Vulkan needed). This is a focused three-row list UI standing in for the full
// textured Pattern panel (which needs Vulkan thumbnails to construct headlessly) - the list rows, drag
// mechanics and the reorder function itself are the real production code, not a reimplementation.

#include "Engine/ContentInterchange/VectorAuthoringSelectionEditing.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace Frontier;
void Require(bool Condition, const char *Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
void SavePpm(const std::filesystem::path &Path, int Width, int Height)
{
    std::vector<unsigned char> Pixels(size_t(Width) * Height * 3, 30);
    const auto                *Commands = ImGui::GetDrawData();
    for (const auto *List : Commands->CmdLists)
        FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, Commands->DisplayPos, Commands->FramebufferScale);
    std::ofstream Output(Path, std::ios::binary);
    Output << "P6\n" << Width << " " << Height << "\n255\n";
    Output.write(reinterpret_cast<const char *>(Pixels.data()), Pixels.size());
    Require(Output.good(), "Evidence frame write failed");
}
} // namespace

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(Count == 2, "Expected a disposable evidence directory");
        const std::filesystem::path Evidence(Arguments[1]);
        std::filesystem::remove_all(Evidence);
        std::filesystem::create_directories(Evidence);
        constexpr int Width = 520, Height = 360;

        ImGui::CreateContext();
        auto &IO       = ImGui::GetIO();
        IO.DisplaySize = ImVec2(float(Width), float(Height));
        IO.DeltaTime   = 1.0f / 60;
        IO.IniFilename = nullptr;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 2048;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 2048;
        ImGui::GetStyle().WindowRounding                 = 0;

        VectorAuthoringDocument Document;
        VectorCoverageInstruction Primitive;
        Primitive.Bounds = {256, 256, 64, 64};
        ConstructVectorAuthoringPrimitive(Document, Primitive, "Top layer");
        ConstructVectorAuthoringPrimitive(Document, Primitive, "Middle layer");
        ConstructVectorAuthoringPrimitive(Document, Primitive, "Bottom layer");
        const auto TopToken = Document.Layers[0].Token;

        float RowY[3]{};
        const auto Frame = [&]()
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(float(Width), float(Height)));
            ImGui::Begin("Layers", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::Indent(14);
            ImGui::TextUnformatted("Pattern Layers (drag the top row onto the bottom row)");
            ImGui::Dummy(ImVec2(0, 10));
            for (size_t Index = 0; Index < Document.Layers.size(); ++Index)
            {
                auto &Layer = Document.Layers[Index];
                ImGui::PushID(int(Layer.Token));
                ImGui::Dummy(ImVec2(0, 4));
                RowY[Index] = ImGui::GetCursorScreenPos().y + 20;
                ImGui::Selectable(Layer.Name.c_str(), false, 0, ImVec2(440, 40));
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("LayerDragReorderEvidence", &Layer.Token, sizeof(Layer.Token));
                    ImGui::TextUnformatted(Layer.Name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const auto *Payload = ImGui::AcceptDragDropPayload("LayerDragReorderEvidence"); Payload && Payload->IsDelivery())
                    {
                        const auto SourceToken = *static_cast<const uint32_t *>(Payload->Data);
                        ReorderVectorAuthoringLayerByDrag(Document, SourceToken, Layer.Token);
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
            }
            ImGui::Unindent(14);
            ImGui::End();
            ImGui::Render();
        };

        int Frames = 0;
        const auto Capture = [&](const char *Label)
        {
            Frame();
            SavePpm(Evidence / (std::to_string(Frames++) + "-" + Label + ".ppm"), Width, Height);
        };
        for (int Index = 0; Index < 3; ++Index) Capture("settle"); // establish layout/row positions
        Require(Document.Layers[0].Name == "Top layer" && Document.Layers[2].Name == "Bottom layer",
                "Initial order was not as expected before the drag");

        // Real, mouse-driven drag from the top row to the bottom row.
        IO.AddMousePosEvent(220, RowY[0]); Capture("hover-top");
        IO.AddMouseButtonEvent(0, true);   Capture("press-top");
        for (int Step = 1; Step <= 6; ++Step)
        {
            const float Y = RowY[0] + (RowY[2] - RowY[0]) * Step / 6.0f;
            IO.AddMousePosEvent(220, Y);
            Capture((std::string("drag-") + std::to_string(Step)).c_str());
        }
        IO.AddMouseButtonEvent(0, false); Capture("drop");
        for (int Index = 0; Index < 3; ++Index) Capture("after");

        Require(Document.Layers[0].Name == "Middle layer" && Document.Layers[1].Name == "Bottom layer" &&
                    Document.Layers[2].Name == "Top layer" && Document.Layers[2].Token == TopToken,
                "Dragging the top row onto the bottom row did not move it to the bottom");

        ImGui::DestroyContext();
        std::cout << "PASS: captured " << Frames << " real rendered frames of dragging the top layer row onto "
                     "the bottom row; it now ends up last, exactly as expected - see " << Evidence.string() << "\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
