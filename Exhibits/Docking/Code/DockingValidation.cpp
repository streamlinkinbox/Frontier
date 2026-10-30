/*==========================================================================================================================================
                                                        DOCKINGVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise patched native docking with original panel instances, personal default-off settings, and native CPU evidence.

#include "Engine/Interface/IconPresentation.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/EditorWorkspace.h"
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Message)
{
    if (!Condition)
        throw std::runtime_error(Message);
}
std::vector<unsigned char> Rasterize()
{
    const auto                *Commands = ImGui::GetDrawData();
    std::vector<unsigned char> Pixels(1100 * 1000 * 3, 24);
    for (const auto *List : Commands->CmdLists)
        FrontierExhibit::RasterizeCommands(List, Pixels.data(), 1100, 1000, Commands->DisplayPos,
                                           Commands->FramebufferScale);
    return Pixels;
}
void Capture(const std::filesystem::path &Path)
{
    const auto    Pixels = Rasterize();
    std::ofstream Stream(Path, std::ios::binary);
    Stream << "P6\n1100 1000\n255\n";
    Stream.write(reinterpret_cast<const char *>(Pixels.data()), Pixels.size());
    Require(Stream.good(), "Capture write failed");
}
void VerifyAddButton(ImGuiDockNode *Region)
{
    const auto &Bar      = *Region->TabBar;
    const auto  Add      = Region->FrontierAddRect;
    float       LastEdge = Bar.BarRect.Min.x;
    for (const auto &Tab : Bar.Tabs)
        if (!(Tab.Flags & ImGuiTabItemFlags_Button))
            LastEdge = std::max(LastEdge, Bar.BarRect.Min.x + Tab.Offset + Tab.Width - Bar.ScrollingAnim);
    Require(Add.Min.x >= LastEdge - 1 && Add.Min.x - LastEdge <= 6 &&
                std::abs(Add.GetCenter().y - Bar.FrontierStripRect.GetCenter().y) < .1f &&
                Add.GetWidth() <= Bar.FrontierStripRect.GetHeight(),
            "Add disc/hit rectangle are not adjacent and vertically centred");
    const auto Pixels   = Rasterize();
    int        MinimumX = 1100, MinimumY = 1000, MaximumX = -1, MaximumY = -1;
    for (int Y = static_cast<int>(Add.Min.y); Y < Add.Max.y; ++Y)
        for (int X = static_cast<int>(Add.Min.x); X < Add.Max.x; ++X)
        {
            Require(X >= 0 && X < 1100 && Y >= 0 && Y < 1000, "Add rect escaped the capture");
            const auto Offset = (Y * 1100 + X) * 3;
            if (Pixels[Offset] > 180 && Pixels[Offset + 1] > 180 && Pixels[Offset + 2] > 180)
            {
                MinimumX = std::min(MinimumX, X);
                MinimumY = std::min(MinimumY, Y);
                MaximumX = std::max(MaximumX, X);
                MaximumY = std::max(MaximumY, Y);
            }
        }
    Require(MaximumX >= MinimumX && MaximumY >= MinimumY &&
                std::abs((MinimumX + MaximumX + 1) / 2.0f - Add.GetCenter().x) <= .8f &&
                std::abs((MinimumY + MaximumY + 1) / 2.0f - Add.GetCenter().y) <= .8f,
            "Rendered plus strokes are not centred on the disc/hit rectangle");
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected EngineContent and disposable evidence directory");
        const auto Content  = std::filesystem::absolute(Arguments[1]);
        const auto Evidence = std::filesystem::absolute(Arguments[2]);
        std::filesystem::remove_all(Evidence);
        std::filesystem::create_directories(Evidence);
        ImGui::CreateContext();
        auto &IO       = ImGui::GetIO();
        IO.DisplaySize = ImVec2(1100, 1000);
        IO.DeltaTime   = 1.0f / 60;
        IO.IniFilename = nullptr;
        IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        ActivationOwnership Ownership(
            new ControlCentrePanel(Content / "FontArchives", Evidence / "Personal.projectspace"));
        auto *Centre = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Centre->Initialize() && Centre->Activate() && !Centre->QueryDetachedWindows(),
                "Default detachment was enabled");
        {
            EditorWorkspace Workspace(*Centre);
            auto           &Panels            = Workspace.QueryPanels();
            auto           &Browser           = Centre->QueryContentBrowser();
            auto           *OriginalDirectory = &Browser.QueryDirectory();
            Require(!Panels.ConfigureDetached("content", true), "Default-off detachment accepted");
            const auto Frame = [&]()
            {
                Centre->PrepareInterface();
                (void)Workspace.ConfigurePlatform();
                ImGui::NewFrame();
                (void)Centre->Integrate(1.0 / 60);
                Workspace.ConstructInterface();
                Centre->ConstructInterface();
                ImGui::Render();
                FrontierExhibit::AcknowledgeTextures();
            };
            const auto Settle = [&]()
            {
                for (int Index = 0; Index < 80; ++Index)
                    Frame();
            };
            const auto Click = [&](PlaneExtent Extent)
            {
                IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) / 2, (Extent.MinimumY + Extent.MaximumY) / 2);
                Frame();
                IO.AddMouseButtonEvent(0, true);
                Frame();
                IO.AddMouseButtonEvent(0, false);
                Frame();
            };
            Frame();
            Frame();
            Frame();
            Require(!Centre->QueryDockableContentBrowser() && !Panels.QueryOpen("content") &&
                        !Panels.ConfigureOpen("content", true),
                    "Content Browser did not default to the draggable drawer");
            Capture(Evidence / "DraggableDefault.ppm");
            const auto OriginalAppearance = Centre->QueryAppearance().QueryApplied();
            for (uint32_t Index = 0; Index < static_cast<uint32_t>(ThemeCategory::Count); ++Index)
                for (const float Scale : {100.0f, 125.0f})
                {
                    auto Preferences           = OriginalAppearance;
                    Preferences.Theme          = static_cast<ThemeCategory>(Index);
                    Preferences.InterfaceScale = Scale;
                    Centre->QueryAppearance().ConfigureDraft(Preferences);
                    Centre->QueryAppearance().Apply();
                    Frame();
                    Frame();
                    ThemeStructure Theme;
                    Theme.ConfigureTheme(Preferences.Theme);
                    const auto Tone = Theme.QueryDarkestSurface();
                    Require(Tone.Alpha == 1, "Theme's darkest surface is not opaque");
                    const auto &Palette   = Theme.QueryPalette();
                    const auto  Luminance = [](ColorQuad Colour)
                    { return .2126f * Colour.Red + .7152f * Colour.Green + .0722f * Colour.Blue; };
                    for (const auto Surface :
                         {Palette.MainBackground, Palette.PanelBackground, Palette.CardBackground,
                          Palette.CardSubBackground, Palette.InputBackground, Palette.ActiveBackground})
                        Require(Luminance(Tone) <= Luminance(Surface) + .0001f,
                                "Tab did not use the darkest theme surface");
                    for (const auto Slot : {ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabDimmed,
                                            ImGuiCol_TabDimmedSelected})
                    {
                        const auto Native = ImGui::GetStyle().Colors[Slot];
                        Require(Native.w == 1 && Native.x == Tone.Red && Native.y == Tone.Green &&
                                    Native.z == Tone.Blue,
                                "Native trapezoid uses a transparent or non-darkest fill");
                    }
                    const auto Pixels = Rasterize();
                    for (const auto Tab :
                         {Centre->QueryExpansionTab().QueryTabExtent(), Browser.QueryExpansionTab().QueryTabExtent()})
                    {
                        const int  X      = static_cast<int>(Tab.MinimumX + Tab.Width() * .2f);
                        const int  Y      = static_cast<int>((Tab.MinimumY + Tab.MaximumY) / 2);
                        const auto Offset = (Y * 1100 + X) * 3;
                        Require(std::abs(Pixels[Offset] - Tone.Red * 255) <= 2 &&
                                    std::abs(Pixels[Offset + 1] - Tone.Green * 255) <= 2 &&
                                    std::abs(Pixels[Offset + 2] - Tone.Blue * 255) <= 2,
                                "Rendered drawer trapezoid is not the opaque darkest theme colour");
                    }
                }
            Centre->QueryAppearance().ConfigureDraft(OriginalAppearance);
            Centre->QueryAppearance().Apply();
            Frame();
            Frame();

            Centre->ActivateSettingsPage(ControlCentrePageCategory::Workspace);
            Settle();
            Capture(Evidence / "WorkspaceSettings.ppm");
            const auto BrowserChoice = Centre->QueryBrowserHostingExtent();
            Click(Spanning(BrowserChoice.MaximumX - 30, BrowserChoice.MinimumY + 16, 2, 2));
            Require(Centre->QueryDockableBrowserDraft() && !Centre->QueryDockableContentBrowser(),
                    "Browser choice bypassed the draft");
            Click(Centre->QueryApplyExtent());
            Frame();
            Click(Centre->QueryConfirmation().QueryCancelExtent());
            Frame();
            Require(!Centre->QueryDockableContentBrowser() && Centre->QueryDockableBrowserDraft() &&
                        !std::filesystem::exists(Evidence / "Personal.projectspace"),
                    "Browser Cancel changed applied hosting/file or discarded the draft");
            Click(Centre->QueryApplyExtent());
            Frame();
            Click(Centre->QueryConfirmation().QueryPrimaryExtent());
            Frame();
            Require(Centre->QueryDockableContentBrowser() && Panels.QueryOpen("content"),
                    "Browser Apply did not switch to the dockable presentation");
            IO.AddKeyEvent(ImGuiKey_Escape, true);
            Frame();
            IO.AddKeyEvent(ImGuiKey_Escape, false);
            Settle();
            Require(IO.ConfigFlags & ImGuiConfigFlags_DockingEnable, "Docking not enabled");
            Require(!(IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable), "Native viewports enabled by default");
            Require(ImGui::GetStyle().TabSlant == 14 && ImGui::GetStyle().TabOverlap == 24 &&
                        ImGui::GetStyle().TabHeight == 24,
                    "Inherited docking geometry not applied");
            auto *ContentWindow = ImGui::FindWindowByName("Content Browser###FrontierPanel-content");
            Require(ContentWindow && ContentWindow->DockId == Panels.QueryDockspace(), "Content was not docked");
            Capture(Evidence / "DockedContent.ppm");
            VerifyAddButton(ContentWindow->DockNode);
            auto ScaledAppearance           = Centre->QueryAppearance().QueryApplied();
            ScaledAppearance.InterfaceScale = 125;
            Centre->QueryAppearance().ConfigureDraft(ScaledAppearance);
            Centre->QueryAppearance().Apply();
            Frame();
            Frame();
            Frame();
            VerifyAddButton(ContentWindow->DockNode);
            ScaledAppearance.InterfaceScale = 100;
            Centre->QueryAppearance().ConfigureDraft(ScaledAppearance);
            Centre->QueryAppearance().Apply();
            Frame();
            Frame();
            Frame();
            Require(Browser.QueryDirectory().Navigate(Content / "Icons"), "Could not navigate actual hosted content");
            Frame();
            Click(Browser.QueryViewExtent());
            Require(!Browser.QueryGrid(), "Hosted browser did not receive real pointer input");
            IO.AddKeyEvent(ImGuiKey_DownArrow, true);
            Frame();
            IO.AddKeyEvent(ImGuiKey_DownArrow, false);
            Frame();
            Require(Browser.QueryDirectory().QuerySelection() != nullptr, "Hosted browser ignored keyboard navigation");
            auto       *InitialRegion = ImGui::DockBuilderGetNode(ContentWindow->DockId);
            const auto  Add           = InitialRegion->FrontierAddRect;
            const auto *ContentTab    = ImGui::TabBarFindTabByID(InitialRegion->TabBar, ContentWindow->TabId);
            const auto  TabEnd        = InitialRegion->TabBar->BarRect.Min.x + ContentTab->Offset + ContentTab->Width;
            Require(Add.Min.x >= TabEnd && Add.Min.x - TabEnd <= 6 &&
                        std::abs(Add.GetCenter().y - InitialRegion->TabBar->FrontierStripRect.GetCenter().y) < .1f &&
                        Add.GetWidth() <= InitialRegion->TabBar->FrontierStripRect.GetHeight(),
                    "Add button is not adjacent to the last trapezoid and centred in its strip");
            Click(Spanning(Add.Min.x, Add.Min.y, Add.GetWidth(), Add.GetHeight()));
            Frame();
            Require(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
                    "Patched plus did not open the panel catalogue");
            const auto *Catalogue = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
            Require(Catalogue != nullptr, "Panel catalogue did not render");
            const float RowHeight = ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
            Click(Spanning(Catalogue->DC.CursorStartPos.x + 20, Catalogue->DC.CursorStartPos.y + RowHeight + 6, 2, 2));
            Require(Panels.QueryOpen("notifications"), "Catalogue selection did not reopen the real stream");
            Frame();
            Frame();
            auto *StreamWindow = ImGui::FindWindowByName("Notification Stream###FrontierPanel-notifications");
            Require(StreamWindow && StreamWindow->DockId == ContentWindow->DockId,
                    "Panels did not share a native tab group");
            auto *Region = ImGui::DockBuilderGetNode(ContentWindow->DockId);
            Require(Region && Region->FrontierAddButton, "Patch D add button was not seated");
            VerifyAddButton(Region);
            const auto *BrowserTab = ImGui::TabBarFindTabByID(Region->TabBar, ContentWindow->TabId);
            Click(Spanning(Region->TabBar->BarRect.Min.x + BrowserTab->Offset + BrowserTab->Width / 2,
                           Region->TabBar->BarRect.Min.y + 10, 2, 2));
            Frame();
            Click(Browser.QuerySearchExtent());
            Require(Browser.QuerySearchActive(), "Hosted search did not activate");
            const auto *Tab = ImGui::TabBarFindTabByID(Region->TabBar, StreamWindow->TabId);
            Require(Tab != nullptr, "Stream native tab absent");
            Click(Spanning(Region->TabBar->BarRect.Min.x + Tab->Offset + Tab->Width / 2,
                           Region->TabBar->BarRect.Min.y + 10, 2, 2));
            Frame();
            Require(Region->SelectedTabId == StreamWindow->TabId, "Real native tab selection failed");
            Require(!Browser.QuerySearchActive(), "Hidden hosted browser retained keyboard ownership");
            Capture(Evidence / "DockedStream.ppm");
            const float Inset = std::max(ImGui::GetStyle().FramePadding.x,
                                         std::max(ImGui::GetStyle().TabSlant, ImGui::GetStyle().TabOverlap) + 4);
            Click(Spanning(Region->TabBar->BarRect.Min.x + Tab->Offset + Tab->Width - Inset - ImGui::GetFontSize() / 2,
                           Region->TabBar->BarRect.GetCenter().y, 2, 2));
            Frame();
            Require(!Panels.QueryOpen("notifications"), "Patched native close button did not close only its tab");
            Require(Panels.QueryOpen("content"), "Closing one tab closed the other panel");
            Require(Panels.ConfigureOpen("notifications", true), "Stream reopen failed");
            Frame();
            Frame();

            const auto *Reopened = ImGui::TabBarFindTabByID(Region->TabBar, StreamWindow->TabId);
            const float TabY     = Region->TabBar->BarRect.GetCenter().y;
            IO.AddMousePosEvent(Region->TabBar->BarRect.Min.x + Reopened->Offset + Reopened->Width / 2, TabY);
            Frame();
            IO.AddMouseButtonEvent(0, true);
            Frame();
            IO.AddMousePosEvent(Region->TabBar->BarRect.Min.x + 40, TabY);
            Frame();
            Frame();
            IO.AddMouseButtonEvent(0, false);
            Frame();
            Frame();
            Require(Region->TabBar->Tabs[0].ID == StreamWindow->TabId && StreamWindow->DockId == ContentWindow->DockId,
                    "Native pointer tab reorder failed");

            ImGuiID Left = 0, Right = 0;
            ImGui::DockBuilderSplitNode(Panels.QueryDockspace(), ImGuiDir_Left, .55f, &Left, &Right);
            ImGui::DockBuilderDockWindow(ContentWindow->Name, Left);
            ImGui::DockBuilderDockWindow(StreamWindow->Name, Right);
            ImGui::DockBuilderFinish(Panels.QueryDockspace());
            Frame();
            Frame();
            Require(ContentWindow->DockId != StreamWindow->DockId && &Browser.QueryDirectory() == OriginalDirectory &&
                        Browser.QueryDirectory().QueryDirectory() == Content / "Icons" && !Browser.QueryGrid(),
                    "Split recreated panel content or erased browsing state");
            Capture(Evidence / "SplitPanels.ppm");
            Require(Panels.ConfigureOpen("content", false), "Close request failed");
            Frame();
            Require(Panels.ConfigureOpen("content", true) && Panels.ConfigureDetached("content", false),
                    "Reopen/dock-back failed");
            Frame();
            Frame();
            Require(&Browser.QueryDirectory() == OriginalDirectory && !Browser.QueryGrid(), "Reopening reset content");
            Centre->ActivateSettingsPage(ControlCentrePageCategory::Workspace);
            Settle();
            auto NarrowAppearance           = Centre->QueryAppearance().QueryApplied();
            NarrowAppearance.InterfaceScale = 125;
            Centre->QueryAppearance().ConfigureDraft(NarrowAppearance);
            Centre->QueryAppearance().Apply();
            IO.DisplaySize = ImVec2(375, 700);
            Settle();
            const auto NarrowToggle = Centre->QueryDetachedToggleExtent();
            Require(NarrowToggle.MinimumX >= 0 && NarrowToggle.MaximumX <= 375 && NarrowToggle.MinimumY >= 0 &&
                        NarrowToggle.MaximumY <= 700,
                    "Narrow workspace setting escaped the viewport");
            const auto NarrowChoice = Centre->QueryBrowserHostingExtent();
            Require(NarrowChoice.MinimumX >= 0 && NarrowChoice.MaximumX <= 375 &&
                        Centre->QueryApplyExtent().MinimumX >= 0 && Centre->QueryApplyExtent().MaximumX <= 375,
                    "Narrow browser choice or Apply escaped viewport");
            NarrowAppearance.InterfaceScale = 100;
            Centre->QueryAppearance().ConfigureDraft(NarrowAppearance);
            Centre->QueryAppearance().Apply();
            IO.DisplaySize = ImVec2(1100, 1000);
            Settle();
            Click(Centre->QueryDetachedToggleExtent());
            Require(Centre->QueryDetachedDraft() && !Centre->QueryDetachedWindows(), "Toggle bypassed the draft");
            Click(Centre->QueryApplyExtent());
            Frame();
            Require(Centre->QueryConfirmation().QueryOpen(), "Workspace Apply bypassed confirmation");
            Click(Centre->QueryConfirmation().QueryCancelExtent());
            Require(!Centre->QueryDetachedWindows(), "Cancel enabled OS windows");
            Click(Centre->QueryApplyExtent());
            Frame();
            Click(Centre->QueryConfirmation().QueryPrimaryExtent());
            Frame();
            Require(Centre->QueryDetachedWindows(), "Confirmed hosting preference not applied");
            // Headless verification has no platform-window adapter: desired=true must not claim effective support.
            Require(!Workspace.ConfigurePlatform() && !(IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) &&
                        !Panels.ConfigureDetached("content", true),
                    "Missing platform adapters did not fail closed");
            ProjectSpaceDocument Saved;
            std::string          Diagnostic;
            Require(ProjectSpaceCodec::Decode(Evidence / "Personal.projectspace", Saved, Diagnostic) &&
                        Saved.Sections["workspace"]["hosting"]["allow_detached_windows"].value_or(false),
                    "Personal hosting preference was not persisted");
            Require(!Saved.Sections["engine"]["personal_overrides"].value_or(false),
                    "Hosting save unexpectedly enabled unrelated appearance/input overrides");
            auto Corrupt = Saved;
            Corrupt.Sections["workspace"]["hosting"].as_table()->insert_or_assign("format", 2);
            Require(!ProjectSpaceCodec::Validate(Corrupt, Diagnostic), "Unknown hosting format accepted");
            Corrupt = Saved;
            Corrupt.Sections["workspace"]["hosting"].as_table()->insert_or_assign("allow_detached_windows", "yes");
            Require(!ProjectSpaceCodec::Validate(Corrupt, Diagnostic), "Mistyped hosting policy accepted");
            Corrupt = Saved;
            Corrupt.Sections["workspace"]["hosting"].as_table()->insert_or_assign("dockable_content_browser", "yes");
            Require(!ProjectSpaceCodec::Validate(Corrupt, Diagnostic), "Mistyped browser hosting accepted");
            Centre->ConfigureDetachedDraft(false);
            Require(Centre->FinalizeWorkspacePreferences(), "Disabling hosting failed");
            Frame();
            Require(!(IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable), "Disabling kept viewports active");
            IO.AddKeyEvent(ImGuiKey_Escape, true);
            Frame();
            IO.AddKeyEvent(ImGuiKey_Escape, false);
            Settle();
            Require(Panels.ConfigureOpen("content", false) && Panels.ConfigureOpen("notifications", false) &&
                        Panels.ConfigureOpen("viewport", false),
                    "Could not close all panels");
            Frame();
            Frame();
            // 📝 FindWindowByName hashes the full window name, including the ### identity suffix the
            // dockspace host appends; the bare label never resolves. Guard before dereferencing.
            auto       *Root     = ImGui::FindWindowByName("Frontier panel dockspace###Frontier shared panels");
            Require(Root != nullptr, "Panel dockspace host window is missing");
            const auto *EmptyBar = ImGui::GetCurrentContext()->TabBars.GetByKey(Root->GetID("Closed panel catalogue"));
            Require(EmptyBar && EmptyBar->Tabs.Size == 1, "Closing the last panel lost the plus/reopen control");
            const auto &OnlyTab = EmptyBar->Tabs[0];
            Click(Spanning(EmptyBar->BarRect.Min.x + OnlyTab.Offset + OnlyTab.Width / 2,
                           EmptyBar->BarRect.GetCenter().y, 2, 2));
            Frame();
            Require(!ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Empty-dock plus did not open catalogue");
            const auto *EmptyCatalogue = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
            Click(Spanning(EmptyCatalogue->DC.CursorStartPos.x + 20, EmptyCatalogue->DC.CursorStartPos.y + 6, 2, 2));
            Frame();
            Require(Panels.QueryOpen("content") && !Panels.QueryOpen("notifications") &&
                        &Browser.QueryDirectory() == OriginalDirectory && !Browser.QueryGrid(),
                    "Last-closed panel could not be reopened with its original content");
            Centre->ConfigureDockableBrowserDraft(false);
            Require(Centre->FinalizeWorkspacePreferences(), "Could not restore draggable hosting");
            Settle();
            Require(!Panels.QueryOpen("content") && !Panels.ConfigureOpen("content", true) &&
                        &Browser.QueryDirectory() == OriginalDirectory && !Browser.QueryGrid() &&
                        Browser.QueryExpansionTab().QueryOpen(),
                    "Switching back to the drawer lost content or left a competing docked browser");
            Click(Browser.QuerySearchExtent());
            Frame();
            Frame();
            Require(Browser.QuerySearchActive(), "Workspace suspended the active draggable browser's search");
            Require(!Panels.Register({"late", "Late", []() {}, true}), "Late registration broke stable panel identity");
        }
        Ownership.reset();
        ActivationOwnership Reloaded(
            new ControlCentrePanel(Content / "FontArchives", Evidence / "Personal.projectspace"));
        auto *Fresh = static_cast<ControlCentrePanel *>(Reloaded.get());
        Require(Fresh->Initialize() && Fresh->Activate() && !Fresh->QueryDetachedWindows() &&
                    !Fresh->QueryDockableContentBrowser(),
                "Disabled policy was not reloaded");
        Reloaded.reset();
        ProjectSpaceDocument Enabled;
        std::string          Diagnostic;
        Require(ProjectSpaceCodec::Decode(Evidence / "Personal.projectspace", Enabled, Diagnostic),
                "Policy read failed");
        Enabled.Sections["workspace"]["hosting"].as_table()->insert_or_assign("allow_detached_windows", true);
        Enabled.Sections["workspace"]["hosting"].as_table()->insert_or_assign("dockable_content_browser", true);
        Require(ProjectSpaceCodec::Encode(Evidence / "Personal.projectspace", Enabled, Diagnostic),
                "Policy write failed");
        ActivationOwnership EnabledReload(
            new ControlCentrePanel(Content / "FontArchives", Evidence / "Personal.projectspace"));
        auto *EnabledCentre = static_cast<ControlCentrePanel *>(EnabledReload.get());
        Require(EnabledCentre->Initialize() && EnabledCentre->Activate() && EnabledCentre->QueryDetachedWindows() &&
                    EnabledCentre->QueryDockableContentBrowser(),
                "Personal enabled policy did not reload independently of unrelated overrides");
        EnabledReload.reset();
        ActivationOwnership ProjectOnly(new ControlCentrePanel(
            Content / "FontArchives", Evidence / "Missing.projectspace", Evidence / "Personal.projectspace"));
        auto               *ProjectCentre = static_cast<ControlCentrePanel *>(ProjectOnly.get());
        Require(ProjectCentre->Initialize() && ProjectCentre->Activate() && !ProjectCentre->QueryDetachedWindows() &&
                    !ProjectCentre->QueryDockableContentBrowser(),
                "Project document enabled a machine-local detached-window policy");
        ProjectOnly.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: patches A-G; centred adjacent plus pixels/hitbox; 8 opaque themes at 100/125%; "
                     "draggable/dockable browser; native tabs/split; shared content; default-off "
                     "personal policy, modal save/cancel, no-adapter refusal. OS windows/GPU not executed.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
