/*==========================================================================================================================================
                                                      CONTENTBROWSERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise the production browser through actual ImGui pointer/key input and rasterize its real draw commands.

#include "Engine/Interface/IconPresentation.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition)
        throw std::runtime_error(Description);
}

void Capture(const std::filesystem::path &Path)
{
    const auto                &IO     = ImGui::GetIO();
    const int                  Width  = static_cast<int>(IO.DisplaySize.x);
    const int                  Height = static_cast<int>(IO.DisplaySize.y);
    std::vector<unsigned char> Pixels(static_cast<size_t>(Width * Height * 3), 24);
    const auto                *Commands = ImGui::GetDrawData();
    for (const auto *List : Commands->CmdLists)
        FrontierExhibit::RasterizeCommands(
            List, Pixels.data(), Width, Height, Commands->DisplayPos, Commands->FramebufferScale);
    std::ofstream Output(Path, std::ios::binary);
    Output << "P6\n" << Width << ' ' << Height << "\n255\n";
    Output.write(reinterpret_cast<const char *>(Pixels.data()), static_cast<std::streamsize>(Pixels.size()));
    Require(Output.good(), "Failed to capture native browser draw commands");
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected fonts and evidence directory");
        const std::filesystem::path Fonts    = Arguments[1];
        const std::filesystem::path Evidence = Arguments[2];
        std::filesystem::create_directories(Evidence);
        ImGui::CreateContext();
        auto &IO       = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1100, 1000);
        IO.DeltaTime   = 1.0f / 60.0f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(EncodeContentPath(Fonts.parent_path() / "Icons"));
        ActivationOwnership Ownership(new ControlCentrePanel(Fonts));
        auto               *Panel = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Host initialization failed");
        auto      &Browser   = Panel->QueryContentBrowser();
        auto      &Directory = Browser.QueryDirectory();
        const auto Frame     = [&]()
        {
            (void)Panel->Integrate(1.0 / 60.0);
            Panel->PrepareInterface();
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        };
        const auto Settle = [&]()
        {
            for (int Ordinal = 0; Ordinal < 100; ++Ordinal)
                Frame();
        };
        const auto Click = [&](PlaneExtent Extent)
        {
            Require(Extent.Width() > 0 && Extent.Height() > 0, "Missing native hit extent");
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) * 0.5f, (Extent.MinimumY + Extent.MaximumY) * 0.5f);
            IO.AddMouseButtonEvent(0, true);
            Frame();
            IO.AddMouseButtonEvent(0, false);
            Frame();
        };
        const auto Key = [&](ImGuiKey Code)
        {
            IO.AddKeyEvent(Code, true);
            Frame();
            IO.AddKeyEvent(Code, false);
            Frame();
        };
        const auto Chord = [&](ImGuiKey Modifier, ImGuiKey Code)
        {
            IO.AddKeyEvent(Modifier, true);
            IO.AddKeyEvent(Code, true);
            Frame();
            IO.AddKeyEvent(Code, false);
            IO.AddKeyEvent(Modifier, false);
            Frame();
        };
        const auto Find = [&](const std::string &Name)
        {
            const auto &Visible = Directory.QueryVisible();
            for (size_t Ordinal = 0; Ordinal < Visible.size(); ++Ordinal)
                if (Directory.QueryEntries()[Visible[Ordinal]].Name == Name)
                    return Ordinal;
            throw std::runtime_error("Expected real file absent: " + Name);
        };
        Frame();
        Require(!Browser.QueryExpansionTab().QueryOpen(), "Browser did not start collapsed");
        Click(Browser.QueryExpansionTab().QueryTabExtent());
        Settle();
        Require(Browser.QueryExpansionTab().QueryOpen() && Browser.QueryBodyExtent().Height() > 100,
                "Bottom expansion tab failed");
        Capture(Evidence / "BrowserRoot.ppm");
        const float DefaultHeight = Browser.QueryExpansionTab().QueryHeight();
        Click(Browser.QuerySizeExtent());
        Settle();
        Require(Browser.QueryExpansionTab().QueryHeight() > DefaultHeight, "Maximize did not enlarge browser");
        Click(Browser.QuerySizeExtent());
        Settle();
        Require(Browser.QueryExpansionTab().QueryHeight() == DefaultHeight, "Restore did not restore browser height");
        const auto FontsOrdinal = Find("FontArchives");
        Click(Browser.QueryEntryExtent(FontsOrdinal));
        Require(Directory.QuerySelection() && Directory.QuerySelection()->Name == "FontArchives",
                "Single click did not select real folder");
        Click(Browser.QueryEntryExtent(FontsOrdinal));
        Frame();
        Require(Directory.QueryDirectory().filename() == "FontArchives", "Double click did not navigate to folder");
        Key(ImGuiKey_Backspace);
        Require(Directory.QueryDirectory() == Directory.QueryRoot(), "Backspace did not travel up");
        // A no-op root-crumb click breaks the rapid click-count chain so the next double-click reads as one.
        Click(Browser.QueryNavigationExtent(0));
        Click(Browser.QueryEntryExtent(Find("FontArchives")));
        Click(Browser.QueryEntryExtent(Find("FontArchives")));
        Frame();
        Chord(ImGuiKey_ReservedForModAlt, ImGuiKey_LeftArrow);
        Require(Directory.QueryDirectory() == Directory.QueryRoot(), "Alt+Left did not travel back");
        Chord(ImGuiKey_ReservedForModAlt, ImGuiKey_RightArrow);
        Require(Directory.QueryDirectory().filename() == "FontArchives", "Alt+Right did not travel forward");
        Click(Browser.QueryNavigationExtent(1));
        Require(Directory.QueryDirectory() == Directory.QueryRoot(), "Parent crumb failed");
        Click(Browser.QueryEntryExtent(Find("Icons")));
        Click(Browser.QueryEntryExtent(Find("Icons")));
        Frame();
        Require(Directory.QueryDirectory().filename() == "Icons", "Icons folder navigation failed");
        Key(ImGuiKey_DownArrow);
        Require(Directory.QuerySelection(), "Arrow-key browsing did not select a real entry");
        IO.AddMousePosEvent(Browser.QueryBodyExtent().MinimumX + 20, Browser.QueryBodyExtent().MinimumY + 20);
        Frame();
        IO.AddMouseWheelEvent(0, -10);
        Frame();
        Frame();
        Require(Browser.QueryEntryExtent(0).Width() == 0,
                "Wheel scrolling did not move the first grid row out of view");
        Key(ImGuiKey_F5);
        Frame();
        Require(Browser.QueryEntryExtent(0).Width() > 0, "Reload did not reset scroll");
        Click(Browser.QueryNavigationExtent(0));
        Require(Directory.QueryDirectory() == Directory.QueryRoot(), "Root crumb failed");
        Click(Browser.QueryEntryExtent(Find("Icons")));
        Click(Browser.QueryEntryExtent(Find("Icons")));
        Frame();
        Require(Directory.QueryDirectory().filename() == "Icons", "Icons folder navigation failed");
        Directory.ResetSelection();
        const auto First  = Browser.QueryEntryExtent(0);
        const auto Second = Browser.QueryEntryExtent(1);
        IO.AddMousePosEvent(First.MinimumX + 20, First.MinimumY + 20);
        IO.AddMouseButtonEvent(0, true);
        Frame();
        IO.AddMousePosEvent(Second.MinimumX + 20, Second.MinimumY + 20);
        IO.AddMouseButtonEvent(0, false);
        Frame();
        Require(!Directory.QuerySelection(), "A cross-card drag release selected an unrelated file");
        // Type an actual filename fragment, then return to the complete list with the shared search widget.
        Click(Browser.QuerySearchExtent());
        IO.AddInputCharactersUTF8("asset-file");
        Frame();
        Require(Browser.QuerySearchActive() && !Directory.QueryVisible().empty(), "Live filename search failed");
        Capture(Evidence / "BrowserSearch.ppm");
        Key(ImGuiKey_Enter);
        Require(!Browser.QuerySearchActive(), "Enter did not finish search editing");
        Chord(ImGuiKey_ReservedForModCtrl, ImGuiKey_F);
        Require(Browser.QuerySearchActive(), "Ctrl+F did not focus search");
        Key(ImGuiKey_Escape);
        Require(!Browser.QuerySearchActive() && !Directory.QueryVisible().empty(),
                "Escape did not leave committed search");
        Click(Browser.QueryViewExtent());
        Require(!Browser.QueryGrid(), "Grid/list switch failed");
        Click(Browser.QueryCategoryExtent(static_cast<size_t>(ContentCategory::VectorImage)));
        Require(!Directory.QueryVisible().empty(), "SVG category hid real icon files");
        Click(Browser.QueryEntryExtent(0));
        Frame();
        Require(Directory.QuerySelection() && Directory.QuerySelection()->Category == ContentCategory::VectorImage,
                "Native SVG selection failed");
        Capture(Evidence / "BrowserSelection.ppm");
        Click(Browser.QuerySearchExtent());
        IO.AddInputCharactersUTF8("no-such-content-file");
        Frame();
        Require(Directory.QueryVisible().empty() && !Directory.QuerySelection(),
                "No-results filter retained stale selection");
        Capture(Evidence / "BrowserEmpty.ppm");
        Key(ImGuiKey_Escape);
        Require(!Browser.QuerySearchActive() && !Directory.QueryVisible().empty() &&
                    Browser.QueryExpansionTab().QueryOpen(),
                "Escape failed to restore search without closing browser");
        // Top drawer wins input ownership while open; it must not leave hidden search capture behind.
        Click(Browser.QuerySearchExtent());
        Panel->ActivatePage(AppearancePageCategory::Theme);
        Settle();
        Require(!Browser.QuerySearchActive(), "Top drawer did not release hidden browser search");
        Key(ImGuiKey_Escape);
        Settle();
        Require(!Panel->QueryQuickOpen() && Browser.QueryExpansionTab().QueryOpen(),
                "Control Centre dismissal affected browser");
        auto Settings  = Panel->QueryAppearance().QueryDraft();
        Settings.Theme = ThemeCategory::Light;
        Panel->QueryAppearance().Seed(Settings);
        Panel->PrepareInterface();
        Frame();
        Capture(Evidence / "BrowserLight.ppm");
        Click(Browser.QueryImportExtent());
        Frame();
        Require(Browser.QueryImportOpen(), "Import button did not open import page");
        Capture(Evidence / "BrowserImport.ppm");
        Click(Browser.QueryImportBackExtent());
        Frame();
        Require(!Browser.QueryImportOpen(), "Import back tab did not return to library");
        Click(Browser.QueryImportExtent());
        Frame();
        const auto &ImportVisible = Browser.QueryImportDirectory().QueryVisible();
        size_t      Imported      = ImportVisible.size();
        for (size_t Ordinal = 0; Ordinal < ImportVisible.size(); ++Ordinal)
            if (Browser.QueryImportDirectory().QueryEntries()[ImportVisible[Ordinal]].Category !=
                ContentCategory::Directory)
            {
                Imported = Ordinal;
                break;
            }
        Require(Imported < ImportVisible.size(), "Import source list held no file");
        const std::string ImportedName =
            Browser.QueryImportDirectory().QueryEntries()[ImportVisible[Imported]].Name;
        Click(Browser.QueryImportEntryExtent(Imported));
        Frame();
        Require(!Browser.QueryImportOpen() && Directory.QuerySelection() &&
                    Directory.QuerySelection()->Name == ImportedName && !Browser.QuerySearchActive(),
                "Import did not take the file into the library");
        Click(Browser.QueryEntryExtent(Find("Icons")));
        Click(Browser.QueryEntryExtent(Find("Icons")));
        Frame();
        Require(Directory.QueryDirectory().filename() == "Icons", "Icons folder navigation failed");
        // The import round-trip cleared the committed search; restore it for the narrow popup below.
        Click(Browser.QuerySearchExtent());
        IO.AddInputCharactersUTF8("asset-file");
        Frame();
        Key(ImGuiKey_Enter);
        Require(!Browser.QuerySearchActive() && !Directory.QueryVisible().empty(), "Search restore failed");
        // Loss of focus cancels drag and releases text input, rather than leaving pointer ownership stuck.
        Click(Browser.QuerySearchExtent());
        IO.AddFocusEvent(false);
        Frame();
        Require(!Browser.QuerySearchActive() && !Browser.QueryExpansionTab().QueryDragging(),
                "Focus loss left browser input captured");
        IO.AddFocusEvent(true);
        Frame();
        Settings.InterfaceScale = 125;
        Panel->QueryAppearance().Seed(Settings);
        Panel->PrepareInterface();
        IO.DisplaySize = ImVec2(375, 700);
        Settle();
        Require(Browser.QueryBodyExtent().MinimumX >= 0 && Browser.QueryBodyExtent().MaximumX <= 375 &&
                    Browser.QueryBodyExtent().MaximumY <= 700 && Browser.QueryBodyExtent().Height() > 50,
                "Narrow scaled browser bounds overflowed");
        const auto NarrowDirectory = Directory.QueryDirectory();
        Click(Browser.QueryFilterExtent());
        Frame();
        const auto OtherChoice = Browser.QueryCategoryExtent(static_cast<size_t>(ContentCategory::Other));
        Require(OtherChoice.MinimumY >= 0 && OtherChoice.MaximumY <= 700, "Narrow popup escaped viewport");
        Click(OtherChoice);
        Frame();
        Require(Directory.QueryDirectory() == NarrowDirectory && Directory.QueryVisible().empty(),
                "Narrow category popup activated an obscured navigation control");
        Click(Browser.QueryFilterExtent());
        Frame();
        Click(Browser.QueryCategoryExtent(static_cast<size_t>(ContentCategory::VectorImage)));
        Frame();
        Require(!Directory.QueryVisible().empty() && !Panel->QueryQuickOpen(),
                "Narrow popup failed to restore category or opened top drawer");
        Capture(Evidence / "BrowserNarrow.ppm");
        const auto  Grip  = Browser.QueryExpansionTab().QueryGripExtent();
        const float GripX = (Grip.MinimumX + Grip.MaximumX) * 0.5f;
        const float GripY = (Grip.MinimumY + Grip.MaximumY) * 0.5f;
        IO.AddMousePosEvent(GripX, GripY);
        IO.AddMouseButtonEvent(0, true);
        Frame();
        IO.AddMousePosEvent(GripX, GripY + 100);
        Frame();
        Capture(Evidence / "BrowserDrag.ppm");
        IO.AddMouseButtonEvent(0, false);
        Frame();
        Settle();
        Require(!Browser.QueryExpansionTab().QueryOpen(), "Downward drawer swipe failed to dismiss");
        Ownership.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: native bottom tab; single/double click; root/parent crumbs; Alt+Left/Right history; "
                           "Backspace/F5 chords; Ctrl+F; search/restore; category; grid/list; real SVG selection; "
                           "empty results; import round-trip; shared light theme; pointer/focus ownership; 375x700 "
                           "at 125%; downward dismissal; eight native captures\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
