//============================================================================================================================================
//                                                   INTERFACESCREENSEQUENCE.H
//============================================================================================================================================
// 🧩 P3 — the director (⑤). Groups figures into named screens and moves between them: fade, slide, and a wipe
//    that uses the clip rectangle every figure already carries.
//
//    Named InterfaceScreenSequence, not "…Director": `Director` is not one of the 19 authorized role suffixes,
//    and what this does is a deterministic multi-step ordered execution, which is exactly what `Sequence` means.
//
//    ⚠️ This adds NO field to InterfaceFigure and does not touch the GPU slot. That was the point of reserving
//    Opacity, ClipExtent, ClipRadius and OrderingRank back in P0: a transition is a per-frame write to fields the
//    renderer already uploads, so P3 is a scheduling problem rather than a layout change. The 112-byte
//    InterfaceInstanceFigure contract is untouched, and the batcher still emits one draw.
//
//    Engine ⇄ project seam. This knows a screen is a set of ordinals that can be faded, slid or wiped. It does
//    not know one screen is a menu and another a warning, and it never decides WHEN to switch — the project calls
//    Present. Screens are identified by an ordinal the project assigns meaning to.
//
//    Interaction with P2: a figure whose screen is not fully present is not a pointer target. A control you can
//    click through a half-faded screen is the classic transition bug, and it is prevented here rather than being
//    left for every caller to remember.

#pragma once

#include "InterfaceStructure.h"

#include <cstdint>
#include <vector>

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                                  TRANSITION CATEGORY
//------------------------------------------------------------------------------------------------------------------------

enum class TransitionCategory : uint32_t
{
    Immediate = 0u,   // no animation — the screen simply is, or is not
    Fade      = 1u,   // opacity 0 ⇄ 1
    SlideX    = 2u,   // travels along the panel's local X, fading as it goes
    SlideY    = 3u,   // travels along local Y
    Wipe      = 4u    // clip rectangle opens left→right, revealing the screen in place
};

struct TransitionConfiguration
{
    TransitionCategory Category = TransitionCategory::Fade;
    float              Seconds  = 0.35f;   // [s]  duration of one direction
    float              Travel   = 0.08f;   // [m]  slide distance, in the figures' own plane
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCREEN RECORD
//------------------------------------------------------------------------------------------------------------------------

struct ScreenRecord
{
    uint32_t              Ordinal   = 0u;      // [-]  the project's own identifier
    std::vector<uint32_t> Figures;             // [-]  roots; descendants follow through the structure
    // Rest placement of each root, captured on the first Advance. A slide must offset from where the layout put
    //    the figure, NOT from wherever the previous frame left it, or the screen walks a little further every
    //    frame and never returns.
    std::vector<float>    RestX;               // [m]
    std::vector<float>    RestY;               // [m]
    // Whether the LAYOUT made each root a pointer target. Gating interaction by overwriting the flag destroys it:
    //    `x = x && present` latches false the first time a screen transitions out, so its controls never work
    //    again. The authored value is kept here and re-applied, never derived from the current value.
    std::vector<uint8_t>  RestPointerTarget;   // [-]
    bool                  RestCaptured = false;
    TransitionConfiguration Transition;
    float                 Presence  = 0.0f;    // [-]  0 = fully gone, 1 = fully present
    bool                  Advancing = false;   // [-]  true → heading toward 1, false → toward 0
};

//------------------------------------------------------------------------------------------------------------------------
//                                                INTERFACE SCREEN SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class InterfaceScreenSequence
{
public:
    static constexpr uint32_t NoScreen = 0xFFFFFFFFu;

    // Declares a screen and the figures it owns. Roots only: a figure's descendants inherit its screen through the
    //    structure, so a housing carries its own controls without them being listed.
    void Construct(uint32_t Ordinal, const std::vector<uint32_t>& Roots,
                   const TransitionConfiguration& Transition) noexcept;

    // Begins a move to Ordinal. Any other screen begins retiring. NoScreen retires everything, which is how a
    //    panel is dismissed without a replacement.
    void Present(uint32_t Ordinal) noexcept;

    // Advances every transition and writes the resulting Opacity / Placement / ClipExtent onto the figures.
    //    Call once per frame, before the composition runs, or a screen renders one frame stale.
    void AdvanceScreens(InterfaceStructure& Structure, float DeltaSeconds) noexcept;

    // True while any screen is mid-transition. A project can hold input off until this clears.
    [[nodiscard]] bool IsTransitioning() const noexcept;

    // The screen a pointer may interact with: the one that is fully present. During a transition this is NoScreen,
    //    so nothing is clickable while it moves.
    [[nodiscard]] uint32_t QueryInteractiveScreen() const noexcept;

    [[nodiscard]] uint32_t QueryPresentedScreen() const noexcept { return Presented; }
    [[nodiscard]] float    QueryPresence(uint32_t Ordinal) const noexcept;
    [[nodiscard]] uint32_t QueryScreenCount() const noexcept { return static_cast<uint32_t>(Screens.size()); }

private:
    void ApplyScreen(InterfaceStructure& Structure, ScreenRecord& Screen) noexcept;

    std::vector<ScreenRecord> Screens;
    uint32_t                  Presented = NoScreen;
};

} // namespace Frontier
