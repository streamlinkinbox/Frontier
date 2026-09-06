//============================================================================================================================================
//                                                  INTERFACESCREENSEQUENCE.CPP
//============================================================================================================================================

#include "InterfaceScreenSequence.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

namespace {

// Smoothstep. A linear fade reads as mechanical because the eye notices the instant it starts and stops; easing
//    both ends is what makes a transition feel deliberate rather than switched.
[[nodiscard]] float Ease(float T) noexcept
{
    const float X = std::clamp(T, 0.0f, 1.0f);
    return X * X * (3.0f - 2.0f * X);
}

constexpr float kWideOpen = 1.0e9f;

} // namespace

void InterfaceScreenSequence::Construct(uint32_t Ordinal, const std::vector<uint32_t>& Roots,
                                        const TransitionConfiguration& Transition) noexcept
{
    for (ScreenRecord& Existing : Screens)
    {
        if (Existing.Ordinal != Ordinal) continue;
        Existing.Figures    = Roots;              // redeclaring a screen replaces it rather than duplicating
        Existing.Transition = Transition;
        return;
    }

    ScreenRecord Screen;
    Screen.Ordinal    = Ordinal;
    Screen.Figures    = Roots;
    Screen.Transition = Transition;
    Screen.Presence   = 0.0f;
    Screen.Advancing  = false;
    Screens.push_back(Screen);
}

void InterfaceScreenSequence::Present(uint32_t Ordinal) noexcept
{
    Presented = Ordinal;
    for (ScreenRecord& Screen : Screens) Screen.Advancing = (Screen.Ordinal == Ordinal);
}

float InterfaceScreenSequence::QueryPresence(uint32_t Ordinal) const noexcept
{
    for (const ScreenRecord& Screen : Screens)
        if (Screen.Ordinal == Ordinal) return Screen.Presence;
    return 0.0f;
}

bool InterfaceScreenSequence::IsTransitioning() const noexcept
{
    for (const ScreenRecord& Screen : Screens)
    {
        const float Target = Screen.Advancing ? 1.0f : 0.0f;
        if (std::fabs(Screen.Presence - Target) > 1.0e-4f) return true;
    }
    return false;
}

uint32_t InterfaceScreenSequence::QueryInteractiveScreen() const noexcept
{
    // Deliberately strict: only a screen that has fully arrived accepts the pointer. Allowing clicks at 0.9
    //    presence would mean a control can be hit while it is still visibly moving, which feels like a misfire
    //    even when the hit was geometrically correct.
    if (IsTransitioning()) return NoScreen;
    for (const ScreenRecord& Screen : Screens)
        if (Screen.Advancing && Screen.Presence >= 1.0f - 1.0e-4f) return Screen.Ordinal;
    return NoScreen;
}

void InterfaceScreenSequence::ApplyScreen(InterfaceStructure& Structure, ScreenRecord& Screen) noexcept
{
    // Capture the layout's own placement once, before any transition has moved anything.
    if (!Screen.RestCaptured)
    {
        Screen.RestX.assign(Screen.Figures.size(), 0.0f);
        Screen.RestY.assign(Screen.Figures.size(), 0.0f);
        Screen.RestPointerTarget.assign(Screen.Figures.size(), 0u);
        for (size_t Index = 0u; Index < Screen.Figures.size(); ++Index)
        {
            const uint32_t Root = Screen.Figures[Index];
            if (Root >= Structure.QueryCount()) continue;
            Screen.RestX[Index] = Structure.Query(Root).Placement.Origin.X;
            Screen.RestY[Index] = Structure.Query(Root).Placement.Origin.Y;
            Screen.RestPointerTarget[Index] = Structure.Query(Root).PointerTarget ? 1u : 0u;
        }
        Screen.RestCaptured = true;
    }

    const float Eased   = Ease(Screen.Presence);
    const bool  Retired = Screen.Presence <= 1.0e-4f;

    for (size_t Index = 0u; Index < Screen.Figures.size(); ++Index)
    {
        const uint32_t Root = Screen.Figures[Index];
        if (Root >= Structure.QueryCount()) continue;
        InterfaceFigure& Figure = Structure.Access(Root);

        // A fully retired screen is switched off entirely: Visible false skips it and its descendants, which is
        //    cheaper than uploading a transparent instance and guarantees it cannot be picked.
        Figure.Visible = !Retired;
        if (Retired) { Structure.MarkDirty(Root); continue; }

        // Interaction follows presence, so a moving screen cannot be clicked even if a caller forgets to ask.
        Figure.PointerTarget = Screen.RestPointerTarget[Index] != 0u && Screen.Presence >= 1.0f - 1.0e-4f;

        switch (Screen.Transition.Category)
        {
            case TransitionCategory::Immediate:
                Figure.Opacity = 1.0f;
                break;

            case TransitionCategory::Fade:
                Figure.Opacity = Eased;
                break;

            case TransitionCategory::SlideX:
                Figure.Opacity          = Eased;
                Figure.Placement.Origin.X = Screen.RestX[Index] + Screen.Transition.Travel * (1.0f - Eased);
                break;

            case TransitionCategory::SlideY:
                Figure.Opacity          = Eased;
                Figure.Placement.Origin.Y = Screen.RestY[Index] + Screen.Transition.Travel * (1.0f - Eased);
                break;

            case TransitionCategory::Wipe:
            {
                // The clip rectangle is in the figure's own plane and already honoured by the fragment stage, so
                //    a wipe costs nothing beyond writing two floats: sweep the right edge across the figure.
                Figure.Opacity      = 1.0f;
                Figure.ClipMinimumX = -kWideOpen;
                Figure.ClipMaximumX = -Figure.HalfWidth + 2.0f * Figure.HalfWidth * Eased;
                Figure.ClipMinimumY = -kWideOpen;
                Figure.ClipMaximumY =  kWideOpen;
                break;
            }
        }
        Structure.MarkDirty(Root);
    }
}

void InterfaceScreenSequence::AdvanceScreens(InterfaceStructure& Structure, float DeltaSeconds) noexcept
{
    for (ScreenRecord& Screen : Screens)
    {
        const float Target   = Screen.Advancing ? 1.0f : 0.0f;
        const float Duration = std::max(Screen.Transition.Seconds, 1.0e-4f);
        const float Step     = Screen.Transition.Category == TransitionCategory::Immediate
                             ? 1.0f
                             : std::clamp(DeltaSeconds / Duration, 0.0f, 1.0f);

        // Move toward the target and SNAP when within a step, so presence lands exactly on 0 or 1. Easing toward
        //    a limit asymptotically would leave IsTransitioning true for ever and the panel permanently unclickable.
        if (Screen.Presence < Target)      Screen.Presence = std::min(Target, Screen.Presence + Step);
        else if (Screen.Presence > Target) Screen.Presence = std::max(Target, Screen.Presence - Step);

        ApplyScreen(Structure, Screen);
    }
}

} // namespace Frontier
