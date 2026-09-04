//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/ControlCentreHost.cpp — Top Notch Control Centre: Drawer Locomotion, Notch Travel and Overlay Recording
//============================================================================================================================================

#include "ControlCentreHost.h"
#include "FidelityClassifier.h"
#include "GlyphSpace.h"
#include "ControlKit.h"

#include <algorithm>
#include <string>
#include <cmath>
#include <cstdio>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ControlCentreHost::ControlCentreHost() noexcept
    : DisplayWidth(1920u)
    , DisplayHeight(1080u)
    , Motion{}
    , ShadeChannel(0u)
    , NotchChannel(0u)
    , SlideChannel(0u)
    , Pose(ControlCentreHostState::Closed)
    , OpenBeforeGrab(false)
    , GrabbedSubject(GrabSubject::Nothing)
    , Grabbed(false)
    , Hovered(false)
    , PointerWithheld(false)
    , PreviousButton(false)
    , AxisResolved(false)
    , YDominant(false)
    , TravelExceeded(false)
    , ContactDuration(0.0)
    , GrabCursorX(0.0f), GrabCursorY(0.0f)
    , GrabShadeY(0.0)
    , GrabNotchX(0.0)
    , PreviousCursorX(0.0f), PreviousCursorY(0.0f)
    , RateX(0.0), RateY(0.0)
    , LastDeltaSeconds(1.0f / 60.0f)
    , ActivePage(ControlCentrePageCategory::Dashboard)
    , PreviousPage(ControlCentrePageCategory::Dashboard)
    , ActiveAppearanceSubTab(AppearanceSubTabCategory::Fonts)   // Notch SettingsModal: useState<TabId>("fonts")
    , Dialogue{}
    , Appearance{}
    , PendingLeave(ControlCentrePageCategory::Dashboard)
    , PendingLeaveBack(true)
    , BodyScrollY(0.0f)
    , BodyContentHeight(0.0f)
    , WheelDelta(0.0f)
    , Pointer{}
    , PressedInBody(false)
    , LastDialoguePreset(DialoguePresetCategory::None)
    , PageHistoryStack{}
    , Settings{}
    , HoveredSlot(-1)
    , GrabbedSlot(-1)
    , PillGrabbed(false)
    , CardWidthChannel(0u)
    , CardHeightChannel(0u)
    , PageSwapProgress(1.0f)
    , PageSwapForward(true)
    , HoveredHubRow(-1)
    , GrabbedHubRow(-1)
    , GrabbedTab(-1)
    , GrabbedPrimaryButton(false)
    , GrabbedPageButton(-1)
    , CloseResetTimer(-1.0f)
    , TabWidthCache{}
    , ButtonWidthCache{}
    , ActiveTheme{}
    , ProjectName("Frontier")
    , HandleContour{}
    , InitializedCondition(false)
{
    ShadeChannel = Motion.Register(0.0);
    NotchChannel = Motion.Register(0.0);
    SlideChannel = Motion.Register(0.0);
    CardWidthChannel  = Motion.Register(static_cast<double>(CardWidth));
    CardHeightChannel = Motion.Register(static_cast<double>(CardHeight));

    // framer-motion { type: "spring", bounce: 0.15, duration: 0.5 } → ζ = 1 − 0.15 = 0.85,
    //    ω₀ = 2π / (duration · 1.2)... framer's duration-based spring solves for stiffness/damping from
    //    (duration, bounce) with mass 1: ω₀ = 2π·(1/duration)·… Reproduced with its published formula:
    //    stiffness = (2π / duration)², damping = 2ζ·√stiffness  →  k = 157.9, c = 21.4 for 0.5 s / 0.15.
    for (uint32_t Channel : { CardWidthChannel, CardHeightChannel })
    {
        SpringChannel& Card = Motion.Spring(Channel);
        Card.Stiffness = 157.9;
        Card.Damping   = 21.4;
    }

    ActiveTheme.AssignTheme(ThemeCategory::Dark);   // dark is the default per direction; palette below
}

bool ControlCentreHost::Initialize(uint32_t DesiredWidth, uint32_t DesiredHeight) noexcept
{
    DisplayWidth  = std::max(1u, DesiredWidth);
    DisplayHeight = std::max(1u, DesiredHeight);

    Motion.Spring(ShadeChannel).Place(0.0);
    Motion.Spring(NotchChannel).Place(0.0);
    Motion.Spring(SlideChannel).Place(0.0);

    Pose = ControlCentreHostState::Closed;
    GenerateHandleContour();
    InitializedCondition = true;
    return true;
}

void ControlCentreHost::Terminate() noexcept
{
    InitializedCondition = false;
    HandleContour.clear();
    PageHistoryStack.clear();
}

void ControlCentreHost::Resize(uint32_t DesiredWidth, uint32_t DesiredHeight) noexcept
{
    const bool WasOpen = (Pose == ControlCentreHostState::Open);
    DisplayWidth  = std::max(1u, DesiredWidth);
    DisplayHeight = std::max(1u, DesiredHeight);

    // Keep an open shade open at the new height and the notch inside the new admissible travel.
    if (WasOpen) Motion.Spring(ShadeChannel).Place(OpenTravel());
    if (InitializedCondition) ResizeCardForPage();   // card max-size follows the (logical) canvas
    SpringChannel& Notch = Motion.Spring(NotchChannel);
    const double Admissible = NotchAdmissible();
    Notch.Place(std::clamp(Notch.Current, -Admissible, Admissible));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PAGE NAVIGATION  (state only in this step)
//------------------------------------------------------------------------------------------------------------------------

void ControlCentreHost::NavigateToPage(ControlCentrePageCategory TargetPage) noexcept
{
    if (ActivePage == TargetPage) return;
    PageHistoryStack.push_back(ActivePage);
    PreviousPage     = ActivePage;
    ActivePage       = TargetPage;
    PageSwapForward  = true;
    PageSwapProgress = 0.0f;
    BodyScrollY      = 0.0f;
    Appearance.CloseMenus();
    Motion.Spring(SlideChannel).Place(0.0);
    ResizeCardForPage();
}

bool ControlCentreHost::IsPageDirty() const noexcept
{
    switch (ActivePage)
    {
        case ControlCentrePageCategory::Appearance:    return Appearance.IsDirty();
        case ControlCentrePageCategory::Input:         return InputPage.IsDirty();
        case ControlCentrePageCategory::Notifications: return NotificationPage.IsDirty();
        default: return false;
    }
}

void ControlCentreHost::ApplyActivePage() noexcept
{
    switch (ActivePage)
    {
        case ControlCentrePageCategory::Appearance:    Appearance.Apply(); break;
        case ControlCentrePageCategory::Input:         InputPage.Apply(); break;
        case ControlCentrePageCategory::Notifications:
            NotificationPage.Apply();
            // "Show FPS Overlay" and the dashboard FPS tile are one flag.
            if (Settings.FrameRateOverlay != NotificationPage.QueryApplied().ShowFrameRateOverlay) { Settings.FrameRateOverlay = NotificationPage.QueryApplied().ShowFrameRateOverlay; ++Settings.Revision; }
            break;
        default: break;
    }
}

void ControlCentreHost::DiscardActivePage() noexcept
{
    switch (ActivePage)
    {
        case ControlCentrePageCategory::Appearance:    Appearance.Discard(); break;
        case ControlCentrePageCategory::Input:         InputPage.Discard(); break;
        case ControlCentrePageCategory::Notifications: NotificationPage.Discard(); break;
        default: break;
    }
}

void ControlCentreHost::ResetActivePage() noexcept
{
    switch (ActivePage)
    {
        case ControlCentrePageCategory::Appearance:    Appearance.ResetDefaults(); break;
        case ControlCentrePageCategory::Input:         InputPage.ResetDefaults(); break;
        case ControlCentrePageCategory::Notifications: NotificationPage.ResetDefaults(); break;
        default: break;
    }
}

void ControlCentreHost::RequestLeave(bool Back) noexcept
{
    if (IsPageDirty())
    {
        PendingLeaveBack   = Back;
        LastDialoguePreset = DialoguePresetCategory::UnsavedChanges;
        Dialogue.Open(DialoguePresetCategory::UnsavedChanges);
        return;
    }
    if (Back) NavigateBack(); else Depart(false);
}

void ControlCentreHost::ResolveDialogueVerdict() noexcept
{
    const DialogueVerdictCategory V = Dialogue.TakeVerdict();
    if (V == DialogueVerdictCategory::Pending) return;
    switch (Dialogue.QueryActive() == DialoguePresetCategory::None ? LastDialoguePreset : Dialogue.QueryActive())
    {
        case DialoguePresetCategory::ConfirmDiscard:
            if (V == DialogueVerdictCategory::Primary) DiscardActivePage();
            break;
        case DialoguePresetCategory::ResetDefaults:
            if (V == DialogueVerdictCategory::Primary) ResetActivePage();
            break;
        case DialoguePresetCategory::UnsavedChanges:
            if (V == DialogueVerdictCategory::Primary)   { ApplyActivePage(); }
            if (V == DialogueVerdictCategory::Secondary) { DiscardActivePage(); }
            if (V != DialogueVerdictCategory::Cancel)    { if (PendingLeaveBack) NavigateBack(); else Depart(false); }
            break;
        default: break;
    }
}

void ControlCentreHost::ResizeCardForPage() noexcept
{
    // ArcNotch.tsx: animate={{ maxWidth: activeSetting ? 840 : 420, height: activeSetting ? 600 : 480 }}
    //    maxWidth is a max — the card shrinks with the container (relevant once the UI scale shrinks the logical canvas);
    //    height is clamped to the visible content box (viewport − notch 35 px − 24 px breathing room) the same way.
    const bool Sub = IsSubPage(ActivePage);
    const float MaxW = static_cast<float>(DisplayWidth)  - 32.0f;
    const float MaxH = static_cast<float>(DisplayHeight) - 35.0f - 24.0f;
    Motion.Spring(CardWidthChannel ).Depart(std::min(Sub ? PageCardWidth  : CardWidth,  MaxW));
    Motion.Spring(CardHeightChannel).Depart(std::min(Sub ? PageCardHeight : CardHeight, MaxH));
}

void ControlCentreHost::NavigateBack() noexcept
{
    if (PageHistoryStack.empty())
    {
        if (ActivePage == ControlCentrePageCategory::Dashboard) return;
        PreviousPage = ActivePage;
        ActivePage   = ControlCentrePageCategory::Dashboard;
    }
    else
    {
        PreviousPage = ActivePage;
        ActivePage   = PageHistoryStack.back();
        PageHistoryStack.pop_back();
    }
    PageSwapForward  = false;
    PageSwapProgress = 0.0f;
    ResizeCardForPage();
}

float ControlCentreHost::QuerySlideOffset() const noexcept
{
    // Signed entry offset of the active page in pixels (Notch: x 20 for the hub, 40 for sub-pages, −20 back).
    const float Remaining = 1.0f - PageSwapProgress;
    const float Entry = IsSubPage(ActivePage) ? 40.0f : 20.0f;
    return (PageSwapForward ? Entry : -Entry) * Remaining;
}

bool ControlCentreHost::IsSlideTransitionActive() const noexcept
{
    return PageSwapProgress < 1.0f
        || !Motion.Spring(CardWidthChannel).Settled || !Motion.Spring(CardHeightChannel).Settled;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 NOTCH OUTLINE TESSELLATION
//------------------------------------------------------------------------------------------------------------------------
// SVG (ArcNotch.tsx):  M 0 0  C 15 0, 20 6, 25 15  L 35 28  C 40 34, 45 36, 52 36  L 348 36
//                      C 355 36, 360 34, 365 28  L 375 15  C 380 6, 385 0, 400 0  Z

namespace {

BezierPointIndex SampleCubic(BezierPointIndex P0, BezierPointIndex P1, BezierPointIndex P2, BezierPointIndex P3, float T) noexcept
{
    const float U  = 1.0f - T;
    const float B0 = U * U * U;
    const float B1 = 3.0f * U * U * T;
    const float B2 = 3.0f * U * T * T;
    const float B3 = T * T * T;
    return BezierPointIndex{ B0 * P0.X + B1 * P1.X + B2 * P2.X + B3 * P3.X,
                             B0 * P0.Y + B1 * P1.Y + B2 * P2.Y + B3 * P3.Y };
}

void AppendCubic(std::vector<BezierPointIndex>& Out, BezierPointIndex P0, BezierPointIndex P1, BezierPointIndex P2, BezierPointIndex P3) noexcept
{
    constexpr int Segments = 16;
    for (int Step = 1; Step <= Segments; ++Step)
        Out.push_back(SampleCubic(P0, P1, P2, P3, static_cast<float>(Step) / static_cast<float>(Segments)));
}

} // namespace

void ControlCentreHost::GenerateHandleContour() noexcept
{
    HandleContour.clear();
    HandleContour.reserve(80);

    HandleContour.push_back({ 0.0f, 0.0f });                                                       // M 0 0
    AppendCubic(HandleContour, { 0.0f, 0.0f },   { 15.0f, 0.0f },  { 20.0f, 6.0f },   { 25.0f, 15.0f });  // C
    HandleContour.push_back({ 35.0f, 28.0f });                                                     // L 35 28
    AppendCubic(HandleContour, { 35.0f, 28.0f }, { 40.0f, 34.0f }, { 45.0f, 36.0f },  { 52.0f, 36.0f });  // C
    HandleContour.push_back({ 348.0f, 36.0f });                                                    // L 348 36
    AppendCubic(HandleContour, { 348.0f, 36.0f },{ 355.0f, 36.0f },{ 360.0f, 34.0f }, { 365.0f, 28.0f }); // C
    HandleContour.push_back({ 375.0f, 15.0f });                                                    // L 375 15
    AppendCubic(HandleContour, { 375.0f, 15.0f },{ 380.0f, 6.0f }, { 385.0f, 0.0f },  { 400.0f, 0.0f });  // C
    // Z — the polygon filler closes back to (0, 0)
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

float ControlCentreHost::QueryCurrentHeight() const noexcept
{
    return static_cast<float>(Motion.Spring(ShadeChannel).Current);
}

float ControlCentreHost::QueryHandleX() const noexcept
{
    return (static_cast<float>(DisplayWidth) - NotchWidth) * 0.5f + static_cast<float>(Motion.Spring(NotchChannel).Current);
}

PlaneExtent ControlCentreHost::QueryHandleExtent() const noexcept
{
    return Spanning(QueryHandleX(), QueryCurrentHeight(), NotchWidth, NotchHeight);
}

double ControlCentreHost::NotchAdmissible() const noexcept
{
    const double Limit = (static_cast<double>(DisplayWidth) - NotchWidth) * 0.5;
    return Limit > 0.0 ? Limit : 0.0;
}

double ControlCentreHost::Constrain(double Value, double Minimum, double Maximum, double Elasticity) noexcept
{
    if (Value < Minimum) return Minimum - (Minimum - Value) * Elasticity;
    if (Value > Maximum) return Maximum + (Value - Maximum) * Elasticity;
    return Value;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       COMMANDS
//------------------------------------------------------------------------------------------------------------------------

void ControlCentreHost::Depart(bool Opening) noexcept
{
    Motion.Spring(ShadeChannel).Depart(Opening ? OpenTravel() : 0.0);
    Pose = Opening ? ControlCentreHostState::Opening : ControlCentreHostState::Closing;

    // ArcNotch.tsx closePanel / handleDragEnd: setTimeout(() => { setShowSettings(false); setActiveSetting(null) }, 300)
    CloseResetTimer = Opening ? -1.0f : 0.3f;
}

void ControlCentreHost::OpenNotch()  noexcept { Depart(true);  }
void ControlCentreHost::CloseNotch() noexcept { Depart(false); }
void ControlCentreHost::ToggleNotch() noexcept { Depart(!IsOpen()); }

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERACTION
//------------------------------------------------------------------------------------------------------------------------

void ControlCentreHost::Grab(GrabSubject Subject, float CursorX, float CursorY) noexcept
{
    GrabbedSubject  = Subject;
    Grabbed         = true;
    AxisResolved    = false;
    YDominant       = false;
    TravelExceeded  = false;
    ContactDuration = 0.0;
    GrabCursorX     = CursorX;
    GrabCursorY     = CursorY;
    GrabShadeY      = Motion.Spring(ShadeChannel).Current;
    GrabNotchX      = Motion.Spring(NotchChannel).Current;
    OpenBeforeGrab  = IsOpen();
    RateX = RateY   = 0.0;

    if (Subject == GrabSubject::Notch)
    {
        // Pin both springs where they stand; the pointer owns them until release.
        Motion.Spring(ShadeChannel).Place(GrabShadeY);
        Motion.Spring(NotchChannel).Place(GrabNotchX);
        Pose = ControlCentreHostState::Dragging;
    }
    else if (Subject == GrabSubject::Card)
    {
        // Card drags carry the shade vertically (Android-sheet dismiss); the notch stays pinned.
        Motion.Spring(ShadeChannel).Place(GrabShadeY);
        Pose = ControlCentreHostState::Dragging;
    }
}

void ControlCentreHost::Carry(float CursorX, float CursorY, float DeltaSeconds) noexcept
{
    // Pointer velocity estimate (px/s), smoothed with 60 % retention of the previous tick's estimate.
    if (DeltaSeconds > 0.0f)
    {
        const double InstantX = static_cast<double>(CursorX - PreviousCursorX) / DeltaSeconds;
        const double InstantY = static_cast<double>(CursorY - PreviousCursorY) / DeltaSeconds;
        RateX = RateRetention * RateX + (1.0 - RateRetention) * InstantX;
        RateY = RateRetention * RateY + (1.0 - RateRetention) * InstantY;
    }

    const double TravelX = static_cast<double>(CursorX - GrabCursorX);
    const double TravelY = static_cast<double>(CursorY - GrabCursorY);

    if (GrabbedSubject == GrabSubject::Pill)
    {
        // The track is a plain slider: position along it → render scale, live while held.
        const PlaneExtent Track = QueryPillTrackExtent();
        const float T = Track.Width() > 0.0f ? (CursorX - Track.MinimumX) / Track.Width() : 1.0f;
        AssignRenderScale(RenderScaleMinimum + (1.0f - RenderScaleMinimum) * std::clamp(T, 0.0f, 1.0f));
        return;
    }

    const bool CardCarry = GrabbedSubject == GrabSubject::Card;
    if (GrabbedSubject != GrabSubject::Notch && !CardCarry)
    {
        // Tiles only care whether the press stayed put (tap) or wandered (cancel).
        if (!TravelExceeded && (std::fabs(TravelX) > TapTravelLimit || std::fabs(TravelY) > TapTravelLimit))
            TravelExceeded = true;
        return;
    }

    if (!TravelExceeded && (std::fabs(TravelX) > TapTravelLimit || std::fabs(TravelY) > TapTravelLimit))
        TravelExceeded = true;

    // The axis is decided ONCE, on the first travel that clears the tap ceiling, by the larger displacement.
    //    Deciding per tick makes a diagonal drag alternate between sliding the notch and opening the shade.
    if (!AxisResolved && TravelExceeded)
    {
        YDominant    = std::fabs(TravelY) > std::fabs(TravelX);
        AxisResolved = true;
    }

    if (!AxisResolved) return;
    if (CardCarry && !YDominant) return;   // the card ignores horizontal drags; the notch stays where it is

    if (YDominant)
    {
        const double Dragged = GrabShadeY + TravelY;
        Motion.Spring(ShadeChannel).Place(Constrain(Dragged, 0.0, OpenTravel(), DragElasticity));
    }
    else
    {
        const double Admissible = NotchAdmissible();
        const double Dragged    = GrabNotchX + TravelX;
        Motion.Spring(NotchChannel).Place(Constrain(Dragged, -Admissible, Admissible, DragElasticity));
    }
}

void ControlCentreHost::Relinquish() noexcept
{
    if (!Grabbed) return;

    const bool Tap = !TravelExceeded && ContactDuration <= TapDurationLimit;

    if (GrabbedSubject == GrabSubject::Scrim)
    {
        // A tap on the scrim while open closes the shade. A drag that started on the scrim does nothing.
        if (Tap && IsOpen()) RequestLeave(false);
    }
    else if (GrabbedSubject == GrabSubject::Tile)
    {
        // A tile fires on release, only if the pointer is still over the disc it was pressed on.
        if (Tap && GrabbedSlot >= 0 && SlotUnder(PreviousCursorX, PreviousCursorY) == GrabbedSlot
            && static_cast<uint32_t>(GrabbedSlot) < static_cast<uint32_t>(QuickTileCategory::Count))
            ToggleTile(static_cast<QuickTileCategory>(GrabbedSlot));
    }
    else if (GrabbedSubject == GrabSubject::Gear)
    {
        // ArcNotch.tsx: <Settings size={16}/> onClick → setShowSettings(true)
        if (Tap && QueryHeaderGearExtent().Encloses(PreviousCursorX, PreviousCursorY))
            NavigateToPage(ControlCentrePageCategory::SettingsHub);
    }
    else if (GrabbedSubject == GrabSubject::HubBack)
    {
        // ChevronLeft onClick → setShowSettings(false)
        if (Tap && QueryHubBackExtent().Encloses(PreviousCursorX, PreviousCursorY)) NavigateBack();
    }
    else if (GrabbedSubject == GrabSubject::PageClose)
    {
        // X onClose → setActiveSetting(null): back to the hub
        if (Tap && QueryPageCloseExtent().Encloses(PreviousCursorX, PreviousCursorY)) RequestLeave(true);
    }
    else if (GrabbedSubject == GrabSubject::PageTab)
    {
        if (Tap && GrabbedTab >= 0 && QueryPageTabExtent(static_cast<uint32_t>(GrabbedTab)).Encloses(PreviousCursorX, PreviousCursorY))
        {
            if (ActiveAppearanceSubTab != static_cast<AppearanceSubTabCategory>(GrabbedTab)) { BodyScrollY = 0.0f; Appearance.CloseMenus(); }
            ActiveAppearanceSubTab = static_cast<AppearanceSubTabCategory>(GrabbedTab);
        }
    }
    else if (GrabbedSubject == GrabSubject::PageButton)
    {
        // Footer pills. Apply commits the page draft; Discard asks (ConfirmDiscard); Reset Defaults asks
        //    (ResetDefaults). Render Settings has no draft (dashboard tiles apply live) so its pills are inert.
        const PlaneExtent Target = GrabbedPageButton == 2 ? QueryPageResetExtent() : QueryPageButtonExtent(GrabbedPrimaryButton);
        if (Tap && Target.Encloses(PreviousCursorX, PreviousCursorY))
        {
            if (GrabbedPageButton == 2)
            {
                if (ActivePage == ControlCentrePageCategory::Input && !InputPage.IsDefault())
                { LastDialoguePreset = DialoguePresetCategory::ResetDefaults; Dialogue.Open(DialoguePresetCategory::ResetDefaults); }
            }
            else if (GrabbedPrimaryButton) { if (IsPageDirty()) ApplyActivePage(); }
            else if (IsPageDirty()) { LastDialoguePreset = DialoguePresetCategory::ConfirmDiscard; Dialogue.Open(DialoguePresetCategory::ConfirmDiscard); }
        }
    }
    else if (GrabbedSubject == GrabSubject::Notch || GrabbedSubject == GrabSubject::Card)
    {
        if (Tap)
        {
            // A notch that cannot be tapped is a notch the user reports as dead. Card taps stay swallowed.
            if (GrabbedSubject == GrabSubject::Notch) { if (OpenBeforeGrab) RequestLeave(false); else Depart(true); }
        }
        else if (!YDominant && GrabbedSubject == GrabSubject::Notch)
        {
            // Horizontal slide: settle inside the admissible band (undo the elastic overshoot).
            SpringChannel& Notch = Motion.Spring(NotchChannel);
            const double Admissible = NotchAdmissible();
            Notch.Depart(std::clamp(Notch.Current, -Admissible, Admissible));
            Pose = OpenBeforeGrab ? ControlCentreHostState::Open : ControlCentreHostState::Closed;
        }
        else
        {
            // Vertical carry — Notch ArcNotch.tsx handleDragEnd, verbatim:
            //   y > H/2 : close only if velocity < −20 px/s or offset < −50 px, else open
            //   y < H/2 : open  only if velocity >  20 px/s or offset >  50 px, else close
            const double ShadeY       = Motion.Spring(ShadeChannel).Current;
            const double Displacement = ShadeY - GrabShadeY;

            bool Opening;
            if (ShadeY > static_cast<double>(DisplayHeight) * 0.5)
                Opening = !(RateY < -SnapRate || Displacement < -SnapOffset);
            else
                Opening =  (RateY >  SnapRate || Displacement >  SnapOffset);

            Depart(Opening);

            // The release's own rate is injected rather than discarded: a spring departing from rest
            //    arrives visibly later than the flick that asked for it.
            Motion.Spring(ShadeChannel).Rate = RateY;
        }
    }

    Grabbed        = false;
    GrabbedSubject = GrabSubject::Nothing;
    GrabbedSlot    = -1;
    GrabbedHubRow  = -1;
    GrabbedTab     = -1;
    PillGrabbed    = false;
}

void ControlCentreHost::AdvanceInteraction(const InputExchange& Input, float CursorX, float CursorY) noexcept
{
    if (!InitializedCondition) return;

    const bool Button = Input.IsMouseButtonPressed(MouseButtonCategory::ButtonLeft);
    const bool Pressed  =  Button && !PreviousButton;
    const bool Released = !Button &&  PreviousButton;
    PreviousButton = Button;

    const PlaneExtent Handle = QueryHandleExtent();
    Hovered = Handle.Encloses(CursorX, CursorY);

    // Shade content extent: everything above the notch's top edge.
    const bool OverShade = CursorY < QueryCurrentHeight();
    const bool ShadeOpenish = QueryCurrentHeight() > 50.0f;   // Notch: scrim intercepts pointer once y > 50

    // Dashboard hit-testing: Notch enables pointer events on the panel once y > 100.
    //    A page in mid-swap (AnimatePresence mode="wait") is not interactive until it has fully entered.
    const bool CardActive = QueryCurrentHeight() > 100.0f;
    const bool PageSettled = PageSwapProgress >= 1.0f && !Dialogue.IsVisible();
    WheelDelta = Input.QueryMouseScrollDelta();

    // Pointer snapshot for the widget layer (recorded this frame, consumed during ConstructControlLayout).
    Pointer.X = CursorX; Pointer.Y = CursorY;
    Pointer.Down = Button; Pointer.Pressed = Pressed; Pointer.Released = Released;
    Pointer.Enabled = CardActive && PageSwapProgress >= 1.0f;

    // Body widgets: a press inside the scrollable body is owned by the page until release (sliders drag out of it).
    const PlaneExtent Body = QueryPageBodyExtent();
    const bool OverBody = CardActive && PageSettled && IsSubPage(ActivePage) && Body.Encloses(CursorX, CursorY);
    if (Pressed && OverBody) PressedInBody = true;
    if (Released) { /* cleared after this frame's recording — see AdvanceLocomotion */ }
    const bool BodyOwned = PressedInBody || Appearance.HasOpenMenu() || InputPage.HasOpenMenu() || Dialogue.IsVisible();
    const bool OnDashboard = ActivePage == ControlCentrePageCategory::Dashboard;
    const bool OnHub       = ActivePage == ControlCentrePageCategory::SettingsHub;
    const bool OnSubPage   = IsSubPage(ActivePage);

    HoveredSlot   = (CardActive && PageSettled && OnDashboard) ? SlotUnder(CursorX, CursorY) : -1;
    HoveredHubRow = -1;
    if (CardActive && PageSettled && OnHub)
        for (uint32_t Row = 0u; Row < 4u; ++Row)
            if (QueryHubRowExtent(Row).Encloses(CursorX, CursorY)) HoveredHubRow = static_cast<int>(Row);

    const bool OverCard = CardActive && QueryCardExtent().Encloses(CursorX, CursorY);
    const bool OverGear = CardActive && PageSettled && OnDashboard && QueryHeaderGearExtent().Encloses(CursorX, CursorY);
    const bool OverBack = CardActive && PageSettled && OnHub       && QueryHubBackExtent().Encloses(CursorX, CursorY);
    const bool OverClose= CardActive && PageSettled && OnSubPage   && QueryPageCloseExtent().Encloses(CursorX, CursorY);
    int OverTab = -1;
    if (CardActive && PageSettled && ActivePage == ControlCentrePageCategory::Appearance)
        for (uint32_t Tab = 0u; Tab < static_cast<uint32_t>(AppearanceSubTabCategory::Count); ++Tab)
            if (QueryPageTabExtent(Tab).Encloses(CursorX, CursorY)) OverTab = static_cast<int>(Tab);
    int OverButton = -1;   // 0 secondary (Discard), 1 primary (Apply), 2 Reset Defaults (Input page)
    if (CardActive && PageSettled && OnSubPage && !BodyOwned)
    {
        const bool Enabled = ActivePage == ControlCentrePageCategory::RenderSettings || IsPageDirty();
        if (Enabled && QueryPageButtonExtent(true ).Encloses(CursorX, CursorY)) OverButton = 1;
        if (Enabled && QueryPageButtonExtent(false).Encloses(CursorX, CursorY)) OverButton = 0;
        if (ActivePage == ControlCentrePageCategory::Input && !InputPage.IsDefault() && QueryPageResetExtent().Encloses(CursorX, CursorY)) OverButton = 2;
    }
    const bool OverPill = CardActive && PageSettled && OnDashboard && [&]
    {
        PlaneExtent Track = QueryPillTrackExtent();
        Track.MinimumY -= (PillCell - PillTrack) * 0.5f;   // the whole 48 px pill row is grabbable
        Track.MaximumY += (PillCell - PillTrack) * 0.5f;
        return Track.Encloses(CursorX, CursorY);
    }();

    if (Pressed && !BodyOwned && !OverBody)
    {
        if (Hovered)                       Grab(GrabSubject::Notch, CursorX, CursorY);
        else if (OverGear)                 Grab(GrabSubject::Gear, CursorX, CursorY);
        else if (OverBack)                 Grab(GrabSubject::HubBack, CursorX, CursorY);
        else if (OverClose)                Grab(GrabSubject::PageClose, CursorX, CursorY);
        else if (OverTab >= 0)             { Grab(GrabSubject::PageTab, CursorX, CursorY); GrabbedTab = OverTab; }
        else if (OverButton >= 0)          { Grab(GrabSubject::PageButton, CursorX, CursorY); GrabbedPrimaryButton = OverButton == 1; GrabbedPageButton = OverButton; }
        else if (HoveredHubRow >= 0)
        {
            // ArcNotch.tsx hub rows fire on onPointerDown, not on release.
            Grab(GrabSubject::HubRow, CursorX, CursorY);
            GrabbedHubRow = HoveredHubRow;
            NavigateToPage(static_cast<ControlCentrePageCategory>(
                static_cast<uint32_t>(ControlCentrePageCategory::RenderSettings) + static_cast<uint32_t>(HoveredHubRow)));
        }
        else if (HoveredSlot >= 0)         { Grab(GrabSubject::Tile, CursorX, CursorY); GrabbedSlot = HoveredSlot; }
        else if (OverPill)                 { Grab(GrabSubject::Pill, CursorX, CursorY); PillGrabbed = true; Carry(CursorX, CursorY, LastDeltaSeconds); }
        else if (OverCard)                 Grab(GrabSubject::Card, CursorX, CursorY);   // swallow; card is not a scrim
        else if (ShadeOpenish && !OverShade) Grab(GrabSubject::Scrim, CursorX, CursorY);
    }

    if (Grabbed && Button)
        Carry(CursorX, CursorY, LastDeltaSeconds);

    if (Released)
        Relinquish();

    // Wheel scrolls the sub-page body.
    if (OverBody && WheelDelta != 0.0f && !Dialogue.IsVisible())
    {
        const float Room = std::max(BodyContentHeight - Body.Height(), 0.0f);
        BodyScrollY = std::clamp(BodyScrollY - WheelDelta * 48.0f, 0.0f, Room);
    }

    // The overlay owns the pointer while it is over the notch, over the pulled-down shade, or during a grab —
    //    hosts use this to keep the pointer away from the scene camera / project panels.
    PointerWithheld = Hovered || Grabbed || ShadeOpenish;

    PreviousCursorX = CursorX;
    PreviousCursorY = CursorY;
}

void ControlCentreHost::AdvanceLocomotion(float DeltaSeconds) noexcept
{
    if (!InitializedCondition || DeltaSeconds <= 0.0f) return;

    LastDeltaSeconds = DeltaSeconds;
    if (Grabbed) ContactDuration += DeltaSeconds;

    Motion.Advance(static_cast<double>(DeltaSeconds));
    Appearance.AdvanceFontsTab(DeltaSeconds);
    Dialogue.Advance(DeltaSeconds);
    ResolveDialogueVerdict();
    if (Dialogue.QueryActive() != DialoguePresetCategory::None) LastDialoguePreset = Dialogue.QueryActive();
    if (!Pointer.Down) PressedInBody = false;

    // Page swap: linear 200 ms tween (framer-motion default easing for opacity/x/scale is easeOut; approximated
    //    by the cubic below — deviation noted in the step report).
    if (PageSwapProgress < 1.0f)
        PageSwapProgress = std::min(1.0f, PageSwapProgress + DeltaSeconds / PageSwapDuration);

    // Deferred reset after close (300 ms): back to the dashboard with the card at rest size.
    if (CloseResetTimer >= 0.0f)
    {
        CloseResetTimer -= DeltaSeconds;
        if (CloseResetTimer < 0.0f)
        {
            CloseResetTimer = -1.0f;
            if (ActivePage != ControlCentrePageCategory::Dashboard)
            {
                PageHistoryStack.clear();
                PreviousPage     = ActivePage;
                ActivePage       = ControlCentrePageCategory::Dashboard;
                PageSwapProgress = 1.0f;   // the shade is shut; nothing to animate
                Motion.Spring(CardWidthChannel ).Place(CardWidth);
                Motion.Spring(CardHeightChannel).Place(CardHeight);
            }
        }
    }

    // Settle the pose once the shade spring comes to rest.
    if (Pose == ControlCentreHostState::Opening && Motion.Spring(ShadeChannel).Settled) Pose = ControlCentreHostState::Open;
    if (Pose == ControlCentreHostState::Closing && Motion.Spring(ShadeChannel).Settled) Pose = ControlCentreHostState::Closed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RECORDING
//------------------------------------------------------------------------------------------------------------------------

void ControlCentreHost::ConstructControlLayout(PixelSpace& Surface) noexcept
{
    if (!InitializedCondition || !Surface.IsRecording()) return;
    SynchroniseTheme();

    const float W      = static_cast<float>(DisplayWidth);
    const float H      = static_cast<float>(DisplayHeight);
    const float ShadeY = QueryCurrentHeight();                    // lower edge of the sheet / top of the notch
    const float NotchX = QueryHandleX();

    // Chrome renders in the APPLIED typeface (Body role); nullptr → backend default until the registry is installed.
    Surface.PushTypeface(Appearance.QueryAppliedFace(3u));
    struct TypefaceScope { PixelSpace& S; ~TypefaceScope() { S.PopTypeface(); } } TypefaceGuard{ Surface };

    // ArcNotch.tsx hard-codes the sheet and notch to #0A0A0B and the caption to text-white/50. Per direction the theme
    //    applies globally, so the sheet follows the theme's mainBg (OLED #000000 / Dark #111111 — within 7 levels of the
    //    reference on the two dark themes) and the caption follows colors.textMuted. Flagged as a deliberate deviation.
    const ColorQuad Sheet = ControlKit::Palette().Field;
    const ColorQuad Label = ControlKit::Palette().TextDim;

    // ① Scrim: black, opacity 0 → 0.4 across the travel (Notch: useTransform(y, [0, H], [0, 0.4]) on bg-black/50).
    const float Travel = static_cast<float>(OpenTravel());
    const float Progress = Travel > 0.0f ? std::clamp(ShadeY / Travel, 0.0f, 1.0f) : 0.0f;
    if (Progress > 0.0f)
        Surface.FillRectangle(Spanning(0.0f, 0.0f, W, H), ColorQuad{ 0.0f, 0.0f, 0.0f, ScrimMaxAlpha * Progress });

    // ② Shade sheet: full width, from far above the top edge down to the notch's top.
    if (ShadeY > 0.0f)
        Surface.FillRectangle(PlaneExtent{ 0.0f, -2000.0f, W, ShadeY }, Sheet);

    // ②b Dashboard card inside the sheet, faded per Notch's panelOpacity = useTransform(y, [100, H/2], [0, 1]).
    const float CardOpacity = QueryCardOpacity();
    if (CardOpacity > 0.0f)
    {
        // AnimatePresence mode="wait": the outgoing page finishes its 200 ms exit before the incoming page
        //    starts its 200 ms entry. Both halves run inside PageSwapProgress ∈ [0, 1].
        const float P = PageSwapProgress;
        auto Eased = [](float T) { const float U = 1.0f - T; return 1.0f - U * U * U; };   // easeOut cubic
        if (P < 0.5f)
        {
            const float T = Eased(P * 2.0f);                              // 0 → 1 across the exit half
            const float Exit = (PageSwapForward || !IsSubPage(PreviousPage)) ? -20.0f : 40.0f;
            //   forward: outgoing exits x −20 (dashboard & hub both exit −20); back from a sub-page: exits x +40
            ConstructPageLayout(Surface, PreviousPage, CardOpacity * (1.0f - T), Exit * T, 1.0f - 0.05f * T, false);
        }
        else
        {
            const float T = Eased((P - 0.5f) * 2.0f);                     // 0 → 1 across the entry half
            const float Entry = PageSwapForward ? (IsSubPage(ActivePage) ? 40.0f : 20.0f) : -20.0f;
            ConstructPageLayout(Surface, ActivePage, CardOpacity * T, Entry * (1.0f - T), 0.95f + 0.05f * T, P >= 1.0f);
        }
    }

    Pointer.Pressed = Pointer.Released = false;   // edges consumed by this frame's widgets

    // ③ Notch handle: tessellated SVG outline translated to (NotchX, ShadeY).
    std::vector<PlanePoint> Outline;
    Outline.reserve(HandleContour.size());
    for (const BezierPointIndex& P : HandleContour)
        Outline.push_back(PlanePoint{ NotchX + P.X, ShadeY + P.Y });
    Surface.FillPolygon(Outline.data(), static_cast<uint32_t>(Outline.size()), Sheet);

    // ④ Project name centred in the handle (Notch: 13 px, font-medium, text-white/50, pb-1 → 4 px lift).
    constexpr float LabelSize = 13.0f;
    const PlanePoint Measured = Surface.MeasureText(ProjectName.c_str(), LabelSize);
    const float TextX = NotchX + (NotchWidth  - Measured.X) * 0.5f;
    const float TextY = ShadeY + (NotchHeight - Measured.Y) * 0.5f - 2.0f;
    Surface.Text(TextX, TextY, Label, ProjectName.c_str(), LabelSize);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       DASHBOARD
//------------------------------------------------------------------------------------------------------------------------

namespace {

constexpr QuickTileStructure TileTable[static_cast<size_t>(QuickTileCategory::Count)] =
{
    { QuickTileCategory::GlobalIllumination, ControlCentreIconCategory::SunIllumination,      "Global Illumination", false },
    { QuickTileCategory::AntiAliasing,       ControlCentreIconCategory::SparklesAntiAliasing, "Anti-Aliasing",       false },
    { QuickTileCategory::FrameRateOverlay,   ControlCentreIconCategory::GaugeFrameRate,       "FPS Overlay",         false },
    { QuickTileCategory::Notifications,      ControlCentreIconCategory::NotificationsBell,    "Notifications",       false },
    { QuickTileCategory::Quality,            ControlCentreIconCategory::SlidersQuality,       "Quality",             true  },
};

constexpr uint32_t GridColumns = 4u;
constexpr uint32_t GridSlots   = 8u;

// ArcNotch.tsx colours, re-derived from the applied theme every frame (ControlKit::Palette()). With the default Dark
//    theme these evaluate to the Tailwind values the reference hard-codes (bg-blue-500 → accent, bg-[#1C1C1E] → CardSub,
//    text-white/70|50 → TextMain/TextMuted); other themes recolour the whole shade.
inline ColorQuad Alpha(ColorQuad C, float A) noexcept { C.Alpha = A; return C; }
inline ColorQuad TileActive()      noexcept { return ControlKit::Palette().Accent; }                                             // bg-blue-500 → accentColor
inline ColorQuad TileActiveHover() noexcept { ColorQuad C = ControlKit::Palette().Accent; C.Red = C.Red * 0.85f + 0.15f; C.Green = C.Green * 0.85f + 0.15f; C.Blue = C.Blue * 0.85f + 0.15f; return C; }   // hover:bg-blue-400 (lighter step)
inline ColorQuad TileIdle()        noexcept { return ControlKit::Palette().CardSub; }                                            // bg-[#1C1C1E]
inline ColorQuad TileIdleHover()   noexcept { return ControlKit::Palette().Selected; }                                           // hover:bg-[#2C2C2E]
inline ColorQuad InkFull()         noexcept { return Alpha(ControlKit::Palette().Text, 1.0f); }                                  // text-white
inline ColorQuad Ink70()           noexcept { return Alpha(ControlKit::Palette().Text, 0.70f); }                                 // text-white/70
inline ColorQuad Ink50()           noexcept { return ControlKit::Palette().TextDim; }                                            // text-white/50 (colors.textMuted)
inline ColorQuad TrackBlack50()    noexcept { return ColorQuad{ 0.0f, 0.0f, 0.0f, 0.50f }; }                                    // bg-black/50
inline ColorQuad TrackFill()       noexcept { return Alpha(ControlKit::Palette().Accent, 0.90f); }                               // bg-indigo-500/90 → accent/90

constexpr ColorQuad Faded(ColorQuad C, float Opacity) noexcept { C.Alpha *= Opacity; return C; }

} // namespace

const QuickTileStructure& ControlCentreHost::QueryTile(uint32_t Slot) noexcept
{
    static constexpr QuickTileStructure Empty{ QuickTileCategory::Count, ControlCentreIconCategory::Count, "", false };
    return Slot < static_cast<uint32_t>(QuickTileCategory::Count) ? TileTable[Slot] : Empty;
}

void ControlCentreHost::SynchroniseTheme() noexcept
{
    // Live preview: the rendered palette follows the DRAFT (tile taps re-target immediately with a cross-fade);
    //    Apply commits what is already showing, Discard re-targets back — so the Unsaved-changes dialogue now asks
    //    about a change the user can already see.
    const AppearanceSettings& D = Appearance.QueryDraft();
    const bool Retargeted = !ThemePreviewSeeded || D.Theme != PushedTheme || D.Accent != PushedAccent
        || D.WarningSwatch != PushedSwatches[0u] || D.SuccessSwatch != PushedSwatches[1u]
        || D.InfoSwatch != PushedSwatches[2u] || D.CautionSwatch != PushedSwatches[3u];
    if (Retargeted)
    {
        // Non-colour state applies instantly; colours cross-fade so the change reads as a morph, not a snap.
        ActiveTheme.AssignTheme(D.Theme);
        ActiveTheme.AssignAccent(D.Accent);
        ActiveTheme.AssignCornerRadius(D.CornerRadius);
        const ControlKitPalette Saved = ControlKit::Palette();
        ControlKit::AssignTheme(ActiveTheme,
                                AppearanceInspector::QuerySemanticColour(0u, D.WarningSwatch),
                                AppearanceInspector::QuerySemanticColour(1u, D.SuccessSwatch),
                                AppearanceInspector::QuerySemanticColour(2u, D.InfoSwatch),
                                AppearanceInspector::QuerySemanticColour(3u, D.CautionSwatch));
        ThemeBlendTo = ControlKit::Palette();
        ControlKit::AssignPalette(Saved);
        PushedTheme = D.Theme; PushedAccent = D.Accent;
        PushedSwatches[0u] = D.WarningSwatch; PushedSwatches[1u] = D.SuccessSwatch;
        PushedSwatches[2u] = D.InfoSwatch;    PushedSwatches[3u] = D.CautionSwatch;
        if (!ThemePreviewSeeded)
        {
            // First frame: snap to the canonical palette (parity with the old instant push), blend after that.
            ThemePreviewSeeded = true;
            ThemeBlendT = 1.0f;
            ControlKit::AssignPalette(ThemeBlendTo);
            return;
        }
        ThemeBlendFrom = Saved;
        ThemeBlendT = 0.0f;
    }
    if (ThemeBlendT < 1.0f)
    {
        ThemeBlendT = std::min(ThemeBlendT + LastDeltaSeconds / ThemeBlendDuration, 1.0f);
        if (ThemeBlendT >= 1.0f) ControlKit::AssignPalette(ThemeBlendTo);
        else ControlKit::BlendPalette(ThemeBlendFrom, ThemeBlendTo, ThemeBlendT);
    }
}

bool ControlCentreHost::IsTileActive(QuickTileCategory Tile) const noexcept
{
    switch (Tile)
    {
        case QuickTileCategory::GlobalIllumination: return Settings.GlobalIllumination;
        case QuickTileCategory::AntiAliasing:       return Settings.AntiAliasing;
        case QuickTileCategory::FrameRateOverlay:   return Settings.FrameRateOverlay;
        case QuickTileCategory::Notifications:      return Settings.Notifications;
        case QuickTileCategory::Quality:            return true;   // a cycler is always "lit"; its label carries the state
        default:                                    return false;
    }
}

void ControlCentreHost::ToggleTile(QuickTileCategory Tile) noexcept
{
    switch (Tile)
    {
        case QuickTileCategory::GlobalIllumination: Settings.GlobalIllumination = !Settings.GlobalIllumination; break;
        case QuickTileCategory::AntiAliasing:       Settings.AntiAliasing       = !Settings.AntiAliasing;       break;
        case QuickTileCategory::FrameRateOverlay:   Settings.FrameRateOverlay   = !Settings.FrameRateOverlay; NotificationPage.MirrorFrameRateOverlay(Settings.FrameRateOverlay); break;
        case QuickTileCategory::Notifications:      Settings.Notifications      = !Settings.Notifications;      break;
        case QuickTileCategory::Quality:            Settings.Quality            = NextFidelity(Settings.Quality); break;
        default: return;
    }
    ++Settings.Revision;
}

void ControlCentreHost::AssignRenderScale(float Scale) noexcept
{
    const float Clamped = std::clamp(Scale, RenderScaleMinimum, 1.0f);
    if (Clamped == Settings.RenderScale) return;
    Settings.RenderScale = Clamped;
    ++Settings.Revision;
}

float ControlCentreHost::QueryCardOpacity() const noexcept
{
    // Notch: useTransform(y, [100, screenHeight * 0.5], [0, 1])
    const float Y = QueryCurrentHeight();
    const float Half = static_cast<float>(DisplayHeight) * 0.5f;
    if (Half <= 100.0f) return Y > 100.0f ? 1.0f : 0.0f;
    return std::clamp((Y - 100.0f) / (Half - 100.0f), 0.0f, 1.0f);
}

PlaneExtent ControlCentreHost::QueryCardExtent() const noexcept
{
    // Notch: the shade's content box is h-[100vh] with pb-[35px], items centred; the card is centred within it.
    //    Its bottom edge is the notch top (ShadeY); the visible content box spans [ShadeY − H, ShadeY − 35].
    const float H      = static_cast<float>(DisplayHeight);
    const float ShadeY = QueryCurrentHeight();
    const float BoxTop = ShadeY - H;
    const float BoxBottom = ShadeY - 35.0f;
    const float LiveW = static_cast<float>(Motion.Spring(CardWidthChannel ).Current);
    const float LiveH = static_cast<float>(Motion.Spring(CardHeightChannel).Current);
    const float CardX = (static_cast<float>(DisplayWidth) - LiveW) * 0.5f;
    const float CardY = BoxTop + ((BoxBottom - BoxTop) - LiveH) * 0.5f;
    return Spanning(CardX, CardY, LiveW, LiveH);
}

PlaneExtent ControlCentreHost::QueryTileDiscExtent(uint32_t Slot) const noexcept
{
    const PlaneExtent Card = QueryCardExtent();
    const float GridTop   = Card.MinimumY + HeaderIconSize + CardGap;        // header row is 16 px tall (icon height)
    const float CellWidth = (Card.Width() - GridColumnGap * (GridColumns - 1u)) / GridColumns;
    const float RowHeight = TileDisc + TileLabelGap + TileLabelSize + 2.0f;  // label leading-tight ≈ size + 2
    const uint32_t Column = Slot % GridColumns, Row = Slot / GridColumns;
    const float CellX = Card.MinimumX + Column * (CellWidth + GridColumnGap);
    const float CellY = GridTop + Row * (RowHeight + GridRowGap);
    return Spanning(CellX + (CellWidth - TileDisc) * 0.5f, CellY, TileDisc, TileDisc);
}

PlaneExtent ControlCentreHost::QueryPillTrackExtent() const noexcept
{
    const PlaneExtent Card = QueryCardExtent();
    const float GridTop   = Card.MinimumY + HeaderIconSize + CardGap;
    const float RowHeight = TileDisc + TileLabelGap + TileLabelSize + 2.0f;
    const float GridBottom = GridTop + RowHeight * 2.0f + GridRowGap;
    const float PillTop   = GridBottom + CardGap + PillTopMargin;
    const float PillH     = PillPadding * 2.0f + PillCell;
    const float TrackX    = Card.MinimumX + PillPadding + PillCell + PillGapX;
    const float TrackW    = Card.Width() - 2.0f * (PillPadding + PillCell + PillGapX);
    return Spanning(TrackX, PillTop + (PillH - PillTrack) * 0.5f, TrackW, PillTrack);
}

int ControlCentreHost::SlotUnder(float CursorX, float CursorY) const noexcept
{
    for (uint32_t Slot = 0u; Slot < static_cast<uint32_t>(QuickTileCategory::Count); ++Slot)
    {
        const PlaneExtent Disc = QueryTileDiscExtent(Slot);
        const float Cx = (Disc.MinimumX + Disc.MaximumX) * 0.5f, Cy = (Disc.MinimumY + Disc.MaximumY) * 0.5f;
        const float Dx = CursorX - Cx, Dy = CursorY - Cy;
        if (Dx * Dx + Dy * Dy <= (TileDisc * 0.5f) * (TileDisc * 0.5f)) return static_cast<int>(Slot);
    }
    return -1;
}

void ControlCentreHost::ConstructDashboardLayout(PixelSpace& Surface, float Opacity) const noexcept
{
    const PlaneExtent Card = QueryCardExtent();

    // Header: "Control Center" left, wifi + settings right, all white/50, row height = 16 px icon.
    const PlanePoint TitleSize = Surface.MeasureText("Control Center", HeaderTextSize);
    Surface.Text(Card.MinimumX + HeaderPadX, Card.MinimumY + (HeaderIconSize - TitleSize.Y) * 0.5f,
                 Faded(Ink50(), Opacity), "Control Center", HeaderTextSize);

    GlyphPlacement Gear{};
    Gear.Size = HeaderIconSize; Gear.StrokeWidth = 2.0f; Gear.Colour = Faded(Ink50(), Opacity);
    Gear.X = Card.MaximumX - HeaderPadX - HeaderIconSize; Gear.Y = Card.MinimumY;
    GlyphSpace::Stroke(Surface, VectorCodec::QueryControlCentreSvgPath(ControlCentreIconCategory::SettingsGear), Gear);

    GlyphPlacement Wifi = Gear;
    Wifi.X = Gear.X - HeaderIconGap - HeaderIconSize;
    GlyphSpace::Stroke(Surface, VectorCodec::QueryControlCentreSvgPath(ControlCentreIconCategory::WirelessSignal), Wifi);

    // Grid
    for (uint32_t Slot = 0u; Slot < GridSlots; ++Slot)
        ConstructTileLayout(Surface, Slot, Opacity);

    // Render-scale pill
    ConstructPillLayout(Surface, Opacity);
}

void ControlCentreHost::ConstructTileLayout(PixelSpace& Surface, uint32_t Slot, float Opacity) const noexcept
{
    if (Slot >= static_cast<uint32_t>(QuickTileCategory::Count)) return;   // empty slot: nothing drawn

    const QuickTileStructure& Tile = QueryTile(Slot);
    const bool Active  = IsTileActive(Tile.Category);
    const bool Hover   = HoveredSlot == static_cast<int>(Slot);
    const PlaneExtent Disc = QueryTileDiscExtent(Slot);

    const ColorQuad Fill = Active ? (Hover ? TileActiveHover() : TileActive()) : (Hover ? TileIdleHover() : TileIdle());
    Surface.FillRectangle(Disc, Faded(Fill, Opacity), TileDisc * 0.5f);

    GlyphPlacement Glyph{};
    Glyph.Size        = TileGlyph;
    Glyph.StrokeWidth = Active ? 2.0f : 1.5f;
    Glyph.Colour      = Faded(Active ? InkFull() : Ink70(), Opacity);
    Glyph.X           = Disc.MinimumX + (TileDisc - TileGlyph) * 0.5f;
    Glyph.Y           = Disc.MinimumY + (TileDisc - TileGlyph) * 0.5f;
    const std::string_view Path = VectorCodec::QueryControlCentreSvgPath(Tile.Glyph);
    if (Active) GlyphSpace::Fill(Surface, Path, Glyph);     // Notch: className "fill-current" on the active icon
    GlyphSpace::Stroke(Surface, Path, Glyph);

    const char* Label = Tile.Cycles ? FidelityLabel(Settings.Quality) : Tile.Label;
    const PlanePoint LabelSize = Surface.MeasureText(Label, TileLabelSize);
    const float LabelX = Disc.MinimumX + (TileDisc - LabelSize.X) * 0.5f;
    Surface.Text(LabelX, Disc.MaximumY + TileLabelGap, Faded(Ink70(), Opacity), Label, TileLabelSize);
}

void ControlCentreHost::ConstructPillLayout(PixelSpace& Surface, float Opacity) const noexcept
{
    const PlaneExtent Card  = QueryCardExtent();
    const PlaneExtent Track = QueryPillTrackExtent();
    const float PillH = PillPadding * 2.0f + PillCell;
    const float PillY = Track.MinimumY - (PillH - PillTrack) * 0.5f;

    Surface.FillRectangle(Spanning(Card.MinimumX, PillY, Card.Width(), PillH), Faded(TileIdle(), Opacity), PillH * 0.5f);

    GlyphPlacement Video{};
    Video.Size = PillGlyph; Video.StrokeWidth = 2.0f; Video.Colour = Faded(Ink50(), Opacity);
    Video.X = Card.MinimumX + PillPadding + (PillCell - PillGlyph) * 0.5f;
    Video.Y = PillY + PillPadding + (PillCell - PillGlyph) * 0.5f;
    GlyphSpace::Stroke(Surface, VectorCodec::QueryControlCentreSvgPath(ControlCentreIconCategory::VideoRenderScale), Video);

    Surface.FillRectangle(Track, Faded(TrackBlack50(), Opacity), PillTrack * 0.5f);
    const float T = (Settings.RenderScale - RenderScaleMinimum) / (1.0f - RenderScaleMinimum);
    Surface.FillRectangle(Spanning(Track.MinimumX, Track.MinimumY, Track.Width() * std::clamp(T, 0.0f, 1.0f), PillTrack),
                          Faded(TrackFill(), Opacity), PillTrack * 0.5f);

    char Value[8];
    std::snprintf(Value, sizeof(Value), "%d%%", static_cast<int>(std::lround(Settings.RenderScale * 100.0f)));
    const PlanePoint ValueSize = Surface.MeasureText(Value, PillValueSize);
    const float CellX = Card.MaximumX - PillPadding - PillCell;
    Surface.Text(CellX + (PillCell - ValueSize.X) * 0.5f, PillY + PillPadding + (PillCell - ValueSize.Y) * 0.5f,
                 Faded(Ink50(), Opacity), Value, PillValueSize);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SETTINGS PAGES
//------------------------------------------------------------------------------------------------------------------------

namespace {

struct HubRowStructure
{
    ControlCentreIconCategory Icon;
    const char*               Label;
    const char*               Description;
};

// Direction (supersedes ArcNotch.tsx's Appearance / Display & Workspace / Input & Keybindings / Telemetry rows):
//    Render Settings · Appearance · Input · Notifications, in that order.
constexpr HubRowStructure HubRows[4] =
{
    { ControlCentreIconCategory::SlidersQuality,    "Render Settings", "Global illumination, anti-aliasing, quality" },
    { ControlCentreIconCategory::AppearancePalette, "Appearance",      "Theme, fonts, and system colors"             },
    { ControlCentreIconCategory::ShieldInput,       "Input",           "Shortcuts, mouse sensitivity, controllers"   },
    { ControlCentreIconCategory::NotificationsBell, "Notifications",   "RAM usage, FPS, baking complete alerts"      },
};

struct PageChromeStructure
{
    const char* Title;
    const char* Subtitle;
    const char* SecondaryButton;
    const char* PrimaryButton;
};

// Titles / subtitles / footer pills verbatim from Notch OtherModals.tsx & SettingsModal.tsx.
constexpr PageChromeStructure PageChrome[4] =
{
    { "Render Settings",            "Configure output rendering quality and passes.",       "Discard Changes", "Apply Render Settings" },
    { "Display Settings",           "Appearance & typography",                              "Reset",           "Apply"                 },
    { "Keybindings Setup",          "Configure navigation style and keyboard shortcuts.",   "Discard Changes", "Save keybindings"      },
    { "Telemetry & Notifications",  "System resource overlay and alert preferences.",       "Discard Changes", "Save Preferences"      },
};

constexpr const char* AppearanceTabs[3] = { "Display", "Fonts", "Theme" };   // SettingsModal tabs, in order

inline ColorQuad HubList()   noexcept { return ControlKit::Palette().Card; }                     // bg-[#141415]  → CardBackground
inline ColorQuad HubDisc()   noexcept { return ControlKit::Palette().Field; }                    // bg-[#09090A]  → MainBackground
inline ColorQuad PageSheet() noexcept { return ControlKit::Palette().Card; }                     // bg-[#161415]  → CardBackground
inline ColorQuad Ink90()     noexcept { return ControlKit::Palette().Text; }                     // colors.text
inline ColorQuad Ink40()     noexcept { return Alpha(ControlKit::Palette().TextDim, ControlKit::Palette().TextDim.Alpha * 0.8f); }   // text-white/40
inline ColorQuad Ink30()     noexcept { return Alpha(ControlKit::Palette().TextDim, ControlKit::Palette().TextDim.Alpha * 0.6f); }   // text-white/30
inline ColorQuad Ink10()     noexcept { return ControlKit::Palette().LightSurface ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.10f } : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.10f }; }   // border-white/10
inline ColorQuad Ink06()     noexcept { return ControlKit::Palette().Divider; }                  // colors.divider
inline ColorQuad Ink05()     noexcept { return ControlKit::Palette().LightSurface ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.05f } : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.05f }; }   // border-white/5, hover:bg-white/5
constexpr ColorQuad Black20  { 0.0f, 0.0f, 0.0f, 0.20f };                                        // bg-black/20 footer (Notch hard-codes)
constexpr ColorQuad InputInk    { 0xEC / 255.0f, 0xEC / 255.0f, 0xEC / 255.0f, 1.0f };   // #ececec
constexpr ColorQuad InputMuted  { 0x86 / 255.0f, 0x83 / 255.0f, 0x84 / 255.0f, 1.0f };   // #868384
constexpr ColorQuad InputBorder { 0x2A / 255.0f, 0x26 / 255.0f, 0x27 / 255.0f, 1.0f };   // #2a2627
constexpr ColorQuad InputAccent { 0xE2 / 255.0f, 0x54 / 255.0f, 0xEB / 255.0f, 1.0f };   // #e254eb

constexpr float LineHeight(float Size) noexcept { return Size * 1.5f; }   // Tailwind default leading for text-sm etc.

void StrokeRoundedRectangle(PixelSpace& Surface, const PlaneExtent& Extent, ColorQuad Colour, float Radius, float Thickness) noexcept
{
    // Poly-line approximation of a rounded rectangle outline (ImGui AddRect is not exposed through PixelSpace).
    std::vector<PlanePoint> Points;
    const float R = std::min(Radius, std::min(Extent.Width(), Extent.Height()) * 0.5f);
    constexpr int Segments = 8;
    auto Arc = [&](float Cx, float Cy, float From)
    {
        for (int I = 0; I <= Segments; ++I)
        {
            const float A = From + (3.14159265f * 0.5f) * (static_cast<float>(I) / Segments);
            Points.push_back(PlanePoint{ Cx + std::cos(A) * R, Cy + std::sin(A) * R });
        }
    };
    Arc(Extent.MaximumX - R, Extent.MinimumY + R, -3.14159265f * 0.5f);
    Arc(Extent.MaximumX - R, Extent.MaximumY - R, 0.0f);
    Arc(Extent.MinimumX + R, Extent.MaximumY - R, 3.14159265f * 0.5f);
    Arc(Extent.MinimumX + R, Extent.MinimumY + R, 3.14159265f);
    Surface.StrokePolyline(Points.data(), static_cast<uint32_t>(Points.size()), Colour, Thickness, true);
}

} // namespace

PlaneExtent ControlCentreHost::QueryHeaderGearExtent() const noexcept
{
    const PlaneExtent Card = QueryCardExtent();
    return Spanning(Card.MaximumX - HeaderPadX - HeaderIconSize, Card.MinimumY, HeaderIconSize, HeaderIconSize);
}

PlaneExtent ControlCentreHost::QueryHubBackExtent() const noexcept
{
    // Hub header: px-2 py-1, ChevronLeft 24 with pr-3; row height = 22 px text line (≈ 33 px leading).
    const PlaneExtent Card = QueryCardExtent();
    const float RowH = LineHeight(HubTitleSize) + 8.0f;   // py-1 ×2
    return Spanning(Card.MinimumX + 8.0f, Card.MinimumY + (RowH - HubBackGlyph) * 0.5f, HubBackGlyph + 12.0f, HubBackGlyph);
}

PlaneExtent ControlCentreHost::QueryHubRowExtent(uint32_t Row) const noexcept
{
    // Hub: flex-col gap-10 → header (py-1 + mb-2) then the list; each row p-4 around a 44 px disc → 76 px.
    const PlaneExtent Card = QueryCardExtent();
    const float HeaderH = LineHeight(HubTitleSize) + 8.0f + 8.0f;   // py-1 ×2 + mb-2
    const float ListTop = Card.MinimumY + HeaderH + 40.0f;          // gap-10
    const float RowH    = HubRowDisc + HubRowPadding * 2.0f;
    return Spanning(Card.MinimumX, ListTop + Row * (RowH + 1.0f), Card.Width(), RowH);   // +1: border-b
}

PlaneExtent ControlCentreHost::QueryPageCloseExtent() const noexcept
{
    // p-2 rounded-full button around a 16 px X → 32 px disc, at the header's top-right (p-8).
    const PlaneExtent Card = QueryCardExtent();
    const float Disc = PageCloseGlyph + 16.0f;
    return Spanning(Card.MaximumX - PagePadding - Disc, Card.MinimumY + PagePadding, Disc, Disc);
}

PlaneExtent ControlCentreHost::QueryPageTabExtent(uint32_t Tab) const noexcept
{
    // Header: p-8 pb-4 → title (24 px, mb-2) + subtitle (14 px). Tabs row: px-8, gap-6, pb-4.
    const PlaneExtent Card = QueryCardExtent();
    const float HeaderBottom = Card.MinimumY + PagePadding + LineHeight(PageTitleSize) + 8.0f + LineHeight(PageSubtitleSize) + 16.0f;
    float X = Card.MinimumX + PagePadding;
    for (uint32_t Index = 0u; Index < 3u; ++Index)
    {
        // Widths are measured at record time; hit-testing uses the last measured widths cached below.
        const float W = TabWidthCache[Index] > 0.0f ? TabWidthCache[Index] : 44.0f;
        if (Index == Tab) return Spanning(X, HeaderBottom, W, LineHeight(PageTabSize) + PageTabPadBottom);
        X += W + PageTabGap;
    }
    return {};
}

PlaneExtent ControlCentreHost::QueryPageButtonExtent(bool Primary) const noexcept
{
    // Footer: p-6, pills px-6 py-2 text-sm, gap-3, right-aligned.
    const PlaneExtent Card = QueryCardExtent();
    const float PillH = LineHeight(PageButtonSize) + PageButtonPadY * 2.0f;
    const float FooterTop = Card.MaximumY - PageFooterPad * 2.0f - PillH;
    const uint32_t Page = static_cast<uint32_t>(ActivePage) - static_cast<uint32_t>(ControlCentrePageCategory::RenderSettings);
    const float PrimaryW   = (Page < 4u ? ButtonWidthCache[Page][1] : 0.0f) + PageButtonPadX * 2.0f;
    const float SecondaryW = (Page < 4u ? ButtonWidthCache[Page][0] : 0.0f) + PageButtonPadX * 2.0f;
    if (ActivePage == ControlCentrePageCategory::Input)
    {
        // Input modal: "Discard Changes" flush-left in the body, "Reset Defaults" + "Save keybindings" flush-right
        //    (mt-2 pb-6 inside the py-8 body, lg:px-12). Recording and hit-testing share this geometry.
        const float InputPillH = LineHeight(13.0f) + 10.0f * 2.0f;   // py-2.5
        const float Top = Card.MaximumY - PagePadding - 24.0f - InputPillH;
        if (Primary) return Spanning(Card.MaximumX - 48.0f - PrimaryW, Top, PrimaryW, InputPillH);
        return Spanning(Card.MinimumX + 48.0f, Top, SecondaryW, InputPillH);
    }
    const float PrimaryX   = Card.MaximumX - PageFooterPad - PrimaryW;
    if (Primary) return Spanning(PrimaryX, FooterTop + PageFooterPad, PrimaryW, PillH);
    return Spanning(PrimaryX - PageButtonGap - SecondaryW, FooterTop + PageFooterPad, SecondaryW, PillH);
}

PlaneExtent ControlCentreHost::QueryPageResetExtent() const noexcept
{
    // Input page only: "Reset Defaults" sits between the left-hand Discard and the right-hand Save pill (gap-3).
    if (ActivePage != ControlCentrePageCategory::Input) return PlaneExtent{};
    const PlaneExtent Primary = QueryPageButtonExtent(true);
    const float RW = ResetWidthCache + 40.0f;
    return Spanning(Primary.MinimumX - PageButtonGap - RW, Primary.MinimumY, RW, Primary.Height());
}

void ControlCentreHost::ConstructPageLayout(PixelSpace& Surface, ControlCentrePageCategory Page, float Opacity, float SlideX, float Scale, bool Live) noexcept
{
    const PlaneExtent Card = QueryCardExtent();
    const float PivotX = (Card.MinimumX + Card.MaximumX) * 0.5f;
    const float PivotY = (Card.MinimumY + Card.MaximumY) * 0.5f;

    const uint32_t Mark = Surface.BeginGroup();
    switch (Page)
    {
        case ControlCentrePageCategory::Dashboard:   ConstructDashboardLayout(Surface, 1.0f); break;
        case ControlCentrePageCategory::SettingsHub: ConstructHubLayout(Surface, 1.0f);       break;
        default:                                     ConstructSubPageLayout(Surface, Page, 1.0f, Live); break;
    }
    Surface.EndGroup(Mark, SlideX, 0.0f, Scale, PivotX, PivotY, Opacity);
}

void ControlCentreHost::ConstructHubLayout(PixelSpace& Surface, float Opacity) const noexcept
{
    const PlaneExtent Card = QueryCardExtent();

    // Header: ChevronLeft 24 (pr-3) + "Settings" text-[22px] font-bold text-white.
    const PlaneExtent Back = QueryHubBackExtent();
    GlyphPlacement Chevron{};
    Chevron.Size = HubBackGlyph; Chevron.StrokeWidth = 2.0f; Chevron.Colour = Faded(InkFull(), Opacity);
    Chevron.X = Back.MinimumX; Chevron.Y = Back.MinimumY;
    GlyphSpace::Stroke(Surface, VectorCodec::QueryControlCentreSvgPath(ControlCentreIconCategory::ChevronBack), Chevron);

    const PlanePoint TitleSize = Surface.MeasureText("Settings", HubTitleSize);
    Surface.Text(Back.MaximumX, Back.MinimumY + (HubBackGlyph - TitleSize.Y) * 0.5f, Faded(InkFull(), Opacity), "Settings", HubTitleSize);

    // List: bg-[#141415] rounded-[24px]; 4 rows.
    const PlaneExtent First = QueryHubRowExtent(0u), Last = QueryHubRowExtent(3u);
    Surface.FillRectangle(PlaneExtent{ First.MinimumX, First.MinimumY, Last.MaximumX, Last.MaximumY }, Faded(HubList(), Opacity), HubListRadius);

    const uint32_t GroupMark = Surface.BeginGroup();   // rows are clipped visually by the rounded list via same colour; hover fill is inset
    for (uint32_t Row = 0u; Row < 4u; ++Row)
    {
        const PlaneExtent Extent = QueryHubRowExtent(Row);
        const HubRowStructure& Item = HubRows[Row];

        if (HoveredHubRow == static_cast<int>(Row))
            Surface.FillRectangle(Extent, Faded(Ink05(), Opacity), Row == 0u || Row == 3u ? HubListRadius : 0.0f);

        // Disc 44 px #09090A with a 20 px white/70 glyph (strokeWidth 2).
        const float DiscX = Extent.MinimumX + HubRowPadding, DiscY = Extent.MinimumY + HubRowPadding;
        Surface.FillRectangle(Spanning(DiscX, DiscY, HubRowDisc, HubRowDisc), Faded(HubDisc(), Opacity), HubRowDisc * 0.5f);
        GlyphPlacement Icon{};
        Icon.Size = HubRowGlyph; Icon.StrokeWidth = 2.0f; Icon.Colour = Faded(Ink70(), Opacity);
        Icon.X = DiscX + (HubRowDisc - HubRowGlyph) * 0.5f; Icon.Y = DiscY + (HubRowDisc - HubRowGlyph) * 0.5f;
        GlyphSpace::Stroke(Surface, VectorCodec::QueryControlCentreSvgPath(Item.Icon), Icon);

        // Label 15 px white/90 + description 13 px white/40, gap-0.5 (2 px), vertically centred on the disc.
        const PlanePoint LabelSize = Surface.MeasureText(Item.Label, HubLabelSize);
        const PlanePoint DescSize  = Surface.MeasureText(Item.Description, HubDescSize);
        const float TextH = LabelSize.Y + 2.0f + DescSize.Y;
        const float TextX = DiscX + HubRowDisc + HubRowGap;
        const float TextY = DiscY + (HubRowDisc - TextH) * 0.5f;
        Surface.Text(TextX, TextY, Faded(Ink90(), Opacity), Item.Label, HubLabelSize);
        Surface.Text(TextX, TextY + LabelSize.Y + 2.0f, Faded(Ink40(), Opacity), Item.Description, HubDescSize);

        // ChevronRight 18 white/30, mr-2.
        GlyphPlacement Forward{};
        Forward.Size = HubChevron; Forward.StrokeWidth = 2.0f; Forward.Colour = Faded(Ink30(), Opacity);
        Forward.X = Extent.MaximumX - HubRowPadding - 8.0f - HubChevron;
        Forward.Y = Extent.MinimumY + (Extent.Height() - HubChevron) * 0.5f;
        GlyphSpace::Stroke(Surface, VectorCodec::QueryControlCentreSvgPath(ControlCentreIconCategory::ChevronForward), Forward);

        // border-b border-white/5 (not on the last row)
        if (Row < 3u)
            Surface.FillRectangle(Spanning(Extent.MinimumX, Extent.MaximumY, Extent.Width(), 1.0f), Faded(Ink05(), Opacity));
    }
    Surface.EndGroup(GroupMark, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
}

PlaneExtent ControlCentreHost::QueryPageFooterExtent() const noexcept
{
    const PlaneExtent Card = QueryCardExtent();
    const float PillH = LineHeight(PageButtonSize) + PageButtonPadY * 2.0f;
    return PlaneExtent{ Card.MinimumX, Card.MaximumY - PageFooterPad * 2.0f - PillH, Card.MaximumX, Card.MaximumY };
}

PlaneExtent ControlCentreHost::QueryPageBodyExtent() const noexcept
{
    // Between the header (and tab bar on Appearance) and the footer band; px-8 py-8 inside.
    const PlaneExtent Card = QueryCardExtent();
    float Top = Card.MinimumY + PagePadding + LineHeight(PageTitleSize) + 8.0f + LineHeight(PageSubtitleSize) + 16.0f;
    if (ActivePage == ControlCentrePageCategory::Appearance) Top += LineHeight(PageTabSize) + PageTabPadBottom + 1.0f;
    // Input page: no footer band — the body stops above the in-flow button row (mt-2) so pill taps are not body-owned.
    const float Bottom = ActivePage == ControlCentrePageCategory::Input ? QueryPageButtonExtent(true).MinimumY - 8.0f : QueryPageFooterExtent().MinimumY;
    return PlaneExtent{ Card.MinimumX, Top, Card.MaximumX, Bottom };
}

void ControlCentreHost::ConstructPageBodyLayout(PixelSpace& Surface, ControlCentrePageCategory Page, const PlaneExtent& Body, float Opacity, bool Live) noexcept
{
    // Scrollable body: px-8 py-8 (Notch overflow-y-auto p-8). Content is clipped to the body.
    const PlaneExtent Inner = PlaneExtent{ Body.MinimumX + PagePadding, Body.MinimumY + PagePadding, Body.MaximumX - PagePadding, Body.MaximumY - PagePadding };
    ControlPointer Local = Pointer;
    Local.Enabled = Pointer.Enabled && Live && !Dialogue.IsVisible() && (Body.Encloses(Pointer.X, Pointer.Y) || PressedInBody || Appearance.HasOpenMenu() || InputPage.HasOpenMenu());

    Surface.PushClip(Body);
    float ContentHeight = 0.0f;
    if (Page == ControlCentrePageCategory::Appearance)
    {
        switch (ActiveAppearanceSubTab)
        {
            case AppearanceSubTabCategory::Display: ContentHeight = Appearance.ConstructDisplayTabLayout(Surface, Inner, BodyScrollY, Local, Opacity); break;
            case AppearanceSubTabCategory::Theme:   ContentHeight = Appearance.ConstructThemeTabLayout  (Surface, Inner, BodyScrollY, Local, Opacity); break;
            case AppearanceSubTabCategory::Fonts:   ContentHeight = Appearance.ConstructFontsTabLayout  (Surface, Inner, BodyScrollY, Local, Opacity); break;
            default: break;
        }
    }
    else if (Page == ControlCentrePageCategory::Input)         ContentHeight = InputPage.ConstructInputLayout(Surface, Inner, BodyScrollY, Local, Opacity);
    else if (Page == ControlCentrePageCategory::Notifications) ContentHeight = NotificationPage.ConstructNotificationLayout(Surface, Inner, BodyScrollY, Local, Opacity);
    Surface.PopClip();
    if (Live) BodyContentHeight = ContentHeight + PagePadding * 2.0f;

    // Scrollbar (custom-scrollbar): 4 px white/10 thumb at the right edge while content overflows.
    const float Room = BodyContentHeight - Body.Height();
    if (Room > 0.0f)
    {
        const float TrackH = Body.Height() - 16.0f;
        const float ThumbH = std::max(TrackH * Body.Height() / BodyContentHeight, 24.0f);
        const float ThumbY = Body.MinimumY + 8.0f + (TrackH - ThumbH) * std::clamp(BodyScrollY / Room, 0.0f, 1.0f);
        Surface.FillRectangle(Spanning(Body.MaximumX - 10.0f, ThumbY, 4.0f, ThumbH), Faded(Ink10(), Opacity), 2.0f);
    }
}

void ControlCentreHost::ConstructSubPageLayout(PixelSpace& Surface, ControlCentrePageCategory Page, float Opacity, bool Live) noexcept
{
    const PlaneExtent Card = QueryCardExtent();
    const uint32_t Index = static_cast<uint32_t>(Page) - static_cast<uint32_t>(ControlCentrePageCategory::RenderSettings);
    if (Index >= 4u) return;
    const PageChromeStructure& Chrome = PageChrome[Index];
    const bool InputStyle = Page == ControlCentrePageCategory::Input;   // Notch's Input modal uses its own palette

    // Sheet: bg-[#161415] rounded-[32px] border border-white/5.
    Surface.FillRectangle(Card, Faded(PageSheet(), Opacity), PageRadius);
    ControlKit::OutlineRounded(Surface, Card, Faded(Ink05(), Opacity), PageRadius);

    // Header p-8 pb-4: title text-2xl semibold (mb-2) + subtitle text-sm muted; back chevron top-right (RequestLeave → previous page, dirty-checked).
    const ColorQuad TextInk  = InputStyle ? InputInk   : Ink90();
    const ColorQuad MutedInk = InputStyle ? InputMuted : Ink50();
    const float HeaderX = Card.MinimumX + PagePadding;
    float Y = Card.MinimumY + PagePadding;
    const PlanePoint TitleSize = Surface.MeasureText(Chrome.Title, PageTitleSize);
    Surface.Text(HeaderX, Y + (LineHeight(PageTitleSize) - TitleSize.Y) * 0.5f, Faded(TextInk, Opacity), Chrome.Title, PageTitleSize);
    Y += LineHeight(PageTitleSize) + 8.0f;
    const PlanePoint SubSize = Surface.MeasureText(Chrome.Subtitle, PageSubtitleSize);
    Surface.Text(HeaderX, Y + (LineHeight(PageSubtitleSize) - SubSize.Y) * 0.5f, Faded(MutedInk, Opacity), Chrome.Subtitle, PageSubtitleSize);
    Y += LineHeight(PageSubtitleSize) + 16.0f;   // pb-4

    const PlaneExtent Close = QueryPageCloseExtent();
    {
        const bool Hover = Live && Close.Encloses(Pointer.X, Pointer.Y) && !Dialogue.IsVisible();
        if (Hover) Surface.FillRectangle(Close, Faded(Ink05(), Opacity), Close.Width() * 0.5f);   // hover:bg-white/5
        ControlKit::OutlineRounded(Surface, Close, Faded(InputStyle ? Ink10() : Ink06(), Opacity), Close.Width() * 0.5f);
        ControlKit::GlyphCentred(Surface, Close, PageCloseGlyph, Faded(MutedInk, Opacity), ControlCentreIconCategory::ChevronBack);
    }

    // Appearance only: tab bar Display · Fonts · Theme with a 2 px white underline under the active tab.
    if (Page == ControlCentrePageCategory::Appearance)
    {
        float X = HeaderX;
        const float TabsBottom = Y + LineHeight(PageTabSize) + PageTabPadBottom;
        Surface.FillRectangle(Spanning(Card.MinimumX + PagePadding, TabsBottom, Card.Width() - PagePadding * 2.0f, 1.0f), Faded(Ink06(), Opacity));
        for (uint32_t Tab = 0u; Tab < 3u; ++Tab)
        {
            const PlanePoint Size = Surface.MeasureText(AppearanceTabs[Tab], PageTabSize);
            TabWidthCache[Tab] = Size.X;
            const bool Active = static_cast<uint32_t>(ActiveAppearanceSubTab) == Tab;
            const bool Hover = Live && Spanning(X, Y, Size.X, TabsBottom - Y).Encloses(Pointer.X, Pointer.Y);
            Surface.Text(X, Y + (LineHeight(PageTabSize) - Size.Y) * 0.5f, Faded(Active || Hover ? Ink90() : Ink50(), Opacity), AppearanceTabs[Tab], PageTabSize);
            if (Active)
                Surface.FillRectangle(Spanning(X, TabsBottom - 1.0f, Size.X, 2.0f), Faded(InkFull(), Opacity));   // bottom-[-1px] h-0.5
            X += Size.X + PageTabGap;
        }
        Y = TabsBottom + 1.0f;
    }

    // Body
    const PlaneExtent Footer = QueryPageFooterExtent();
    const PlaneExtent Body = PlaneExtent{ Card.MinimumX, Y, Card.MaximumX, InputStyle ? QueryPageButtonExtent(true).MinimumY - 8.0f : Footer.MinimumY };
    ConstructPageBodyLayout(Surface, Page, Body, Opacity, Live);

    // Footer p-6 border-t bg-black/20: status text-[10px] muted left, pills right.
    const float PillH = LineHeight(PageButtonSize) + PageButtonPadY * 2.0f;
    if (!InputStyle)
    {
        // Notch's Input modal keeps its buttons inside the body (no footer band); every other page has one.
        Surface.FillRectangleBottomRounded(Footer, Faded(Black20, Opacity), PageRadius);
        Surface.FillRectangle(Spanning(Card.MinimumX, Footer.MinimumY, Card.Width(), 1.0f), Faded(Ink06(), Opacity));
    }

    const bool Dirty = IsPageDirty();
    std::string Status;
    switch (Page)
    {
        case ControlCentrePageCategory::RenderSettings:
            Status  = Settings.GlobalIllumination ? "Global illumination" : "Direct only";
            Status += " - ";
            Status += Settings.AntiAliasing ? "AA on" : "AA off";
            Status += " - ";
            Status += FidelityLabel(Settings.Quality);
            {
                char Scale[16];
                std::snprintf(Scale, sizeof(Scale), " - %d%%", static_cast<int>(std::lround(Settings.RenderScale * 100.0f)));
                Status += Scale;
            }
            break;
        case ControlCentrePageCategory::Appearance:
            if (Dirty)
            {
                const AppearanceDifference D = Appearance.QueryDifference();
                char Head[48]; std::snprintf(Head, sizeof(Head), "%u unsaved change%s: ", D.Count, D.Count == 1u ? "" : "s");
                Status = Head;
                for (uint32_t I = 0u; I < std::min(D.Count, 8u); ++I) { if (I) Status += ", "; Status += D.Names[I]; }
                if (D.Count > 8u) Status += ", ...";
            }
            else
            {
                const AppearanceSettings& A = Appearance.QueryApplied();
                char Line[96];
                std::snprintf(Line, sizeof(Line), "%s - %dpx radius - %s accent - MSAA %s", AppearanceInspector::QueryThemeName(A.Theme),
                              static_cast<int>(std::lround(A.CornerRadius)), "", A.AntiAliasing == SampleCountCategory::Single ? "1x" : A.AntiAliasing == SampleCountCategory::Double ? "2x" : A.AntiAliasing == SampleCountCategory::Quad ? "4x" : "8x");
                Status = Line;
                // tidy the empty accent slot
                size_t P = Status.find(" -  accent"); if (P != std::string::npos) Status.erase(P, 10);
            }
            break;
        case ControlCentrePageCategory::Notifications:
            if (Dirty)
            {
                uint32_t N = 0u; const NotificationPreferences& A = NotificationPage.QueryApplied(); const NotificationPreferences& D = NotificationPage.QueryDraft();
                N += A.ShowFrameRateOverlay != D.ShowFrameRateOverlay; N += A.ShowMemoryUsage != D.ShowMemoryUsage; N += A.ShowSceneMetadata != D.ShowSceneMetadata;
                N += A.BakingComplete != D.BakingComplete; N += A.RenderFinished != D.RenderFinished; N += A.AutosaveErrors != D.AutosaveErrors;
                N += A.FrameRateDrops != D.FrameRateDrops; N += A.HoldSeconds != D.HoldSeconds;
                char Head[48]; std::snprintf(Head, sizeof(Head), "%u unsaved change%s.", N, N == 1u ? "" : "s"); Status = Head;
            }
            else Status = "Ready to apply changes.";   // GenericSettingsModal verbatim
            break;
        default:
            Status = "Ready to apply changes.";   // GenericSettingsModal verbatim
            break;
    }

    const PlanePoint StatusSize = Surface.MeasureText(Status.c_str(), PageFooterNote);
    const PlaneExtent Primary   = QueryPageButtonExtentFor(Index, true, Surface);
    const PlaneExtent Secondary = QueryPageButtonExtentFor(Index, false, Surface);
    if (!InputStyle)
        Surface.Text(Card.MinimumX + PageFooterPad, Primary.MinimumY + (PillH - StatusSize.Y) * 0.5f, Faded(MutedInk, Opacity), Status.c_str(), PageFooterNote);

    // Footer pills through ControlKit. Appearance: disabled (opacity .35, no hit) until the draft differs.
    //    Hits are handled by the Grab/Relinquish path (GrabSubject::PageButton) so taps obey the same rules as
    //    every other page control; the widget here only draws.
    ControlPointer Inert{}; Inert.X = Pointer.X; Inert.Y = Pointer.Y; Inert.Enabled = Live && !Dialogue.IsVisible();
    const bool Disabled = Page != ControlCentrePageCategory::RenderSettings && !Dirty;
    if (InputStyle)
    {
        // Input page (Notch InputSettingsModal): bordered rounded-xl pills, #ececec 13 px, hover:bg-white/5;
        //    primary filled #e254eb hover:bg-[#d040d9]. Disabled pills follow the kit rule (opacity .35, no hit).
        const float DimA = Disabled ? ControlKitTokens::DisabledAlpha : 1.0f;
        auto Hover = [&](const PlaneExtent& E) { return Inert.Enabled && !Disabled && E.Encloses(Pointer.X, Pointer.Y); };
        if (Hover(Secondary)) Surface.FillRectangle(Secondary, Faded(Ink05(), Opacity), 12.0f);
        ControlKit::OutlineRounded(Surface, Secondary, Faded(InputBorder, Opacity * DimA), 12.0f);
        ControlKit::TextCentred(Surface, Secondary, Faded(InputInk, Opacity * DimA), Chrome.SecondaryButton, 13.0f);
        {
            const PlaneExtent Reset = QueryPageResetExtent();
            const float ResetA = InputPage.IsDefault() ? ControlKitTokens::DisabledAlpha : 1.0f;
            if (Inert.Enabled && !InputPage.IsDefault() && Reset.Encloses(Pointer.X, Pointer.Y)) Surface.FillRectangle(Reset, Faded(Ink05(), Opacity), 12.0f);
            ControlKit::OutlineRounded(Surface, Reset, Faded(InputBorder, Opacity * ResetA), 12.0f);
            ControlKit::TextCentred(Surface, Reset, Faded(InputInk, Opacity * ResetA), "Reset Defaults", 13.0f);
        }
        Surface.FillRectangle(Primary, Faded(Hover(Primary) ? ColorQuad{ 0xD0 / 255.0f, 0x40 / 255.0f, 0xD9 / 255.0f, 1.0f } : InputAccent, Opacity * DimA), 12.0f);
        ControlKit::TextCentred(Surface, Primary, Faded(InkFull(), Opacity * DimA), Chrome.PrimaryButton, 13.0f);
    }
    else
    {
        ButtonStructure S{}; S.Label = Chrome.SecondaryButton; S.Tone = ButtonToneCategory::Ghost; S.Disabled = Disabled; S.FontSize = PageButtonSize; S.PaddingX = PageButtonPadX; S.Height = PillH;
        ButtonStructure Pr{}; Pr.Label = Chrome.PrimaryButton; Pr.Tone = ButtonToneCategory::Primary; Pr.Disabled = Disabled; Pr.FontSize = PageButtonSize; Pr.PaddingX = PageButtonPadX; Pr.Height = PillH;
        // Notch secondary is text-only (muted → text on hover); the kit's Ghost tone matches. Text tint follows the page ink.
        ControlKit::PillButton(Surface, Secondary, S, Inert, Opacity);
        ControlKit::PillButton(Surface, Primary, Pr, Inert, Opacity);
    }

    // Floating layers: open dropdown menus, then any dialogue — both above the footer.
    if (Live)
    {
        Appearance.ConstructFloatingLayout(Surface, Pointer, Opacity);
        InputPage.ConstructFloatingLayout(Surface, Pointer, Opacity);
        Dialogue.ConstructDialogueLayout(Surface, Card, Pointer);
    }
}

PlaneExtent ControlCentreHost::QueryPageButtonExtentFor(uint32_t Index, bool Primary, PixelSpace& Surface) const noexcept
{
    // Measures the labels (cached for hit-testing) and lays the two pills out right-to-left in the footer band.
    const bool InputStyle = Index == 2u;
    const float Size = InputStyle ? 13.0f : PageButtonSize;
    const float PadX = InputStyle ? 20.0f : PageButtonPadX;   // px-5 vs px-6
    ButtonWidthCache[Index][0] = Surface.MeasureText(PageChrome[Index].SecondaryButton, Size).X + (PadX - PageButtonPadX) * 2.0f;
    ButtonWidthCache[Index][1] = Surface.MeasureText(PageChrome[Index].PrimaryButton,   Size).X + (PadX - PageButtonPadX) * 2.0f;
    if (InputStyle) ResetWidthCache = Surface.MeasureText("Reset Defaults", 13.0f).X;
    if (InputStyle)
    {
        // Input modal: "Discard Changes" flush-left in the body, "Reset Defaults" + "Save keybindings" flush-right.
        const PlaneExtent Card = QueryCardExtent();
        const float PillH = LineHeight(13.0f) + 10.0f * 2.0f;   // py-2.5
        const float Top = Card.MaximumY - PagePadding - 24.0f - PillH;   // pb-6 inside py-8 body
        const float PrimaryW   = ButtonWidthCache[Index][1] + PageButtonPadX * 2.0f;
        const float SecondaryW = ButtonWidthCache[Index][0] + PageButtonPadX * 2.0f;
        if (Primary) return Spanning(Card.MaximumX - 48.0f - PrimaryW, Top, PrimaryW, PillH);   // lg:px-12
        return Spanning(Card.MinimumX + 48.0f, Top, SecondaryW, PillH);
    }
    return QueryPageButtonExtent(Primary);
}

} // namespace Frontier
