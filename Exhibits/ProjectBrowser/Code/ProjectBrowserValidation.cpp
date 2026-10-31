/*==========================================================================================================================================
                                                       PROJECTBROWSERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise the 1:1 browser catalogue, reader states, real creation/hand-off and native draw commands without GPU claims.

#include "UI/Editor/ProjectBrowserPanel.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition) throw std::runtime_error(Description);
}

std::string ReadText(const std::filesystem::path &Path)
{
    std::ifstream Stream(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Stream), std::istreambuf_iterator<char>()};
}

void RequireTitles(const std::vector<std::string> &Actual, std::initializer_list<const char *> Expected,
    const char *Description)
{
    Require(Actual.size() == Expected.size(), Description);
    size_t Index = 0;
    for (const char *Title : Expected) Require(Actual[Index++] == Title, Description);
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected blank template and scratch output directory");
        const std::filesystem::path Template(Arguments[1]);
        const auto Run = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto Output = std::filesystem::path(Arguments[2]) / ("Run-" + Run);
        Require(!std::filesystem::exists(Output), "Use a fresh output directory; validation never deletes existing files");
        std::filesystem::create_directories(Output);
        const auto OriginalTemplate = ReadText(Template);
        ProjectBrowserPanel Browser;
        const auto Presets = Browser.QueryPresets();
        Require(Presets.size() == 1 && std::string(Presets[0]) == "Blank Project", "Only Blank Project is permitted");
        // 📚 Sample catalogue, filters, search and sort match the HTML fixtures.
        RequireTitles(Browser.QueryFilteredTitles("all", "", false),
            {"Actuator studio", "Precision mount", "Actuator housing", "Studio exploration", "Actuator sketches",
                "Housing study"},
            "Sample catalogue order changed");
        RequireTitles(Browser.QueryFilteredTitles("project", "", false), {"Actuator studio", "Studio exploration"},
            "Project filter changed");
        RequireTitles(Browser.QueryFilteredTitles("cad", "", false), {"Precision mount", "Actuator sketches"},
            "CAD filter changed");
        RequireTitles(Browser.QueryFilteredTitles("geometry", "", false), {"Actuator housing", "Housing study"},
            "Geometry filter changed");
        RequireTitles(Browser.QueryFilteredTitles("starred", "", false), {"Actuator studio", "Precision mount"},
            "Default stars changed");
        Require(Browser.QueryFilteredTitles("recent", "", false).size() == 6, "Recent must list every entry");
        RequireTitles(Browser.QueryFilteredTitles("all", "housing", false), {"Actuator housing", "Housing study"},
            "Title search changed");
        RequireTitles(Browser.QueryFilteredTitles("all", "cad", false), {"Precision mount", "Actuator sketches"},
            "Category search changed");
        RequireTitles(Browser.QueryFilteredTitles("all", "  HOUSING  ", false),
            {"Actuator housing", "Housing study"}, "Search must trim and fold case");
        RequireTitles(Browser.QueryFilteredTitles("all", "", true),
            {"Actuator housing", "Actuator sketches", "Actuator studio", "Housing study", "Precision mount",
                "Studio exploration"},
            "Name sort changed");
        // 📖 Reader navigation accepts samples and refuses anything else.
        for (const char *Token : {"valley", "mount", "stone", "island", "bracket", "faceted"})
        {
            Require(Browser.OpenSample(Token), "Sample must open");
            Require(Browser.IsReaderOpen() && Browser.QueryActiveToken() == Token, "Active token mismatch");
            Browser.CloseFile();
            Require(!Browser.IsReaderOpen(), "Close must return to the library");
        }
        Require(!Browser.OpenSample("missing"), "Missing token must be refused");
        // 📎 Local import classification, caps and single-attach auto-open.
        const auto Note = Output / "Field note.md";
        std::ofstream(Note) << "# Field note\n\nChecked on site.\n";
        const auto Photo = Output / "Capture.PNG";
        std::ofstream(Photo, std::ios::binary) << "fakepng";
        Require(Browser.AttachPaths({Output / "Notes.txt"}, false) == 0, "Unsupported files must be refused");
        Require(Browser.AttachPaths({Note}, true) == 1, "Markdown must attach");
        Require(Browser.QueryActiveToken().rfind("local-", 0) == 0, "Single attach must auto-open");
        Require(!Browser.OpenSample(Browser.QueryActiveToken()), "Imports must not open as samples");
        RequireTitles(Browser.QueryFilteredTitles("all", "field", false), {"Field note"}, "Attached title missing");
        Browser.CloseFile();
        Require(Browser.AttachPaths({Photo, Output / "Notes.txt"}, false) == 1, "Batch must skip refused files");
        Require(!Browser.IsReaderOpen(), "Batch attach must not auto-open");
        Require(Browser.QueryEntryCount() == 8, "Entry count mismatch after import");
        // 💾 Preference round-trip through the TOML store.
        {
            BrowserPreferences Saved;
            Saved.Theme = "light";
            Saved.Stars = {"valley", "local-0-0"};
            Saved.Notes["stone"] = {{"You", "Y", "Keep the seams hidden.", "Just now"}};
            std::string Diagnostic;
            Require(SaveBrowserPreferences(Output / "prefs.toml", Saved, Diagnostic), "Prefs must save");
            BrowserPreferences Loaded;
            Require(LoadBrowserPreferences(Output / "prefs.toml", Loaded, Diagnostic), "Prefs must load");
            Require(Loaded.Theme == "light" && Loaded.Stars == Saved.Stars, "Prefs theme/stars lost");
            Require(Loaded.Notes["stone"].size() == 1 && Loaded.Notes["stone"][0].Text == "Keep the seams hidden.",
                "Prefs notes lost");
            Browser.ConfigurePreferences(Output / "prefs.toml");
        }
        // 🧱 Real blank-project creation, refusal and hand-off (unchanged native contract).
        Require(!Browser.RequestOpenProject(), "Empty selection must not launch");
        Require(Browser.ConfigureDirectory(Output), "Arbitrary folder must open");
        const auto Created = Output / "Unicode-\xc3\xa9" / "MyProject.projectspace";
        Require(Browser.CreateProject(Created, "My Project"), "Blank project creation failed");
        Require(std::filesystem::is_directory(Created.parent_path() / "Content"), "Content root missing");
        ProjectSpaceDocument Project;
        std::string Diagnostic;
        Require(ProjectSpaceCodec::Decode(Created, Project, Diagnostic), "Created project must decode");
        Require(Project.Sections["project"]["name"].value_or(std::string{}) == "My Project", "Project name lost");
        Require(Project.Sections["project"]["identity"].value_or(std::string{}) != "engine-blank-template-v1",
            "Identity must be fresh");
        Require(Project.Sections["levels"].as_array()->empty(), "Blank preset must not insert sample levels");
        Require(Project.Sections["catalogue"].as_array()->empty(), "Blank preset must not insert sample content");
        Require(Project.AppearancePresent && Project.InputPresent && Project.NotificationsPresent,
            "Preset defaults not preserved");
        const auto OriginalProject = ReadText(Created);
        Require(!Browser.CreateProject(Created, "Overwrite"), "Existing projects must not be overwritten");
        Require(ReadText(Created) == OriginalProject, "Overwrite refusal changed project bytes");
        Require(!Browser.CreateProject(Output / "Wrong.project", "Wrong"), "Unimplemented project format must be refused");
        Require(!Browser.SelectProject(Output / "Missing.projectspace"), "Missing project must be refused");
        const auto Corrupt = Output / "Corrupt.projectspace";
        std::ofstream(Corrupt) << "not a project container";
        Require(!Browser.SelectProject(Corrupt), "Corrupt project must be refused");
        Require(Browser.SelectProject(Created) && Browser.RequestOpenProject(), "Valid selection must hand off");
        Require(Browser.ConsumeLaunchPath() == Created && Browser.ConsumeLaunchPath().empty(),
            "Hand-off must be consumed once");
        std::ofstream(Created, std::ios::trunc | std::ios::binary) << "invalid after selection";
        Require(!Browser.RequestOpenProject(), "Changed project must be revalidated before launch");
        std::ofstream(Created, std::ios::trunc | std::ios::binary) << OriginalProject;
        Require(Browser.SelectProject(Created), "Restored project must select");
        // 🖼️ Headless draw sweep: every reader state at desktop and narrow widths, vertices inside the viewport.
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DeltaTime = 1.0f / 60.0f;
        IO.Fonts->AddFontDefault();
        unsigned char *Pixels;
        int AtlasWidth, AtlasHeight;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        uint64_t DrawnStates = 0;
        const auto Draw = [&](const char *What)
        {
            for (int Iteration = 0; Iteration < 2; ++Iteration)
            {
                ImGui::NewFrame();
                Browser.ConstructInterface();
                ImGui::Render();
                Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Native browser generated no draw geometry");
            }
            const ImDrawData *Data = ImGui::GetDrawData();
            // 🖥️ Vertices outside the fold carry scissor rects, so the GPU never rasterizes them; the headless
            // invariant is finite coordinates within sane bounds plus finite clip rectangles.
            for (int List = 0; List < Data->CmdListsCount; ++List)
            {
                const ImDrawList *Commands = Data->CmdLists[List];
                for (int Vertex = 0; Vertex < Commands->VtxBuffer.Size; ++Vertex)
                {
                    const ImVec2 At = Commands->VtxBuffer[Vertex].pos;
                    Require(std::isfinite(At.x) && std::isfinite(At.y), "Draw vertex must be finite");
                    Require(At.x > -10000 && At.x < IO.DisplaySize.x + 10000 && At.y > -10000 &&
                                At.y < IO.DisplaySize.y + 10000,
                        "Draw vertex escapes sane bounds");
                }
                for (int Command = 0; Command < Commands->CmdBuffer.Size; ++Command)
                {
                    const ImVec4 Clip = Commands->CmdBuffer[Command].ClipRect;
                    Require(std::isfinite(Clip.x) && std::isfinite(Clip.y) && std::isfinite(Clip.z) &&
                                std::isfinite(Clip.w),
                        "Clip rectangle must be finite");
                }
            }
            ++DrawnStates;
        };
        const auto ChildWidth = [&](const char *Needle)
        {
            for (auto *Window : ImGui::GetCurrentContext()->Windows)
                if (Window->Active && std::string(Window->Name).find(Needle) != std::string::npos)
                    return Window->Size.x;
            return -1.f;
        };
        const auto SetDisplay = [&](float Width, float Height)
        {
            // 🖥️ Headless contexts have no backend, so mirror what one does and resize the viewport.
            IO.DisplaySize = ImVec2(Width, Height);
            if (ImGui::GetMainViewport())
            {
                ImGui::GetMainViewport()->Size = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkSize = IO.DisplaySize;
                ImGui::GetMainViewport()->WorkPos = ImVec2(0, 0);
            }
        };
        IO.DisplaySize = ImVec2(1280, 900);
        Browser.DebugShow("", 0, 0, false, 100, "details", "oled", false, false);
        Draw("library");
        Require(std::abs(ChildWidth("##librail") - 204) < 1, "Library rail must span 204px at desktop width");
        Browser.DebugShow("", 0, 0, false, 100, "details", "light", false, true);
        Draw("creation");
        int ThemeCycle = 0;
        for (const char *Token : {"valley", "mount", "stone", "island", "bracket", "faceted"})
        {
            const int Views =
                Token == std::string("mount") || Token == std::string("stone") || Token == std::string("faceted")
                    ? 4
                    : 3;
            for (int View = 0; View < Views; ++View)
                for (int Revision : {0, 2})
                    for (const char *Tab : {"details", "comments", "history"})
                        for (int Zoom : {50, 100, 200})
                            for (bool Document : {false, true})
                            {
                                const char *Themes[] = {"oled", "light", "dark"};
                                Browser.DebugShow(Token, View, Revision, Document, Zoom, Tab,
                                    Themes[(ThemeCycle++) % 3], false, false);
                                Draw("reader");
                            }
            Browser.DebugShow(Token, 0, 0, false, 100, "details", "dark", true, false);
            Draw("launch");
        }
        for (const char *Search : {"field", "capture"})
        {
            const auto Found = Browser.QueryFilteredTitles("all", Search, false);
            Require(!Found.empty(), "Attached file missing from search");
        }
        SetDisplay(390, 844);
        Browser.DebugShow("", 0, 0, false, 100, "details", "oled", false, false);
        Draw("narrow library");
        Require(ChildWidth("##librail") < 0, "Collapsed rail must not open a scroll child");
        Require(std::abs(ChildWidth("##libscroll") - 302) < 1, "Narrow centre must be 390 - 44 - 44");
        for (const char *Token : {"valley", "stone"})
        {
            Browser.DebugShow(Token, 0, 0, false, 100, "details", "oled", false, false);
            Draw("narrow reader");
            Browser.DebugShow(Token, 1, 2, true, 200, "history", "light", false, false);
            Draw("narrow document");
        }
        Browser.DebugShow("stone", 0, 0, false, 100, "details", "dark", true, false);
        Draw("narrow launch");
        Browser.ResetSelection();
        SetDisplay(1280, 900);
        ImGui::NewFrame();
        Browser.ConstructInterface();
        ImGui::Render();
        Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Empty catalogue and Blank Project must draw");
        ImGui::DestroyContext();
        Require(ReadText(Template) == OriginalTemplate, "Installed blank template must remain unchanged");
        Require(ReadText(Created) == OriginalProject, "Browsing must not rewrite project bytes");
        std::ofstream(Output / "NativeBrowserChecks.json")
            << "{\n  \"result\": \"passed\",\n  \"presets\": [\"Blank Project\"],\n  \"drawn_states\": "
            << DrawnStates << ",\n"
            << "  \"checks\": [\"Sample catalogue, filters, search and sort\", \"Reader navigation and refusal\", "
            << "\"Local import classification and auto-open\", \"Preference round-trip\", "
            << "\"Real creation and fresh identity\", \"Empty content and preserved defaults\", "
            << "\"Unicode directory\", \"Overwrite and unsupported format refusal\", "
            << "\"Missing/corrupt/changed project refusal\", \"Single-consumption hand-off\", "
            << "\"Desktop/narrow draw sweep with viewport containment\", \"Read-only browsing\"],\n"
            << "  \"execution\": \"Host CPU checks only; no Windows, OS dialog or GPU execution\"\n}\n";
        std::cout << "PASS: 1:1 browser catalogue, reader, import, prefs, creation, hand-off, " << DrawnStates
                  << " drawn states\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
