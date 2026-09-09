//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/ControlCentreHost.h — Top Notch Control Centre: Drawer Locomotion, Notch Travel and Overlay Recording
//============================================================================================================================================
// 🧩 The pull-down shade behind a notch that hangs from the top edge of the display.
//
//    Geometry  (from the Notch reference, ArcNotch.tsx):
//      • notch handle 400 × 36 px, SVG outline
//            M 0 0  C 15 0, 20 6, 25 15  L 35 28  C 40 34, 45 36, 52 36  L 348 36  C 355 36, 360 34, 365 28
//            L 375 15  C 380 6, 385 0, 400 0  Z
//        tessellated once; the handle carries the project name centred in it.
//      • shade: a full-width sheet whose lower edge sits at the handle's top; closed at Y = 0, open at
//        Y = DisplayHeight − 36. Scrim over the scene fades 0 → 40 % black across that travel.
//
//    Interaction (from the Slate DrawerSpace):
//      • press on the handle grabs it; a release within 6 px / 350 ms is a TAP → toggle open/closed.
//      • the first travel past 6 px decides the axis ONCE: mostly-vertical → carry the shade (Y);
//        mostly-horizontal → slide the notch along the top edge (X) within ±(DisplayWidth − 400)/2.
//        Beyond a bound the carry accepts 5 % of the overshoot (elastic) and springs back on release.
//      • Y release classification (Notch ArcNotch.tsx): released past H/2 it stays open unless flung upward
//        faster than 20 px/s or pulled back more than 50 px; released before H/2 it closes unless flung downward
//        faster than 20 px/s or pulled more than 50 px. The release velocity is injected into the spring.
//      • while open, a press on the scrim (outside the shade content) closes the shade.
//
//    Dashboard (Notch ArcNotch.tsx "control-center" page), shown inside the shade once it is pulled down:
//      • card 420 × 480 centred in the shade, opacity 0 → 1 as shade Y goes 100 → H/2, pointer-active once Y > 100.
//      • header row: "Control Center" 13 px white/50; right: wifi 16 px, settings gear 16 px.
//      • quick-settings grid 4 columns, column gap 16, row gap 40: 64 px disc (#3B82F6 active / #1C1C1E idle,
//        hover #60A5FA / #2C2C2E), 24 px lucide glyph (stroke 2 active, 1.5 idle), 12 px white/70 label under a 12 px gap.
//      • render-scale pill: #1C1C1E rounded-full, 8 px padding, 48 px glyph cell, 8 px track black/50 with #6366F1/90
//        fill, 48 px mono 11 px bold value cell.
//    Tile contents are the engine's controls (the Notch mock's Blender labels are not reproduced):
//        Global Illumination (toggle) · Anti-Aliasing (toggle) · FPS Overlay (toggle) · Notifications (toggle) ·
//        Quality (tap cycles Minimal → Economy → Standard → Ultra → Reference) · three empty slots.
//
//    Rendering: ConstructControlLayout(PixelSpace&) — hosts call it after Advance*; nothing here names ImGui.
//
// ⚠️ Public API kept source-compatible with WorkspaceHost / Project-F20 (Initialize, Terminate, AdvanceInteraction,
//    AdvanceLocomotion, OpenNotch, CloseNotch, ToggleNotch, IsOpen, IsDragging, Query*/Convert).

#pragma once

#if defined(_MSC_VER)
    #pragma warning(disable: 4324)
#endif

#include "ThemeStructure.h"
#include "MotionIntegrator.h"
#include "AppearanceInspector.h"
#include "ControlKit.h"
#include "ConfigurationInspector.h"
#include "DialogueHost.h"
#include "PixelSpace.h"
#include "FidelityClassifier.h"
#include "VectorCodec.h"
#include "../DeviceExchange/InputExchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                 BEZIER POINT RECORD
//------------------------------------------------------------------------------------------------------------------------

struct BezierPointIndex
{
    float X;                 // [px]
    float Y;                 // [px]
};

//------------------------------------------------------------------------------------------------------------------------
//                                         CONTROL CENTRE PAGE CATEGORY  (content pages arrive in later steps)
//------------------------------------------------------------------------------------------------------------------------

enum class ControlCentrePageCategory : uint32_t
{
    Dashboard      = 0,
    SettingsHub    = 1,      // list of the four settings rows (420 × 480 card)
    RenderSettings = 2,      // sub-pages: card springs to 840 × 600
    Appearance     = 3,
    Input          = 4,
    Notifications  = 5,
    Count          = 6
};

// Notch SettingsModal tab order: Display · Fonts · Theme (Fonts is the initial tab).
enum class AppearanceSubTabCategory : uint32_t { Display = 0, Fonts = 1, Theme = 2, Count = 3 };

//------------------------------------------------------------------------------------------------------------------------
//                                                   QUICK TILE
//------------------------------------------------------------------------------------------------------------------------

enum class QuickTileCategory : uint32_t
{
    GlobalIllumination = 0,
    AntiAliasing       = 1,
    FrameRateOverlay   = 2,
    Notifications      = 3,
    Quality            = 4,
    Count              = 5
};

// One entry of the 4 × 2 quick-settings grid. Slots ≥ Count are empty and draw nothing.
struct QuickTileStructure
{
    QuickTileCategory         Category;
    ControlCentreIconCategory Glyph;
    const char*               Label;      // [text] under the disc; Quality shows the tier name instead
    bool                      Cycles;     // [-]    true: each tap advances an enum; false: toggle
};

// Everything the dashboard controls, read by the project each frame and pushed into the renderer.
struct ControlCentreSettings
{
    bool             GlobalIllumination = true;
    bool             AntiAliasing       = true;
    bool             FrameRateOverlay   = false;
    bool             Notifications      = true;
    FidelityCategory Quality            = FidelityCategory::StandardFidelity;
    float            RenderScale        = 1.0f;     // [-] 0.25 … 1.0
    uint32_t         Revision           = 0u;       // [-] bumps on every change; projects compare to react
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRAWER POSE
//------------------------------------------------------------------------------------------------------------------------

enum class ControlCentreHostState : uint32_t
{
    Closed   = 0,            // shade resting at Y = 0
    Opening  = 1,            // spring travelling downward
    Open     = 2,            // shade resting at Y = DisplayHeight − 36
    Closing  = 3,            // spring travelling upward
    Dragging = 4             // pointer carries the shade or the notch
};

//------------------------------------------------------------------------------------------------------------------------
//                                                CONTROL CENTRE HOST
//------------------------------------------------------------------------------------------------------------------------

class ControlCentreHost
{
public:
    // ── Figures (all from the references; change here, nowhere else) ────────────────────────────────────────────────
    static constexpr float  NotchWidth        = 400.0f;   // [px]
    static constexpr float  NotchHeight       =  36.0f;   // [px]
    static constexpr float  TapTravelLimit    =   6.0f;   // [px]   beyond this a contact is a drag
    static constexpr double TapDurationLimit  =   0.350;  // [s]    beyond this a contact is a press
    static constexpr double DragElasticity    =   0.05;   // [-]    fraction of overshoot accepted past a bound
    static constexpr double SnapRate          =  20.0;    // [px/s] Notch release-velocity threshold
    static constexpr double SnapOffset        =  50.0;    // [px]   Notch release-offset threshold
    static constexpr double RateRetention     =   0.60;   // [-]    velocity estimate smoothing
    static constexpr float  ScrimMaxAlpha     =   0.40f;  // [-]    black over the scene when fully open

    // Dashboard figures (Notch ArcNotch.tsx, Tailwind classes resolved to pixels)
    static constexpr float  CardWidth         = 420.0f;   // [px]  max-w-[420px]
    static constexpr float  CardHeight        = 480.0f;   // [px]  h-[480px]
    static constexpr float  CardGap           =  40.0f;   // [px]  gap-10 between header, grid, pill
    static constexpr float  HeaderTextSize    =  13.0f;   // [px]  text-[13px]
    static constexpr float  HeaderIconSize    =  16.0f;   // [px]  size={16}
    static constexpr float  HeaderIconGap     =  16.0f;   // [px]  gap-4
    static constexpr float  HeaderPadX        =   8.0f;   // [px]  px-2
    static constexpr float  TileDisc          =  64.0f;   // [px]  w-16 h-16
    static constexpr float  TileGlyph         =  24.0f;   // [px]  size={24}
    static constexpr float  TileLabelGap      =  12.0f;   // [px]  gap-3
    static constexpr float  TileLabelSize     =  12.0f;   // [px]  text-[12px]
    static constexpr float  GridColumnGap     =  16.0f;   // [px]  gap-x-4
    static constexpr float  GridRowGap        =  40.0f;   // [px]  gap-y-10
    static constexpr float  PillPadding       =   8.0f;   // [px]  p-2
    static constexpr float  PillCell          =  48.0f;   // [px]  w-12 h-12
    static constexpr float  PillGlyph         =  20.0f;   // [px]  size={20}
    static constexpr float  PillTrack         =   8.0f;   // [px]  h-2
    static constexpr float  PillGapX          =  16.0f;   // [px]  gap-4
    static constexpr float  PillValueSize     =  11.0f;   // [px]  text-[11px]
    static constexpr float  PillTopMargin     =   8.0f;   // [px]  mt-2
    static constexpr float  RenderScaleMinimum=   0.25f;  // [-]

    // Settings hub / sub-page figures (Notch ArcNotch.tsx, SettingsModal.tsx, GenericSettingsModal)
    static constexpr float  PageMarginX       =  24.0f;   // [px]  sub-pages fill the canvas to this side padding…
    static constexpr float  PageMarginY       =  24.0f;   // [px]  …and this top/bottom padding (corners are the only limit)
    static constexpr float  PageRadius        =  32.0f;   // [px]  rounded-[32px]
    static constexpr float  HubTitleSize      =  22.0f;   // [px]  text-[22px] font-bold
    static constexpr float  HubBackGlyph      =  24.0f;   // [px]  ChevronLeft size 24
    static constexpr float  HubListRadius     =  24.0f;   // [px]  rounded-[24px]
    static constexpr float  HubRowPadding     =  16.0f;   // [px]  p-4
    static constexpr float  HubRowDisc        =  44.0f;   // [px]  w-11 h-11
    static constexpr float  HubRowGlyph       =  20.0f;   // [px]  size 20
    static constexpr float  HubRowGap         =  16.0f;   // [px]  gap-4
    static constexpr float  HubLabelSize      =  15.0f;   // [px]  text-[15px]
    static constexpr float  HubDescSize       =  13.0f;   // [px]  text-[13px]
    static constexpr float  HubChevron        =  18.0f;   // [px]  ChevronRight size 18, mr-2
    static constexpr float  PagePadding       =  32.0f;   // [px]  p-8
    static constexpr float  PageTitleSize     =  24.0f;   // [px]  text-2xl
    static constexpr float  PageSubtitleSize  =  14.0f;   // [px]  text-sm
    static constexpr float  PageCloseGlyph    =  16.0f;   // [px]  X size 16 in a p-2 bordered round button (32 px)
    static constexpr float  PageTabSize       =  14.0f;   // [px]  text-sm
    static constexpr float  PageTabGap        =  24.0f;   // [px]  gap-6
    static constexpr float  PageTabPadBottom  =  16.0f;   // [px]  pb-4
    static constexpr float  PageFooterPad     =  24.0f;   // [px]  p-6
    static constexpr float  PageFooterNote    =  10.0f;   // [px]  text-[10px]
    static constexpr float  PageButtonSize    =  14.0f;   // [px]  text-sm
    static constexpr float  PageButtonPadX    =  24.0f;   // [px]  px-6
    static constexpr float  PageButtonPadY    =   8.0f;   // [px]  py-2
    static constexpr float  PageButtonGap     =  12.0f;   // [px]  gap-3
    static constexpr float  PageSwapDuration  =   0.20f;  // [s]   transition duration 0.2
    static constexpr float  ThemeBlendDuration =  0.25f;  // [s]   live theme preview cross-fade (tile tap → palette morph)

    ControlCentreHost() noexcept;
    ~ControlCentreHost() noexcept = default;

    ControlCentreHost(const ControlCentreHost&)            = delete;
    ControlCentreHost& operator=(const ControlCentreHost&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool      Initialize(uint32_t DesiredWidth, uint32_t DesiredHeight) noexcept;
    void                    Terminate() noexcept;
    void                    Resize(uint32_t DesiredWidth, uint32_t DesiredHeight) noexcept;

    // ── Per-frame ─────────────────────────────────────────────────────────────────────────────────────────────────
    void                    AdvanceInteraction(const InputExchange& Input, float CursorX, float CursorY) noexcept;
    void                    AdvanceLocomotion(float DeltaSeconds) noexcept;
    void                    ConstructControlLayout(PixelSpace& Surface) noexcept;   // non-const: page widgets edit draft state

    // ── Commands ──────────────────────────────────────────────────────────────────────────────────────────────────
    void                    OpenNotch() noexcept;
    void                    CloseNotch() noexcept;
    void                    ToggleNotch() noexcept;
    void                    AssignProjectName(std::string Name) noexcept { ProjectName = std::move(Name); }

    // ── Dashboard settings ────────────────────────────────────────────────────────────────────────────────────────
    [[nodiscard]] const ControlCentreSettings& QuerySettings() const noexcept { return Settings; }
    void                    AssignSettings(const ControlCentreSettings& Desired) noexcept { Settings = Desired; ++Settings.Revision; }
    void                    ToggleTile(QuickTileCategory Tile) noexcept;             // toggles, or advances Quality
    void                    AssignRenderScale(float Scale) noexcept;
    [[nodiscard]] bool      IsTileActive(QuickTileCategory Tile) const noexcept;
    [[nodiscard]] PlaneExtent QueryCardExtent() const noexcept;                      // [px] dashboard card on the display
    [[nodiscard]] PlaneExtent QueryTileDiscExtent(uint32_t Slot) const noexcept;     // [px] disc of grid slot 0..7
    [[nodiscard]] PlaneExtent QueryPillTrackExtent() const noexcept;                 // [px] render-scale track
    [[nodiscard]] float     QueryCardOpacity() const noexcept;                       // [-]  Notch panelOpacity
    [[nodiscard]] int       QueryHoveredSlot() const noexcept { return HoveredSlot; }
    [[nodiscard]] static const QuickTileStructure& QueryTile(uint32_t Slot) noexcept;

    // ── Page navigation ───────────────────────────────────────────────────────────────────────────────────────────
    void                    NavigateToPage(ControlCentrePageCategory TargetPage) noexcept;
    void                    NavigateBack() noexcept;
    [[nodiscard]] ControlCentrePageCategory QueryActivePage()   const noexcept { return ActivePage; }
    [[nodiscard]] ControlCentrePageCategory QueryPreviousPage() const noexcept { return PreviousPage; }
    [[nodiscard]] float     QuerySlideOffset() const noexcept;
    [[nodiscard]] bool      IsSlideTransitionActive() const noexcept;
    [[nodiscard]] float     QueryPageSwapProgress() const noexcept { return PageSwapProgress; }   // 0 → 1 over 200 ms
    [[nodiscard]] PlaneExtent QueryHubRowExtent(uint32_t Row) const noexcept;                    // [px] rows 0..3
    [[nodiscard]] PlaneExtent QueryHubBackExtent() const noexcept;
    [[nodiscard]] PlaneExtent QueryHeaderGearExtent() const noexcept;
    [[nodiscard]] PlaneExtent QueryPageCloseExtent() const noexcept;
    [[nodiscard]] PlaneExtent QueryPageTabExtent(uint32_t Tab) const noexcept;                   // Appearance tabs
    [[nodiscard]] PlaneExtent QueryPageButtonExtent(bool Primary) const noexcept;               // footer Apply / Discard
    [[nodiscard]] int       QueryHoveredHubRow() const noexcept { return HoveredHubRow; }

    void                    AssignAppearanceSubTab(AppearanceSubTabCategory SubTab) noexcept { ActiveAppearanceSubTab = SubTab; }
    [[nodiscard]] AppearanceSubTabCategory QueryAppearanceSubTab() const noexcept { return ActiveAppearanceSubTab; }

    [[nodiscard]] bool      IsDialogueOpen() const noexcept { return Dialogue.IsOpen(); }
    [[nodiscard]] const DialogueHost& QueryDialogue() const noexcept { return Dialogue; }
    [[nodiscard]] const AppearanceInspector& QueryAppearance() const noexcept { return Appearance; }
    AppearanceInspector&                     AccessAppearance() noexcept { return Appearance; }
    [[nodiscard]] const InputInspector&        QueryInput()        const noexcept { return InputPage; }
    InputInspector&                            AccessInput()       noexcept { return InputPage; }
    [[nodiscard]] const NotificationInspector& QueryNotifications() const noexcept { return NotificationPage; }
    NotificationInspector&                     AccessNotifications() noexcept { return NotificationPage; }
    [[nodiscard]] PlaneExtent QueryPageResetExtent() const noexcept;                              // Input page "Reset Defaults" pill
    // Seed the dashboard record without a revision bump beyond one (start-up from persisted preferences).
    void                    SeedSettings(const ControlCentreSettings& Persisted) noexcept { const uint32_t R = Settings.Revision; Settings = Persisted; Settings.Revision = R + 1u; }
    [[nodiscard]] bool      IsPageDirty() const noexcept;                                       // active page has unapplied edits
    [[nodiscard]] float     QueryBodyScroll() const noexcept { return BodyScrollY; }
    [[nodiscard]] PlaneExtent QueryPageBodyExtent() const noexcept;

    // ── Theme ─────────────────────────────────────────────────────────────────────────────────────────────────────
    void                    SelectTheme(ThemeCategory Theme) noexcept { ActiveTheme.AssignTheme(Theme); }
    void                    AssignCornerRadius(float RadiusPixels) noexcept { ActiveTheme.AssignCornerRadius(RadiusPixels); }
    void                    SelectFontFamily(FontFamilyCategory Family) noexcept { ActiveTheme.AssignFontFamily(Family); }
    [[nodiscard]] ThemeCategory      QueryThemeCategory() const noexcept { return ActiveTheme.QueryActiveTheme(); }
    [[nodiscard]] float              QueryCornerRadius()  const noexcept { return ActiveTheme.QueryCornerRadius(); }
    [[nodiscard]] FontFamilyCategory QueryFontFamily()    const noexcept { return ActiveTheme.QueryActiveFontFamily(); }
    [[nodiscard]] uint32_t  QueryDisplayWidth()  const noexcept { return DisplayWidth; }    // logical pixels (post UI scale)
    [[nodiscard]] uint32_t  QueryDisplayHeight() const noexcept { return DisplayHeight; }
    [[nodiscard]] const ThemeStructure& QueryTheme() const noexcept { return ActiveTheme; }
    ThemeStructure&         AccessTheme() noexcept { return ActiveTheme; }

    // ── Queries ───────────────────────────────────────────────────────────────────────────────────────────────────
    [[nodiscard]] bool      IsOpen()      const noexcept { return Pose == ControlCentreHostState::Open || Pose == ControlCentreHostState::Opening; }
    [[nodiscard]] bool      IsDragging()  const noexcept { return Pose == ControlCentreHostState::Dragging; }
    [[nodiscard]] bool      IsSelected()  const noexcept { return Grabbed; }
    [[nodiscard]] bool      IsHovered()   const noexcept { return Hovered; }
    [[nodiscard]] bool      CoversPointer() const noexcept { return PointerWithheld; }   // true when the overlay owns the pointer this frame
    [[nodiscard]] ControlCentreHostState QueryPose() const noexcept { return Pose; }
    [[nodiscard]] float     QueryCurrentHeight() const noexcept;                          // [px] shade Y (0 closed … H−36 open)
    [[nodiscard]] float     QueryHandleX() const noexcept;                                // [px] notch left edge
    [[nodiscard]] float     QueryHandleY() const noexcept { return QueryCurrentHeight(); }
    [[nodiscard]] float     QueryHandleWidth()  const noexcept { return NotchWidth;  }
    [[nodiscard]] float     QueryHandleHeight() const noexcept { return NotchHeight; }
    [[nodiscard]] PlaneExtent QueryHandleExtent() const noexcept;
    [[nodiscard]] const std::vector<BezierPointIndex>& QueryHandleContour() const noexcept { return HandleContour; }

    template<typename TargetType>
    [[nodiscard]] TargetType Convert() const noexcept;

private:
    enum class GrabSubject : uint32_t { Nothing = 0, Notch = 1, Scrim = 2, Tile = 3, Pill = 4, Card = 5,
                                        Gear = 6, HubBack = 7, HubRow = 8, PageClose = 9, PageTab = 10, PageButton = 11 };

    [[nodiscard]] bool      IsSubPage(ControlCentrePageCategory Page) const noexcept
    {
        return Page != ControlCentrePageCategory::Dashboard && Page != ControlCentrePageCategory::SettingsHub;
    }
    void                    ResizeCardForPage() noexcept;
    [[nodiscard]] PlaneExtent QueryPageButtonExtentFor(uint32_t Index, bool Primary, PixelSpace& Surface) const noexcept;
    [[nodiscard]] PlaneExtent QueryPageFooterExtent() const noexcept;
    void                    ConstructPageLayout(PixelSpace& Surface, ControlCentrePageCategory Page, float Opacity, float SlideX, float Scale, bool Live) noexcept;
    void                    ConstructHubLayout(PixelSpace& Surface, float Opacity) const noexcept;
    void                    ConstructSubPageLayout(PixelSpace& Surface, ControlCentrePageCategory Page, float Opacity, bool Live) noexcept;
    void                    ConstructPageBodyLayout(PixelSpace& Surface, ControlCentrePageCategory Page, const PlaneExtent& Body, float Opacity, bool Live) noexcept;
    void                    RequestLeave(bool Back) noexcept;    // X / back / shade-close with dirty-check
    void                    ResolveDialogueVerdict() noexcept;
    void                    ConstructDashboardLayout(PixelSpace& Surface, float Opacity) const noexcept;
    void                    ConstructTileLayout(PixelSpace& Surface, uint32_t Slot, float Opacity) const noexcept;
    void                    ConstructPillLayout(PixelSpace& Surface, float Opacity) const noexcept;
    [[nodiscard]] int       SlotUnder(float CursorX, float CursorY) const noexcept;  // -1 none

    void                    GenerateHandleContour() noexcept;
    void                    Grab(GrabSubject Subject, float CursorX, float CursorY) noexcept;
    void                    Carry(float CursorX, float CursorY, float DeltaSeconds) noexcept;
    void                    Relinquish() noexcept;
    void                    Depart(bool Opening) noexcept;
    [[nodiscard]] double    OpenTravel() const noexcept { return static_cast<double>(DisplayHeight) - NotchHeight; }
    [[nodiscard]] double    NotchAdmissible() const noexcept;
    [[nodiscard]] static double Constrain(double Value, double Minimum, double Maximum, double Elasticity) noexcept;

    // ── Display ───────────────────────────────────────────────────────────────────────────────────────────────────
    uint32_t                DisplayWidth;
    uint32_t                DisplayHeight;
    uint32_t                LastResizeWidth  = 0u;          // [px] Resize() re-targets springs only when these change…
    uint32_t                LastResizeHeight = 0u;          // [px] …so per-frame calls never restart a settled spring

    // ── Motion ────────────────────────────────────────────────────────────────────────────────────────────────────
    MotionIntegrator        Motion;
    uint32_t                ShadeChannel;            // [px] Y of the shade's lower edge (= notch top)
    uint32_t                NotchChannel;            // [px] signed X travel of the notch from centre
    uint32_t                SlideChannel;            // [px] page carousel offset (later steps)

    // ── Pose ──────────────────────────────────────────────────────────────────────────────────────────────────────
    ControlCentreHostState  Pose;
    bool                    OpenBeforeGrab;          // [-] pose the grab started from, for classification

    // ── Contact ───────────────────────────────────────────────────────────────────────────────────────────────────
    GrabSubject             GrabbedSubject;
    bool                    Grabbed;
    bool                    Hovered;
    bool                    PointerWithheld;
    bool                    PreviousButton;
    bool                    AxisResolved;
    bool                    YDominant;
    bool                    TravelExceeded;
    double                  ContactDuration;         // [s]
    float                   GrabCursorX, GrabCursorY;
    double                  GrabShadeY;
    double                  GrabNotchX;
    float                   PreviousCursorX, PreviousCursorY;
    double                  RateX, RateY;            // [px/s] smoothed pointer velocity
    float                   LastDeltaSeconds;        // [s]    last locomotion step, used by the velocity estimate

    // ── Pages (state only) ────────────────────────────────────────────────────────────────────────────────────────
    ControlCentrePageCategory ActivePage;
    ControlCentrePageCategory PreviousPage;
    AppearanceSubTabCategory  ActiveAppearanceSubTab;
    DialogueHost              Dialogue;
    AppearanceInspector       Appearance;
    InputInspector            InputPage;
    NotificationInspector     NotificationPage;
    ControlCentrePageCategory PendingLeave;     // page navigation deferred behind an Unsaved-changes dialogue
    bool                      PendingLeaveBack; // true: NavigateBack, false: close shade
    float                     BodyScrollY;      // [px] sub-page body scroll
    float                     BodyContentHeight;// [px] measured last frame
    float                     WheelDelta;       // [clicks] accumulated this frame from AdvanceInteraction
    ControlPointer            Pointer;          // pointer snapshot handed to widgets during recording
    bool                      PressedInBody;
    DialoguePresetCategory    LastDialoguePreset;
    std::vector<ControlCentrePageCategory> PageHistoryStack;

    // ── Dashboard ─────────────────────────────────────────────────────────────────────────────────────────────────
    ControlCentreSettings   Settings;
    int                     HoveredSlot;             // [-] grid slot under the pointer, -1 none
    int                     GrabbedSlot;             // [-] slot the press landed on
    bool                    PillGrabbed;

    // ── Pages ─────────────────────────────────────────────────────────────────────────────────────────────────────
    uint32_t                CardWidthChannel;        // [px] spring: 420 ↔ 840
    uint32_t                CardHeightChannel;       // [px] spring: 480 ↔ 600
    float                   PageSwapProgress;        // [-]  0 → 1 over PageSwapDuration after a page change
    bool                    PageSwapForward;         // [-]  true: entering from the right (deeper), false: from the left
    int                     HoveredHubRow;           // [-]  -1 none
    int                     GrabbedHubRow;
    int                     GrabbedTab;
    bool                    GrabbedPrimaryButton;
    int                     GrabbedPageButton;       // 0 secondary · 1 primary · 2 reset (Input page)
    float                   CloseResetTimer;         // [s]  Notch resets to the dashboard 300 ms after closing
    mutable float           TabWidthCache[3];        // [px] measured tab label widths (record → hit-test)
    mutable float           ButtonWidthCache[4][2];  // [px] measured footer pill label widths per page
    mutable float           ResetWidthCache = 0.0f;  // [px] Input page "Reset Defaults" label width
    void                    ApplyActivePage()   noexcept;
    void                    DiscardActivePage() noexcept;
    void                    ResetActivePage()   noexcept;

    // ── Appearance ────────────────────────────────────────────────────────────────────────────────────────────────
    ThemeStructure          ActiveTheme;
    void                    SynchroniseTheme() noexcept;         // draft Appearance → live preview (blended) → ControlKit palette
    // Live theme preview: a tile tap re-targets the rendered palette immediately (dirty/Apply still commit it;
    //    Discard re-targets back, so the Unsaved-changes dialogue now asks about a change the user can already see).
    bool                    ThemePreviewSeeded = false;          // [-] first sync snaps instead of blending
    ThemeCategory           PushedTheme = ThemeCategory::Oled;   // [-] draft theme last pushed toward the palette
    AccentCategory          PushedAccent = AccentCategory::Blue; // [-] draft accent last pushed
    uint32_t                PushedSwatches[4] = { 0u, 0u, 0u, 0u }; // [-] draft semantic swatches last pushed
    ControlKitPalette       ThemeBlendFrom;                      // [color] palette the cross-fade started from
    ControlKitPalette       ThemeBlendTo;                        // [color] palette the cross-fade heads to
    float                   ThemeBlendT = 1.0f;                  // [-] 0 → 1 over ThemeBlendDuration; ≥1 = settled
    std::string             ProjectName;
    std::vector<BezierPointIndex> HandleContour;   // [px] outline in notch-local space (0..400 × 0..36)
    bool                    InitializedCondition;
};

template<> inline bool  ControlCentreHost::Convert<bool>()  const noexcept { return IsOpen(); }
template<> inline float ControlCentreHost::Convert<float>() const noexcept { return QueryCurrentHeight(); }
template<> inline ControlCentrePageCategory ControlCentreHost::Convert<ControlCentrePageCategory>() const noexcept { return ActivePage; }

} // namespace Frontier
