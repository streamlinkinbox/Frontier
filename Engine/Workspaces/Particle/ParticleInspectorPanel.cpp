/*============================================================================================================================================
                                                      PARTICLEINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleInspectorPanel: 4-tab Particle Effect Inspector + direct delegation to Frontier's C++ ForceInspectorView

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleInspectorPanel.h"

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

[[nodiscard]] constexpr ColorRgba ConvertRgba(ParticleRgba8 Color) noexcept
{
    return ColorRgba{ Color.Red, Color.Green, Color.Blue, Color.Alpha };
}


using namespace SimulationSpace::ForceFields;

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFACE TOKENS
//------------------------------------------------------------------------------------------------------------------------

namespace ParticleInspectorColors
{
inline constexpr ColorRgba RootBg          { 18u,  18u,  18u,  255u };
inline constexpr ColorRgba SectionBg       { 29u,  29u,  30u,  255u };
inline constexpr ColorRgba SurfaceDark     { 26u,  26u,  26u,  255u };
inline constexpr ColorRgba SubWellBg       { 22u,  22u,  24u,  255u };
inline constexpr ColorRgba RecessDark      { 10u,  10u,  11u,  255u };
inline constexpr ColorRgba TabTrackBg      { 8u,   8u,   9u,   255u };
inline constexpr ColorRgba HairlineBorder  { 255u, 255u, 255u, 16u  };
inline constexpr ColorRgba SubtleBorder    { 255u, 255u, 255u, 28u  };
inline constexpr ColorRgba TabActiveBg     { 40u,  40u,  42u,  255u };
inline constexpr ColorRgba TabHoverBg      { 48u,  48u,  50u,  255u };
inline constexpr ColorRgba InkPrimary      { 242u, 242u, 242u, 255u };
inline constexpr ColorRgba InkHeading      { 228u, 228u, 228u, 255u };
inline constexpr ColorRgba InkLabel        { 167u, 167u, 170u, 255u };
inline constexpr ColorRgba InkMuted        { 132u, 132u, 136u, 255u };
inline constexpr ColorRgba InkDim          { 108u, 108u, 112u, 255u };
inline constexpr ColorRgba EmeraldLive     { 62u,  196u, 109u, 255u };
inline constexpr ColorRgba EmeraldSoft     { 131u, 214u, 161u, 255u };
inline constexpr ColorRgba AxisRed         { 226u, 116u, 112u, 255u };
inline constexpr ColorRgba AxisGreen       { 103u, 196u, 133u, 255u };
inline constexpr ColorRgba AxisBlue        { 110u, 167u, 229u, 255u };
inline constexpr ColorRgba WarmGold        { 232u, 194u, 122u, 255u };
inline constexpr ColorRgba WarmGoldInk     { 246u, 220u, 176u, 255u };
inline constexpr ColorRgba DangerSoft      { 244u, 155u, 149u, 255u };
inline constexpr ColorRgba DangerBorder    { 255u, 92u,  92u,  72u  };
inline constexpr ColorRgba HighlightKpiBg  { 239u, 244u, 207u, 255u };
inline constexpr ColorRgba HighlightKpiInk { 23u,  25u,  14u,  255u };
inline constexpr ColorRgba TealCode        { 169u, 217u, 200u, 255u };
inline constexpr ColorRgba FooterBg        { 18u,  18u,  18u,  255u };
} // namespace ParticleInspectorColors

//------------------------------------------------------------------------------------------------------------------------
//                                              HIGH-CONTRAST FLUID SWITCH
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ConstructFluidToggleSwitch(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    bool                  Enabled,
    const ControlPointer& Pointer) noexcept
{
    const bool  Hovered = Pointer.Enabled && Bounds.Contains({ Pointer.X, Pointer.Y });
    const float Radius  = Bounds.Height * 0.5f;
    const ColorRgba TrackBg = Enabled
        ? ColorRgba{ 255u, 255u, 255u, 255u }
        : (Hovered ? ColorRgba{ 44u, 44u, 46u, 255u } : ColorRgba{ 35u, 35u, 37u, 255u });
    const ColorRgba Border = Enabled
        ? ColorRgba{ 255u, 255u, 255u, 255u }
        : ColorRgba{ 255u, 255u, 255u, 26u };
    const ColorRgba KnobInk = Enabled
        ? ColorRgba{ 17u, 17u, 17u, 255u }
        : ColorRgba{ 188u, 188u, 188u, 255u };

    Surface.FillRectangle(Bounds, TrackBg, Radius);
    Surface.StrokeRectangle(Bounds, Border, Radius, 1.0f);

    const float KnobRadius = (Bounds.Height - 6.0f) * 0.5f;
    const float KnobX      = Enabled ? (Bounds.Right() - KnobRadius - 3.0f) : (Bounds.X + KnobRadius + 3.0f);
    Surface.FillCircle({ KnobX, Bounds.Y + Bounds.Height * 0.5f }, KnobRadius, KnobInk);

    return Hovered && Pointer.Released;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           OVERVIEW HERO INFOGRAPHIC DIAGRAM
//------------------------------------------------------------------------------------------------------------------------

void ConstructHeroSolverInfographic(
    PixelSpace&                      Surface,
    Rectangle2D                      DiagramRect,
    const ParticleEffectDescriptor& Effect,
    float                            CoupledWindMs) noexcept
{
    Surface.FillRectangle(DiagramRect, ColorRgba{ 11u, 11u, 13u, 220u }, 14.0f);
    Surface.StrokeRectangle(DiagramRect, ColorRgba{ 255u, 255u, 255u, 13u }, 14.0f, 1.0f);
    Surface.PushClip(DiagramRect);

    // Subtle schematic gridlines
    for (int32_t LineIdx = 1; LineIdx <= 3; ++LineIdx)
    {
        const float Gy = DiagramRect.Y + DiagramRect.Height * (static_cast<float>(LineIdx) / 4.0f);
        Surface.FillRectangle({ DiagramRect.X + 10.0f, Gy, DiagramRect.Width - 20.0f, 1.0f }, ColorRgba{ 255u, 255u, 255u, 8u }, 0.0f);
    }

    const float CenterX = DiagramRect.X + DiagramRect.Width * 0.5f;
    const float BaseY   = DiagramRect.Bottom() - 22.0f;

    if (Effect.AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux)
    {
        // Petri dish meniscus + concave Rosensweig catenoid peaks + magnetic dipole arcs
        const float DishHalfW = DiagramRect.Width * 0.34f;
        Surface.FillRectangle(
            { CenterX - DishHalfW, BaseY, DishHalfW * 2.0f, 9.0f },
            ColorRgba{ 26u, 30u, 38u, 255u },
            4.5f);
        Surface.StrokeRectangle(
            { CenterX - DishHalfW, BaseY, DishHalfW * 2.0f, 9.0f },
            ColorRgba{ 148u, 184u, 218u, 96u },
            4.5f,
            1.0f);

        // Concave Rosensweig spikes rising toward orbiting magnet pole
        const int32_t SpikeVis = 7;
        for (int32_t S = 0; S < SpikeVis; ++S)
        {
            const float Norm   = (static_cast<float>(S) / static_cast<float>(SpikeVis - 1)) * 2.0f - 1.0f;
            const float SpikeX = CenterX + Norm * (DishHalfW * 0.78f);
            const float Height = (1.0f - 0.52f * Norm * Norm) * (32.0f + Effect.MaxSpikeHeightMeters * 14.0f);
            const float HalfB  = 11.5f;
            const Point2D Cone[3] = {
                { SpikeX - HalfB, BaseY + 1.0f },
                { SpikeX,         BaseY - Height },
                { SpikeX + HalfB, BaseY + 1.0f },
            };
            Surface.FillPolygon(Cone, ColorRgba{ 20u, 24u, 32u, 255u });
            const Point2D SpecEdge[2] = {
                { SpikeX - HalfB * 0.65f, BaseY - Height * 0.18f },
                { SpikeX,                 BaseY - Height },
            };
            Surface.StrokePolyline(SpecEdge, ColorRgba{ 216u, 232u, 248u, 175u }, 1.35f, false);
            Surface.FillCircle({ SpikeX, BaseY - Height }, 1.6f, ColorRgba{ 238u, 246u, 255u, 200u });
        }

        // Dipole flux curves & orbiting magnet pole marker
        const Point2D PolePos{ CenterX + DishHalfW * 0.38f, DiagramRect.Y + 24.0f };
        for (int32_t Arc = 0; Arc < 2; ++Arc)
        {
            const float Dir = (Arc == 0) ? -1.0f : 1.0f;
            std::array<Point2D, 12u> LoopPts{};
            for (std::size_t K = 0u; K < LoopPts.size(); ++K)
            {
                const float A = static_cast<float>(K) / static_cast<float>(LoopPts.size() - 1u) * 3.14159f;
                LoopPts[K]    = {
                    PolePos.X + Dir * std::sin(A) * 58.0f,
                    PolePos.Y + (1.0f - std::cos(A)) * 26.0f,
                };
            }
            Surface.StrokePolyline(LoopPts, ColorRgba{ 232u, 194u, 122u, 72u }, 1.15f, false);
        }
        Surface.FillCircle(PolePos, 11.0f, ColorRgba{ 232u, 194u, 122u, 24u });
        Surface.FillCircle(PolePos, 7.0f, ColorRgba{ 232u, 194u, 122u, 52u });
        Surface.FillCircle(PolePos, 3.8f, ParticleInspectorColors::WarmGold);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { DiagramRect.X + 12.0f, DiagramRect.Y + 9.0f },
            9.5f,
            ParticleInspectorColors::InkMuted,
            "Rosensweig catenoid profile · orbiting dipole");
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        // Parabolic launch arc + bursting chrysanthemum crown
        const Point2D BurstOrigin{ CenterX + 26.0f, DiagramRect.Y + 42.0f };
        std::array<Point2D, 14u> ArcPts{};
        for (std::size_t K = 0u; K < ArcPts.size(); ++K)
        {
            const float T = static_cast<float>(K) / static_cast<float>(ArcPts.size() - 1u);
            ArcPts[K]     = {
                DiagramRect.X + 44.0f + T * (BurstOrigin.X - (DiagramRect.X + 44.0f)),
                BaseY - std::sin(T * 1.5708f) * (BaseY - BurstOrigin.Y),
            };
        }
        Surface.StrokePolyline(ArcPts, ColorRgba{ 232u, 194u, 122u, 120u }, 1.4f, false);

        for (int32_t Ray = 0; Ray < 14; ++Ray)
        {
            const float Ang = static_cast<float>(Ray) * 0.4488f;
            const float Len = (Ray % 2 == 0) ? 28.0f : 19.0f;
            const Point2D Tip{ BurstOrigin.X + std::cos(Ang) * Len, BurstOrigin.Y + std::sin(Ang) * Len };
            Surface.StrokeLine(BurstOrigin, Tip, ColorRgba{ 246u, 220u, 176u, 165u }, 1.2f);
            Surface.FillCircle(Tip, 2.2f, ParticleInspectorColors::WarmGold);
        }
        Surface.FillCircle(BurstOrigin, 4.5f, ColorRgba{ 255u, 248u, 228u, 255u });
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { DiagramRect.X + 12.0f, DiagramRect.Y + 9.0f },
            9.5f,
            ParticleInspectorColors::InkMuted,
            "Hanabi burst envelope · live volley interval");
    }
    else
    {
        // Live particle advection & density waveform
        std::array<Point2D, 24u> WavePts{};
        const float SpanW = DiagramRect.Width - 28.0f;
        for (std::size_t K = 0u; K < WavePts.size(); ++K)
        {
            const float T = static_cast<float>(K) / static_cast<float>(WavePts.size() - 1u);
            const float X = DiagramRect.X + 14.0f + T * SpanW;
            const float H = 18.0f + 22.0f * std::sin(T * 4.2f) * std::sin(T * 3.14159f) + std::min(16.0f, CoupledWindMs * 0.7f);
            WavePts[K]    = { X, BaseY - H };
        }
        Surface.StrokePolyline(WavePts, ParticleInspectorColors::WarmGold, 1.8f, false);
        for (std::size_t K = 2u; K < WavePts.size(); K += 4u)
        {
            Surface.FillCircle(WavePts[K], 3.0f, ParticleInspectorColors::EmeraldLive);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { DiagramRect.X + 12.0f, DiagramRect.Y + 9.0f },
            9.5f,
            ParticleInspectorColors::InkMuted,
            "Live particle solver & ForceFields advection profile");
    }

    char CaptionBuf[64]{};
    std::snprintf(CaptionBuf, sizeof(CaptionBuf), "Local wind %.1f m/s · %u slots", CoupledWindMs, Effect.Capacity);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { DiagramRect.X + 12.0f, DiagramRect.Bottom() - 15.0f },
        9.2f,
        ParticleInspectorColors::InkDim,
        CaptionBuf);
    Surface.PopClip();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                SPECTRUM RAMP SAMPLING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ColorRgba EvaluateColorRamp(
    const ParticleEffectDescriptor& Effect,
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
        return ColorRgba::Blend(ConvertRgba(Effect.ColorA), ConvertRgba(Effect.ColorC), U * 2.0f);
    }
    return ColorRgba::Blend(ConvertRgba(Effect.ColorC), ConvertRgba(Effect.ColorB), (U - 0.5f) * 2.0f);
}

void ConstructSpectrumRibbon(
    PixelSpace&                      Surface,
    Rectangle2D                      Rect,
    const ParticleEffectDescriptor& Effect,
    bool                             ShowStopHandles = false) noexcept
{
    const float Radius = Rect.Height * 0.5f;
    Surface.FillRectangle(Rect, ParticleInspectorColors::RecessDark, Radius);
    constexpr int32_t SegmentCount = 36;
    const float       InnerX       = Rect.X + 2.0f;
    const float       InnerY       = Rect.Y + 2.0f;
    const float       InnerW       = std::max(1.0f, Rect.Width - 4.0f);
    const float       InnerH       = std::max(1.0f, Rect.Height - 4.0f);
    const float       StepW        = InnerW / static_cast<float>(SegmentCount);

    Surface.PushClip({ Rect.X + 2.0f, Rect.Y + 2.0f, InnerW, InnerH });
    for (int32_t Segment = 0; Segment < SegmentCount; ++Segment)
    {
        const float     T      = (static_cast<float>(Segment) + 0.5f) / static_cast<float>(SegmentCount);
        const ColorRgba Sample = EvaluateColorRamp(Effect, T).WithAlpha(255u);
        Surface.FillRectangle({ InnerX + static_cast<float>(Segment) * StepW, InnerY, StepW + 0.8f, InnerH }, Sample, 0.0f);
    }
    Surface.PopClip();
    Surface.StrokeRectangle(Rect, ColorRgba{ 255u, 255u, 255u, 32u }, Radius, 1.0f);

    if (ShowStopHandles)
    {
        const float MidY = Rect.Y + Rect.Height * 0.5f;
        const std::array<std::pair<float, ColorRgba>, 3u> Stops = {{
            { 0.08f, ConvertRgba(Effect.ColorA).WithAlpha(255u) },
            { 0.50f, ConvertRgba(Effect.ColorC).WithAlpha(255u) },
            { 0.92f, ConvertRgba(Effect.ColorB).WithAlpha(255u) },
        }};
        for (std::size_t Idx = 0u; Idx < Stops.size(); ++Idx)
        {
            const Point2D Pos{ Rect.X + Rect.Width * Stops[Idx].first, MidY };
            if (Idx == 1u)
            {
                Surface.FillCircle(Pos, 10.5f, ColorRgba{ 232u, 194u, 122u, 110u });
            }
            Surface.FillCircle(Pos, 8.0f, Stops[Idx].second);
            Surface.StrokeCircle(Pos, 8.0f, ColorRgba{ 255u, 255u, 255u, 235u }, 1.8f);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              FLUID 22PX SECTION PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

float BeginSectionBox(
    PixelSpace&      Surface,
    float            LeftX,
    float            TopY,
    float            InnerW,
    float            BoxHeight,
    std::string_view Title,
    std::string_view TagText,
    ColorRgba        /*TagAccent*/) noexcept
{
    const Rectangle2D Box{ LeftX, TopY, InnerW, BoxHeight };
    Surface.FillRectangle(Box, ParticleInspectorColors::SectionBg, 22.0f);
    Surface.StrokeRectangle(Box, ParticleInspectorColors::HairlineBorder, 22.0f, 1.0f);

    // Warm dot accent preceding the Fluid 16px light heading
    Surface.FillCircle({ LeftX + 22.0f, TopY + 23.0f }, 3.5f, ParticleInspectorColors::WarmGold);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Light,
        { LeftX + 33.0f, TopY + 14.0f },
        16.0f,
        ParticleInspectorColors::InkHeading,
        Title);

    if (!TagText.empty())
    {
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Medium,
            { LeftX + InnerW - 18.0f, TopY + 18.0f },
            9.0f,
            ParticleInspectorColors::InkDim,
            TagText,
            true);
    }

    return TopY + 46.0f;
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
    return ScalarSlider::ConstructLabeledInspectorRow(
        Surface,
        { X, Y, W, 46.0f },
        SliderId,
        Label,
        Minimum,
        Maximum,
        InOutScalar,
        UnitSuffix,
        static_cast<int32_t>(Precision),
        Pointer,
        ActiveSliderId,
        1.0f,
        26.0f,
        24.0f);
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
            ParticleInspectorColors::InkMuted,
            UnitText,
            true);
    }

    const float Gap   = 6.0f;
    const float CellW = (W - Gap * 2.0f) / 3.0f;
    const float CellY = Y + 18.0f;
    const float CellH = 30.0f;
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

        Surface.FillRectangle(Cell, ParticleInspectorColors::RecessDark, 15.0f);
        Surface.StrokeRectangle(
            Cell,
            Active ? AxisColors[Axis] : (Hovered ? ParticleInspectorColors::SubtleBorder : ParticleInspectorColors::HairlineBorder),
            15.0f,
            1.0f);

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { Cell.X + 11.0f, Cell.Y + 8.5f },
            10.0f,
            AxisColors[Axis],
            AxisLabels[Axis],
            true);

        char NumBuf[24]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%.2f", *Components[Axis]);
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { Cell.Right() - 10.0f, Cell.Y + 8.0f },
            11.5f,
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
        { X, Y + 5.0f },
        W - 58.0f,
        11.8f,
        ParticleInspectorColors::InkLabel,
        Label);

    const Rectangle2D SwitchRect{ X + W - 44.0f, Y, 44.0f, 25.0f };
    if (ConstructFluidToggleSwitch(Surface, SwitchRect, InOutEnabled, Pointer))
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
    Surface.FillRectangle(Rect, ParticleInspectorColors::SurfaceDark, 14.0f);
    Surface.StrokeRectangle(Rect, ParticleInspectorColors::HairlineBorder, 14.0f, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { Rect.X + 10.0f, Rect.Y + 7.0f },
        Rect.Width - 20.0f,
        9.2f,
        ParticleInspectorColors::InkMuted,
        Caption);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Medium,
        { Rect.X + 10.0f, Rect.Y + 21.0f },
        Rect.Width - 20.0f,
        13.0f,
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

void ParticleInspectorPanel::SyncForceOutlinerMirror()
{
    if (Session == nullptr)
    {
        return;
    }
    if (SyncedForceMirrorRevision == Session->QueryRevision())
    {
        return;
    }
    SyncedForceMirrorRevision = Session->QueryRevision();
    ForceOutlinerMirror.ClearAll();
    ForceOutlinerTokens.clear();

    OutlinerConstructTemplate Tpl{};
    Tpl.Category        = ConstructSlotCategory::ForceField;
    Tpl.DropdownLabel   = "Force Source";
    Tpl.CategoryTitle   = "ForceFields";
    Tpl.DefaultTitle    = "Force Source";
    Tpl.ForceModelOrdinal = 0u;
    ForceOutlinerMirror.ConfigureConstructTemplates(std::span<const OutlinerConstructTemplate>(&Tpl, 1u));

    for (const SimulationSpace::ForceFields::ForceSourceRecordEntry& Source : Session->QueryForceSources())
    {
        const OutlinerSlotToken Slot = ForceOutlinerMirror.ConstructFromTemplate(0u, {});
        ForceOutlinerTokens.push_back(Slot);
        if (OutlinerRecordEntry* Row = ForceOutlinerMirror.AccessEntry(Slot))
        {
            Row->Title             = Source.Label;
            Row->ForceModelOrdinal = static_cast<uint32_t>(Source.Model);
            Row->VisibilityEnabled = Source.Visible && Source.Enabled;
        }
    }
}

OutlinerRecordEntry* ParticleInspectorPanel::AccessActiveForceOutlinerEntry() noexcept
{
    if (Session == nullptr)
    {
        return nullptr;
    }
    const auto Sources = Session->QueryForceSources();
    const auto* Active = Session->AccessActiveForceInspectorSource();
    if (Active == nullptr)
    {
        return nullptr;
    }
    for (std::size_t Idx = 0u; Idx < Sources.size() && Idx < ForceOutlinerTokens.size(); ++Idx)
    {
        if (Sources[Idx].UniqueToken == Active->UniqueToken)
        {
            return ForceOutlinerMirror.AccessEntry(ForceOutlinerTokens[Idx]);
        }
    }
    return nullptr;
}

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
        SyncForceOutlinerMirror();
        Targets.Outliner          = &ForceOutlinerMirror;
        Targets.OutlinerEntry     = AccessActiveForceOutlinerEntry();
        Targets.Accumulator       = &Session->AccessForceAccumulator();
        Targets.LatticeCascade    = &Session->AccessLatticeCascade();
        Targets.ActiveForce       = Session->AccessActiveForceInspectorSource();
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

    ParticleEffectDescriptor* Effect = Session->AccessSelectedEffect();
    if (Effect == nullptr)
    {
        Session->SelectEnvironmentField();
        return;
    }

    const ParticleCategoryDescriptor& CategoryDesc = ParticlePresetRegistry::QueryCategoryDescriptor(Effect->Category);
    const ColorRgba Accent = (ConvertRgba(Effect->ColorA).Luminance() < 0.08f) ? ConvertRgba(CategoryDesc.AccentTint) : ConvertRgba(Effect->ColorA);

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

    // 1. Top Breadcrumb & Capsule Reset Button
    const std::string Crumb = std::string("Inspector / ") + CategoryDesc.FullTitle;
    const Rectangle2D ResetBtnRect{ InnerX + InnerW - 68.0f, CursorY - 2.0f, 68.0f, 24.0f };
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { InnerX, CursorY + 3.0f },
        std::max(40.0f, ResetBtnRect.X - InnerX - 8.0f),
        11.2f,
        ParticleInspectorColors::InkMuted,
        Crumb);

    const bool ResetHover = ChromeEnabled && ResetBtnRect.Contains(Cursor);
    Surface.FillRectangle(
        ResetBtnRect,
        ResetHover ? ParticleInspectorColors::TabHoverBg : ParticleInspectorColors::SurfaceDark,
        12.0f);
    Surface.StrokeRectangle(ResetBtnRect, ColorRgba{ 255u, 255u, 255u, 36u }, 12.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { ResetBtnRect.X + 11.0f, ResetBtnRect.Y + 5.5f },
        10.5f,
        ParticleInspectorColors::InkHeading,
        "Reset");
    WorkspaceAppearance::ConstructResetSymbol(
        Surface,
        { ResetBtnRect.Right() - 13.0f, ResetBtnRect.Y + 12.0f },
        8.5f,
        ParticleInspectorColors::WarmGold,
        1.3f);
    if (ChromeEnabled && Input.PrimaryPointer.Pressed && ResetHover)
    {
        (void)Session->ResetEffectPreset(Effect->UniqueToken);
        SyncedTitleToken   = 0u;
        SyncedSkyTextToken = 0u;
    }

    CursorY += 30.0f;

    // 2. Selected Effect Heading Box (20px radius, 38x44 emblem, 18.5px light title, pure-white Fluid switch)
    const Rectangle2D HeadingBox{ InnerX, CursorY, InnerW, 68.0f };
    Surface.FillRectangle(HeadingBox, ParticleInspectorColors::SectionBg, 20.0f);
    Surface.StrokeRectangle(HeadingBox, ParticleInspectorColors::HairlineBorder, 20.0f, 1.0f);

    const Rectangle2D SymbolBox{ HeadingBox.X + 14.0f, HeadingBox.Y + 12.0f, 38.0f, 44.0f };
    Surface.FillRectangle(SymbolBox, ColorRgba{ 255u, 255u, 255u, 13u }, 14.0f);
    Surface.StrokeRectangle(SymbolBox, ColorRgba{ 255u, 255u, 255u, 18u }, 14.0f, 1.0f);
    Surface.FillCircle(SymbolBox.Center(), 7.5f, Accent);
    Surface.StrokeCircle(SymbolBox.Center(), 11.5f, Accent.ScaledAlpha(0.45f), 1.25f);

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

    const Rectangle2D EnableSwitchRect{ HeadingBox.Right() - 58.0f, HeadingBox.Y + 21.5f, 44.0f, 25.0f };
    const float TitleX = SymbolBox.Right() + 12.0f;
    const float TitleW = std::max(40.0f, EnableSwitchRect.X - TitleX - 10.0f);
    const Rectangle2D TitleRect{ TitleX, HeadingBox.Y + 13.0f, TitleW, 25.0f };

    if (TitleEditor.Active)
    {
        (void)TextInputField::Construct(Surface, TitleRect, TitleEditor, ChromePointer, 16.0f);
    }
    else
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Light,
            { TitleRect.X, TitleRect.Y + 1.0f },
            TitleRect.Width,
            18.5f,
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
        { TitleX, HeadingBox.Y + 41.0f },
        TitleW,
        9.0f,
        ParticleInspectorColors::InkMuted,
        TypeSubtitle,
        true);

    if (ConstructFluidToggleSwitch(Surface, EnableSwitchRect, Effect->Visible, ChromePointer))
    {
        Effect->Visible = !Effect->Visible;
        Session->IncrementRevision();
    }

    CursorY += 78.0f;

    // 3. Capsule Segmented Inspector Tab Strip (38px #080808 track + #282828 active tab)
    static constexpr std::array<const char*, 4u> TabLabels = {{
        "Overview",
        "Source",
        "Motion & FX",
        "Spec & Export",
    }};
    const Rectangle2D TabTrackRect{ InnerX, CursorY, InnerW, 38.0f };
    Surface.FillRectangle(TabTrackRect, ParticleInspectorColors::TabTrackBg, 19.0f);
    Surface.StrokeRectangle(TabTrackRect, ParticleInspectorColors::HairlineBorder, 19.0f, 1.0f);

    const float TabPad = 4.0f;
    const float TabGap = 3.0f;
    const float TabH   = TabTrackRect.Height - TabPad * 2.0f;
    const float TabW   = (TabTrackRect.Width - TabPad * 2.0f - TabGap * 3.0f) * 0.25f;

    for (uint32_t TabOrd = 0u; TabOrd < 4u; ++TabOrd)
    {
        const Rectangle2D TabRect{
            TabTrackRect.X + TabPad + static_cast<float>(TabOrd) * (TabW + TabGap),
            TabTrackRect.Y + TabPad,
            TabW,
            TabH,
        };
        const bool Active  = (static_cast<uint32_t>(ActiveTab) == TabOrd);
        const bool Hovered = ChromeEnabled && TabRect.Contains(Cursor);

        if (Active)
        {
            Surface.FillRectangle(TabRect, ParticleInspectorColors::TabActiveBg, TabH * 0.5f);
            Surface.StrokeRectangle(TabRect, ColorRgba{ 255u, 255u, 255u, 16u }, TabH * 0.5f, 1.0f);
        }
        else if (Hovered)
        {
            Surface.FillRectangle(TabRect, ColorRgba{ 255u, 255u, 255u, 10u }, TabH * 0.5f);
        }

        const float LabelW = WorkspaceAppearance::MeasureWeightedTextWidth(
            Surface,
            Active ? FontWeightClass::Medium : FontWeightClass::Regular,
            11.0f,
            TabLabels[TabOrd]);
        const float TextX = TabRect.X + std::max(4.0f, (TabRect.Width - LabelW) * 0.5f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            Active ? FontWeightClass::Medium : FontWeightClass::Regular,
            { TextX, TabRect.Y + 8.5f },
            TabRect.Width - 8.0f,
            11.0f,
            Active ? ParticleInspectorColors::InkPrimary : ParticleInspectorColors::InkMuted,
            TabLabels[TabOrd]);

        if (ChromeEnabled && Input.PrimaryPointer.Pressed && Hovered)
        {
            ConfigureActiveTab(static_cast<ParticleInspectorTab>(TabOrd));
        }
    }

    CursorY += TabTrackRect.Height + 12.0f;

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
        case ParticleInspectorTab::MotionDynamics:
            EndY = RenderMotionDynamicsTab(Surface, ScrollViewport, InnerX, StartY, InnerW, *Effect, BodyInput, DeferredOverlay);
            break;
        case ParticleInspectorTab::SpecificationExport:
            EndY = RenderSpecificationExportTab(Surface, InnerX, StartY, InnerW, *Effect);
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
    ParticleEffectDescriptor& Effect,
    const PaneInputReading&    Input,
    double                     Δt,
    DeferredDropdownSheet&     OutOverlay)
{
    const float ContentX = LeftX + 18.0f;
    const float ContentW = InnerW - 36.0f;
    const ControlPointer& Pointer = Input.PrimaryPointer;
    const ParticleCategoryDescriptor& CatDesc = ParticlePresetRegistry::QueryCategoryDescriptor(Effect.Category);
    float Y = TopY;
    bool  Modified = false;

    const SpatialForceSample CoupledSample = Session->SampleCoupledForce(Effect.OriginMeters);
    const float CoupledWindMs              = VectorLength(CoupledSample.AdvectionVelocity);

    // -----------------------------------------------------------------------------------------------------------------
    // 1. HERO LIVE SOLVER BOX (22px radius, status check, 36px metric, embedded vector infographic)
    // -----------------------------------------------------------------------------------------------------------------
    const float HeroH = 224.0f;
    const Rectangle2D HeroRect{ LeftX, Y, InnerW, HeroH };
    Surface.FillRectangle(HeroRect, ParticleInspectorColors::SectionBg, 22.0f);
    Surface.StrokeRectangle(HeroRect, ColorRgba{ 255u, 255u, 255u, 17u }, 22.0f, 1.0f);

    float Hy = Y + 16.0f;
    const Point2D MarkCenter{ ContentX + 10.0f, Hy + 10.0f };
    const bool    ActiveVis = Effect.Visible;
    Surface.FillCircle(
        MarkCenter,
        10.0f,
        ActiveVis ? ColorRgba{ 62u, 196u, 109u, 42u } : ColorRgba{ 232u, 194u, 122u, 46u });
    if (ActiveVis)
    {
        const Point2D CheckPts[3] = {
            { MarkCenter.X - 3.8f, MarkCenter.Y + 0.3f },
            { MarkCenter.X - 1.0f, MarkCenter.Y + 3.0f },
            { MarkCenter.X + 4.2f, MarkCenter.Y - 2.8f },
        };
        Surface.StrokePolyline(CheckPts, ParticleInspectorColors::EmeraldLive, 1.7f, false);
    }
    else
    {
        Surface.StrokeLine({ MarkCenter.X - 3.5f, MarkCenter.Y - 3.5f }, { MarkCenter.X + 3.5f, MarkCenter.Y + 3.5f }, ParticleInspectorColors::WarmGold, 1.6f);
    }

    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { ContentX + 28.0f, Hy + 3.5f },
        11.0f,
        ColorRgba{ 185u, 185u, 185u, 255u },
        ActiveVis ? "Within capacity" : "Hidden");
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface,
        FontWeightClass::Medium,
        { ContentX + ContentW, Hy + 4.5f },
        9.0f,
        ParticleInspectorColors::InkDim,
        "01 / LIVE SOLVER",
        true);

    Hy += 26.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { ContentX, Hy },
        11.0f,
        ParticleInspectorColors::InkMuted,
        "Particles alive");

    Hy += 15.0f;
    char AliveBuf[32]{};
    std::snprintf(AliveBuf, sizeof(AliveBuf), "%u", Effect.Capacity);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Light,
        { ContentX, Hy },
        36.0f,
        ParticleInspectorColors::InkPrimary,
        AliveBuf,
        true);

    const char* ModeRight = Effect.Sublabel.c_str();
    char RateRightBuf[48]{};
    if (Effect.BirthRatePerSec > 0.05f)
    {
        std::snprintf(RateRightBuf, sizeof(RateRightBuf), "%.0f /s", Effect.BirthRatePerSec);
        ModeRight = RateRightBuf;
    }
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface,
        FontWeightClass::Light,
        { ContentX + ContentW, Hy + 7.0f },
        15.0f,
        ColorRgba{ 222u, 222u, 222u, 255u },
        ModeRight,
        true);
    char CapRightBuf[48]{};
    std::snprintf(CapRightBuf, sizeof(CapRightBuf), "of %u cap", Effect.Capacity);
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface,
        FontWeightClass::Regular,
        { ContentX + ContentW, Hy + 25.0f },
        10.5f,
        ParticleInspectorColors::InkMuted,
        CapRightBuf,
        true);

    Hy += 44.0f;
    ConstructHeroSolverInfographic(Surface, { ContentX, Hy, ContentW, 108.0f }, Effect, CoupledWindMs);

    Y += HeroH + 10.0f;

    // -----------------------------------------------------------------------------------------------------------------
    // 2. SIGNATURE 3-TILE KPI HIGHLIGHT STRIP (3rd tile is iconic #EFF4CF warm lime-ivory!)
    // -----------------------------------------------------------------------------------------------------------------
    const float KpiGap = 8.0f;
    const float KpiW   = (InnerW - KpiGap * 2.0f) / 3.0f;
    const float KpiH   = 72.0f;

    auto RenderKpiTile = [&](Rectangle2D Rect, const char* Label, const char* MainReading, const char* SubCaption, bool Highlighted)
    {
        Surface.FillRectangle(
            Rect,
            Highlighted ? ParticleInspectorColors::HighlightKpiBg : ParticleInspectorColors::SurfaceDark,
            16.0f);
        if (!Highlighted)
        {
            Surface.StrokeRectangle(Rect, ParticleInspectorColors::HairlineBorder, 16.0f, 1.0f);
        }

        const ColorRgba LabelColor = Highlighted ? ColorRgba{ 23u, 25u, 14u, 166u } : ParticleInspectorColors::InkMuted;
        const ColorRgba MainColor  = Highlighted ? ParticleInspectorColors::HighlightKpiInk : ParticleInspectorColors::InkPrimary;
        const ColorRgba SubColor   = Highlighted ? ColorRgba{ 23u, 25u, 14u, 152u } : ParticleInspectorColors::InkDim;

        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Rect.X + 12.0f, Rect.Y + 11.0f }, Rect.Width - 24.0f, 10.0f, LabelColor, Label);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Rect.X + 12.0f, Rect.Y + 26.0f }, Rect.Width - 24.0f, 16.5f, MainColor, MainReading, true);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Rect.X + 12.0f, Rect.Y + 49.0f }, Rect.Width - 24.0f, 9.5f, SubColor, SubCaption);
    };

    const char* BlendSub = (Effect.BlendCategory == ParticleBlendCategory::PremultipliedAlpha) ? "alpha blend" : "additive glow";
    const char* ShapeMain = ParticlePresetRegistry::QuerySilhouetteLabel(Effect.Silhouette);
    if (Effect.AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux)
    {
        ShapeMain = "Rosensweig";
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        ShapeMain = "Hanabi Stars";
    }

    const char* MidLabel = "Gravity";
    char MidValBuf[48]{};
    char MidSubBuf[64]{};
    if (Effect.AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux)
    {
        MidLabel = "Ferrofluid";
        std::snprintf(MidValBuf, sizeof(MidValBuf), "%s", (Effect.FerrofluidVessel == 2u) ? "Orb" : "Petri Dish");
        std::snprintf(MidSubBuf, sizeof(MidSubBuf), "%u spikes · %.1f rad/s", Effect.FerroSpikeCount, Effect.MagnetOrbitSpeedRadS);
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        MidLabel = "Firework Show";
        std::snprintf(MidValBuf, sizeof(MidValBuf), "%s", ParticlePresetRegistry::QueryFireworkPatternLabel(Effect.FireworkPattern));
        std::snprintf(MidSubBuf, sizeof(MidSubBuf), "text \"%s\"", Effect.FireworkSkyText.c_str());
    }
    else
    {
        std::snprintf(MidValBuf, sizeof(MidValBuf), "%.2f g", Effect.GravityMultiplier);
        std::snprintf(MidSubBuf, sizeof(MidSubBuf), "%.2f drag", Effect.DragCoefficient);
    }

    char WindMainBuf[32]{};
    char WindSubBuf[48]{};
    if (Effect.ForceCouplingGain > 0.001f)
    {
        std::snprintf(WindMainBuf, sizeof(WindMainBuf), "%.2f /s", Effect.ForceCouplingGain);
        std::snprintf(WindSubBuf, sizeof(WindSubBuf), "lattice · %.1f m/s", CoupledWindMs);
    }
    else
    {
        std::snprintf(WindMainBuf, sizeof(WindMainBuf), "Locked");
        std::snprintf(WindSubBuf, sizeof(WindSubBuf), "magnetic / local");
    }

    RenderKpiTile({ LeftX, Y, KpiW, KpiH }, "Shape", ShapeMain, BlendSub, false);
    RenderKpiTile({ LeftX + KpiW + KpiGap, Y, KpiW, KpiH }, MidLabel, MidValBuf, MidSubBuf, false);
    RenderKpiTile({ LeftX + (KpiW + KpiGap) * 2.0f, Y, KpiW, KpiH }, "Wind coupling", WindMainBuf, WindSubBuf, true);

    Y += KpiH + 10.0f;

    // -----------------------------------------------------------------------------------------------------------------
    // 3. SUN QUICK-CONTROL 4-TILE FEATURE SWITCHES
    // -----------------------------------------------------------------------------------------------------------------
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { LeftX + 4.0f, Y + 4.0f },
        9.5f,
        ParticleInspectorColors::InkDim,
        "QUICK CONTROLS");
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + InnerW - 4.0f, Y + 4.0f },
        9.5f,
        ParticleInspectorColors::InkDim,
        CatDesc.FullTitle);
    Y += 22.0f;

    const float SwGap = 7.0f;
    const float SwW   = (InnerW - SwGap * 3.0f) * 0.25f;
    const float SwH   = 76.0f;

    auto RenderQuickSwitchTile = [&](Rectangle2D Rect, const char* Title, ControlIcon Icon, bool On, auto&& OnToggle)
    {
        const bool Hovered = Pointer.Enabled && Rect.Contains({ Pointer.X, Pointer.Y });
        Surface.FillRectangle(
            Rect,
            Hovered ? ColorRgba{ 34u, 34u, 36u, 255u } : ParticleInspectorColors::SurfaceDark,
            16.0f);
        Surface.StrokeRectangle(
            Rect,
            Hovered ? ParticleInspectorColors::SubtleBorder : ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(On ? 18u : 10u) },
            16.0f,
            1.0f);

        const Rectangle2D IconWell{ Rect.X + (Rect.Width - 28.0f) * 0.5f, Rect.Y + 11.0f, 28.0f, 28.0f };
        if (On)
        {
            Surface.FillRectangle({ IconWell.X - 2.0f, IconWell.Y - 2.0f, 32.0f, 32.0f }, ColorRgba{ 62u, 196u, 109u, 34u }, 12.0f);
        }
        Surface.FillRectangle(
            IconWell,
            On ? ColorRgba{ 40u, 95u, 60u, 255u } : ColorRgba{ 43u, 35u, 35u, 255u },
            10.0f);
        VectorIconRepository::ConstructControl(
            Surface,
            Icon,
            {
                IconWell.X + 7.5f,
                IconWell.Y + 7.5f,
                13.0f,
                On ? ColorRgba{ 215u, 245u, 226u, 255u } : ColorRgba{ 156u, 124u, 122u, 255u },
                1.65f,
            });

        const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, Title);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { Rect.X + std::max(4.0f, (Rect.Width - TitleW) * 0.5f), Rect.Y + 45.0f },
            Rect.Width - 8.0f,
            9.8f,
            ColorRgba{ 212u, 212u, 212u, 255u },
            Title);

        const char* TagStr = On ? "ON" : "OFF";
        const float TagW   = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, TagStr, true);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { Rect.X + (Rect.Width - TagW) * 0.5f, Rect.Y + 60.0f },
            8.4f,
            On ? ParticleInspectorColors::EmeraldLive : ColorRgba{ 126u, 114u, 114u, 255u },
            TagStr,
            true);

        if (Hovered && Pointer.Released)
        {
            OnToggle();
            Modified = true;
        }
    };

    const bool WindDriven  = Effect.ForceCouplingGain > 0.01f;
    const bool AdditiveOn  = (Effect.BlendCategory == ParticleBlendCategory::AdditiveGlow);
    const bool BounceOn    = Effect.FloorRestitution > 0.01f;

    RenderQuickSwitchTile(
        { LeftX, Y, SwW, SwH },
        "Visible",
        Effect.Visible ? ControlIcon::EyeVisible : ControlIcon::EyeHidden,
        Effect.Visible,
        [&]() { Effect.Visible = !Effect.Visible; });
    RenderQuickSwitchTile(
        { LeftX + SwW + SwGap, Y, SwW, SwH },
        "Wind-driven",
        ControlIcon::MotionActivity,
        WindDriven,
        [&]() { Effect.ForceCouplingGain = WindDriven ? 0.0f : 1.0f; });
    RenderQuickSwitchTile(
        { LeftX + (SwW + SwGap) * 2.0f, Y, SwW, SwH },
        "Additive glow",
        ControlIcon::SparklesAntiAliasing,
        AdditiveOn,
        [&]() { Effect.BlendCategory = AdditiveOn ? ParticleBlendCategory::PremultipliedAlpha : ParticleBlendCategory::AdditiveGlow; });
    RenderQuickSwitchTile(
        { LeftX + (SwW + SwGap) * 3.0f, Y, SwW, SwH },
        "Floor bounce",
        ControlIcon::GaugeRate,
        BounceOn,
        [&]() { Effect.FloorRestitution = BounceOn ? 0.0f : 0.42f; });

    Y += SwH + 10.0f;

    // -----------------------------------------------------------------------------------------------------------------
    // 4. 4-BUTTON CAPSULE ACTION ROW (Primary Accent | Clear | Duplicate | Danger Delete)
    // -----------------------------------------------------------------------------------------------------------------
    const float ActGap = 7.0f;
    const float ActW   = (InnerW - ActGap * 3.0f) * 0.25f;
    const float ActH   = 34.0f;

    auto RenderActionButton = [&](Rectangle2D Rect, const char* Label, int32_t ToneMode, auto&& OnInvoke)
    {
        const bool Hovered = Pointer.Enabled && Rect.Contains({ Pointer.X, Pointer.Y });
        ColorRgba Fill{ 31u, 31u, 33u, 255u };
        ColorRgba Border{ 255u, 255u, 255u, 20u };
        ColorRgba Ink{ 220u, 220u, 220u, 255u };
        if (ToneMode == 1)
        {
            Fill   = Hovered ? ColorRgba{ 232u, 194u, 122u, 58u } : ColorRgba{ 232u, 194u, 122u, 40u };
            Border = ColorRgba{ 232u, 194u, 122u, 96u };
            Ink    = ParticleInspectorColors::WarmGoldInk;
        }
        else if (ToneMode == 2)
        {
            Fill   = Hovered ? ColorRgba{ 255u, 92u, 92u, 42u } : ColorRgba{ 31u, 25u, 26u, 255u };
            Border = ParticleInspectorColors::DangerBorder;
            Ink    = ParticleInspectorColors::DangerSoft;
        }
        else if (Hovered)
        {
            Fill   = ColorRgba{ 42u, 42u, 44u, 255u };
            Border = ColorRgba{ 255u, 255u, 255u, 46u };
        }

        Surface.FillRectangle(Rect, Fill, ActH * 0.5f);
        Surface.StrokeRectangle(Rect, Border, ActH * 0.5f, 1.0f);
        const float LabelW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.8f, Label);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { Rect.X + std::max(6.0f, (Rect.Width - LabelW) * 0.5f), Rect.Y + 10.5f },
            Rect.Width - 12.0f,
            10.8f,
            Ink,
            Label);

        if (Hovered && Pointer.Released)
        {
            OnInvoke();
        }
    };

    const char* PrimaryActionLabel = "Burst";
    if (Effect.AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux
        || Effect.AuthoringDomain == ParticleAuthoringDomain::StrandBotanical
        || Effect.AuthoringDomain == ParticleAuthoringDomain::MoGraphEffector)
    {
        PrimaryActionLabel = "Throw Ball";
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        PrimaryActionLabel = "Next Volley";
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FractureSeismic)
    {
        PrimaryActionLabel = "Fracture";
    }

    RenderActionButton({ LeftX, Y, ActW, ActH }, PrimaryActionLabel, 1, [&]() { Session->TriggerEffectBurst(Effect.UniqueToken); });
    RenderActionButton({ LeftX + ActW + ActGap, Y, ActW, ActH }, "Clear", 0, [&]() { (void)Session->ResetEffectPreset(Effect.UniqueToken); });
    RenderActionButton({ LeftX + (ActW + ActGap) * 2.0f, Y, ActW, ActH }, "Duplicate", 0, [&]() { (void)Session->DuplicateEffect(Effect.UniqueToken); });
    RenderActionButton({ LeftX + (ActW + ActGap) * 3.0f, Y, ActW, ActH }, "Delete", 2, [&]() { (void)Session->RemoveEffect(Effect.UniqueToken); });

    Y += ActH + 12.0f;

    // -----------------------------------------------------------------------------------------------------------------
    // 5. CONTEXTUAL DOMAIN 22PX PARAMETER BOX (ZERO Quarantine domains! Fluid SliderCapsule controls!)
    // -----------------------------------------------------------------------------------------------------------------
    float Sy = Y;
    if (Effect.AuthoringDomain == ParticleAuthoringDomain::FerrofluidFlux)
    {
        const float Sec3H = 290.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Magnetic Rosensweig ferrofluid", "02 / FERROFLUID", ColorRgba{ 158u, 192u, 224u, 255u });

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 6.0f }, 11.2f, ParticleInspectorColors::InkLabel, "Vessel form");
        const Rectangle2D VesselRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 30.0f };
        const char* VesselLabel = (Effect.FerrofluidVessel == 2u) ? "Floating Magnetic Orb" : "Petri Dish Spikes";
        if (DropdownSelector::ConstructButton(Surface, VesselRect, VesselLabel, OpenDropdown == OpenDropdownCategory::FerrofluidVessel, Pointer).Clicked)
        {
            OpenDropdown = (OpenDropdown == OpenDropdownCategory::FerrofluidVessel) ? OpenDropdownCategory::None : OpenDropdownCategory::FerrofluidVessel;
        }
        if (OpenDropdown == OpenDropdownCategory::FerrofluidVessel)
        {
            OutOverlay = { OpenDropdownCategory::FerrofluidVessel, VesselRect };
        }
        Sy += 40.0f;

        float SpikeFloat = static_cast<float>(Effect.FerroSpikeCount);
        if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 110, "Magnetic spike count", 32.0f, 560.0f, SpikeFloat, "cnt", 0, Pointer, ActiveSliderId))
        {
            Effect.FerroSpikeCount = static_cast<uint32_t>(std::lround(SpikeFloat));
            Modified               = true;
        }
        Sy += 52.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 111, "Magnet orbit speed", 0.2f, 5.0f, Effect.MagnetOrbitSpeedRadS, "rad/s", 2, Pointer, ActiveSliderId);
        Sy += 52.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 112, "Max cusp height", 0.2f, 1.8f, Effect.MaxSpikeHeightMeters, "m", 2, Pointer, ActiveSliderId);
        Sy += 54.0f;

        Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Manual magnet pole XYZ", Effect.ManualMagnetPole, Pointer);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FireworksSpectacle)
    {
        const float Sec3H = 264.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Fireworks volleys & sky text", "02 / PYROTECHNICS", ParticleInspectorColors::WarmGold);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 7.0f }, 11.2f, ParticleInspectorColors::InkLabel, "Burst pattern");
        const Rectangle2D PatRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 30.0f };
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
        Sy += 40.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy }, 10.8f, ParticleInspectorColors::InkLabel, "Sky-writing custom text");
        Sy += 17.0f;
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
        const Rectangle2D SkyRect{ ContentX, Sy, ContentW, 32.0f };
        Surface.FillRectangle(SkyRect, ParticleInspectorColors::RecessDark, 16.0f);
        Surface.StrokeRectangle(
            SkyRect,
            SkyTextEditor.Active ? ParticleInspectorColors::WarmGold : ParticleInspectorColors::SubtleBorder,
            16.0f,
            1.0f);
        (void)TextInputField::Construct(Surface, { SkyRect.X + 8.0f, SkyRect.Y + 3.0f, SkyRect.Width - 16.0f, 26.0f }, SkyTextEditor, Pointer, 12.0f);
        if (Pointer.Enabled && Pointer.Pressed && ScrollViewport.Contains({ Pointer.X, Pointer.Y }))
        {
            SkyTextEditor.Active = SkyRect.Contains({ Pointer.X, Pointer.Y });
        }
        Sy += 40.0f;

        // Quick New Year's sky-text words
        static constexpr std::array<const char*, 4u> QuickWords = {{ "2026", "NEW YEAR", "FRONTIER", "BOOM" }};
        float WordX = ContentX;
        for (const char* Word : QuickWords)
        {
            const float BtnW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, Word, true) + 16.0f;
            const Rectangle2D WordBtn{ WordX, Sy, BtnW, 22.0f };
            const bool Active  = (Effect.FireworkSkyText == Word);
            const bool Hovered = Pointer.Enabled && WordBtn.Contains({ Pointer.X, Pointer.Y });
            Surface.FillRectangle(
                WordBtn,
                Active ? ColorRgba{ 232u, 194u, 122u, 42u } : (Hovered ? ParticleInspectorColors::TabHoverBg : ParticleInspectorColors::SurfaceDark),
                11.0f);
            Surface.StrokeRectangle(WordBtn, Active ? ParticleInspectorColors::WarmGold : ParticleInspectorColors::HairlineBorder, 11.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Medium,
                { WordBtn.X + 8.0f, WordBtn.Y + 5.0f },
                9.5f,
                Active ? ParticleInspectorColors::WarmGoldInk : ParticleInspectorColors::InkLabel,
                Word,
                true);
            if (Pointer.Enabled && Pointer.Pressed && Hovered)
            {
                Effect.FireworkSkyText = Word;
                SkyTextEditor.Begin(Word);
                Modified = true;
            }
            WordX += BtnW + 6.0f;
        }
        Sy += 32.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 120, "Burst interval", 0.2f, 8.0f, Effect.BurstIntervalSec, "s", 1, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::StrandBotanical)
    {
        const float Sec3H = 342.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Dynamic rigging & flora curves", "02 / RIGGING", ColorRgba{ 126u, 198u, 118u, 255u });

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 7.0f }, 11.2f, ParticleInspectorColors::InkLabel, "Architecture");
        const Rectangle2D ArchRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 30.0f };
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
        Sy += 40.0f;

        float RowsF = static_cast<float>(Effect.StrandRows);
        if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 130, "Strands / canopy limbs", 4.0f, 48.0f, RowsF, "cnt", 0, Pointer, ActiveSliderId))
        {
            Effect.StrandRows = static_cast<uint32_t>(std::lround(RowsF));
            Modified          = true;
        }
        Sy += 52.0f;

        float SegsF = static_cast<float>(Effect.CurveSegments);
        if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 131, "Curve segments per strand", 2.0f, 32.0f, SegsF, "segs", 0, Pointer, ActiveSliderId))
        {
            Effect.CurveSegments = static_cast<uint32_t>(std::lround(SegsF));
            Modified             = true;
        }
        Sy += 52.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 132, "Elastic stiffness", 0.2f, 3.0f, Effect.ElasticStiffness, "x", 2, Pointer, ActiveSliderId);
        Sy += 52.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 133, "Impact tear speed", 0.0f, 18.0f, Effect.TearSpeedMs, "m/s", 1, Pointer, ActiveSliderId);
        Sy += 54.0f;

        Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Show curve segment debug", Effect.CurveDebugEnabled, Pointer);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::FractureSeismic)
    {
        const float Sec3H = 204.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Glacial & seismic fracture", "02 / CRACKS", ColorRgba{ 126u, 206u, 240u, 255u });

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 7.0f }, 11.2f, ParticleInspectorColors::InkLabel, "Fracture mode");
        const Rectangle2D FracRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 30.0f };
        const char* FracLabel = (Effect.FracturePattern == 2u) ? "Seismic Basalt Quake" : "Glacial Ice Sheet";
        if (DropdownSelector::ConstructButton(Surface, FracRect, FracLabel, OpenDropdown == OpenDropdownCategory::FracturePattern, Pointer).Clicked)
        {
            OpenDropdown = (OpenDropdown == OpenDropdownCategory::FracturePattern) ? OpenDropdownCategory::None : OpenDropdownCategory::FracturePattern;
        }
        if (OpenDropdown == OpenDropdownCategory::FracturePattern)
        {
            OutOverlay = { OpenDropdownCategory::FracturePattern, FracRect };
        }
        Sy += 40.0f;

        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 140, "Incision depth", 0.05f, 1.5f, Effect.FractureDepthMeters, "m", 2, Pointer, ActiveSliderId);
        Sy += 52.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 141, "Branching density", 0.3f, 3.0f, Effect.CrackBranchDensity, "x", 2, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::MoGraphEffector)
    {
        const float Sec3H = 164.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "MoGraph effector lattice", "02 / MOGRAPH", ColorRgba{ 244u, 118u, 102u, 255u });
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 150, "Effector falloff radius", 0.3f, 6.0f, Effect.EffectorFalloffMeters, "m", 2, Pointer, ActiveSliderId);
        Sy += 52.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 151, "Height extrusion gain", 0.1f, 4.0f, Effect.EffectorExtrusionMeters, "m", 2, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::EnergyShield)
    {
        const float Sec3H = 204.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Energy shield barrier", "02 / SHIELD", ColorRgba{ 104u, 226u, 174u, 255u });
        static constexpr std::array<const char*, 4u> ShieldLabels = {{ "Sphere", "Hex Dome", "Prism Barrier", "Cylinder" }};
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 7.0f }, 11.2f, ParticleInspectorColors::InkLabel, "Barrier shape");
        const Rectangle2D ShRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 30.0f };
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
        Sy += 40.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 160, "Impact ripple speed", 0.5f, 8.0f, Effect.ShieldRippleSpeed, "Hz", 2, Pointer, ActiveSliderId);
        Sy += 52.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 161, "Tessellation density", 4.0f, 36.0f, Effect.ShieldCellDensity, "/m", 1, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }
    else if (Effect.AuthoringDomain == ParticleAuthoringDomain::BoidsFlock)
    {
        const float Sec3H = 204.0f;
        Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Boids flock & murmuration", "02 / FLOCK", ColorRgba{ 166u, 214u, 168u, 255u });
        static constexpr std::array<const char*, 3u> CreatureLabels = {{ "Birds / Starlings", "Butterflies / Wisps", "Fish School" }};
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 7.0f }, 11.2f, ParticleInspectorColors::InkLabel, "Creature form");
        const Rectangle2D BoRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 30.0f };
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
        Sy += 40.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 170, "Perception radius", 0.3f, 4.0f, Effect.BoidsPerceptionMeters, "m", 2, Pointer, ActiveSliderId);
        Sy += 52.0f;
        Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 171, "Cruise flight speed", 0.5f, 9.0f, Effect.BoidsCruiseSpeedMs, "m/s", 1, Pointer, ActiveSliderId);
        Y += Sec3H + 10.0f;
    }

    if (Modified)
    {
        Session->RebuildEffectSimulation(Effect.UniqueToken);
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
    ParticleEffectDescriptor& Effect,
    const PaneInputReading&    Input,
    DeferredDropdownSheet&     OutOverlay)
{
    (void)ScrollViewport;
    const float ContentX = LeftX + 18.0f;
    const float ContentW = InnerW - 36.0f;
    const ControlPointer& Pointer = Input.PrimaryPointer;
    float Y        = TopY;
    bool  Modified = false;

    // Section 01: Release Volume & Capacity
    const float Sec1H = 312.0f;
    float Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec1H, "Release volume & slots", "01 / SOURCE", ParticleInspectorColors::WarmGold);

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 7.0f }, 11.2f, ParticleInspectorColors::InkLabel, "Volume shape");
    const Rectangle2D VolRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 30.0f };
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
    Sy += 40.0f;

    Modified |= RenderAxisVectorRow(Surface, ContentX, Sy, ContentW, 200, "Origin (+Z Up)", Effect.OriginMeters, 0.03f, "m", Pointer, ActiveAxisDragId, AxisDragStartX, AxisDragOriginScalar);
    Sy += 58.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 210, "Volume radius", 0.05f, 8.0f, Effect.RadiusMeters, "m", 2, Pointer, ActiveSliderId);
    Sy += 52.0f;

    float CapFloat = static_cast<float>(Effect.Capacity);
    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 211, "Particle capacity", 32.0f, 12000.0f, CapFloat, "slots", 0, Pointer, ActiveSliderId))
    {
        Effect.Capacity = static_cast<uint32_t>(std::lround(CapFloat));
        Modified        = true;
    }
    Sy += 52.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 212, "Continuous birth rate", 0.0f, 2000.0f, Effect.BirthRatePerSec, "/s", 0, Pointer, ActiveSliderId);
    Y += Sec1H + 10.0f;

    // Section 02: Launch Kinematics & Lifetime
    const float Sec2H = 382.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec2H, "Launch velocity & lifetime", "02 / KINEMATICS", ParticleInspectorColors::EmeraldSoft);

    Modified |= RenderAxisVectorRow(Surface, ContentX, Sy, ContentW, 220, "Launch direction", Effect.LaunchDirection, 0.01f, "dir", Pointer, ActiveAxisDragId, AxisDragStartX, AxisDragOriginScalar);
    Sy += 58.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 230, "Cone spread", 0.0f, 3.1416f, Effect.ConeSpreadRad, "rad", 2, Pointer, ActiveSliderId);
    Sy += 52.0f;

    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 231, "Speed minimum", 0.0f, 20.0f, Effect.SpeedMinMs, "m/s", 2, Pointer, ActiveSliderId))
    {
        Effect.SpeedMaxMs = std::max(Effect.SpeedMaxMs, Effect.SpeedMinMs);
        Modified          = true;
    }
    Sy += 52.0f;

    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 232, "Speed maximum", 0.0f, 20.0f, Effect.SpeedMaxMs, "m/s", 2, Pointer, ActiveSliderId))
    {
        Effect.SpeedMinMs = std::min(Effect.SpeedMinMs, Effect.SpeedMaxMs);
        Modified          = true;
    }
    Sy += 52.0f;

    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 233, "Lifetime minimum", 0.05f, 20.0f, Effect.LifeMinSec, "s", 2, Pointer, ActiveSliderId))
    {
        Effect.LifeMaxSec = std::max(Effect.LifeMaxSec, Effect.LifeMinSec);
        Modified          = true;
    }
    Sy += 52.0f;

    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 234, "Lifetime maximum", 0.05f, 20.0f, Effect.LifeMaxSec, "s", 2, Pointer, ActiveSliderId))
    {
        Effect.LifeMinSec = std::min(Effect.LifeMinSec, Effect.LifeMaxSec);
        Modified          = true;
    }
    Y += Sec2H + 10.0f;

    // Section 03: Timed Burst Interval
    const float Sec3H = 194.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec3H, "Timed burst interval", "03 / BURSTS", ParticleInspectorColors::WarmGold);

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 240, "Burst interval (0 = off)", 0.0f, 12.0f, Effect.BurstIntervalSec, "s", 1, Pointer, ActiveSliderId);
    Sy += 52.0f;

    float BurstFloat = static_cast<float>(Effect.BurstCount);
    if (RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 241, "Particles per burst", 1.0f, 600.0f, BurstFloat, "cnt", 0, Pointer, ActiveSliderId))
    {
        Effect.BurstCount = static_cast<uint32_t>(std::lround(BurstFloat));
        Modified          = true;
    }
    Sy += 52.0f;

    Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Release bursts at local origin", Effect.BurstOriginAnchored, Pointer);
    Y += Sec3H + 10.0f;

    if (Modified)
    {
        Session->RebuildEffectSimulation(Effect.UniqueToken);
        Session->IncrementRevision();
    }
    return Y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            TAB 2 — MOTION & FX
//------------------------------------------------------------------------------------------------------------------------

float ParticleInspectorPanel::RenderMotionDynamicsTab(
    PixelSpace&                Surface,
    Rectangle2D                ScrollViewport,
    float                      LeftX,
    float                      TopY,
    float                      InnerW,
    ParticleEffectDescriptor& Effect,
    const PaneInputReading&    Input,
    DeferredDropdownSheet&     OutOverlay)
{
    (void)ScrollViewport;
    const float ContentX = LeftX + 18.0f;
    const float ContentW = InnerW - 36.0f;
    const ControlPointer& Pointer = Input.PrimaryPointer;
    float Y        = TopY;
    bool  Modified = false;

    // Section 01: Particle Appearance, Color Ramp & Sizing
    const float Sec1H = 382.0f;
    float Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec1H, "Particle appearance", "01 / SHADING", ParticleInspectorColors::WarmGold);

    ConstructSpectrumRibbon(Surface, { ContentX, Sy, ContentW, 10.0f }, Effect, false);
    Sy += 20.0f;

    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy + 7.0f }, 11.2f, ParticleInspectorColors::InkLabel, "Silhouette");
    const Rectangle2D SilRect{ ContentX + 94.0f, Sy, ContentW - 94.0f, 30.0f };
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
    Sy += 40.0f;

    const char* BlendLabels[2] = { "Additive glow", "Alpha · premultiplied" };
    uint32_t BlendIdx          = static_cast<uint32_t>(Effect.BlendCategory);
    if (CapsuleButton::ConstructSegmented(Surface, { ContentX, Sy, ContentW, 28.0f }, BlendLabels, 2u, BlendIdx, Pointer, BlendIdx).Clicked)
    {
        Effect.BlendCategory = static_cast<ParticleBlendCategory>(BlendIdx);
        Modified             = true;
    }
    Sy += 38.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 301, "Size at birth", 0.005f, 3.0f, Effect.SizeStartMeters, "m", 3, Pointer, ActiveSliderId);
    Sy += 52.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 302, "Size at death", 0.005f, 3.0f, Effect.SizeEndMeters, "m", 3, Pointer, ActiveSliderId);
    Sy += 52.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 303, "Size multiplier", 0.1f, 4.0f, Effect.SizeMultiplier, "x", 2, Pointer, ActiveSliderId);
    Sy += 56.0f;

    // Color ramp track with circular stop handles (.ramp-track + .ramp-stop)
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ContentX, Sy }, 11.2f, ParticleInspectorColors::InkLabel, "Color ramp stops (A · C · B)");
    Sy += 18.0f;
    ConstructSpectrumRibbon(Surface, { ContentX, Sy, ContentW, 26.0f }, Effect, true);
    Y += Sec1H + 10.0f;

    // Section 02: Physics & Direct C++ ForceFields Coupling
    const float Sec2H = 492.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec2H, "Physics & ForceFields coupling", "02 / FORCEFIELDS", ParticleInspectorColors::EmeraldSoft);

    // Live coupled ForceFieldAccumulator reading at Effect.OriginMeters
    const SpatialForceSample Sample = Session->SampleCoupledForce(Effect.OriginMeters);
    const float AdvSpeed            = VectorLength(Sample.AdvectionVelocity);
    const float AccelMag            = VectorLength(Sample.LinearAcceleration);
    char AdvBuf[48]{};
    char AccBuf[48]{};
    std::snprintf(AdvBuf, sizeof(AdvBuf), "%.2f m/s", AdvSpeed * Effect.ForceCouplingGain);
    std::snprintf(AccBuf, sizeof(AccBuf), "%.2f m/s2", AccelMag * Effect.ForceCouplingGain);

    const float HalfW = (ContentW - 8.0f) * 0.5f;
    RenderMetricTile(Surface, { ContentX, Sy, HalfW, 44.0f }, "COUPLED ADVECTION", AdvBuf, ParticleInspectorColors::EmeraldSoft);
    RenderMetricTile(Surface, { ContentX + HalfW + 8.0f, Sy, HalfW, 44.0f }, "COUPLED ACCEL", AccBuf, ParticleInspectorColors::TealCode);
    Sy += 56.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 310, "ForceFields coupling gain", 0.0f, 3.0f, Effect.ForceCouplingGain, "/s", 2, Pointer, ActiveSliderId);
    Sy += 52.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 311, "Gravity scale", -2.0f, 3.0f, Effect.GravityMultiplier, "g", 2, Pointer, ActiveSliderId);
    Sy += 52.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 312, "Aerodynamic drag", 0.0f, 4.0f, Effect.DragCoefficient, "/s", 2, Pointer, ActiveSliderId);
    Sy += 52.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 313, "Local curl amplitude", 0.0f, 5.0f, Effect.CurlAmplitude, "m/s", 2, Pointer, ActiveSliderId);
    Sy += 52.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 314, "Local curl frequency", 0.1f, 4.0f, Effect.CurlFrequency, "/m", 2, Pointer, ActiveSliderId);
    Sy += 52.0f;

    Modified |= RenderLabeledSliderRow(Surface, ContentX, Sy, ContentW, 315, "Floor restitution", 0.0f, 1.0f, Effect.FloorRestitution, "x", 2, Pointer, ActiveSliderId);
    Sy += 54.0f;

    Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Interactive ball collisions", Effect.BallInteractionEnabled, Pointer);
    Sy += 32.0f;

    Modified |= RenderSwitchRow(Surface, ContentX, Sy, ContentW, "Tapered 3D ribbon trails", Effect.RibbonTrailsEnabled, Pointer);
    Y += Sec2H + 10.0f;

    if (Modified)
    {
        Session->RebuildEffectSimulation(Effect.UniqueToken);
        Session->IncrementRevision();
    }
    return Y;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TAB 3 — SPEC & EXPORT
//------------------------------------------------------------------------------------------------------------------------

float ParticleInspectorPanel::RenderSpecificationExportTab(
    PixelSpace&                      Surface,
    float                            LeftX,
    float                            TopY,
    float                            InnerW,
    const ParticleEffectDescriptor& Effect)
{
    const float ContentX = LeftX + 18.0f;
    const float ContentW = InnerW - 36.0f;
    float Y = TopY;

    // Section 01: Capacity & Coupled ForceFields Budget
    const auto ForceSpan    = Session->QueryForceSources();
    const uint32_t SlotKiB  = std::max(1u, (Effect.Capacity * 80u) / 1024u);
    const float Sec1H       = 126.0f + static_cast<float>(ForceSpan.size()) * 26.0f;
    float Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec1H, "Memory budget & coupled forces", "01 / SPEC", ParticleInspectorColors::EmeraldSoft);

    const float TileGap = 8.0f;
    const float TileW   = (ContentW - TileGap * 2.0f) / 3.0f;
    char MemBuf[24]{};
    char FrcBuf[24]{};
    char SeedBuf[24]{};
    std::snprintf(MemBuf, sizeof(MemBuf), "%u KiB", SlotKiB);
    std::snprintf(FrcBuf, sizeof(FrcBuf), "%u active", Session->QueryForceAccumulator().QueryActiveSourceCount());
    std::snprintf(SeedBuf, sizeof(SeedBuf), "#%u", Effect.DeterministicSeed);

    RenderMetricTile(Surface, { ContentX, Sy, TileW, 46.0f }, "BUFFER SIZE", MemBuf, ParticleInspectorColors::InkPrimary);
    RenderMetricTile(Surface, { ContentX + TileW + TileGap, Sy, TileW, 46.0f }, "FORCE MODELS", FrcBuf, ParticleInspectorColors::EmeraldSoft);
    RenderMetricTile(Surface, { ContentX + (TileW + TileGap) * 2.0f, Sy, TileW, 46.0f }, "RNG SEED", SeedBuf, ParticleInspectorColors::WarmGold);
    Sy += 58.0f;

    for (const ForceSourceRecordEntry& Force : ForceSpan)
    {
        const Rectangle2D RowRect{ ContentX, Sy, ContentW, 22.0f };
        Surface.FillRectangle(RowRect, ParticleInspectorColors::SurfaceDark, 11.0f);
        const ColorRgba Tint = WorkspaceAppearance::QueryForceModelTint(static_cast<uint32_t>(Force.Model));
        Surface.FillCircle({ RowRect.X + 10.0f, RowRect.Y + 11.0f }, 3.5f, Force.Enabled ? Tint : ParticleInspectorColors::InkDim);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { RowRect.X + 20.0f, RowRect.Y + 4.5f },
            RowRect.Width - 108.0f,
            10.5f,
            Force.Enabled ? ParticleInspectorColors::InkHeading : ParticleInspectorColors::InkDim,
            Force.Label);
        char GainBuf[40]{};
        std::snprintf(GainBuf, sizeof(GainBuf), "%.1f m/s · %s", Force.Strength, Force.Enabled ? "ON" : "OFF");
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface,
            FontWeightClass::Regular,
            { RowRect.Right() - 10.0f, RowRect.Y + 4.5f },
            9.8f,
            Force.Enabled ? ParticleInspectorColors::EmeraldSoft : ParticleInspectorColors::InkDim,
            GainBuf,
            true);
        Sy += 26.0f;
    }

    Y += Sec1H + 10.0f;

    // Section 02: Async GPU Reduction Readback (32 B READ) + Staging Summary
    const float SecReadbackH = 122.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, SecReadbackH, "Async GPU reduction readback", "32 B READ", ParticleInspectorColors::WarmGold);

    const float ReadGap = 8.0f;
    const float ReadW   = (ContentW - ReadGap * 2.0f) / 3.0f;
    char AliveReadBuf[24]{};
    char PeakSpdBuf[24]{};
    char BoundsBuf[24]{};
    std::snprintf(AliveReadBuf, sizeof(AliveReadBuf), "%u", Effect.Capacity);
    std::snprintf(PeakSpdBuf, sizeof(PeakSpdBuf), "%.1f m/s", std::max(Effect.SpeedMaxMs, 1.2f));
    std::snprintf(BoundsBuf, sizeof(BoundsBuf), "%.1f m", std::max(2.0f, Effect.RadiusMeters * 14.0f));

    RenderMetricTile(Surface, { ContentX, Sy, ReadW, 46.0f }, "ALIVE", AliveReadBuf, ParticleInspectorColors::InkPrimary);
    RenderMetricTile(Surface, { ContentX + ReadW + ReadGap, Sy, ReadW, 46.0f }, "PEAK SPEED", PeakSpdBuf, ParticleInspectorColors::EmeraldSoft);
    RenderMetricTile(Surface, { ContentX + (ReadW + ReadGap) * 2.0f, Sy, ReadW, 46.0f }, "BOUNDS R", BoundsBuf, ParticleInspectorColors::WarmGold);
    Sy += 56.0f;

    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { ContentX, Sy },
        ContentW,
        9.8f,
        ParticleInspectorColors::InkMuted,
        "Workgroup reduction staging · zero CPU stall on compute dispatch");

    Y += SecReadbackH + 10.0f;

    // Section 03: Live .ParticleSpace TOML Descriptor Preview
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
    const float WellH = std::max(120.0f, static_cast<float>(Lines.size()) * LineH + 20.0f);
    const float Sec2H = WellH + 64.0f;
    Sy = BeginSectionBox(Surface, LeftX, Y, InnerW, Sec2H, ".ParticleSpace TOML preview", "03 / TOML", ParticleInspectorColors::TealCode);

    const Rectangle2D CodeWell{ ContentX, Sy, ContentW, WellH };
    Surface.FillRectangle(CodeWell, ParticleInspectorColors::RecessDark, 14.0f);
    Surface.StrokeRectangle(CodeWell, ParticleInspectorColors::HairlineBorder, 14.0f, 1.0f);

    float LineY = CodeWell.Y + 10.0f;
    for (const std::string& Line : Lines)
    {
        ColorRgba LineInk = ParticleInspectorColors::InkLabel;
        if (!Line.empty() && Line[0] == '[')
        {
            LineInk = ParticleInspectorColors::WarmGold;
        }
        else if (Line.find('=') != std::string::npos)
        {
            LineInk = ParticleInspectorColors::TealCode;
        }
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { CodeWell.X + 12.0f, LineY },
            CodeWell.Width - 24.0f,
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
    ParticleEffectDescriptor&   Effect,
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

        case OpenDropdownCategory::ColorDriver:
            for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(ParticleColorDriver::Count); ++Ord)
            {
                Options.push_back(ParticlePresetRegistry::QueryColorDriverLabel(static_cast<ParticleColorDriver>(Ord)));
            }
            SelectedIndex = static_cast<uint32_t>(Effect.ColorDriver);
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
            case OpenDropdownCategory::ColorDriver:
                Effect.ColorDriver = static_cast<ParticleColorDriver>(ChosenIndex);
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
        Session->RebuildEffectSimulation(Effect.UniqueToken);
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
