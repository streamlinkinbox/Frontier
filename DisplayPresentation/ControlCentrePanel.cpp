//============================================================================================================================================
// 📦 Frontier/DisplayPresentation/ControlCentrePanel.cpp — Organic Top Notch Control Centre Overlay and Stepper Carousel Locomotion
//============================================================================================================================================

#include "ControlCentrePanel.h"
#include <cmath>
#include <algorithm>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

ControlCentrePanel::ControlCentrePanel() noexcept
    : ViewportWidth(1920)
    , ViewportHeight(1080)
    , CurrentOffsetY(0.0f)
    , TargetOffsetY(0.0f)
    , LocomotionVelocity(0.0f)
    , ActivePage(ControlCentrePageCategory::Dashboard)
    , PreviousPage(ControlCentrePageCategory::Dashboard)
    , ActiveAppearanceSubTab(AppearanceSubTabCategory::Theme)
    , ActiveDialogue(DialogueCategory::None)
    , CurrentSlideOffset(0.0f)
    , TargetSlideOffset(0.0f)
    , SlideVelocity(0.0f)
    , PageHistoryStack{}
    , HandlePositionX(760.0f)
    , HandlePositionY(0.0f)
    , DragStartCursorY(0.0f)
    , DragStartOffsetY(0.0f)
    , OpenCondition(false)
    , DraggingCondition(false)
    , SelectedCondition(false)
    , HoveredCondition(false)
    , InitializedCondition(false)
    , ActiveTheme{}
    , HandleContour{}
{
}

bool ControlCentrePanel::Initialize(uint32_t DesiredWidth, uint32_t DesiredHeight) noexcept
{
    ViewportWidth  = std::max(1u, DesiredWidth);
    ViewportHeight = std::max(1u, DesiredHeight);

    CurrentOffsetY     = 0.0f;
    TargetOffsetY      = 0.0f;
    LocomotionVelocity = 0.0f;

    ActivePage         = ControlCentrePageCategory::Dashboard;
    PreviousPage       = ControlCentrePageCategory::Dashboard;
    CurrentSlideOffset = 0.0f;
    TargetSlideOffset  = 0.0f;
    SlideVelocity      = 0.0f;
    PageHistoryStack.clear();

    HandlePositionX = (static_cast<float>(ViewportWidth) - 400.0f) * 0.5f;
    HandlePositionY = 0.0f;

    GenerateHandleContour();

    InitializedCondition = true;
    return true;
}

void ControlCentrePanel::Terminate() noexcept
{
    InitializedCondition = false;
    HandleContour.clear();
    PageHistoryStack.clear();
}

//------------------------------------------------------------------------------------------------------------------------
//                                            STEPPER CAROUSEL NAVIGATION
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::NavigateToPage(ControlCentrePageCategory TargetPage) noexcept
{
    if (ActivePage == TargetPage) return;

    PageHistoryStack.push_back(ActivePage);
    PreviousPage       = ActivePage;
    ActivePage         = TargetPage;

    // Rightward slide carousel transition (+ViewportWidth -> 0)
    CurrentSlideOffset = static_cast<float>(ViewportWidth) * 0.65f;
    TargetSlideOffset  = 0.0f;
    SlideVelocity      = 0.0f;
}

void ControlCentrePanel::NavigateBack() noexcept
{
    if (PageHistoryStack.empty())
    {
        if (ActivePage != ControlCentrePageCategory::Dashboard)
        {
            PreviousPage       = ActivePage;
            ActivePage         = ControlCentrePageCategory::Dashboard;
            CurrentSlideOffset = -static_cast<float>(ViewportWidth) * 0.65f;
            TargetSlideOffset  = 0.0f;
            SlideVelocity      = 0.0f;
        }
        return;
    }

    PreviousPage       = ActivePage;
    ActivePage         = PageHistoryStack.back();
    PageHistoryStack.pop_back();

    // Leftward slide carousel transition (-ViewportWidth -> 0)
    CurrentSlideOffset = -static_cast<float>(ViewportWidth) * 0.65f;
    TargetSlideOffset  = 0.0f;
    SlideVelocity      = 0.0f;
}

bool ControlCentrePanel::IsSlideTransitionActive() const noexcept
{
    return std::abs(CurrentSlideOffset - TargetSlideOffset) > 0.5f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ORGANIC SVG TESSELLATION
//------------------------------------------------------------------------------------------------------------------------

static BezierPointRecord SampleCubicBezier(
    BezierPointRecord P0,
    BezierPointRecord P1,
    BezierPointRecord P2,
    BezierPointRecord P3,
    float ParameterT) noexcept
{
    float InverseT = 1.0f - ParameterT;
    float B0 = InverseT * InverseT * InverseT;
    float B1 = 3.0f * InverseT * InverseT * ParameterT;
    float B2 = 3.0f * InverseT * ParameterT * ParameterT;
    float B3 = ParameterT * ParameterT * ParameterT;

    return BezierPointRecord{
        B0 * P0.X + B1 * P1.X + B2 * P2.X + B3 * P3.X,
        B0 * P0.Y + B1 * P1.Y + B2 * P2.Y + B3 * P3.Y
    };
}

void ControlCentrePanel::GenerateHandleContour() noexcept
{
    HandleContour.clear();
    HandleContour.reserve(128);

    // M 0 0
    HandleContour.push_back(BezierPointRecord{ 0.0f, 0.0f });

    // C 15 0, 20 6, 25 15
    for (int Step = 1; Step <= 12; ++Step)
    {
        float T = static_cast<float>(Step) / 12.0f;
        HandleContour.push_back(SampleCubicBezier(
            BezierPointRecord{ 0.0f, 0.0f },
            BezierPointRecord{ 15.0f, 0.0f },
            BezierPointRecord{ 20.0f, 6.0f },
            BezierPointRecord{ 25.0f, 15.0f },
            T
        ));
    }

    // L 35 28
    HandleContour.push_back(BezierPointRecord{ 35.0f, 28.0f });

    // C 40 34, 45 36, 52 36
    for (int Step = 1; Step <= 12; ++Step)
    {
        float T = static_cast<float>(Step) / 12.0f;
        HandleContour.push_back(SampleCubicBezier(
            BezierPointRecord{ 35.0f, 28.0f },
            BezierPointRecord{ 40.0f, 34.0f },
            BezierPointRecord{ 45.0f, 36.0f },
            BezierPointRecord{ 52.0f, 36.0f },
            T
        ));
    }

    // L 348 36
    HandleContour.push_back(BezierPointRecord{ 348.0f, 36.0f });

    // C 355 36, 360 34, 365 28
    for (int Step = 1; Step <= 12; ++Step)
    {
        float T = static_cast<float>(Step) / 12.0f;
        HandleContour.push_back(SampleCubicBezier(
            BezierPointRecord{ 348.0f, 36.0f },
            BezierPointRecord{ 355.0f, 36.0f },
            BezierPointRecord{ 360.0f, 34.0f },
            BezierPointRecord{ 365.0f, 28.0f },
            T
        ));
    }

    // L 375 15
    HandleContour.push_back(BezierPointRecord{ 375.0f, 15.0f });

    // C 380 6, 385 0, 400 0
    for (int Step = 1; Step <= 12; ++Step)
    {
        float T = static_cast<float>(Step) / 12.0f;
        HandleContour.push_back(SampleCubicBezier(
            BezierPointRecord{ 375.0f, 15.0f },
            BezierPointRecord{ 380.0f, 6.0f },
            BezierPointRecord{ 385.0f, 0.0f },
            BezierPointRecord{ 400.0f, 0.0f },
            T
        ));
    }

    // Z (Return to 0, 0)
    HandleContour.push_back(BezierPointRecord{ 0.0f, 0.0f });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                SPRING LOCOMOTION PHYSICS
//------------------------------------------------------------------------------------------------------------------------

float ControlCentrePanel::EvaluateSpringEase(float ParameterT) const noexcept
{
    float T = std::clamp(ParameterT, 0.0f, 1.0f);
    float InvT = 1.0f - T;
    float P1_y = 0.885f;
    float P2_y = 1.150f;
    return (3.0f * InvT * InvT * T * P1_y) + (3.0f * InvT * T * T * P2_y) + (T * T * T);
}

void ControlCentrePanel::AdvanceLocomotion(float DeltaSeconds) noexcept
{
    if (DeltaSeconds <= 0.0f) return;

    HandlePositionX = (static_cast<float>(ViewportWidth) - 400.0f) * 0.5f;

    // 1. Vertical Pulldown Shade Spring Locomotion
    if (!DraggingCondition)
    {
        constexpr float SpringStiffness = 160.0f;                // [1/s^2] spring constant
        constexpr float DampingRatio    = 20.0f;                 // [1/s] damping ratio

        float Displacement = CurrentOffsetY - TargetOffsetY;
        float SpringForce  = -SpringStiffness * Displacement;
        float DampingForce = -DampingRatio * LocomotionVelocity;
        float Acceleration = SpringForce + DampingForce;

        LocomotionVelocity += Acceleration * DeltaSeconds;
        CurrentOffsetY     += LocomotionVelocity * DeltaSeconds;

        if (std::abs(Displacement) < 0.1f && std::abs(LocomotionVelocity) < 0.1f)
        {
            CurrentOffsetY     = TargetOffsetY;
            LocomotionVelocity = 0.0f;
        }
    }

    HandlePositionY = CurrentOffsetY;

    // 2. Horizontal Carousel Slide Spring Locomotion
    {
        constexpr float SlideStiffness = 200.0f;                // [1/s^2] horizontal carousel spring constant
        constexpr float SlideDamping   = 24.0f;                 // [1/s] horizontal damping ratio

        float SlideDisplacement = CurrentSlideOffset - TargetSlideOffset;
        float SlideSpringForce  = -SlideStiffness * SlideDisplacement;
        float SlideDampForce    = -SlideDamping * SlideVelocity;
        float SlideAccel        = SlideSpringForce + SlideDampForce;

        SlideVelocity      += SlideAccel * DeltaSeconds;
        CurrentSlideOffset += SlideVelocity * DeltaSeconds;

        if (std::abs(SlideDisplacement) < 0.2f && std::abs(SlideVelocity) < 0.2f)
        {
            CurrentSlideOffset = TargetSlideOffset;
            SlideVelocity      = 0.0f;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             INTERACTION & DRAG CONTROLLER
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::OpenNotch() noexcept
{
    OpenCondition = true;
    TargetOffsetY = static_cast<float>(ViewportHeight - 36);
}

void ControlCentrePanel::CloseNotch() noexcept
{
    OpenCondition = false;
    TargetOffsetY = 0.0f;
}

void ControlCentrePanel::ToggleNotch() noexcept
{
    if (OpenCondition)
    {
        CloseNotch();
    }
    else
    {
        OpenNotch();
    }
}

void ControlCentrePanel::AdvanceInteraction(const InputExchange& Input, float CursorX, float CursorY) noexcept
{
    bool InsideX = (CursorX >= HandlePositionX && CursorX <= (HandlePositionX + 400.0f));
    bool InsideY = (CursorY >= HandlePositionY && CursorY <= (HandlePositionY + 36.0f));
    HoveredCondition = (InsideX && InsideY);

    if (Input.IsMouseButtonPressed(MouseButtonCategory::ButtonLeft))
    {
        if (!DraggingCondition && HoveredCondition)
        {
            DraggingCondition = true;
            SelectedCondition = true;
            DragStartCursorY  = CursorY;
            DragStartOffsetY   = CurrentOffsetY;
            LocomotionVelocity = 0.0f;
        }

        if (DraggingCondition)
        {
            float DeltaY = CursorY - DragStartCursorY;
            float MaxY   = static_cast<float>(ViewportHeight - 36);
            CurrentOffsetY = std::clamp(DragStartOffsetY + DeltaY, 0.0f, MaxY);
            TargetOffsetY  = CurrentOffsetY;
            HandlePositionY = CurrentOffsetY;
        }
    }
    else
    {
        if (DraggingCondition)
        {
            DraggingCondition = false;
            float MaxY = static_cast<float>(ViewportHeight - 36);
            float DragDelta = std::abs(CurrentOffsetY - DragStartOffsetY);

            if (DragDelta < 6.0f)
            {
                ToggleNotch();
            }
            else
            {
                if (CurrentOffsetY > (MaxY * 0.35f))
                {
                    OpenNotch();
                }
                else
                {
                    CloseNotch();
                }
            }
        }
    }

    // Content Hit Testing (When fully deployed/open)
    if (OpenCondition && !DraggingCondition && CurrentOffsetY > (static_cast<float>(ViewportHeight) * 0.5f))
    {
        float StageW = 520.0f;
        float StageX0 = (static_cast<float>(ViewportWidth) - StageW) * 0.5f;
        float StageY0 = (CurrentOffsetY - 480.0f) * 0.5f;

        if (Input.IsMouseButtonPressed(MouseButtonCategory::ButtonLeft))
        {
            if (ActivePage == ControlCentrePageCategory::Dashboard)
            {
                // Top header Settings button click: (StageX0 + 470, StageY0 + 10, 32x32)
                if (CursorX >= (StageX0 + 460.0f) && CursorX <= (StageX0 + 510.0f) &&
                    CursorY >= StageY0 && CursorY <= (StageY0 + 40.0f))
                {
                    NavigateToPage(ControlCentrePageCategory::SettingsHub);
                }
            }
            else if (ActivePage == ControlCentrePageCategory::SettingsHub)
            {
                // Back button click: (StageX0, StageY0, 40, 40)
                if (CursorX >= StageX0 && CursorX <= (StageX0 + 50.0f) &&
                    CursorY >= StageY0 && CursorY <= (StageY0 + 40.0f))
                {
                    NavigateBack();
                }

                // 4 Menu Row Clicks:
                float MenuTopY = StageY0 + 60.0f;
                float RowHeight = 64.0f;
                if (CursorX >= StageX0 && CursorX <= (StageX0 + StageW))
                {
                    if (CursorY >= MenuTopY && CursorY < (MenuTopY + RowHeight))
                    {
                        NavigateToPage(ControlCentrePageCategory::Appearance);
                    }
                    else if (CursorY >= (MenuTopY + RowHeight) && CursorY < (MenuTopY + RowHeight * 2))
                    {
                        NavigateToPage(ControlCentrePageCategory::Display);
                    }
                    else if (CursorY >= (MenuTopY + RowHeight * 2) && CursorY < (MenuTopY + RowHeight * 3))
                    {
                        NavigateToPage(ControlCentrePageCategory::Input);
                    }
                    else if (CursorY >= (MenuTopY + RowHeight * 3) && CursorY < (MenuTopY + RowHeight * 4))
                    {
                        NavigateToPage(ControlCentrePageCategory::Notifications);
                    }
                }
            }
            else if (ActivePage == ControlCentrePageCategory::Appearance)
            {
                // Back button click: (StageX0, StageY0, 40, 40)
                if (CursorX >= StageX0 && CursorX <= (StageX0 + 50.0f) &&
                    CursorY >= StageY0 && CursorY <= (StageY0 + 40.0f))
                {
                    NavigateBack();
                }

                // 8 Theme Selection Cards (4 cols x 2 rows)
                float GridX0 = StageX0 + 20.0f;
                float GridY0 = StageY0 + 90.0f;
                float CardW  = (StageW - 40.0f - 36.0f) / 4.0f;
                float CardH  = 70.0f;
                float GapX   = 12.0f;
                float GapY   = 12.0f;

                for (uint32_t Row = 0; Row < 2; ++Row)
                {
                    for (uint32_t Col = 0; Col < 4; ++Col)
                    {
                        float TileX = GridX0 + static_cast<float>(Col) * (CardW + GapX);
                        float TileY = GridY0 + static_cast<float>(Row) * (CardH + GapY);

                        if (CursorX >= TileX && CursorX <= (TileX + CardW) &&
                            CursorY >= TileY && CursorY <= (TileY + CardH))
                        {
                            uint32_t ThemeIdx = Row * 4 + Col;
                            if (ThemeIdx < static_cast<uint32_t>(ThemeCategory::Count))
                            {
                                ActiveTheme.AssignTheme(static_cast<ThemeCategory>(ThemeIdx));
                            }
                        }
                    }
                }

                // Global Rounding Slider: [ number | unit ] [---O---]
                float SliderTrackX0 = StageX0 + 220.0f;
                float SliderTrackX1 = StageX0 + StageW - 30.0f;
                float SliderTrackY0 = StageY0 + 270.0f;
                float SliderTrackY1 = SliderTrackY0 + 32.0f;

                if (CursorX >= SliderTrackX0 && CursorX <= SliderTrackX1 &&
                    CursorY >= SliderTrackY0 && CursorY <= SliderTrackY1)
                {
                    float Normalized = (CursorX - SliderTrackX0) / (SliderTrackX1 - SliderTrackX0);
                    float NewRadius  = std::clamp(Normalized * 32.0f, 0.0f, 32.0f);
                    ActiveTheme.AssignCornerRadius(NewRadius);
                }
            }
            else
            {
                // Sub-Page Back button click
                if (CursorX >= StageX0 && CursorX <= (StageX0 + 50.0f) &&
                    CursorY >= StageY0 && CursorY <= (StageY0 + 40.0f))
                {
                    NavigateBack();
                }
            }
        }
    }
}

} // namespace Frontier
