/*============================================================================================================================================
                                                      PARTICLEINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleInspectorPanel: 4-tab Particle Effect Inspector + direct delegation to Frontier's C++ ForceInspectorView

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particles/ParticleInspectorPanel.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlTooltip.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/ExclusiveSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace Frontier::Workspaces
{

namespace
{

using namespace SimulationSpace::ForceFields;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFACE TOKENS
//------------------------------------------------------------------------------------------------------------------------

namespace ParticleInspectorColors
{
inline constexpr ColorRgba RootBg         { 18u,  18u,  18u,  255u };
inline constexpr ColorRgba SectionBg      { 28u,  28u,  30u,  255u };
inline constexpr ColorRgba SubWellBg      { 22u,  22u,  24u,  255u };
inline constexpr ColorRgba RecessDark     { 13u,  14u,  17u,  255u };
inline constexpr ColorRgba HairlineBorder { 255u, 255u, 255u, 15u  };
inline constexpr ColorRgba SubtleBorder   { 255u, 255u, 255u, 26u  };
inline constexpr ColorRgba TabInactiveBg  { 23u,  23u,  25u,  255u };
inline constexpr ColorRgba TabActiveBg    { 38u,  38u,  41u,  255u };
inline constexpr ColorRgba TabHoverBg     { 45u,  45u,  48u,  255u };
inline constexpr ColorRgba InkPrimary     { 240u, 240u, 240u, 255u };
inline constexpr ColorRgba InkHeading     { 224u, 224u, 224u, 255u };
inline constexpr ColorRgba InkLabel       { 170u, 176u, 182u, 255u };
inline constexpr ColorRgba InkMuted       { 132u, 138u, 144u, 255u };
inline constexpr ColorRgba InkDim         { 102u, 108u, 114u, 255u };
inline constexpr ColorRgba EmeraldSoft    { 146u, 218u, 192u, 255u };
inline constexpr ColorRgba AxisRed        { 226u, 116u, 112u, 255u };
inline constexpr ColorRgba AxisGreen      { 103u, 196u, 133u, 255u };
inline constexpr ColorRgba AxisBlue       { 110u, 167u, 229u, 255u };
inline constexpr ColorRgba AmberAccent    { 255u, 180u, 84u,  255u };
inline constexpr ColorRgba TealCode       { 169u, 217u, 200u, 255u };
inline constexpr ColorRgba FooterBg       { 21u,  21u,  21u,  255u };
} // namespace ParticleInspectorColors

//------------------------------------------------------------------------------------------------------------------------
//                                                SPECTRUM RAMP SAMPLING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ColorRgba EvaluateColorRamp(
    const ParticleEffectRecordEntry& Effect,
    float                            NormalizedT) noexcept
{
    float U = NormalizedT + Effect.RampPhaseOffset;
    if (Effect.RampWrap == ParticleRampWrapCategory::Repeat)
    {
        U = U - std::floor(U);
    }
    else if (Effect.RampWrap == ParticleRampWrapCategory::Mirror)
    {
        const float Mod2 = std::fmod(std::abs(U), 2.0f);
        U                = (Mod2 <= 1.0f) ? Mod2 : (2.0f - Mod2);
    }
    else
    {
        U = std::clamp(U, 0.0f, 1.0f);
    }

    if (Effect.RampInterp == ParticleRampInterpCategory::Stepped)
    {
        U = std::floor(U * 5.0f) / 4.0f;
        U = std::clamp(U, 0.0f, 1.0f);
    }
    else if (Effect.RampInterp == ParticleRampInterpCategory::Smooth)
    {
        U = U * U * (3.0f - 2.0f * U);
    }

    if (U < 0.5f)
    {
        return ColorRgba::Blend(Effect.ColorA, Effect.ColorC, U * 2.0f);
    }
    return ColorRgba::Blend(Effect.ColorC, Effect.ColorB, (U - 0.5f) * 2.0f);
}

void ConstructSpectrumRibbon(
    PixelSpace&                      Surface,
    Rectangle2D                      Rect,
    const ParticleEffectRecordEntry& Effect) noexcept
{
    Surface.FillRectangle(Rect, ParticleInspectorColors::RecessDark, 5.0f);
    constexpr int32_t SegmentCount = 36;
    const float       InnerX       = Rect.X + 2.0f;
    const float       InnerY       = Rect.Y + 2.0f;
    const float       InnerW       = std::max(1.0f, Rect.Width - 4.0f);
    const float       InnerH       = std::max(1.0f, Rect.Height - 4.0f);
    const float       StepW        = InnerW / static_cast<float>(SegmentCount);

    for (int32_t Segment = 0; Segment < SegmentCount; ++Segment)
    {
        const float     T      = (static_cast<float>(Segment) + 0.5f) / static_cast<float>(SegmentCount);
        const ColorRgba Sample = EvaluateColorRamp(Effect, T).WithAlpha(255u);
        Surface.FillRectangle({ InnerX + static_cast<float>(Segment) * StepW, InnerY, StepW + 0.6f, InnerH }, Sample, 0.0f);
    }
    Surface.StrokeRectangle(Rect, ParticleInspectorColors::SubtleBorder, 5.0f, 1.0f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              COMPACT SECTION PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

float BeginSectionBox(
    PixelSpace&      Surface,
    float            LeftX,
    float            TopY,
    float            InnerW,
    float            BoxHeight,
    std::string_view Title,
    std::string_view TagText,
    ColorRgba        TagAccent) noexcept
{
    const Rectangle2D Box{ LeftX, TopY, InnerW, BoxHeight };
    Surface.FillRectangle(Box, ParticleInspectorColors::SectionBg, 9.0f);
    Surface.StrokeRectangle(Box, ParticleInspectorColors::HairlineBorder, 9.0f, 1.0f);

    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::SemiBold,
        { LeftX + 12.0f, TopY + 10.0f },
        12.0f,
        ParticleInspectorColors::InkHeading,
        Title);

    if (!TagText.empty())
    {
        const float TagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.2f, TagText, true) + 12.0f;
        const Rectangle2D TagRect{ LeftX + InnerW - TagW - 10.0f, TopY + 8.0f, TagW, 17.0f };
        Surface.FillRectangle(TagRect, ColorRgba{ TagAccent.Red, TagAccent.Green, TagAccent.Blue, 28u }, 4.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { TagRect.X + 6.0f, TagRect.Y + 3.0f },
            9.2f,
            TagAccent,
            TagText,
            true);
    }

    Surface.FillRectangle(
        { LeftX + 10.0f, TopY + 31.0f, InnerW - 20.0f, 1.0f },
        ParticleInspectorColors::HairlineBorder,
        0.0f);

    return TopY + 39.0f;
}

bool RenderLabeledSliderRow(
    PixelSpace&           Surface,
    float                 X,
    float                 Y,
    float                 W,
    int32_t               SliderId,
    const char*           Label,
    float                 Minimum,
    float                 Maximum,
    float&                InOutScalar,
    const char*           UnitSuffix,
    int                   Precision,
    const ControlPointer& Pointer,
    int32_t&              ActiveSliderId) noexcept
{
    char DecoratedUnit[32]{};
    if (UnitSuffix != nullptr && UnitSuffix[0] != '\0')
    {
        std::snprintf(DecoratedUnit, sizeof(DecoratedUnit), " %s", UnitSuffix);
    }

    return ScalarSlider::ConstructLabeledInspectorRow(
        Surface,
        { X, Y, W, 36.0f },
        SliderId,
        Label,
        Minimum,
        Maximum,
        InOutScalar,
        DecoratedUnit,
        static_cast<int32_t>(Precision),
        Pointer,
        ActiveSliderId,
        1.0f,
        14.0f,
        14.0f);
}

bool RenderAxisVectorRow(
    PixelSpace&             Surface,
    float                   X,
    float                   Y,
    float                   W,
    int32_t                 BaseAxisId,
    const char*             Label,
    SpatialVector3&         InOutVec,
    float                   Sensitivity,
    const char*             UnitText,
    const ControlPointer&   Pointer,
    int32_t&                ActiveAxisDragId,
    float&                  AxisDragStartX,
    float&                  AxisDragOrigin) noexcept
{
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { X, Y },
        W - 44.0f,
        11.0f,
        ParticleInspectorColors::InkLabel,
        Label);
    if (UnitText != nullptr && UnitText[0] != '\0')
    {
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { X + W, Y },
            10.0f,
            ParticleInspectorColors::InkDim,
            UnitText,
            true);
    }

    const float Gap   = 6.0f;
    const float CellW = (W - Gap * 2.0f) / 3.0f;
    const float CellY = Y + 16.0f;
    const float CellH = 24.0f;
    const std::array<const char*, 3u> AxisLabels = { "X", "Y", "Z" };
    const std::array<ColorRgba, 3u>   AxisColors = {
        ParticleInspectorColors::AxisRed,
        ParticleInspectorColors::AxisGreen,
        ParticleInspectorColors::AxisBlue,
    };
    float* Components[3] = { &InOutVec.X, &InOutVec.Y, &InOutVec.Z };

    bool Changed = false;
    for (int32_t Axis = 0; Axis < 3; ++Axis)
    {
        const Rectangle2D Cell{ X + static_cast<float>(Axis) * (CellW + Gap), CellY, CellW, CellH };
        const int32_t     DragId  = BaseAxisId + Axis;
        const bool        Active  = (ActiveAxisDragId == DragId);
        const bool        Hovered = Pointer.Enabled && Cell.Contains({ Pointer.X, Pointer.Y });

        Surface.FillRectangle(Cell, ParticleInspectorColors::RecessDark, 5.0f);
        Surface.StrokeRectangle(
            Cell,
            Active ? AxisColors[Axis] : (Hovered ? ParticleInspectorColors::SubtleBorder : ParticleInspectorColors::HairlineBorder),
            5.0f,
            1.0f);
        Surface.FillRectangle({ Cell.X, Cell.Y + 3.0f, 3.0f, Cell.Height - 6.0f }, AxisColors[Axis], 1.5f);

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { Cell.X + 8.0f, Cell.Y + 5.5f },
            10.5f,
            AxisColors[Axis],
            AxisLabels[Axis],
            true);

        char NumBuf[24]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%.2f", *Components[Axis]);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { Cell.Right() - 6.0f, Cell.Y + 5.5f },
            10.5f,
            ParticleInspectorColors::InkPrimary,
            NumBuf,
            true);

        if (Pointer.Enabled && Pointer.Pressed && Hovered)
        {
            ActiveAxisDragId = DragId;
            AxisDragStartX   = Pointer.X;
            AxisDragOrigin   = *Components[Axis];
        }
        if (Active && Pointer.Down)
        {
            const float NextScalar = AxisDragOrigin + (Pointer.X - AxisDragStartX) * Sensitivity;
            if (std::abs(NextScalar - *Components[Axis]) > 1.0e-4f)
            {
                *Components[Axis] = NextScalar;
                Changed           = true;
            }
        }
        else if (Active && !Pointer.Down)
        {
            ActiveAxisDragId = -1;
        }
    }
    return Changed;
}

bool RenderSwitchRow(
    PixelSpace&           Surface,
    float                 X,
    float                 Y,
    float                 W,
    const char*           Label,
    bool&                 InOutEnabled,
    const ControlPointer& Pointer) noexcept
{
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { X, Y + 4.0f },
        W - 58.0f,
        11.2f,
        ParticleInspectorColors::InkLabel,
        Label);

    const Rectangle2D SwitchRect{ X + W - 44.0f, Y, 44.0f, 22.0f };
    const ControlContact Contact = ExclusiveSelector::ConstructCompact(Surface, SwitchRect, InOutEnabled, Pointer);
    if (Contact.Clicked)
    {
        InOutEnabled = !InOutEnabled;
        return true;
    }
    return false;
}

void RenderMetricTile(
    PixelSpace& Surface,
    Rectangle2D Rect,
    const char* Caption,
    const char* ReadingText,
    ColorRgba   Accent) noexcept
{
    Surface.FillRectangle(Rect, ParticleInspectorColors::SubWellBg, 6.0f);
    Surface.StrokeRectangle(Rect, ParticleInspectorColors::HairlineBorder, 6.0f, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { Rect.X + 8.0f, Rect.Y + 5.0f },
        Rect.Width - 16.0f,
        9.2f,
        ParticleInspectorColors::InkDim,
        Caption);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::SemiBold,
        { Rect.X + 8.0f, Rect.Y + 19.0f },
        Rect.Width - 16.0f,
        11.5f,
        Accent,
        ReadingText,
        true);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              LIFECYCLE & CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

ParticleInspectorPanel::~ParticleInspectorPanel() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void ParticleInspectorPanel::AttachSession(ParticleWorkspaceSession* TargetSession) noexcept
{
    Session            = TargetSession;
    ScrollOffsetY      = 0.0f;
    OpenDropdown       = OpenDropdownCategory::None;
    ActiveSliderId     = -1;
    ActiveAxisDragId   = -1;
    SyncedTitleToken   = 0u;
    SyncedSkyTextToken = 0u;
}

void ParticleInspectorPanel::ConfigureActiveTab(ParticleInspectorTab Tab) noexcept
{
    ActiveTab     = Tab;
    ScrollOffsetY = 0.0f;
    OpenDropdown  = OpenDropdownCategory::None;
}

void ParticleInspectorPanel::ConfigureForceInspectorTab(Editor::Simulation::ForceInspectorTab Tab) noexcept
{
    ForceView.ConfigureActiveTab(Tab);
}

void ParticleInspectorPanel::ConfigureScrollOffset(float OffsetY) noexcept
{
    ScrollOffsetY = std::max(0.0f, OffsetY);
}

ParticleInspectorTab ParticleInspectorPanel::QueryActiveTab() const noexcept
{
    return ActiveTab;
}

Editor::Simulation::ForceInspectorTab ParticleInspectorPanel::QueryForceInspectorTab() const noexcept
{
    return ForceView.QueryActiveTab();
}

bool ParticleInspectorPanel::QueryForceInspectorDelegated() const noexcept
{
    if (Session == nullptr)
    {
        return false;
    }
    return Session->QuerySelectionDomain() != ParticleSelectionDomain::ParticleEffect;
}

float ParticleInspectorPanel::QueryScrollOffset() const noexcept
{
    return ScrollOffsetY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             PRIMARY INSPECTOR RENDERER
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    if (Session == nullptr)
    {
        Surface.PushClip(Bounds);
        Surface.FillRectangle(Bounds, ParticleInspectorColors::RootBg, 0.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { Bounds.X + 16.0f, Bounds.Y + 18.0f },
            12.0f,
            ParticleInspectorColors::InkMuted,
            "No Particle Workspace Session attached.");
        Surface.PopClip();
        return;
    }

    // DIRECT REUSE: Delegate Environment / Ambient Wind / ForceSource selections straight to Frontier's C++ ForceInspectorView
    if (QueryForceInspectorDelegated())
    {
        Editor::Simulation::ForceInspectorViewTargets Targets{};
        Targets.Outliner          = &Session->AccessForceOutlinerMirror();
        Targets.OutlinerEntry     = Session->AccessActiveForceOutlinerEntry();
        Targets.Accumulator       = &Session->AccessForceAccumulator();
        Targets.LatticeCascade    = &Session->AccessLatticeCascade();
        Targets.ActiveForce       = Session->AccessActiveForceForInspector();
        Targets.OverlaySettings   = &Session->AccessForceOverlay();
        Targets.AnemometerSamples = Session->QueryAnemometerSamples();
        Targets.SimulationTimeSec = Session->QuerySimulationTimeSec();
        Targets.SelectedWaypoint  = Session->AccessSelectedWaypointIndex();

        if (ForceView.Render(Surface, Bounds, Targets, Input, Δt))
        {
            Session->SynchronizeForceFieldsAccumulator();
            Session->IncrementRevision();
        }
        return;
    }

    ParticleEffectRecordEntry* Effect = Session->AccessSelectedEffect();
    if (Effect == nullptr)
    {
        Session->SelectEnvironmentField();
        return;
    }

    const ParticleCategoryDescriptor& CategoryDesc = ParticlePresetRegistry::QueryCategoryDescriptor(Effect->Category);
    const ColorRgba Accent = (Effect->ColorA.Luminance() < 0.08f) ? CategoryDesc.AccentTint : Effect->ColorA;

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, ParticleInspectorColors::RootBg, 0.0f);

    const float PadX   = 14.0f;
    const float InnerX = Bounds.X + PadX;
    const float InnerW = std::max(160.0f, Bounds.Width - PadX * 2.0f);
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    SheetOpen     = (OpenDropdown != OpenDropdownCategory::None);
    const bool    ChromeEnabled = Input.PrimaryPointer.Enabled && !SheetOpen;

    ControlPointer ChromePointer = Input.PrimaryPointer;
    if (SheetOpen)
    {
        ChromePointer.Enabled  = false;
        ChromePointer.Pressed  = false;
        ChromePointer.Down     = false;
        ChromePointer.Released = false;
    }

    float CursorY = Bounds.Y + 10.0f;

    // 1. Top Breadcrumb & Reset Button
    const std::string Crumb = std::string("Inspector / ") + CategoryDesc.FullTitle;
    const Rectangle2D ResetBtnRect{ InnerX + InnerW - 64.0f, CursorY - 1.0f, 64.0f, 22.0f };
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { InnerX, CursorY + 3.0f },
        std::max(40.0f, ResetBtnRect.X - InnerX - 8.0f),
        10.8f,
        ParticleInspectorColors::InkMuted,
        Crumb);

    const bool ResetHover = ChromeEnabled && ResetBtnRect.Contains(Cursor);
    Surface.FillRectangle(
        ResetBtnRect,
        ResetHover ? ParticleInspectorColors::TabHoverBg : ParticleInspectorColors::SubWellBg,
        6.0f);
    Surface.StrokeRectangle(ResetBtnRect, ParticleInspectorColors::SubtleBorder, 6.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { ResetBtnRect.X + 10.0f, ResetBtnRect.Y + 4.5f },
        10.5f,
        ParticleInspectorColors::InkHeading,
        "Reset");
    WorkspaceAppearance::ConstructResetSymbol(
        Surface,
        { ResetBtnRect.Right() - 12.0f, ResetBtnRect.Y + 11.0f },
        9.0f,
        ParticleInspectorColors::AmberAccent,
        1.3f);
    if (ChromeEnabled && Input.PrimaryPointer.Pressed && ResetHover)
    {
        (void)Session->ResetEffectToPreset(Effect->UniqueToken);
        SyncedTitleToken   = 0u;
        SyncedSkyTextToken = 0u;
    }

    CursorY += 28.0f;

    // 2. Selected Effect Heading Bar (Symbol + Editable Title + Silhouette Tag + Master Switch)
    const Rectangle2D HeadingBox{ InnerX, CursorY, InnerW, 52.0f };
    Surface.FillRectangle(HeadingBox, ParticleInspectorColors::SectionBg, 9.0f);
    Surface.StrokeRectangle(HeadingBox, ParticleInspectorColors::HairlineBorder, 9.0f, 1.0f);

    const Rectangle2D SymbolBox{ HeadingBox.X + 9.0f, HeadingBox.Y + 9.0f, 34.0f, 34.0f };
    Surface.FillRectangle(SymbolBox, ColorRgba{ Accent.Red, Accent.Green, Accent.Blue, 32u }, 8.0f);
    Surface.StrokeRectangle(SymbolBox, Accent.ScaledAlpha(0.55f), 8.0f, 1.0f);
    Surface.FillCircle(SymbolBox.Center(), 7.0f, Accent);
    Surface.StrokeCircle(SymbolBox.Center(), 10.5f, Accent.ScaledAlpha(0.40f), 1.2f);

    // Synchronize TitleEditor when switching effects
    if (SyncedTitleToken != Effect->UniqueToken)
    {
        TitleEditor.Begin(Effect->Title.c_str());
        TitleEditor.Active = false;
        SyncedTitleToken   = Effect->UniqueToken;
    }
    if (TitleEditor.Active)
    {
        WorkspaceAppearance::ForwardInputToTextBuffer(TitleEditor, Input, Δt);
        Effect->Title.assign(TitleEditor.Text, TitleEditor.Length);
    }

    const Rectangle2D EnableSwitchRect{ HeadingBox.Right() - 54.0f, HeadingBox.Y + 15.0f, 44.0f, 22.0f };
    const float TitleX = SymbolBox.Right() + 10.0f;
    const float TitleW = std::max(40.0f, EnableSwitchRect.X - TitleX - 10.0f);
    const Rectangle2D TitleRect{ TitleX, HeadingBox.Y + 6.0f, TitleW, 22.0f };

    if (TitleEditor.Active)
    {
        (void)TextInputField::Construct(Surface, TitleRect, TitleEditor, ChromePointer, 13.0f);
    }
    else
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::SemiBold,
            { TitleRect.X, TitleRect.Y + 3.0f },
            TitleRect.Width,
            13.5f,
            ParticleInspectorColors::InkPrimary,
            Effect->Title);
        if (ChromeEnabled && Input.PrimaryPointer.Pressed && TitleRect.Contains(Cursor))
        {
            TitleEditor.Begin(Effect->Title.c_str());
            TitleEditor.Active = true;
        }
    }
    if (ChromeEnabled && Input.PrimaryPointer.Pressed && !TitleRect.Contains(Cursor))
    {
        TitleEditor.Active = false;
    }

    std::string TypeSubtitle = std::string(CategoryDesc.TagCode) + " · "
                             + ParticlePresetRegistry::QuerySilhouetteLabel(Effect->Silhouette);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { TitleX, HeadingBox.Y + 31.0f },
        TitleW,
        9.8f,
        CategoryDesc.AccentTint,
        TypeSubtitle,
        true);

    const ControlContact SwitchContact = ExclusiveSelector::ConstructCompact(Surface, EnableSwitchRect, Effect->Visible, ChromePointer);
    if (SwitchContact.Clicked)
    {
        Effect->Visible = !Effect->Visible;
        Session->IncrementRevision();
    }

    CursorY += 60.0f;

    // 3. 4-Tab Bar: Overview | Source | Motion & FX | Spec & Export
    static constexpr std::array<const char*, 4u> TabLabels = {{
        "Overview",
        "Source",
        "Motion & FX",
        "Spec & Export",
    }};
    const float TabH   = 28.0f;
    const float TabGap = 4.0f;
    const float TabW   = (InnerW - TabGap * 3.0f) * 0.25f;

    for (uint32_t TabOrd = 0u; TabOrd < 4u; ++TabOrd)
    {
        const Rectangle2D TabRect{ InnerX + static_cast<float>(TabOrd) * (TabW + TabGap), CursorY, TabW, TabH };
        const bool Active  = (static_cast<uint32_t>(ActiveTab) == TabOrd);
        const bool Hovered = ChromeEnabled && TabRect.Contains(Cursor);
        const ColorRgba Bg = Active
            ? ParticleInspectorColors::TabActiveBg
            : (Hovered ? ParticleInspectorColors::TabHoverBg : ParticleInspectorColors::TabInactiveBg);

        Surface.FillRectangle(TabRect, Bg, 6.0f);
        Surface.StrokeRectangle(
            TabRect,
            Active ? ParticleInspectorColors::SubtleBorder : ParticleInspectorColors::HairlineBorder,
            6.0f,
            1.0f);
        if (Active)
        {
            Surface.FillRectangle(
                { TabRect.X + 8.0f, TabRect.Bottom() - 2.5f, TabRect.Width - 16.0f, 2.5f },
                ParticleInspectorColors::AmberAccent,
                1.0f);
        }

        const float LabelW = WorkspaceAppearance::MeasureWeightedTextWidth(
            Surface,
            Active ? FontWeightClass::SemiBold : FontWeightClass::Medium,
            10.5f,
            TabLabels[TabOrd]);
        const float TextX = TabRect.X + std::max(4.0f, (TabRect.Width - LabelW) * 0.5f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            Active ? FontWeightClass::SemiBold : FontWeightClass::Medium,
            { TextX, TabRect.Y + 7.0f },
            TabRect.Width - 8.0f,
            10.5f,
            Active ? ParticleInspectorColors::InkPrimary : ParticleInspectorColors::InkMuted,
            TabLabels[TabOrd]);

        if (ChromeEnabled && Input.PrimaryPointer.Pressed && Hovered)
        {
            ConfigureActiveTab(static_cast<ParticleInspectorTab>(TabOrd));
        }
    }

    CursorY += TabH + 10.0f;

    // 4. Scrollable Tab Content Area
    const float FooterH = 26.0f;
    const Rectangle2D ScrollViewport{
        Bounds.X,
        CursorY,
        Bounds.Width,
        std::max(60.0f, Bounds.Bottom() - FooterH - CursorY),
    };

    if (ChromeEnabled && ScrollViewport.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 34.0f;
    }
    const float MaxScroll = std::max(0.0f, LastContentH - ScrollViewport.Height);
    ScrollOffsetY         = std::clamp(ScrollOffsetY, 0.0f, MaxScroll);

    DeferredDropdownSheet DeferredOverlay{};
    PaneInputReading      BodyInput = Input;
    if (SheetOpen)
    {
        WorkspaceAppearance::SuppressPanePointerInput(BodyInput);
    }

    Surface.PushClip(ScrollViewport);
    const float StartY = ScrollViewport.Y + 2.0f - ScrollOffsetY;
    float       EndY   = StartY;

    switch (ActiveTab)
    {
        case ParticleInspectorTab::Overview:
            EndY = RenderOverviewTab(Surface, ScrollViewport, InnerX, StartY, InnerW, *Effect, BodyInput, Δt, DeferredOverlay);
            break;
        case ParticleInspectorTab::Source:
            EndY = RenderSourceTab(Surface, ScrollViewport, InnerX, StartY, InnerW, *Effect, BodyInput, DeferredOverlay);
            break;
        case ParticleInspectorTab::MotionAndFx:
            EndY = RenderMotionAndFxTab(Surface, ScrollViewport, InnerX, StartY, InnerW, *Effect, BodyInput, DeferredOverlay);
            break;
        case ParticleInspectorTab::SpecAndExport:
            EndY = RenderSpecAndExportTab(Surface, InnerX, StartY, InnerW, *Effect);
            break;
    }

    LastContentH = (EndY - StartY) + 14.0f;
    WorkspaceAppearance::ConstructVerticalScrollIndicator(Surface, ScrollViewport, LastContentH, ScrollOffsetY, Cursor);
    Surface.PopClip();

    // 5. Status Footer
    const Rectangle2D FooterRect{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FooterRect, ParticleInspectorColors::FooterBg, 0.0f);
    Surface.FillRectangle({ FooterRect.X, FooterRect.Y, FooterRect.Width, 1.0f }, ParticleInspectorColors::HairlineBorder, 0.0f);

    char RevBuf[64]{};
    std::snprintf(RevBuf, sizeof(RevBuf), "Parameters · Revision %llu", static_cast<unsigned long long>(Session->QueryRevision()));
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { InnerX, FooterRect.Y + 6.5f },
        10.2f,
        ParticleInspectorColors::InkMuted,
        RevBuf);
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface,
        FontWeightClass::Medium,
        { InnerX + InnerW, FooterRect.Y + 6.5f },
        10.2f,
        ParticleInspectorColors::EmeraldSoft,
        "ForceFields Coupled");

    // 6. Deferred Dropdown Option Sheet (rendered above scroll clip)
    if (OpenDropdown != OpenDropdownCategory::None && DeferredOverlay.Category == OpenDropdown)
    {
        RenderDeferredDropdown(Surface, Bounds, DeferredOverlay, *Effect, Input);
    }

    Surface.PopClip();
    (void)ControlTooltip::PresentHoveredControlTooltip(Surface, Bounds, Input.PrimaryPointer);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              TAB 0 — OVERVIEW
//------------------------------------------------------------------------------------------------------------------------

float ParticleInspectorPanel::RenderOverviewTab(
    PixelSpace&                Surface,
    Rectangle2D                ScrollViewport,
    float                      LeftX,
    float                      TopY,
    float                      InnerW,
    ParticleEffectRecordEntry& Effect,
    const PaneInputReading&    Input,
    double                     Δt,
    DeferredDropdownSheet&     OutOverlay)
{
    const float ContentX = LeftX + 12.0f;
    const float ContentW = InnerW - 24.0f;
    const ControlPointer& Pointer = Input.PrimaryPointer;
    float Y = TopY;
    bool  Modified = false;

    // Section 01: Effect Summary, Coupled ForceField Sample & Quick Actions
    const float Sec1H = 148.0f;
    float Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec1H, "Effect summary & actions", "01 / OVERVIEW", ParticleInspectorColors::AmberAccent);

    const SpatialForceSample CoupledSample = Session->SampleCoupledForceAt(Effect.OriginMeters);
    const float CoupledWindMs              = VectorLength(CoupledSample.AdvectionVelocity);

    const float TileGap = 6.0f;
    const float TileW   = (ContentW - TileGap * 2.0f) / 3.0f;
    char CapBuf[24]{};
    char RateBuf[24]{};
    char WindBuf[24]{};
    std::snprintf(CapBuf, sizeof(CapBuf), "%u", Effect.Capacity);
    std::snprintf(RateBuf, sizeof(RateBuf), "%.0f /s", Effect.BirthRatePerSec);
    std::snprintf(WindBuf, sizeof(WindBuf), "%.1f m/s", CoupledWindMs);

    RenderMetricTile(Surface, { ContentX, Sy, TileW, 40.0f }, "CAPACITY", CapBuf, ParticleInspectorColors::InkPrimary);
    RenderMetricTile(Surface, { ContentX + TileW + TileGap, Sy, TileW, 40.0f }, "BIRTH RATE", RateBuf, ParticleInspectorColors::AmberAccent);
    RenderMetricTile(Surface, { ContentX + (TileW + TileGap) * 2.0f, Sy, TileW, 40.0f }, "LOCAL FORCE", WindBuf, ParticleInspectorColors::EmeraldSoft);
    Sy += 50.0f;

    // Quick Action Buttons: Burst Now | Duplicate
    const float BtnGap = 8.0f;
    const float BtnW   = (ContentW - BtnGap) * 0.5f;
    ButtonDescriptor BurstBtn{};
    BurstBtn.Label    = "Burst Now";
    BurstBtn.Tone     = ButtonTone::Primary;
    BurstBtn.FontSize = 11.5f;
    BurstBtn.PaddingX = 12.0f;
    if (CapsuleButton::Construct(Surface, { ContentX, Sy, BtnW, 30.0f }, BurstBtn, Pointer).Clicked)
    {
        Session->TriggerEffectBurst(Effect.UniqueToken);
    }

    ButtonDescriptor DupBtn{};
    DupBtn.Label    = "Duplicate Effect";
    DupBtn.Tone     = ButtonTone::Secondary;
    DupBtn.FontSize = 11.5f;
    DupBtn.PaddingX = 12.0f;
    if (CapsuleButton::Construct(Surface, { ContentX + BtnW + BtnGap, Sy, BtnW, 30.0f }, DupBtn, Pointer).Clicked)
    {
        (void)Session->DuplicateEffect(Effect.UniqueToken);
    }

    Y += Sec1H + 10.0f;

    // Section 02: Color Spectrum Ramp & Drivers
    const float Sec2H = 212.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec2H, "Color spectrum & ramp", "02 / COLOR RAMP", ParticleInspectorColors::EmeraldSoft);

    ConstructSpectrumRibbon(Surface, { ContentX, Sy, ContentW, 20.0f }, Effect);
    Sy += 28.0f;

    // ColorBy dropdown
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Color driver");
    const Rectangle2D ColorByRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
    if (DropdownSelector::ConstructButton(
            Surface,
            ColorByRect,
            ParticlePresetRegistry::QueryColorByLabel(Effect.ColorBy),
            OpenDropdown == OpenDropdownCategory::ColorBy,
            Pointer)
            .Clicked)
    {
        OpenDropdown = (OpenDropdown == OpenDropdownCategory::ColorBy) ? OpenDropdownCategory::None : OpenDropdownCategory::ColorBy;
    }
    if (OpenDropdown == OpenDropdownCategory::ColorBy)
    {
        OutOverlay = { OpenDropdownCategory::ColorBy, ColorByRect };
    }
    Sy += 34.0f;

    // Ramp wrap dropdown
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Ramp wrap");
    const Rectangle2D WrapRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
    if (DropdownSelector::ConstructButton(
            Surface,
            WrapRect,
            ParticlePresetRegistry::QueryRampWrapLabel(Effect.RampWrap),
            OpenDropdown == OpenDropdownCategory::RampWrap,
            Pointer)
            .Clicked)
    {
        OpenDropdown = (OpenDropdown == OpenDropdownCategory::RampWrap) ? OpenDropdownCategory::None : OpenDropdownCategory::RampWrap;
    }
    if (OpenDropdown == OpenDropdownCategory::RampWrap)
    {
        OutOverlay = { OpenDropdownCategory::RampWrap, WrapRect };
    }
    Sy += 34.0f;

    // Interpolation segmented bar
    const char* InterpLabels[3] = { "Linear", "Smooth", "Stepped" };
    uint32_t InterpIdx          = static_cast<uint32_t>(Effect.RampInterp);
    if (CapsuleButton::ConstructSegmented(Surface, { ContentX, Sy, ContentW, 24.0f }, InterpLabels, 3u, InterpIdx, Pointer, InterpIdx).Clicked)
    {
        Effect.RampInterp = static_cast<ParticleRampInterpCategory>(InterpIdx);
        Modified          = true;
    }
    Sy += 32.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 101, "Ramp phase offset", -1.0f, 1.0f, Effect.RampPhaseOffset, "", 2, Pointer, ActiveSliderId);

    Y += Sec2H + 10.0f;

    // Section 03: Contextual Domain Authoring Section (ZERO Quarantine domains!)
    if (Effect.AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux)
    {
        const float Sec3H = 228.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Magnetic Rosensweig ferrofluid", "03 / FERROFLUID", ColorRgba{ 158u, 192u, 224u, 255u });

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Vessel form");
        const Rectangle2D VesselRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
        const char* VesselLabel = (Effect.FerrofluidVessel == 2u) ? "Floating Magnetic Orb" : "Petri Dish Spikes";
        if (DropdownSelector::ConstructButton(Surface, VesselRect, VesselLabel, OpenDropdown == OpenDropdownCategory::FerrofluidVessel, Pointer).Clicked)
        {
            OpenDropdown = (OpenDropdown == OpenDropdownCategory::FerrofluidVessel) ? OpenDropdownCategory::None : OpenDropdownCategory::FerrofluidVessel;
        }
        if (OpenDropdown == OpenDropdownCategory::FerrofluidVessel)
        {
            OutOverlay = { OpenDropdownCategory::FerrofluidVessel, VesselRect };
        }
        Sy += 34.0f;

        float SpikeFloat = static_cast<float>(Effect.FerroSpikeCount);
        if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 110, "Magnetic spike count", 32.0f, 560.0f, SpikeFloat, "", 0, Pointer, ActiveSliderId))
        {
            Effect.FerroSpikeCount = static_cast<uint32_t>(std::lround(SpikeFloat));
            Modified               = true;
        }
        Sy += 38.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 111, "Magnet orbit speed", 0.2f, 5.0f, Effect.MagnetOrbitSpeedRadS, "rad/s", 2, Pointer, ActiveSliderId);
        Sy += 38.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 112, "Max cusp height", 0.2f, 1.8f, Effect.MaxSpikeHeightMeters, "m", 2, Pointer, ActiveSliderId);
        Sy += 38.0f;

        Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Manual magnet pole XYZ", Effect.ManualMagnetPole, Pointer);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        const float Sec3H = 228.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Fireworks shells & sky text", "03 / PYROTECHNICS", ParticleInspectorColors::AmberAccent);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Shell pattern");
        const Rectangle2D PatRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
        if (DropdownSelector::ConstructButton(
                Surface,
                PatRect,
                ParticlePresetRegistry::QueryFireworkPatternLabel(Effect.FireworkPattern),
                OpenDropdown == OpenDropdownCategory::FireworkPattern,
                Pointer)
                .Clicked)
        {
            OpenDropdown = (OpenDropdown == OpenDropdownCategory::FireworkPattern) ? OpenDropdownCategory::None : OpenDropdownCategory::FireworkPattern;
        }
        if (OpenDropdown == OpenDropdownCategory::FireworkPattern)
        {
            OutOverlay = { OpenDropdownCategory::FireworkPattern, PatRect };
        }
        Sy += 34.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy }, 10.8f, ParticleInspectorColors::InkLabel, "Sky-writing custom text");
        Sy += 16.0f;
        if (SyncedSkyTextToken != Effect.UniqueToken)
        {
            SkyTextEditor.Begin(Effect.FireworkSkyText.c_str());
            SkyTextEditor.Active = false;
            SyncedSkyTextToken   = Effect.UniqueToken;
        }
        if (SkyTextEditor.Active)
        {
            WorkspaceAppearance::ForwardInputToTextBuffer(SkyTextEditor, Input, Δt);
            Effect.FireworkSkyText.assign(SkyTextEditor.Text, SkyTextEditor.Length);
        }
        const Rectangle2D SkyRect{ ContentX, Sy, ContentW, 28.0f };
        Surface.FillRectangle(SkyRect, ParticleInspectorColors::RecessDark, 6.0f);
        Surface.StrokeRectangle(
            SkyRect,
            SkyTextEditor.Active ? ParticleInspectorColors::AmberAccent : ParticleInspectorColors::SubtleBorder,
            6.0f,
            1.0f);
        (void)TextInputField::Construct(Surface, { SkyRect.X + 4.0f, SkyRect.Y + 1.0f, SkyRect.Width - 8.0f, 26.0f }, SkyTextEditor, Pointer, 12.0f);
        if (Pointer.Enabled && Pointer.Pressed && ScrollViewport.Contains({ Pointer.X, Pointer.Y }))
        {
            SkyTextEditor.Active = SkyRect.Contains({ Pointer.X, Pointer.Y });
        }
        Sy += 34.0f;

        // Quick New Year's sky-text words
        static constexpr std::array<const char*, 4u> QuickWords = {{ "2026", "NEW YEAR", "FRONTIER", "BOOM" }};
        float WordX = ContentX;
        for (const char* Word : QuickWords)
        {
            const float BtnW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 9.5f, Word, true) + 12.0f;
            const Rectangle2D WordBtn{ WordX, Sy, BtnW, 20.0f };
            const bool Active  = (Effect.FireworkSkyText == Word);
            const bool Hovered = Pointer.Enabled && WordBtn.Contains({ Pointer.X, Pointer.Y });
            Surface.FillRectangle(
                WordBtn,
                Active ? ColorRgba{ 255u, 180u, 84u, 42u } : (Hovered ? ParticleInspectorColors::TabHoverBg : ParticleInspectorColors::SubWellBg),
                5.0f);
            Surface.StrokeRectangle(WordBtn, Active ? ParticleInspectorColors::AmberAccent : ParticleInspectorColors::HairlineBorder, 5.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::SemiBold,
                { WordBtn.X + 6.0f, WordBtn.Y + 4.0f },
                9.5f,
                Active ? ParticleInspectorColors::AmberAccent : ParticleInspectorColors::InkLabel,
                Word,
                true);
            if (Pointer.Enabled && Pointer.Pressed && Hovered)
            {
                Effect.FireworkSkyText = Word;
                SkyTextEditor.Begin(Word);
                Modified = true;
            }
            WordX += BtnW + 5.0f;
        }
        Sy += 30.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 120, "Burst interval", 0.2f, 8.0f, Effect.BurstIntervalSec, "s", 1, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::RiggingAndFlora)
    {
        const float Sec3H = 266.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Dynamic rigging & flora curves", "03 / RIGGING", ColorRgba{ 126u, 198u, 118u, 255u });

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Architecture");
        const Rectangle2D ArchRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
        if (DropdownSelector::ConstructButton(
                Surface,
                ArchRect,
                ParticlePresetRegistry::QueryRiggingArchitectureLabel(Effect.RiggingArchitecture),
                OpenDropdown == OpenDropdownCategory::RiggingArchitecture,
                Pointer)
                .Clicked)
        {
            OpenDropdown = (OpenDropdown == OpenDropdownCategory::RiggingArchitecture) ? OpenDropdownCategory::None : OpenDropdownCategory::RiggingArchitecture;
        }
        if (OpenDropdown == OpenDropdownCategory::RiggingArchitecture)
        {
            OutOverlay = { OpenDropdownCategory::RiggingArchitecture, ArchRect };
        }
        Sy += 34.0f;

        float RowsF = static_cast<float>(Effect.StrandRows);
        if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 130, "Strands / canopy limbs", 4.0f, 48.0f, RowsF, "", 0, Pointer, ActiveSliderId))
        {
            Effect.StrandRows = static_cast<uint32_t>(std::lround(RowsF));
            Modified          = true;
        }
        Sy += 38.0f;

        float SegsF = static_cast<float>(Effect.CurveSegments);
        if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 131, "Curve segments per strand", 2.0f, 32.0f, SegsF, "segs", 0, Pointer, ActiveSliderId))
        {
            Effect.CurveSegments = static_cast<uint32_t>(std::lround(SegsF));
            Modified             = true;
        }
        Sy += 38.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 132, "Elastic stiffness", 0.2f, 3.0f, Effect.ElasticStiffness, "x", 2, Pointer, ActiveSliderId);
        Sy += 38.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 133, "Impact tear speed", 0.0f, 18.0f, Effect.TearSpeedMs, "m/s", 1, Pointer, ActiveSliderId);
        Sy += 38.0f;

        Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Show curve segment debug", Effect.CurveDebugEnabled, Pointer);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FractureAndCracks)
    {
        const float Sec3H = 160.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Glacial & seismic fracture", "03 / CRACKS", ColorRgba{ 126u, 206u, 240u, 255u });

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Fracture law");
        const Rectangle2D FracRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
        const char* FracLabel = (Effect.FracturePattern == 2u) ? "Seismic Basalt Quake" : "Glacial Ice Sheet";
        if (DropdownSelector::ConstructButton(Surface, FracRect, FracLabel, OpenDropdown == OpenDropdownCategory::FracturePattern, Pointer).Clicked)
        {
            OpenDropdown = (OpenDropdown == OpenDropdownCategory::FracturePattern) ? OpenDropdownCategory::None : OpenDropdownCategory::FracturePattern;
        }
        if (OpenDropdown == OpenDropdownCategory::FracturePattern)
        {
            OutOverlay = { OpenDropdownCategory::FracturePattern, FracRect };
        }
        Sy += 34.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 140, "Incision depth", 0.05f, 1.5f, Effect.FractureDepthMeters, "m", 2, Pointer, ActiveSliderId);
        Sy += 38.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 141, "Branching density", 0.3f, 3.0f, Effect.CrackBranchDensity, "x", 2, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::MoGraphEffector)
    {
        const float Sec3H = 124.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "MoGraph effector lattice", "03 / MOGRAPH", ColorRgba{ 244u, 118u, 102u, 255u });
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 150, "Effector falloff radius", 0.3f, 6.0f, Effect.EffectorFalloffMeters, "m", 2, Pointer, ActiveSliderId);
        Sy += 38.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 151, "Height extrusion gain", 0.1f, 4.0f, Effect.EffectorExtrusionMeters, "m", 2, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::EnergyShield)
    {
        const float Sec3H = 160.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Energy shield barrier", "03 / SHIELD", ColorRgba{ 104u, 226u, 174u, 255u });
        static constexpr std::array<const char*, 4u> ShieldLabels = {{ "Sphere", "Hex Dome", "Prism Barrier", "Cylinder" }};
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Barrier shape");
        const Rectangle2D ShRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
        if (DropdownSelector::ConstructButton(
                Surface,
                ShRect,
                ShieldLabels[Effect.ShieldGeometry < 4u ? Effect.ShieldGeometry : 0u],
                OpenDropdown == OpenDropdownCategory::ShieldGeometry,
                Pointer)
                .Clicked)
        {
            OpenDropdown = (OpenDropdown == OpenDropdownCategory::ShieldGeometry) ? OpenDropdownCategory::None : OpenDropdownCategory::ShieldGeometry;
        }
        if (OpenDropdown == OpenDropdownCategory::ShieldGeometry)
        {
            OutOverlay = { OpenDropdownCategory::ShieldGeometry, ShRect };
        }
        Sy += 34.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 160, "Impact ripple speed", 0.5f, 8.0f, Effect.ShieldRippleSpeed, "Hz", 2, Pointer, ActiveSliderId);
        Sy += 38.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 161, "Tessellation density", 4.0f, 36.0f, Effect.ShieldCellDensity, "/m", 1, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::BoidsFlock)
    {
        const float Sec3H = 160.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Boids flock & murmuration", "03 / FLOCK", ColorRgba{ 166u, 214u, 168u, 255u });
        static constexpr std::array<const char*, 3u> CreatureLabels = {{ "Birds / Starlings", "Butterflies / Wisps", "Fish School" }};
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Creature form");
        const Rectangle2D BoRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
        if (DropdownSelector::ConstructButton(
                Surface,
                BoRect,
                CreatureLabels[Effect.BoidsCreatureVariant < 3u ? Effect.BoidsCreatureVariant : 0u],
                OpenDropdown == OpenDropdownCategory::BoidsCreature,
                Pointer)
                .Clicked)
        {
            OpenDropdown = (OpenDropdown == OpenDropdownCategory::BoidsCreature) ? OpenDropdownCategory::None : OpenDropdownCategory::BoidsCreature;
        }
        if (OpenDropdown == OpenDropdownCategory::BoidsCreature)
        {
            OutOverlay = { OpenDropdownCategory::BoidsCreature, BoRect };
        }
        Sy += 34.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 170, "Perception radius", 0.3f, 4.0f, Effect.BoidsPerceptionMeters, "m", 2, Pointer, ActiveSliderId);
        Sy += 38.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 171, "Cruise flight speed", 0.5f, 9.0f, Effect.BoidsCruiseSpeedMs, "m/s", 1, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }

    if (Modified)
    {
        Session->IncrementRevision();
    }
    return Y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                               TAB 1 — SOURCE
//------------------------------------------------------------------------------------------------------------------------

float ParticleInspectorPanel::RenderSourceTab(
    PixelSpace&                Surface,
    Rectangle2D                ScrollViewport,
    float                      LeftX,
    float                      TopY,
    float                      InnerW,
    ParticleEffectRecordEntry& Effect,
    const PaneInputReading&    Input,
    DeferredDropdownSheet&     OutOverlay)
{
    (void)ScrollViewport;
    const float ContentX = LeftX + 12.0f;
    const float ContentW = InnerW - 24.0f;
    const ControlPointer& Pointer = Input.PrimaryPointer;
    float Y        = TopY;
    bool  Modified = false;

    // Section 01: Release Volume & Capacity
    const float Sec1H = 248.0f;
    float Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec1H, "Release volume & slots", "01 / SOURCE", ParticleInspectorColors::AmberAccent);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Volume shape");
    const Rectangle2D VolRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
    if (DropdownSelector::ConstructButton(
            Surface,
            VolRect,
            ParticlePresetRegistry::QueryReleaseVolumeLabel(Effect.ReleaseVolume),
            OpenDropdown == OpenDropdownCategory::ReleaseVolume,
            Pointer)
            .Clicked)
    {
        OpenDropdown = (OpenDropdown == OpenDropdownCategory::ReleaseVolume) ? OpenDropdownCategory::None : OpenDropdownCategory::ReleaseVolume;
    }
    if (OpenDropdown == OpenDropdownCategory::ReleaseVolume)
    {
        OutOverlay = { OpenDropdownCategory::ReleaseVolume, VolRect };
    }
    Sy += 34.0f;

    Modified |= RenderAxisVectorRow(Surface, ContentX, Sy, ContentW, 200, "Origin (+Z Up)", Effect.OriginMeters, 0.03f, "m", Pointer, ActiveAxisDragId, AxisDragStartX, AxisDragOriginScalar);
    Sy += 48.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 210, "Volume radius", 0.05f, 8.0f, Effect.RadiusMeters, "m", 2, Pointer, ActiveSliderId);
    Sy += 38.0f;

    float CapFloat = static_cast<float>(Effect.Capacity);
    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 211, "Particle capacity", 32.0f, 12000.0f, CapFloat, "slots", 0, Pointer, ActiveSliderId))
    {
        Effect.Capacity = static_cast<uint32_t>(std::lround(CapFloat));
        Modified        = true;
    }
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 212, "Continuous birth rate", 0.0f, 2000.0f, Effect.BirthRatePerSec, "/s", 0, Pointer, ActiveSliderId);
    Y += Sec1H + 10.0f;

    // Section 02: Launch Kinematics & Lifetime
    const float Sec2H = 290.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec2H, "Launch velocity & lifetime", "02 / KINEMATICS", ParticleInspectorColors::EmeraldSoft);

    Modified |= RenderAxisVectorRow(Surface, ContentX, Sy, ContentW, 220, "Launch direction", Effect.LaunchDirection, 0.01f, "dir", Pointer, ActiveAxisDragId, AxisDragStartX, AxisDragOriginScalar);
    Sy += 48.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 230, "Cone spread", 0.0f, 3.1416f, Effect.ConeSpreadRad, "rad", 2, Pointer, ActiveSliderId);
    Sy += 38.0f;

    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 231, "Speed minimum", 0.0f, 20.0f, Effect.SpeedMinMs, "m/s", 2, Pointer, ActiveSliderId))
    {
        Effect.SpeedMaxMs = std::max(Effect.SpeedMaxMs, Effect.SpeedMinMs);
        Modified          = true;
    }
    Sy += 38.0f;

    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 232, "Speed maximum", 0.0f, 20.0f, Effect.SpeedMaxMs, "m/s", 2, Pointer, ActiveSliderId))
    {
        Effect.SpeedMinMs = std::min(Effect.SpeedMinMs, Effect.SpeedMaxMs);
        Modified          = true;
    }
    Sy += 38.0f;

    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 233, "Lifetime minimum", 0.05f, 20.0f, Effect.LifeMinSec, "s", 2, Pointer, ActiveSliderId))
    {
        Effect.LifeMaxSec = std::max(Effect.LifeMaxSec, Effect.LifeMinSec);
        Modified          = true;
    }
    Sy += 38.0f;

    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 234, "Lifetime maximum", 0.05f, 20.0f, Effect.LifeMaxSec, "s", 2, Pointer, ActiveSliderId))
    {
        Effect.LifeMinSec = std::min(Effect.LifeMinSec, Effect.LifeMaxSec);
        Modified          = true;
    }
    Y += Sec2H + 10.0f;

    // Section 03: Timed Burst Cadence
    const float Sec3H = 152.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Timed burst cadence", "03 / BURSTS", ParticleInspectorColors::AmberAccent);

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 240, "Burst interval (0 = off)", 0.0f, 12.0f, Effect.BurstIntervalSec, "s", 1, Pointer, ActiveSliderId);
    Sy += 38.0f;

    float BurstFloat = static_cast<float>(Effect.BurstCount);
    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 241, "Particles per burst", 1.0f, 600.0f, BurstFloat, "", 0, Pointer, ActiveSliderId))
    {
        Effect.BurstCount = static_cast<uint32_t>(std::lround(BurstFloat));
        Modified          = true;
    }
    Sy += 38.0f;

    Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Release bursts at local origin", Effect.BurstAtOrigin, Pointer);
    Y += Sec3H + 10.0f;

    if (Modified)
    {
        Session->IncrementRevision();
    }
    return Y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            TAB 2 — MOTION & FX
//------------------------------------------------------------------------------------------------------------------------

float ParticleInspectorPanel::RenderMotionAndFxTab(
    PixelSpace&                Surface,
    Rectangle2D                ScrollViewport,
    float                      LeftX,
    float                      TopY,
    float                      InnerW,
    ParticleEffectRecordEntry& Effect,
    const PaneInputReading&    Input,
    DeferredDropdownSheet&     OutOverlay)
{
    (void)ScrollViewport;
    const float ContentX = LeftX + 12.0f;
    const float ContentW = InnerW - 24.0f;
    const ControlPointer& Pointer = Input.PrimaryPointer;
    float Y        = TopY;
    bool  Modified = false;

    // Section 01: Particle Silhouette, Blend & Sizing
    const float Sec1H = 250.0f;
    float Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec1H, "Shading & visual silhouette", "01 / SHADING", ParticleInspectorColors::AmberAccent);

    ConstructSpectrumRibbon(Surface, { ContentX, Sy, ContentW, 14.0f }, Effect);
    Sy += 22.0f;

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.0f, ParticleInspectorColors::InkLabel, "Silhouette");
    const Rectangle2D SilRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 26.0f };
    if (DropdownSelector::ConstructButton(
            Surface,
            SilRect,
            ParticlePresetRegistry::QuerySilhouetteLabel(Effect.Silhouette),
            OpenDropdown == OpenDropdownCategory::Silhouette,
            Pointer)
            .Clicked)
    {
        OpenDropdown = (OpenDropdown == OpenDropdownCategory::Silhouette) ? OpenDropdownCategory::None : OpenDropdownCategory::Silhouette;
    }
    if (OpenDropdown == OpenDropdownCategory::Silhouette)
    {
        OutOverlay = { OpenDropdownCategory::Silhouette, SilRect };
    }
    Sy += 34.0f;

    const char* BlendLabels[2] = { "Additive Glow", "Premultiplied Alpha" };
    uint32_t BlendIdx          = static_cast<uint32_t>(Effect.BlendCategory);
    if (CapsuleButton::ConstructSegmented(Surface, { ContentX, Sy, ContentW, 25.0f }, BlendLabels, 2u, BlendIdx, Pointer, BlendIdx).Clicked)
    {
        Effect.BlendCategory = static_cast<ParticleBlendCategory>(BlendIdx);
        Modified             = true;
    }
    Sy += 33.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 301, "Size at birth", 0.005f, 3.0f, Effect.SizeStartMeters, "m", 3, Pointer, ActiveSliderId);
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 302, "Size at death", 0.005f, 3.0f, Effect.SizeEndMeters, "m", 3, Pointer, ActiveSliderId);
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 303, "Size multiplier", 0.1f, 4.0f, Effect.SizeMultiplier, "x", 2, Pointer, ActiveSliderId);
    Y += Sec1H + 10.0f;

    // Section 02: Physics & Direct C++ ForceFields Coupling
    const float Sec2H = 384.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec2H, "Physics & ForceFields coupling", "02 / FORCEFIELDS", ParticleInspectorColors::EmeraldSoft);

    // Live coupled ForceFieldAccumulator reading at Effect.OriginMeters
    const SpatialForceSample Sample = Session->SampleCoupledForceAt(Effect.OriginMeters);
    const float AdvSpeed            = VectorLength(Sample.AdvectionVelocity);
    const float AccelMag            = VectorLength(Sample.LinearAcceleration);
    char AdvBuf[48]{};
    char AccBuf[48]{};
    std::snprintf(AdvBuf, sizeof(AdvBuf), "%.2f m/s", AdvSpeed * Effect.ForceCouplingGain);
    std::snprintf(AccBuf, sizeof(AccBuf), "%.2f m/s2", AccelMag * Effect.ForceCouplingGain);

    const float HalfW = (ContentW - 6.0f) * 0.5f;
    RenderMetricTile(Surface, { ContentX, Sy, HalfW, 40.0f }, "COUPLED ADVECTION", AdvBuf, ParticleInspectorColors::EmeraldSoft);
    RenderMetricTile(Surface, { ContentX + HalfW + 6.0f, Sy, HalfW, 40.0f }, "COUPLED ACCEL", AccBuf, ParticleInspectorColors::TealCode);
    Sy += 50.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 310, "ForceFields coupling gain", 0.0f, 3.0f, Effect.ForceCouplingGain, "x", 2, Pointer, ActiveSliderId);
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 311, "Gravity scale", -2.0f, 3.0f, Effect.GravityMultiplier, "g", 2, Pointer, ActiveSliderId);
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 312, "Aerodynamic drag", 0.0f, 4.0f, Effect.DragCoefficient, "/s", 2, Pointer, ActiveSliderId);
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 313, "Local curl amplitude", 0.0f, 5.0f, Effect.CurlAmplitude, "m/s", 2, Pointer, ActiveSliderId);
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 314, "Local curl frequency", 0.1f, 4.0f, Effect.CurlFrequency, "/m", 2, Pointer, ActiveSliderId);
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 315, "Floor restitution", 0.0f, 1.0f, Effect.FloorRestitution, "", 2, Pointer, ActiveSliderId);
    Sy += 38.0f;

    Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Interactive ball collisions", Effect.BallInteractionEnabled, Pointer);
    Sy += 28.0f;

    Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Tapered 3D ribbon trails", Effect.RibbonTrailsEnabled, Pointer);
    Y += Sec2H + 10.0f;

    if (Modified)
    {
        Session->IncrementRevision();
    }
    return Y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TAB 3 — SPEC & EXPORT
//------------------------------------------------------------------------------------------------------------------------

float ParticleInspectorPanel::RenderSpecAndExportTab(
    PixelSpace&                      Surface,
    float                            LeftX,
    float                            TopY,
    float                            InnerW,
    const ParticleEffectRecordEntry& Effect)
{
    const float ContentX = LeftX + 12.0f;
    const float ContentW = InnerW - 24.0f;
    float Y = TopY;

    // Section 01: Capacity & Coupled ForceFields Budget
    const auto ForceSpan    = Session->QueryForceSources();
    const uint32_t SlotKiB  = std::max(1u, (Effect.Capacity * 80u) / 1024u);
    const float Sec1H       = 104.0f + static_cast<float>(ForceSpan.size()) * 24.0f;
    float Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec1H, "Memory budget & coupled forces", "01 / SPEC", ParticleInspectorColors::EmeraldSoft);

    const float TileGap = 6.0f;
    const float TileW   = (ContentW - TileGap * 2.0f) / 3.0f;
    char MemBuf[24]{};
    char FrcBuf[24]{};
    char SeedBuf[24]{};
    std::snprintf(MemBuf, sizeof(MemBuf), "%u KiB", SlotKiB);
    std::snprintf(FrcBuf, sizeof(FrcBuf), "%u active", Session->QueryForceAccumulator().QueryActiveSourceCount());
    std::snprintf(SeedBuf, sizeof(SeedBuf), "#%u", Effect.DeterministicSeed);

    RenderMetricTile(Surface, { ContentX, Sy, TileW, 40.0f }, "BUFFER SIZE", MemBuf, ParticleInspectorColors::InkPrimary);
    RenderMetricTile(Surface, { ContentX + TileW + TileGap, Sy, TileW, 40.0f }, "FORCE LAWS", FrcBuf, ParticleInspectorColors::EmeraldSoft);
    RenderMetricTile(Surface, { ContentX + (TileW + TileGap) * 2.0f, Sy, TileW, 40.0f }, "RNG SEED", SeedBuf, ParticleInspectorColors::AmberAccent);
    Sy += 50.0f;

    for (const ForceSourceRecordEntry& Force : ForceSpan)
    {
        const Rectangle2D RowRect{ ContentX, Sy, ContentW, 20.0f };
        Surface.FillRectangle(RowRect, ParticleInspectorColors::SubWellBg, 4.0f);
        const ColorRgba Tint = WorkspaceAppearance::QueryForceLawTint(static_cast<uint32_t>(Force.Model));
        Surface.FillCircle({ RowRect.X + 9.0f, RowRect.Y + 10.0f }, 3.5f, Force.Enabled ? Tint : ParticleInspectorColors::InkDim);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Medium,
            { RowRect.X + 18.0f, RowRect.Y + 3.5f },
            RowRect.Width - 102.0f,
            10.2f,
            Force.Enabled ? ParticleInspectorColors::InkHeading : ParticleInspectorColors::InkDim,
            Force.Label);
        char GainBuf[40]{};
        std::snprintf(GainBuf, sizeof(GainBuf), "%.1f m/s · %s", Force.Strength, Force.Enabled ? "ON" : "OFF");
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { RowRect.Right() - 6.0f, RowRect.Y + 3.5f },
            9.8f,
            Force.Enabled ? ParticleInspectorColors::EmeraldSoft : ParticleInspectorColors::InkDim,
            GainBuf,
            true);
        Sy += 24.0f;
    }

    Y += Sec1H + 10.0f;

    // Section 02: Live .ParticleSpace TOML Descriptor Preview
    const std::string Toml = Session->SerializeSelectedEffectTomlPreview();
    std::vector<std::string> Lines{};
    {
        std::istringstream Stream(Toml);
        std::string Line;
        while (std::getline(Stream, Line))
        {
            Lines.push_back(std::move(Line));
        }
    }

    const float LineH = 14.5f;
    const float WellH = std::max(120.0f, static_cast<float>(Lines.size()) * LineH + 18.0f);
    const float Sec2H = WellH + 52.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec2H, ".ParticleSpace TOML preview", "02 / TOML", ParticleInspectorColors::TealCode);

    const Rectangle2D CodeWell{ ContentX, Sy, ContentW, WellH };
    Surface.FillRectangle(CodeWell, ParticleInspectorColors::RecessDark, 7.0f);
    Surface.StrokeRectangle(CodeWell, ParticleInspectorColors::SubtleBorder, 7.0f, 1.0f);

    float LineY = CodeWell.Y + 9.0f;
    for (const std::string& Line : Lines)
    {
        ColorRgba LineInk = ParticleInspectorColors::InkLabel;
        if (!Line.empty() && Line[0] == '[')
        {
            LineInk = ParticleInspectorColors::AmberAccent;
        }
        else if (Line.find('=') != std::string::npos)
        {
            LineInk = ParticleInspectorColors::TealCode;
        }
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { CodeWell.X + 9.0f, LineY },
            CodeWell.Width - 18.0f,
            10.2f,
            LineInk,
            Line,
            true);
        LineY += LineH;
    }

    Y += Sec2H + 10.0f;
    return Y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           DEFERRED DROPDOWN OVERLAY
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::RenderDeferredDropdown(
    PixelSpace&                  Surface,
    Rectangle2D                  PaneBounds,
    const DeferredDropdownSheet& Overlay,
    ParticleEffectRecordEntry&   Effect,
    const PaneInputReading&      Input)
{
    std::vector<const char*> Options{};
    uint32_t                 SelectedIndex = 0u;

    switch (Overlay.Category)
    {
        case OpenDropdownCategory::ReleaseVolume:
            for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(ParticleReleaseVolume::Count); ++Ord)
            {
                Options.push_back(ParticlePresetRegistry::QueryReleaseVolumeLabel(static_cast<ParticleReleaseVolume>(Ord)));
            }
            SelectedIndex = static_cast<uint32_t>(Effect.ReleaseVolume);
            break;

        case OpenDropdownCategory::Silhouette:
            for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(ParticleVisualSilhouette::Count); ++Ord)
            {
                Options.push_back(ParticlePresetRegistry::QuerySilhouetteLabel(static_cast<ParticleVisualSilhouette>(Ord)));
            }
            SelectedIndex = static_cast<uint32_t>(Effect.Silhouette);
            break;

        case OpenDropdownCategory::ColorBy:
            for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(ParticleColorByCategory::Count); ++Ord)
            {
                Options.push_back(ParticlePresetRegistry::QueryColorByLabel(static_cast<ParticleColorByCategory>(Ord)));
            }
            SelectedIndex = static_cast<uint32_t>(Effect.ColorBy);
            break;

        case OpenDropdownCategory::RampWrap:
            for (uint32_t Ord = 0u; Ord < 3u; ++Ord)
            {
                Options.push_back(ParticlePresetRegistry::QueryRampWrapLabel(static_cast<ParticleRampWrapCategory>(Ord)));
            }
            SelectedIndex = static_cast<uint32_t>(Effect.RampWrap);
            break;

        case OpenDropdownCategory::FireworkPattern:
            for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(FireworkPatternCategory::Count); ++Ord)
            {
                Options.push_back(ParticlePresetRegistry::QueryFireworkPatternLabel(static_cast<FireworkPatternCategory>(Ord)));
            }
            SelectedIndex = static_cast<uint32_t>(Effect.FireworkPattern);
            break;

        case OpenDropdownCategory::RiggingArchitecture:
            for (uint8_t Ord = 1u; Ord <= 9u; ++Ord)
            {
                Options.push_back(ParticlePresetRegistry::QueryRiggingArchitectureLabel(Ord));
            }
            SelectedIndex = Effect.RiggingArchitecture >= 1u ? static_cast<uint32_t>(Effect.RiggingArchitecture - 1u) : 0u;
            break;

        case OpenDropdownCategory::FerrofluidVessel:
            Options       = { "Petri Dish Spikes", "Floating Magnetic Orb" };
            SelectedIndex = (Effect.FerrofluidVessel == 2u) ? 1u : 0u;
            break;

        case OpenDropdownCategory::FracturePattern:
            Options       = { "Glacial Ice Sheet", "Seismic Basalt Quake" };
            SelectedIndex = (Effect.FracturePattern == 2u) ? 1u : 0u;
            break;

        case OpenDropdownCategory::ShieldGeometry:
            Options       = { "Sphere", "Hex Dome", "Prism Barrier", "Cylinder" };
            SelectedIndex = Effect.ShieldGeometry < 4u ? Effect.ShieldGeometry : 0u;
            break;

        case OpenDropdownCategory::BoidsCreature:
            Options       = { "Birds / Starlings", "Butterflies / Wisps", "Fish School" };
            SelectedIndex = Effect.BoidsCreatureVariant < 3u ? Effect.BoidsCreatureVariant : 0u;
            break;

        default:
            OpenDropdown = OpenDropdownCategory::None;
            return;
    }

    const uint32_t OptionCount = static_cast<uint32_t>(Options.size());
    uint32_t       ChosenIndex = SelectedIndex;

    // Clamp trigger so option sheet fits vertically within the pane
    Rectangle2D AdjustedTrigger = Overlay.TriggerBounds;
    const Rectangle2D NaturalSheet = DropdownSelector::QuerySheetBounds(AdjustedTrigger, OptionCount);
    if (NaturalSheet.Bottom() > PaneBounds.Bottom() - 8.0f)
    {
        const float ShiftUp = NaturalSheet.Bottom() - (PaneBounds.Bottom() - 8.0f);
        AdjustedTrigger.Y   = std::max(PaneBounds.Y + 8.0f, AdjustedTrigger.Y - ShiftUp);
    }

    const ControlContact Contact = DropdownSelector::ConstructOptionSheet(
        Surface,
        AdjustedTrigger,
        Options.data(),
        OptionCount,
        SelectedIndex,
        Input.PrimaryPointer,
        ChosenIndex);

    if (Contact.Clicked)
    {
        switch (Overlay.Category)
        {
            case OpenDropdownCategory::ReleaseVolume:
                Effect.ReleaseVolume = static_cast<ParticleReleaseVolume>(ChosenIndex);
                break;
            case OpenDropdownCategory::Silhouette:
                Effect.Silhouette = static_cast<ParticleVisualSilhouette>(ChosenIndex);
                break;
            case OpenDropdownCategory::ColorBy:
                Effect.ColorBy = static_cast<ParticleColorByCategory>(ChosenIndex);
                break;
            case OpenDropdownCategory::RampWrap:
                Effect.RampWrap = static_cast<ParticleRampWrapCategory>(ChosenIndex);
                break;
            case OpenDropdownCategory::FireworkPattern:
                Effect.FireworkPattern = static_cast<FireworkPatternCategory>(ChosenIndex);
                break;
            case OpenDropdownCategory::RiggingArchitecture:
                Effect.RiggingArchitecture = static_cast<uint8_t>(ChosenIndex + 1u);
                break;
            case OpenDropdownCategory::FerrofluidVessel:
                Effect.FerrofluidVessel = static_cast<uint8_t>(ChosenIndex + 1u);
                break;
            case OpenDropdownCategory::FracturePattern:
                Effect.FracturePattern = static_cast<uint8_t>(ChosenIndex + 1u);
                break;
            case OpenDropdownCategory::ShieldGeometry:
                Effect.ShieldGeometry = static_cast<uint8_t>(ChosenIndex);
                break;
            case OpenDropdownCategory::BoidsCreature:
                Effect.BoidsCreatureVariant = static_cast<uint8_t>(ChosenIndex);
                break;
            default:
                break;
        }
        Session->IncrementRevision();
        OpenDropdown = OpenDropdownCategory::None;
    }
    else if (Input.EscapePressed
             || (Input.PrimaryPointer.Enabled && Input.PrimaryPointer.Pressed
                 && !DropdownSelector::QuerySheetBounds(AdjustedTrigger, OptionCount).Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y })
                 && !Overlay.TriggerBounds.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y })))
    {
        OpenDropdown = OpenDropdownCategory::None;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 IMGUI PANE ADAPTER
//------------------------------------------------------------------------------------------------------------------------

void ParticleInspectorPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##ParticleInspectorPaneCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, Δt);

    WorkspaceAppearance::UpdatePanePointerOwnership(
        this,
        ScreenBounds,
        SafeScale,
        Input,
        ActiveSliderId >= 0 || ActiveAxisDragId >= 0);
}

} // namespace Frontier::Workspaces
