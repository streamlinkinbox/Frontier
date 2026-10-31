/*============================================================================================================================================
                                                   STAMPTEXTGESTUREVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Headless CPU-only repro/regression test for the reported "Text tool is broken" bug: drives the real
//    StampWorkspacePanel with synthetic ImGui pointer/keyboard events (no GPU, no Vulkan, no real window —
//    pure software ImGui context, same technique as PatternWorkspaceValidation.cpp) through the exact
//    user gesture (select Text tool, drag out a box on the canvas, type characters, commit) and asserts
//    a real VectorAuthoringComponentKind::Text layer lands in the document. No QueryCoverage/QuerySelectionToken
//    callback is installed on purpose — the Text draw/compose/commit path under test never needs GPU coverage
//    baking or GPU hit-testing, so this test isolates the gesture/composer logic from bug #2 (stuck bake badge).

#include "UI/TextureStudio/StampWorkspacePanel.h"
#include "Engine/ContentInterchange/VectorAuthoringProperties.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition) throw std::runtime_error(Message);
}
}

int main(int Count, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(Count == 2, "Expected a DM Sans regular face path.");
        ImGui::CreateContext();
        auto &Input = ImGui::GetIO();
        Input.IniFilename = nullptr;
        Input.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        Input.DisplaySize = ImVec2(1440, 900);
        Input.DeltaTime = 1.f / 60;
        auto *Typeface = Input.Fonts->AddFontFromFileTTF(Arguments[1], 16);
        Require(Typeface != nullptr, "Pinned reference typeface did not load.");
        Input.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        Input.Fonts->TexDesiredFormat = ImTextureFormat_RGBA32;

        StampWorkspacePanel Workspace;
        Require(Workspace.Initialize() && Workspace.Activate(), "Native Stamp lifecycle activation failed.");
        Workspace.ConfigureTypeface(Typeface, Typeface);
        Workspace.ConfigureContentRoot(std::filesystem::current_path());

        const auto Frame = [&]() { ImGui::NewFrame(); Workspace.ConstructInterface(); ImGui::Render(); };
        for (int Index = 0; Index < 8; ++Index) Frame();

        const auto Click = [&](const char *Caption)
        {
            auto Extent = Workspace.QueryControlExtent(Caption);
            for (int Attempt = 0; Attempt < 16 && (Extent.MaximumY > 890 || Extent.MinimumY < 80); ++Attempt)
            {
                const auto Panel = Workspace.QueryPanelExtent("Inspector");
                Input.AddMousePosEvent((Panel.MinimumX + Panel.MaximumX) * .5f, 500);
                Input.AddMouseWheelEvent(0, Extent.MaximumY > 890 ? -5 : 5);
                Frame(); Frame(); Frame();
                Extent = Workspace.QueryControlExtent(Caption);
            }
            Require(Extent.Width() > 0 && Extent.Height() > 0, (std::string("Control not found: ") + Caption).c_str());
            const ImVec2 Centre((Extent.MinimumX + Extent.MaximumX) * .5f, (Extent.MinimumY + Extent.MaximumY) * .5f);
            Input.AddMousePosEvent(Centre.x, Centre.y); Frame();
            Input.AddMouseButtonEvent(0, true); Frame();
            Input.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame();
        };

        Require(Workspace.QueryDocument().Layers.empty(), "Idle headless Stamp UI already had artwork.");
        Click("Text tool (T)");
        for (int Index = 0; Index < 3; ++Index) Frame();

        // Drag a text box on the canvas, exactly like a user click-dragging to author the text region —
        // multiple incremental mouse-move frames, not a single teleport, since real pointer drags stream
        // many small deltas and the gesture state machine must track each one.
        const auto Board = Workspace.QueryBoardExtent();
        const ImVec2 Start(Board.MinimumX + Board.Width() * .2f, Board.MinimumY + Board.Height() * .2f);
        const ImVec2 Finish(Board.MinimumX + Board.Width() * .6f, Board.MinimumY + Board.Height() * .4f);
        Input.AddMousePosEvent(Start.x, Start.y); Frame();
        Input.AddMouseButtonEvent(0, true); Frame();
        for (int Index = 1; Index <= 6; ++Index)
        {
            const float Parameter = float(Index) / 6;
            Input.AddMousePosEvent(Start.x + (Finish.x - Start.x) * Parameter, Start.y + (Finish.y - Start.y) * Parameter);
            Frame();
        }
        Input.AddMouseButtonEvent(0, false); Frame(); Frame(); Frame();

        std::cout << "After drag+release: Diagnostic='" << Workspace.QueryDiagnostic()
            << "' Settled=" << Workspace.QueryAuthoringSettled()
            << " Layers=" << Workspace.QueryDocument().Layers.size() << "\n";
        Require(!Workspace.QueryAuthoringSettled(), "Dragging out a text box did not leave the panel mid-gesture (composer never opened).");
        Require(Workspace.QueryDiagnostic().empty(), "An exception fired while opening the text composer.");

        for (const char Character : std::string("Hello world"))
        {
            Input.AddInputCharacter((unsigned)Character);
            Frame();
        }
        for (int Index = 0; Index < 3; ++Index) Frame();
        std::cout << "After typing: Settled=" << Workspace.QueryAuthoringSettled()
            << " Layers=" << Workspace.QueryDocument().Layers.size() << "\n";

        Input.AddKeyEvent(ImGuiKey_Enter, true); Frame();
        Input.AddKeyEvent(ImGuiKey_Enter, false); Frame(); Frame(); Frame();

        std::cout << "After commit: Diagnostic='" << Workspace.QueryDiagnostic()
            << "' Settled=" << Workspace.QueryAuthoringSettled()
            << " Layers=" << Workspace.QueryDocument().Layers.size() << "\n";

        Require(Workspace.QueryAuthoringSettled(), "Panel did not return to the idle gesture state after committing text.");
        Require(Workspace.QueryDocument().Layers.size() == 1, "Committing the text composer did not add exactly one layer.");
        const auto &Layer = Workspace.QueryDocument().Layers.front();
        Require(Layer.ComponentKind == VectorAuthoringComponentKind::Text, "Committed layer was not a Text component.");
        std::cout << "SourceMarkup='" << Layer.SourceMarkup << "' Name='" << Layer.Name << "'\n";
        Require(Layer.SourceMarkup.find("Hello world") != std::string::npos, "Committed Text layer lost the typed content.");
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "StampTextGestureValidation failed: " << Failure.what() << "\n";
        return 1;
    }
    std::cout << "StampTextGestureValidation passed.\n";
    return 0;
}
