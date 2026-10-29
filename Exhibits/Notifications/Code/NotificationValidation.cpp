/*==========================================================================================================================================
                                                        NOTIFICATIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise bounded notification storage, real preference events, top-right dismissal isolation, and native cards.

#include "Engine/Interface/IconPresentation.h"
#include "Exhibits/ControlCentre/Code/CpuRasterization.h"
#include "UI/Editor/ControlCentrePanel.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
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
    const int                  Width  = static_cast<int>(ImGui::GetIO().DisplaySize.x);
    const int                  Height = static_cast<int>(ImGui::GetIO().DisplaySize.y);
    std::vector<unsigned char> Pixels(Width * Height * 3, 65);
    for (const auto *List : ImGui::GetDrawData()->CmdLists)
        FrontierExhibit::RasterizeCommands(List, Pixels.data(), Width, Height, ImGui::GetDrawData()->DisplayPos,
                                           ImGui::GetDrawData()->FramebufferScale);
    std::ofstream Output(Path, std::ios::binary);
    Output << "P6\n" << Width << ' ' << Height << "\n255\n";
    Output.write(reinterpret_cast<const char *>(Pixels.data()), static_cast<std::streamsize>(Pixels.size()));
    Require(Output.good(), "Capture publication failed");
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 3, "Expected engine content and disposable evidence directory");
        const std::filesystem::path Content  = std::filesystem::absolute(Arguments[1]);
        const std::filesystem::path Evidence = std::filesystem::absolute(Arguments[2]);
        std::filesystem::remove_all(Evidence);
        std::filesystem::create_directories(Evidence);
        NotificationSequence Sequence;
        const auto           First = Sequence.Broadcast(NotificationCategory::Success, "Saved", "Actual message", 2);
        Sequence.Integrate(100, 0);
        Require(Sequence.QueryPending().size() == 1, "Invisible alerts lost their hold interval");
        Sequence.Integrate(100, 1, First);
        Require(Sequence.QueryPending().size() == 1, "Hovered alert did not pause expiry");
        Sequence.Integrate(std::numeric_limits<double>::quiet_NaN(), 1);
        Sequence.Integrate(-1, 1);
        Require(Sequence.QueryPending().front().RemainingSeconds == 2, "Invalid time modified expiry");
        Sequence.Integrate(2, 1);
        Require(Sequence.QueryPending().empty() && Sequence.QueryHistory().size() == 1,
                "Expired alert did not remain in history");
        const auto Hidden = Sequence.Broadcast(NotificationCategory::Error, "Hidden", "History only", 3, false);
        Require(Sequence.QueryPending().empty() && Sequence.QueryHistory().back().Token == Hidden,
                "Suppressed alert disappeared from history");
        for (size_t Index = 0; Index < 100; ++Index)
            Sequence.Broadcast(NotificationCategory::Information, std::string(200, 'A'), std::string(3000, 'B'),
                               std::numeric_limits<double>::infinity());
        Require(Sequence.QueryHistory().size() == 64 && Sequence.QueryPending().size() == 16,
                "Notification capacities exceeded");
        Require(Sequence.QueryPending().back().Title.size() <= 160 &&
                    Sequence.QueryPending().back().Description.size() <= 2048 &&
                    Sequence.QueryPending().back().RemainingSeconds == 3.5,
                "Notification text or duration was not bounded");
        const auto PendingToken = Sequence.QueryPending().front().Token;
        Require(Sequence.Reclaim(PendingToken) && !Sequence.Reclaim(PendingToken) &&
                    Sequence.QueryHistory().size() == 64,
                "Dismissal damaged history or authenticated a stale token");
        Sequence.ResetPresentation();
        Require(Sequence.QueryPending().empty() && Sequence.QueryHistory().size() == 64,
                "Presentation reset erased history");
        Sequence.ResetHistory();
        Require(Sequence.QueryHistory().empty(), "History clear failed");
        Require(Sequence.Broadcast(static_cast<NotificationCategory>(255), "", "", 3) == 0,
                "Invalid notification category accepted");

        std::string Unicode;
        for (int Index = 0; Index < 1100; ++Index)
            Unicode += "Δ";
        Sequence.Broadcast(NotificationCategory::Information, Unicode, Unicode, 1);
        const auto &UnicodeEntry = Sequence.QueryPending().back();
        Require(UnicodeEntry.Title.size() == 159 && UnicodeEntry.Description.size() == 2047 &&
                    UnicodeEntry.Title.substr(UnicodeEntry.Title.size() - 5) == "Δ..." &&
                    UnicodeEntry.Description.substr(UnicodeEntry.Description.size() - 5) == "Δ...",
                "Storage truncation split a UTF-8 code point");
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        IO.IniFilename = nullptr;
        IO.DisplaySize = ImVec2(1100, 1000);
        IO.DeltaTime   = 1.0f / 60.0f;
        IO.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth  = 4096;
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
        IconPresentation::Attach(Content / "Icons");
        ActivationOwnership Ownership(
            new ControlCentrePanel(Content / "FontArchives", Evidence / "Personal.projectspace"));
        auto *Panel = static_cast<ControlCentrePanel *>(Ownership.get());
        Require(Panel->Initialize() && Panel->Activate(), "Notification host initialization failed");
        const auto Frame = [&]()
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
            for (int Index = 0; Index < 80; ++Index)
                Frame();
        };
        const auto Pointer = [&](float X, float Y, bool Down)
        {
            IO.AddMousePosEvent(X, Y);
            IO.AddMouseButtonEvent(0, Down);
            Frame();
        };
        Frame();
        Panel->ActivatePage(AppearancePageCategory::Theme);
        Settle();
        auto Input             = Panel->QueryInput().QueryDraft();
        Input.MouseSensitivity = 61;
        Panel->QueryInput().ConfigureDraft(Input);
        Require(Panel->FinalizeInputPreferences(), "Real input preference save failed");
        Frame();
        Require(Panel->QueryVisibleNotifications() == 1 &&
                    Panel->QueryNotificationHistory().QueryHistory().back().Category == NotificationCategory::Success,
                "Successful save did not broadcast a semantic card/history entry");
        auto Card = Panel->QueryNotificationExtent(0);
        Require(Card.MinimumX > 550 && Card.MaximumX <= 1088 && Card.MinimumY >= 12,
                "Saved card was not anchored top-right");
        Capture(Evidence / "SavedTopRight.ppm");
        // A release over X is not a dismissal unless that same X received the press.
        Pointer(Card.MinimumX + 70, Card.MinimumY + 30, true);
        Pointer(Card.MaximumX - 22, Card.MinimumY + 29, false);
        Require(Panel->QueryNotificationHistory().QueryPending().size() == 1,
                "Body-to-close drag incorrectly dismissed an alert");
        Pointer(Card.MaximumX - 22, Card.MinimumY + 29, true);
        Pointer(Card.MaximumX - 22, Card.MinimumY + 29, false);
        Frame();
        Require(Panel->QueryNotificationHistory().QueryPending().empty() &&
                    Panel->QueryNotificationHistory().QueryHistory().size() == 1 && Panel->QueryCentreOpen(),
                "Dismissal erased history or clicked through into the drawer");
        // A real write refusal must be an error, retain the applied profile, and keep the draft.
        std::filesystem::create_directory(Evidence / "Personal.projectspace.tmp");
        Input.MouseSensitivity = 72;
        Panel->QueryInput().ConfigureDraft(Input);
        Require(!Panel->FinalizeInputPreferences() && Panel->QueryInput().QueryDirty() &&
                    Panel->QueryInput().QueryApplied().MouseSensitivity == 61,
                "Failed save changed the applied profile");
        Frame();
        Require(Panel->QueryNotificationHistory().QueryHistory().back().Category == NotificationCategory::Error,
                "Actual save failure did not broadcast an error");
        Capture(Evidence / "SaveError.ppm");
        std::filesystem::remove(Evidence / "Personal.projectspace.tmp");
        Panel->QueryInput().ResetDraft();
        Panel->ActivateSettingsPage(ControlCentrePageCategory::Notifications);
        Settle();
        Panel->ActivatePreviousPage();
        Settle();
        Panel->ActivatePreviousPage();
        Settle();
        Require(Panel->QueryPage() == ControlCentrePageCategory::QuickControls &&
                    Panel->QueryNotificationStreamExtent().MinimumX > Panel->QueryQuickTileExtent(1).MaximumX,
                "Notification Stream is not split to the right of quick-control tiles");
        Capture(Evidence / "NotificationHistory.ppm");
        const auto Clear = Panel->QueryNotificationClearExtent();
        Pointer((Clear.MinimumX + Clear.MaximumX) / 2, (Clear.MinimumY + Clear.MaximumY) / 2, true);
        Pointer((Clear.MinimumX + Clear.MaximumX) / 2, (Clear.MinimumY + Clear.MaximumY) / 2, false);
        Require(Panel->QueryNotificationHistory().QueryHistory().empty() &&
                    !Panel->QueryNotificationHistory().QueryPending().empty(),
                "Native Clear history did not clear only the retained history");
        auto Preferences               = Panel->QueryNotifications().QueryDraft();
        Preferences.PreferenceErrors   = false;
        Preferences.PreferencesApplied = false;
        Panel->QueryNotifications().ConfigureDraft(Preferences);
        Require(Panel->FinalizeNotificationPreferences(), "Suppression preference save failed");
        IO.AddMousePosEvent(20, 900);
        for (int Index = 0; Index < 240; ++Index)
            Frame();
        const auto Before      = Panel->QueryNotificationHistory().QueryHistory().size();
        Input.MouseSensitivity = 73;
        Panel->QueryInput().ConfigureDraft(Input);
        Require(Panel->FinalizeInputPreferences(), "Suppressed preference save failed");
        Frame();
        Require(Panel->QueryNotificationHistory().QueryPending().empty() &&
                    Panel->QueryNotificationHistory().QueryHistory().size() == Before + 1,
                "Disabled success alerts were shown or lost from history");
        // Scale, narrow viewport, and the shared tab must not overlap the top-right card.
        Preferences.PreferencesApplied = true;
        Panel->QueryNotifications().ConfigureDraft(Preferences);
        Require(Panel->FinalizeNotificationPreferences(), "Re-enabling alerts failed");
        auto Appearance           = Panel->QueryAppearance().QueryDraft();
        Appearance.InterfaceScale = 125;
        Appearance.Theme          = ThemeCategory::Light;
        Panel->QueryAppearance().ConfigureDraft(Appearance);
        Require(Panel->FinalizePreferences(), "Light/narrow preferences failed");
        IO.DisplaySize = ImVec2(375, 700);
        Frame();
        Card = Panel->QueryNotificationExtent(0);
        Require(Card.Width() > 0 && Card.MinimumX >= 0 && Card.MaximumX <= 375 && Card.MaximumY <= 700,
                "Narrow/scaled notification escaped the viewport");
        Capture(Evidence / "NotificationNarrow.ppm");
        // Close the drawer using the real keyboard route, then verify tab separation.
        IO.AddKeyEvent(ImGuiKey_Escape, true);
        Frame();
        IO.AddKeyEvent(ImGuiKey_Escape, false);
        Settle();
        Card           = Panel->QueryNotificationExtent(0);
        const auto Tab = Panel->QueryExpansionTab().QueryTabExtent();
        Require(Card.MinimumY >= Tab.MaximumY, "Closed expansion tab overlapped the notification card");
        IO.DisplaySize = ImVec2(1100, 1000);
        Panel->ActivatePage(AppearancePageCategory::Theme);
        Settle();
        const float AppliedRadius = Panel->QueryAppearance().QueryApplied().CornerRadius;
        Appearance                = Panel->QueryAppearance().QueryDraft();
        Appearance.CornerRadius   = AppliedRadius == 24 ? 28 : 24;
        Panel->QueryAppearance().ConfigureDraft(Appearance);
        const auto ClickExtent = [&](PlaneExtent Extent)
        {
            Pointer((Extent.MinimumX + Extent.MaximumX) / 2, (Extent.MinimumY + Extent.MaximumY) / 2, true);
            Pointer((Extent.MinimumX + Extent.MaximumX) / 2, (Extent.MinimumY + Extent.MaximumY) / 2, false);
        };
        const auto QueryBytes = [&]()
        {
            std::ifstream Stream(Evidence / "Personal.projectspace", std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(Stream), {});
        };
        const auto BytesBefore   = QueryBytes();
        const auto HistoryBefore = Panel->QueryNotificationHistory().QueryHistory().size();
        ClickExtent(Panel->QueryApplyExtent());
        Frame();
        Require(Panel->QueryVisibleNotifications() == 0 && Panel->QueryConfirmation().QueryOpen() &&
                    Panel->QueryAppearance().QueryDirty() &&
                    Panel->QueryAppearance().QueryApplied().CornerRadius == AppliedRadius,
                "Save request applied changes before confirmation");
        Capture(Evidence / "ConfirmApply.ppm");
        const auto PageBefore = Panel->QueryPage();
        ClickExtent(Panel->QueryBackExtent());
        Require(Panel->QueryConfirmation().QueryOpen() && Panel->QueryPage() == PageBefore,
                "Modal outside click dismissed the popup or changed underlying preferences");
        ClickExtent(Panel->QueryConfirmation().QueryCancelExtent());
        Frame();
        Require(!Panel->QueryConfirmation().QueryOpen() && Panel->QueryAppearance().QueryDirty() &&
                    Panel->QueryAppearance().QueryApplied().CornerRadius == AppliedRadius &&
                    QueryBytes() == BytesBefore &&
                    Panel->QueryNotificationHistory().QueryHistory().size() == HistoryBefore,
                "Cancel changed the applied preferences or erased the draft");
        ClickExtent(Panel->QueryApplyExtent());
        Frame();
        IO.AddKeyEvent(ImGuiKey_Escape, true);
        Frame();
        IO.AddKeyEvent(ImGuiKey_Escape, false);
        Frame();
        Require(!Panel->QueryConfirmation().QueryOpen() && Panel->QueryCentreOpen() &&
                    Panel->QueryAppearance().QueryDirty(),
                "Escape did not cancel only the modal");
        ClickExtent(Panel->QueryApplyExtent());
        Frame();
        // Native keyboard navigation: default focus is Cancel, Enter must not approve the save.
        IO.AddKeyEvent(ImGuiKey_Enter, true);
        Frame();
        IO.AddKeyEvent(ImGuiKey_Enter, false);
        Frame();
        Require(!Panel->QueryConfirmation().QueryOpen() && Panel->QueryAppearance().QueryDirty(),
                "Default Enter did not select the safe Cancel action");
        ClickExtent(Panel->QueryApplyExtent());
        Frame();
        IO.AddKeyEvent(ImGuiKey_Tab, true);
        Frame();
        IO.AddKeyEvent(ImGuiKey_Tab, false);
        Frame();
        IO.AddKeyEvent(ImGuiKey_Enter, true);
        Frame();
        IO.AddKeyEvent(ImGuiKey_Enter, false);
        Frame();
        Require(!Panel->QueryConfirmation().QueryOpen() && !Panel->QueryAppearance().QueryDirty() &&
                    Panel->QueryAppearance().QueryApplied().CornerRadius == Appearance.CornerRadius &&
                    QueryBytes() != BytesBefore,
                "Confirmed Apply did not perform the actual preference save");
        Panel->ActivatePreviousPage();
        Settle();
        Panel->ActivatePreviousPage();
        Settle();
        IO.DisplaySize = ImVec2(375, 700);
        Settle();
        Require(Panel->QueryNotificationStreamExtent().MinimumY > Panel->QueryQuickTileExtent(1).MaximumY &&
                    Panel->QueryNotificationStreamExtent().MaximumX <= 375,
                "Narrow Notification Stream did not stack below quick-control tiles");
        Capture(Evidence / "StreamNarrow.ppm");
        IO.DisplaySize = ImVec2(1100, 1000);
        // The second supplied dialogue variant is explicitly a fixture, with no invented operation.
        ConfirmationDialogue      Fixture;
        ConfirmationSpecification Prompt;
        Prompt.Title = "Error presentation fixture";
        Prompt.Description =
            "Semantic header and cancel/apply footer. No application operation is executed by this fixture.";
        Prompt.Category     = NotificationCategory::Error;
        Prompt.Presentation = ConfirmationPresentationCategory::SemanticHeader;
        Require(Fixture.Activate(Prompt), "Semantic confirmation activation failed");
        for (int Index = 0; Index < 2; ++Index)
        {
            ImGui::NewFrame();
            (void)Fixture.Construct(1);
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
        }
        Capture(Evidence / "ConfirmSemantic.ppm");
        IO.AddKeyEvent(ImGuiKey_Escape, true);
        ImGui::NewFrame();
        Require(Fixture.Construct(1) == ConfirmationResponseCategory::Cancelled,
                "Semantic fixture Escape did not cancel");
        ImGui::Render();
        IO.AddKeyEvent(ImGuiKey_Escape, false);
        // These four semantic examples are labelled fixtures, not invented operational events.
        IO.DisplaySize = ImVec2(1100, 1000);
        for (const bool Light : {false, true})
        {
            ThemeStructure Theme;
            Theme.ConfigureTheme(Light ? ThemeCategory::Light : ThemeCategory::Dark);
            ControlKit::ConfigureTheme(Theme, ColorQuad{1, .7f, 0, 1}, ColorQuad{.1f, .8f, .4f, 1},
                                       ColorQuad{.2f, .5f, 1, 1}, ColorQuad{1, .3f, .3f, 1});
            ImGui::NewFrame();
            PixelSpace Surface;
            Require(Surface.Begin(SurfaceLayer::Above, 1100, 1000), "Fixture surface failed");
            Surface.FillRectangle(Spanning(0, 0, 1100, 1000), ControlKit::Palette().Raised);
            Surface.Text(50, 70, ControlKit::Palette().Text, "Semantic card fixtures - production NotificationCard",
                         20);
            const char *Titles[] = {"Information fixture", "Success fixture", "Warning fixture", "Error fixture"};
            for (size_t Index = 0; Index < 4; ++Index)
            {
                NotificationEntry Entry;
                Entry.Category    = static_cast<NotificationCategory>(Index);
                Entry.Title       = Titles[Index];
                Entry.Description = "Short and clear additional description.";
                (void)NotificationCard::Construct(Surface, Spanning(625, 190 + Index * 125, 450, 88), Entry, {}, true);
            }
            ImGui::Render();
            FrontierExhibit::AcknowledgeTextures();
            Capture(Evidence / (Light ? "CardsLight.ppm" : "CardsDark.ppm"));
        }
        Ownership.reset();
        ImGui::DestroyContext();
        std::cout
            << "PASS: bounded history/queue, visible-only expiry, hover pause, actual save/error events, suppression, "
               "authenticated dismiss/no click-through, top-right placement, responsive stream, 125% narrow viewport, "
               "modal cancel/Escape/default-focus/Tab+Enter/apply; nine native captures\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        return 1;
    }
}
