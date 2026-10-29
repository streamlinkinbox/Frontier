/*==========================================================================================================================================
                                                     WORKSPACELAYOUTVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Restart real editor contexts to verify personal projectspace layout persistence, guarded fallback, and native captures.

#include "Engine/Interface/IconPresentation.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/EditorWorkspace.h"
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{
using namespace Frontier;
const char *ContentName = "Content Browser###FrontierPanel-content";
const char *StreamName  = "Notification Stream###FrontierPanel-notifications";
void        Require(bool Condition, const char *Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
std::string Bytes(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
}
class Session
{
  public:
    ActivationOwnership              Ownership;
    ControlCentrePanel              *Centre = nullptr;
    std::unique_ptr<EditorWorkspace> Workspace;
    Session(const std::filesystem::path &Content, const std::filesystem::path &Personal, bool Dockable = false)
    {
        ImGui::CreateContext();
        auto &IO       = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1100, 1000);
        IO.DeltaTime   = 1.0f / 60;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth = ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        Ownership.reset(new ControlCentrePanel(Content / "FontArchives", Personal));
        Centre = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Centre->Initialize() && Centre->Activate(), "Centre initialization failed");
        if (Dockable)
        {
            Centre->ConfigureDockableBrowserDraft(true);
            Require(Centre->FinalizeWorkspacePreferences(), "Could not enable dockable browser");
        }
        Workspace = std::make_unique<EditorWorkspace>(*Centre);
    }
    ~Session()
    {
        Workspace.reset();
        Ownership.reset();
        ImGui::DestroyContext();
    }
    void Frame(int Count = 1)
    {
        for (int Index = 0; Index < Count; ++Index)
        {
            Centre->PrepareInterface();
            (void)Workspace->ConfigurePlatform();
            ImGui::NewFrame();
            (void)Centre->Integrate(1.0 / 60);
            Workspace->ConstructInterface();
            Centre->ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
            Workspace->IntegrateLayoutPersistence(1.0 / 60);
        }
    }
    void Capture(const std::filesystem::path &Path)
    {
        std::vector<unsigned char> Pixels(1100 * 1000 * 3, 24);
        const auto                *Commands = ImGui::GetDrawData();
        for (const auto *List : Commands->CmdLists)
            FrontierExhibit::RasterizeCommands(List, Pixels.data(), 1100, 1000, Commands->DisplayPos,
                                               Commands->FramebufferScale);
        std::ofstream Output(Path, std::ios::binary);
        Output << "P6\n1100 1000\n255\n";
        Output.write(reinterpret_cast<const char *>(Pixels.data()), Pixels.size());
        Require(Output.good(), "Evidence write failed");
    }
};
ProjectSpaceDocument Decode(const std::filesystem::path &Path)
{
    ProjectSpaceDocument Document;
    std::string          Diagnostic;
    Require(ProjectSpaceCodec::Decode(Path, Document, Diagnostic), "Saved projectspace did not decode");
    return Document;
}
void Encode(const std::filesystem::path &Path, const ProjectSpaceDocument &Document)
{
    std::string Diagnostic;
    Require(ProjectSpaceCodec::Encode(Path, Document, Diagnostic), "Fixture projectspace write failed");
}
} // namespace

int main(int Count, char **Arguments)
{
    try
    {
        Require(Count == 3, "Expected content and disposable evidence directory");
        const auto Content  = std::filesystem::absolute(Arguments[1]);
        const auto Evidence = std::filesystem::absolute(Arguments[2]);
        std::filesystem::remove_all(Evidence);
        std::filesystem::create_directories(Evidence);
        const auto Personal   = Evidence / "Personal.projectspace";
        const bool IniExisted = std::filesystem::exists("imgui.ini");
        {
            Session Current(Content, Personal, true);
            Require(!Current.Workspace->QueryLayoutRestored(), "Missing layout did not start with defaults");
            auto &Panels = Current.Workspace->QueryPanels();
            Require(Panels.ConfigureOpen("notifications", true), "Stream open failed");
            Current.Frame(10);
            auto *Browser = ImGui::FindWindowByName(ContentName);
            auto *Stream  = ImGui::FindWindowByName(StreamName);
            auto *Bar     = Browser->DockNode->TabBar;
            ImGui::TabBarQueueReorder(Bar, ImGui::TabBarFindTabByID(Bar, Stream->TabId), -1);
            Current.Frame(3);
            ImGui::TabBarQueueFocus(Bar, ImGui::TabBarFindTabByID(Bar, Stream->TabId));
            Current.Frame(3);
            Require(Bar->Tabs[0].ID == Stream->TabId && Bar->SelectedTabId == Stream->TabId,
                    "Fixture tab order failed");
            Require(Current.Workspace->FinalizeLayout(), "Initial layout save failed");
            auto  Document  = Decode(Personal);
            auto &Workspace = *Document.Sections["workspace"].as_table();
            Workspace.insert("future_extension", toml::table{{"token", "retained"}});
            Encode(Personal, Document);
            auto Draft  = Current.Centre->QueryAppearance().QueryDraft();
            Draft.Theme = ThemeCategory::Light;
            Current.Centre->QueryAppearance().ConfigureDraft(Draft);
            ImGui::TabBarQueueFocus(Bar, ImGui::TabBarFindTabByID(Bar, Browser->TabId));
            Current.Frame(3);
            Require(Current.Workspace->FinalizeLayout(), "Layout save with an unrelated draft failed");
            Require(Current.Centre->QueryAppearance().QueryDirty(), "Layout save applied an unrelated draft");
            Require(Decode(Personal).Sections["workspace"]["future_extension"]["token"].value_or(std::string{}) ==
                        "retained",
                    "Layout save erased an unrelated extension");
            Require(!Decode(Personal).AppearancePresent &&
                        !Decode(Personal).Sections["engine"]["personal_overrides"].value_or(false),
                    "Layout save serialized an unrelated draft or enabled overrides");
            ImGui::TabBarQueueFocus(Bar, ImGui::TabBarFindTabByID(Bar, Stream->TabId));
            Current.Frame(3);
        }
        {
            Session Current(Content, Personal);
            Require(Current.Workspace->QueryLayoutRestored(), "Personal layout was not loaded");
            Current.Frame(10);
            auto *Browser = ImGui::FindWindowByName(ContentName);
            auto *Stream  = ImGui::FindWindowByName(StreamName);
            Require(Browser->DockId == Stream->DockId && Browser->DockId != 0, "Native tab group did not restore");
            const auto *Bar = Browser->DockNode->TabBar;
            Require(Bar->Tabs[0].ID == Stream->TabId && Bar->SelectedTabId == Stream->TabId,
                    "Tab order/selection did not survive restart");
            Current.Capture(Evidence / "RestoredTabs.ppm");
            ImGuiID Left = 0, Right = 0;
            ImGui::DockBuilderSplitNode(Current.Workspace->QueryPanels().QueryDockspace(), ImGuiDir_Left, .55f, &Left,
                                        &Right);
            ImGui::DockBuilderDockWindow(ContentName, Left);
            ImGui::DockBuilderDockWindow(StreamName, Right);
            ImGui::DockBuilderFinish(Current.Workspace->QueryPanels().QueryDockspace());
            Current.Frame(10);
            Require(Current.Workspace->FinalizeLayout(), "Split save failed");
        }
        {
            Session Current(Content, Personal);
            Current.Frame(10);
            auto *Browser = ImGui::FindWindowByName(ContentName);
            auto *Stream  = ImGui::FindWindowByName(StreamName);
            Require(Browser->DockId && Stream->DockId && Browser->DockId != Stream->DockId,
                    "Split did not survive restart");
            Require(std::abs(Browser->Size.x / 1100 - .55f) < .02f, "Split ratio did not restore");
            Current.Capture(Evidence / "RestoredSplit.ppm");
            ImGui::DockBuilderDockWindow(ContentName, 0);
            ImGui::SetWindowPos(ContentName, ImVec2(80, 90));
            ImGui::SetWindowSize(ContentName, ImVec2(420, 320));
            (void)Current.Workspace->QueryPanels().ConfigureOpen("notifications", false);
            Current.Frame(10);
            Require(Current.Workspace->FinalizeLayout(), "Floating layout save failed");
        }
        {
            Session Current(Content, Personal);
            Current.Frame(10);
            auto *Browser = ImGui::FindWindowByName(ContentName);
            Require(!Browser->DockId && std::abs(Browser->Pos.x - 80) <= 1 && std::abs(Browser->Pos.y - 90) <= 1 &&
                        std::abs(Browser->Size.x - 420) <= 1 &&
                        !Current.Workspace->QueryPanels().QueryOpen("notifications"),
                    "Floating geometry or closed panels did not restore");
            (void)Current.Workspace->QueryPanels().ConfigureOpen("content", false);
            Current.Frame(10);
        }
        {
            Session Current(Content, Personal);
            Current.Frame(10);
            auto &Panels = Current.Workspace->QueryPanels();
            Require(!Panels.QueryOpen("content") && !Panels.QueryOpen("notifications"),
                    "All-closed layout did not restore");
            (void)Panels.ConfigureOpen("content", true);
            (void)Panels.ConfigureOpen("notifications", true);
            Current.Frame(8);
            auto Floating = ImGui::DockBuilderAddNode(0);
            ImGui::DockBuilderSetNodePos(Floating, ImVec2(90, 110));
            ImGui::DockBuilderSetNodeSize(Floating, ImVec2(750, 600));
            ImGuiID Upper = 0, Lower = 0;
            ImGui::DockBuilderSplitNode(Floating, ImGuiDir_Up, .6f, &Upper, &Lower);
            ImGui::DockBuilderDockWindow(ContentName, Upper);
            ImGui::DockBuilderDockWindow(StreamName, Lower);
            ImGui::DockBuilderFinish(Floating);
            Current.Frame(10);
        }
        {
            Session Current(Content, Personal);
            Current.Frame(10);
            auto *Browser = ImGui::FindWindowByName(ContentName);
            auto *Stream  = ImGui::FindWindowByName(StreamName);
            Require(Browser->DockNode && Stream->DockNode && Browser->DockId != Stream->DockId &&
                        ImGui::DockNodeGetRootNode(Browser->DockNode)->ID !=
                            Current.Workspace->QueryPanels().QueryDockspace() &&
                        ImGui::DockNodeGetRootNode(Browser->DockNode) == ImGui::DockNodeGetRootNode(Stream->DockNode),
                    "Floating native split group did not survive restart");
            const auto Before = Bytes(Personal);
            Current.Frame(150);
            Require(Current.Workspace->QueryLayoutDiagnostic().find("saved") != std::string::npos,
                    "Debounced autosave did not run");
            const auto Stable     = Bytes(Personal);
            const auto StableTime = std::filesystem::last_write_time(Personal);
            Current.Frame(150);
            Require(Bytes(Personal) == Stable && std::filesystem::last_write_time(Personal) == StableTime,
                    "Unchanged frames rewrote the saved layout");
            (void)Before;
        }
        auto        Document = Decode(Personal);
        const auto  Valid    = *Document.Sections["workspace"]["layout"].as_table();
        std::string Diagnostic;
        Require(PanelDockspace::ValidateLayout(Valid, Diagnostic), "Encoded layout failed its validator");
        for (int Variant = 0; Variant < 9; ++Variant)
        {
            auto Invalid = Valid;
            if (Variant == 0)
                Invalid.insert_or_assign("format", 99);
            if (Variant == 1)
                Invalid["regions"].as_array()->clear();
            if (Variant == 2)
                Invalid["panels"].as_array()->get_as<toml::table>(0)->insert_or_assign("width", -1);
            if (Variant == 3)
                Invalid["roots"].as_array()->get_as<toml::table>(0)->insert_or_assign("region", 900);
            if (Variant == 4)
                Invalid["panels"].as_array()->get_as<toml::table>(0)->insert_or_assign("open", "yes");
            if (Variant == 5)
                Invalid["roots"].as_array()->get_as<toml::table>(0)->insert_or_assign(
                    "width", std::numeric_limits<double>::infinity());
            if (Variant == 6)
            {
                auto *Region = Invalid["regions"].as_array()->get_as<toml::table>(0);
                Region->insert_or_assign("axis", "x");
                Region->insert_or_assign("ratio", .5);
                Region->insert_or_assign("first", 0);
                Region->insert_or_assign("second", 0);
            }
            if (Variant == 7)
                Invalid["panels"].as_array()->get_as<toml::table>(0)->insert_or_assign("monitor", "invalid");
            if (Variant == 8)
                Invalid["panels"].as_array()->get_as<toml::table>(0)->insert_or_assign("monitor",
                                                                                       toml::table{{"x", 0},
                                                                                                   {"y", 0},
                                                                                                   {"width", 1920},
                                                                                                   {"height", 1080},
                                                                                                   {"dpi", 0},
                                                                                                   {"window_x", 100},
                                                                                                   {"window_y", 100}});
            Require(!PanelDockspace::ValidateLayout(Invalid, Diagnostic), "Malformed layout was accepted");
        }
        Document.Sections["workspace"]["layout"].as_table()->insert_or_assign("format", 99);
        Encode(Personal, Document);
        const auto Unsupported = Bytes(Personal);
        {
            Session Current(Content, Personal);
            Require(!Current.Workspace->QueryLayoutRestored() && !Current.Workspace->QueryLayoutDiagnostic().empty(),
                    "Unsupported layout did not report fallback");
            Current.Frame(160);
            Require(!Current.Workspace->FinalizeLayout() && Bytes(Personal) == Unsupported,
                    "Unsupported layout was silently overwritten");
            Current.Centre->ActivateSettingsPage(ControlCentrePageCategory::Workspace);
            Current.Frame(80);
            const auto Reset = Current.Centre->QueryLayoutResetExtent();
            auto      &IO    = ImGui::GetIO();
            IO.AddMousePosEvent((Reset.MinimumX + Reset.MaximumX) / 2, (Reset.MinimumY + Reset.MaximumY) / 2);
            Current.Frame();
            IO.AddMouseButtonEvent(0, true);
            Current.Frame();
            IO.AddMouseButtonEvent(0, false);
            Current.Frame();
            Require(Current.Centre->QueryLayoutResetRevision() == 1,
                    "Native reset button did not request a layout reset");
            Current.Frame(10);
            Require(Current.Workspace->FinalizeLayout(), "Explicit reset could not replace refused layout");
            Require(Decode(Personal).Sections["workspace"]["layout"]["format"].value<int64_t>() == 1,
                    "Reset did not save a current-format layout");
            const auto BeforeFailure = Bytes(Personal);
            const auto Temporary     = std::filesystem::path(Personal.string() + ".tmp");
            std::filesystem::create_directories(Temporary);
            std::ofstream(Temporary / "occupied") << "preserve";
            (void)Current.Workspace->QueryPanels().ConfigureOpen("notifications", true);
            Current.Frame(10);
            Require(!Current.Workspace->FinalizeLayout() && Bytes(Personal) == BeforeFailure,
                    "Failed atomic replacement changed the previous projectspace");
            std::filesystem::remove_all(Temporary);
            Require(Current.Workspace->FinalizeLayout(), "Failed write could not be retried");
            std::ofstream(Personal, std::ios::trunc) << "broken existing container";
            (void)Current.Workspace->QueryPanels().ConfigureOpen("notifications", false);
            Current.Frame(10);
            Require(!Current.Workspace->FinalizeLayout() && Bytes(Personal) == "broken existing container",
                    "Failed layout replacement damaged previous bytes");
            std::ofstream(Personal, std::ios::trunc) << BeforeFailure;
        }
        Document          = Decode(Personal);
        auto NativeLayout = Valid;
        for (auto &Panel : *NativeLayout["panels"].as_array())
            Panel.as_table()->insert_or_assign("native", true);
        for (auto &Root : *NativeLayout["roots"].as_array())
            if (!(*Root.as_table())["main"].value_or(false))
                Root.as_table()->insert_or_assign("native", true);
        Document.Sections["workspace"].as_table()->insert_or_assign("layout", NativeLayout);
        Encode(Personal, Document);
        {
            Session Current(Content, Personal);
            Current.Frame(10);
            auto *Browser = ImGui::FindWindowByName(ContentName);
            Require(!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) && Browser->DockNode &&
                        ImGui::DockNodeGetRootNode(Browser->DockNode)->ID ==
                            Current.Workspace->QueryPanels().QueryDockspace(),
                    "Restored native layout bypassed default-off policy or stranded the browser");
            Current.Centre->ConfigureDockableBrowserDraft(false);
            Require(Current.Centre->FinalizeWorkspacePreferences(), "Draggable setting save failed");
            Current.Frame(10);
            Current.Centre->QueryContentBrowser().ConfigureOpen(true);
            Current.Frame(10);
        }
        {
            Session Current(Content, Personal);
            Current.Frame(10);
            Require(!Current.Centre->QueryDockableContentBrowser() &&
                        Current.Centre->QueryContentBrowser().QueryExpansionTab().QueryOpen() &&
                        !Current.Workspace->QueryPanels().QueryOpen("content"),
                    "Draggable browser visibility was not restored independently of docked tabs");
        }
        const auto FreshPersonal = Evidence / "LayoutOnly.projectspace";
        {
            Session Current(Content, FreshPersonal);
            auto    Draft = Current.Centre->QueryAppearance().QueryDraft();
            Draft.Theme   = ThemeCategory::Light;
            Current.Centre->QueryAppearance().ConfigureDraft(Draft);
            Current.Frame(160);
            const auto Saved = Decode(FreshPersonal);
            Require(!Saved.AppearancePresent && !Saved.InputPresent && !Saved.NotificationsPresent &&
                        Saved.Sections["workspace"]["layout"].is_table() &&
                        !Saved.Sections["workspace"]["hosting"]["dockable_content_browser"].value_or(true) &&
                        !Saved.Sections["workspace"]["hosting"]["allow_detached_windows"].value_or(true),
                    "Autosave did not create a layout-only personal container with safe defaults");
        }
        {
            Session Current(Content, Evidence / "Legacy.toml");
            Current.Frame(160);
            Require(Current.Workspace->QueryLayoutDiagnostic().find("session-only") != std::string::npos &&
                        !std::filesystem::exists(Evidence / "Legacy.toml"),
                    "Legacy host wrote an unauthorized sidecar");
        }
        Require(IniExisted == std::filesystem::exists("imgui.ini"), "Standalone imgui.ini was created");
        std::cout << "PASS: fresh-context native tabs/order/selection/splits/floating groups/geometry/open state; "
                     "personal-only merge; drafts/extensions retained; debounce; malformed/cyclic/versioned fallback; "
                     "explicit reset; failed-write preservation; no INI sidecar. OS windows/GPU not executed.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
