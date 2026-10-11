//============================================================================================================================================
//                                                           PROJECTOPENINGPANEL.H
//============================================================================================================================================
// 📦 Shared OLED-black ImGui startup card with white controls, project previews and host-owned actions.

#pragma once
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Frontier
{
struct ProjectOpeningPreview
{
    ImTextureRef          Texture;
    int                   Width = 0, Height = 0;
    std::filesystem::path Source;
    std::string           Refusal;
};

struct ProjectOpeningSpecification
{
    char                                                   Directory[4096]{};
    std::vector<std::filesystem::path>                     Projects, Scenes, PreviewRequests;
    std::map<std::filesystem::path, ProjectOpeningPreview> Previews;
    std::filesystem::path                                  SelectedProject, SelectedScene;
    std::string                                            Refusal;
    int                                                    Quality = 2, Rendering = 0, Scale = 2;
    bool   Loading = false, Close = false, Scan = false, BrowseDirectory = false, BrowseScene = false;
    bool   SelectionChanged = false, Open = false;
    double LoadingSeconds = 0.0;
};

enum class ProjectOpeningGlyph
{
    Folder,
    Scene,
    Quality,
    Renderer,
    Scale,
    Refresh
};

inline std::string OpeningPathText(const std::filesystem::path &Path)
{
    const auto Text = Path.u8string();
    return {reinterpret_cast<const char *>(Text.data()), Text.size()};
}

inline void ApplyProjectOpeningTheme()
{
    ImGui::StyleColorsDark();
    auto &Style                           = ImGui::GetStyle();
    Style.WindowRounding                  = 16;
    Style.ChildRounding                   = 12;
    Style.FrameRounding                   = 18;
    Style.PopupRounding                   = 12;
    Style.WindowPadding                   = ImVec2(18, 16);
    Style.FrameBorderSize                 = 1;
    Style.FramePadding                    = ImVec2(12, 7);
    Style.ItemSpacing                     = ImVec2(10, 10);
    Style.Colors[ImGuiCol_Text]           = ImVec4(.94f, .94f, .94f, 1);
    Style.Colors[ImGuiCol_TextDisabled]   = ImVec4(.52f, .52f, .52f, 1);
    Style.Colors[ImGuiCol_WindowBg]       = ImVec4(0, 0, 0, 1);
    Style.Colors[ImGuiCol_ChildBg]        = ImVec4(.025f, .025f, .025f, 1);
    Style.Colors[ImGuiCol_Border]         = ImVec4(.18f, .18f, .18f, 1);
    Style.Colors[ImGuiCol_FrameBg]        = ImVec4(.065f, .065f, .065f, 1);
    Style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(.14f, .14f, .14f, 1);
    Style.Colors[ImGuiCol_FrameBgActive]  = ImVec4(.19f, .19f, .19f, 1);
    Style.Colors[ImGuiCol_Header]         = ImVec4(.18f, .18f, .18f, 1);
    Style.Colors[ImGuiCol_HeaderHovered]  = ImVec4(.24f, .24f, .24f, 1);
    Style.Colors[ImGuiCol_HeaderActive]   = ImVec4(.30f, .30f, .30f, 1);
    Style.Colors[ImGuiCol_NavCursor]      = ImVec4(1, 1, 1, 1);
    Style.Colors[ImGuiCol_PopupBg]        = ImVec4(.045f, .045f, .045f, 1);
    Style.Colors[ImGuiCol_Button]         = ImVec4(.09f, .09f, .09f, 1);
    Style.Colors[ImGuiCol_ButtonHovered]  = ImVec4(.19f, .19f, .19f, 1);
    Style.Colors[ImGuiCol_ButtonActive]   = ImVec4(.25f, .25f, .25f, 1);
    Style.Colors[ImGuiCol_CheckMark]      = ImVec4(1, 1, 1, 1);
    Style.Colors[ImGuiCol_SliderGrab] = Style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(.9f, .9f, .9f, 1);
    Style.Colors[ImGuiCol_TextSelectedBg]                                       = ImVec4(.35f, .35f, .35f, .7f);
}

inline void RecordOpeningGlyph(ImDrawList *List, ImVec2 Centre, ProjectOpeningGlyph Glyph)
{
    const ImU32 Colour = ImGui::GetColorU32(ImGuiCol_Text);
    const auto  Line   = [&](float Left, float Top, float Right, float Bottom)
    { List->AddLine(ImVec2(Centre.x + Left, Centre.y + Top), ImVec2(Centre.x + Right, Centre.y + Bottom), Colour, 1.4f); };
    switch (Glyph)
    {
    case ProjectOpeningGlyph::Folder:
        Line(-7, -3, -7, 5);
        Line(-7, 5, 7, 5);
        Line(7, 5, 7, -3);
        Line(7, -3, 0, -3);
        Line(0, -3, -2, -6);
        Line(-2, -6, -7, -6);
        Line(-7, -6, -7, -3);
        break;
    case ProjectOpeningGlyph::Scene:
        Line(-7, 0, 0, -5);
        Line(0, -5, 7, 0);
        Line(7, 0, 0, 5);
        Line(0, 5, -7, 0);
        Line(-7, 4, 0, 9);
        Line(0, 9, 7, 4);
        break;
    case ProjectOpeningGlyph::Quality:
        Line(-5, 5, -5, 0);
        Line(0, 5, 0, -4);
        Line(5, 5, 5, -8);
        break;
    case ProjectOpeningGlyph::Renderer:
        List->AddRect(ImVec2(Centre.x - 6, Centre.y - 6), ImVec2(Centre.x + 6, Centre.y + 6), Colour, 2, 0, 1.4f);
        Line(-3, -9, -3, -6);
        Line(3, -9, 3, -6);
        Line(-3, 6, -3, 9);
        Line(3, 6, 3, 9);
        Line(-9, -3, -6, -3);
        Line(6, 3, 9, 3);
        break;
    case ProjectOpeningGlyph::Scale:
        Line(-6, 0, -6, -6);
        Line(-6, -6, 0, -6);
        Line(6, 0, 6, 6);
        Line(6, 6, 0, 6);
        Line(-5, -5, 5, 5);
        break;
    case ProjectOpeningGlyph::Refresh:
        List->PathArcTo(Centre, 6, .4f, 5.5f, 18);
        List->PathStroke(Colour, 0, 1.4f);
        Line(4, -5, 7, -5);
        Line(7, -5, 7, -8);
        break;
    }
}

inline bool BeginOpeningChoice(const char *Identity, const char *Caption, ProjectOpeningGlyph Glyph)
{
    auto        *List     = ImGui::GetWindowDrawList();
    const ImVec2 Position = ImGui::GetCursorScreenPos();
    const float  Width    = ImGui::CalcItemWidth();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(34, 8));
    const bool  Expanded = ImGui::BeginCombo(Identity, Caption, ImGuiComboFlags_NoArrowButton);
    const float Height   = ImGui::GetFrameHeight();
    ImGui::PopStyleVar();
    RecordOpeningGlyph(List, ImVec2(Position.x + 17, Position.y + Height * .5f), Glyph);
    const ImU32 Colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float Centre = Position.y + Height * .5f;
    List->AddLine(ImVec2(Position.x + Width - 21, Centre - 2), ImVec2(Position.x + Width - 16, Centre + 3), Colour, 1.5f);
    List->AddLine(ImVec2(Position.x + Width - 16, Centre + 3), ImVec2(Position.x + Width - 11, Centre - 2), Colour, 1.5f);
    return Expanded;
}

inline void RecordOpeningChoice(const char *Identity, int &Selection, const char *const *Choices, int Count, ProjectOpeningGlyph Glyph)
{
    ImGui::SetNextItemWidth(-1);
    if (BeginOpeningChoice(Identity, Choices[Selection], Glyph))
    {
        for (int Index = 0; Index < Count; ++Index)
        {
            if (ImGui::Selectable(Choices[Index], Selection == Index))
                Selection = Index;
            if (Selection == Index)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

inline void RecordOpeningPreview(ProjectOpeningSpecification &Opening, const std::filesystem::path &Project, ImVec2 Position, ImVec2 Size)
{
    auto *List = ImGui::GetWindowDrawList();
    List->AddRectFilled(Position, ImVec2(Position.x + Size.x, Position.y + Size.y), IM_COL32(18, 18, 18, 255), 8);
    if (!Project.empty() &&
        std::find(Opening.PreviewRequests.begin(), Opening.PreviewRequests.end(), Project) == Opening.PreviewRequests.end())
        Opening.PreviewRequests.push_back(Project);
    const auto Found = Opening.Previews.find(Project);
    if (Found != Opening.Previews.end() && Found->second.Width > 0 && Found->second.Height > 0)
    {
        const auto  &Preview = Found->second;
        const float  Scale   = std::min(Size.x / Preview.Width, Size.y / Preview.Height);
        const ImVec2 Extent(Preview.Width * Scale, Preview.Height * Scale);
        const ImVec2 Origin(Position.x + (Size.x - Extent.x) * .5f, Position.y + (Size.y - Extent.y) * .5f);
        List->AddImageRounded(Preview.Texture, Origin, ImVec2(Origin.x + Extent.x, Origin.y + Extent.y), ImVec2(0, 0), ImVec2(1, 1),
                              ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), 6);
    }
    else
        RecordOpeningGlyph(List, ImVec2(Position.x + Size.x * .5f, Position.y + Size.y * .5f), ProjectOpeningGlyph::Folder);
}

inline void RecordProjectOpeningPanel(ProjectOpeningSpecification &State)
{
    State.PreviewRequests.clear();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(840, 640));
    ImGui::Begin("Project browser", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("FRONTIER");
    ImGui::SameLine();
    ImGui::TextDisabled(" /  PROJECT BROWSER");
    ImGui::SameLine(784);
    const ImVec2 ClosePosition = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("Close project browser", ImVec2(32, 26)))
        State.Close = true;
    auto        *List = ImGui::GetWindowDrawList();
    const ImVec2 CloseCentre(ClosePosition.x + 16, ClosePosition.y + 13);
    List->AddCircleFilled(CloseCentre, 13, ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button), 32);
    List->AddCircle(CloseCentre, 13, ImGui::GetColorU32(ImGuiCol_Border), 32);
    const ImU32 Cross = ImGui::GetColorU32(ImGuiCol_Text);
    List->AddLine(ImVec2(CloseCentre.x - 4, CloseCentre.y - 4), ImVec2(CloseCentre.x + 4, CloseCentre.y + 4), Cross, 1.5f);
    List->AddLine(ImVec2(CloseCentre.x + 4, CloseCentre.y - 4), ImVec2(CloseCentre.x - 4, CloseCentre.y + 4), Cross, 1.5f);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(State.Loading ? "Cancel project startup" : "Close");
    ImGui::Separator();
    ImGui::TextDisabled("Your projects. Your next session.");
    ImGui::BeginDisabled(State.Loading);
    ImGui::SetNextItemWidth(558);
    ImGui::InputText("##directory", State.Directory, sizeof(State.Directory));
    ImGui::SameLine();
    if (ImGui::Button("Browse folder", ImVec2(144, 0)))
        State.BrowseDirectory = true;
    ImGui::SameLine();
    if (ImGui::Button("Scan", ImVec2(82, 0)))
        State.Scan = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Refresh projects and preview images");
    ImGui::BeginChild("Projects", ImVec2(296, 372), true);
    RecordOpeningPreview(State, State.SelectedProject, ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetContentRegionAvail().x, 124));
    ImGui::Dummy(ImVec2(0, 124));
    if (ImGui::IsItemHovered())
    {
        const auto Preview = State.Previews.find(State.SelectedProject);
        if (Preview != State.Previews.end())
            ImGui::SetTooltip("%s", Preview->second.Refusal.empty() ? OpeningPathText(Preview->second.Source).c_str()
                                                                    : Preview->second.Refusal.c_str());
    }
    ImGui::TextDisabled("PROJECTS / %zu", State.Projects.size());
    ImGui::BeginChild("Project list", ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 6));
    ImGuiListClipper Clipper;
    Clipper.Begin(static_cast<int>(State.Projects.size()), 54);
    while (Clipper.Step())
        for (int Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
        {
            const auto &Project = State.Projects[Index];
            ImGui::PushID(OpeningPathText(Project).c_str());
            const ImVec2 Position = ImGui::GetCursorScreenPos();
            const float  Width    = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("Project", ImVec2(Width, 48)))
            {
                State.SelectedProject  = Project;
                State.SelectionChanged = true;
            }
            auto *Rows = ImGui::GetWindowDrawList();
            if (Project == State.SelectedProject || ImGui::IsItemHovered())
                Rows->AddRectFilled(Position, ImVec2(Position.x + Width, Position.y + 48), ImGui::GetColorU32(ImGuiCol_Header), 10);
            RecordOpeningPreview(State, Project, ImVec2(Position.x + 5, Position.y + 5), ImVec2(60, 38));
            Rows->PushClipRect(ImVec2(Position.x + 76, Position.y), ImVec2(Position.x + Width - 4, Position.y + 48), true);
            Rows->AddText(ImVec2(Position.x + 76, Position.y + 15), ImGui::GetColorU32(ImGuiCol_Text),
                          OpeningPathText(Project.stem()).c_str());
            Rows->PopClipRect();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", OpeningPathText(Project).c_str());
            ImGui::PopID();
        }
    ImGui::PopStyleVar();
    if (State.Projects.empty())
        ImGui::TextWrapped("No .frontier projects found. Choose a folder and scan it.");
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Opening", ImVec2(0, 372), true);
    ImGui::TextDisabled("SCENE & SESSION");
    ImGui::SetNextItemWidth(-1);
    if (BeginOpeningChoice("##scene",
                           State.SelectedScene.empty() ? "Choose a project" : OpeningPathText(State.SelectedScene.filename()).c_str(),
                           ProjectOpeningGlyph::Scene))
    {
        for (const auto &Scene : State.Scenes)
        {
            ImGui::PushID(OpeningPathText(Scene).c_str());
            if (ImGui::Selectable(OpeningPathText(Scene.filename()).c_str(), Scene == State.SelectedScene))
                State.SelectedScene = Scene;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", OpeningPathText(Scene).c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Browse scene..."))
        State.BrowseScene = true;
    ImGui::TextDisabled("Quality");
    const char *Qualities[] = {"Minimal", "Economy", "Standard", "Ultra", "Reference"};
    RecordOpeningChoice("##quality", State.Quality, Qualities, 5, ProjectOpeningGlyph::Quality);
    ImGui::TextDisabled("Renderer");
    const char *Renderers[] = {"ReSTIR / Software BVH (testing)", "ReSTIR / Automatic hardware", "Distance Field GI", "Raster / No GI"};
    RecordOpeningChoice("##renderer", State.Rendering, Renderers, 4, ProjectOpeningGlyph::Renderer);
    ImGui::TextDisabled("Render scale");
    const char *Scales[] = {"50%", "75%", "100%"};
    RecordOpeningChoice("##scale", State.Scale, Scales, 3, ProjectOpeningGlyph::Scale);
    ImGui::TextDisabled("Session overrides. Software BVH runs on the GPU.");
    ImGui::EndChild();
    ImGui::EndDisabled();
    ImGui::TextDisabled("Custom preview: place an image in your project's Preview folder, then Scan.");
    if (!State.Refusal.empty())
        ImGui::TextWrapped("%s", State.Refusal.c_str());
    ImGui::SetCursorPosY(574);
    if (State.Loading)
    {
        ImGui::Text("Opening project...  %.0f seconds", State.LoadingSeconds);
        ImGui::TextDisabled("Building renderer resources. Console stays open until the first frame.");
    }
    else
    {
        ImGui::BeginDisabled(State.SelectedProject.empty() || State.SelectedScene.empty());
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.94f, .94f, .94f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.75f, .75f, .75f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.03f, .03f, .03f, 1));
        if (ImGui::Button("Open project", ImVec2(200, 38)))
            State.Open = true;
        ImGui::PopStyleColor(4);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Frontier.exe + your selected project.");
    }
    ImGui::End();
}
} // namespace Frontier
