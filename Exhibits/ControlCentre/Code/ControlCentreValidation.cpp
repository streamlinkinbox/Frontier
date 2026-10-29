/*==========================================================================================================================================
                                                    CONTROLCENTREVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise actual native appearance controls, persistence, font discovery,
// and CPU image capture.

#include "CpuRasterization.h"
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Interface/FontCodec.h"
#include "Engine/Interface/IconPresentation.h"
#include "UI/Editor/ControlCentrePanel.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui_internal.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    VALIDATION SUPPORT
//------------------------------------------------------------------------------------------------------------------------

void Require(bool Condition, const char *Description)
{
    if (!Condition)
        throw std::runtime_error(Description);
}

void Capture(const std::filesystem::path &Path, PlaneExtent Footer = {}, PlaneExtent Inversion = {})
{
    constexpr int              Width  = 1100;
    constexpr int              Height = 1000;
    std::vector<unsigned char> Pixels(Width * Height * 3, 24);
    const auto                *Commands = ImGui::GetDrawData();
    for (const auto *List : Commands->CmdLists)
        FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, Commands->DisplayPos,
                                           Commands->FramebufferScale);
    if (Footer.Width() > 0)
    {
        const auto  Pixel     = [&](int X, int Y) { return Pixels.data() + (Y * Width + X) * 3; };
        const auto *TopCorner = Pixel(static_cast<int>(Footer.MinimumX) + 4, static_cast<int>(Footer.MinimumY) + 4);
        const auto *TopCentre =
            Pixel(static_cast<int>((Footer.MinimumX + Footer.MaximumX) * 0.5f), static_cast<int>(Footer.MinimumY) + 4);
        Require(std::memcmp(TopCorner, TopCentre, 3) == 0,
                "Footer has rounded upper corners instead of a straight tab-page seam");
        const auto *LowerCorner = Pixel(static_cast<int>(Footer.MinimumX) + 4, static_cast<int>(Footer.MaximumY) - 4);
        const auto *Outside     = Pixel(static_cast<int>(Footer.MinimumX) - 2, static_cast<int>(Footer.MaximumY) - 4);
        Require(std::memcmp(LowerCorner, Outside, 3) == 0, "Footer rounding leaks outside the page contour");
    }
    if (Inversion.Width() > 0)
    {
        const int X = static_cast<int>(Inversion.MinimumX - 60);
        const int Y = static_cast<int>(Inversion.MinimumY + Inversion.Height() / 2);
        Require(X >= 0 && Y >= 24 && X < Width && Y < Height, "Invert switch pixel probe escaped the viewport");
        Require(std::memcmp(Pixels.data() + (Y * Width + X) * 3, Pixels.data() + ((Y - 24) * Width + X) * 3, 3) == 0,
                "Invert Y-Axis still has a filled field surrounding the plain switch");
    }
    std::ofstream Output(Path, std::ios::binary);
    Output << "P6\n" << Width << ' ' << Height << "\n255\n";
    Output.write(reinterpret_cast<const char *>(Pixels.data()), Pixels.size());
    Require(Output.good(), "Could not save actual native draw output");
}
} // namespace Frontier

//------------------------------------------------------------------------------------------------------------------------
//                                                       VERIFICATION
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected font directory and evidence output");
        const std::filesystem::path Fonts(Arguments[1]);
        const std::filesystem::path Evidence(Arguments[2]);
        std::filesystem::create_directories(Evidence);
        const auto PreferencesPath = Evidence / "Appearance.toml";
        std::filesystem::remove(PreferencesPath);
        std::filesystem::remove(Evidence / "Input.Appearance.toml");
        const auto OriginalDirectory = std::filesystem::current_path();
        const auto LaunchDirectory   = Evidence / "UnrelatedLaunch";
        std::filesystem::create_directories(LaunchDirectory / "EngineContent/FontArchives");
        std::filesystem::current_path(LaunchDirectory);
        const auto ResolvedFonts = ResolveContentDirectory("EngineContent/FontArchives");
        const auto ResolvedRoot  = ResolveContentDirectory("EngineContent");
        std::filesystem::current_path(OriginalDirectory);
        Require(std::filesystem::equivalent(ResolvedFonts, Fonts),
                "Launch directory shadowed executable-relative bundled fonts");
        Require(std::filesystem::equivalent(ResolvedRoot, Fonts.parent_path()),
                "Launch directory shadowed executable-relative content root");
        FontCodec Catalogue;
        Require(Catalogue.ScanDirectory(Fonts.string()), "Native font discovery failed");
        // 📝 Pinned manifest Automation/FontSources.json now ships 5 families / 35 accepted faces (DM Sans added
        // for TextureStudio parity); the catalogue rejects the 5 OFL notices. Keep these counts in lockstep.
        Require(Catalogue.QueryFamilyCount() == 5 && Catalogue.QueryTotalVariantCount() == 35,
                "Downloaded font inventory differs");
        Require(Catalogue.ScanDirectory(Fonts.string()) && Catalogue.QueryTotalVariantCount() == 35,
                "Repeated discovery duplicates fonts");
        const auto LocalFonts = Evidence / "LocalFonts";
        std::filesystem::create_directories(LocalFonts);
        const auto LocalFace = LocalFonts / "LocalTypeface-Regular.ttf";
        std::filesystem::copy_file(Fonts / "Archivo/Archivo-Regular.ttf", LocalFace,
                                   std::filesystem::copy_options::overwrite_existing);
        Require(Catalogue.ScanDirectory(LocalFonts.string()) && Catalogue.QueryTotalVariantCount() == 1,
                "New local family was not discovered");
        std::filesystem::remove(LocalFace);
        Require(!Catalogue.ScanDirectory(LocalFonts.string()) && Catalogue.QueryTotalVariantCount() == 0,
                "Removed font survived through a stale descriptor");
        Require(Catalogue.ScanEngineAndGameContent(Fonts.string(), LocalFonts.string()) &&
                    Catalogue.QueryTotalVariantCount() == 35,
                "Empty application fonts erased shared fonts");

        ImGui::CreateContext();
        auto &IO       = ImGui::GetIO();
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1100, 1000);
        IO.DeltaTime   = 1.0f / 60.0f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach((Fonts.parent_path() / "Icons").string());
        const auto UnicodeFonts = Evidence / std::filesystem::path(u8"字形 Δ");
        std::filesystem::create_directories(UnicodeFonts);
        std::filesystem::copy_file(Fonts / "Archivo/Archivo-Regular.ttf", UnicodeFonts / "unusual.ttf",
                                   std::filesystem::copy_options::overwrite_existing);
        std::ofstream(UnicodeFonts / "Broken-Regular.ttf") << "corrupted typeface";
        std::ofstream(UnicodeFonts / "FontCatalogue.toml") << "format = 1\n[[faces]]\nfamily = \"Archivo\"\nfile = "
                                                              "\"unusual.ttf\"\nweight = 400\nitalic = false\n";
        TypefaceRegistry UnicodeRegistry;
        Require(UnicodeRegistry.Load(UnicodeFonts) == 1 && UnicodeRegistry.ResolveFamilyIndex("Archivo") == 0,
                "Unicode content path or metadata-based discovery failed");
        Require(UnicodeRegistry.QueryRejectedFaceCount() == 1, "Corrupt font was not rejected safely");
        FontCodec RoundTrip;
        Require(RoundTrip.IngestToml(EncodeContentPath(UnicodeFonts / "Archivo.toml")) &&
                    RoundTrip.QueryVariant("Archivo", FontWeightCategory::Regular),
                "Font archive TOML did not round-trip");
        ActivationOwnership Ownership(new ControlCentrePanel(Fonts, PreferencesPath));
        auto               *Panel = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Control Centre initialization failed");
        Require(Panel->QueryTypefaces().QueryFamilyCount() == 5, "Registry did not load downloaded fonts");
        const auto Integrate = [&]()
        {
            (void)Panel->Integrate(1.0 / 60.0);
            Panel->PrepareInterface();
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
            Require(ImGui::GetDrawData()->TotalVtxCount > 0, "Control Centre produced no commands");
        };
        const auto Click = [&](PlaneExtent Extent)
        {
            IO.AddMousePosEvent((Extent.MinimumX + Extent.MaximumX) * 0.5f, (Extent.MinimumY + Extent.MaximumY) * 0.5f);
            Integrate();
            IO.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            Integrate();
            IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            Integrate();
            if (Panel->QueryConfirmation().QueryOpen())
            {
                const auto Primary = Panel->QueryConfirmation().QueryPrimaryExtent();
                IO.AddMousePosEvent((Primary.MinimumX + Primary.MaximumX) / 2,
                                    (Primary.MinimumY + Primary.MaximumY) / 2);
                Integrate();
                IO.AddMouseButtonEvent(0, true);
                Integrate();
                IO.AddMouseButtonEvent(0, false);
                Integrate();
            }
        };
        const auto DismissAlerts = [&]()
        {
            for (size_t Count = 0; Count < NotificationSequence::PendingCapacity &&
                                   !Panel->QueryNotificationHistory().QueryPending().empty();
                 ++Count)
            {
                const auto  Alert = Panel->QueryNotificationExtent(0);
                const float Scale = Panel->QueryAppearance().QueryApplied().InterfaceScale / 100.0f;
                Click(Spanning(Alert.MaximumX - 22 * Scale - 1, Alert.MinimumY + 29 * Scale - 1, 2, 2));
                Integrate();
            }
            Require(Panel->QueryNotificationHistory().QueryPending().empty(), "Native alert dismissal failed");
        };
        for (int Ordinal = 0; Ordinal < 30; ++Ordinal)
            Integrate();
        Capture(Evidence / "ClosedNotch.ppm");
        Click(Panel->QueryExpansionTab().QueryTabExtent());
        Require(Panel->QueryQuickOpen(), "Notch tap did not open drawer");
        for (int Ordinal = 0; Ordinal < 90; ++Ordinal)
            Integrate();
        Capture(Evidence / "QuickPanel.ppm");
        const auto Settle = [&]()
        {
            for (int Ordinal = 0; Ordinal < 90; ++Ordinal)
                Integrate();
            Require(!Panel->QueryPageMoving(), "Page transition failed to settle");
        };
        Require(Panel->QueryPage() == ControlCentrePageCategory::QuickControls,
                "Drawer did not start at quick controls");
        const auto Disc = Panel->QueryQuickTileExtent(0);
        Require(Disc.Width() == 64 && Disc.Height() == 64, "Reference circular tile dimensions changed");
        Click(Disc);
        Require(!Panel->QueryNotifications().QueryApplied().ShowFrameRateOverlay, "FPS quick toggle did not apply");
        Click(Panel->QueryQuickTileExtent(0));
        Require(Panel->QueryNotifications().QueryApplied().ShowFrameRateOverlay, "FPS quick toggle did not restore");
        Click(Panel->QueryQuickTileExtent(1));
        Require(!Panel->QueryNotificationsEnabled(), "Notification quick toggle did not disable");
        Click(Panel->QueryQuickTileExtent(1));
        Require(Panel->QueryNotificationsEnabled(), "Notification quick toggle did not restore");
        Click(Panel->QuerySettingsExtent());
        Require(Panel->QueryPage() == ControlCentrePageCategory::Settings,
                "Settings gear did not open the category list");
        Require(Panel->QueryPageMoving(), "Settings navigation skipped its inherited page transition");
        for (int Ordinal = 0; Ordinal < 16; ++Ordinal)
            Integrate();
        Capture(Evidence / "SettingsTransition.ppm");
        Settle();
        Capture(Evidence / "Settings.ppm");
        Click(Panel->QueryCategoryExtent(1));
        Require(Panel->QueryPage() == ControlCentrePageCategory::Input, "Input category navigation failed");
        Settle();
        Capture(Evidence / "Input.ppm", Panel->QueryFooterExtent());
        const auto PressChord = [&](ImGuiKey Key, bool Control = false, bool Alt = false, bool Shift = false)
        {
            IO.AddKeyEvent(ImGuiMod_Ctrl, Control);
            IO.AddKeyEvent(ImGuiMod_Alt, Alt);
            IO.AddKeyEvent(ImGuiMod_Shift, Shift);
            Integrate();
            IO.AddKeyEvent(Key, true);
            Integrate();
            IO.AddKeyEvent(Key, false);
            IO.AddKeyEvent(ImGuiMod_Ctrl, false);
            IO.AddKeyEvent(ImGuiMod_Alt, false);
            IO.AddKeyEvent(ImGuiMod_Shift, false);
            Integrate();
        };
        Click(Panel->QueryInput().QueryCustomShortcutsSwitchExtent());
        Require(Panel->QueryInput().QueryDraft().CustomShortcuts && !Panel->QueryInput().QueryApplied().CustomShortcuts,
                "Input edit did not stay in draft");
        Click(Panel->QueryInput().QueryShortcutExtent(0));
        Require(Panel->QueryInput().QueryKeyCapture(), "Shortcut field failed to start capture");
        Capture(Evidence / "InputCapture.ppm", Panel->QueryFooterExtent());
        PressChord(ImGuiKey_K, true);
        Require(!Panel->QueryInput().QueryKeyCapture() &&
                    Panel->QueryInput().QueryDraft().Shortcuts[0].Key == InputKeyCategory::K,
                "Modified shortcut capture failed");
        Require(Panel->QueryQuickOpen(), "Captured shortcut was also executed");
        Click(Panel->QueryInput().QueryShortcutExtent(1));
        PressChord(ImGuiKey_K, true);
        Require(Panel->QueryInput().QueryKeyCapture() && !Panel->QueryInput().QueryDiagnostic().empty(),
                "Duplicate chord was accepted without a conflict diagnostic");
        Capture(Evidence / "InputConflict.ppm", Panel->QueryFooterExtent());
        PressChord(ImGuiKey_Escape);
        Require(!Panel->QueryInput().QueryKeyCapture() && Panel->QueryQuickOpen(),
                "Escape did not cancel capture safely");
        Click(Panel->QueryInput().QueryShortcutExtent(2));
        IO.AddFocusEvent(false);
        Integrate();
        Require(!Panel->QueryInput().QueryKeyCapture(), "Focus loss left shortcut capture active");
        IO.AddFocusEvent(true);
        Integrate();
        Click(Panel->QueryInput().QueryProfileDropdownExtent());
        Click(Panel->QueryInput().QueryProfileOptionExtent(0));
        Require(!Panel->QueryInput().QueryDraft().CustomShortcuts, "Default profile selection failed");
        Click(Panel->QueryInput().QueryProfileDropdownExtent());
        Click(Panel->QueryInput().QueryProfileOptionExtent(1));
        Require(Panel->QueryInput().QueryDraft().CustomShortcuts, "Custom profile selection failed");
        const auto Sensitivity = Panel->QueryInput().QuerySensitivitySliderExtent();
        Click(Spanning(Sensitivity.MaximumX - 13.5f, Sensitivity.MinimumY, 1, Sensitivity.Height()));
        Require(Panel->QueryInput().QueryDraft().MouseSensitivity == 100, "Sensitivity slider endpoint differs");
        Click(Panel->QueryInput().QueryAdvancedSwitchExtent());
        Click(Panel->QueryInput().QueryInvertSwitchExtent());
        Require(Panel->QueryInput().QueryDraft().AdvancedControls && Panel->QueryInput().QueryDraft().InvertPitch,
                "Advanced/inversion controls failed");
        Click(Panel->QueryApplyExtent());
        Integrate();
        Require(!Panel->QueryInput().QueryDirty() && std::filesystem::exists(Panel->QueryInputPreferencesPath()),
                "Input Apply failed to save the edited preferences");
        Capture(Evidence / "InputConfigured.ppm", Panel->QueryFooterExtent(),
                Panel->QueryInput().QueryInvertSwitchExtent());
        IO.AddMousePosEvent(IO.MousePos.x + 5, IO.MousePos.y + 7);
        Integrate();
        Require(Panel->QueryInput().QueryPointerDelta().X == 10 && Panel->QueryInput().QueryPointerDelta().Y == -14,
                "Applied sensitivity/inversion did not reach the real pointer-response preview");
        Capture(Evidence / "InputPointerResponse.ppm", Panel->QueryFooterExtent());
        Click(Panel->QueryDefaultsExtent());
        Require(Panel->QueryInput().QueryDirty() && Panel->QueryInput().QueryDraft() == InputPreferences{},
                "Defaults did not produce an unapplied default draft");
        Panel->QueryInput().ResetDraft();
        Require(Panel->QueryInput().QueryApplied().CustomShortcuts, "Discard defaults overwrote the applied map");
        PressChord(ImGuiKey_K, true, false, true);
        Require(Panel->QueryQuickOpen(), "Shortcut ignored an extra modifier");
        PressChord(ImGuiKey_K, true);
        Require(!Panel->QueryQuickOpen(), "Applied custom shortcut did not close the drawer");
        Settle();
        PressChord(ImGuiKey_Comma, true);
        Require(!Panel->QueryQuickOpen(), "Previous shortcut remained active after replacement");
        PressChord(ImGuiKey_K, true);
        Settle();
        Require(Panel->QueryQuickOpen(), "Applied custom shortcut did not open the drawer");
        PressChord(ImGuiKey_F, true, true);
        Settle();
        Require(Panel->QueryPage() == ControlCentrePageCategory::Appearance &&
                    Panel->QueryAppearancePage() == AppearancePageCategory::Fonts,
                "Typography shortcut did not navigate");
        PressChord(ImGuiKey_T, true, true);
        Settle();
        Require(Panel->QueryAppearancePage() == AppearancePageCategory::Theme, "Theme shortcut did not navigate");
        PressChord(ImGuiKey_Escape);
        Require(!Panel->QueryQuickOpen(), "Dismiss shortcut did not close the drawer");
        Settle();
        PressChord(ImGuiKey_K, true);
        Settle();
        Click(Panel->QueryBackExtent());
        Settle();
        Click(Panel->QueryCategoryExtent(1));
        Settle();
        Click(Panel->QueryInput().QueryProfileDropdownExtent());
        Require(Panel->QueryInput().QueryMenuOpen(), "Profile menu did not open");
        Click(Spanning(2, 400, 2, 2));
        Require(!Panel->QueryQuickOpen() && !Panel->QueryInput().QueryMenuOpen(),
                "Drawer dismissal left a hidden menu active");
        Settle();
        PressChord(ImGuiKey_K, true);
        Settle();
        Click(Panel->QueryInput().QueryShortcutExtent(2));
        Require(Panel->QueryInput().QueryKeyCapture(), "Capture did not restart");
        Click(Spanning(2, 400, 2, 2));
        Require(!Panel->QueryQuickOpen() && !Panel->QueryInput().QueryKeyCapture(),
                "Drawer dismissal left hidden key capture active");
        Settle();
        PressChord(ImGuiKey_K, true);
        Settle();
        Click(Panel->QueryBackExtent());
        Require(Panel->QueryPage() == ControlCentrePageCategory::Settings,
                "Input back did not return to category list");
        Settle();
        Click(Panel->QueryCategoryExtent(2));
        Require(Panel->QueryPage() == ControlCentrePageCategory::Notifications,
                "Notifications category navigation failed");
        Settle();
        Click(Panel->QueryNotifications().QueryToggleExtent(0));
        Require(!Panel->QueryNotifications().QueryDraft().ShowFrameRateOverlay &&
                    Panel->QueryNotifications().QueryApplied().ShowFrameRateOverlay,
                "Notification page failed to keep its change in draft");
        Click(Panel->QueryApplyExtent());
        Require(!Panel->QueryNotifications().QueryApplied().ShowFrameRateOverlay,
                "Notification session apply did not update the real overlay flag");
        Integrate();
        Capture(Evidence / "Notifications.ppm", Panel->QueryFooterExtent());
        // The new top-right cards intentionally own input over the underlying back button.
        DismissAlerts();
        Click(Panel->QueryBackExtent());
        Settle();
        Click(Panel->QueryBackExtent());
        Require(Panel->QueryPage() == ControlCentrePageCategory::QuickControls,
                "Settings back did not return to quick controls");
        Settle();
        Click(Panel->QueryQuickTileExtent(0));
        Require(Panel->QueryNotifications().QueryApplied().ShowFrameRateOverlay,
                "Quick tile did not share notification page state");
        const auto  Grip  = Panel->QueryExpansionTab().QueryGripExtent();
        const float GripX = (Grip.MinimumX + Grip.MaximumX) * 0.5f;
        const float GripY = (Grip.MinimumY + Grip.MaximumY) * 0.5f;
        IO.AddMousePosEvent(GripX, GripY);
        Integrate();
        IO.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        Integrate();
        IO.AddMousePosEvent(GripX, GripY - 120);
        Integrate();
        Require(Panel->QueryExpansionTab().QueryHeight() < IO.DisplaySize.y - 100, "Swipe did not carry the sheet");
        Integrate();
        Capture(Evidence / "SwipeUp.ppm");
        IO.AddMousePosEvent(GripX, GripY - 240);
        Integrate();
        IO.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        Integrate();
        Require(!Panel->QueryQuickOpen(), "Upward mouse swipe failed to close drawer");
        for (int Ordinal = 0; Ordinal < 90; ++Ordinal)
            Integrate();
        Require(Panel->QueryExpansionTab().QueryHeight() == 0, "Dismissed sheet failed to settle closed");
        Click(Panel->QueryExpansionTab().QueryTabExtent());
        Settle();
        Click(Panel->QuerySettingsExtent());
        Settle();
        Click(Panel->QueryCategoryExtent(0));
        Require(Panel->QueryPage() == ControlCentrePageCategory::Appearance, "Appearance category navigation failed");
        Settle();
        Capture(Evidence / "Fonts.ppm", Panel->QueryFooterExtent());
        Click(Panel->QueryTabExtent(0));
        Require(Panel->QueryAppearancePage() == AppearancePageCategory::Display,
                "Display tab pointer navigation failed");
        Integrate();
        Capture(Evidence / "Display.ppm", Panel->QueryFooterExtent());
        Click(Panel->QueryAppearance().QueryScaleSliderExtent());
        Require(Panel->QueryAppearance().QueryDirty(), "Interface scale slider did not edit its draft");
        Panel->ResetPreferences();
        Click(Panel->QueryTabExtent(1));
        Require(Panel->QueryAppearancePage() == AppearancePageCategory::Fonts,
                "Typography tab pointer navigation failed");
        IO.AddKeyEvent(ImGuiMod_Ctrl, true);
        IO.AddKeyEvent(ImGuiKey_K, true);
        Integrate();
        Require(!Panel->QueryCentreOpen(), "Keyboard shortcut did not close Control Centre");
        IO.AddKeyEvent(ImGuiKey_K, false);
        IO.AddKeyEvent(ImGuiMod_Ctrl, false);
        Integrate();
        Panel->ActivatePage(AppearancePageCategory::Fonts);
        for (int Ordinal = 0; Ordinal < 90; ++Ordinal)
            Integrate();
        for (auto *Window : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(Window->Name, "Appearance content"))
                ImGui::SetScrollY(Window, 850.0f);
        Integrate();
        Integrate();
        Capture(Evidence / "TypeScale.ppm", Panel->QueryFooterExtent());
        for (auto *Window : ImGui::GetCurrentContext()->Windows)
            if (std::strstr(Window->Name, "Appearance content"))
                ImGui::SetScrollY(Window, 0.0f);
        Integrate();
        Integrate();
        Click(Panel->QueryAppearance().QueryFontCardExtent(1));
        Require(Panel->QueryAppearance().QueryDraft().FontFamily == 1, "Font card did not respond to click");
        Require(Panel->QueryAppearance().QueryApplied().FontFamily == 0, "Draft font applied prematurely");
        Click(Panel->QueryBackExtent());
        Settle();
        Click(Panel->QueryCategoryExtent(0));
        Settle();
        Require(Panel->QueryAppearance().QueryDraft().FontFamily == 1 && Panel->QueryAppearance().QueryDirty(),
                "Back navigation silently discarded an unsaved appearance draft");
        Panel->ResetPreferences();
        Require(!Panel->QueryAppearance().QueryDirty(), "Discard did not restore applied font");
        Click(Panel->QueryTabExtent(2));
        Require(Panel->QueryAppearancePage() == AppearancePageCategory::Theme, "Theme tab pointer navigation failed");
        Integrate();
        Integrate();
        Capture(Evidence / "Theme.ppm", Panel->QueryFooterExtent());
        Click(Panel->QueryAppearance().QueryThemeTileExtent(2));
        Require(Panel->QueryAppearance().QueryDraft().Theme == ThemeCategory::Light,
                "Theme tile did not respond to native pointer events");
        Require(Panel->QueryAppearance().QueryApplied().Theme == ThemeCategory::Dark,
                "Theme draft applied prematurely");
        Click(Panel->QueryApplyExtent());
        Require(!Panel->QueryAppearance().QueryDirty() && std::filesystem::exists(PreferencesPath),
                "Native Apply button did not save the appearance draft");
        Integrate();
        Integrate();
        Require(ControlKit::Palette().LightSurface, "Theme apply did not change the actual palette");
        Require(Panel->QueryVisibleNotifications() > 0 &&
                    Panel->QueryNotificationExtent(0).MinimumX > IO.DisplaySize.x * 0.5f &&
                    !Panel->QueryNotificationHistory().QueryHistory().empty(),
                "Applied preference alert did not reach the top-right card stack/history");
        Capture(Evidence / "LightTheme.ppm", Panel->QueryFooterExtent());

        auto Preferences           = Panel->QueryAppearance().QueryDraft();
        Preferences.FontFamily     = 1;
        Preferences.RoleSize[3]    = 18;
        Preferences.RoleWeight[3]  = FontWeightCategory::SemiBold;
        Preferences.InterfaceScale = 125;
        Panel->QueryAppearance().ConfigureDraft(Preferences);
        Require(Panel->FinalizePreferences(), "Typography apply failed");
        Integrate();
        Require(ImGui::GetIO().FontDefault == Panel->QueryTypefaces().QueryHandle(1, FontWeightCategory::SemiBold),
                "Applied body weight did not select the actual font face");
        Require(ImGui::GetStyle().FontSizeBase == 18, "Applied body size differs");
        Require(ImGui::GetStyle().FontScaleMain == 1.25f, "Applied interface scale differs");
        const auto ScaledTile = Panel->QueryAppearance().QueryThemeTileExtent(0);
        Click(PlaneExtent{ScaledTile.MinimumX * 1.25f, ScaledTile.MinimumY * 1.25f, ScaledTile.MaximumX * 1.25f,
                          ScaledTile.MaximumY * 1.25f});
        Require(Panel->QueryAppearance().QueryDraft().Theme == ThemeCategory::Oled,
                "Scaled pointer coordinates did not select the correct theme");
        Panel->ResetPreferences();
        DismissAlerts();
        Click(Panel->QueryBackExtent());
        Settle();
        Require(Panel->QueryPage() == ControlCentrePageCategory::Settings, "Scaled sub-page back failed");
        Click(Panel->QueryBackExtent());
        Settle();
        Click(Panel->QuerySettingsExtent());
        Settle();
        Click(Panel->QueryCategoryExtent(0));
        Settle();
        Require(Panel->QueryPage() == ControlCentrePageCategory::Appearance, "Scaled category navigation failed");
        const auto       *Family         = Panel->QueryTypefaces().QueryFamily(1);
        const std::string ExpectedFamily = Family->Name;
        Ownership.reset();
        Require(TypefaceRegistry::QueryCurrent() == nullptr, "Finalization left a dangling global registry");
        Ownership.reset(new ControlCentrePanel(Fonts, PreferencesPath));
        Panel = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Restored initialization failed");
        Require(Panel->QueryAppearance().QueryApplied().Theme == ThemeCategory::Light,
                "Theme was not restored from disk");
        Require(Panel->QueryTypefaces().QueryFamily(Panel->QueryAppearance().QueryApplied().FontFamily)->Name ==
                    ExpectedFamily,
                "Font family was not restored by name");
        Require(Panel->QueryAppearance().QueryApplied().RoleSize[3] == 18, "Body size was not restored");
        Require(Panel->QueryInput().QueryApplied().Shortcuts[0].Key == InputKeyCategory::K &&
                    Panel->QueryInput().QueryApplied().MouseSensitivity == 100 &&
                    Panel->QueryInput().QueryApplied().InvertPitch,
                "Input preferences did not survive a fresh panel lifecycle");
        auto Invalid         = Panel->QueryAppearance().QueryDraft();
        Invalid.CornerRadius = std::numeric_limits<float>::quiet_NaN();
        Require(!AppearanceCodec::Validate(Invalid), "NaN appearance accepted");
        std::string Diagnostic;
        Require(!AppearanceCodec::Encode(PreferencesPath, Invalid, ExpectedFamily, Diagnostic),
                "Invalid preferences overwrote the saved file");
        AppearanceSettings Restored;
        std::string        FamilyName;
        Require(AppearanceCodec::Decode(PreferencesPath, Restored, FamilyName, Diagnostic),
                "Failed save corrupted previously valid preferences");
        IO.DisplaySize = ImVec2(375, 700);
        Panel->ActivateSettingsPage(ControlCentrePageCategory::Input);
        Settle();
        const auto NarrowFooter   = Panel->QueryFooterExtent();
        const auto NarrowApply    = Panel->QueryApplyExtent();
        const auto NarrowDefaults = Panel->QueryDefaultsExtent();
        Require(NarrowApply.MinimumX >= NarrowFooter.MinimumX && NarrowApply.MaximumX <= NarrowFooter.MaximumX &&
                    NarrowDefaults.MinimumX >= NarrowFooter.MinimumX &&
                    NarrowDefaults.MaximumX <= NarrowFooter.MaximumX &&
                    NarrowDefaults.MaximumY <= NarrowFooter.MaximumY && NarrowFooter.MaximumX <= 375,
                "Narrow scaled footer controls overflowed or hid Defaults");
        const auto NarrowRange = Panel->QueryInput().QuerySensitivitySliderExtent();
        Require(NarrowRange.Width() > 0 && NarrowRange.MinimumX >= 0 && NarrowRange.MaximumX * 1.25f <= 375,
                "Narrow scaled sensitivity slider has invalid bounds");
        Ownership.reset();
        ImGui::DestroyContext();
        std::cout << "PASS: 4 families / 30 faces, discovery, removal, native "
                     "font/theme clicks, draft/discard/apply, "
                  << "settings gear/category/back/tab navigation, transitions, "
                     "shared FPS state, session notifications, "
                  << "actual palette/font selection, persistent preferences, "
                     "invalid settings, lifecycle cleanup\n"
                  << "Images are actual ImGui CPU raster output, not Windows GPU "
                     "screenshots.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        return 1;
    }
}
