/*====================================================================================================================================
                                                PROJECTBROWSERPRESENTATION.CPP
====================================================================================================================================*/
// 🧩 Native Frontier application and project selection screens in the shared ControlKit palette.

#include "ProjectBrowserPresentation.h"

#include "Engine/Interface/ControlKit.h"
#include "Engine/Interface/ProjectContent.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace Frontier
{

namespace
{

ImVec4 ToImVec4(const ColorQuad& Colour) noexcept
{
    return ImVec4(Colour.Red, Colour.Green, Colour.Blue, Colour.Alpha);
}

ProjectBrowserButtonBounds CurrentButtonBounds() noexcept
{
    const ImVec2 Minimum = ImGui::GetItemRectMin();
    const ImVec2 Maximum = ImGui::GetItemRectMax();
    return {Minimum.x, Minimum.y, Maximum.x, Maximum.y, true};
}

void ConstructApplicationPage(ProjectBrowserPresentation& Presentation)
{
    const ControlKitPalette& Palette = ControlKit::Palette();
    ImGui::TextColored(ToImVec4(Palette.TextDim), "APPLICATION");
    ImGui::Spacing();
    ImGui::TextUnformatted("Select an application");
    ImGui::TextColored(ToImVec4(Palette.TextDim),
                       "Open the Frontier Editor, then choose a .frontier project.");
    ImGui::Spacing();

    const float CardWidth = std::min(720.0f, std::max(420.0f, ImGui::GetContentRegionAvail().x - 56.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(Palette.Card));
    ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(Palette.StrokeStrong));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
    if (ImGui::BeginChild("##FrontierEditorCard", ImVec2(CardWidth, 230.0f), true))
    {
        ImGui::TextColored(ToImVec4(Palette.Accent), "F");
        ImGui::SameLine();
        ImGui::TextUnformatted("Frontier Editor");
        ImGui::TextColored(ToImVec4(Palette.TextDim), "Native C++ editor workspace");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("The editor opens .frontier projects with the shared dockspace, Control Centre, "
                           "Outliner, Inspector, and Log Stream.");
        ImGui::Spacing();
        const bool OpenEditor = ImGui::Button("Open Frontier Editor", ImVec2(196.0f, 38.0f));
        Presentation.ApplicationButton = CurrentButtonBounds();
        if (OpenEditor)
        {
            Presentation.Page = ProjectBrowserPage::Projects;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void ConstructProjectPage(ProjectBrowserPresentation& Presentation)
{
    const ControlKitPalette& Palette = ControlKit::Palette();
    if (ImGui::Button("< Applications", ImVec2(148.0f, 32.0f)))
    {
        Presentation.Page = ProjectBrowserPage::Applications;
    }
    ImGui::Spacing();
    ImGui::TextColored(ToImVec4(Palette.TextDim), "FRONTIER EDITOR");
    ImGui::Spacing();
    ImGui::TextUnformatted("Open a project");
    ImGui::TextColored(ToImVec4(Palette.TextDim), "Select a .frontier project to open its native workspace.");
    ImGui::Spacing();
    Presentation.ProjectOpenButtons.assign(Presentation.Listing.Entries.size(), {});

    for (size_t Index = 0; Index < Presentation.Listing.Entries.size(); ++Index)
    {
        const ProjectBrowserEntry& Entry = Presentation.Listing.Entries[Index];
        ImGui::PushID(static_cast<int>(Index));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImVec4(Palette.Card));
        ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(Palette.StrokeStrong));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        if (ImGui::BeginChild("##ProjectCard", ImVec2(-1.0f, 100.0f), true))
        {
            ImGui::TextUnformatted(Entry.Descriptor.Name.c_str());
            ImGui::TextColored(ToImVec4(Palette.TextDim), "%s",
                               EncodeUtf8Path(Entry.Descriptor.ProjectDirectory).c_str());
            ImGui::SameLine();
            const bool OpenProject = ImGui::Button("Open project", ImVec2(144.0f, 32.0f));
            Presentation.ProjectOpenButtons[Index] = CurrentButtonBounds();
            if (OpenProject)
            {
                Presentation.ChosenDescriptor = Entry.Descriptor.DescriptorPath;
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        ImGui::Spacing();
    }

    if (Presentation.Listing.Entries.empty())
    {
        ImGui::TextColored(ToImVec4(Palette.TextDim), "No .frontier projects were found.");
    }
    for (const std::string& Diagnostic : Presentation.Listing.Diagnostics)
    {
        ImGui::TextColored(ToImVec4(Palette.Warning), "Skipped: %s", Diagnostic.c_str());
    }
}

} // namespace

void ConstructProjectBrowserInterface(ProjectBrowserPresentation& Presentation)
{
    Presentation.ApplicationButton = {};
    Presentation.ProjectOpenButtons.clear();
    const ImVec2 Display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(Display);

    const ControlKitPalette& Palette = ControlKit::Palette();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(Palette.Panel));
    ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(Palette.Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ToImVec4(Palette.TextDim));
    ImGui::PushStyleColor(ImGuiCol_Button, ToImVec4(Palette.Raised));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToImVec4(Palette.Selected));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToImVec4(Palette.Selected));
    ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(Palette.StrokeStrong));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f, 28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);

    if (ImGui::Begin("Frontier Project Browser", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
    {
        ImGui::TextColored(ToImVec4(Palette.Accent), "FRONTIER");
        ImGui::SameLine();
        ImGui::TextColored(ToImVec4(Palette.TextDim), "/ PROJECT BROWSER");
        ImGui::Separator();
        ImGui::Spacing();
        if (Presentation.Page == ProjectBrowserPage::Applications)
        {
            ConstructApplicationPage(Presentation);
        }
        else
        {
            ConstructProjectPage(Presentation);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(7);
}

} // namespace Frontier
