/*============================================================================================================================================
                                                  VECTORAUTHORINGDOCUMENTVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Verify bounded native authoring, mask retention, source-only undo/redo and shared icon/switch inspector construction.

#include "Engine/ContentInterchange/VectorAuthoringDocument.h"
#include "UI/TextureStudio/VectorMaskControls.h"
#include "RasterizeLibraryEvidence.h"
#include <filesystem>
#include <imgui.h>
#include <iostream>
#include <fstream>

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition, const char *Caption) { if (!Condition) throw std::runtime_error(Caption); ++Checks; };
        Confirm(Count == 2, "Expected validation report destination.");
        for (bool Stamp : {false, true})
        {
            VectorAuthoringHistory History(Stamp);
            VectorAuthoringDocument Document; Document.Stamp = Stamp;
            Document.TokenClock = 2;
            VectorCoverageInstruction Square, Circle, Difference, Mask;
            Square.Bounds = {256, 256, 300, 300}; Circle = Square; Circle.Topology[0] = 1;
            Difference.Topology = {17, 0, 1, 0}; Mask = Circle;
            Document.Instructions = {Square, Circle, Difference};
            Document.Layers = {{1, 2, "Non-destructive Boolean"}, {2, 1, "Hidden source", false, true}};
            Document.Layers[0].Mask = Mask;
            Document.Layers[0].MaskInverted = true;
            Document.Layers[0].MaskLuminance = true;
            History.Finalize(Document);
            const auto Program = ConstructVectorAuthoringCoverage(History.QueryDocument());
            Confirm(Program.Instructions[Program.Root].Topology[0] == 25, "Luminance mask was not compiled into a GPU expression.");
            Confirm(Program.Instructions[Program.Root].Topology[3] == 1, "Mask inversion lost during metadata compilation.");
            const auto Encoded = EncodeVectorAuthoring(Document);
            Confirm(EncodeVectorAuthoring(DecodeVectorAuthoring(Encoded)) == Encoded, "Native TOML source did not round-trip deterministically.");
            Confirm(History.RestoreHistory(false) && History.QueryDocument().Layers.empty(), "Undo did not restore empty source.");
            Confirm(History.RestoreHistory(true) && History.QueryDocument().Layers.size() == 2, "Redo did not restore native Boolean and mask source.");
            Confirm(History.QueryRevision() == 3, "History revision did not invalidate GPU output.");
            Document.Layers[0].MaskEnabled = false;
            Confirm(ConstructVectorAuthoringCoverage(Document).Root == 2, "Disabled mask retained a GPU masking instruction.");
            Document.Layers[0].Visible = false;
            auto Empty = ConstructVectorAuthoringCoverage(Document);
            Confirm(Empty.Instructions[Empty.Root].Transform[2] == 0, "Hidden source leaked into artwork output.");
            Document.Layers[1].Token = 1;
            bool Refused = false; try { ConstructVectorAuthoringCoverage(Document); } catch (const std::exception &) { Refused = true; }
            Confirm(Refused, "Duplicate native layer identity accepted.");
            Refused = false; try { DecodeVectorAuthoring(std::string(600001, 'x')); } catch (const std::exception &) { Refused = true; }
            Confirm(Refused, "Oversized native source accepted.");
            Refused = false; try { DecodeVectorAuthoring("format='frontier.vector_artwork'\nversion=2\n"); } catch (const std::exception &) { Refused = true; }
            Confirm(Refused, "Unknown native format version accepted.");
        }
        ImGui::CreateContext(); auto &Input = ImGui::GetIO(); Input.IniFilename = nullptr;
        Input.DisplaySize = ImVec2(640, 800); Input.DeltaTime = 1.f / 60;
        unsigned char *Pixels = nullptr; int Width = 0, Height = 0;
        Input.Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height); Input.Fonts->SetTexID(ImTextureID(1));
        VectorAuthoringLayer Layer; VectorCoverageInstruction Mask; Mask.Topology[0] = 1; Layer.Mask = Mask;
        ImVec2 BooleanOrigin, MaskOrigin;
        std::optional<uint32_t> SelectedOperation;
        bool MaskChanged = false;
        const auto IntegrateControls = [&]()
        {
            ImGui::NewFrame(); ImGui::SetNextWindowPos(ImVec2(10, 10)); ImGui::SetNextWindowSize(ImVec2(620, 780));
            ImGui::Begin("Shared artwork controls — not the workspace port");
            ImGui::PushID("Disabled specimen");
            Confirm(!ConstructVectorBooleanControls(false), "Disabled native Boolean action dispatched.");
            ImGui::PopID();
            BooleanOrigin = ImGui::GetCursorScreenPos();
            if (auto Operation = ConstructVectorBooleanControls(true)) SelectedOperation = Operation;
            MaskOrigin = ImGui::GetCursorScreenPos();
            MaskChanged |= ConstructVectorAuthoringMaskControls(Layer, {256, 256, 300, 300});
            ImGui::End(); ImGui::Render();
        };
        for (int Sample = 0; Sample < 4; ++Sample) IntegrateControls();
        Confirm(!MaskChanged, "Idle native mask controls mutated authored source.");
        const auto Click = [&](ImVec2 Point)
        {
            Input.AddMousePosEvent(Point.x, Point.y); IntegrateControls();
            Input.AddMouseButtonEvent(0, true); IntegrateControls();
            Input.AddMouseButtonEvent(0, false); IntegrateControls();
        };
        Click(ImVec2(BooleanOrigin.x + 18, BooleanOrigin.y + 16));
        Confirm(SelectedOperation == 16, "Native Union icon did not respond to real pointer input.");
        Click(ImVec2(MaskOrigin.x + 23, MaskOrigin.y + ImGui::GetTextLineHeightWithSpacing() + 14));
        Confirm(MaskChanged && !Layer.MaskEnabled, "Native mask switch did not respond to real pointer input.");
        Layer.Locked = true;
        MaskChanged = false;
        Click(ImVec2(MaskOrigin.x + 23, MaskOrigin.y + ImGui::GetTextLineHeightWithSpacing() + 14));
        Confirm(!MaskChanged && !Layer.MaskEnabled, "Locked native artwork mask accepted real pointer editing.");
        Confirm(ImGui::GetDrawData()->TotalVtxCount > 0, "Native Boolean/mask controls did not construct retained presentation geometry.");
        EncodeLibraryDrawCapture(std::filesystem::path(Arguments[1]).parent_path() / "NativeVectorAuthoringControls.ppm", *ImGui::GetDrawData(), Pixels, Width, Height);
        ImGui::DestroyContext();
        std::ofstream Report(Arguments[1]);
        Confirm(bool(Report), "Native artwork report could not be opened.");
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"windows_executed\":false,\"workspace_ui_integrated\":false}\n";
        if (!Report) throw std::runtime_error("Native artwork report could not be written.");
        std::cout << Checks << " native artwork source/control checks passed.\n"; return 0;
    }
    catch (const std::exception &Failure)
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << Failure.what() << '\n'; return 1;
    }
}
